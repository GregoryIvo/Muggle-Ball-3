#pragma once

#include "mb/engine/Math.h"
#include "mb/engine/Physics.h"
#include "mb/game/Level.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/PhysicsScene.h"
#include "mb/game/Prizes.h"

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace mb {

class GameSession;

// What flies: the shots of the Machine Gun (a bullet from each barrel), the Cannon (a kernel) and the Bazooka (a rocket), and what
// the two strike prizes throw (a bolt of the Thunderbolt, a meteor). The Laser throws no body: its beam is a line (LaserBeam).
enum class ProjectileKind : std::uint8_t { Bullet, Kernel, Rocket, Bolt, Meteor };

// What one kind of projectile is and does (docs/GAMEPLAY.md sections 18.2 to 18.4). The comment says where a number comes from:
// [M] measured from footage, [D] read from the game's data (Data/Objects/Bullets, Interactive/thunderbolt and meteor, the materials
// of the same names), [I] inferred, [?] a guess.
struct ProjectileRules {
    // Where a shot starts, in the bat's frame (x across, y up from the floor, z up the board); a strike's is worked out (18.7). [D]
    Vec3 spawn{};
    Vec3 velocity{};          // [D] in the bat's frame for a shot (+z is up the board), in the world's for a strike
    bool gravity = false;     // [D] the kernel has no disableGravity: it is lobbed
    float radius = 0;         // [D] of its sphere, or of its capsule
    float halfHeight = 0;     // [D] a capsule lies along z, this is half the length of its cylinder (the data's height / 2); 0: a sphere
    float density = 5;        // [D] its material's; its mass is its volume times this
    float friction = 0.7f;    // [D]
    float restitution = 0.3f; // [D]
    // What a hit does (18.4)
    int damage = 1;           // [I] energy it takes off what it hits
    float blastRadius = 0;    // [I] a burst on impact takes `blastDamage` off everything else damageable within this (0: none)
    int blastDamage = 1;      // [I]
    float blastPower = 0.5f;  // [I] and pushes the loose bodies in reach, as a charge of dynamite (SpecialObjectRules::blastSpeed) times this
    bool dropsPrize = true;   // [D] false: disablePrize, what it destroys leaves no prize (the thunderbolt and the meteor)
};

// Every number the weapons and the strikes use (docs/GAMEPLAY.md section 18); the comments are tagged as in GameplayRules.
struct WeaponRules {
    // ---- Firing (18.1)
    // The least time between two shots; a click inside it fires nothing and costs no shot. The Machine Gun's own interval was about
    // 0.2 s at the quickest (4 to 6 shots a second) [M, weak]; no other weapon showed one (the quickest seen were 0.55 s for the
    // Cannon and 0.5 s for the Laser), so all share it [I].
    double shotInterval = 0.2;

    // ---- The shots (18.2, 18.3). The barrels are frames of the bat model, which stands on the floor under the centre of the
    // bat's sphere and is drawn batScale() times its size (docs/GAMEPLAY.md section 16.3): a shot starts that much farther out [I].
    // The Machine Gun's two bullets mirror each other (the data's left and right differ by 0.003: rounding).
    ProjectileRules bullet{.spawn = {2.1556f, 1.19839f, 3.41114f},
                           .velocity = {0, 0, 30},
                           .radius = 0.4f,
                           .halfHeight = 0.221745f,
                           .damage = 1};
    ProjectileRules kernel{.spawn = {0, 1.70413f, 3.06672f},
                           .velocity = {0, 3, 40},
                           .gravity = true,
                           .radius = 0.86775f,
                           // The burst is a flash about 3 units wide in the footage (F48), and the data's exploseyadro flash is 10 across,
                           // which for the dynamite's 5 times the radius is 2: the rule takes the data's.
                           .damage = 3,
                           .blastRadius = 2.0f};
    ProjectileRules rocket{.spawn = {0.00554f, 1.40648f, 4.79101f},
                           .velocity = {0, 0, 52.5f},
                           .radius = 0.45183f,
                           .halfHeight = 0.5f,
                           // "a white-orange star 4-5 units wide" [M], so a radius of 2.25
                           .damage = 2,
                           .blastRadius = 2.25f};

    // ---- The Laser (18.3)
    // lazer.XML: two identical capsules of radius 0.15 at height 1.0, which the game puts at the barrel (Lazer-Stvol, which ends at
    // z 2.91 of bat.x) [D]. The beam is full length within 0.1 s and fades in about 0.45 s [M: 62.78 to 63.26].
    Vec3 laserSpawn{0, 1.0f, 2.91f};
    float laserRadius = 0.15f;
    // How far it reaches if nothing stops it: farther than the board is long.
    float laserRange = 60.0f;
    int laserDamage = 1;  // a pirate (three energy) survived a beam and the planks (one) died [M]
    double laserGrow = 0.1;
    double laserLife = 0.45;

    // ---- What flies after the shot
    double projectileLife = 5.0;  // a projectile that has hit nothing by then is gone: a lob lands within 1 s, so only a stray [I]
    // A projectile this far from the board's middle (or this far below it) has left the world.
    float farLimit = 80.0f;
    float lowLimit = -20.0f;

    // ---- Thunderbolt (18.5). thunderbolt.XML: a sphere that falls at 50 units/s, disablePrize [D]
    ProjectileRules bolt{.velocity = {0, -50, 0}, .radius = 0.49178f, .density = 10, .friction = 0.9f, .restitution = 0.5f,
                         .damage = 1, .dropsPrize = false};
    // A catch gives a burst of 2 or 3 bolts within 0.7 s at different places [M]; each is thrown `boltGap` after the one before, so
    // that they strike one after another within that time though they fall for 0.3 to 0.6 s [I]. (The footage's bolts are on show for
    // about 0.3 s each and overlap: V2 408.0 to 408.7, GAMEPLAY.md 18.12.)
    int boltBurstLeast = 2, boltBurstMost = 3;
    double boltGap = 0.2;
    // and more strikes later, up to about 12 s after the catch (8 and 12 s in V2, 0.5 s and 7 s after the caption in V1 level 2) [M,
    // weak]: this many single bolts, each at a moment drawn evenly between the two times, seconds after the catch [I]
    int boltsLater = 2;
    double boltLaterFrom = 2.0, boltLaterUntil = 12.0;

    // ---- Meteorite Rain (18.6). meteor.XML: a sphere with the velocity (-10, -30, 10), disablePrize [D]
    ProjectileRules meteor{.velocity = {-10, -30, 10},
                           .radius = 0.49178f,
                           .density = 10,
                           .friction = 0.9f,
                           .restitution = 0.5f,
                           .damage = 2,
                           // explosemeteor's flash is 10 across: a radius of 2 as for the cannonball [D, I]
                           .blastRadius = 2.0f,
                           .dropsPrize = false};
    // About five meteors over five seconds [M]: meteors, one in each fifth of the span, at a moment of its own [I]
    int meteors = 5;
    double meteorSpan = 5.0;
    // Where they come down: a point of the board drawn evenly from this rectangle (the rings were seen on the water [M]) [I]
    float meteorX = 18.0f;
    float meteorZLow = -10.0f, meteorZHigh = 20.0f;

    // ---- Where from (18.7): a bolt or a meteor starts this far beyond the top edge of the screen [I]
    float offscreen = 2.0f;
};

// ---- State the presentation reads

struct Projectile {
    int id = 0;  // unique in the session
    ProjectileKind kind = ProjectileKind::Bullet;
    PrizeKind prize = PrizeKind::Gun;  // what threw it: a weapon, or Thunderbolt or Meteor
    Vec3 position{};                   // of its centre
    Vec3 velocity{};
    double bornAt = 0;                 // GameSession::time() when it left
    double age = 0;                    // seconds since
    int barrel = 0;                    // the Machine Gun's: 0 the left, 1 the right
    BodyId body = 0;                   // its body in the physics world
};

// The Laser's beam, from the barrel to where it ends: it grows to its length within WeaponRules::laserGrow and is gone WeaponRules::laserLife
// after the shot.
struct LaserBeam {
    Vec3 from{};
    Vec3 to{};
    double firedAt = 0;  // GameSession::time()
    int hits = 0;        // the actors it reached
};

// Where a point of the bat's frame is on the board (x across, y up from the floor, z up the board): the bat's model stands on the floor
// under the centre of the bat's sphere (y = `ballPlaneY` - kBallRadius there) and is drawn `batScale` times its size. A shot's spawn point is
// one, so the weapons and whoever aims them (the Autopilot) work it out the same way.
inline Vec3 onBat(float ballPlaneY, Vec3 batPosition, float batScale, Vec3 offset) {
    return Vec3{batPosition.x, ballPlaneY - kBallRadius, batPosition.z} + offset * batScale;
}

// The height at which the vertical line through `below` leaves the picture at its top edge, as the game's camera
// (GameCamera.h) sees it: where a thunderbolt starts above a target. The window's aspect makes no difference to the top edge.
float screenTopHeight(Vec3 below);
// Where a point that moves along `direction` towards `target` last was in the picture: the point on that line (through `target`) at the
// top edge of the screen, where a meteor starts.
Vec3 screenTopPoint(Vec3 target, Vec3 direction);

// The weapons and the strikes of the prizes (docs/GAMEPLAY.md section 18): GameSession owns one, a friend of it, and calls it at three
// places: a click that launches no ball and lets none go (`fire`), the catch of a Thunderbolt or a Meteorite Rain (`startStrike`) and once
// a fixed step after the contacts (`update`). It reports through the session's events (ShotFired, ProjectileHit, ProjectileExploded,
// ProjectileGone, LaserBeam, BoltStruck, MeteorFalling, MeteorImpact) and keeps the state the picture reads. Projectiles are
// bodies of the session's PhysicsScene (PhysicsScene::addProjectile): they touch the level's actors and nothing else, so they go through
// the balls, the bat, each other and the teleports. What they do to an actor goes through SpecialObjects (weaponHit, burst), so
// that dynamite, cannons, firework crates and the rest are hurt as docs/GAMEPLAY.md section 17.1 says.
class Weapons {
public:
    // `seed` makes the choices of the strikes (how many bolts, where and when) repeat.
    Weapons(const MaterialLibrary& materials, const WeaponRules& rules, std::uint64_t seed);

    // A click that no held ball or Magnet takes: fires one shot of the weapon caught last (GameSession::weapon()) if the bat is there
    // and the balls are in play, and not too soon after the last shot. The shot is counted (GameSession::useShot()). Returns whether
    // anything was fired.
    bool fire(GameSession& session);
    // The catch of a Thunderbolt or a Meteorite Rain: its strikes are put off to the times the rules give.
    void startStrike(GameSession& session, PrizeKind kind);
    // Once a fixed step, after the physics: throws the strikes that are due, lets the projectiles that hit something do their work,
    // takes out the ones that are gone and the beams that have faded.
    void update(GameSession& session);

    // In flight now, in the order they left.
    std::span<const Projectile> projectiles() const { return projectiles_; }
    // The beams that have not faded, oldest first.
    std::span<const LaserBeam> beams() const { return beams_; }
    // Strikes (bolts and meteors) that are still to be thrown.
    int strikesPending() const { return static_cast<int>(pending_.size()); }

    struct Stats {
        int shots = 0;         // shots fired (clicks that fired)
        int projectiles = 0;   // bodies made, strikes included
        int hits = 0;          // projectiles that hit something
        int gone = 0;          // and that left the board or ran out of time
        int beams = 0;         // laser shots
        int beamReached = 0;   // actors the beams reached
        int bolts = 0, meteors = 0;  // thrown
        int hurt = 0;          // actors that something here took energy from
    };
    const Stats& stats() const { return stats_; }

private:
    struct Strike {
        double at = 0;  // GameSession::time() it is thrown at
        PrizeKind kind = PrizeKind::Thunderbolt;
        int burst = -1;  // the number of the catch for the bolts that strike together, -1 for the others
    };

    WeaponRules rules_;
    PrizeRandom random_;
    std::array<const GameMaterial*, 5> materials_{};  // by ProjectileKind
    const GameMaterial* laserMaterial_ = nullptr;
    std::vector<Projectile> projectiles_;
    std::vector<LaserBeam> beams_;
    std::vector<Strike> pending_;
    std::vector<ActorIndex> candidates_;
    std::vector<std::pair<int, ActorIndex>> burstTargets_;  // what the bolts of a burst that are thrown already struck, by burst
    int nextBurst_ = 0;
    double lastShot_ = -1e9;
    int nextId_ = 0;
    bool over_ = false;  // the level is done: nothing flies any more
    Stats stats_;

    const ProjectileRules& rulesOf(ProjectileKind kind) const;
    const GameMaterial* materialOf(ProjectileKind kind) const { return materials_[static_cast<std::size_t>(kind)]; }
    // Makes the body and the entry; false if the world refused it.
    bool launch(GameSession& session, ProjectileKind kind, PrizeKind prize, Vec3 position, Vec3 velocity, int barrel = 0);
    void fireLaser(GameSession& session, Vec3 from);
    void throwBolt(GameSession& session, int burst);
    void throwMeteor(GameSession& session);
    // The projectile at `index` touched `actor` at `at`: it is spent, and does its work.
    void land(GameSession& session, std::size_t index, ActorIndex actor, Vec3 at);
    // Hurts the actor and counts it; `by` is the weapon or strike (what BrickDestroyed names if this destroys it).
    bool hurt(GameSession& session, ActorIndex actor, Vec3 at, int damage, bool dropsPrize, PrizeKind by);
    void clearAll(GameSession& session);
};

}  // namespace mb
