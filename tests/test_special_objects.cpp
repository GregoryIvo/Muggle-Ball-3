// Tests of the level's special objects (SpecialObjects, docs/GAMEPLAY.md sections 14 and 17) on made-up levels: dynamite and
// its chains, the interactive cannon and its cannonball, teleports, firework crates, springs, volcanoes, generators, metal,
// vibros and flickers, run in a GameSession with the physics on the calling thread. Needs neither a GPU nor game data. The
// same objects in the real levels: mb_special_object_smoke.

#include "mb/game/Autopilot.h"
#include "mb/game/Gameplay.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) <= tolerance;
}

// ---- Materials and levels ----------------------------------------------------------------------------------------

// The numbers of the real materials (Data/Materials/materials.json) for the objects, and a shark, a pirate (three energy) and a
// block of steel (type 2: nothing damages it) to stand about.
const MaterialLibrary& materials() {
    static const MaterialLibrary library = MaterialLibrary::parse(R"({"version": 1, "materials": [
      {"name": "BallMaterial", "density": 1, "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 0.999},
       "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "BoardMaterial", "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 1.0},
       "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "Steel.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 2, "energy": 1, "score": 10}},
      {"name": "Shark.jpg", "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 91, "exploseType": 3, "exploseDelay": 250}},
      {"name": "Pirate.jpg", "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 3, "score": 290, "exploseType": 5, "exploseDelay": 100}},
      {"name": "Dinamit.jpg", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 120, "exploseType": 8, "exploseDelay": 100, "explosePower": 1.0},
       "sounds": {"dead": "dynamite_dead"}},
      {"name": "int_cannon", "density": 50, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 2, "score": 100, "exploseType": 5, "exploseDelay": 100}},
      {"name": "Fireworks", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 120, "exploseType": 5, "exploseDelay": 100}},
      {"name": "spring", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 5, "exploseDelay": 0},
       "sounds": {"hit": "spring_hit"}},
      {"name": "volcano", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 5, "exploseDelay": 0}},
      {"name": "gen-good", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 77, "exploseType": 5, "exploseDelay": 0}},
      {"name": "Metall.jpg", "density": 20, "physics": {"staticFriction": 0.999, "dynamicFriction": 0.999, "restitution": 0.1},
       "brick": {"type": 0, "energy": 1, "score": 50, "exploseType": 6, "exploseDelay": 100}, "sounds": {"hit": "metall_hit"}},
      {"name": "portal-in", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 5, "exploseDelay": 100}},
      {"name": "portal-out", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 5, "exploseDelay": 100}},
      {"name": "Krug", "density": 10, "physics": {"restitution": 0.3},
       "brick": {"type": 0, "energy": 1, "score": 14, "exploseType": 3, "exploseDelay": 250}},
      {"name": "kotel.jpg", "density": 3, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 70, "exploseType": 5, "exploseDelay": 150}}
    ]})");
    return library;
}

constexpr std::uint32_t kDynamic = 0x100, kKinematic = 0x180, kStatic = 0x17E;  // NxBodyFlag, with the visualization bit
constexpr std::uint32_t kDynamicNoTurn = 0x170;                                  // a cannon, a firework crate: it cannot turn
constexpr std::uint32_t kTriggerFlags = 15;                                      // enter, leave, stay and visualization
// A turn that makes the object's +x point along the world's +z (PhysX stores the matrix for column vectors, row by row).
constexpr std::array<float, 9> kFacesPlusZ{0, 0, -1, 0, 1, 0, 1, 0, 0};

Shape box(Vec3 half, Vec3 at = {}, std::uint16_t group = 2, std::uint32_t flags = 8) {
    Shape s;
    s.type = ShapeType::Box;
    s.halfExtents = half;
    s.localPose.position = at;
    s.group = group;
    s.shapeFlags = flags;
    return s;
}

Actor actor(ObjectType type, Vec3 at, std::uint32_t flags, float mass, std::vector<Shape> shapes, const std::string& material) {
    Actor a;
    a.pose.position = at;
    Body body;
    body.mass = mass;
    body.massSpaceInertia = {mass / 6, mass / 6, mass / 6};
    body.flags = flags;
    a.body = body;
    a.shapes = std::move(shapes);
    GameObject object;
    object.type = type;
    object.id = 1;
    object.material = material;
    a.object = object;
    return a;
}

Actor brick(Vec3 at, const std::string& material, std::uint32_t flags = kDynamic, Vec3 half = {1, 1, 1}, float mass = 50) {
    return actor(ObjectType::Brick, at, flags, mass, {box(half)}, material);
}

// Dynamite: 1.8 wide and 2.6 tall; `radius` goes into the level as the bits of a float, as the converter writes it.
Actor dynamite(Vec3 at, float radius = 5.0f, std::uint32_t flags = kDynamic) {
    Actor a = actor(ObjectType::Dynamite, at, flags, 85, {box({0.9f, 1.3f, 0.9f}, {0, 1.3f, 0})}, "Dinamit.jpg");
    a.object->target = radius > 4 ? "explosebig" : "explosesmall";
    a.object->param = std::bit_cast<std::uint32_t>(radius);
    return a;
}

Actor cannon(Vec3 at, const std::array<float, 9>& turn = {1, 0, 0, 0, 1, 0, 0, 0, 1}) {
    Actor a = actor(ObjectType::Cannon, at, kDynamicNoTurn, 495, {box({1.25f, 0.99f, 1.0f}, {-0.41f, 0.99f, 0})}, "int_cannon");
    a.pose.rotation = turn;
    return a;
}

// A teleport as the levels have it: two solid plates and, for a source, a trigger box between them whose long side runs
// across the tunnel's length (z), 1.6 long.
Actor portal(ObjectType type, Vec3 at) {
    std::vector<Shape> shapes = {box({0.2f, 1.9f, 0.9f}, {-1.3f, 1.75f, 0}), box({0.2f, 1.9f, 0.9f}, {1.3f, 1.75f, 0})};
    if (type == ObjectType::TeleportSrc) {
        shapes.push_back(box({1.0f, 1.0f, 0.8f}, {0, 1.0f, 0}, 2, kTriggerFlags));
    }
    return actor(type, at, kKinematic, 48, std::move(shapes), type == ObjectType::TeleportSrc ? "portal-in" : "portal-out");
}

// A teleport as knight-turnir has it: two plates 25.5 degrees off the vertical, leaning in at the top, 1.78 long, and for a source the
// trigger box between them (shape flags 15; its long side runs along z, 1.6 long). A ball is 0.97 to 0.99 from the middle of the tunnel
// to either plate at its height: the plain ball (0.6) fits with room to spare, the Big Ball (1.0) not at all.
Actor tent(ObjectType type, Vec3 at) {
    Shape left = box({0.17595f, 1.90439f, 0.89009f}, {-0.73784f, 1.75f, 0});
    left.localPose.rotation = {0.90258f, 0.43052f, 0, -0.43052f, 0.90258f, 0, 0, 0, 1};
    Shape right = box({0.17595f, 1.90439f, 0.89009f}, {0.71968f, 1.74913f, 0});
    right.localPose.rotation = {-0.90258f, -0.43052f, 0, -0.43052f, 0.90258f, 0, 0, 0, -1};
    std::vector<Shape> shapes = {left, right};
    if (type == ObjectType::TeleportSrc) {
        Shape trigger = box({0.8f, 1.0f, 1.0f}, {0.01222f, 1.01799f, -0.00203f}, 2, kTriggerFlags);
        trigger.localPose.rotation = {0, 1, 0, 0, 0, 1, 1, 0, 0};
        shapes.push_back(trigger);
    }
    return actor(type, at, kKinematic, 48, std::move(shapes), type == ObjectType::TeleportSrc ? "portal-in" : "portal-out");
}

Actor firework(Vec3 at) {
    return actor(ObjectType::Firework, at, kDynamicNoTurn, 90, {box({1.0f, 1.6f, 0.7f}, {0, 1.6f, 0})}, "Fireworks");
}

Actor board(bool nearWall = true) {
    std::vector<Shape> shapes = {box({30, 0.5f, 30}, {0, -0.5f, 0}, 4), box({1, 2, 30}, {-21.4f, 2, 0}, 4),
                                 box({1, 2, 30}, {21.4f, 2, 0}, 4), box({30, 2, 1}, {0, 2, 23.6f}, 4)};
    if (nearWall) {
        shapes.push_back(box({30, 2, 1}, {0, 2, -21.3f}, 4));
    }
    return actor(ObjectType::Board, {0, 0, 0}, 0xFF, 1000, std::move(shapes), "BoardMaterial");
}

Level levelOf(std::vector<Actor> actors) {
    Level level;
    level.gravity = {0, -9.8f, 0};
    level.actors = std::move(actors);
    return level;
}

SessionOptions options(TargetRule rule = TargetRule::Never) {
    SessionOptions o;
    o.levelEnd = rule;
    o.physicsSettings.threads = 0;  // on the caller, so that a run repeats exactly
    o.rules.prizes.drops = false;   // the rules of the objects alone; the prizes are given by hand, see "Prizes and the objects"
    return o;
}

struct Game {
    GameSession session;
    std::vector<GameEvent> log;

    explicit Game(const Level& level, const SessionOptions& o = options()) : session(level, materials(), o) {}

    void step(const GameInput& input = {}) {
        session.update(1.0 / 60, input);
        for (const GameEvent& event : session.takeEvents()) {
            log.push_back(event);
        }
    }
    void run(double seconds) {
        for (int i = 0, steps = static_cast<int>(std::lround(seconds * 60)); i < steps; ++i) {
            step();
        }
    }
    bool runUntil(const std::function<bool()>& done, double limit) {
        for (int i = 0, steps = static_cast<int>(std::lround(limit * 60)); i < steps; ++i) {
            if (done()) {
                return true;
            }
            step();
        }
        return done();
    }
    bool ready() {
        return runUntil([&] { return session.phase() == SessionPhase::Ready; }, 3);
    }
    // A ball in flight from `at` along `heading`; its id.
    int shoot(Vec3 at, Vec3 heading) {
        return session.addBall(at, heading);
    }
    int count(EventType type) const { return static_cast<int>(std::ranges::count(log, type, &GameEvent::type)); }
    std::vector<const GameEvent*> all(EventType type) const {
        std::vector<const GameEvent*> found;
        for (const GameEvent& event : log) {
            if (event.type == type) {
                found.push_back(&event);
            }
        }
        return found;
    }
    // By value: the log grows as the game goes on, and a pointer into it would not last.
    std::optional<GameEvent> first(EventType type) const {
        const auto found = std::ranges::find(log, type, &GameEvent::type);
        return found == log.end() ? std::nullopt : std::optional<GameEvent>(*found);
    }
    // The BrickDestroyed of an actor, or null.
    const GameEvent* destroyed(ActorIndex actor) const {
        for (const GameEvent& event : log) {
            if (event.type == EventType::BrickDestroyed && event.actor == actor) {
                return &event;
            }
        }
        return nullptr;
    }
    const BallView* ball(int id) const {
        for (const BallView& view : session.balls()) {
            if (view.id == id) {
                return &view;
            }
        }
        return nullptr;
    }
    Vec3 where(ActorIndex actor) const { return session.scene().actorPose(actor).translationPart(); }
};

// ---- Dynamite ----------------------------------------------------------------------------------------------------

// A ball hits a charge: 0.1 s later (exploseDelay) it goes off, scores, and its blast takes what it reaches (up to its radius
// from the box round it) and pushes the loose bodies.
void testBlast(float radius) {
    const std::string name = radius > 4 ? "big" : "small";
    Game g(levelOf({board(),
                    dynamite({0, 0, 10}, radius),                                 // 1
                    brick({4.5f, 1, 10}, "Shark.jpg"),                             // 2: 3.5 units from its edge
                    brick({6.5f, 1, 10}, "Shark.jpg"),                             // 3: 5.5
                    brick({0, 1, 13.5f}, "Pirate.jpg"),                            // 4: three energy
                    actor(ObjectType::Brick, {-3, 0.5f, 10}, kDynamic, 30, {box({0.5f, 0.5f, 0.5f})}, "Steel.jpg"),  // 5: loose
                    brick({0, 1, 17.5f}, "Shark.jpg", kStatic)}));                 // 6: out of reach of both
    g.shoot({0, 0.6f, 0}, {0, 0, 1});
    check(g.runUntil([&] { return g.count(EventType::DynamiteBlast) > 0; }, 2), name + " dynamite: a ball sets it off");
    const std::optional<GameEvent> blast = g.first(EventType::DynamiteBlast);
    if (!blast) {
        return;
    }
    check(near(blast->speed, radius) && blast->actor == 1 && near(blast->position.x, 0, 0.2f) && near(blast->position.z, 10, 0.2f),
          name + " dynamite: the blast has the level's radius and stands at the charge");
    const std::optional<GameEvent> hit = g.first(EventType::BrickHit);
    const GameEvent* gone = g.destroyed(1);
    check(hit && gone != nullptr && near(static_cast<float>(gone->time - hit->time), 0.1f, 0.02f) && gone->points == 120 &&
              gone->time == blast->time,
          name + " dynamite: 0.1 s after the hit it is destroyed, for 120 points, and goes off then");
    check(g.session.actor(1).removed, name + " dynamite: and is gone");

    const float step = 0.3f;
    g.run(0.5 + step);
    const bool nearGone = g.destroyed(2) != nullptr;
    check(nearGone == (radius > 4), name + " dynamite: a shark 3.5 units from the charge's edge " + (nearGone ? "is" : "is not") +
                                         " destroyed (reach " + std::to_string(radius) + ")");
    check(g.destroyed(3) == nullptr && !g.session.actor(3).removed, name + " dynamite: one 5.5 units off is not");
    check(g.destroyed(6) == nullptr, name + " dynamite: and one far off is not");
    if (radius > 4) {
        check(g.session.actor(4).energy == 1 && !g.session.actor(4).removed,
              "big dynamite: a pirate of three energy is hurt for two and lives, as the log of V1 8192.9 did");
        check(g.session.score() == 120 + 91, "big dynamite: the score is the charge's and the shark's, nothing else");
        check(g.where(5).x < -3.5f, "big dynamite: a loose block is pushed away from the charge");
    }
}

// A row of charges 3.6 apart goes off one link at a time, one fixed step a link, and stops at a gap wider than the reach.
void testChain() {
    std::vector<Actor> actors = {board()};
    for (const float x : {-16.0f, -12.4f, -8.8f, -5.2f, 2.0f, 5.6f, 9.2f}) {  // four, a gap of 7.2, three
        actors.push_back(dynamite({x, 0, 10}));
    }
    actors.push_back(brick({-9.0f, 1, 14}, "Shark.jpg"));  // 8: beside the third charge, out of reach of the first
    Game g(levelOf(actors));
    g.shoot({-16, 0.6f, 0}, {0, 0, 1});
    g.run(2.0);
    const auto blasts = g.all(EventType::DynamiteBlast);
    check(blasts.size() == 4, "chain: the four charges of the first group go off (" + std::to_string(blasts.size()) + ")");
    bool linked = blasts.size() == 4;
    for (std::size_t i = 1; i < blasts.size(); ++i) {
        const double gap = blasts[i]->time - blasts[i - 1]->time;
        linked = linked && near(static_cast<float>(gap), 1.0f / 60, 0.004f);
        check(blasts[i]->actor == blasts[i - 1]->actor + 1, "chain: in order along the row");
    }
    check(linked, "chain: one fixed step a link");
    for (ActorIndex i = 5; i <= 7; ++i) {
        check(!g.session.actor(i).removed && g.session.actor(i).energy == 1, "chain: the gap holds the second group back");
    }
    check(g.destroyed(8) != nullptr, "chain: the shark beside the charges is taken by them");
    check(g.session.objects().stats().blasts == 4, "chain: it ended");

    // A tight crowd of them: all go off, none twice, and it ends.
    std::vector<Actor> crowd = {board()};
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column) {
            crowd.push_back(dynamite({-6.0f + 3.0f * static_cast<float>(column), 0, 6.0f + 3.0f * static_cast<float>(row)}));
        }
    }
    Game big(levelOf(crowd));
    big.shoot({-6, 0.6f, 0}, {0, 0, 1});
    big.run(3.0);
    check(big.count(EventType::DynamiteBlast) == 25 && big.count(EventType::BrickDestroyed) == 25 && big.session.score() == 25 * 120,
          "chain: twenty-five charges in a block each go off once");
}

// The ball hurts none of metal, springs, volcanoes and generators; a blast takes them for their points. A teleport nothing hurts.
void testHurtByBlastsOnly() {
    const auto thing = [](ObjectType type, Vec3 at, const char* material) {
        return actor(type, at, kDynamic, 100, {box({1, 1, 1}, {0, 1, 0})}, material);
    };
    Game g(levelOf({board(),
                    thing(ObjectType::Metall, {-8, 0, 8}, "Metall.jpg"),          // 1
                    thing(ObjectType::Spring, {-4, 0, 8}, "spring"),              // 2
                    thing(ObjectType::Volcano, {0, 0, 8}, "volcano"),             // 3
                    thing(ObjectType::GeneratorGood, {4, 0, 8}, "gen-good"),      // 4
                    brick({8, 1, 8}, "Shark.jpg")}));                              // 5: the ball hurts this one
    for (int i = 0; i < 5; ++i) {
        const float x = -8 + 4.0f * static_cast<float>(i);
        g.shoot({x, 0.6f, 0}, {0, 0, 1});
        g.run(1.0);
    }
    for (ActorIndex i = 1; i <= 4; ++i) {
        check(!g.session.actor(i).removed && g.session.actor(i).energy == 1 && g.destroyed(i) == nullptr,
              "ball: actor " + std::to_string(i) + " (metal, spring, volcano, generator) is hit and not hurt");
    }
    check(g.destroyed(5) != nullptr, "ball: a brick beside them is");
    check(g.count(EventType::BrickHit) >= 5, "ball: each of the five is reported as hit, for its sound");
    const GameEvent* metalHit = nullptr;
    for (const GameEvent& event : g.log) {
        if (event.type == EventType::BrickHit && event.actor == 1) {
            metalHit = &event;
        }
    }
    check(metalHit != nullptr && metalHit->energy == 1 && metalHit->material == materials().find("Metall.jpg"),
          "ball: the metal's hit says it has all its energy and carries the material for the sound");

    // A charge among them takes them all: 50 + 10 + 10 + 77 and its own 120.
    Game b(levelOf({board(), dynamite({0, 0, 8}), thing(ObjectType::Metall, {-3, 0, 8}, "Metall.jpg"),
                    thing(ObjectType::Spring, {3, 0, 8}, "spring"), thing(ObjectType::Volcano, {0, 0, 4}, "volcano"),
                    thing(ObjectType::GeneratorGood, {0, 0, 12}, "gen-good"), portal(ObjectType::TeleportSrc, {-3.8f, 0, 4}),
                    portal(ObjectType::TeleportDest, {3.8f, 0, 4})}));
    b.session.hit(1, {});
    b.run(1.5);
    check(b.session.score() == 120 + 50 + 10 + 10 + 77, "blast: it takes metal, a spring, a volcano and a generator for their points");
    check(!b.session.actor(6).removed && !b.session.actor(7).removed && !b.session.actor(6).damageable,
          "blast: the portals 3.8 units away stand, as in V1 8192.8 (level 67)");
    check(b.session.hit(2, {}) == false, "blast: and the metal is gone, so a second hit on it does nothing");
}

// The ball does not hurt metal, so a ball that only bounces off it has done nothing, to the stall rule: a last target that nothing
// reaches (a bird hanging 5 units up in cowboys-banditos) is let go after 90 s, however often the ball hits the metal (docs/GAMEPLAY.md
// 17.1).
void testStallAmongMetal() {
    SessionOptions blind = options(TargetRule::BrickType);
    blind.ignoreUnreachable = false;
    const Actor wall = actor(ObjectType::Metall, {0, 0, 4}, kStatic, 1000, {box({20.4f, 2, 0.5f}, {0, 2, 0})}, "Metall.jpg");
    const Actor pinned = brick({0, 8, 15}, "Shark.jpg", kStatic);  // above the ball for good: nothing can touch it
    Game g(levelOf({board(), wall, pinned}), blind);
    const PlayResult played = autoplay(g.session, 200);
    check(std::ranges::count(played.events, EventType::BrickHit, &GameEvent::type) > 5 && g.session.stats().bricksHit == 0,
          "stall: the ball hits the wall again and again and hurts nothing");
    check(g.session.phase() == SessionPhase::Complete && g.session.stats().targetsLetGo == 1 &&
              played.seconds > g.session.rules().stallSeconds && played.seconds < g.session.rules().stallSeconds + 5,
          "stall: the ball bounced off metal for ninety seconds, and the target it cannot reach is let go");
}

// ---- Cannons -----------------------------------------------------------------------------------------------------

void testCannon() {
    // A cannon turned along +x, a pirate in its line 20 units on, a ball sent at the cannon.
    Game g(levelOf({board(), cannon({-10, 0, 5}), brick({10, 2, 5}, "Shark.jpg", kDynamic, {1, 2, 1})}));
    g.shoot({-10, 0.6f, 14}, {0, 0, -1});
    check(g.runUntil([&] { return g.count(EventType::CannonFired) > 0; }, 2), "cannon: a ball sets it off");
    const std::optional<GameEvent> fired = g.first(EventType::CannonFired);
    if (!fired) {
        return;
    }
    check(fired->actor == 1 && near(fired->velocity.x, 30, 0.01f) && near(fired->velocity.y, 3, 0.01f) && near(fired->velocity.z, 0, 0.01f),
          "cannon: the cannonball leaves along the cannon's +x at 30 with a lift of 3 (yadro.XML)");
    check(near(fired->position.x, -10 + 1.986f, 0.05f) && near(fired->position.y, 1.69f, 0.05f) && near(fired->position.z, 5, 0.05f),
          "cannon: from the muzzle");
    check(g.session.objects().shots().size() == 1, "cannon: and flies");
    // It flies: 0.3 s later it has gone 9 units along x.
    const double firedAt = fired->time;
    g.runUntil([&] { return g.session.time() >= firedAt + 0.3; }, 1);
    if (!g.session.objects().shots().empty()) {
        const SpecialObjects::Shot& shot = g.session.objects().shots().front();
        const auto flown = static_cast<float>(g.session.time() - firedAt);
        check(near(shot.position.x, -10 + 1.986f + 30.0f * flown, 0.1f) && near(shot.velocity.y, 3 - 9.8f * flown, 0.1f),
              "cannon: it flies at 30 units a second and falls under the gravity of the level");
    }
    check(g.runUntil([&] { return g.count(EventType::ShotHit) > 0; }, 2), "cannon: it hits the pirate's crate");
    const std::optional<GameEvent> burst = g.first(EventType::ShotHit);
    check(burst && burst->actor == 2 && near(burst->position.x, 9, 0.8f), "cannon: at its near face");
    g.run(0.6);
    check(g.destroyed(2) != nullptr && g.session.score() == 91 && g.session.objects().shots().empty(),
          "cannon: the cannonball destroyed it (91 points) and is gone");
    check(!g.session.actor(1).removed && g.session.actor(1).energy == 2, "cannon: the ball that set the cannon off did not hurt it");

    // Touched again at once: nothing (0.5 s between shots); touched later: another, and still not hurt, three times over.
    Game h(levelOf({board(), cannon({-10, 0, 5})}));
    for (int shot = 0; shot < 3; ++shot) {
        h.shoot({-10, 0.6f, 14}, {0, 0, -1});
        h.run(1.5);
    }
    check(h.count(EventType::CannonFired) == 3 && h.session.actor(1).energy == 2 && !h.session.actor(1).removed,
          "cannon: three balls, three shots, and the cannon, of energy 2, lives");
    // The cannonball lands on level ground about 26 units on.
    const auto landings = h.all(EventType::ShotHit);
    check(!landings.empty() && landings.front()->position.x > 15 && landings.front()->position.x < 32 && landings.front()->actor == 0,
          "cannon: with no target it lands on the board's floor some 26 units away");
    check(h.session.objects().shots().empty(), "cannon: and none is left flying");

    // Turned the other way it fires along +z; the cannonball takes a second cannon (energy 2) at once.
    Game t(levelOf({board(), cannon({0, 0, -6}, kFacesPlusZ), cannon({0, 0, 8}, kFacesPlusZ)}));
    t.shoot({-8, 0.6f, -6}, {1, 0, 0});
    t.run(1.0);
    const std::optional<GameEvent> shot = t.first(EventType::CannonFired);
    check(shot && near(shot->velocity.z, 30, 0.01f) && near(shot->velocity.x, 0, 0.01f),
          "cannon: a turned cannon fires along its own +x, which is the world's +z");
    check(t.destroyed(2) != nullptr && t.destroyed(2)->points == 100, "cannon: its cannonball destroys the second cannon, for 100 points");
    check(t.count(EventType::CannonFired) == 1, "cannon: and the one it destroyed does not answer");
}

// A cannon fires at most every half second: two balls that touch it 0.2 s apart, from opposite sides, make one shot, a third that
// comes later another.
void testCooldown() {
    Game g(levelOf({board(), cannon({-10, 0, 5})}));
    g.shoot({-10, 0.6f, 14}, {0, 0, -1});     // meets the cannon's far side (z = 6) at about 0.5 s
    g.shoot({-10, 0.6f, -6.95f}, {0, 0, 1});  // and its near side (z = 4) 0.2 s later
    g.run(0.9);
    check(g.count(EventType::CannonFired) == 1, "cooldown: two touches 0.2 s apart make one shot");
    g.run(1.0);
    g.shoot({-10, 0.6f, 14}, {0, 0, -1});
    g.run(1.5);
    check(g.count(EventType::CannonFired) == 2, "cooldown: and a later one another");
}

// A ball that only grazes a cannon or a crate sets it off, as the footage's did, and hurts nothing.
void testGraze() {
    Game g(levelOf({board(), cannon({0, 0, 5}), firework({10, 0, 12})}));  // (the crate is out of the cannon's line)
    g.shoot({1.4f, 0.6f, -4}, {0, 0, 1});  // along the cannon's flank, a hair into it
    g.run(1.0);
    check(g.count(EventType::CannonFired) == 1 && g.session.actor(1).energy == 2, "graze: a ball along a cannon's side sets it off");
    g.shoot({11.58f, 0.6f, 4}, {0, 0, 1});
    g.run(2.5);
    check(g.count(EventType::RocketLaunched) == 1 && !g.session.actor(2).removed, "graze: and one along a crate's side launches a rocket");
}

// Debris sets a cannon off: a block that falls on it fast enough, though no ball is near.
void testDebris() {
    SessionOptions o = options();
    o.physics.start = StartState::Still;  // everything awake, so the block falls
    Game g(levelOf({board(), cannon({0, 0, 5}),
                    actor(ObjectType::Brick, {-0.4f, 6, 5}, kDynamic, 20, {box({0.5f, 0.5f, 0.5f})}, "Steel.jpg")}),
           o);
    g.run(1.5);
    check(g.count(EventType::CannonFired) == 1, "debris: a block that falls onto a cannon sets it off");

    Game quiet(levelOf({board(), cannon({0, 0, 5}),
                        actor(ObjectType::Brick, {-0.4f, 2.0f, 5}, kDynamic, 20, {box({0.5f, 0.5f, 0.5f})}, "Steel.jpg")}),
               o);
    quiet.run(1.5);
    check(quiet.count(EventType::CannonFired) == 0, "debris: one that settles on it gently does not");
}

// ---- Teleports ---------------------------------------------------------------------------------------------------

void testTeleport() {
    // A source at x = -10, its exit at x = 10: a ball sent through the source along z.
    const Level level = levelOf({board(false), portal(ObjectType::TeleportSrc, {-10, 0, 5}), portal(ObjectType::TeleportDest, {10, 0, 5})});
    Game g(level);
    const int id = g.shoot({-10, 0.6f, -4}, {0, 0, 1});
    check(g.runUntil([&] { return g.count(EventType::BallEntered) > 0; }, 2), "teleport: a ball entering the trigger box goes in");
    const std::optional<GameEvent> in = g.first(EventType::BallEntered);
    if (!in) {
        return;
    }
    check(in->ball == id && in->actor == 1 && near(in->position.x, -10, 0.2f) && near(in->position.z, 4, 0.4f),
          "teleport: at the tunnel's mouth");
    const BallView* view = g.ball(id);
    check(view != nullptr && view->appear == 0 && g.count(EventType::BallLost) == 0,
          "teleport: the ball is gone from sight and not lost");

    // Gone for 0.2 s, out at the exit.
    check(g.runUntil([&] { return g.count(EventType::BallEmerged) > 0; }, 1), "teleport: it comes out");
    const std::optional<GameEvent> out = g.first(EventType::BallEmerged);
    check(out && near(static_cast<float>(out->time - in->time), 0.2f, 0.02f) && out->actor == 2 &&
              near(out->position.x, 10, 0.05f) && near(out->position.z, 5, 0.05f),
          "teleport: 0.2 s later, at the exit's middle");
    view = g.ball(id);
    check(view != nullptr && view->appear < 0.3f, "teleport: a speck that grows");
    g.run(0.05);
    const float grown = g.ball(id)->appear;
    g.run(0.1);
    check(grown > 0.3f && grown < 0.9f && near(g.ball(id)->appear, 1), "teleport: whole after 0.1 s");
    const Vec3 early = g.ball(id)->position;
    check(std::hypot(early.x - 10, early.z - 5) < 1.4f && g.ball(id)->velocity.x == 0 && g.ball(id)->velocity.z == 0,
          "teleport: and it drifts, a unit or so from the exit, for 0.2 s");

    // Then it leaves toward the bat side at the ball's speed, within 45 degrees of straight on.
    check(g.runUntil([&] { return g.ball(id) != nullptr && g.ball(id)->velocity.z != 0; }, 1), "teleport: it leaves");
    const Vec3 v = g.ball(id)->velocity;
    const float speed = std::hypot(v.x, v.z);
    check(near(speed, 15, 0.05f) && v.z < 0 && std::fabs(std::atan2(v.x, -v.z)) <= radians(45.5f),
          "teleport: at 15 units a second, toward -z, within 45 degrees of it");
    check(g.count(EventType::BallLost) == 0 && g.session.phase() != SessionPhase::Lost, "teleport: and nothing was lost");

    // The same level again gives the same exit: the angle is drawn from a seed.
    Game again(level);
    const int id2 = again.shoot({-10, 0.6f, -4}, {0, 0, 1});
    again.runUntil([&] { return again.ball(id2) != nullptr && again.ball(id2)->velocity.z != 0 && again.count(EventType::BallEmerged) > 0; }, 2);
    check(again.ball(id2) != nullptr && near(again.ball(id2)->velocity.x, v.x, 0.01f), "teleport: a game repeats exactly");

    // Two sources, one exit: both lead there. A ball that is the only one in play stays in play while it is away.
    Game two(levelOf({board(false), portal(ObjectType::TeleportSrc, {-10, 0, 5}), portal(ObjectType::TeleportSrc, {-3, 0, 5}),
                      portal(ObjectType::TeleportDest, {10, 0, 5})}));
    const int a = two.shoot({-10, 0.6f, -4}, {0, 0, 1});
    const int b = two.shoot({-3, 0.6f, -4}, {0, 0, 1});
    two.run(1.0);
    const auto emerged = two.all(EventType::BallEmerged);
    check(emerged.size() == 2 && emerged[0]->actor == 3 && emerged[1]->actor == 3 && emerged[0]->ball != emerged[1]->ball &&
              (emerged[0]->ball == a || emerged[0]->ball == b),
          "teleport: two sources send their balls to the one exit");
    check(two.session.objects().stats().teleports == 2, "teleport: and are counted");
}

// The game's own ball, the only one, goes into a source: while it is away no heart is spent, and it comes back.
void testTeleportKeepsTheBall() {
    Game g(levelOf({board(false), portal(ObjectType::TeleportSrc, {-5, 0, 5}), portal(ObjectType::TeleportDest, {10, 0, 5})}));
    check(g.ready(), "teleport: the bat is in");
    g.step({.launch = true});
    check(g.runUntil([&] { return g.count(EventType::BallEntered) > 0; }, 3), "teleport: the launched ball reaches the source");
    check(g.session.balls().size() == 1 && g.session.phase() == SessionPhase::Playing && g.session.balls()[0].appear == 0,
          "teleport: it is the only ball, away, and the game plays on");
    g.run(0.3);
    check(g.session.phase() == SessionPhase::Playing && g.session.hearts() == 2 && g.session.balls().size() == 1 &&
              g.count(EventType::BallLost) == 0 && !g.session.balls()[0].held && g.session.balls()[0].appear > 0.99f,
          "teleport: it is back, whole, and no heart was spent");
}

// ---- Fireworks ---------------------------------------------------------------------------------------------------

// A ball sent past the side of a crate (half width 1, the ball 0.59), toward the bat's side, with its edge a hair off it: a touch without a
// blow (one that goes in by more than 0.05 is a blow, and destroys the crate), once (the ball leaves the board, which has no near wall,
// and is lost). Its id.
int graze(Game& g, ActorIndex crate) {
    const Vec3 at = g.where(crate);
    return g.shoot({at.x + 1.58f, 0.6f, at.z + 8}, {0, 0, -1});
}

void testFirework() {
    Game g(levelOf({board(false), firework({0, 0, 10})}));
    const int id = graze(g, 1);
    check(g.runUntil([&] { return g.ball(id) != nullptr && g.ball(id)->position.z < 11.3f; }, 2), "firework: the ball reaches the crate");
    const double touchedAt = g.session.time();
    check(g.runUntil([&] { return g.count(EventType::RocketLaunched) > 0; }, 2), "firework: a rocket leaves the crate");
    const std::optional<GameEvent> launched = g.first(EventType::RocketLaunched);
    if (!launched) {
        return;
    }
    check(near(static_cast<float>(launched->time - touchedAt), 0.7f, 0.1f) && launched->actor == 1,
          "firework: 0.7 s after the touch");
    check(near(launched->position.x, 0, 0.1f) && near(launched->position.y, 3.7f, 0.1f), "firework: from the top of the crate");
    g.run(0.3);
    check(g.session.objects().rockets().size() == 1 &&
              near(g.session.objects().rockets().front().position.y, launched->position.y + 15.6f * 0.3f, 0.3f),
          "firework: it rises at 15.6 units a second");
    check(g.runUntil([&] { return g.count(EventType::RocketBurst) > 0; }, 2), "firework: and bursts");
    const std::optional<GameEvent> burst = g.first(EventType::RocketBurst);
    check(burst && near(static_cast<float>(burst->time - launched->time), 1.2f, 0.03f) &&
              burst->position.y > 20 && burst->position.y < 24,
          "firework: 1.2 s after launching, some 20 units up");
    check(g.session.objects().rockets().empty(), "firework: the rocket is spent");
    check(g.session.score() == 0 && !g.session.actor(1).removed && g.session.actor(1).energy == 1 && g.destroyed(1) == nullptr,
          "firework: no score, and a touch that is no blow leaves the crate standing");

    // While one is up another touch starts nothing; once it burst, a touch does.
    g.log.clear();
    graze(g, 1);
    g.run(0.4);
    graze(g, 1);  // touches while the rocket is pending
    g.run(0.7);
    graze(g, 1);  // and with it up
    g.run(3.0);
    check(g.count(EventType::RocketLaunched) == 1 && g.count(EventType::RocketBurst) == 1,
          "firework: touched while a rocket is pending or up, a crate starts no other");
    graze(g, 1);
    g.run(3.0);
    check(g.count(EventType::RocketLaunched) == 2 && g.count(EventType::RocketBurst) == 2,
          "firework: once it burst, a touch starts the next");
    check(g.session.stats().bricksDestroyed == 0, "firework: and nothing is destroyed");
}

// A ball that hits the crate squarely hurts it as it hurts a brick (energy 1: destroyed for 120 points). The touch lit the rocket,
// and it comes all the same; a crate that only a blast reached was never lit, and gives none (docs/GAMEPLAY.md 17.5).
void testFireworkBlow() {
    Game g(levelOf({board(), firework({0, 0, 10})}));
    g.shoot({0, 0.6f, 0}, {0, 0, 1});
    g.run(3.5);
    const GameEvent* gone = g.destroyed(1);
    check(gone != nullptr && gone->points == 120 && g.session.score() == 120 && g.session.actor(1).removed,
          "firework blow: a ball that hits the crate squarely destroys it, for 120 points");
    check(g.count(EventType::RocketLaunched) == 1 && g.count(EventType::RocketBurst) == 1,
          "firework blow: and the rocket that the touch lit comes all the same");

    Game b(levelOf({board(), dynamite({0, 0, 8}), firework({4, 0, 8})}));
    b.session.hit(1, {});
    b.run(3.0);
    check(b.destroyed(2) != nullptr && b.count(EventType::RocketLaunched) == 0,
          "firework blow: a crate that a blast destroys, never touched, gives no rocket");
}

// The crate that stood on the plug of a gap in a wall and fell into the gap when the plug broke (village-apple's two metal
// fences): the ball takes it out. When the ball did not hurt a crate it sat in the gap for good, and the level could not be won.
void testFireworkInAGap() {
    const auto wall = [](float low, float high) {
        return actor(ObjectType::Metall, {(low + high) / 2, 0, 10}, kStatic, 1000, {box({(high - low) / 2, 2, 0.5f}, {0, 2, 0})}, "Metall.jpg");
    };
    Game g(levelOf({board(), wall(-20.4f, -1.25f), wall(1.25f, 20.4f), firework({0, 0, 10})}));  // a gap of 2.5; the crate is 2 wide
    g.shoot({0, 0.6f, 0}, {0, 0, 1});
    g.run(2.0);
    check(g.session.actor(3).removed, "firework in a gap: the ball destroyed the crate that filled it");
    const int through = g.shoot({0, 0.6f, 0}, {0, 0, 1});
    check(g.runUntil([&] { return g.ball(through) != nullptr && g.ball(through)->position.z > 12; }, 2),
          "firework in a gap: and the next ball goes through");
}

// ---- Springs, vibros, flickers ------------------------------------------------------------------------------------

void testSpring() {
    // A spring seen by a ball that glances off its corner: it is kicked away from the spring's middle, not mirrored.
    Game g(levelOf({board(), actor(ObjectType::Spring, {0, 0, 10}, kDynamic, 230, {box({1.5f, 1.0f, 1.5f}, {0, 1.0f, 0})}, "spring")}));
    g.shoot({2.0f, 0.6f, 4}, {0, 0, 1});  // up the board along x = 2.0, onto the face whose corner is at x = 1.5
    check(g.runUntil([&] { return g.count(EventType::BrickHit) > 0; }, 2), "spring: a ball hits it");
    const std::optional<GameEvent> hit = g.first(EventType::BrickHit);
    check(hit && hit->actor == 1 && hit->energy == 1 && hit->material == materials().find("spring"),
          "spring: it is reported as hit, whole, with its material for the spring_hit sound");
    g.step();
    const Vec3 v = g.session.balls().back().velocity;
    const Vec3 centre = g.session.centreOf(1);
    const Vec3 at = g.session.balls().back().position;
    Vec3 radial{at.x - centre.x, 0, at.z - centre.z};
    radial = normalize(radial);
    check(near(std::hypot(v.x, v.z), 15, 0.05f), "spring: the ball leaves at its speed");
    check(dot(normalize({v.x, 0, v.z}), radial) > 0.97f, "spring: along the line from the spring's middle");
    g.run(0.5);
    check(!g.session.actor(1).removed && g.session.score() == 0, "spring: and it is not hurt");
}

void testVibro() {
    // A barrel that bobs in place: its body, kinematic, goes up and down by a tenth of a unit with a period of 2.4 s.
    Actor barrel = actor(ObjectType::Vibro, {6, 0, 10}, kKinematic, 144, {box({1.2f, 0.34f, 2.1f}, {0, 0.4f, 0})}, "Krug");
    Game g(levelOf({board(), barrel}));
    float low = 1e9f, high = -1e9f;
    std::vector<float> samples;
    for (int i = 0; i < 2 * 60 + 60; ++i) {
        g.step();
        const float y = g.where(1).y;
        low = std::min(low, y);
        high = std::max(high, y);
        samples.push_back(y);
    }
    check(high - low > 0.15f && high - low < 0.21f && std::fabs(g.where(1).x - 6) < 0.01f && std::fabs(g.where(1).z - 10) < 0.01f,
          "vibro: it bobs by a tenth of a unit either way, in place");
    // Periodic: the pose of 2.4 s later is the same (144 steps).
    check(samples.size() > 144 + 10 && near(samples[20], samples[20 + 144], 0.01f), "vibro: with a period of 2.4 s");
    // A ball destroys it like a brick (14 points).
    g.shoot({6, 0.6f, 0}, {0, 0, 1});
    g.run(1.5);
    check(g.session.score() == 14, "vibro: and a ball destroys it, for its 14 points");
}

void testFlicker() {
    Game g(levelOf({board(), actor(ObjectType::Flicker, {5, 0, 10}, kDynamic, 27, {box({0.7f, 0.64f, 1.4f}, {0, 0.64f, 0})}, "kotel.jpg")}));
    check(g.session.objects().flickers().size() == 1 && g.session.objects().flickers().front() == 1, "flicker: it is listed for its glow");
    g.shoot({5, 0.6f, 0}, {0, 0, 1});
    g.run(1.0);
    check(g.session.score() == 70, "flicker: a cauldron is a brick the ball destroys, for 70 points");
}

// ---- Destroyed objects, prizes and the objects ---------------------------------------------------------------------

// A spring or a crate that a blast, a cannonball or a weapon destroyed shrinks away for 0.35 s with its body still in the world. In
// that time it is a body in the way and no more: the spring neither kicks the ball nor makes its sound, and the crate gives no rocket
// (docs/GAMEPLAY.md 17.5, 17.6). A whole spring does both (testSpring).
void testDestroyedObjects() {
    Game g(levelOf({board(), actor(ObjectType::Spring, {0, 0, 10}, kStatic, 230, {box({1.5f, 1.0f, 1.5f}, {0, 1.0f, 0})}, "spring")}));
    g.session.hit(1, {});
    g.run(0.05);
    check(g.session.actor(1).shrink > 0 && !g.session.actor(1).removed, "destroyed spring: it is shrinking away, its body still there");
    const int id = g.shoot({0.8f, 0.6f, 7.5f}, {0, 0, 1});  // 0.4 units short of its face: there in 0.03 s
    g.run(0.2);
    check(g.ball(id) != nullptr, "destroyed spring: the ball is in play");
    const bool hitByTheBall = std::ranges::any_of(g.log, [](const GameEvent& e) { return e.type == EventType::BrickHit && e.ball >= 0; });
    check(!hitByTheBall, "destroyed spring: a ball that meets it is not kicked and the spring makes no sound for it");

    // The crate: destroyed by a hit that is no ball's, never touched; a ball brushes its shrinking body 0.25 s later.
    Game c(levelOf({board(false), firework({0, 0, 10})}));
    c.session.hit(1, {});
    c.run(0.15);  // exploding for 0.1 s, then shrinking
    check(c.session.actor(1).shrink > 0 && !c.session.actor(1).removed, "destroyed crate: it is shrinking away, its body still there");
    const Vec3 at = c.where(1);
    c.shoot({at.x + 1.58f, 0.6f, at.z + 3}, {0, 0, -1});
    c.run(3.0);
    check(c.count(EventType::RocketLaunched) == 0, "destroyed crate: a brush on its shrinking body lights no rocket");
}

// What the speed prizes make of the ball's speed reaches the spring's kick and the teleport's exit, and the debris that sets a cannon
// off has to hit as hard as a blow of the ball at that speed (a blow is a share of the ball's speed, GameplayRules::hitFraction).
void testSpeedPrizesAndObjects() {
    Game g(levelOf({board(), actor(ObjectType::Spring, {0, 0, 10}, kDynamic, 230, {box({1.5f, 1.0f, 1.5f}, {0, 1.0f, 0})}, "spring")}));
    for (int i = 0; i < 3; ++i) {
        g.session.givePrize(PrizeKind::SpeedUp);
    }
    const float faster = g.session.ballSpeed();
    g.shoot({2.0f, 0.6f, 4}, {0, 0, 1});
    g.runUntil([&] { return g.count(EventType::BrickHit) > 0; }, 2);
    g.step();
    const Vec3 v = g.session.balls().back().velocity;
    check(near(faster, 15 * 1.1f * 1.1f * 1.1f, 0.01f) && near(std::hypot(v.x, v.z), faster, 0.05f),
          "speed prizes: the spring kicks the ball at the session's speed, " + std::to_string(std::hypot(v.x, v.z)));

    Game t(levelOf({board(false), portal(ObjectType::TeleportSrc, {-10, 0, 5}), portal(ObjectType::TeleportDest, {10, 0, 5})}));
    for (int i = 0; i < 3; ++i) {
        t.session.givePrize(PrizeKind::SlowDown);
    }
    const int id = t.shoot({-10, 0.6f, -4}, {0, 0, 1});
    check(t.runUntil([&] { return t.count(EventType::BallEmerged) > 0 && t.ball(id) != nullptr && t.ball(id)->velocity.z != 0; }, 3),
          "speed prizes: the ball comes out");
    const Vec3 w = t.ball(id) != nullptr ? t.ball(id)->velocity : Vec3{};
    check(near(t.session.ballSpeed(), 15 * 0.91f * 0.91f * 0.91f, 0.01f) && near(std::hypot(w.x, w.z), t.session.ballSpeed(), 0.05f),
          "speed prizes: and leaves the teleport at the session's speed, " + std::to_string(std::hypot(w.x, w.z)));

    // A block that falls onto the cannon at about 6 units/s: a blow of the ball at 15 (4.5), none of the ball at twice that (9).
    const auto fires = [](int speedUps) {
        SessionOptions o = options();
        o.physics.start = StartState::Still;
        Game game(levelOf({board(), cannon({0, 0, 5}),
                           actor(ObjectType::Brick, {-0.4f, 4.32f, 5}, kDynamic, 20, {box({0.5f, 0.5f, 0.5f})}, "Steel.jpg")}),
                  o);
        for (int i = 0; i < speedUps; ++i) {
            game.session.givePrize(PrizeKind::SpeedUp);
        }
        game.run(1.5);
        return game.count(EventType::CannonFired);
    };
    check(fires(0) == 1 && fires(8) == 0, "speed prizes: a block that hits the cannon at 6 units/s sets it off at speed 15 and not at 30");
}

// A Big Ball is 2.0 across and the tent of a teleport 1.94 to 1.97 between its plates, so its centre cannot get into the trigger box:
// it goes in when it reaches the box. A ball that only goes by does not.
void testBigBallEntersASource() {
    const auto enters = [](bool big, float offset) {
        Game g(levelOf({board(false), tent(ObjectType::TeleportSrc, {-10, 0, 5}), tent(ObjectType::TeleportDest, {10, 0, 5})}));
        if (big) {
            g.session.givePrize(PrizeKind::BigBall);
        }
        g.shoot({-10 + offset, 0.6f, -4}, {0, 0, 1});
        g.run(2.5);
        return g.count(EventType::BallEntered) > 0;
    };
    check(enters(false, 0), "big ball: a plain ball goes into the tent");
    check(enters(true, 0) && enters(true, 0.5f) && enters(true, -0.5f), "big ball: so does a Big Ball, centred or half a unit off");
    check(!enters(true, 2.6f), "big ball: but not one that goes by the side of the tent");
}

// The exit's tent is solid and the way out does not follow its orientation (17.4): with the exit turned a quarter, the plates stand across
// the way out, and the point 1.2 units on is inside the near one. The ball is made beyond it, whatever its size.
void testExitLaunchClearsThePlates() {
    for (const bool big : {false, true}) {
        SessionOptions o = options();
        o.rules.objects.portalSpreadDegrees = 0;  // straight on: -z
        Actor exit = portal(ObjectType::TeleportDest, {10, 0, 5});
        exit.pose.rotation = kFacesPlusZ;  // the plates (1.3 off the middle along the exit's x) are now 1.3 off along z
        Game g(levelOf({board(false), portal(ObjectType::TeleportSrc, {-10, 0, 5}), exit}), o);
        if (big) {
            g.session.givePrize(PrizeKind::BigBall);
        }
        const std::string who = big ? "big ball" : "ball";
        const int id = g.shoot({-10, 0.6f, -4}, {0, 0, 1});
        check(g.runUntil([&] { return g.count(EventType::BallEmerged) > 0 && g.ball(id) != nullptr && g.ball(id)->velocity.z != 0; }, 3),
              "exit: the " + who + " leaves the teleport");
        const BallView* view = g.ball(id);
        if (view == nullptr) {
            continue;
        }
        const float nearFace = 5 - 1.3f - 0.2f;  // of the plate on the bat's side: z = 3.5
        check(view->position.z + view->radius <= nearFace + 0.01f && near(view->radius, big ? 1.0f : 0.6f),
              "exit: the " + who + " is made clear of the plate that stood in its way, at z " + std::to_string(view->position.z));
        g.run(2.0);
        check(g.count(EventType::BallFreed) == 0, "exit: and is not jammed in it for a second");
    }
}

// A game's only ball is in a teleport (no body, not lost) when a prize that works on balls is caught.
void testBallInATeleportMeetsThePrizes() {
    SessionOptions o = options();
    o.rules.prizes.skullBallsVanish = 0.1;  // within the 0.4 s that the ball is away
    const Level level = levelOf({board(false), portal(ObjectType::TeleportSrc, {-5, 0, 5}), portal(ObjectType::TeleportDest, {10, 0, 5})});
    // The bat is in, the ball is let go and reaches the source.
    const auto away = [](Game& g) {
        if (!g.ready()) {
            return false;
        }
        g.step({.launch = true});
        return g.runUntil([&] { return g.count(EventType::BallEntered) > 0; }, 3);
    };

    // Multiball has no ball to take balls from, and the ball comes back.
    Game m(level, o);
    check(away(m), "teleport and prizes: the only ball is in the source");
    m.session.givePrize(PrizeKind::Multiball);
    check(m.session.balls().size() == 1, "multiball: a ball in a teleport has no body to copy from: no balls are made");
    m.run(0.6);
    check(m.session.balls().size() == 1 && m.count(EventType::BallEmerged) == 1 && m.count(EventType::BallLost) == 0,
          "multiball: and the ball comes out all the same");

    // The Big Ball makes the ball that is away big: it comes out with the new radius.
    Game b(level, o);
    check(away(b), "teleport and prizes: again, the ball is in the source");
    b.session.givePrize(PrizeKind::BigBall);
    check(near(b.session.balls()[0].radius, 1.0f), "big ball: the ball in a teleport has the new radius at once");
    b.run(0.6);
    check(b.session.balls().size() == 1 && !b.session.balls()[0].held && b.session.balls()[0].appear > 0.99f &&
              near(b.session.balls()[0].radius, 1.0f),
          "big ball: and comes out whole and big");

    // The Skull takes every ball, one in a teleport too: it is lost at the source, and does not come out afterwards.
    Game k(level, o);
    check(away(k), "teleport and prizes: and again");
    k.session.givePrize(PrizeKind::Skull);
    k.run(0.5);
    const std::optional<GameEvent> lost = k.first(EventType::BallLost);
    check(lost && near(lost->position.x, -5, 0.5f) && near(lost->position.z, 5, 0.5f) && k.session.balls().empty() &&
              k.count(EventType::BallEmerged) == 0,
          "skull: the ball that was in a teleport is lost at the source, and does not come out afterwards");
    k.run(3.5);
    check(k.count(EventType::LifeLost) == 1 && k.count(EventType::BallLost) == 1 && k.session.phase() == SessionPhase::Ready &&
              k.session.balls().size() == 1 && k.session.balls()[0].held,
          "skull: a heart is spent, and the new bat has its ball");
}

// The Explosive Ball's blast takes the objects around the brick it hurt: metal, springs, dynamite (which goes off and has its own
// blast). A ball that only meets metal hurts nothing and does not use the charge up.
void testExplosiveBallAndObjects() {
    const auto thing = [](ObjectType type, Vec3 at, const char* material) {
        return actor(type, at, kStatic, 100, {box({1, 1, 1}, {0, 1, 0})}, material);
    };
    const auto level = [&] {
        return levelOf({board(), brick({0, 1, 10}, "Shark.jpg", kStatic),                  // 1: the ball hits this, at (0, 0.6, 9)
                        thing(ObjectType::Metall, {3.2f, 0, 9.5f}, "Metall.jpg"),          // 2: 2.2 from the contact
                        dynamite({-3.0f, 0, 9.5f}),                                        // 3: 2.1 from it
                        thing(ObjectType::Spring, {0, 0, 18.0f}, "spring"),                // 4: out of reach of the blast and the charge
                        thing(ObjectType::Metall, {12, 0, 9.5f}, "Metall.jpg")});          // 5: far
    };
    Game plain(level());
    plain.shoot({0, 0.6f, 3}, {0, 0, 1});
    plain.run(1.5);
    check(plain.destroyed(1) != nullptr && plain.destroyed(2) == nullptr && plain.destroyed(3) == nullptr,
          "explosive ball: without it the ball takes the shark and nothing around it");

    Game g(level());
    g.session.givePrize(PrizeKind::ExplosiveBall);
    g.shoot({0, 0.6f, 3}, {0, 0, 1});
    g.run(1.5);
    check(g.destroyed(1) != nullptr && g.destroyed(2) != nullptr && g.destroyed(3) != nullptr && g.count(EventType::Blast) == 1,
          "explosive ball: the blast takes the metal and the dynamite within 2.5 as well");
    check(g.count(EventType::DynamiteBlast) == 1 && g.destroyed(4) == nullptr && g.destroyed(5) == nullptr,
          "explosive ball: the dynamite goes off in its turn, but not out to the spring (7.8 from it) or the far metal");

    Game h(levelOf({board(), thing(ObjectType::Metall, {0, 0, 10}, "Metall.jpg")}));
    h.session.givePrize(PrizeKind::ExplosiveBall);
    h.shoot({0, 0.6f, 3}, {0, 0, 1});
    h.run(1.0);
    check(h.count(EventType::Blast) == 0 && h.session.effect(PrizeKind::ExplosiveBall) != nullptr && h.destroyed(1) == nullptr,
          "explosive ball: a ball that only meets metal hurts nothing and keeps its charge");
}

// ---- Ending ------------------------------------------------------------------------------------------------------

// The level ends with its bricks, with the dynamite, metal and the rest still standing.
void testEnd() {
    Game g(levelOf({board(), brick({0, 1, 10}, "Shark.jpg", kStatic), dynamite({-8, 0, 10}), cannon({8, 0, 5}),
                    actor(ObjectType::Metall, {12, 0, 12}, kDynamic, 100, {box({1, 1, 1}, {0, 1, 0})}, "Metall.jpg")}),
           options(TargetRule::Bricks));
    check(g.session.targetsLeft(TargetRule::Bricks) == 1, "end: only the brick is required");
    g.session.hit(1, {});
    g.run(0.6);
    check(g.count(EventType::LevelComplete) == 1 && !g.session.actor(2).removed && !g.session.actor(3).removed &&
              !g.session.actor(4).removed,
          "end: it is over with the charge, the cannon and the metal standing");
}

void testDescribe() {
    for (const EventType type : {EventType::DynamiteBlast, EventType::CannonFired, EventType::ShotHit, EventType::RocketLaunched,
                                 EventType::RocketBurst, EventType::BallEntered, EventType::BallEmerged}) {
        GameEvent event;
        event.type = type;
        const std::string text = describe(event);
        check(text.find("?") == std::string::npos && text.size() > 10, "describe: every new event has a name");
    }
}

}  // namespace

int main() {
    testBlast(5.0f);
    testBlast(3.0f);
    testChain();
    testHurtByBlastsOnly();
    testStallAmongMetal();
    testCannon();
    testCooldown();
    testGraze();
    testDebris();
    testTeleport();
    testTeleportKeepsTheBall();
    testFirework();
    testFireworkBlow();
    testFireworkInAGap();
    testSpring();
    testVibro();
    testFlicker();
    testDestroyedObjects();
    testSpeedPrizesAndObjects();
    testBigBallEntersASource();
    testExitLaunchClearsThePlates();
    testBallInATeleportMeetsThePrizes();
    testExplosiveBallAndObjects();
    testEnd();
    testDescribe();
    if (failures > 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::printf("special object tests passed\n");
    return 0;
}
