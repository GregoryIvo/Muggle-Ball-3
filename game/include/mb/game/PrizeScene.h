#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelScene.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/ModelCache.h"
#include "mb/game/TextureIndex.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mb {

// What a ball is drawn as: the Power Ball and the Explosive Ball have models of their own (ball-powerfull.x, ball-explosive.x).
enum class BallLook : std::uint8_t { Plain, Power, Explosive };

// How the prizes look (docs/GAMEPLAY.md section 16): the orbs falling down the board, turning, each in its halo; what happens
// where one is caught, where one is missed and where the Explosive Ball goes off; the models of the balls that are not plain;
// and what the effects add to the bat and the board (the Magnet's horseshoe, the Wall's rope, the Yo-Yo's string). PlayScene calls it; it reads the session and draws nothing by itself. The night's light is at the end.
class PrizeScene {
public:
    // `batMaterial` is the texture of the player's bat (Settings::bat), which the Magnet's horseshoe shares.
    PrizeScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, std::string_view batMaterial);

    // The flashes of what the events report (a prize caught or missed, a blast), `seconds` after the last call.
    void update(std::span<const GameEvent> events, double seconds);

    // The orbs as the session has them, `lead` seconds past its last step (they fall on at their speed), with their halos, and
    // the flashes where prizes were caught or missed and the Explosive Ball went off.
    void appendPrizes(const GameSession& session, double lead, std::vector<DrawItem>& out) const;
    // The Magnet's horseshoe on the bat; `place` puts the bat's model where it is.
    void appendMagnet(const GameSession& session, const Mat4& place, std::vector<DrawItem>& out) const;
    // The Wall's rope along the near edge, rising as the effect starts and sinking as it ends.
    void appendWall(const GameSession& session, std::vector<DrawItem>& out) const;
    // The Yo-Yo's string between two points.
    void appendString(Vec3 from, Vec3 to, std::vector<DrawItem>& out) const;

    // The look the session's balls have: the Power Ball's wins over the Explosive Ball's.
    static BallLook ballLook(const GameSession& session);
    // The draws of one ball of that look at the origin, as big as the plain ball's model, to be placed like it (the plain
    // ball's are LevelScene::ballItems()).
    std::span<const DrawItem> ballItems(BallLook look) const { return look == BallLook::Power ? powerBall_ : explosiveBall_; }

private:
    // Where a prize's model stands: the middle of its box goes to the orb's centre.
    struct Fit {
        Vec3 centre{};
        float low = 0;  // the lowest point, for keeping it above the floor
    };
    struct Flash {
        Vec3 at{};
        double age = 0;
        float size = 0;  // Blast: across; Pop: the halo's
        bool bad = false;
    };

    void addHalo(Vec3 at, bool bad, double age, std::vector<DrawItem>& out) const;
    void addPrize(const FallingPrize& prize, Vec3 at, std::vector<DrawItem>& out) const;
    void addFizzle(const Flash& flash, std::vector<DrawItem>& out) const;
    void addBlast(const Flash& flash, std::vector<DrawItem>& out) const;
    void addPop(const Flash& flash, std::vector<DrawItem>& out) const;

    LevelScene props_;  // every model at the origin, one actor each: the prizes in the order of PrizeKind, then the extras
    std::array<Fit, kPrizeKindCount> fit_{};
    std::vector<DrawItem> powerBall_, explosiveBall_;

    MeshHandle quad_ = 0;
    Mat4 facing_;
    Vec3 forward_;  // along the line of sight
    std::array<TextureHandle, 2> glow_{}, mark_{};  // good, bad
    TextureHandle ring_ = 0, dot_ = 0, star_ = 0;
    std::vector<Flash> pops_, fizzles_, blasts_;
};

// The scene's light at night, 0 (day) to 1 (full night): the board's brightness falls to 0.38 of itself and the sky's to 0.49
// (docs/GAMEPLAY.md section 14.3 [M]).
DirectionalLight nightLight(const DirectionalLight& day, float night);
// The same for what has no light of its own (the sky, the halos, the flashes): their colour is dimmed, by this share of itself
// (1 by day) ...
float unlitNightShare(float night);
// ... as dimUnlit does to draws.
void dimUnlit(std::span<DrawItem> items, float night);

}  // namespace mb
