#pragma once

#include "mb/engine/Renderer.h"
#include "mb/engine/Window.h"
#include "mb/game/Campaign.h"
#include "mb/game/Hud.h"
#include "mb/game/Menu.h"
#include "mb/game/UiAssets.h"
#include "mb/game/UiInput.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mb_app {

// What the front end needs from the code that owns the 3D scene (today the level viewer in main.cpp; later the game
// with its physics and rules). The front end decides when a level starts, pauses and ends; the session does the
// work of the level.
class LevelSession {
public:
    virtual ~LevelSession() = default;

    // Reports how far a load has got, 0 to 1; each call redraws the Loading form.
    using Progress = std::function<void(float)>;

    // Builds the scene for this level (a name from the campaign, any case); false if it cannot be shown, with the
    // reason logged. Blocks until the level is ready; `progress` may be called any number of times while it works.
    virtual bool start(std::string_view levelName, const Progress& progress) = 0;
    // One frame of play, `seconds` after the last: not called while paused. `input` is what the window polled.
    // Changes to `hud` (lives, points, bonuses) show at once.
    virtual void update(const mb::InputEvents& input, double seconds, mb::GameHud& hud) = 0;
    // Draws the scene into the window with the 2D overlay (the HUD, the pause screen) on top.
    virtual void render(mb::Renderer& renderer, std::span<const mb::Sprite> overlay) = 0;
};

struct FrontEndOptions {
    std::string language;  // a language of the string table; empty keeps the game's default
    // The hooks other parts of the program set: sound for playSound, saving for loadProfile and saveProfile.
    // runFrontEnd adds the window ones (applyVideo) to what is set here.
    mb::MenuHooks hooks;
};

// The game as the player meets it: the main menu on the window, from which levels are started, played with the
// HUD, paused and left. Returns when the player quits or closes the window.
int runFrontEnd(mb::Window& window, mb::Renderer& renderer, LevelSession& session, const FrontEndOptions& options);

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
