#pragma once

#include "mb/engine/Renderer.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelScene.h"
#include "mb/game/ModelCache.h"
#include "mb/game/ParticleSystem.h"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mb {

// What the level's special objects look like in play (docs/GAMEPLAY.md sections 17 and 19): the flash of a charge of dynamite,
// the cannonball and its burst, the smoke at the muzzle, the rocket of a firework crate with its trail and its burst, the ring
// where a ball goes into a teleport or comes out, a spring that gives with a hit, the flame and smoke of a flicker, the leaves
// of a tree that is hit. The ones the data names a particle file for are the particle system's (the shared one PlayScene owns:
// they are spawned there and drawn with all the rest); what is left is a few billboards and two meshes. It reads the session
// (SpecialObjects) and the events it reported; it keeps no rules.
class ObjectEffects {
public:
    // `level` is the one the session plays, for the particle files its objects name (the dynamite's blast, the flickers' fire).
    ObjectEffects(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, const Level& level,
                  ParticleSystem& particles);

    // Brings the effects up to the session, `seconds` after the last call, with the events since then. `level` is the picture
    // of the level, for the springs that give.
    void update(const GameSession& session, std::span<const GameEvent> events, double seconds, LevelScene& level);

    // What to draw on top of the level and the balls, as of the last update().
    std::span<const DrawItem> items() const { return items_; }

private:
    enum class Kind { Muzzle, Portal };
    struct Flash {
        Kind kind = Kind::Muzzle;
        Vec3 at{};
        Vec3 direction{};  // Muzzle: the way the cannonball went
        double age = 0;
    };
    struct Give {  // a spring that was hit
        ActorIndex actor = kNoActor;
        double age = 0;
    };

    // What a flicker's fire is made of: the particle effects it emits and where its flame stands above its centre.
    struct Fire {
        std::vector<EffectId> effects;
        float lift = 0;
    };

    void addBillboard(Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend);
    void addFloor(Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend);
    void addMesh(const LevelScene& model, const Mat4& place);
    void addShadow(Vec3 under, float size);
    void drawFlash(const Flash& flash);
    void followRockets(const SpecialObjects& objects);
    void followFires(const GameSession& session);
    std::string_view blastEffect(const GameEvent& event) const;
    std::string burstEffect(const GameEvent& event) const;

    LevelScene rocket_, shot_;  // the rocket and the cannonball at the origin
    ParticleSystem& particles_;
    MeshHandle quad_ = 0;
    TextureHandle burstTexture_ = 0, glowTexture_ = 0, ringTexture_ = 0;
    Mat4 facing_;  // turns a quad to face the game's camera
    double time_ = 0;
    std::vector<std::string> blastTargets_;  // by ActorIndex: the particle file a charge of dynamite names
    std::vector<std::vector<std::string>> fireNames_;  // by ActorIndex: the files a flicker names
    std::unordered_map<ActorIndex, EffectId> trails_;  // by crate: the trail of the rocket in the air
    std::unordered_map<ActorIndex, Fire> fires_;       // by flicker
    std::vector<Flash> flashes_;
    std::vector<Give> gives_;
    std::vector<DrawItem> items_;
};

}  // namespace mb
