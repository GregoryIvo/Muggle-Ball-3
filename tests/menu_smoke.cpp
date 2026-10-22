// Runs the menus and the HUD against the real game data in $MB3_ASSETS: the campaign and its level files, what every
// button of every form does, a scripted walk through the screens with real layouts, and (with a GPU) the sprites of
// the faded-in forms, the HUD and the pause screen.
// Skips (exit code 77) without the converted data; the drawing half is skipped alone when there is no GPU device.

#include "RealData.h"
#include "mb/engine/Renderer.h"
#include "mb/game/Campaign.h"
#include "mb/game/GameFlow.h"
#include "mb/game/Hud.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/Menu.h"
#include "mb/game/SaveData.h"
#include "mb/game/SoundLibrary.h"
#include "mb/game/UiAssets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

using realdata::lower;

struct Game {
    explicit Game(const fs::path& root)
        : strings(mb::StringTable::load(root / "Data" / "xml" / "languages.json")),
          campaign(mb::Campaign::load(root / "Data" / "xml" / "levels.json")),
          sizes(root),
          save{fs::path()},  // in memory: the walk must not touch the player's files
          menu([this, root](std::string_view name) { return realdata::loadForm(root, strings, name); }, campaign, save) {
        menu.setVideoModes({{800, 600}, {1024, 768}, {1280, 960}});
    }

    mb::ImageSizeFn imageSizes() {
        return [this](std::string_view name) { return sizes(name); };
    }
    mb::MenuResult frame(const mb::UiInput& input = {}, double seconds = 1.0) {
        return menu.update(input, seconds, layout, imageSizes());
    }
    const mb::UiWidget* widget(const std::string& name) const { return menu.form().find(name); }

    mb::StringTable strings;
    mb::Campaign campaign;
    realdata::ImageSizes sizes;
    mb::UiLayout layout = mb::UiLayout::fit(1024, 768);
    mb::SaveData save;
    mb::Menu menu;
};

std::vector<std::string> formNames(const fs::path& root) {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(root / "Data" / "Menu")) {
        if (entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(names);
    return names;
}

void checkCampaign(const fs::path& root, Game& game) {
    const mb::Campaign& campaign = game.campaign;
    check(campaign.levelCount() == 83 && campaign.stages().size() == 4, "the campaign has 83 levels in 4 stages");
    mb::LevelCatalog catalog(root / "Data" / "Levels");
    std::set<std::string> seen;
    for (const mb::Stage& stage : campaign.stages()) {
        check(!stage.form.empty() && fs::exists(root / "Data" / "Menu" / (stage.form + ".json")), stage.name + ": its form " + stage.form + " exists");
        // Every level of the stage has a button on the stage's form, numbered like its campaign index.
        game.menu.open(stage.form);
        std::set<int> buttons;
        for (const mb::UiWidget& w : game.menu.form().widgets()) {
            if (w.userData == "SelectLevel") {
                buttons.insert(std::atoi(w.name.c_str()));
            }
        }
        std::set<int> expected;
        for (std::size_t i = 0; i < stage.levels.size(); ++i) {
            expected.insert(stage.first + static_cast<int>(i));
        }
        check(buttons == expected, stage.name + ": its form has one button for each of its levels, numbered by campaign index");
        for (const std::string& level : stage.levels) {
            check(catalog.find(level).has_value(), "the level " + level + " has a converted file");
            check(seen.insert(lower(level)).second, "the level " + level + " is in the campaign once");
        }
    }
    check(seen.size() == catalog.levels().size(), "the campaign lists every converted level");
    check(*campaign.levelName(0) == "piratos-ostrovok" && campaign.indexOf("Piratos-SOS") == 15 && campaign.stageOf(46)->name == "Cowboys" &&
              campaign.stageOf(82)->name == "Knights" && campaign.levelName(83) == nullptr,
          "the campaign: first level, a lookup ignoring case, the stages' ranges");
    check(campaign.stageForForm("StageFarm")->name == "Village" && campaign.stageForForm("selectstage") == nullptr, "the campaign: forms and stages");
}

// What each button does, and that every one of them can be reached with the pointer.
void checkButtons(const fs::path& root, Game& game) {
    int buttons = 0;
    std::set<mb::MenuIntent> intents;
    for (const std::string& name : formNames(root)) {
        mb::UiForm form = mb::UiForm::load(root / "Data" / "Menu" / (name + ".json"), game.strings);
        form.layout(game.layout, game.imageSizes());
        for (mb::UiWidget& w : form.widgets()) {
            if (!w.interactive()) {
                continue;
            }
            const mb::MenuIntent intent = mb::Menu::intentOf(form, w);
            check(intent != mb::MenuIntent::Unknown, name + ": the widget \"" + w.name + "\" has an intent");
            intents.insert(intent);
            ++buttons;
            // One that is enabled in the data answers the pointer at its centre: nothing lies over it.
            if (w.enabled) {
                check(form.hitTest(w.rect.center()) == &w, name + ": the " + w.name + " widget is reachable at its centre");
            }
        }
    }
    check(buttons > 100, "the forms have well over a hundred interactive widgets (levels, bats, menus)");
    check(intents.size() >= 15, "all kinds of intent occur in the data");
}

// The sounds the forms ask for are in the sound list, and some of them have a file in the game's data.
void checkSounds(const fs::path& root) {
    const mb::SoundCatalog catalog = mb::SoundCatalog::load(root);
    std::set<std::string> names;
    for (const std::string& name : formNames(root)) {
        mb::UiForm form = mb::UiForm::load(root / "Data" / "Menu" / (name + ".json"), mb::StringTable::load(root / "Data" / "xml" / "languages.json"));
        for (const mb::UiWidget& w : form.widgets()) {
            for (const std::string& sound : {w.soundMouseIn, w.soundMouseDown}) {
                if (!sound.empty() && names.insert(lower(sound)).second) {
                    const mb::SoundDef* def = catalog.findSound(sound);
                    check(def != nullptr, name + ": the sound \"" + sound + "\" of the " + w.name + " widget is in the sound list");
                    check(!def || !def->samples.empty(), "the sound \"" + sound + "\" has a file");
                }
            }
        }
    }
    check(!names.empty(), "the forms ask for sounds");
}

// Follows every Navigate button from the main menu by clicking it, and checks which forms that reaches and that
// Escape leads back from each.
void checkCrawl(const fs::path& root, Game& game) {
    std::set<std::string> reached{"mainmenu"};
    std::vector<std::string> todo{"MainMenu"};
    while (!todo.empty()) {
        const std::string name = todo.back();
        todo.pop_back();
        game.menu.open(name);
        game.frame();
        // (info2's and info3's root forms are called "Info" like the button that goes back there, so buttons are
        // found by position, not by name.)
        std::vector<std::pair<std::string, mb::Vec2>> targets;
        for (const mb::UiWidget& w : game.menu.form().widgets()) {
            if (w.userData == "Navigate" && w.enabled && w.visible) {
                targets.emplace_back(w.name, w.rect.center());
            }
        }
        for (const auto& [target, at] : targets) {
            game.menu.open(name);
            game.frame();
            mb::UiInput in;
            in.pointer = {{at, false}, {at, true}, {at, false}};
            game.frame(in, 0);
            check(game.menu.formName() == lower(target), name + ": clicking " + target + " opens " + target + ", not " + game.menu.formName());
            if (reached.insert(game.menu.formName()).second) {
                todo.push_back(game.menu.formName());
            }
        }
    }
    const std::set<std::string> expected{"mainmenu", "selectstage", "stagepiratos", "stagefarm", "stagecowboys", "stageknights", "info", "info2",
                                         "info3", "credits", "options", "highscores"};
    check(reached == expected, "the Navigate buttons reach exactly the menus a player can browse (the score, story and loading forms come with the game)");
    // The forms only the game opens still lead somewhere.
    for (const char* name : {"endgame", "endgame2", "endgame3", "GameOver", "EnterName"}) {
        game.menu.open(name);
        for (const mb::UiWidget& w : game.menu.form().widgets()) {
            if (w.userData == "Navigate") {
                check(fs::exists(root / "Data" / "Menu" / (w.name + ".json")), std::string(name) + ": the button " + w.name + " leads to a form");
            }
        }
    }
    // Escape from each browsable form comes back to the main menu in at most two steps.
    mb::UiInput esc;
    esc.keys = {mb::UiKey::Escape};
    for (const std::string& name : expected) {
        game.menu.open(name);
        game.frame(esc, 0);
        game.frame(esc, 0);
        check(game.menu.formName() == "mainmenu", name + ": Escape leads back to the main menu");
    }
}

// A walk through the screens with the real forms and layouts.
void checkWalk(Game& game) {
    mb::GameFlow flow(game.menu, game.campaign);
    const auto step = [&](const mb::UiInput& in = {}, double seconds = 0.016) {
        return flow.update(in, seconds, game.layout, game.imageSizes());
    };
    const auto click = [&](const std::string& name) {
        const mb::UiWidget* w = game.widget(name);
        check(w != nullptr, game.menu.formName() + ": has " + name);
        mb::UiInput in;
        if (w) {
            const mb::Vec2 at = w->rect.center();
            in.pointer = {{at, false}, {at, true}, {at, false}};
        }
        return step(in);
    };
    game.menu.open("MainMenu");
    check(game.widget("SelectStage")->text == "Start Game" && game.widget("Exit") != nullptr, "walk: the main menu");
    click("SelectStage");
    check(game.menu.formName() == "selectstage", "walk: Start Game leads to the stage selection");
    click("StageCowboys");
    check(game.menu.formName() == "stagecowboys", "walk: a stage button leads to its levels");
    check(game.widget("46")->enabled && !game.widget("47")->enabled && !game.widget("62")->enabled && game.widget("45") == nullptr,
          "walk: the cowboys have levels 46 to 62, and a new player can start the first only");
    game.menu.setLevelsLocked(false);  // as the game has them while nothing finishes a level
    game.menu.open("stagecowboys");
    check(game.widget("46")->enabled && game.widget("62")->enabled, "walk: with the locks off all of them are open");
    mb::GameFlow::Step s = click("50");
    check(flow.mode() == mb::GameFlow::Mode::Loading && game.menu.formName() == "loading" && flow.level() == 50, "walk: a level button shows Loading");
    s = step();
    check(s.loadLevel && *game.campaign.levelName(*s.loadLevel) == "cowboys-napadenie", "walk: and asks for cowboys-napadenie, the fifty-first level");
    flow.loaded(true);
    step();
    step({}, 0.1);
    check(flow.mode() == mb::GameFlow::Mode::Playing && flow.hud().level == 51 && flow.hud().lives == 3, "walk: Playing, with the HUD's placeholders");
    mb::UiInput esc;
    esc.keys = {mb::UiKey::Escape};
    step(esc);
    step(esc);
    check(flow.mode() == mb::GameFlow::Mode::Menu && game.menu.formName() == "mainmenu", "walk: Escape, Escape: back at the main menu");

    // Options, with the real geometry of its sliders and boxes.
    click("Options");
    check(game.menu.formName() == "options" && game.widget("MusicVolume")->value == 70 && game.widget("Video")->text == "1024 x 768", "walk: Options shows the settings");
    const mb::UiWidget* music = game.widget("MusicVolume");
    const float y = music->rect.center().y;
    mb::UiInput drag;
    drag.pointer = {{{music->rect.x + music->knobSize.x / 2 + music->rect.w * 0.25f, y}, false}, {{music->rect.x + music->knobSize.x / 2 + music->rect.w * 0.25f, y}, true}};
    step(drag);
    check(std::fabs(game.widget("MusicVolume")->value - 25) < 1e-3f && std::fabs(game.menu.settings().musicVolume - 0.25f) < 1e-6f,
          "walk: pressing the music slider's track sets 25, which is a volume of 0.25");
    mb::UiInput up;
    up.pointer = {{{music->rect.x, y}, false}};
    step(up);
    const mb::UiWidget* camera = game.widget("Camera");
    check(!camera->checked, "walk: Smart Camera starts off");
    click("Camera");
    check(game.widget("Camera")->checked && game.menu.settings().smartCamera, "walk: and a click turns it on");
    click("Fullscreen");
    check(game.widget("Fullscreen")->checked && game.menu.settings().fullscreen, "walk: the Fullscreen box, a checkbox despite its tag");
    click("ApplyVideo");
    check(game.menu.formName() == "mainmenu", "walk: Main Menu leaves Options");

    // High scores and the name entry.
    game.save.highScores.add("Ann", 900, "piratos-ostrovok");
    game.save.highScores.add("Bob", 500, "piratos-ostrovok");
    click("HighScores");
    check(game.menu.formName() == "highscores" && !game.widget("86400")->visible, "walk: the high scores, without the online buttons");
    click("MainMenu");
    game.menu.openGameOver(700);
    check(game.menu.formName() == "entername" && game.widget("Score")->text == "700", "walk: a score of 700 is entered");
    mb::UiInput typing;
    typing.text = "Zed";
    game.frame(typing, 0);
    click("ApplyRecord");
    check(game.menu.formName() == "highscores" && game.save.highScores.entries[1].name == "Zed",
          "walk: the name is in the table at its place");
    game.menu.open("MainMenu");
    check(click("Exit").quit, "walk: Exit quits");
}

// With a GPU: sprites of the fading menus, the HUD and the pause screen.
void checkDrawing(const fs::path& root, Game& game) {
    std::unique_ptr<mb::Renderer> renderer;
    try {
        renderer = std::make_unique<mb::Renderer>();
    } catch (const std::exception& e) {
        std::printf("no GPU device (%s): skipping the drawing checks\n", e.what());
        return;
    }
    mb::UiAssets assets(*renderer, root);
    const auto draw = [&] {
        std::vector<mb::Sprite> sprites;
        game.menu.draw(assets, sprites);
        return sprites;
    };

    // The fade: halfway through 400 ms the form is half transparent and its logo is not; the background is not faded at all.
    game.menu.open("MainMenu");
    game.frame({}, 0.2);
    const mb::UiAssets::Image* logo = assets.image("forms/logo.dds");
    const mb::UiAssets::Image* button = assets.image("forms/button.dds");
    const mb::UiAssets::Image* sky = assets.image("Forms/sky.jpg");
    bool logoOpaque = false, buttonsHalf = false, skyOpaque = false;
    for (const mb::Sprite& s : draw()) {
        logoOpaque = logoOpaque || (logo && s.texture == logo->texture && s.tint.a == 1.0f);
        buttonsHalf = buttonsHalf || (button && s.texture == button->texture && std::fabs(s.tint.a - 0.5f) < 1e-3f);
        skyOpaque = skyOpaque || (sky && s.texture == sky->texture && s.tint.a == 1.0f);
    }
    check(logoOpaque && buttonsHalf && skyOpaque, "draw: half way through its fade the buttons are half transparent, the logo and the sky are not");
    game.frame({}, 0.3);
    bool full = false;
    for (const mb::Sprite& s : draw()) {
        full = full || (button && s.texture == button->texture && s.tint.a == 1.0f && std::fabs(s.tint.r - 240.0f / 255.0f) < 1e-3f);  // 0xfff0f0f0
    }
    check(full, "draw: after its fade time the buttons are fully opaque, in their own colour");

    // The sky scrolls (img_translation), and stops with the Freeze Sky setting.
    const auto skyOffset = [&] {
        for (const mb::Sprite& s : draw()) {
            if (sky && s.texture == sky->texture) {
                return s.uv0.x;
            }
        }
        return -1.0f;
    };
    const float before = skyOffset();
    game.frame({}, 10.0);
    const float after = skyOffset();
    check(before >= 0 && std::fabs(std::fmod(after - before + 1.0f, 1.0f) - 0.3f) < 1e-3f,
          "draw: ten seconds of the background's img_translation 3 move the sky by 30% of its width");
    game.save.settings.freezeSky = true;
    game.frame({}, 10.0);
    check(std::fabs(skyOffset() - after) < 1e-6f, "draw: with Freeze Sky on, the sky stands still");
    game.save.settings.freezeSky = false;

    // Every form, with the Menu's own state: something is drawn, and every sprite is sound.
    for (const std::string& name : formNames(root)) {
        game.menu.open(name);
        game.frame({}, 1.0);
        const std::vector<mb::Sprite> sprites = draw();
        bool valid = !sprites.empty() || name == "background";
        for (const mb::Sprite& s : sprites) {
            valid = valid && s.width >= 0 && s.height >= 0 && std::isfinite(s.x) && std::isfinite(s.y) && (s.texture != 0 || s.tint.a > 0);
        }
        check(valid, name + ": the menu draws valid sprites");
        const mb::Image image = renderer->renderToImage(1024, 768, {}, {}, {}, sprites);
        check(!image.empty(), name + ": renders");
    }

    // The high-score table: eight lines on a page, the text on the map.
    game.save.highScores.entries.clear();
    for (int i = 0; i < 12; ++i) {
        game.save.highScores.add("Player" + std::to_string(i), 1000 - i, "piratos-ostrovok");
    }
    game.menu.open("highscores");
    game.frame({}, 1.0);
    const std::size_t withTable = draw().size();
    game.save.highScores.entries.clear();
    game.menu.open("highscores");
    game.frame({}, 1.0);
    check(withTable > draw().size() + 8 * 8, "draw: the high-score table adds the text of eight lines to the form");

    // The HUD, on a target the size of the original's 800x600 screenshots and on a wide one.
    mb::GameHud hud;
    hud.lives = 5;
    hud.score = 768915;
    hud.level = 62;
    hud.powerUps = {"Cannon", "Magnit"};
    const mb::UiAssets::Image* heart = assets.image("Icons/Heart.png");
    for (const auto [width, height] : {std::pair{1024, 768}, {800, 600}, {1600, 768}}) {
        std::vector<mb::Sprite> sprites;
        mb::drawHud(assets, hud, width, height, sprites);
        const float scale = static_cast<float>(height) / 768.0f;
        std::vector<const mb::Sprite*> hearts;
        float leftEnd = 0, rightStart = 1e9f, rightEnd = 0, textTop = 1e9f, textBottom = 0;
        int icons = 0;
        for (const mb::Sprite& s : sprites) {
            if (heart && s.texture == heart->texture) {
                hearts.push_back(&s);
            } else if (s.x > static_cast<float>(width) - 80 * scale && s.y < 200 * scale) {
                ++icons;
            } else if (s.y > static_cast<float>(height) - 60 * scale) {
                const float half = static_cast<float>(width) / 2;
                if (s.x < half) {
                    leftEnd = std::max(leftEnd, s.x + s.width);
                } else {
                    rightStart = std::min(rightStart, s.x);
                    rightEnd = std::max(rightEnd, s.x + s.width);
                }
                textTop = std::min(textTop, s.y);
                textBottom = std::max(textBottom, s.y + s.height);
            }
        }
        const std::string size = std::to_string(width) + "x" + std::to_string(height);
        check(hearts.size() == 5, "HUD " + size + ": five hearts for five lives");
        if (hearts.size() == 5) {
            check(std::fabs(hearts[0]->x - 11 * scale) < 1.0f && std::fabs(hearts[0]->y - 9 * scale) < 1.0f && std::fabs(hearts[0]->width - 40 * scale) < 1.0f &&
                      std::fabs(hearts[1]->y - hearts[0]->y - 44 * scale) < 1.0f && std::fabs(hearts[1]->x - hearts[0]->x) < 1e-3f,
                  "HUD " + size + ": the hearts are 40 px, at the top left, 44 px apart");
        }
        check(icons == 2, "HUD " + size + ": two bonus icons at the top right");
        check(leftEnd > 100 * scale && leftEnd < 260 * scale, "HUD " + size + ": the points text starts at the left and is about 220 px wide");
        check(std::fabs(rightEnd - (static_cast<float>(width) - 15 * scale)) < 3.0f && rightStart > static_cast<float>(width) - 200 * scale,
              "HUD " + size + ": the level text ends 15 px from the right edge of the target");
        check(std::fabs((textTop + textBottom) / 2 - (static_cast<float>(height) - 18 * scale)) < 2 * scale,
              "HUD " + size + ": the text's line is centred 18 px above the bottom edge");
    }
    std::vector<mb::Sprite> none;
    hud.lives = 0;
    hud.powerUps.clear();
    mb::drawHud(assets, hud, 1024, 768, none);
    bool anyHeart = false;
    for (const mb::Sprite& s : none) {
        anyHeart = anyHeart || (heart && s.texture == heart->texture);
    }
    check(!anyHeart && !none.empty(), "HUD: no lives, no hearts, but the text");
    hud.lives = 99;
    none.clear();
    mb::drawHud(assets, hud, 1024, 768, none);
    int hearts = 0;
    for (const mb::Sprite& s : none) {
        hearts += heart && s.texture == heart->texture;
    }
    check(hearts == 12, "HUD: at most twelve hearts are drawn");

    std::vector<mb::Sprite> pause;
    mb::drawPause(assets, 1024, 768, pause);
    check(pause.size() > 40 && pause[0].texture == 0 && pause[0].width == 1024 && pause[0].height == 768 && pause[0].tint.a > 0 && pause[0].tint.a < 1,
          "pause: the scene is dimmed, then the PAUSE text and its two lines");
    const mb::Image image = renderer->renderToImage(1024, 768, {}, {}, {}, pause);
    check(!image.empty(), "pause: renders");
}

}  // namespace

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path root = env;
    if (!fs::is_regular_file(root / "Data" / "xml" / "levels.json") || !fs::is_regular_file(root / "Data" / "xml" / "languages.json") ||
        !fs::is_directory(root / "Data" / "Menu")) {
        std::printf("SKIP: converted menu data and the level list not found under %s (run tools/convert_assets.py)\n", root.string().c_str());
        return 77;
    }
    try {
        Game game(root);
        checkCampaign(root, game);
        checkButtons(root, game);
        checkSounds(root);
        checkCrawl(root, game);
        checkWalk(game);
        checkDrawing(root, game);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("all menu smoke checks passed");
    }
    return failures == 0 ? 0 : 1;
}
