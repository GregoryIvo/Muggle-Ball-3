// Scripted tests of the menu runtime and the game flow: synthetic pointer events and keys go into Menu and GameFlow,
// and what comes out is checked (hover frames, sounds, navigation, settings, high scores, loading). The forms are
// written here in the format tools/ui.py produces, so the tests need neither a GPU nor the game data.

#include "mb/game/Campaign.h"
#include "mb/game/GameFlow.h"
#include "mb/game/Menu.h"
#include "mb/game/SaveData.h"
#include "mb/game/StringTable.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using nlohmann::json;
using namespace mb;
namespace fs = std::filesystem;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// ---------------------------------------------------------------------------
// Forms

json element(const char* tag, json attributes, json children = json::array()) {
    return {{"tag", tag}, {"attributes", std::move(attributes)}, {"children", std::move(children)}};
}

// Children written as a braced list. Without this overload a one-element list such as {button(...)} is, for
// some compilers (Apple clang 15), a copy of that element rather than an array holding it.
json element(const char* tag, json attributes, std::initializer_list<json> children) {
    return element(tag, std::move(attributes), json(json::array_t(children.begin(), children.end())));
}

json form(const char* name, json children, json extra = json::object());

json form(const char* name, std::initializer_list<json> children, json extra = json::object()) {
    return form(name, json(json::array_t(children.begin(), children.end())), std::move(extra));
}

json form(const char* name, json children, json extra) {
    json attributes = {{"name", name}, {"fade_time_msec", 400}};
    attributes.update(extra);
    return {{"version", 1}, {"tag", "forms"}, {"attributes", json::object()}, {"children", json::array({element("form", attributes, std::move(children))})}};
}

// A rectangle on a 10 x 10 grid of the screen, y counted up from the bottom like the data: cell (column, row).
json cell(int column, int row, int columns = 1, int rows = 1) {
    return {{"pos", {column * 0.1, row * 0.1}}, {"size", {columns * 0.1, rows * 0.1}}};
}

json withCell(json attributes, int column, int row, int columns = 1, int rows = 1) {
    attributes.update(cell(column, row, columns, rows));
    return attributes;
}

json button(const char* name, int column, int row, const char* userData = "", json extra = json::object()) {
    json attributes = {{"name", name}, {"img_name", "forms/button.png"}, {"img_params", {1, 2, 0, 1}},
                       {"sound_mouse_in", "in"}, {"sound_mouse_down", "down"}};
    if (*userData) {
        attributes["user_data"] = userData;
    }
    attributes.update(cell(column, row, 2, 1));
    attributes.update(extra);  // a size given there wins over the cell's
    return element("button", attributes);
}

json label(const char* name, int column, int row, const char* text = "", int columns = 1, int rows = 1) {
    json attributes = {{"name", name}, {"text", text}};
    return element("label", withCell(attributes, column, row, columns, rows));
}

json bat(const char* name, int column, bool disabled = false) {
    json extra = {{"img_params", {2, 3, 0, 0, 3, 3}}, {"size", {0.1, 0.1}}};
    if (disabled) {
        extra["disabled"] = true;
    }
    return button(name, column, 8, "SelectBat", extra);
}

json level(int index, int column, int row) {
    return button(std::to_string(index).c_str(), column, row, "SelectLevel");
}

json checkbox(const char* name, int row) {
    return element("checkbox", withCell({{"name", name}, {"img_name", "forms/checkbox.png"}, {"img_params", {2, 1, 1, 0}},
                                         {"sound_mouse_in", "in"}, {"sound_mouse_down", "down"}},
                                        8, row, 1, 1));
}

json slider(const char* name, int row) {
    json attributes = {{"name", name}, {"img_name", "forms/scull.png"}, {"params", {0, 100, 5}}, {"size_pos", {0.03, 0.06}}};
    attributes["pos"] = {0.3, row * 0.1};
    attributes["size"] = {0.3, 0.06};
    json children = json::array({button("-", 1, row, "", {{"size", {0.05, 0.06}}}), button("+", 7, row, "", {{"size", {0.05, 0.06}}})});
    children[0]["attributes"]["pos"] = {0.2, row * 0.1};
    children[1]["attributes"]["pos"] = {0.7, row * 0.1};
    return element("slider", attributes, children);
}

std::map<std::string, std::string> formFiles() {
    std::map<std::string, std::string> files;
    const auto add = [&](const char* name, json doc) { files[name] = doc.dump(); };

    add("background", form("bg", json::array()));
    add("MainMenu", form("MainMenu",
                         {button("SelectStage", 0, 8, "Navigate"), button("Options", 2, 8, "Navigate"), button("HighScores", 4, 8, "Navigate"),
                          button("Info", 6, 8, "Navigate"), button("Exit", 0, 6)}));
    add("Info", form("Info", {button("MainMenu", 0, 0, "Navigate")}));
    add("selectstage", form("SelectLevel", {button("StagePiratos", 0, 5, "Navigate"), button("StageFarm", 2, 5, "Navigate"),
                                             bat("Metall-Raketka-Red.jpg", 5, true), bat("Metall-Raketka-Blue.jpg", 6), bat("Metall-Raketka-Yellow.jpg", 7),
                                             button("QuickStart", 0, 3), button("MainMenu", 0, 0, "Navigate")}));
    add("stagepiratos", form("SelectLevel", {level(0, 0, 5), level(1, 2, 5), level(2, 4, 5), bat("Metall-Raketka-Red.jpg", 5, true),
                                              bat("Metall-Raketka-Blue.jpg", 6), button("QuickStart", 0, 3), button("SelectStage", 0, 0, "Navigate"),
                                              button("Logo", 8, 3)}));
    add("stagefarm", form("SelectLevel", {level(3, 0, 5), level(4, 2, 5), button("QuickStart", 0, 3), button("SelectStage", 0, 0, "Navigate")}));
    add("Options", form("Options",
                        {element("list", {{"name", "VideoMode"}, {"text", "%s"}, {"cycled", false}},
                                 {button("-", 0, 4, "", {{"size", {0.05, 0.06}}}), label("Video", 1, 4, "<empty>", 2, 1), button("+", 3, 4, "", {{"size", {0.05, 0.06}}})}),
                         slider("MusicVolume", 2), slider("SoundVolume", 1), checkbox("BG", 7), checkbox("Camera", 6), checkbox("Shadow", 5),
                         checkbox("Mirror", 4),
                         // The misspelt tag, as on the real Options form: a checkbox all the same.
                         element("La1bel", withCell({{"name", "Fullscreen"}, {"img_name", "forms/checkbox.png"}, {"img_params", {2, 1, 1, 0}}}, 8, 3)),
                         button("ApplyVideo", 4, 0, "ApplyVideo")}));
    add("highscores", form("HighScores", {label("MapSmall", 3, 2, "", 5, 5), label("Caption", 5, 8, "High Scores"), label("LeftCaption", 1, 7, "Online :"),
                                           element("label", withCell({{"name", "PleaseWait"}, {"text", "Wait"}}, 5, 4), {label("PleaseWaitA", 5, 3, "please")}),
                                           button("LocalScores", 0, 6), button("86400", 0, 4, "OnlineScore"), button("2073600", 0, 3, "OnlineScore"),
                                           element("button", withCell({{"name", "PrevPage"}, {"img_name", "forms/left.png"}, {"img_params", {1, 1, 0}}}, 6, 1)),
                                           element("button", withCell({{"name", "NextPage"}, {"img_name", "forms/right.png"}, {"img_params", {1, 1, 0}}}, 7, 1)),
                                           button("MainMenu", 4, 0, "Navigate")}));
    add("EnterName", form("EnterName", {label("Score", 4, 6, "1000"),
                                         element("textedit", withCell({{"name", "Name"}, {"img_name", "forms/field.png"}}, 4, 4, 2, 1)),
                                         button("ApplyRecord", 4, 1, "ApplyRecord")}));
    add("GameOver", form("GameOver", {label("Score", 4, 6, "1000"), button("MainMenu", 4, 1, "Navigate")}));
    add("endgame", form("Story", {button("GameOver", 4, 1, "FinishGame")}));
    add("Loading", form("Loading", {element("progress", withCell({{"name", "Progress"}, {"img_name", "progress.png"}, {"params", {0, 10, 1, 0.3}}}, 1, 1, 8, 1))},
                        {{"fade_time_msec", 0}, {"show_time_msec", 300}}));
    return files;
}

StringTable strings() {
    return StringTable::parse(R"({"languages":[{"name":"English","strings":{"FontName":"face.ttf"}}]})");
}

Campaign campaign() {
    return Campaign::parse(R"({"version":1,"stages":[
        {"name":"Pirates","levels":["piratos-a","piratos-b","Piratos-C"]},
        {"name":"Village","levels":["village-d","village-e"]}]})");
}

// A fresh folder that is removed again: the menus' save data lives in it.
class TempDir {
public:
    TempDir() : path(fs::temp_directory_path() / ("mb3-menu-test-" + std::to_string(std::random_device{}()))) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    fs::path path;
};

// A Menu on a 1024x768 target with the forms above, over save data in a temporary folder, recording what it asks of
// the program. `prepare` sets the save data up before the Menu shows anything. The stage screens start with every
// level open, which most tests want; testLocking turns that off to see the Menu as it is made.
struct Harness {
    explicit Harness(const std::function<void(SaveData&)>& prepare = {}, bool openAllLevels = true)
        : files(formFiles()), table(strings()), save(dir.path), menu(loader(), campaign(), save, hooks()) {
        if (prepare) {
            prepare(save);
        }
        menu.setVideoModes({{800, 600}, {1024, 768}, {1280, 960}});
        if (openAllLevels) {
            menu.setLevelsLocked(false);
        }
        menu.open("MainMenu");
    }

    Menu::FormLoader loader() {
        return [this](std::string_view name) {
            for (const auto& [file, text] : files) {
                if (file.size() == name.size() && std::equal(file.begin(), file.end(), name.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
                    return UiForm::parse(text, table);
                }
            }
            throw std::runtime_error("no form " + std::string(name));
        };
    }

    MenuHooks hooks() {
        MenuHooks h;
        h.playSound = [this](std::string_view s) { sounds.emplace_back(s); };
        h.settingsChanged = [this](const Settings& s) { changes.push_back(s); };
        h.applyVideo = [this](const Settings& s) { applied.push_back(s); };
        return h;
    }

    MenuResult frame(const UiInput& input = {}, double seconds = 1.0) { return menu.update(input, seconds, layout, sizes); }

    // What the files in the folder say now, as a later run of the game would read it.
    SaveData reloaded() const { return SaveData(dir.path); }
    bool wrote(const char* file) const { return fs::exists(dir.path / file); }

    const UiWidget& widget(const char* name) const {
        const UiWidget* w = menu.form().find(name);
        if (!w) throw std::runtime_error(std::string("no widget ") + name);
        return *w;
    }
    // The '-' or '+' button of the slider or list with this name.
    const UiWidget& stepper(const char* owner, const char* which) const {
        const UiForm& f = menu.form();
        const auto base = static_cast<int>(&widget(owner) - f.widgets().data());
        for (const UiWidget& w : f.widgets()) {
            if (w.parent == base && w.name == which) return w;
        }
        throw std::runtime_error("no stepper");
    }
    Vec2 center(const UiWidget& w) const { return w.rect.center(); }
    Vec2 center(const char* name) const { return center(widget(name)); }

    UiInput move(Vec2 to, bool down = false) const {
        UiInput in;
        in.pointer.push_back({to, down});
        return in;
    }
    // A whole click in one frame: the pointer arrives, the button goes down and comes up.
    MenuResult click(Vec2 at) {
        UiInput in;
        in.pointer = {{at, false}, {at, true}, {at, false}};
        return frame(in, 0);
    }
    MenuResult click(const char* name) { return click(center(name)); }
    MenuResult click(const UiWidget& w) { return click(center(w)); }
    // Presses the track of a slider at this percentage of its length.
    void dragTo(const char* slider, float percent) {
        const UiWidget& s = widget(slider);
        const float x = s.rect.x + s.knobSize.x / 2 + s.rect.w * percent / 100.0f;
        frame(move({x, s.rect.center().y}, false), 0);
        frame(move({x, s.rect.center().y}, true), 0);
        frame(move({x, s.rect.center().y}, false), 0);
    }
    // Drops the log of sounds and changes.
    void forget() {
        sounds.clear();
        changes.clear();
    }
    int count(const std::string& sound) const { return static_cast<int>(std::count(sounds.begin(), sounds.end(), sound)); }

    TempDir dir;
    std::map<std::string, std::string> files;
    StringTable table;
    UiLayout layout = UiLayout::fit(1024, 768);
    ImageSizeFn sizes = [](std::string_view) { return Vec2{}; };
    std::vector<std::string> sounds;
    std::vector<Settings> changes, applied;
    SaveData save;
    Menu menu;
};

bool near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) <= tolerance;
}

// ---------------------------------------------------------------------------
// Tests

void testHoverAndSounds() {
    Harness h;
    const UiWidget& start = h.widget("SelectStage");
    check(start.state() == UiState::Normal && start.frame() == 0, "hover: a button at rest shows its first frame");
    h.frame(h.move(h.center(start)));
    check(start.hovered && start.state() == UiState::Hover && start.frame() == 1, "hover: the pointer on a button shows its second frame");
    check(h.sounds == std::vector<std::string>{"in"}, "hover: the mouse-in sound plays once on entering");
    const Vec2 nearby{h.center(start).x + 3, h.center(start).y + 2};
    h.frame(h.move(nearby));
    h.frame();
    h.frame();
    check(h.sounds.size() == 1, "hover: staying on the button plays nothing more, over any number of frames");
    h.frame(h.move({1, 1}));
    check(!start.hovered && start.frame() == 0 && h.sounds.size() == 1, "hover: leaving shows the first frame again, silently");
    h.frame(h.move(h.center(start)));
    check(h.sounds.size() == 2, "hover: entering again plays the sound again");

    h.forget();
    h.frame(h.move(h.center(start), true));
    check(start.pressed && start.state() == UiState::Pressed && h.sounds == std::vector<std::string>{"down"},
          "press: the button going down plays the mouse-down sound once");
    h.frame(h.move(h.center(start), true));
    check(h.sounds.size() == 1, "press: holding it plays nothing more");

    h.frame(h.move({-1, -1}));
    check(!start.hovered, "hover: a pointer at (-1, -1), off the window, hovers nothing");
}

void testNavigation() {
    Harness h;
    check(h.menu.formName() == "mainmenu", "navigate: the menu starts where it was opened");
    check(h.click("SelectStage").kind == MenuResult::Kind::None && h.menu.formName() == "selectstage",
          "navigate: a click on a Navigate button opens the form named like it, ignoring case");
    check(h.click("StagePiratos").kind == MenuResult::Kind::None && h.menu.formName() == "stagepiratos", "navigate: on to a stage");
    h.click("SelectStage");
    check(h.menu.formName() == "selectstage", "navigate: a stage's Select Stage button goes back");
    h.click("MainMenu");
    check(h.menu.formName() == "mainmenu", "navigate: the Main Menu button");

    // Press and release on different buttons clicks neither.
    UiInput in;
    in.pointer = {{h.center("Options"), false}, {h.center("Options"), true}, {h.center("Info"), true}, {h.center("Info"), false}};
    h.frame(in, 0);
    check(h.menu.formName() == "mainmenu", "navigate: releasing over another button clicks nothing");

    // A click and a press in the same frame at different places: each lands where it was made.
    in = {};
    in.pointer = {{h.center("Info"), false}, {h.center("Info"), true}, {h.center("Info"), false}};
    h.frame(in, 0);
    check(h.menu.formName() == "info", "navigate: a press and release inside one frame is a click");

    // A button held down through a navigation presses nothing on the new form.
    h.frame(h.move(h.center("MainMenu"), true), 0);
    check(h.menu.formName() == "info" && h.widget("MainMenu").pressed, "navigate: pressing a button");
    h.menu.open("MainMenu");
    h.frame();
    check(!h.widget("SelectStage").pressed && !h.widget("Info").pressed, "navigate: a button held through a change presses nothing");
    h.frame(h.move(h.center("Info"), true), 0);
    check(!h.widget("Info").pressed, "navigate: ... until it is let go");
    h.frame(h.move(h.center("Info"), false), 0);
    h.frame(h.move(h.center("Info"), true), 0);
    check(h.widget("Info").pressed, "navigate: ... and then pressing works again");
    h.frame(h.move(h.center("Info"), false), 0);

    // The form under the pointer is hovered as soon as it opens (its sound plays).
    h.menu.open("selectstage");
    h.frame(h.move(h.center("QuickStart")), 0);
    h.forget();
    h.menu.open("stagepiratos");
    h.frame();
    check(h.widget("QuickStart").hovered && h.count("in") == 1, "navigate: a widget under the pointer of a new form is hovered at once, with its sound");

    bool threw = false;
    try {
        h.menu.open("nosuchform");
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw && h.menu.formName() == "stagepiratos", "navigate: opening a form that is not there throws and changes nothing");
}

void testEscape() {
    Harness h;
    UiInput esc;
    esc.keys = {UiKey::Escape};
    h.frame(esc);
    check(h.menu.formName() == "mainmenu", "escape: the main menu has nowhere to go back to");
    h.menu.open("stagepiratos");
    h.frame(esc);
    check(h.menu.formName() == "selectstage", "escape: from a stage back to the stage selection");
    h.frame(esc);
    check(h.menu.formName() == "mainmenu", "escape: from the stage selection to the main menu");
    h.menu.open("Info");
    h.frame(esc);
    check(h.menu.formName() == "mainmenu", "escape: from Info to the main menu");
    h.menu.open("highscores");
    h.frame(esc);
    check(h.menu.formName() == "mainmenu", "escape: from the high scores to the main menu");
    h.menu.open("endgame");
    h.frame(esc);
    check(h.menu.formName() == "endgame", "escape: the story cannot be skipped");
    h.menu.open("EnterName");
    h.frame(esc);
    check(h.menu.formName() == "entername", "escape: a high score cannot be skipped");
    h.menu.open("Loading");
    h.frame(esc);
    check(h.menu.formName() == "loading", "escape: nor a loading screen");
}

void testQuitAndStart() {
    Harness h;
    check(h.click("Exit").kind == MenuResult::Kind::Quit, "exit: the Exit button asks to quit");

    h.menu.open("selectstage");
    MenuResult r = h.click("QuickStart");
    check(r.kind == MenuResult::Kind::StartLevel && r.level == 0, "quick start: on the stage selection, the first level of the game");
    h.menu.open("stagefarm");
    r = h.click("QuickStart");
    check(r.kind == MenuResult::Kind::StartLevel && r.level == 3, "quick start: on a stage, its first level");

    h.menu.open("stagepiratos");
    r = h.click("1");
    check(r.kind == MenuResult::Kind::StartLevel && r.level == 1, "level: a level button starts the level numbered like it");
    h.menu.open("stagefarm");
    r = h.click("4");
    check(r.kind == MenuResult::Kind::StartLevel && r.level == 4, "level: numbers run on across stages");

    h.menu.open("stagepiratos");
    check(h.click("Logo").kind == MenuResult::Kind::None && h.menu.formName() == "stagepiratos", "level: the stage emblem only plays its sound");
}

void testLocking() {
    // The stage screens open the levels the saved progress opened: the first of each stage, and what finishing levels opened.
    const auto progress = [](SaveData& save) { save.progress.recordCompletion("Pirates", 0, "piratos-a", 100); };
    Harness h(progress, false);
    h.menu.open("stagepiratos");
    check(h.widget("0").enabled && h.widget("1").enabled && !h.widget("2").enabled,
          "locks: the levels from the first one the progress has not opened on are disabled");
    check(h.click("2").kind == MenuResult::Kind::None, "locks: a locked level cannot be started");
    check(h.click("1").kind == MenuResult::Kind::StartLevel, "locks: an unlocked one can");
    h.menu.open("stagefarm");
    check(h.widget("3").enabled && !h.widget("4").enabled, "locks: each stage has its own progress, and its first level is open from the start");
    h.menu.open("selectstage");
    check(h.widget("StagePiratos").enabled && h.widget("StageFarm").enabled, "locks: so every stage can be opened");

    h.save.progress.recordCompletion("village", 0, "village-d", 50);
    h.menu.open("stagefarm");
    check(h.widget("4").enabled, "locks: finishing a level opens the next when the stage is shown again");
    h.save.progress.recordCompletion("pirates", 2, "Piratos-C", 70);
    h.menu.open("stagepiratos");
    check(h.widget("2").enabled, "locks: the stage's name in any case is its key in the progress");
    h.menu.open("selectstage");
    check(h.click("QuickStart").level == 0, "locks: Quick Start still starts the first level");

    Harness fresh({}, false);
    fresh.menu.open("stagepiratos");
    check(fresh.widget("0").enabled && !fresh.widget("1").enabled && !fresh.widget("2").enabled,
          "locks: a new player has the first level of a stage only, unless the Menu is told to leave them open");

    Harness open;  // setLevelsLocked(false)
    open.menu.open("stagepiratos");
    check(open.widget("0").enabled && open.widget("2").enabled, "locks: with the locks off every level is open");
    open.menu.open("stagefarm");
    check(open.widget("3").enabled && open.widget("4").enabled, "locks: ... in every stage");
}

void testBats() {
    Harness h;
    h.menu.open("selectstage");
    check(!h.widget("Metall-Raketka-Red.jpg").enabled && h.widget("Metall-Raketka-Blue.jpg").enabled, "bat: the red bat starts chosen (its button disabled)");
    h.forget();
    h.click("Metall-Raketka-Blue.jpg");
    check(h.menu.settings().bat == 1 && h.widget("Metall-Raketka-Blue.jpg").enabled == false && h.widget("Metall-Raketka-Red.jpg").enabled,
          "bat: clicking another chooses it and releases the old one");
    check(h.changes.size() == 1 && h.changes[0].bat == 1 && h.reloaded().settings.bat == 1, "bat: the choice is reported and saved");
    h.menu.open("stagepiratos");
    check(!h.widget("Metall-Raketka-Blue.jpg").enabled && h.widget("Metall-Raketka-Red.jpg").enabled, "bat: the stage forms show the same choice");

    Harness again([](SaveData& save) { save.settings.bat = 2; });
    again.menu.open("selectstage");
    check(!again.widget("Metall-Raketka-Yellow.jpg").enabled && again.widget("Metall-Raketka-Red.jpg").enabled, "bat: a saved choice is shown when the form opens");
}

// Whether the progress or the high scores were written: only the part that changed should be.
bool wroteOthers(const Harness& h) {
    return h.wrote("progress.json") || h.wrote("highscores.json");
}

void testOptions() {
    Harness h([](SaveData& save) {
        save.settings.musicVolume = 0.4f;
        save.settings.freezeSky = true;
        save.settings.smartCamera = true;
        save.settings.mirrors = false;
    });
    h.menu.open("Options");
    check(near(h.widget("MusicVolume").value, 40) && near(h.widget("SoundVolume").value, 80),
          "options: the sliders show the settings' volumes (0 to 1) as percentages, and the defaults are the save data's");
    check(h.widget("BG").checked && h.widget("Camera").checked && h.widget("Shadow").checked && !h.widget("Mirror").checked,
          "options: the checkboxes show the settings");
    check(h.widget("Video").text == "1024 x 768", "options: the video list shows the current window size");

    // A checkbox toggles on a click, with its frame; the hook hears of it.
    h.forget();
    const UiWidget& sky = h.widget("BG");
    check(sky.frame() == 0, "checkbox: a checked box shows the second frame listed");
    h.click(sky);
    check(!sky.checked && sky.frame() == 1 && !h.menu.settings().freezeSky && h.changes.size() == 1 && !h.changes[0].freezeSky,
          "checkbox: a click unchecks it, the setting follows, the hook is called");
    h.click(sky);
    check(sky.checked && h.menu.settings().freezeSky && h.changes.size() == 2, "checkbox: and checks it again");
    h.click("Camera");
    h.click("Shadow");
    h.click("Mirror");
    h.click("Fullscreen");
    const Settings& s = h.menu.settings();
    check(!s.smartCamera && !s.shadows && s.mirrors && s.fullscreen, "checkbox: each box is its own setting (and Fullscreen is a checkbox too)");
    check(h.count("down") == 5 && h.count("in") == 4, "checkbox: each click played the box's down sound, and entering it the in sound");

    // The sliders: a press jumps to the pointer, dragging follows it, even past the ends; the value snaps to 5.
    h.forget();
    const UiWidget& music = h.widget("MusicVolume");
    const float left = music.rect.x + music.knobSize.x / 2, width = music.rect.w;
    const float y = music.rect.center().y;
    h.frame(h.move({left + width * 0.5f, y}, false), 0);
    h.frame(h.move({left + width * 0.5f, y}, true), 0);
    check(near(music.value, 50) && near(h.menu.settings().musicVolume, 0.5f) && h.changes.size() == 1 && near(h.changes[0].musicVolume, 0.5f),
          "slider: pressing on the track moves the knob there, and the volume (0 to 1) follows");
    h.frame(h.move({left + width * 0.77f, y + 200}, true), 0);
    check(near(music.value, 75) && near(h.menu.settings().musicVolume, 0.75f), "slider: dragging follows the pointer, wherever it is vertically");
    h.frame(h.move({left + width * 3, y}, true), 0);
    check(near(music.value, 100), "slider: dragging past the end clamps");
    h.frame(h.move({left - width, y}, true), 0);
    check(near(music.value, 0) && h.menu.settings().musicVolume == 0, "slider: ... at both ends");
    h.frame(h.move({left + width * 0.3f, y}, false), 0);
    h.frame(h.move({left + width * 0.9f, y}, false), 0);
    check(near(music.value, 0), "slider: after the release the knob stays where it was");
    check(h.count("down") == 0, "slider: pressing and dragging the track makes no mouse-down sound (it has none)");

    // The - and + buttons beside a slider step it.
    h.forget();
    h.click(h.stepper("MusicVolume", "+"));
    check(near(music.value, 5) && near(h.menu.settings().musicVolume, 0.05f), "slider: + steps up by the slider's step");
    h.click(h.stepper("MusicVolume", "+"));
    h.click(h.stepper("MusicVolume", "-"));
    check(near(music.value, 5), "slider: - steps down");
    h.click(h.stepper("MusicVolume", "-"));
    h.click(h.stepper("MusicVolume", "-"));
    check(near(music.value, 0) && h.menu.settings().musicVolume == 0, "slider: stepping stops at the minimum");
    const std::size_t calls = h.changes.size();
    h.click(h.stepper("MusicVolume", "-"));
    check(h.changes.size() == calls, "slider: and a step that changes nothing reports nothing");
    check(near(h.menu.settings().soundVolume, 0.8f) && near(h.widget("SoundVolume").value, 80), "slider: the sound volume is its own");

    // The video list.
    check(h.widget("Video").text == "1024 x 768", "video: the current mode");
    h.click(h.stepper("VideoMode", "+"));
    check(h.widget("Video").text == "1280 x 960", "video: + shows the next mode");
    h.click(h.stepper("VideoMode", "+"));
    check(h.widget("Video").text == "1280 x 960", "video: a list that does not cycle stops at its end");
    h.click(h.stepper("VideoMode", "-"));
    h.click(h.stepper("VideoMode", "-"));
    h.click(h.stepper("VideoMode", "-"));
    check(h.widget("Video").text == "800 x 600", "video: - shows the previous one, down to the first");
    h.click(h.stepper("VideoMode", "+"));
    check(h.applied.empty() && h.menu.settings().windowWidth == 1024, "video: choosing a mode applies nothing yet");

    // The Main Menu button applies and saves the settings and goes back.
    check(!h.wrote("settings.json"), "apply: nothing was written while the settings were being changed");
    h.click("ApplyVideo");
    check(h.menu.formName() == "mainmenu", "apply: the Main Menu button leaves Options");
    check(h.applied.size() == 1 && h.applied[0].windowWidth == 1024 && h.applied[0].windowHeight == 768 && h.applied[0].fullscreen,
          "apply: the video hook gets the chosen window size and fullscreen");
    const SaveData stored = h.reloaded();
    check(stored.settings.windowWidth == 1024 && stored.settings.musicVolume == 0 && stored.settings.fullscreen && !stored.settings.shadows &&
              !wroteOthers(h),
          "apply: and the settings, and only the settings, are saved");

    // Escape does the same.
    h.menu.open("Options");
    h.click(h.stepper("VideoMode", "+"));
    UiInput esc;
    esc.keys = {UiKey::Escape};
    h.frame(esc);
    check(h.menu.formName() == "mainmenu" && h.applied.size() == 2 && h.applied[1].windowWidth == 1280, "apply: Escape leaves Options the same way");

    // Leaving without touching the video settings applies nothing (the player may have resized the window since), but still saves.
    const bool cameraBefore = h.reloaded().settings.smartCamera;
    h.menu.open("Options");
    h.click(h.widget("Camera"));
    h.click("ApplyVideo");
    check(h.applied.size() == 2 && h.reloaded().settings.smartCamera != cameraBefore,
          "apply: with no video change there is nothing to apply, and the settings are saved anyway");
    h.menu.open("Options");
    h.click(h.stepper("VideoMode", "-"));
    h.click(h.stepper("VideoMode", "+"));
    h.click("ApplyVideo");
    check(h.applied.size() == 2, "apply: a change that was changed back is no change");

    // Without modes to choose from the list is hidden.
    Harness none;
    none.menu.setVideoModes({});
    none.menu.open("Options");
    check(!none.widget("VideoMode").visible, "video: with no modes the list is hidden");

    // A window size that is not in the list shows the nearest mode.
    Harness near1([](SaveData& save) {
        save.settings.windowWidth = 1180;
        save.settings.windowHeight = 900;
    });
    near1.menu.open("Options");
    check(near1.widget("Video").text == "1280 x 960", "video: an unlisted size shows the nearest mode");

    // A cycling list wraps.
    Harness wrap;
    json options = json::parse(wrap.files["Options"]);
    options["children"][0]["children"][0]["attributes"]["cycled"] = true;
    wrap.files["Options"] = options.dump();
    wrap.menu.open("Options");
    wrap.click(wrap.stepper("VideoMode", "+"));
    wrap.click(wrap.stepper("VideoMode", "+"));
    check(wrap.widget("Video").text == "800 x 600", "video: a list with cycled=\"yes\" wraps past its end");
}

// The sliders count 0 to 100 in steps of 5; the save data holds volumes from 0 to 1.
void testVolumeConversion() {
    Harness odd([](SaveData& save) {
        save.settings.musicVolume = 0.333f;
        save.settings.soundVolume = 1.0f;
    });
    odd.menu.open("Options");
    check(near(odd.widget("MusicVolume").value, 33) && near(odd.widget("SoundVolume").value, 100),
          "volume: a volume between two percents shows at the nearest, and 1 is the slider's end");
    odd.click(odd.stepper("MusicVolume", "+"));
    check(near(odd.menu.settings().musicVolume, 0.38f) && near(odd.widget("MusicVolume").value, 38), "volume: a step goes on from there");

    // Every position of the sliders comes back as it was from a save and a load.
    Harness h;
    bool allBack = true;
    for (int percent = 0; percent <= 100; percent += 5) {
        h.menu.open("Options");
        h.dragTo("MusicVolume", static_cast<float>(percent));
        h.dragTo("SoundVolume", static_cast<float>(100 - percent));
        h.click("ApplyVideo");
        const Settings stored = h.reloaded().settings;
        Harness again([&](SaveData& save) { save.settings = stored; });
        again.menu.open("Options");
        allBack = allBack && near(stored.musicVolume, static_cast<float>(percent) / 100.0f) &&
                  near(stored.soundVolume, static_cast<float>(100 - percent) / 100.0f) &&
                  near(again.widget("MusicVolume").value, static_cast<float>(percent)) &&
                  near(again.widget("SoundVolume").value, static_cast<float>(100 - percent));
    }
    check(allBack, "volume: all 21 positions of both sliders survive the slider, the file and the slider again");
}

// What the menus write, and when.
void testSaving() {
    Harness h;
    for (const char* form : {"selectstage", "stagepiratos", "highscores", "Info", "Options"}) {
        h.menu.open(form);
    }
    h.click(h.widget("Camera"));
    check(!h.wrote("settings.json") && !h.wrote("progress.json") && !h.wrote("highscores.json"),
          "saving: looking around and changing a setting writes nothing yet");
    h.click("ApplyVideo");
    check(h.wrote("settings.json") && !wroteOthers(h), "saving: leaving Options writes the settings, and nothing else");

    h.menu.open("selectstage");
    h.click("Metall-Raketka-Yellow.jpg");
    check(h.reloaded().settings.bat == 2 && h.reloaded().settings.smartCamera == h.menu.settings().smartCamera && !wroteOthers(h),
          "saving: picking a bat writes the settings at once, with the changes before it");

    // The saved settings come back whole in the next run's menus.
    const Settings stored = h.reloaded().settings;
    Harness next([&](SaveData& save) { save.settings = stored; });
    next.menu.open("Options");
    check(next.widget("Camera").checked == stored.smartCamera && next.widget("Camera").checked != Settings{}.smartCamera,
          "saving: the Options of the next run show what was set");
    next.menu.open("selectstage");
    check(!next.widget("Metall-Raketka-Yellow.jpg").enabled, "saving: and the stage screens the bat that was picked");

    // Save data with no folder (userDataDir() found none) keeps the choices in memory for the run; the failed save is no trouble.
    SaveData memory{fs::path()};
    Menu inMemory(h.loader(), campaign(), memory);
    inMemory.open("selectstage");
    inMemory.update({}, 1.0, h.layout, h.sizes);
    const Vec2 yellow = inMemory.form().find("Metall-Raketka-Yellow.jpg")->rect.center();
    UiInput clickYellow;
    clickYellow.pointer = {{yellow, false}, {yellow, true}, {yellow, false}};
    inMemory.update(clickYellow, 0, h.layout, h.sizes);
    check(memory.settings.bat == 2, "saving: without a folder the choice stays in memory");
}

void testSliderSounds() {
    // The knob has no sound of its own, but the - and + buttons do.
    Harness h;
    h.menu.open("Options");
    h.forget();
    const UiWidget& plus = h.stepper("SoundVolume", "+");
    h.frame(h.move(h.center(plus)), 0);
    h.frame(h.move(h.center(plus), true), 0);
    check(h.sounds == std::vector<std::string>({"in", "down"}), "slider: the + button plays its in and down sounds");
}

void testHighScores() {
    Harness h([](SaveData& save) {
        for (int i = 0; i < 20; ++i) {
            save.highScores.add("p" + std::to_string(i), 10000 - i * 100, "piratos-a");
        }
    });
    h.menu.open("highscores");
    check(!h.widget("86400").visible && !h.widget("2073600").visible && !h.widget("LeftCaption").visible && !h.widget("PleaseWait").visible,
          "scores: the online buttons and the download text are hidden");
    check(h.widget("LocalScores").visible && h.widget("MapSmall").visible && h.widget("MainMenu").visible, "scores: the rest stays");
    check(!h.widget("PrevPage").visible && h.widget("NextPage").visible, "scores: twenty entries are three pages of eight; page one has no previous");
    h.click("NextPage");
    check(h.widget("PrevPage").visible && h.widget("NextPage").visible, "scores: the middle page has both arrows");
    h.click("NextPage");
    check(h.widget("PrevPage").visible && !h.widget("NextPage").visible, "scores: the last has no next");
    h.click("LocalScores");
    check(!h.widget("PrevPage").visible && h.widget("NextPage").visible, "scores: Local Scores returns to the first page");

    Harness empty;
    empty.menu.open("highscores");
    check(!empty.widget("PrevPage").visible && !empty.widget("NextPage").visible, "scores: an empty table has no arrows");
    check(empty.click("MainMenu").kind == MenuResult::Kind::None && empty.menu.formName() == "mainmenu", "scores: Main Menu leaves");

    // The table the save data keeps is as long as the form has pages for: a full one ends on a full page.
    Harness full([](SaveData& save) {
        for (std::size_t i = 0; i < kMaxHighScores; ++i) {
            save.highScores.add("p", 5000 - static_cast<int>(i), "");
        }
    });
    full.menu.open("highscores");
    int pages = 1;
    while (full.widget("NextPage").visible && pages < 20) {
        full.click("NextPage");
        ++pages;
    }
    check(pages == 5 && kMaxHighScores == 5 * 8, "scores: a full table is five pages of eight lines");
}

void typeInto(Harness& h, const std::string& text, std::vector<UiKey> keys = {}) {
    UiInput in;
    in.text = text;
    in.keys = std::move(keys);
    h.frame(in, 0);
}

void testNameEntry() {
    Harness h([](SaveData& save) {
        save.highScores.add("top", 5000, "piratos-a");
        save.highScores.add("mid", 3000, "piratos-a");
        save.highScores.add("low", 100, "piratos-a");
    });

    h.menu.openGameOver(0);
    check(h.menu.formName() == "gameover" && h.widget("Score").text == "0" && !h.menu.wantsText(), "name: a score of 0 goes to Game Over");
    h.menu.openGameOver(4000, "village-d");
    check(h.menu.formName() == "entername" && h.widget("Score").text == "4000" && h.menu.wantsText(),
          "name: a score that makes the table goes to the name entry, which wants typed text");

    typeInto(h, "Bob");
    check(h.widget("Name").text == "Bob", "name: typed text appears in the field");
    typeInto(h, "", {UiKey::Backspace});
    check(h.widget("Name").text == "Bo", "name: Backspace removes the last character");
    typeInto(h, "\xC3\xA9");  // é
    typeInto(h, "", {UiKey::Backspace});
    check(h.widget("Name").text == "Bo", "name: and a two-byte character goes whole");
    typeInto(h, "a\tb\n\x7f");
    check(h.widget("Name").text == "Boab", "name: control characters are not typed");
    typeInto(h, "", {UiKey::Backspace, UiKey::Backspace, UiKey::Backspace, UiKey::Backspace, UiKey::Backspace});
    check(h.widget("Name").text.empty(), "name: Backspace on an empty field does nothing");
    typeInto(h, "ABCDEFGHIJKLMNOP");
    check(h.widget("Name").text == "ABCDEFGHIJKL", "name: at most twelve characters");
    typeInto(h, "", {UiKey::Backspace, UiKey::Backspace});
    typeInto(h, "\xD0\x96\xD0\x96\xD0\x96");  // Ж three times: characters, not bytes, are counted
    check(h.widget("Name").text == "ABCDEFGHIJ\xD0\x96\xD0\x96", "name: the limit counts characters, not bytes");

    // Enter, or the Apply button, enters the score; the table opens with the new line.
    typeInto(h, "", {UiKey::Backspace, UiKey::Backspace, UiKey::Backspace, UiKey::Backspace, UiKey::Backspace, UiKey::Backspace, UiKey::Backspace,
                     UiKey::Backspace, UiKey::Backspace, UiKey::Backspace, UiKey::Backspace, UiKey::Backspace});
    typeInto(h, "  Ann ");
    h.forget();
    check(!h.wrote("highscores.json"), "name: nothing is written before the score is entered");
    typeInto(h, "", {UiKey::Enter});
    check(h.menu.formName() == "highscores", "name: Enter enters the score and shows the table");
    const std::vector<HighScore>& now = h.save.highScores.entries;
    check(now.size() == 4 && now[1].name == "Ann" && now[1].score == 4000 && now[1].level == "village-d" && now[0].name == "top",
          "name: the score is in its place, with the name without spaces around it and the level it was made on");
    const SaveData stored = h.reloaded();
    check(stored.highScores.entries.size() == 4 && stored.highScores.entries[1].name == "Ann" && stored.highScores.entries[1].level == "village-d" &&
              !h.wrote("settings.json") && !h.wrote("progress.json"),
          "name: and the table, and only the table, is saved");

    h.menu.openGameOver(50000);
    check(h.menu.formName() == "entername" && h.widget("Name").text.empty(), "name: a new entry starts with an empty field");
    h.click("ApplyRecord");
    check(h.menu.formName() == "highscores" && now[0].name == "Player" && now[0].score == 50000 && now[0].level.empty(),
          "name: the Apply button enters it, an empty name as Player");

    // A full table with a better score than its last entry still has room; one that does not beat it goes to Game Over.
    Harness f([](SaveData& save) {
        for (int i = 0; i < static_cast<int>(kMaxHighScores); ++i) {
            save.highScores.add("x", 2000 - i, "");
        }
    });
    const int lowest = 2000 - static_cast<int>(kMaxHighScores) + 1;
    f.menu.openGameOver(lowest - 1);
    check(f.menu.formName() == "gameover", "name: a score below a full table's last goes to Game Over");
    f.menu.openGameOver(lowest + 1);
    check(f.menu.formName() == "entername", "name: a score above it can be entered");

    // The ending leads to the same.
    Harness e;
    e.menu.openEnding(777, "knight-castle");
    check(e.menu.formName() == "endgame", "ending: the story form");
    e.click("GameOver");
    check(e.menu.formName() == "entername" && e.widget("Score").text == "777", "ending: its last button goes on to the score");
    e.click("ApplyRecord");
    check(e.save.highScores.entries.size() == 1 && e.save.highScores.entries[0].level == "knight-castle", "ending: and the level goes with it");

    // Typed text outside the name entry goes nowhere.
    Harness other;
    typeInto(other, "text");
    check(!other.menu.wantsText() && other.menu.formName() == "mainmenu", "name: text typed on another form is ignored");
}

void testLoadingAndFades() {
    Harness h;
    h.menu.open("Loading");
    check(near(h.widget("Progress").value, 0), "loading: the bar starts empty");
    h.menu.setProgress(0.5f);
    check(near(h.widget("Progress").value, 5) && near(h.widget("Progress").fraction(), 0.5f), "loading: the bar fills in its own range");
    h.menu.setProgress(3);
    check(near(h.widget("Progress").value, 10), "loading: and is clamped");
    h.menu.setProgress(-1);
    check(near(h.widget("Progress").value, 0), "loading: ... at both ends");

    h.menu.open("Loading");
    check(h.menu.shownSeconds() == 0 && !h.menu.shownLongEnough(), "show time: a form that has to stay (show_time_msec 300) is not done at once");
    h.frame({}, 0.2);
    check(!h.menu.shownLongEnough(), "show time: ... nor after 200 ms");
    h.frame({}, 0.1);
    check(h.menu.shownLongEnough() && near(static_cast<float>(h.menu.shownSeconds()), 0.3f), "show time: ... but after 300 ms");
    h.menu.open("MainMenu");
    check(h.menu.shownLongEnough() && h.menu.shownSeconds() == 0, "show time: a form with no show time is always long enough");
}

void testFlow() {
    Harness h;
    const Campaign levels = campaign();
    GameFlow flow(h.menu, levels);
    const auto step = [&](const UiInput& in = {}, double seconds = 0.016) { return flow.update(in, seconds, h.layout, h.sizes); };
    const auto clickNamed = [&](const char* name) {
        UiInput in;
        const Vec2 at = h.center(name);
        in.pointer = {{at, false}, {at, true}, {at, false}};
        return step(in);
    };
    UiInput esc;
    esc.keys = {UiKey::Escape};
    UiInput anyKey;
    anyKey.keys = {UiKey::Other};

    check(flow.mode() == GameFlow::Mode::Menu, "flow: starts in the menus");
    clickNamed("SelectStage");
    clickNamed("StagePiratos");
    check(flow.mode() == GameFlow::Mode::Menu && h.menu.formName() == "stagepiratos", "flow: menus lead to a stage");
    GameFlow::Step s = clickNamed("1");
    check(flow.mode() == GameFlow::Mode::Loading && h.menu.formName() == "loading" && !s.loadLevel && !s.quit,
          "flow: choosing a level shows Loading, and waits a frame so that it is drawn before the build");
    check(flow.level() == 1, "flow: the level is remembered");
    s = step();
    check(s.loadLevel && *s.loadLevel == 1 && flow.mode() == GameFlow::Mode::Loading, "flow: the next frame asks the app to build level 1");
    s = step();
    check(!s.loadLevel && flow.mode() == GameFlow::Mode::Loading, "flow: ... only once");
    flow.hud().score = 99;  // leftovers of a previous game must not survive
    flow.loaded(true);
    check(flow.hud().lives == 3 && flow.hud().score == 0 && flow.hud().level == 2 && flow.hud().powerUps.empty(),
          "flow: the HUD gets its placeholders: three lives, no points, the level's number counting from 1");
    check(near(h.widget("Progress").value, 10), "flow: the loading bar is full when the level is ready");
    step();
    check(flow.mode() == GameFlow::Mode::Loading || flow.mode() == GameFlow::Mode::Playing, "flow: on to play");
    step({}, 0.5);
    check(flow.mode() == GameFlow::Mode::Playing, "flow: once the loading form has been up for its show time, the level plays");

    // Pause.
    step(esc);
    check(flow.mode() == GameFlow::Mode::Paused, "flow: Escape pauses");
    step(anyKey);
    check(flow.mode() == GameFlow::Mode::Playing, "flow: any other key resumes");
    step(esc);
    step(esc);
    check(flow.mode() == GameFlow::Mode::Menu && h.menu.formName() == "mainmenu", "flow: Escape again goes back to the main menu");

    // A mouse button resumes, but one held down when the pause began does not.
    clickNamed("SelectStage");
    clickNamed("StagePiratos");
    clickNamed("0");
    step();
    flow.loaded(true);
    step();
    step({}, 0.5);
    check(flow.mode() == GameFlow::Mode::Playing && flow.hud().level == 1, "flow: a second level plays");
    UiInput held;
    held.pointer = {{{500, 400}, true}};
    step(held);
    step(esc);
    check(flow.mode() == GameFlow::Mode::Paused, "flow: paused with the button held");
    step(held);
    check(flow.mode() == GameFlow::Mode::Paused, "flow: a button already held does not resume");
    UiInput released;
    released.pointer = {{{500, 400}, false}};
    step(released);
    check(flow.mode() == GameFlow::Mode::Paused, "flow: letting it go does not either");
    step(held);
    check(flow.mode() == GameFlow::Mode::Playing, "flow: pressing the button again does");
    step(esc);
    step(esc);
    check(flow.mode() == GameFlow::Mode::Menu, "flow: back in the menus");

    // A level that cannot be built sends the player back to its stage.
    clickNamed("SelectStage");
    clickNamed("StageFarm");
    check(h.menu.formName() == "stagefarm", "flow: the village stage");
    clickNamed("4");
    s = step();
    check(s.loadLevel && *s.loadLevel == 4, "flow: level 4 is asked for");
    flow.loaded(false);
    check(flow.mode() == GameFlow::Mode::Menu && h.menu.formName() == "stagefarm", "flow: a failed build returns to the stage's form");
    flow.loaded(true);
    check(flow.mode() == GameFlow::Mode::Menu, "flow: an answer nobody asked for changes nothing");

    // Exit from the main menu.
    h.menu.open("MainMenu");
    check(clickNamed("Exit").quit, "flow: the Exit button quits");
}

}  // namespace

int main() {
    testHoverAndSounds();
    testNavigation();
    testEscape();
    testQuitAndStart();
    testLocking();
    testBats();
    testOptions();
    testVolumeConversion();
    testSaving();
    testSliderSounds();
    testHighScores();
    testNameEntry();
    testLoadingAndFades();
    testFlow();
    if (failures == 0) {
        std::puts("all menu tests passed");
    }
    return failures == 0 ? 0 : 1;
}
