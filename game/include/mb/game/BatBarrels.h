#pragma once

#include "mb/engine/Model.h"
#include "mb/engine/Renderer.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelScene.h"
#include "mb/game/ModelCache.h"
#include "mb/game/Prizes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace mb {

// What a weapon's barrel is doing on the bat (docs/GAMEPLAY.md section 18.11).
enum class BarrelPhase : std::uint8_t {
    Hidden,     // the weapon is not the one a click fires
    Growing,    // it was caught: the barrel swells to full size and the gun pushes out of it
    Ready,      // it stands
    Recoiling,  // a shot
    Shrinking,  // the weapon ended, or another one was caught: it draws back and shrinks away
    Receding,   // the weapon went while the barrel was still growing: the growth played backwards, so that nothing jumps
};

// One barrel's place in the "Common" animation of bat.x, a single take of 4 seconds that the original's artist keyed for all the
// weapons at once: each grows from a tenth of its size and pushes out its gun, waits, recoils once and settles, waits, and draws
// back and shrinks away (the keys of all but the Cannon's end at 3.5 s). The times are the data's (in the keys of the file); cutting the take into a pose that waits, a recoil that
// can be played again and a shrinking that ends the weapon is a choice (docs/GAMEPLAY.md 18.11).
struct BarrelClip {
    double grown;       // the end of the growing: the barrel stands in the pose it waits in
    double recoilFrom;  // where the barrel starts to move back, and
    double recoilTo;    // where it has settled again
    double shrinkFrom;  // where it starts to draw back and shrink, and
    double shrinkTo;    // where it is gone
};
BarrelClip barrelClip(WeaponType type);

// The time of one barrel's animation, as the session's weapons come and go and shoot. It knows nothing of the model.
//
//     BarrelClock clock(WeaponType::Cannon);
//     clock.update(true, dt);   // every frame, `wanted`: the cannon is the weapon a click fires
//     clock.fired();           // on each ShotFired of the cannon
//     pose the barrel at clock.time() seconds into "Common" unless clock.phase() is Hidden
class BarrelClock {
public:
    explicit BarrelClock(WeaponType type) : clip_(barrelClip(type)) {}

    // Brings it on by `seconds`. A weapon that is wanted grows, and waits; one that is not draws back (a recoil in progress is
    // played to its end first, so that the last shot of a weapon is seen). A barrel that is still growing when its weapon goes
    // grows backwards from where it is, and grows on from there if the weapon comes back; one that is drawing back and is wanted
    // again starts to grow from the beginning.
    void update(bool wanted, double seconds);
    // A shot: the barrel recoils from the start of its recoil (from where it is, if it is already recoiling). A barrel that is
    // growing, drawing back or not shown has no recoil. A shot is told before the update of its frame, whose `wanted` may
    // already be false because it was the last: see BarrelClocks.
    void fired();

    BarrelPhase phase() const { return phase_; }
    // Seconds into the animation to pose the barrel at; meaningless while Hidden.
    double time() const { return time_; }

private:
    BarrelClip clip_;
    BarrelPhase phase_ = BarrelPhase::Hidden;
    double time_ = 0;
};

// The four clocks as the session drives them, with no model in it (tested without a GPU): in a frame the shots of the events come
// first and the update after, so that the last shot of a weapon, whose weapon is gone from the session by the time its event is
// seen, still makes its barrel recoil before it draws back.
class BarrelClocks {
public:
    BarrelClocks() : clocks_{BarrelClock(WeaponType::Gun), BarrelClock(WeaponType::Cannon), BarrelClock(WeaponType::Bazooka), BarrelClock(WeaponType::Laser)} {}

    // `armed`: the weapon a click fires now (GameSession::weapon()), if any.
    void update(std::optional<WeaponType> armed, std::span<const GameEvent> events, double seconds);

    const BarrelClock& operator[](WeaponType type) const { return clocks_[static_cast<std::size_t>(type)]; }

private:
    std::array<BarrelClock, kWeaponTypeCount> clocks_;
};

// The weapons' barrels on the bat: frames of bat.x (Pulemet-Left and -Right with their Stvol, Pushka, Bazuka-Korpus with its Stvol,
// lazer-Korpus with its Lazer-Stvol) that are drawn only while their weapon is the one a click fires, moved by the model's own
// "Common" animation (Model::animations: the converter exports it). PlayScene calls it; it reads the session and the events and draws
// nothing by itself.
class BatBarrels {
public:
    // `batMaterial` is the texture of the player's bat (Settings::bat), which the barrels share with the rest of it.
    BatBarrels(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, std::string_view batMaterial);

    // Brings the barrels on by `seconds`: the weapon the session would fire (GameSession::weapon()) grows and the others draw back;
    // each ShotFired of the events makes its barrel recoil.
    void update(const GameSession& session, std::span<const GameEvent> events, double seconds);

    // The draws of the barrels that are showing, at the bat model's own origin: `place` puts the bat's model where it is, as for
    // the rest of the bat. Nothing when the model has no "Common" animation (assets converted before the converter wrote them).
    void append(const Mat4& place, std::vector<DrawItem>& out) const;

    BarrelPhase phase(WeaponType type) const { return clocks_[type].phase(); }
    double time(WeaponType type) const { return clocks_[type].time(); }
    // Whether the model has the animation to show the barrels with.
    bool animated() const { return animation_ != nullptr; }

private:
    LevelScene frames_;  // one actor for each frame the weapons use, at the model's rest pose
    const Model* model_ = nullptr;
    const ModelAnimation* animation_ = nullptr;
    BarrelClocks clocks_;
};

}  // namespace mb
