#include "mb/game/Menu.h"

#include "StringUtil.h"
#include "mb/engine/Log.h"
#include "mb/game/UiDraw.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <format>
#include <limits>
#include <utility>

namespace mb {

namespace {

constexpr int kScoreRows = 8;  // table lines per page
static_assert(kMaxHighScores % kScoreRows == 0, "the high-score table should end on a full page");

// The sliders count 0 to 100 in steps of 5; the settings hold volumes from 0 to 1.
float percentOf(float volume) {
    return std::round(volume * 100.0f);
}

float volumeOf(float percent) {
    return std::round(percent) / 100.0f;
}

// The bat a button of the stage forms stands for, from its name ("Metall-Raketka-Red.jpg"); -1 if it is none.
int batOf(std::string_view buttonName) {
    const std::string name = lowerAscii(buttonName);
    for (const auto& [colour, bat] : {std::pair{"red", 0}, {"blue", 1}, {"yellow", 2}}) {
        if (name.find(colour) != std::string::npos) {
            return bat;
        }
    }
    return -1;
}

// The campaign index a level button is named with, -1 if its name is not a number.
int levelOf(std::string_view buttonName) {
    int level = -1;
    const auto [end, error] = std::from_chars(buttonName.data(), buttonName.data() + buttonName.size(), level);
    return error == std::errc() && end == buttonName.data() + buttonName.size() ? level : -1;
}

// Bytes of the UTF-8 character that starts with `lead` (1 for a stray byte).
std::size_t charLength(unsigned char lead) {
    return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
}

std::size_t charCount(std::string_view text) {
    std::size_t count = 0;
    for (const char c : text) {
        count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }
    return count;
}

void dropLastChar(std::string& text) {
    while (!text.empty()) {
        const bool continuation = (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80;
        text.pop_back();
        if (!continuation) {
            break;
        }
    }
}

Color colorOf(unsigned rgb, float alpha = 1) {
    return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

}  // namespace

Menu::Menu(FormLoader loadForm, Campaign campaign, SaveData& save, MenuHooks hooks)
    : loadForm_(std::move(loadForm)),
      campaign_(std::move(campaign)),
      save_(save),
      hooks_(std::move(hooks)),
      imageSizes_([](std::string_view) { return Vec2{}; }) {}

void Menu::setVideoModes(std::vector<VideoMode> modes) {
    videoModes_ = std::move(modes);
}

void Menu::setFullscreenAvailable(bool available) {
    fullscreenAvailable_ = available;
}

void Menu::open(std::string_view form) {
    const std::string name(form);  // `form` may point into the widgets about to be replaced
    UiForm next = loadForm_(name);  // throws before anything changes if there is no such form
    formName_ = lowerAscii(name);
    form_ = std::move(next);
    const bool withBackdrop = background_ && formName_ != "loading" && formName_ != "background";
    if (withBackdrop && backdrop_.widgets().empty()) {
        backdrop_ = loadForm_("background");
    }
    backdropShown_ = withBackdrop;
    shown_ = 0;
    dragging_ = -1;
    edit_ = -1;
    scorePage_ = 0;
    newScore_ = -1;
    blockDown_ = pointer_.down;  // a button held through the change belongs to the old form
    prepare();
    form_.layout(layout_, imageSizes_);
}

void Menu::openGameOver(int score, std::string_view level) {
    setScore(score, level);
    open(save_.highScores.qualifies(score) ? "EnterName" : "GameOver");
}

void Menu::openEnding(int score, std::string_view level) {
    setScore(score, level);
    open("endgame");
}

void Menu::setScore(int score, std::string_view level) {
    pendingScore_ = score;
    pendingLevel_ = std::string(level);  // `level` may be a view of pendingLevel_ itself
}

void Menu::setProgress(float fraction) {
    if (UiWidget* bar = form_.find("Progress")) {
        bar->value = bar->rangeMin + std::clamp(fraction, 0.0f, 1.0f) * (bar->rangeMax - bar->rangeMin);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// What each form shows when it opens

void Menu::prepare() {
    if (formName_ == "options") {
        prepareOptions();
    } else if (formName_ == "selectstage" || campaign_.stageForForm(formName_)) {
        prepareStage();
    } else if (formName_ == "highscores") {
        prepareScores();
    } else if (formName_ == "entername") {
        prepareEnterName();
    } else if (formName_ == "gameover") {
        setLabel("Score", std::to_string(pendingScore_));
    } else if (formName_ == "loading") {
        setProgress(0);
    }
}

void Menu::prepareOptions() {
    const Settings& s = save_.settings;
    const auto set = [&](std::string_view name, auto apply) {
        if (UiWidget* w = form_.find(name)) {
            apply(*w);
        }
    };
    set("MusicVolume", [&](UiWidget& w) { w.value = percentOf(s.musicVolume); });
    set("SoundVolume", [&](UiWidget& w) { w.value = percentOf(s.soundVolume); });
    set("BG", [&](UiWidget& w) { w.checked = s.freezeSky; });
    set("Camera", [&](UiWidget& w) { w.checked = s.smartCamera; });
    set("Shadow", [&](UiWidget& w) { w.checked = s.shadows; });
    set("Mirror", [&](UiWidget& w) { w.checked = s.mirrors; });
    set("Fullscreen", [&](UiWidget& w) {
        w.checked = s.fullscreen;
        if (!fullscreenAvailable_) {
            w.visible = false;  // nothing to toggle: one fixed display, no window (the Vita)
        }
    });
    videoAtOpen_ = s;

    if (videoModes_.empty()) {
        set("VideoMode", [](UiWidget& w) { w.visible = false; });  // nothing to choose from
        return;
    }
    // The mode the settings name, or failing that the closest one.
    int best = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < videoModes_.size(); ++i) {
        const int distance = std::abs(videoModes_[i].width - s.windowWidth) + std::abs(videoModes_[i].height - s.windowHeight);
        if (distance < best) {
            best = distance;
            videoMode_ = static_cast<int>(i);
        }
    }
    showVideoMode();
}

void Menu::showVideoMode() {
    const UiWidget* list = form_.find("VideoMode");
    UiWidget* label = form_.find("Video");
    if (!list || !label || videoModes_.empty()) {
        return;
    }
    const VideoMode& mode = videoModes_[static_cast<std::size_t>(videoMode_)];
    std::string text = list->text;  // "%s": the format of the line the list shows
    const std::size_t at = text.find("%s");
    label->text = at == std::string::npos ? text : text.replace(at, 2, std::format("{} x {}", mode.width, mode.height));
}

void Menu::prepareStage() {
    for (UiWidget& w : form_.widgets()) {
        if (w.kind != WidgetKind::Button) {
            continue;
        }
        if (w.userData == "SelectBat") {
            // The bat in use shows its "chosen" frame, which the data gives the disabled state.
            if (const int bat = batOf(w.name); bat >= 0) {
                w.enabled = bat != save_.settings.bat;
            }
        } else if (w.userData == "SelectLevel") {
            const int level = levelOf(w.name);
            w.enabled = level >= 0 && level < campaign_.levelCount() && (!lockLevels_ || levelOpen(level));
        }
    }
}

void Menu::prepareScores() {
    // The online tables came from a server that is gone: the buttons for them and the "downloading" text stay out.
    for (UiWidget& w : form_.widgets()) {
        if (w.userData == "OnlineScore" || w.name == "LeftCaption" || w.name == "PleaseWait") {
            w.visible = false;
        }
    }
    const int pages = scorePages();
    scorePage_ = std::clamp(scorePage_, 0, pages - 1);
    if (UiWidget* prev = form_.find("PrevPage")) {
        prev->visible = scorePage_ > 0;
    }
    if (UiWidget* next = form_.find("NextPage")) {
        next->visible = scorePage_ + 1 < pages;
    }
}

void Menu::prepareEnterName() {
    setLabel("Score", std::to_string(pendingScore_));
    const auto field = std::ranges::find_if(form_.widgets(), [](const UiWidget& w) { return w.kind == WidgetKind::TextEdit; });
    if (field != form_.widgets().end()) {
        edit_ = static_cast<int>(field - form_.widgets().begin());
    }
}

void Menu::setLabel(std::string_view name, std::string text) {
    if (UiWidget* w = form_.find(name)) {
        w->text = std::move(text);
    }
}

int Menu::scorePages() const {
    const int lines = static_cast<int>(save_.highScores.entries.size());
    return std::max(1, (lines + kScoreRows - 1) / kScoreRows);
}

// ---------------------------------------------------------------------------------------------------------------
// Input

MenuResult Menu::update(const UiInput& input, double seconds, const UiLayout& layout, const ImageSizeFn& imageSizes) {
    layout_ = layout;
    imageSizes_ = imageSizes;
    shown_ += seconds;
    if (!save_.settings.freezeSky) {
        skyTime_ += seconds;
    }
    form_.layout(layout_, imageSizes_);
    if (backdropShown_) {
        backdrop_.layout(layout_, imageSizes_);
    }

    MenuResult result;
    typeText(input.text);
    for (const UiKey key : input.keys) {
        pressKey(key);
    }
    for (const UiInput::Pointer& sample : input.pointer) {
        feed(sample.position, sample.down, result);
        if (result.kind != MenuResult::Kind::None) {
            return result;
        }
    }
    // Once more at rest: after a new form opened, or the target changed shape, the widget under the pointer
    // changes without the pointer moving.
    feed(pointer_.position, pointer_.down, result);
    return result;
}

void Menu::feed(Vec2 position, bool down, MenuResult& result) {
    pointer_ = {position, down};
    if (!down) {
        blockDown_ = false;
    }
    const bool held = down && !blockDown_;
    const PointerEvents events = form_.updatePointer(position, held);
    if (events.entered) {
        sound(events.entered->soundMouseIn);
    }
    if (events.pressed) {
        sound(events.pressed->soundMouseDown);
        press(*events.pressed);
    }
    if (dragging_ >= 0) {
        UiWidget& slider = form_.widgets()[static_cast<std::size_t>(dragging_)];
        if (held) {
            setSlider(slider, slider.valueAt(position.x));
        } else {
            dragging_ = -1;
        }
    }
    if (events.clicked) {
        click(*events.clicked, result);
    }
}

void Menu::press(UiWidget& widget) {
    if (widget.kind == WidgetKind::Slider) {
        dragging_ = static_cast<int>(&widget - form_.widgets().data());
        setSlider(widget, widget.valueAt(pointer_.position.x));
    }
}

void Menu::click(UiWidget& widget, MenuResult& result) {
    const std::string name = widget.name;  // opening a form replaces the widgets
    switch (intentOf(form_, widget)) {
        case MenuIntent::Navigate:
            try {
                open(name);
            } catch (const std::exception& e) {
                logError("cannot open the form \"{}\": {}", name, e.what());
            }
            break;
        case MenuIntent::StartLevel:
            if (const int level = levelOf(name); level >= 0) {
                result = {MenuResult::Kind::StartLevel, level};
            }
            break;
        case MenuIntent::QuickStart: {
            const Stage* stage = campaign_.stageForForm(formName_);
            result = {MenuResult::Kind::StartLevel, stage ? stage->first : 0};
            break;
        }
        case MenuIntent::SelectBat:
            selectBat(batOf(name));
            break;
        case MenuIntent::ApplyVideo:
            applyOptions();
            open("MainMenu");
            break;
        case MenuIntent::ApplyRecord:
            submitName();
            break;
        case MenuIntent::FinishGame:
            openGameOver(pendingScore_, pendingLevel_);
            break;
        case MenuIntent::Exit:
            result.kind = MenuResult::Kind::Quit;
            break;
        case MenuIntent::StepDown:
        case MenuIntent::StepUp:
            step(widget, widget.name == "-" ? -1 : 1);
            break;
        case MenuIntent::Checkbox:
            setCheckbox(widget, !widget.checked);
            break;
        case MenuIntent::LocalScores:
            scorePage_ = 0;
            prepareScores();
            break;
        case MenuIntent::PrevPage:
        case MenuIntent::NextPage:
            scorePage_ += widget.name == "PrevPage" ? -1 : 1;
            prepareScores();
            break;
        case MenuIntent::Slider:       // follows the pointer from the press on
        case MenuIntent::TextField:    // has the keyboard from the start
        case MenuIntent::OnlineScores:
        case MenuIntent::SoundOnly:
            break;
        case MenuIntent::Unknown:
            logWarn("{}: the widget \"{}\" does nothing (user_data \"{}\")", formName_, name, widget.userData);
            break;
    }
}

void Menu::pressKey(UiKey key) {
    switch (key) {
        case UiKey::Escape:
            back();
            break;
        case UiKey::Enter:
            if (edit_ >= 0) {
                submitName();
            }
            break;
        case UiKey::Backspace:
            if (edit_ >= 0) {
                dropLastChar(form_.widgets()[static_cast<std::size_t>(edit_)].text);
            }
            break;
        case UiKey::Other:
            break;
    }
}

void Menu::typeText(std::string_view text) {
    if (edit_ < 0) {
        return;
    }
    std::string& typed = form_.widgets()[static_cast<std::size_t>(edit_)].text;
    while (!text.empty()) {
        const std::size_t length = std::min(charLength(static_cast<unsigned char>(text.front())), text.size());
        const std::string_view character = text.substr(0, length);
        text.remove_prefix(length);
        const bool control = static_cast<unsigned char>(character.front()) < 0x20 || character.front() == 0x7F;
        if (!control && charCount(typed) < kMaxNameChars) {
            typed += character;
        }
    }
}

// Escape goes one step back; false where there is none.
bool Menu::back() {
    if (formName_ == "options") {
        applyOptions();  // the same as its Main Menu button
        open("MainMenu");
        return true;
    }
    if (formName_ == "mainmenu" || formName_ == "loading" || formName_ == "entername" || formName_.starts_with("endgame")) {
        return false;  // nothing before the first screen; a score or the story is not to be skipped
    }
    open(campaign_.stageForForm(formName_) ? "selectstage" : "MainMenu");
    return true;
}

void Menu::applyOptions() {
    // Leaving Options untouched must not put the window back to the size the settings remember.
    const Settings& s = save_.settings;
    const bool videoChanged = s.windowWidth != videoAtOpen_.windowWidth || s.windowHeight != videoAtOpen_.windowHeight ||
                              s.fullscreen != videoAtOpen_.fullscreen;
    if (videoChanged && hooks_.applyVideo) {
        hooks_.applyVideo(s);
    }
    save_.saveSettings();
}

void Menu::submitName() {
    if (edit_ < 0) {
        return;
    }
    const std::optional<std::size_t> rank =
        save_.highScores.add(form_.widgets()[static_cast<std::size_t>(edit_)].text, pendingScore_, pendingLevel_);
    save_.saveHighScores();
    open("HighScores");
    if (rank) {
        newScore_ = static_cast<int>(*rank);
        scorePage_ = newScore_ / kScoreRows;
        prepareScores();
    }
}

void Menu::setSlider(UiWidget& slider, float value) {
    value = std::clamp(value, slider.rangeMin, slider.rangeMax);
    if (value == slider.value) {
        return;
    }
    slider.value = value;
    if (slider.name == "MusicVolume") {
        save_.settings.musicVolume = volumeOf(value);
    } else if (slider.name == "SoundVolume") {
        save_.settings.soundVolume = volumeOf(value);
    }
    changed();
}

void Menu::setCheckbox(UiWidget& box, bool checked) {
    box.checked = checked;
    Settings& s = save_.settings;
    if (box.name == "BG") {
        s.freezeSky = checked;
    } else if (box.name == "Camera") {
        s.smartCamera = checked;
    } else if (box.name == "Shadow") {
        s.shadows = checked;
    } else if (box.name == "Mirror") {
        s.mirrors = checked;
    } else if (box.name == "Fullscreen") {
        s.fullscreen = checked;
    }
    changed();
}

void Menu::step(const UiWidget& button, int direction) {
    UiWidget& owner = form_.widgets()[static_cast<std::size_t>(button.parent)];
    if (owner.kind == WidgetKind::Slider) {
        setSlider(owner, owner.value + static_cast<float>(direction) * (owner.step > 0 ? owner.step : 1.0f));
    } else if (owner.kind == WidgetKind::List && !videoModes_.empty()) {
        const int count = static_cast<int>(videoModes_.size());
        int next = videoMode_ + direction;
        if (next < 0 || next >= count) {
            if (!owner.cycled) {
                return;
            }
            next = (next + count) % count;
        }
        videoMode_ = next;
        save_.settings.windowWidth = videoModes_[static_cast<std::size_t>(next)].width;
        save_.settings.windowHeight = videoModes_[static_cast<std::size_t>(next)].height;
        showVideoMode();
    }
}

void Menu::selectBat(int bat) {
    if (bat < 0) {
        return;
    }
    save_.settings.bat = bat;
    prepareStage();
    changed();
    save_.saveSettings();
}

void Menu::changed() {
    if (hooks_.settingsChanged) {
        hooks_.settingsChanged(save_.settings);
    }
}

// The level's place in its stage is below the number of levels the progress opened in that stage.
bool Menu::levelOpen(int index) const {
    const Stage* stage = campaign_.stageOf(index);
    return stage && index - stage->first < save_.progress.unlockedLevels(stage->name);
}

void Menu::sound(const std::string& name) const {
    if (hooks_.playSound && !name.empty()) {
        hooks_.playSound(name);
    }
}

MenuIntent Menu::intentOf(const UiForm& form, const UiWidget& widget) {
    switch (widget.kind) {
        case WidgetKind::Checkbox:
            return MenuIntent::Checkbox;
        case WidgetKind::Slider:
            return MenuIntent::Slider;
        case WidgetKind::TextEdit:
            return MenuIntent::TextField;
        case WidgetKind::Button:
            break;
        default:
            return MenuIntent::Unknown;
    }
    const std::string& data = widget.userData;
    if (data == "Navigate") return MenuIntent::Navigate;
    if (data == "SelectLevel") return MenuIntent::StartLevel;
    if (data == "SelectBat") return MenuIntent::SelectBat;
    if (data == "ApplyVideo") return MenuIntent::ApplyVideo;
    if (data == "ApplyRecord") return MenuIntent::ApplyRecord;
    if (data == "FinishGame") return MenuIntent::FinishGame;
    if (data == "OnlineScore") return MenuIntent::OnlineScores;
    if (!data.empty()) return MenuIntent::Unknown;

    // The buttons with no user_data are told apart by name.
    const std::string& name = widget.name;
    if (name == "Exit") return MenuIntent::Exit;
    if (name == "QuickStart") return MenuIntent::QuickStart;
    if (name == "LocalScores") return MenuIntent::LocalScores;
    if (name == "PrevPage") return MenuIntent::PrevPage;
    if (name == "NextPage") return MenuIntent::NextPage;
    if (name == "Logo") return MenuIntent::SoundOnly;
    if ((name == "-" || name == "+") && widget.parent >= 0) {
        const WidgetKind owner = form.widgets()[static_cast<std::size_t>(widget.parent)].kind;
        if (owner == WidgetKind::Slider || owner == WidgetKind::List) {
            return name == "-" ? MenuIntent::StepDown : MenuIntent::StepUp;
        }
    }
    return MenuIntent::Unknown;
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

float Menu::fade() const {
    const int fadeMs = form_.fadeTimeMs();
    return fadeMs > 0 ? static_cast<float>(std::min(shown_ * 1000.0 / fadeMs, 1.0)) : 1.0f;
}

void Menu::draw(UiAssets& assets, std::vector<Sprite>& out) const {
    if (backdropShown_) {
        UiDrawOptions options;
        options.scrollSeconds = skyTime_;
        drawForm(backdrop_, assets, out, options);
    }
    UiDrawOptions options;
    options.fade = fade();
    if (edit_ >= 0 && std::fmod(shown_, 1.0) < 0.5) {
        options.caret = &form_.widgets()[static_cast<std::size_t>(edit_)];  // blinks
    }
    drawForm(form_, assets, out, options);
    if (formName_ == "highscores") {
        drawScores(assets, out);
    }
}

// The page of the table on the map: rank, name and score, one line each.
void Menu::drawScores(UiAssets& assets, std::vector<Sprite>& out) const {
    const UiWidget* map = form_.find("MapSmall");
    const UiWidget* caption = form_.find("Caption");
    if (!map || !caption) {
        return;
    }
    const UiAssets::FontChoice choice = assets.font(caption->fontFace, 20);
    if (!choice.font) {
        return;
    }
    const float s = form_.screen().scale();
    const float alpha = fade();
    TextParams params;
    params.scale = choice.scale * s;

    const Rect& area = map->rect;
    const float left = area.x + area.w * 0.14f, right = area.right() - area.w * 0.14f;
    const float lineHeight = 33 * s;
    float y = area.y + area.h * 0.095f;
    const std::size_t first = static_cast<std::size_t>(scorePage_ * kScoreRows);
    for (std::size_t i = first; i < std::min(first + kScoreRows, save_.highScores.entries.size()); ++i) {
        const HighScore& entry = save_.highScores.entries[i];
        const bool fresh = static_cast<int>(i) == newScore_;
        params.color = colorOf(fresh ? 0x00ff00 : 0x604020, alpha);
        params.shadowOffset = fresh ? Vec2{s, s} : Vec2{};
        params.shadowColor = colorOf(0x000000, 0.56f * alpha);
        params.align = HAlign::Right;
        choice.font->draw(out, std::format("{}.", i + 1), {left + 26 * s, y}, params);
        choice.font->draw(out, std::to_string(entry.score), {right, y}, params);
        params.align = HAlign::Left;
        choice.font->draw(out, entry.name, {left + 40 * s, y}, params);
        y += lineHeight;
    }
}

}  // namespace mb
