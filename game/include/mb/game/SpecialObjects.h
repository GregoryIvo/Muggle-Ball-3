#pragma once

#include "mb/engine/Math.h"
#include "mb/game/Level.h"
#include "mb/game/PhysicsScene.h"
#include "mb/game/Prizes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mb {

class GameSession;

// Every number the level's special objects use (docs/GAMEPLAY.md section 17, built on the footage of section 14). The
// comment says where it comes from: [M] measured from footage, [D] read from the game's data, [I] inferred, [?] a guess.
struct SpecialObjectRules {
    // ---- What hurts what (17.1)
    // The ball hurts bricks, trees, dynamite, firework crates, vibros and flickers. The rest of the level's objects (cannons,
    // springs, volcanoes, generators and metal) react to it and are hurt by blasts and cannonballs (and by whatever a weapon
    // does through GameSession::hit()). A blast and a cannonball take this much energy off what they reach: what has one or
    // two (metal, dynamite, fireworks, cannons, the springs, volcanoes and generators) is destroyed, a person's three are not
    // [I: the log that the chain of V1 8192.9 pushed lived; the cannon's energy 2 [D] is then one cannonball].
    int blastDamage = 2;
    int cannonballDamage = 2;

    // ---- Dynamite (17.2)
    // The blast radius when the level gives none that makes sense (it is 5 for the big charge and 3 for the small [D]).
    float defaultBlastRadius = 5.0f;
    // A charge that a blast reaches goes off this long after it, whatever the material's exploseDelay (100 ms for one a
    // ball hits): one fixed step. The flash ran from the first of the five charges of V1 8192.76-8192.90 to the last
    // in 0.07 s, four links of 17 ms; the footage's 20 to 30 ms are frames of 33 ms [M].
    double chainDelay = 1.0 / 60;
    // What a blast reaches is what its sphere overlaps: the distance from its centre to the box round the body is at most
    // the radius [I; the footage brackets the reach of centre to centre between 4.0 and 6.9 units for the radius 5, and a
    // charge is 1.8 wide]. A loose body is pushed away from the centre, faster the nearer it is, by up to blastSpeed times
    // the material's explosePower [D: 1.0 for the dynamite], and upward by blastLift of that. Heavy bodies are pushed less:
    // by the square root of blastReferenceMass over their mass, up to the full push [I].
    float blastSpeed = 14.0f;
    float blastLift = 0.5f;
    float blastReferenceMass = 100.0f;

    // ---- Interactive cannon (17.3)
    // The cannonball leaves the muzzle with the velocity yadro.XML gives, in the cannon's frame (+x is its forward) [D;
    // 29.4 to 33.3 units/s measured, 31 on average [M]], and falls under the level's gravity: a lob that lands about 26
    // units away on level ground [I; the footage shows no arc over 20 units, which a lob of this kind does not show either].
    float shotSpeed = 30.0f;
    float shotLift = 3.0f;
    Vec3 muzzle{1.98555f, 1.68952f, 0.0f};  // where yadro.XML's sphere stands in the cannon's frame [D]
    float shotRadius = 0.59128f;            // [D]
    float shotMass = 8.65f;                 // the sphere's volume times Telega1.jpg's density 10 [D]
    double shotLife = 3.0;                  // a ball that has hit nothing by then is gone [I]
    // A cannon fires at most this often [I; the same one fired 4.7 s apart at the quickest in 28 s].
    double cannonCooldown = 0.5;

    // ---- Teleports (17.4)
    double portalGone = 0.2;           // the ball is gone this long [M: 0.13 to 0.25]
    double portalGrow = 0.1;           // and grows from a speck at the exit in this [M: 3 frames at 30 fps]
    double portalDrift = 0.2;          // then drifts this long [M: 0.15 to 0.3] ...
    float portalDriftSpeed = 6.0f;     // ... at this speed [M: 0.7 units in the first 0.1 s] before it leaves at the ball's speed
    float portalSpreadDegrees = 45.0f; // it leaves toward the bat side within this of straight on [M: 0, -14, +45; I for the spread]
    // The tent of an exit is solid and does not turn the way the ball leaves (17.4), so the point 1.2 units out can be inside a
    // plate; the ball is put on along its way until it clears the plates by this much [I].
    float portalClearance = 0.05f;

    // ---- Fireworks (17.5)
    double rocketDelay = 0.7;        // a touch to the rocket coming off the crate [M: 1013.33 to 1014.02]
    float rocketSpeed = 15.6f;       // straight up [M]
    double rocketLife = 1.2;         // to the burst [M: not the 2.5 s of lifeTime [D]]
    float rocketStartHeight = 3.7f;  // above the crate's origin: its top, where the rocket starts [M: 6.5 units at 0.18 s]

    // ---- Vibro (17.7)
    double vibroPeriod = 2.4;     // [M]
    float vibroBob = 0.1f;        // [M: within 0.1 units]
    float vibroTiltDegrees = 6.0f;  // [I]

    // Seeds the exits of the teleports, so that a game repeats exactly.
    std::uint32_t seed = 0x9E3779B9u;
};

// The rules of the level's special objects: dynamite, interactive cannons, teleports, firework crates, springs, volcanoes,
// generators, metal, vibros and flickers (docs/GAMEPLAY.md sections 14 and 17). GameSession owns one and calls it at the
// places the objects touch the rules of play:
//   - ballHit(): a ball with a blow met an object that it does not hurt (instead of hurting it);
//   - destroyed(): a charge of dynamite went off;
//   - update(): once a fixed step, after the contacts: what touched the cannons and the crates, the cannonballs, the rockets,
//     the teleports, the vibros.
// It works on the session's state (it is its friend) and reports through its events; the presentation reads shots(),
// rockets() and the other views.
class SpecialObjects {
public:
    // Finds the level's special objects among the plan's actors.
    SpecialObjects(const Level& level, const PhysicsPlan& plan, const SpecialObjectRules& rules);

    // A ball with a blow met `actor` (an object that something can hurt): true when this handles it, which is for every type but
    // the ones the ball hurts. `contact` is the physics' contact of the ball with it.
    bool ballHit(GameSession& session, ActorIndex actor, const ContactEvent& contact, int ball);
    // `actor`, a charge of dynamite, was destroyed at `at`: its blast.
    void destroyed(GameSession& session, ActorIndex actor, Vec3 at);
    void update(GameSession& session);

    // ---- What the weapons do to the level's objects (Weapons, docs/GAMEPLAY.md section 18.4). The same law as a blast or a cannonball:
    // the object is hurt `damage` energy at a time, and what that does to it is its own (a charge of dynamite goes off after its
    // material's delay, a cannon of two energy takes two, a firework crate a weapon destroys gives no rocket). False if nothing was
    // hurt: the object is gone or going, or nothing hurts it (the teleports, the board). `dropsPrize` false: if this destroys it,
    // it leaves no prize (the thunderbolt and the meteor, which carry disablePrize [D]). `by` is the weapon or strike that hurt it
    // (BrickDestroyed says so if this destroys it).
    bool weaponHit(GameSession& session, ActorIndex actor, Vec3 at, int damage, bool dropsPrize, std::optional<PrizeKind> by = std::nullopt);
    // A burst at `centre`, the kernel's, the rocket's or the meteor's (a charge of dynamite's blast is the same law): `damage` energy off
    // every damageable object within `radius` of it (the distance to the box round it) but `source`, the one the projectile hit; and
    // the loose bodies in reach are pushed away and up as dynamite pushes them, `power` times as hard. A charge of dynamite in reach
    // goes off at the next fixed step, as in a chain.
    void burst(GameSession& session, ActorIndex source, Vec3 centre, float radius, float power, int damage, bool dropsPrize,
               std::optional<PrizeKind> by = std::nullopt);

    // ---- What the presentation draws
    struct Shot {  // a cannonball in flight
        Vec3 position{};
        Vec3 velocity{};
        ActorIndex shooter = kNoActor;
        double firedAt = 0;
    };
    struct Rocket {  // a firework crate's rocket on its way up
        ActorIndex crate = kNoActor;
        Vec3 from{};
        Vec3 position{};
        double launchedAt = 0;
    };
    // A ball that is in a teleport: where to draw it and how much of it (0 gone, 1 whole).
    struct Away {
        Vec3 position{};
        float appear = 0;
    };
    std::span<const Shot> shots() const { return shots_; }
    std::span<const Rocket> rockets() const { return rockets_; }
    // The flickers (cauldrons and torches on a fire) still in the level.
    std::span<const ActorIndex> flickers() const { return flickers_; }
    Away away(int ball, double time) const;

    struct Stats {
        int blasts = 0;          // charges that went off
        int shotsFired = 0;      // cannonballs
        int shotsLanded = 0;     // and how many of them hit something
        int rocketsLaunched = 0;
        int rocketsBurst = 0;
        int teleports = 0;       // balls that went into a source
    };
    const Stats& stats() const { return stats_; }

private:
    enum class Kind : std::uint8_t {
        None,      // the ball hurts it as it hurts a brick: bricks, trees, flickers and vibros
        Dynamite,  // a brick of one energy that blasts when it goes
        Cannon,
        Firework,  // the ball hurts it as a brick, and any touch of it sets a rocket off
        Spring,
        Inert,     // volcanoes, generators and metal: the ball bounces off them, nothing more
        Vibro,
    };
    struct Charge {
        ActorIndex actor = kNoActor;
        float radius = 0;
    };
    struct Cannon {
        ActorIndex actor = kNoActor;
        double firedAt = -1e9;
    };
    struct Crate {
        ActorIndex actor = kNoActor;
        bool waiting = false;  // touched: the rocket comes at launchAt
        bool flying = false;   // the rocket is up, and the crate cannot fire another until it bursts
        double launchAt = 0;
        Vec3 from{};           // where the rocket starts
    };
    struct Plate {  // one of the solid boxes of a teleport (the two plates of its tent)
        Mat4 toBox;   // the world to its frame
        Vec3 half{};
    };
    struct Portal {
        ActorIndex actor = kNoActor;
        Mat4 toBox;     // the world to the frame of its trigger box
        Vec3 half{};    // the trigger box's half extents
        Vec3 centre{};  // the middle of the pyramid: where a ball that comes out of it appears
        std::vector<Plate> plates;
    };
    struct Transit {
        int ball = 0;
        ActorIndex exitActor = kNoActor;
        Vec3 from{};     // the source
        Vec3 exit{};     // and the exit
        Vec3 heading{};  // the way it leaves
        double emergeAt = 0;
        double launchAt = 0;
        bool emerged = false;
    };
    struct Hit {
        Vec3 point{};
        ActorIndex actor = kNoActor;  // kNoActor: something that is no actor of the level
        float distance = 0;
    };

    SpecialObjectRules rules_;
    std::vector<Kind> kinds_;  // by ActorIndex
    std::vector<Charge> charges_;
    std::vector<Cannon> cannons_;
    std::vector<Crate> crates_;
    std::vector<Portal> sources_, exits_;
    std::vector<ActorIndex> vibros_, flickers_;
    std::vector<float> reach_;            // by ActorIndex: the farthest corner of its shapes from its origin
    std::vector<Bounds> staticBounds_;    // by ActorIndex, for the immovable ones
    std::vector<Shot> shots_;
    std::vector<Rocket> rockets_;
    std::vector<Transit> transits_;
    std::uint32_t random_;
    Stats stats_;

    Kind kindOf(ActorIndex actor) const { return actor < kinds_.size() ? kinds_[actor] : Kind::None; }
    float random01();
    // The damage of a blast or a cannonball: `damage` hits, the last of which may fire a charge after `fuse` seconds (a fuse of 0 or
    // less leaves the charge its material's own delay).
    bool strike(GameSession& session, ActorIndex actor, Vec3 at, int damage, double fuse, bool dropsPrize = true,
                std::optional<PrizeKind> by = std::nullopt);
    void blast(GameSession& session, ActorIndex source, Vec3 centre, float radius, float power);
    void push(GameSession& session, ActorIndex actor, Vec3 centre, float radius, float power);
    void fire(GameSession& session, ActorIndex actor);
    void touchCrate(GameSession& session, ActorIndex actor);
    void kick(GameSession& session, ActorIndex actor, int ball);
    void touches(GameSession& session);
    void runShots(GameSession& session);
    std::optional<Hit> sweep(GameSession& session, const Shot& shot, Vec3 step) const;
    void land(GameSession& session, const Shot& shot, const Hit& hit);
    void runCrates(GameSession& session);
    void runPortals(GameSession& session);
    void enterPortal(GameSession& session, int ball, std::size_t source);
    Vec3 launchPoint(const Transit& transit, float radius) const;
    void runVibros(GameSession& session);
};

}  // namespace mb
