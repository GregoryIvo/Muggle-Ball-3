#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/UiAssets.h"

#include <string>
#include <vector>

namespace mb {

// What the in-game overlay shows. The game fills it in as play goes on; until it does (step 4: balls, bat, bonuses)
// the app sets the placeholders below.
struct GameHud {
    int lives = 3;   // hearts, down the left edge
    int score = 0;   // "POINTS : n", bottom left
    int level = 1;   // "LEVEL : n", bottom right: the level's place in the campaign, counting from 1
    // The bonuses in effect, oldest first, as the stem of their icon in Textures/Icons: "Cannon", "Gun", "Lazer",
    // "Magnit", "Bazuka", "Night", "Powerful", "Rubber", "big", "drunken", "wall", "weak".
    std::vector<std::string> powerUps;
};

// Appends the sprites of the in-game overlay for a target of this size, in pixels, over the 3D scene: a heart for
// each life at the top left, the points and the level number along the bottom (over the purple band the levels
// end in), the icons of the active bonuses down the top right. Placed like the original's 800x600 screenshots,
// scaled to the target's height and kept to the target's edges (the menus' 4:3 area would put them mid-screen on
// a wide window).
void drawHud(UiAssets& assets, const GameHud& hud, int width, int height, std::vector<Sprite>& out);

// Appends the pause screen: the scene dimmed, "PAUSE" and the two ways out ("Press any key or mouse button to
// continue...", "Press ESCAPE to main menu..."), from the string table.
void drawPause(UiAssets& assets, int width, int height, std::vector<Sprite>& out);

}  // namespace mb
