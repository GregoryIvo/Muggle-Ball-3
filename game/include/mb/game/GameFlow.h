#pragma once

#include "mb/game/Campaign.h"
#include "mb/game/Hud.h"
#include "mb/game/Menu.h"

#include <optional>

namespace mb {

// Where the player is in the game, and the way between the menus and a level:
//
//   Menu     a menu form is up; choosing a level (or Quick Start) moves on to
//   Loading  the Loading form while the app builds the level's scene; then
//   Playing  the level with the HUD; Escape pauses it
//   Paused   "PAUSE": Escape goes back to the main menu, any other key or a mouse button resumes
//
// It holds no GPU or window objects, so the whole flow runs in tests. The app drives it once a frame:
//
//   GameFlow::Step step = flow.update(input, dt, layout, imageSizes);
//   if (step.loadLevel) { ... build the scene for *step.loadLevel ...; flow.loaded(ok); }
//   then draw by flow.mode(): the menu, or the level with the HUD (and the pause screen over it).
class GameFlow {
public:
    enum class Mode { Menu, Loading, Playing, Paused };

    struct Step {
        bool quit = false;                // the Exit button was clicked
        std::optional<int> loadLevel;     // build this level (a campaign index), then call loaded()
    };

    GameFlow(Menu& menu, const Campaign& campaign) : menu_(menu), campaign_(campaign) {}

    Mode mode() const { return mode_; }
    // The campaign index of the level being loaded or played.
    int level() const { return level_; }
    // What the HUD shows; set to the placeholders when a level starts, then the game's to update.
    GameHud& hud() { return hud_; }
    const GameHud& hud() const { return hud_; }

    Step update(const UiInput& input, double seconds, const UiLayout& layout, const ImageSizeFn& imageSizes);
    // The answer to Step::loadLevel. A level that failed to load sends the player back to its stage's form.
    void loaded(bool ok);

private:
    Menu& menu_;
    const Campaign& campaign_;
    Mode mode_ = Mode::Menu;
    int level_ = -1;
    bool loadAsked_ = false;
    bool loadDone_ = false;
    bool pointerDown_ = false;  // the left button as of the last pointer event seen while a level was up
    GameHud hud_;
};

}  // namespace mb
