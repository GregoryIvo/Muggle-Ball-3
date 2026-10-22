// Tests of the level's special objects (SpecialObjects, docs/GAMEPLAY.md sections 14 and 15) on made-up levels: dynamite and
// its chains, the interactive cannon and its cannonball, teleports, firework crates, springs, volcanoes, generators, metal,
// vibros and flickers, run in a GameSession with the physics on the calling thread. Needs neither a GPU nor game data. The
// same objects in the real levels: mb_special_object_smoke.

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
    check(g.runUntil([&] { return g.count(EventType::Blast) > 0; }, 2), name + " dynamite: a ball sets it off");
    const std::optional<GameEvent> blast = g.first(EventType::Blast);
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
    const auto blasts = g.all(EventType::Blast);
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
    check(big.count(EventType::Blast) == 25 && big.count(EventType::BrickDestroyed) == 25 && big.session.score() == 25 * 120,
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
    g.shoot({11.45f, 0.6f, 4}, {0, 0, 1});
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

void testFirework() {
    Game g(levelOf({board(), firework({0, 0, 10})}));
    const int id = g.shoot({0, 0.6f, 0}, {0, 0, 1});
    check(g.runUntil([&] { return g.ball(id) != nullptr && g.ball(id)->velocity.z < 0; }, 2), "firework: the ball reaches the crate");
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
          "firework: no score, and the crate stays");

    // While one is up another touch starts nothing; once it burst, a touch does.
    const auto touch = [&] {
        const Vec3 crate = g.where(1);
        g.shoot({crate.x, 0.6f, crate.z - 10}, {0, 0, 1});
    };
    g.log.clear();
    touch();
    g.run(1.0);  // the first ball is on its way, the second's touch comes with the rocket pending
    touch();
    g.run(0.3);
    touch();  // and with it up
    g.run(2.5);
    check(g.count(EventType::RocketLaunched) == 1 && g.count(EventType::RocketBurst) == 1,
          "firework: touched while a rocket is pending or up, a crate starts no other");
    touch();
    g.run(2.5);
    check(g.count(EventType::RocketLaunched) == 2 && g.count(EventType::RocketBurst) == 2,
          "firework: once it burst, a touch starts the next");
    check(g.session.stats().bricksDestroyed == 0, "firework: and nothing is destroyed");
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
    for (const EventType type : {EventType::Blast, EventType::CannonFired, EventType::ShotHit, EventType::RocketLaunched,
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
    testCannon();
    testCooldown();
    testGraze();
    testDebris();
    testTeleport();
    testTeleportKeepsTheBall();
    testFirework();
    testSpring();
    testVibro();
    testFlicker();
    testEnd();
    testDescribe();
    if (failures > 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::printf("special object tests passed\n");
    return 0;
}
