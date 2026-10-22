#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelScene.h"
#include "mb/game/ModelCache.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mb {

// What the level's special objects look like in play (docs/GAMEPLAY.md section 15): the flash of a charge of dynamite, the
// cannonball and the puff at the muzzle, the rocket of a firework crate and its burst, the ring where a ball goes into a
// teleport or comes out, a spring that gives with a hit, the glow of a flicker. It reads the session (SpecialObjects) and the
// events it reported, and draws billboards and two meshes; it keeps no rules. The full particle systems of the original
// (Data/Particles) come in step 5's last wave: these are a flash, some sparks and a few sprites each.
class ObjectEffects {
public:
    ObjectEffects(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures);

    // Brings the effects up to the session, `seconds` after the last call, with the events since then. `level` is the picture
    // of the level, for the springs that give.
    void update(const GameSession& session, std::span<const GameEvent> events, double seconds, LevelScene& level);

    // What to draw on top of the level and the balls, as of the last update().
    std::span<const DrawItem> items() const { return items_; }

private:
    enum class Kind { Blast, ShotBurst, Muzzle, Portal, Burst };
    struct Flash {
        Kind kind = Kind::Blast;
        Vec3 at{};
        Vec3 direction{};  // Muzzle: the way the cannonball went
        float size = 0;    // Blast: the radius
        double age = 0;
        std::uint32_t seed = 0;
    };
    struct Give {  // a spring that was hit
        ActorIndex actor = kNoActor;
        double age = 0;
    };

    void addBillboard(Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend);
    void addFloor(Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend);
    void addMesh(const LevelScene& model, const Mat4& place);
    void addShadow(Vec3 under, float size);
    void drawFlash(const Flash& flash);
    void drawBlast(const Flash& flash);
    void drawBurst(const Flash& flash);

    LevelScene rocket_, shot_;  // the rocket and the cannonball at the origin
    MeshHandle quad_ = 0;
    TextureHandle blastTexture_ = 0, burstTexture_ = 0, dotTexture_ = 0, glowTexture_ = 0, ringTexture_ = 0, smokeTexture_ = 0;
    Mat4 facing_;  // turns a quad to face the game's camera
    double time_ = 0;
    std::vector<Flash> flashes_;
    std::vector<Give> gives_;
    std::vector<DrawItem> items_;
};

}  // namespace mb
