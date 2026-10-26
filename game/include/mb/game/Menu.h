#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/Campaign.h"
#include "mb/game/SaveData.h"
#include "mb/game/UiAssets.h"
#include "mb/game/UiForm.h"
#include "mb/game/UiInput.h"
#include "mb/game/UiLayout.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mb {

// A window size the Options form's video list offers, in points.
struct VideoMode {
    int width = 0;
    int height = 0;
};

// What the menus ask of the rest of the program. All are optional; the app fills them in (sound, the window).
struct MenuHooks {
    // A sound name from the forms (sound_mouse_in, sound_mouse_down: "menustonein", "menustonedown", ...), called
    // once each time the pointer enters a widget that has one and once each time the button goes down on one.
    std::function<void(std::string_view soundName)> playSound;
    // Called with every change of a setting as it happens (the volume sliders, the checkboxes, the bat), so that the
    // new volume can be heard before the player leaves Options.
    std::function<void(const Settings&)> settingsChanged;
    // Called when Options is left with its Main Menu button, to make the video settings (window size, fullscreen) take effect.
    std::function<void(const Settings&)> applyVideo;
};

// What a frame of the menus asks the game to do.
struct MenuResult {
    enum class Kind {
        None,
        StartLevel,  // `level`, a campaign index
        Quit,        // the Exit button
    };
    Kind kind = Kind::None;
    int level = 0;
};

// What a widget of a form does when it is clicked. Every interactive widget of every shipped form has one other
// than Unknown (the smoke test checks), so a new form that Menu does not understand shows up there.
enum class MenuIntent {
    Unknown,
    Navigate,      // the widget's name is the form to show
    StartLevel,    // the name is the level's campaign index
    QuickStart,    // the first level of the stage the form is for, or of the whole game
    SelectBat,
    ApplyVideo,    // Options' Main Menu button: apply and save the settings, go back
    ApplyRecord,   // EnterName's Apply: put the name and the score in the table
    FinishGame,    // the end of the story: on to the score
    Exit,
    StepDown,      // the "-" of a slider or list
    StepUp,
    Slider,
    Checkbox,
    TextField,
    LocalScores,
    PrevPage,
    NextPage,
    OnlineScores,  // asks the dead score server; hidden
    SoundOnly,     // the stage emblem: plays its sound
};

// The menu screens of the game: the forms of Data/Menu, the way between them and what their widgets do.
//
// It owns the current form and fades it in, shows the background form behind it, and turns the pointer and the
// keyboard into the original's behaviour: hover and press frames and sounds, navigation by "Navigate" buttons,
// back with Escape, level and bat selection, the Options form (sliders, checkboxes, the video list), the
// high-score table, name entry and the loading bar. What the player keeps is the SaveData it is given: the menus show
// its settings, progress and high scores, change the settings and the table, and write them (Options left, a bat
// picked, a score entered). It knows nothing of the GPU: forms come from a loader and drawing is done with UiAssets.
//
//   Menu menu([&](std::string_view name) { return assets.loadForm(name); }, campaign, saveData, hooks);
//   menu.open("MainMenu");
//   each frame:  MenuResult r = menu.update(input, dt, UiLayout::fit(w, h), assets.imageSizes());
//                menu.draw(assets, sprites);
class Menu {
public:
    // Loads a form by name, case-insensitively; throws if there is none. Called for every form the menus show and
    // for the background form.
    using FormLoader = std::function<UiForm(std::string_view name)>;

    // `save` must outlive the Menu.
    Menu(FormLoader loadForm, Campaign campaign, SaveData& save, MenuHooks hooks = {});

    // The window sizes the Options video list offers. With none the list is hidden.
    void setVideoModes(std::vector<VideoMode> modes);
    // Whether Options offers the Fullscreen check box at all (on by default); off hides it the same way
    // setVideoModes({}) hides the size list, for a platform with one fixed display and no window (the Vita).
    void setFullscreenAvailable(bool available);
    // Whether the background form (the sky and the stone frame) is drawn behind the forms; on until turned off.
    // Takes effect with the next form opened. The Loading form has its own picture and never has it.
    void setBackground(bool on) { background_ = on; }
    // Whether the stage screens lock the levels that the saved Progress has not opened (a stage's first level is
    // always open; finishing a level opens the next). On until turned off. The game cannot finish a level yet, so the
    // app turns it off until it can.
    void setLevelsLocked(bool locked) { lockLevels_ = locked; }

    // Shows a form, fading it in. The name is the form's file name, any case ("MainMenu", "stagepiratos").
    void open(std::string_view form);
    // The game ended with this score, made on this level (its catalog name, for the table; may be empty): the
    // high-score entry form if it makes the table, else Game Over.
    void openGameOver(int score, std::string_view level = {});
    // The story ending, which leads to the same.
    void openEnding(int score, std::string_view level = {});
    // The score the name entry and Game Over forms show and the table gets: set by openGameOver and openEnding, or
    // by hand before opening one of those forms.
    void setScore(int score, std::string_view level = {});
    // The progress bar of the Loading form, 0 to 1.
    void setProgress(float fraction);

    // Advances the menus by `seconds` and handles one frame of input on a target laid out as `layout`.
    MenuResult update(const UiInput& input, double seconds, const UiLayout& layout, const ImageSizeFn& imageSizes);
    // Appends the sprites of the background and the current form.
    void draw(UiAssets& assets, std::vector<Sprite>& out) const;

    // The name of the form shown, in lowercase.
    const std::string& formName() const { return formName_; }
    const UiForm& form() const { return form_; }
    // Whether the current form wants typed text (so the window should deliver it).
    bool wantsText() const { return edit_ >= 0; }
    // How long the current form has been up, and whether that is at least its show_time_msec.
    double shownSeconds() const { return shown_; }
    bool shownLongEnough() const { return shown_ * 1000.0 >= form_.showTimeMs(); }

    const Settings& settings() const { return save_.settings; }

    // What clicking a widget of `form` does.
    static MenuIntent intentOf(const UiForm& form, const UiWidget& widget);

private:
    struct Pointer {
        Vec2 position{-1, -1};
        bool down = false;
    };

    void prepare();
    void prepareOptions();
    void prepareStage();
    void prepareScores();
    void prepareEnterName();
    void showVideoMode();
    void setLabel(std::string_view name, std::string text);

    void feed(Vec2 position, bool down, MenuResult& result);
    void click(UiWidget& widget, MenuResult& result);
    void press(UiWidget& widget);
    void pressKey(UiKey key);
    void typeText(std::string_view text);
    bool back();
    void applyOptions();
    void submitName();
    void setSlider(UiWidget& slider, float value);
    void setCheckbox(UiWidget& box, bool checked);
    void step(const UiWidget& button, int direction);
    void selectBat(int bat);
    void changed();
    bool levelOpen(int index) const;
    void sound(const std::string& name) const;
    float fade() const;

    void drawScores(UiAssets& assets, std::vector<Sprite>& out) const;
    int scorePages() const;

    FormLoader loadForm_;
    Campaign campaign_;
    SaveData& save_;
    MenuHooks hooks_;
    bool lockLevels_ = true;
    std::vector<VideoMode> videoModes_;
    int videoMode_ = 0;
    bool fullscreenAvailable_ = true;
    Settings videoAtOpen_;  // the video settings when Options opened, to tell whether Apply has anything to apply

    std::string formName_;
    UiForm form_;
    UiForm backdrop_;
    bool background_ = true;
    bool backdropShown_ = false;
    UiLayout layout_;
    ImageSizeFn imageSizes_;
    double shown_ = 0;       // seconds since the form opened
    double skyTime_ = 0;     // seconds the sky has scrolled; stops with the Freeze Sky setting

    Pointer pointer_;
    bool blockDown_ = false;  // a form just opened with the button held: ignore it until it comes up
    int dragging_ = -1;       // the slider being dragged
    int edit_ = -1;           // the text field that has the keyboard

    int pendingScore_ = 0;    // the score the game ended with, for the name entry and Game Over forms
    std::string pendingLevel_;  // and the level it was made on
    int scorePage_ = 0;
    int newScore_ = -1;       // the rank of the score just entered, highlighted in the table
};

}  // namespace mb
