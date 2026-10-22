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
//   Playing  the level with the HUD; Escape pauses it, unless the level is over and fading out (setLevelEnding)
//   Paused   "PAUSE": Escape goes back to the main menu, any other key or a mouse button resumes
//
// A game starts when a level is chosen, with the hearts of a new game and no points; the hearts and points carry
// from one level to the next in the HUD (the level's own code changes them as it plays). When a level ends, the app
// says how (levelEnded()): a level that was won is recorded in the save data and the next level of the campaign
// follows at once, with no Loading form (the original has none); after the last one come the ending forms. A lost game
// goes to the Game Over form, or to the name entry if the score makes the table, and so to the high scores.
//
// It holds no GPU or window objects, so the whole flow runs in tests. The app drives it once a frame:
//
//   GameFlow::Step step = flow.update(input, dt, layout, imageSizes);
//   if (step.loadLevel) { ... build the scene for *step.loadLevel ...; flow.loaded(ok); }
//   then draw by flow.mode(): the menu, or the level with the HUD (and the pause screen over it);
//   while it plays: if (outcome != Playing) { step = flow.levelEnded(outcome); ... the same ... }
// How a level that is being played stands.
enum class LevelOutcome {
    Playing,
    Complete,  // the last target is gone
    GameOver,  // the last ball is lost with no heart left
};

class GameFlow {
public:
    enum class Mode { Menu, Loading, Playing, Paused };

    struct Step {
        bool quit = false;                // the Exit button was clicked
        std::optional<int> loadLevel;     // build this level (a campaign index), then call loaded()
        // The pause ended in this frame. What ended it (a click, Space) was the player's way back, not a move in the level:
        // the level must not see this frame's input, or a click would launch the ball.
        bool resumed = false;
    };

    // `save` is the one the menu works on: the flow records the progress and the scores of the levels played.
    GameFlow(Menu& menu, const Campaign& campaign, SaveData& save) : menu_(menu), campaign_(campaign), save_(save) {}

    Mode mode() const { return mode_; }
    // The campaign index of the level being loaded or played.
    int level() const { return level_; }
    // What the HUD shows. A new game sets its hearts and no points; the game updates it as the level plays, and what it
    // shows when the level ends is what the next level starts with.
    GameHud& hud() { return hud_; }
    const GameHud& hud() const { return hud_; }

    // Whether the level being played is over, won or lost, and fading out: its outcome is on the way to levelEnded(). The
    // app says so every frame; Escape does not pause a level that is ending, because leaving from the pause would lose
    // the outcome (the win never recorded, the game over never reaching the name entry).
    void setLevelEnding(bool ending) { levelEnding_ = ending; }

    Step update(const UiInput& input, double seconds, const UiLayout& layout, const ImageSizeFn& imageSizes);
    // Starts a new game at this level (a campaign index) as choosing it in the menus does; for a program that goes to a
    // level without them.
    void startGame(int level);
    // The answer to Step::loadLevel. A level that failed to load sends the player back to its stage's form.
    void loaded(bool ok);
    // How the level being played ended: won or lost (Playing changes nothing). The app calls it once the level's own end
    // is over, with its fade. A level that was won is recorded; the Step asks for the next level to be built (answer it
    // with loaded() as any other), or is empty where the menus come back: the ending after the last level, Game Over or
    // the name entry after a lost game.
    Step levelEnded(LevelOutcome outcome);

private:
    Menu& menu_;
    const Campaign& campaign_;
    SaveData& save_;
    Mode mode_ = Mode::Menu;
    int level_ = -1;
    int levelStartScore_ = 0;  // the points the player brought into the level
    bool loadAsked_ = false;
    bool loadDone_ = false;
    bool advancing_ = false;  // the next level is being built from within Playing
    bool levelEnding_ = false;
    bool pointerDown_ = false;  // the left button as of the last pointer event seen while a level was up
    GameHud hud_;
};

}  // namespace mb
