#pragma once

#include "mb/engine/Renderer.h"
#include "mb/engine/Window.h"
#include "mb/game/Campaign.h"
#include "mb/game/GameFlow.h"
#include "mb/game/Hud.h"
#include "mb/game/Menu.h"
#include "mb/game/SaveData.h"
#include "mb/game/UiAssets.h"
#include "mb/game/UiInput.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mb_app {

// What the front end needs from the code that owns the 3D scene (PlaySession: the game's physics and rules). The front
// end decides when a level starts and pauses; the session does the work of the level, and says when it is over.
class LevelSession {
public:
    virtual ~LevelSession() = default;

    // Reports how far a load has got, 0 to 1; each call redraws the Loading form.
    using Progress = std::function<void(float)>;

    // Builds the scene for this level (a name from the campaign, any case); false if it cannot be shown, with the
    // reason logged. Blocks until the level is ready; `progress` may be called any number of times while it works.
    // `hud` has the lives and the points the player brings into the level (a new game's, or those of the level before).
    virtual bool start(std::string_view levelName, const mb::GameHud& hud, const Progress& progress) = 0;
    // One frame of play, `seconds` after the last: not called while paused. `input` is what the window polled.
    // Changes to `hud` (lives, points, bonuses) show at once. Returns how the level ended once it is over (its own
    // fade out included), else Playing; the front end then moves on and does not call update() again for this level.
    virtual mb::LevelOutcome update(const mb::InputEvents& input, double seconds, mb::GameHud& hud) = 0;
    // Whether the level is over, won or lost, and fading out: update() reports how once the fade is done. The front end
    // does not pause a level that is ending, so that its outcome is not held back, or lost by leaving from the pause.
    virtual bool ending() const = 0;
    // The pause is over and the level goes on. The input of the frame that ended it (a click, Space) was the player's way
    // back, not a move in the level: update() has not been given it, and a button that is down now is no click.
    virtual void resume() = 0;
    // Draws the scene into the window with the 2D overlay (the HUD, the pause screen) on top.
    virtual void render(mb::Renderer& renderer, std::span<const mb::Sprite> overlay) = 0;
};

// What the front end tells the rest of the program as the game moves between the menus, a level and the pause
// screen (the app plays the music and pauses the sound from it). All are optional.
struct FrontEndEvents {
    // The menus are up: at the start, and again when the player comes back from a level or a level cannot be built.
    std::function<void()> menuShown;
    // A level is ready and play begins; `levelName` is the campaign's name for it (any case).
    std::function<void(std::string_view levelName)> levelStarted;
    // The pause screen came up (true) or went away (false, before the menus or the level go on).
    std::function<void(bool paused)> paused;
    // Once per frame, for clocks.
    std::function<void()> frame;
};

struct FrontEndOptions {
    std::string language;  // a language of the string table; empty keeps the game's default
    // The hooks other parts of the program set: sound for playSound, the volume for settingsChanged (called once at
    // the start with the saved settings, too). runFrontEnd adds the window ones (applyVideo) to what is set here.
    mb::MenuHooks hooks;
    FrontEndEvents events;
    // Whether the stage screens lock the levels the saved progress has not opened (Menu::setLevelsLocked).
    bool lockLevels = true;
    // A level's name (a campaign's, any case): the game opens on it as if it had been chosen in the menus. Empty opens
    // the main menu; a name the campaign does not have is logged and ignored.
    std::string startLevel;
    // Whether the system cursor is hidden and kept in the window while a level plays, because the pointer is the bat. Off
    // where nobody plays with it (--autoplay), so that the pointer stays the player's.
    bool hideCursor = true;
};

// The game as the player meets it: the main menu on the window, from which levels are started, played with the
// HUD, paused and left; a level that is won leads to the next, one that is lost to the high scores. The menus show and
// change `save` (options, bat, progress, high scores) and write it. The system cursor is hidden while a level plays
// (FrontEndOptions::hideCursor). Open the window with its video settings: from then on the settings follow the window as
// the player resizes it or goes fullscreen, and Options moves the window to the choices made there. Returns when the
// player quits or closes the window.
int runFrontEnd(mb::Window& window, mb::Renderer& renderer, LevelSession& session, mb::SaveData& save,
                const FrontEndOptions& options);

// One frame of the window's input as the menus take it.
mb::UiInput toUiInput(const mb::InputEvents& input);

// `--hud`: the in-game overlay on a level that is shown on its own (screenshots, the level viewer).
struct HudOptions {
    bool enabled = false;
    std::optional<mb::GameHud> state;  // --hud-state; otherwise the placeholders: 3 lives, no points, the level's number
};

// "lives,score,level" and then any number of bonus icon names: "3,1529,1" or "5,768915,62,Cannon,Magnit". False if
// the text is not that.
bool parseHudState(std::string_view text, mb::GameHud& hud);

class HudOverlay {
public:
    // Loads nothing unless the HUD is enabled.
    HudOverlay(mb::Renderer& renderer, const HudOptions& options);

    // The overlay for a target of this size, empty when the HUD is off. `levelName` is the level on screen (a
    // catalog name or a campaign name), for the number the placeholder HUD shows.
    std::vector<mb::Sprite> sprites(int width, int height, std::string_view levelName);

private:
    std::optional<mb::UiAssets> assets_;
    std::optional<mb::Campaign> campaign_;
    std::optional<mb::GameHud> fixed_;
};

}  // namespace mb_app
