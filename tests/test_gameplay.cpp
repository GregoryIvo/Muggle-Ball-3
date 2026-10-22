// Gameplay rules tests: the bat, the ball, bricks that take hits and explode, hearts and the end of the level, on made-up
// levels (a walled arena with a few blocks) run in a GameSession with the physics on the calling thread. Needs neither a
// GPU nor game data. The same rules against the real levels: mb_gameplay_smoke.

#include "mb/game/Autopilot.h"
#include "mb/game/Gameplay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
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

bool near(Vec3 a, Vec3 b, float tolerance = 1e-3f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

// ---- Materials and levels ----------------------------------------------------------------------------------------

// Shark.jpg: a brick of one hit with the shark's numbers; Tough.jpg: three hits, three textures worst first; Pirate.jpg: a
// person, who shrinks away when destroyed (exploseType 5); Crate.jpg and Tile.jpg: type 4, ground and furniture; Tree:
// type 5; Steel.jpg: type 2, which nothing damages.
const MaterialLibrary& materials() {
    static const MaterialLibrary library = MaterialLibrary::parse(R"({"version": 1, "materials": [
      {"name": "BallMaterial", "density": 1, "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 0.999},
       "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "BoardMaterial", "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 1.0},
       "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "Steel.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 2, "energy": 1, "score": 10}},
      {"name": "Shark.jpg", "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 91, "exploseType": 3, "exploseDelay": 250},
       "sounds": {"hit": "shark_hit", "dead": "shark_dead"}},
      {"name": "Pirate.jpg", "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 3, "score": 290, "exploseType": 5, "exploseDelay": 100}},
      {"name": "Tough.jpg", "textures": ["Tough-3", "Tough-2", "Tough-1"], "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 3, "score": 100, "exploseType": 6, "exploseDelay": 100}},
      {"name": "Crate.jpg", "physics": {"restitution": 0.5},
       "brick": {"type": 4, "energy": 1, "score": 20, "exploseDelay": 0}},
      {"name": "Slow.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 1, "score": 77, "exploseDelay": 500}},
      {"name": "Tile.jpg", "physics": {"restitution": 0.5},
       "brick": {"type": 4, "energy": 1, "score": 10, "exploseDelay": 50}},
      {"name": "Tree", "physics": {"restitution": 0.5}, "brick": {"type": 5, "energy": 2, "score": 130, "exploseDelay": 100}},
      {"name": "Sail", "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 1, "score": 60, "exploseDelay": 10}},
      {"name": "Huge.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 100000, "score": 1, "exploseDelay": 0}},
      {"name": "Metall.jpg", "physics": {"restitution": 0.1}, "brick": {"type": 0, "energy": 1, "score": 50, "exploseDelay": 100}}
    ]})");
    return library;
}

constexpr std::uint32_t kDynamic = 0x100, kKinematic = 0x180, kStatic = 0x17E;  // NxBodyFlag, with the visualization bit

Shape box(Vec3 half, Vec3 at = {}, std::uint16_t group = 2) {
    Shape s;
    s.type = ShapeType::Box;
    s.halfExtents = half;
    s.localPose.position = at;
    s.group = group;
    s.shapeFlags = 8;
    return s;
}

Actor actor(ObjectType type, Vec3 at, std::uint32_t flags, float mass, std::vector<Shape> shapes,
            const std::string& material) {
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

// A brick whose top is at y = 2 by default: the ball (y 0.6) meets its side.
Actor brick(Vec3 at, const std::string& material, std::uint32_t flags = kDynamic, Vec3 half = {1, 1, 1},
            float mass = 50) {
    return actor(ObjectType::Brick, at, flags, mass, {box(half)}, material);
}

// The board as the levels have it: a floor with its top at y = 0, side walls with their inner faces at x = +-20.4, a far
// wall at z = 22.6 and, unless `nearWall` is false, one at z = -20.3 in front of the bat's line (the bat's end is open in
// a real level).
Actor board(bool nearWall = true, bool farWall = true) {
    std::vector<Shape> shapes = {box({30, 0.5f, 30}, {0, -0.5f, 0}, 4), box({1, 2, 30}, {-21.4f, 2, 0}, 4),
                                 box({1, 2, 30}, {21.4f, 2, 0}, 4)};
    if (farWall) {
        shapes.push_back(box({30, 2, 1}, {0, 2, 23.6f}, 4));
    }
    if (nearWall) {
        shapes.push_back(box({30, 2, 1}, {0, 2, -21.3f}, 4));
    }
    return actor(ObjectType::Board, {0, 0, 0}, 0xFF, 1000, std::move(shapes), "BoardMaterial");
}

Level levelOf(std::vector<Actor> actors, std::vector<Joint> joints = {}) {
    Level level;
    level.gravity = {0, -9.8f, 0};
    level.actors = std::move(actors);
    level.joints = std::move(joints);
    return level;
}

SessionOptions options(TargetRule rule = TargetRule::Never) {
    SessionOptions o;
    o.levelEnd = rule;
    o.physicsSettings.threads = 0;  // on the caller, so that a run repeats exactly
    o.rules.prizes.drops = false;   // the rules of the bricks and the ball alone; the prizes have tests/test_prizes.cpp
    return o;
}

// A session being played, with every event it reported.
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
    void run(double seconds, const GameInput& input = {}) {
        for (int i = 0, steps = static_cast<int>(std::lround(seconds * 60)); i < steps; ++i) {
            step(input);
        }
    }
    // Steps until `done`; false if it did not happen within `limit` seconds.
    bool runUntil(const std::function<bool()>& done, double limit, const GameInput& input = {}) {
        for (int i = 0, steps = static_cast<int>(std::lround(limit * 60)); i < steps; ++i) {
            if (done()) {
                return true;
            }
            step(input);
        }
        return done();
    }
    bool ready() { return runUntil([&] { return session.phase() == SessionPhase::Ready; }, 3); }

    int count(EventType type) const {
        return static_cast<int>(std::ranges::count(log, type, &GameEvent::type));
    }
    const GameEvent* find(EventType type, std::size_t from = 0) const {
        for (std::size_t i = from; i < log.size(); ++i) {
            if (log[i].type == type) {
                return &log[i];
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

// ---- Tests --------------------------------------------------------------------------------------------------------

void testPredicates() {
    const GameMaterial& shark = *materials().find("Shark.jpg");
    const GameMaterial& crate = *materials().find("Crate.jpg");
    const GameMaterial& steel = *materials().find("Steel.jpg");
    const GameMaterial& tree = *materials().find("Tree");
    const GameMaterial& tough = *materials().find("Tough.jpg");
    check(canBeDamaged(ObjectType::Brick, &shark) && canBeDamaged(ObjectType::Brick, &crate) &&
              canBeDamaged(ObjectType::Tree, &tree) && canBeDamaged(ObjectType::Metall, materials().find("Metall.jpg")),
          "damage: bricks, trees and metal with a brick material can be damaged");
    check(!canBeDamaged(ObjectType::Brick, &steel) && !canBeDamaged(ObjectType::Brick, nullptr) &&
              !canBeDamaged(ObjectType::Brick, materials().find("BoardMaterial")),
          "damage: a material of type 2, or none at all, never");
    check(!canBeDamaged(ObjectType::Board, &shark) && !canBeDamaged(ObjectType::Light, &shark) &&
              !canBeDamaged(ObjectType::Background, &shark) && !canBeDamaged(ObjectType::ShadowCaster, &shark) &&
              !canBeDamaged(ObjectType::Cloth, materials().find("Sail")),
          "damage: the board, the markers and a cloth never, whatever their material says");

    // Every brick and tree, moving or not, against bricks of material type 0.
    const GameMaterial& tile = *materials().find("Tile.jpg");
    check(isTarget(TargetRule::Bricks, ObjectType::Brick, &shark) && isTarget(TargetRule::Bricks, ObjectType::Brick, &crate) &&
              isTarget(TargetRule::Bricks, ObjectType::Brick, &tile) && isTarget(TargetRule::Bricks, ObjectType::Tree, &tree),
          "required: a brick of type 0 or of type 4 (a shark, a crate, a ground tile) and a tree");
    check(isTarget(TargetRule::BrickType, ObjectType::Brick, &shark) && !isTarget(TargetRule::BrickType, ObjectType::Brick, &crate) &&
              !isTarget(TargetRule::BrickType, ObjectType::Tree, &tree),
          "H-type: a brick of type 0 is a target; a crate (type 4) and a tree (5) are not");
    check(!isTarget(TargetRule::BrickType, ObjectType::Brick, &steel) && !isTarget(TargetRule::Bricks, ObjectType::Brick, &steel) &&
              !isTarget(TargetRule::Never, ObjectType::Brick, &shark),
          "targets: what cannot be damaged is no target, and nothing is one under Never");

    // The rule of the footage, the default: every brick and tree. Nothing else is required, though all of it can be damaged.
    check(SessionOptions{}.levelEnd == TargetRule::Bricks && !GameplayRules{}.fallScores,
          "required: the default is the rule the footage shows, and what falls scores nothing");
    bool others = true, damageable = true;
    for (const ObjectType type :
         {ObjectType::Metall, ObjectType::Cannon, ObjectType::TeleportSrc, ObjectType::TeleportDest, ObjectType::Dynamite,
          ObjectType::Firework, ObjectType::GeneratorGood, ObjectType::GeneratorBad, ObjectType::Spring, ObjectType::Volcano,
          ObjectType::Vibro, ObjectType::Flicker}) {
        const bool portal = type == ObjectType::TeleportSrc || type == ObjectType::TeleportDest;
        others = others && !isTarget(TargetRule::Bricks, type, &shark);
        damageable = damageable && canBeDamaged(type, &shark) != portal;
    }
    check(others && damageable,
          "required: not metal, portals, cannons, dynamite, fireworks, generators, springs, volcanoes, vibros or flickers; a blast "
          "can still destroy them, but for the portals, which nothing hurts (a charge 3.8 units away left one standing: "
          "docs/GAMEPLAY.md section 17.1)");

    // What shrinks away: the materials of exploseType 5.
    check(shrinksAway(materials().find("Pirate.jpg")) && !shrinksAway(&shark) && !shrinksAway(&crate) && !shrinksAway(&steel) &&
              !shrinksAway(nullptr),
          "shrink: a person (exploseType 5) does; a shark (3), a crate (none) and steel do not");

    check(damageTextureIndex(tough, 3) == 2 && damageTextureIndex(tough, 2) == 1 && damageTextureIndex(tough, 1) == 0 &&
              damageTextureIndex(tough, 0) == 0 && damageTextureIndex(tough, 9) == 2,
          "texture: the index is energy - 1, held to the textures there are");
    check(damageTextureIndex(shark, 1) == 0, "texture: a material with one texture (or none) always shows the first");
}

void testBallSpeedStaysAndStaysOnThePlane() {
    // A walled arena with blocks of steel to bounce off, one of them loose, for a minute.
    Actor loose = actor(ObjectType::Brick, {-4, 1, 12}, kDynamic, 3000, {box({1.5f, 1, 1.5f})}, "Steel.jpg");
    Game g(levelOf({board(), actor(ObjectType::Brick, {6, 1, 8}, kStatic, 100, {box({2, 1, 1})}, "Steel.jpg"),
                    actor(ObjectType::Brick, {-9, 1, 4}, kStatic, 100, {box({1, 1, 3})}, "Steel.jpg"),
                    actor(ObjectType::Brick, {13, 1, -8}, kKinematic, 100, {box({1, 1, 1})}, "Steel.jpg"), loose}));
    check(g.ready(), "speed: the first bat comes in");
    g.step({.batTargetX = 5, .launch = true});
    check(g.session.balls().size() == 1 && !g.session.balls()[0].held, "speed: launched");

    float worstSpeed = 0, worstHeight = 0, worstSideways = 0, longestStep = 0;
    double path = 0;
    Vec3 last = g.session.balls()[0].position;
    for (int i = 0; i < 60 * 60; ++i) {
        g.step({.batTargetX = 5});
        check(g.session.balls().size() == 1, "speed: the ball stays in play");
        if (g.session.balls().empty()) {
            return;
        }
        const BallView& ball = g.session.balls()[0];
        worstSpeed = std::max(worstSpeed, std::fabs(std::hypot(ball.velocity.x, ball.velocity.z) - 15.0f));
        worstSideways = std::max(worstSideways, std::fabs(ball.velocity.y));
        worstHeight = std::max(worstHeight, std::fabs(ball.position.y - 0.6f));
        const float moved = length(ball.position - last);
        longestStep = std::max(longestStep, moved);
        path += moved;
        last = ball.position;
    }
    check(worstSpeed < 0.15f, "speed: 15 units/s +-1% after every step for 60 s, worst " + std::to_string(worstSpeed));
    check(worstHeight < 1e-3f && worstSideways == 0, "plane: the ball stays on y = 0.6 and never moves up or down");
    check(longestStep < 15.0f / 60 * 1.02f, "speed: it never covers more than a step's worth in a step");
    check(path / 60 > 14.3, "speed: and over the minute covers about 15 units/s, " + std::to_string(path / 60));
    check(g.count(EventType::BoardHit) > 15 && g.count(EventType::BallLost) == 0,
          "speed: it bounced off the walls (" + std::to_string(g.count(EventType::BoardHit)) + " hits) and was not lost");
    check(g.session.stats().batHits + g.count(EventType::BoardHit) > 15, "speed: ... and kept bouncing");
    check(g.count(EventType::BallFreed) == 0, "speed: a ball in free flight is never taken for a pinned one");
}

void testSlopesDoNotLiftTheBall() {
    // A wedge, turned 35 degrees about x, whose faces push the ball up as well as back: the ball is on its plane all the same.
    Actor wedge = actor(ObjectType::Brick, {0, 1, 10}, kStatic, 100, {box({3, 1.5f, 1})}, "Steel.jpg");
    const float c = std::cos(radians(35.0f)), sn = std::sin(radians(35.0f));
    wedge.pose.rotation = {1, 0, 0, 0, c, -sn, 0, sn, c};
    Game g(levelOf({board(), wedge}));
    g.session.addBall({0.5f, 0.6f, 0}, {0, 0, 1});
    bool pushedUp = false, onPlane = true;
    for (int i = 0; i < 90; ++i) {
        g.step();
        for (const ContactEvent& contact : g.session.scene().contacts()) {
            pushedUp = pushedUp || ((contact.userDataA == 1 || contact.userDataB == 1) && std::fabs(contact.normal.y) > 0.3f);
        }
        const BallView& ball = g.session.balls()[1];
        onPlane = onPlane && near(ball.position.y, 0.6f, 1e-4f) && ball.velocity.y == 0;
    }
    check(pushedUp, "slope: the wedge was hit on a face that leans");
    check(onPlane, "slope: and the ball never left its plane");
    check(g.session.balls()[1].velocity.z < 0, "slope: it bounced back");
}

void testLaunch() {
    Game g(levelOf({board()}));
    g.step({.batTargetX = 3, .launch = true});  // too early: the bat is still swooping in
    check(g.count(EventType::BallLaunched) == 0, "launch: a click while the bat swoops in does nothing");
    check(g.ready(), "launch: ready after the swoop");
    const double ready = g.session.time();
    check(near(static_cast<float>(ready), 0.7f, 0.02f), "launch: the swoop takes 0.7 s at the start of a level");
    check(near(static_cast<float>(g.session.bat().phaseDuration), 0.0f) &&
              near(static_cast<float>(Game(levelOf({board()})).session.bat().phaseDuration), 0.7f),
          "launch: the bat says how long its swoop lasts, 0.7 s, and nothing once it is there");
    g.run(1);
    check(g.count(EventType::BallLaunched) == 0 && g.session.phase() == SessionPhase::Ready,
          "launch: the ball waits for the click, with no auto-launch");

    g.step({.batTargetX = 3, .launch = true});
    const GameEvent* launched = g.find(EventType::BallLaunched);
    check(launched != nullptr && near(launched->position, {g.session.bat().position.x - 0.75f, 0.6f, -17.3f}),
          "launch: from 0.75 left of the bat's centre, 3.7 in front of it");
    check(g.session.phase() == SessionPhase::Playing && g.session.stats().ballsLaunched == 1, "launch: now in play");
    const BallView& ball = g.session.balls()[0];
    const float angle = radians(GameplayRules{}.launchAngleDegrees);
    check(near(GameplayRules{}.launchAngleDegrees, 11.0f, 0.5f) &&
              near(ball.velocity, {-std::sin(angle) * 15, 0, std::cos(angle) * 15}, 1e-3f),
          "launch: 11 degrees left of +z at 15 units/s, at once (velocity " + std::to_string(ball.velocity.x) + ", " +
              std::to_string(ball.velocity.z) + ")");
    check(near(std::atan2(-ball.velocity.x, ball.velocity.z), angle, 1e-3f), "launch: to the left");
    g.step({.batTargetX = 3, .launch = true});
    check(g.count(EventType::BallLaunched) == 1, "launch: a second click has no ball to launch");

    // The angle does not depend on where the bat is.
    Game other(levelOf({board()}));
    other.run(0.1, {.batTargetX = -12});
    other.ready();
    other.step({.batTargetX = -12, .launch = true});
    check(near(other.session.balls()[0].velocity, ball.velocity, 1e-3f), "launch: the same velocity wherever the bat is");
}

void testWallsTurnTheBall() {
    // The walls are not mirrors: friction takes sideways speed off a ball that comes in at a slant, and the spin it gets
    // turns the next bounce. From no spin at 45 degrees the ball comes off between 28 and 44 degrees from the normal, not
    // at 45, and at the far wall the same.
    Game g(levelOf({board()}));
    const int right = g.session.addBall({15, 0.6f, 0}, {1, 0, 1});
    g.runUntil([&] { return g.count(EventType::BoardHit) == 1; }, 2);
    const Vec3 after = g.ball(right)->velocity;
    const float offNormal = std::atan2(after.z, -after.x) * 180 / kPi;
    check(after.x < 0 && after.z > 0 && offNormal > 28 && offNormal < 44,
          "friction: off the right wall at 45 degrees the ball leaves at " + std::to_string(offNormal) + " degrees from the normal");
    Game far(levelOf({board()}));
    const int ahead = far.session.addBall({0, 0.6f, 15}, {1, 0, 1});
    far.runUntil([&] { return far.count(EventType::BoardHit) == 1; }, 2);
    const Vec3 back = far.ball(ahead)->velocity;
    const float offFar = std::atan2(back.x, -back.z) * 180 / kPi;
    check(back.z < 0 && back.x > 0 && offFar > 28 && offFar < 44,
          "friction: and off the far wall at " + std::to_string(offFar) + " degrees");
    const GameEvent* hit = far.find(EventType::BoardHit);
    check(hit != nullptr && hit->speed > 10 && near(hit->position.z, 22.6f, 0.1f) && hit->actor == 0,
          "friction: the event is a board hit at the far wall's face, z = 22.6");

    // Straight on is a clean bounce.
    Game straight(levelOf({board()}));
    const int normal = straight.session.addBall({3, 0.6f, 15}, {0, 0, 1});
    straight.runUntil([&] { return straight.count(EventType::BoardHit) == 1; }, 2);
    check(near(straight.ball(normal)->velocity.z, -15 * std::cos(radians(5.0f)), 0.1f) &&
              std::fabs(straight.ball(normal)->velocity.x) < 1.4f,
          "friction: straight on it comes straight back (but for the 5 degree turn out of the vertical)");
}

void testStraightPathsAreTurnedOut() {
    // Friction takes the sideways speed off a ball that bounces between two parallel walls; left alone it ends up going
    // straight up and down, or along x, for ever. After a bounce a path nearer an axis than 10 degrees to x, or 5 to z,
    // is turned out to that angle, to the side it was going (towards the middle of the board if it went to none).
    const float flat = radians(10.0f), steep = radians(5.0f);
    Game g(levelOf({board()}));
    const int along = g.session.addBall({-10, 0.6f, 0}, {1, 0, 0});
    const int below = g.session.addBall({-10, 0.6f, 5}, {1, 0, -0.01f});
    const int up = g.session.addBall({3, 0.6f, 0}, {0, 0, 1});
    const int downLeft = g.session.addBall({-3, 0.6f, 6}, {-0.01f, 0, 1});
    g.step();
    check(near(g.ball(along)->velocity, {15, 0, 0}, 0.01f) && near(g.ball(up)->velocity, {0, 0, 15}, 0.01f),
          "straight: a path along an axis is left alone until it bounces");
    g.runUntil([&] { return g.count(EventType::BoardHit) >= 4; }, 4);
    check(near(g.ball(along)->velocity, {-15 * std::cos(flat), 0, 15 * std::sin(flat)}, 0.01f),
          "straight: off the wall along x, it goes back 10 degrees up the board");
    check(near(g.ball(below)->velocity, {-15 * std::cos(flat), 0, -15 * std::sin(flat)}, 0.01f),
          "straight: and keeps going down if it was");
    check(near(g.ball(up)->velocity, {-15 * std::sin(steep), 0, -15 * std::cos(steep)}, 0.01f),
          "straight: off the far wall, straight on, it turns 5 degrees towards the middle");
    check(near(g.ball(downLeft)->velocity, {-15 * std::sin(steep), 0, -15 * std::cos(steep)}, 0.01f),
          "straight: and to the side it was going if it went to one");
    const int steepEnough = g.session.addBall({-10, 0.6f, 10}, {1, 0, 1});
    g.run(0.1);
    check(near(g.ball(steepEnough)->velocity / 15.0f, {std::sqrt(0.5f), 0, std::sqrt(0.5f)}, 0.01f),
          "straight: a path at 45 degrees is left as it is");
}

void testHeldBallFollowsTheBat() {
    Game g(levelOf({board()}));
    float previous = 0, peak = 0;
    bool overshot = false, followed = true;
    g.run(0.3, {.batTargetX = 0});
    for (int i = 0; i < 90; ++i) {
        const GameInput input{.batTargetX = i < 45 ? 10.0f : -6.0f};
        g.step(input);
        const BatView& bat = g.session.bat();
        const BallView& ball = g.session.balls()[0];
        followed = followed && ball.held && near(ball.position, {bat.position.x - 0.75f, 0.6f, bat.position.z + 3.7f});
        peak = std::max(peak, std::fabs(bat.velocityX));
        overshot = overshot || (i < 45 && bat.position.x > 10.0001f) || (i >= 45 && bat.position.x < -6.0001f);
        previous = bat.position.x;
    }
    check(followed, "held: the ball sits 0.75 left of the bat's centre and 3.7 in front of it, and follows it");
    check(near(previous, -6, 0.05f), "held: the bat reaches the target");
    check(!overshot, "bat: it eases in without overshooting");
    check(peak > 60, "bat: no speed limit below 60 units/s (peak " + std::to_string(peak) + ")");

    // The ramp of V1 60.9-61.1: a 4.5 unit move peaks at about 42 units/s.
    Game small(levelOf({board()}));
    small.run(0.3, {.batTargetX = 0});
    float smallPeak = 0;
    for (int i = 0; i < 30; ++i) {
        small.step({.batTargetX = 4.5f});
        smallPeak = std::max(smallPeak, small.session.bat().velocityX);
    }
    check(near(smallPeak, 42, 3), "bat: a 4.5 unit move peaks at about 42 units/s, " + std::to_string(smallPeak));

    Game far(levelOf({board()}));
    far.run(1, {.batTargetX = 40});
    check(near(far.session.bat().position.x, 16) && near(far.session.bat().position.z, -21) &&
              near(far.session.bat().position.y, 0.6f),
          "bat: the centre stops at x = 16, on the line z = -21");
    far.run(1, {.batTargetX = -40});
    check(near(far.session.bat().position.x, -16) && near(far.session.bat().position.z, -21),
          "bat: and at -16, still on the same line");
}

// Meets the bat from `start` along `direction` and returns the hit event and the ball's velocity after it.
struct BatMeeting {
    bool hit = false;
    Vec3 ballAt{};
    Vec3 batAt{};
    Vec3 velocity{};
    int hits = 0;
};

BatMeeting meetBat(Vec3 start, Vec3 direction, float batX = 0) {
    Game g(levelOf({board(false)}));
    g.run(0.1, {.batTargetX = batX});
    const int id = g.session.addBall(start, direction);
    BatMeeting meeting;
    meeting.hit = g.runUntil([&] { return g.count(EventType::BatHit) > 0; }, 4, {.batTargetX = batX});
    if (!meeting.hit) {
        return meeting;
    }
    const GameEvent* hit = g.find(EventType::BatHit);
    meeting.ballAt = hit->position;
    meeting.batAt = g.session.bat().position;
    meeting.velocity = g.ball(id)->velocity;
    g.run(0.5, {.batTargetX = batX});
    meeting.hits = g.count(EventType::BatHit);
    return meeting;
}

void testBatReflection() {
    // The point of the bat's circle (radius 3.6 with the ball's) at offset d from its centre, and a ball aimed at it from
    // different directions: the ball leaves along (ball - bat), whatever it came with.
    struct Case {
        float offset;
        Vec3 from;
    };
    const Case cases[] = {{0.0f, {0, 0.6f, -8}},    {0.8f, {5, 0.6f, -8}},    {-1.9f, {-9, 0.6f, -8}},
                          {1.9f, {1.9f, 0.6f, -9}}, {-2.9f, {2, 0.6f, -6}},   {3.3f, {9, 0.6f, -12}},
                          {-3.4f, {-3.4f, 0.6f, -12}}};
    std::vector<float> offsets;
    for (const Case& c : cases) {
        const Vec3 aim{c.offset, 0.6f, -21 + std::sqrt(3.6f * 3.6f - c.offset * c.offset)};
        const BatMeeting m = meetBat(c.from, aim - c.from);
        const std::string label = "reflection (aimed at " + std::to_string(c.offset) + ")";
        check(m.hit, label + ": the ball meets the bat");
        if (!m.hit) {
            continue;
        }
        const float dx = m.ballAt.x - m.batAt.x, dz = m.ballAt.z - m.batAt.z;
        offsets.push_back(dx);
        check(near(std::hypot(dx, dz), 3.6f, 0.12f), label + ": the ball is a ball plus a bat radius from the centre");
        const float expected = std::clamp(std::atan2(dx, dz), radians(-70.0f), radians(70.0f));
        const Vec3 out = m.velocity / 15.0f;
        check(near(out, {std::sin(expected), 0, std::cos(expected)}, 2e-3f),
              label + ": it leaves along (ball - bat), with the exit held to 70 degrees from +z");
        check(m.velocity.z > 4.9f, label + ": and away from the bat, never back");
        check(m.hits == 1, label + ": one hit for one visit");
    }
    check(offsets.size() == 7 && *std::ranges::min_element(offsets) < -3.2f && *std::ranges::max_element(offsets) > 3.1f &&
              std::ranges::any_of(offsets, [](float d) { return std::fabs(d) < 0.3f; }),
          "reflection: the cases cover the middle and both edges");

    // Straight up off the middle (the straight-path rules leave a bat's exit alone).
    const BatMeeting middle = meetBat({0, 0.6f, -8}, {0, 0, -1});
    check(middle.hit && near(middle.velocity, {0, 0, 15}, 0.3f), "reflection: off the middle it goes straight up");

    // A ball that meets the bat along its flank, just in front of its line, is sent forward too, to the side it came from.
    const BatMeeting side = meetBat({-9, 0.6f, -20.7f}, {1, 0, 0});
    check(side.hit && side.velocity.z > 4.9f && side.velocity.x < 0,
          "reflection: from the side, level with the centre, it still leaves forward and to the side it came from");
    const BatMeeting otherSide = meetBat({9, 0.6f, -20.7f}, {-1, 0, 0});
    check(otherSide.hit && otherSide.velocity.z > 4.9f && otherSide.velocity.x > 0,
          "reflection: and from the other side");

    // A bat that chases the ball it has just hit, at over 100 units/s, runs into it again and again while it is in touch:
    // that is one hit (without the rule it is three).
    Game chase(levelOf({board(false)}));
    chase.run(0.1, {.batTargetX = 0});
    const Vec3 edge{3.3f, 0.6f, -21 + std::sqrt(3.6f * 3.6f - 3.3f * 3.3f)};
    chase.session.addBall({9, 0.6f, -12}, edge - Vec3{9, 0.6f, -12});
    float target = 0;
    for (int i = 0; i < 90; ++i) {
        target = chase.count(EventType::BatHit) > 0 ? 16.0f : 0.0f;
        chase.step({.batTargetX = target});
    }
    check(chase.count(EventType::BatHit) == 1, "reflection: a bat running into the ball it just hit is one hit (" +
                                                   std::to_string(chase.count(EventType::BatHit)) + " hits)");

    // The bat's own movement does not matter: the same law with the bat on the run.
    Game g(levelOf({board(false)}));
    g.run(0.2, {.batTargetX = -10});
    const int id = g.session.addBall({3, 0.6f, -8}, {0, 0, -1});
    bool moving = false;
    for (int i = 0; i < 90 && g.count(EventType::BatHit) == 0; ++i) {
        g.step({.batTargetX = i < 12 ? -10.0f : 3.0f});
        moving = moving || std::fabs(g.session.bat().velocityX) > 20;
    }
    const GameEvent* hit = g.find(EventType::BatHit);
    check(hit != nullptr && moving, "reflection: the bat was on the run when the ball met it");
    if (hit != nullptr) {
        const Vec3 d = hit->position - g.session.bat().position;
        check(near(g.ball(id)->velocity, normalize({d.x, 0, d.z}) * 15.0f, 0.3f) && g.ball(id)->velocity.z > 0,
              "reflection: and the ball leaves along (ball - bat) all the same");
    }
}

// Plays until the last ball is lost with the bat out of the way.
void loseTheBall(Game& g) {
    check(g.ready(), "lose: the ball is held");
    const std::size_t from = g.log.size();
    g.step({.batTargetX = 16, .launch = true});
    check(g.runUntil([&] { return g.find(EventType::BallLost, from) != nullptr; }, 20, {.batTargetX = 16}),
          "lose: the ball goes past the bat");
}

void testLosingTheBall() {
    Game g(levelOf({board(false)}));
    loseTheBall(g);
    const GameEvent* lost = g.find(EventType::BallLost);
    check(lost != nullptr && lost->position.z < -21.0f && lost->position.z > -21.3f && lost->velocity.z < -5,
          "lost: when the ball's centre passes the bat's line, z = -21, with the way it was going");
    check(g.session.balls().empty() && g.session.phase() == SessionPhase::Lost && g.session.hearts() == 2,
          "lost: no ball, nothing spent yet");
    check(g.session.bat().phase == BatPhase::Present, "lost: the bat is still there for a moment");
    const double t0 = lost->time;

    g.runUntil([&] { return g.find(EventType::BatLeft) != nullptr; }, 2);
    check(near(static_cast<float>(g.find(EventType::BatLeft)->time - t0), 0.6f, 0.03f) &&
              g.session.bat().phase == BatPhase::Leaving && near(static_cast<float>(g.session.bat().phaseDuration), 0.4f),
          "lost: the bat leaves 0.6 s later, for 0.4 s");
    g.runUntil([&] { return g.session.bat().phase == BatPhase::Absent; }, 2);
    check(near(static_cast<float>(g.session.time() - t0), 1.0f, 0.03f) && g.session.hearts() == 2,
          "lost: and is gone by 1.0 s, still with both hearts");
    g.runUntil([&] { return g.find(EventType::LifeLost) != nullptr; }, 3);
    const GameEvent* heart = g.find(EventType::LifeLost);
    check(heart != nullptr && near(static_cast<float>(heart->time - t0), 2.1f, 0.03f) && heart->hearts == 1 &&
              g.session.hearts() == 1,
          "lost: a heart is spent 2.1 s after the loss");
    g.run(0.1);
    const GameEvent* entered = nullptr;
    for (const GameEvent& e : g.log) {
        if (e.type == EventType::BatEntered && e.time > t0) {
            entered = &e;
        }
    }
    check(entered != nullptr && near(static_cast<float>(entered->time - heart->time), 0.0f, 0.04f) &&
              g.session.phase() == SessionPhase::Entering && g.session.bat().phase == BatPhase::Entering &&
              near(static_cast<float>(g.session.bat().phaseDuration), 0.53f, 0.03f),
          "lost: the new bat swoops in as the heart is spent, for 0.53 s");
    check(g.session.balls().size() == 1 && g.session.balls()[0].held, "lost: with the ball on it");
    g.runUntil([&] { return g.session.phase() == SessionPhase::Ready; }, 2);
    check(near(static_cast<float>(g.session.time() - t0), 2.63f, 0.03f) && g.session.bat().phase == BatPhase::Present,
          "lost: and it can be launched at 2.63 s");
    check(g.session.stats().ballsLost == 1 && g.session.hearts() == 1, "lost: one ball lost, one heart spent");

    // The next ball launches as the first did.
    g.step({.batTargetX = 16, .launch = true});
    check(g.session.phase() == SessionPhase::Playing && g.count(EventType::BallLaunched) == 2, "lost: and is launched");
}

void testGameOver() {
    Game g(levelOf({board(false)}));
    int lostBalls = 0;
    for (int life = 0; life < 3; ++life) {
        if (g.session.phase() == SessionPhase::GameOver) {
            break;
        }
        loseTheBall(g);
        ++lostBalls;
        g.runUntil([&] { return g.session.phase() == SessionPhase::Ready || g.session.phase() == SessionPhase::GameOver; }, 4);
    }
    check(lostBalls == 3 && g.session.phase() == SessionPhase::GameOver,
          "game over: the third ball with two hearts to spend is the last");
    check(g.count(EventType::LifeLost) == 2 && g.count(EventType::GameOver) == 1 && g.session.hearts() == 0,
          "game over: two hearts spent, then the game is over");
    check(g.count(EventType::BatEntered) == 3, "game over: three bats came (the first and two after a loss)");
    g.run(5);
    check(g.count(EventType::BatEntered) == 3 && g.session.balls().empty() && g.session.phase() == SessionPhase::GameOver,
          "game over: no bat and no ball come after it");
    const GameEvent* over = g.find(EventType::GameOver);
    const GameEvent* last = nullptr;
    for (const GameEvent& e : g.log) {
        last = e.type == EventType::BallLost ? &e : last;
    }
    check(over != nullptr && last != nullptr && near(static_cast<float>(over->time - last->time), 2.1f, 0.03f),
          "game over: 2.1 s after the last ball, when the heart would have been spent");

    // With no hearts to begin with, the first loss ends it.
    SessionOptions none = options();
    none.hearts = 0;
    Game bare(levelOf({board(false)}), none);
    loseTheBall(bare);
    bare.run(3);
    check(bare.count(EventType::GameOver) == 1 && bare.count(EventType::LifeLost) == 0, "game over: with no hearts at all, at once");
}

void testMultiball() {
    Game g(levelOf({board(false)}));
    check(g.ready(), "multiball: ready");
    g.step({.batTargetX = 16, .launch = true});
    const int second = g.session.addBall({-8, 0.6f, 0}, {0, 0, -1});
    const int third = g.session.addBall({-12, 0.6f, 0}, {0.2f, 0, -1});
    check(g.session.balls().size() == 3 && second != third, "multiball: three balls in play");
    const bool twoLost = g.runUntil([&] { return g.count(EventType::BallLost) == 2; }, 10, {.batTargetX = 16});
    check(twoLost && g.session.phase() == SessionPhase::Playing && g.count(EventType::BatLeft) == 0,
          "multiball: losing a ball while others are in play costs nothing");
    check(g.runUntil([&] { return g.count(EventType::BallLost) == 3; }, 20, {.batTargetX = 16}) &&
              g.session.phase() == SessionPhase::Lost,
          "multiball: the last ball lost starts the life sequence");
    g.run(2.3);
    check(g.count(EventType::LifeLost) == 1 && g.session.hearts() == 1, "multiball: and costs one heart, not three");
    for (const BallView& ball : g.session.balls()) {
        check(near(std::hypot(ball.velocity.x, ball.velocity.z), 15, 0.05f) || ball.held,
              "multiball: every ball in flight has the same speed");
    }
}

void testBallEscapes() {
    // A board with no far wall: the ball leaves through the gap and is taken out instead of flying on for ever.
    Game g(levelOf({board(false, false)}));
    check(g.ready(), "escape: ready");
    g.step({.batTargetX = 0, .launch = true});
    check(g.runUntil([&] { return g.count(EventType::BallLost) == 1; }, 10), "escape: the ball is lost");
    const GameEvent* lost = g.find(EventType::BallLost);
    check(lost != nullptr && lost->position.z > 40 && lost->position.z < 41, "escape: beyond z = 40, not behind the bat");
    g.run(3);
    check(g.session.hearts() == 1, "escape: and counts as the last ball");
}

void testBrickDamage() {
    // A brick of three hits that the ball hits three times: it bounces between the brick and a steel block in front of it.
    Actor tough = actor(ObjectType::Brick, {0, 1, 8}, kStatic, 100, {box({3, 1, 1})}, "Tough.jpg");
    Actor steel = actor(ObjectType::Brick, {0, 1, 2}, kStatic, 100, {box({3, 1, 1})}, "Steel.jpg");
    Game g(levelOf({board(), tough, steel}), options(TargetRule::BrickType));
    const ActorIndex brickIndex = 1;
    check(g.session.actor(brickIndex).damageable && g.session.actor(brickIndex).energy == 3 &&
              g.session.actor(brickIndex).maxEnergy == 3 && g.session.actor(brickIndex).textureIndex == 2 &&
              g.session.actor(brickIndex).target && !g.session.actor(2).damageable,
          "damage: a fresh brick has its 3 energy, shows its third (intact) texture and is a target; steel is not damageable");
    g.session.addBall({0.3f, 0.6f, 5}, {0, 0, 1});
    const int stages[3][2] = {{2, 1}, {1, 0}, {0, 0}};  // energy, texture index after the hit
    for (int hit = 0; hit < 3; ++hit) {
        check(g.runUntil([&] { return g.count(EventType::BrickHit) == hit + 1; }, 4), "damage: hit " + std::to_string(hit + 1));
        const ActorStatus status = g.session.actor(brickIndex);
        check(status.energy == stages[hit][0] && status.textureIndex == stages[hit][1],
              "damage: after hit " + std::to_string(hit + 1) + " energy " + std::to_string(status.energy) + ", texture " +
                  std::to_string(status.textureIndex));
        std::size_t seen = 0;
        const GameEvent* event = nullptr;
        for (const GameEvent& e : g.log) {
            if (e.type == EventType::BrickHit && ++seen == static_cast<std::size_t>(hit + 1)) {
                event = &e;
            }
        }
        check(event != nullptr && event->actor == brickIndex && event->energy == stages[hit][0] && event->ball == 1 &&
                  event->material == materials().find("Tough.jpg") && event->speed > 5,
              "damage: the event names the brick, the energy left, its material and the blow");
        if (hit < 2) {
            check(!status.exploding && g.count(EventType::BrickDestroyed) == 0 && g.session.score() == 0,
                  "damage: it still stands and nothing is scored");
        }
    }
    check(g.session.actor(brickIndex).exploding, "damage: out of energy, it is about to explode");
    g.run(0.3);
    check(g.count(EventType::BrickHit) == 3, "damage: three hits, no more");
    const GameEvent* destroyed = g.find(EventType::BrickDestroyed);
    check(destroyed != nullptr && destroyed->points == 100 && g.session.score() == 100 && g.session.actor(brickIndex).removed,
          "damage: it exploded for its 100 points");
    check(g.count(EventType::LevelComplete) == 1 && g.session.phase() == SessionPhase::Complete,
          "damage: the only target gone, the level is complete");
}

void testExplosionDelayAndScore() {
    Game g(levelOf({board(), brick({0, 1, 10}, "Shark.jpg")}));
    const ActorIndex index = 1;
    g.session.addBall({0, 0.6f, 0}, {0, 0, 1});
    const std::size_t bodies = g.session.scene().world().bodyCount();
    check(g.runUntil([&] { return g.count(EventType::BrickHit) == 1; }, 5), "explosion: the ball hits the brick");
    const GameEvent* hit = g.find(EventType::BrickHit);
    check(hit->energy == 0 && g.session.actor(index).exploding && g.session.score() == 0 &&
              g.session.scene().body(index) != 0,
          "explosion: one hit for an energy of 1; it is still there, and nothing is scored yet");
    check(g.runUntil([&] { return g.count(EventType::BrickDestroyed) == 1; }, 1), "explosion: and it explodes");
    const GameEvent* destroyed = g.find(EventType::BrickDestroyed);
    check(near(static_cast<float>(destroyed->time - hit->time), 0.25f, 0.02f), "explosion: exploseDelay (250 ms) after the hit");
    check(destroyed->points == 91 && g.session.score() == 91 && destroyed->material == materials().find("Shark.jpg") &&
              destroyed->material->deadSound == "shark_dead",
          "explosion: +91 at that moment, and the event has the material for the sound");
    check(near(destroyed->position.x, 0, 0.5f) && near(destroyed->position.z, 10, 1.0f), "explosion: where the brick stood");
    check(g.session.scene().body(index) == 0 && g.session.scene().world().bodyCount() == bodies - 1,
          "explosion: its body is out of the world");
    ActorStatus status = g.session.actor(index);
    check(status.removed && !status.exploding && !status.damageable, "explosion: and the actor is marked gone");
    g.run(2);
    check(g.session.score() == 91 && g.count(EventType::BrickDestroyed) == 1 && g.count(EventType::BrickHit) == 1,
          "explosion: once only");

    // exploseDelay 0: at once.
    Game crate(levelOf({board(), brick({0, 1, 10}, "Crate.jpg")}));
    crate.session.addBall({0, 0.6f, 0}, {0, 0, 1});
    crate.runUntil([&] { return crate.count(EventType::BrickDestroyed) == 1; }, 5);
    check(crate.find(EventType::BrickHit) != nullptr && crate.find(EventType::BrickDestroyed) != nullptr &&
              crate.find(EventType::BrickDestroyed)->time == crate.find(EventType::BrickHit)->time,
          "explosion: a delay of 0 explodes in the step of the hit");
}

void testKinematicBrick() {
    // The shark: kinematic, one hit, 250 ms, 91 points.
    Game g(levelOf({board(), brick({-5, 1, 12}, "Shark.jpg", kKinematic)}), options(TargetRule::Bricks));
    check(g.session.targetsLeft(TargetRule::BrickType) == 1 && g.session.targetsLeft(TargetRule::Bricks) == 1,
          "kinematic: a kinematic brick is a target under both rules");
    g.session.addBall({-5, 0.6f, 0}, {0, 0, 1});
    check(g.runUntil([&] { return g.count(EventType::BrickDestroyed) == 1; }, 5), "kinematic: the ball destroys it");
    check(g.session.score() == 91 && g.session.scene().body(1) == 0, "kinematic: for its 91 points, and it is gone");
    check(g.count(EventType::LevelComplete) == 1, "kinematic: and it was the last target");
}

void testPathShark() {
    // A shark that swims along z = 12 towards +x at five units a second (a track speed of 0.005 a millisecond), the way the
    // levels' sharks and birds go round their paths; a ball that meets it destroys it, and a destroyed shark stays gone.
    Actor shark = brick({-10, 1, 12}, "Shark.jpg", kKinematic);
    shark.object->id = 7;
    Level level = levelOf({board(), shark});
    level.paths.push_back({"track/shark", false, false,
                           {PathPoint{{-10, 1, 12}, {}, {1, 0, 0}, {0, 1, 0}}, PathPoint{{20, 1, 12}, {}, {1, 0, 0}, {0, 1, 0}}}});
    level.pathBindings.push_back({"track/shark", {PathTrack{0.005f, {PathKey{{-10, 1, 12}, {1, 0, 0}, {0, 1, 0}, 7}}}}});
    Game g(level, options(TargetRule::BrickType));
    check(g.session.scene().pathMovers().size() == 1 && g.session.targetsLeft(TargetRule::Bricks) == 1,
          "path shark: a path actor is a kinematic brick, and so a target of the default rule");
    g.run(1);
    check(near(g.where(1), {-5, 1, 12}, 0.05f), "path shark: in a second of play it has swum five units");

    // The ball reaches z = 10.4, the shark's face, 0.69 s later, when the shark is at x = -1.5.
    g.session.addBall({-1.5f, 0.6f, 0}, {0, 0, 1});
    check(g.runUntil([&] { return g.count(EventType::BrickDestroyed) == 1; }, 3), "path shark: the ball destroys it as it passes");
    const GameEvent* hit = g.find(EventType::BrickHit);
    check(hit != nullptr && hit->actor == 1 && hit->position.z < 12 && hit->position.x > -3 && hit->position.x < 0,
          "path shark: on its side, where it was swimming");
    check(g.session.score() == 91 && g.session.actor(1).removed && g.session.scene().body(1) == 0,
          "path shark: for its 91 points, and it is gone");
    g.run(1);
    check(g.session.scene().body(1) == 0 && g.session.actor(1).removed && g.count(EventType::BrickDestroyed) == 1,
          "path shark: and nothing drives it any more");
}

void testRollingIsNoHit() {
    // A strip of floor tiles level with the floor (kinematic, top at y = 0, scored like the real ones): the ball meets the
    // edge of each at a slant of up to a quarter of its speed, and no tile is hurt.
    std::vector<Actor> flat = {board()};
    for (int i = 0; i < 8; ++i) {
        flat.push_back(actor(ObjectType::Brick, {-14.0f + 4 * static_cast<float>(i), -0.25f, 0}, kKinematic, 20,
                             {box({2, 0.25f, 6})}, "Tile.jpg"));
    }
    Game strip(levelOf(flat));
    strip.session.addBall({-18.5f, 0.6f, -1}, {std::cos(radians(10.0f)), 0, std::sin(radians(10.0f))});
    strip.run(2.3);
    check(strip.count(EventType::BrickHit) == 0 && strip.session.stats().bricksHit == 0, "rolling: over flush tiles, no hit");
    bool intact = true;
    for (ActorIndex i = 1; i <= 8; ++i) {
        intact = intact && strip.session.actor(i).energy == 1 && !strip.session.actor(i).exploding;
    }
    check(intact, "rolling: and every tile has its energy");

    // A ball that slides along the face of a column of tiles that stand proud, as the real ones do (tops at y = 0.56), is
    // in touch with each in turn at no closing speed.
    std::vector<Actor> column = {board()};
    for (int i = 0; i < 12; ++i) {
        column.push_back(actor(ObjectType::Brick, {5, 0.28f, 2 * static_cast<float>(i)}, kKinematic, 20,
                               {box({1, 0.28f, 1})}, "Tile.jpg"));
    }
    Game slide(levelOf(column));
    slide.session.addBall({3.4f, 0.6f, -3}, {0, 0, 1});
    slide.run(1.4);
    check(slide.session.balls()[1].position.z > 16 && slide.count(EventType::BrickHit) == 0,
          "rolling: sliding along a column of tiles passes eight of them and hurts none");

    // The same tiles met head on are hit.
    Game head(levelOf(column));
    head.session.addBall({1.4f, 0.6f, 2}, {1, 0, 0});
    head.run(0.5);
    check(head.count(EventType::BrickHit) == 1 && head.find(EventType::BrickHit)->actor == 2 &&
              head.find(EventType::BrickHit)->speed > 14,
          "rolling: a tile met head on is hit, once, at the ball's speed");

    // A slanted hit is one: 60 degrees from the face's normal the ball closes at half its speed, which is above the
    // 30% that counts and below 60%.
    Game glancing(levelOf({board(), brick({0, 1, 8}, "Shark.jpg", kStatic)}));
    glancing.session.addBall({-6.6f, 0.6f, -0.66f}, {0.5f, 0, 0.866f});
    glancing.run(1.2);
    const GameEvent* slant = glancing.find(EventType::BrickHit);
    check(slant != nullptr && slant->speed > 6.0f && slant->speed < 9.0f,
          "rolling: a ball that meets a face at 60 degrees from its normal hits it, at about half its speed");
}

void testCompoundBrickHitOnce() {
    // Two boxes side by side make one brick. A ball that arrives on the seam touches both at once: one hit.
    Actor twin = actor(ObjectType::Brick, {0, 1, 10}, kStatic, 100, {box({2, 1, 1}, {-2, 0, 0}), box({2, 1, 1}, {2, 0, 0})},
                       "Tough.jpg");
    Game g(levelOf({board(), twin}));
    g.session.addBall({0, 0.6f, 0}, {0, 0, 1});
    g.runUntil([&] { return g.count(EventType::BrickHit) > 0; }, 5);
    g.run(0.05);
    check(g.count(EventType::BrickHit) == 1 && g.session.actor(1).energy == 2,
          "compound: a ball on the seam of two shapes hurts the brick once");
}

void testLevelEndRules() {
    // dynamic shark (1), kinematic shark (2), dynamic crate (3), dynamic tree (4)
    const auto level = [] {
        return levelOf({board(), brick({-12, 1, 12}, "Shark.jpg"), brick({-4, 1, 12}, "Shark.jpg", kKinematic),
                        brick({4, 1, 12}, "Crate.jpg"),
                        actor(ObjectType::Tree, {12, 1, 12}, kDynamic, 50, {box({1, 1, 1})}, "Tree")});
    };
    const auto settle = [](Game& g) { g.run(0.6); };

    Game all(level(), options(TargetRule::Bricks));
    check(all.session.targetsLeft(TargetRule::Bricks) == 4 && all.session.targetsLeft(TargetRule::BrickType) == 2,
          "end: 4 targets under Bricks (two sharks, a crate, a tree), 2 under H-type (the two sharks)");
    check(all.session.actor(1).target && all.session.actor(2).target && all.session.actor(3).target && all.session.actor(4).target,
          "end: the targets are marked, the kinematic shark's too");
    all.session.hit(1, {});
    all.session.hit(3, {});
    settle(all);
    check(all.count(EventType::LevelComplete) == 0 && all.session.targetsLeft(TargetRule::Bricks) == 2,
          "end Bricks: not done with the tree standing");
    all.session.hit(4, {});
    settle(all);
    check(all.count(EventType::LevelComplete) == 0, "end Bricks: a tree takes two hits");
    all.session.hit(4, {});
    settle(all);
    check(all.count(EventType::LevelComplete) == 0 && !all.session.actor(2).removed &&
              all.session.targetsLeft(TargetRule::Bricks) == 1,
          "end Bricks: nor with the kinematic shark swimming: it is required");
    all.session.hit(2, {});
    settle(all);
    check(all.count(EventType::LevelComplete) == 1 && all.session.phase() == SessionPhase::Complete,
          "end Bricks: done when the last of them is gone, a kinematic one");
    const GameEvent* complete = all.find(EventType::LevelComplete);
    const GameEvent* lastDestroyed = nullptr;
    for (const GameEvent& e : all.log) {
        lastDestroyed = e.type == EventType::BrickDestroyed ? &e : lastDestroyed;
    }
    check(complete != nullptr && lastDestroyed != nullptr && complete->time == lastDestroyed->time && lastDestroyed->actor == 2,
          "end Bricks: at the moment of the last explosion");
    check(all.session.score() == 91 + 91 + 20 + 130, "end Bricks: the score is the four bricks' points, nothing more");

    Game type(level(), options(TargetRule::BrickType));
    type.session.hit(1, {});
    type.session.hit(3, {});
    type.session.hit(4, {});
    type.session.hit(4, {});
    settle(type);
    check(type.count(EventType::LevelComplete) == 0, "end H-type: crates and trees do not count");
    type.session.hit(2, {});
    settle(type);
    check(type.count(EventType::LevelComplete) == 1, "end H-type: done with the last brick of type 0, the kinematic shark");

    Game never(level(), options(TargetRule::Never));
    for (ActorIndex i = 1; i <= 4; ++i) {
        never.session.hit(i, {});
        never.session.hit(i, {});
    }
    never.run(1);
    check(never.count(EventType::BrickDestroyed) == 4 && never.count(EventType::LevelComplete) == 0 &&
              never.session.targetsLeft(TargetRule::Bricks) == 0,
          "end Never: everything can go and the level goes on");

    // A forced end ("Next Level"), and after the end nothing is launched or lost.
    Game forced(level(), options(TargetRule::BrickType));
    forced.ready();
    forced.session.forceLevelComplete();
    forced.step({.launch = true});
    check(forced.count(EventType::LevelComplete) == 1 && forced.session.phase() == SessionPhase::Complete &&
              forced.count(EventType::BallLaunched) == 0,
          "end: Next Level completes the level at once, and the click launches nothing after it");
    forced.session.forceLevelComplete();
    forced.step();
    check(forced.count(EventType::LevelComplete) == 1, "end: once");
}

// The default rule played out: a level ends with the bricks and trees gone, whatever their motion, and every other kind of
// object (metal, portals, a cannon, dynamite, ...) still on the board.
void testRequiredSet() {
    const auto thing = [](ObjectType type, Vec3 at, std::uint32_t flags, const char* material) {
        return actor(type, at, flags, 50, {box({1, 1, 1})}, material);
    };
    Game g(levelOf({board(),
                    thing(ObjectType::Brick, {-16, 1, 12}, kDynamic, "Shark.jpg"),        // 1: required
                    thing(ObjectType::Brick, {-12, 1, 12}, kDynamic, "Crate.jpg"),        // 2: required (type 4)
                    thing(ObjectType::Tree, {-8, 1, 12}, kDynamic, "Tree"),               // 3: required
                    thing(ObjectType::Brick, {-4, 1, 12}, kKinematic, "Shark.jpg"),       // 4: required, though it moves on a path
                    thing(ObjectType::Brick, {0, 1, 12}, kKinematic, "Tile.jpg"),         // 5: a floor tile: required
                    thing(ObjectType::Metall, {4, 1, 12}, kDynamic, "Metall.jpg"),        // 6
                    thing(ObjectType::Cannon, {8, 1, 12}, kDynamic, "Shark.jpg"),         // 7
                    thing(ObjectType::TeleportSrc, {12, 1, 12}, kDynamic, "Shark.jpg"),   // 8
                    thing(ObjectType::TeleportDest, {16, 1, 12}, kDynamic, "Shark.jpg"),  // 9
                    thing(ObjectType::Dynamite, {-16, 1, 16}, kDynamic, "Shark.jpg"),     // 10
                    thing(ObjectType::Firework, {-12, 1, 16}, kDynamic, "Shark.jpg"),     // 11
                    thing(ObjectType::GeneratorGood, {-8, 1, 16}, kDynamic, "Shark.jpg"), // 12
                    thing(ObjectType::Volcano, {-4, 1, 16}, kDynamic, "Shark.jpg"),       // 13
                    thing(ObjectType::Vibro, {0, 1, 16}, kDynamic, "Shark.jpg"),          // 14
                    thing(ObjectType::Brick, {4, 1, 16}, kStatic, "Crate.jpg")}),         // 15: fixed in place: required
           options(TargetRule::Bricks));
    check(g.session.targetsLeft(TargetRule::Bricks) == 6, "required: six targets of the fifteen objects");
    for (ActorIndex i = 1; i <= 15; ++i) {
        check(g.session.actor(i).target == (i <= 5 || i == 15), "required: actor " + std::to_string(i) + " is a target only if it is a brick or a tree");
    }
    for (const ActorIndex i : {1, 2, 4, 5, 15}) {
        g.session.hit(i, {});
    }
    g.session.hit(3, {});
    g.run(0.6);
    check(g.count(EventType::LevelComplete) == 0 && g.session.targetsLeft(TargetRule::Bricks) == 1,
          "required: with the tree still standing the level goes on");
    g.session.hit(3, {});
    g.run(0.6);
    check(g.count(EventType::LevelComplete) == 1 && g.session.phase() == SessionPhase::Complete,
          "required: the level is complete when every brick and the tree are gone, the kinematic and the fixed ones too");
    bool standing = true;
    for (ActorIndex i = 6; i <= 14; ++i) {
        const bool teleport = i == 8 || i == 9;  // (they cannot be hurt at all)
        standing = standing && !g.session.actor(i).removed && g.session.actor(i).damageable == !teleport;
    }
    check(standing, "required: and the other nine are standing, and can still be destroyed (but for the two portals)");
    const std::int64_t before = g.session.score();
    g.session.hit(6, {});  // the metal: 50 points for the destruction, which nothing waited for
    g.run(0.5);
    check(g.session.actor(6).removed && g.session.score() == before + 50 && g.count(EventType::LevelComplete) == 1,
          "required: destroying one of them still scores");
}

// A person or an animal is paid as it is destroyed and shrinks away for a third of a second, a target until it is gone; the level
// ends when the last of them has shrunk, not at the score. The others are gone with the score.
void testShrinkingAway() {
    Game g(levelOf({board(), brick({0, 1, 10}, "Pirate.jpg", kStatic)}), options(TargetRule::Bricks));
    const ActorIndex pirate = 1;
    g.session.hit(pirate, {});
    g.session.hit(pirate, {});
    g.session.hit(pirate, {});
    check(g.runUntil([&] { return g.count(EventType::BrickDestroyed) == 1; }, 1), "shrink: the pirate is destroyed");
    const GameEvent* destroyed = g.find(EventType::BrickDestroyed);
    check(destroyed->points == 290 && g.session.score() == 290 && destroyed->material == materials().find("Pirate.jpg"),
          "shrink: it is paid for as it is destroyed");
    ActorStatus status = g.session.actor(pirate);
    check(!status.removed && status.target && status.shrink < 0.01f && !status.exploding && g.session.scene().body(pirate) != 0 &&
              g.session.targetsLeft(TargetRule::Bricks) == 1,
          "shrink: and is still there, whole, and a target");
    check(!g.session.hit(pirate, {}), "shrink: a hit does nothing to it now");

    float shrunk = 0;
    bool grows = true;
    double removedAt = -1;
    for (int i = 0; i < 60 && !g.session.actor(pirate).removed; ++i) {
        g.step();
        status = g.session.actor(pirate);
        if (status.removed) {
            removedAt = g.session.time();
            break;
        }
        grows = grows && status.shrink >= shrunk && status.shrink <= 1;
        shrunk = status.shrink;
        check(g.count(EventType::LevelComplete) == 0 && status.target, "shrink: the level is not complete while it shrinks");
    }
    check(grows && shrunk > 0.9f && status.removed && g.session.scene().body(pirate) == 0, "shrink: it shrinks to nothing, then is removed");
    check(near(static_cast<float>(removedAt - destroyed->time), 0.35f, 0.02f), "shrink: 0.35 s after its score");
    const GameEvent* complete = g.find(EventType::LevelComplete);
    check(complete != nullptr && complete->time == removedAt && g.session.score() == 290 && g.count(EventType::BrickDestroyed) == 1,
          "shrink: the level is complete as it is gone, and nothing more is scored");

    // A shark has no such thing: gone with its score, and the level over with it.
    Game shark(levelOf({board(), brick({0, 1, 10}, "Shark.jpg", kStatic)}), options(TargetRule::Bricks));
    shark.session.hit(1, {});
    shark.runUntil([&] { return shark.count(EventType::BrickDestroyed) == 1; }, 1);
    check(shark.session.actor(1).removed && shark.session.actor(1).shrink == 0 && shark.count(EventType::LevelComplete) == 1 &&
              shark.find(EventType::LevelComplete)->time == shark.find(EventType::BrickDestroyed)->time,
          "shrink: a shark is gone with its score");

    // A shrinking body that falls off the board is not taken out as a fallen piece (it is paid, and about to go): removed once,
    // counted once. Here the board's edge is set at y = 2.4 so that it is below it for most of the shrink.
    Actor island = actor(ObjectType::Board, {0, 0, 0}, 0xFF, 1000, {box({10, 0.5f, 30}, {0, -0.5f, 0}, 4)}, "BoardMaterial");
    SessionOptions low = options(TargetRule::Bricks);
    low.rules.fallLimitY = 2.4f;
    low.ignoreUnreachable = false;
    Game falling(levelOf({island, brick({14, 1, 0}, "Crate.jpg", kKinematic), brick({14, 2.5f, 0}, "Pirate.jpg", kDynamic, {1, 0.5f, 1}),
                          brick({0, 1, 8}, "Tough.jpg", kStatic)}),
                 low);
    for (int hit = 0; hit < 3; ++hit) {
        falling.session.hit(2, {});
    }
    falling.session.hit(1, {});
    falling.run(1);
    check(falling.count(EventType::ActorFell) == 0 && falling.count(EventType::BrickDestroyed) == 2 &&
              falling.session.actor(2).removed && falling.session.score() == 290 + 20 &&
              falling.session.targetsLeft(TargetRule::Bricks) == 1 && falling.count(EventType::LevelComplete) == 0,
          "shrink: one that falls while it shrinks is removed once, with no ActorFell");
}

// A target the ball cannot get at does not count, so that a level cannot be one that never ends.
void testUnreachableTargets() {
    const auto steel = [](Vec3 at, Vec3 half) { return brick(at, "Steel.jpg", kStatic, half); };
    const auto level = [&] {
        return levelOf({
            board(),
            brick({0, 1, 12}, "Shark.jpg"),                  // 1: in the open
            brick({27, 1, 5}, "Shark.jpg"),                  // 2: beyond a side wall
            brick({0, 1, 27}, "Shark.jpg"),                  // 3: beyond the far wall
            brick({-10, 8, 5}, "Shark.jpg", kStatic),        // 4: pinned in mid-air, out of the ball's height
            steel({10, 2, 10}, {3, 2, 3}),                   // 5: a platform (top at y = 4) of something no ball breaks
            brick({10, 5.5f, 10}, "Shark.jpg"),              // 6: standing on it
            brick({-10, 1, 15}, "Crate.jpg"),                // 7: a crate
            brick({-10, 3.5f, 15}, "Shark.jpg"),             // 8: with a shark on it, which falls if the crate goes
            steel({-17.5f, 1, 9}, {1, 1, 3}),                // 9, 10: a slot 1 wide, open to the front, that the ball
            steel({-15.5f, 1, 9}, {1, 1, 3}),                //     cannot enter ...
            brick({-16.5f, 1, 11.5f}, "Shark.jpg", kDynamic, {0.4f, 0.4f, 0.4f}, 5),  // 11: ... with a shark at the back
        });
    };
    constexpr int kInTheOpen = 1, kBeyondSide = 2, kBeyondFar = 3, kPinned = 4, kOnPlatform = 6, kOnCrate = 8, kInSlot = 11;

    Game g(level(), options(TargetRule::BrickType));
    check(g.session.targetsLeft(TargetRule::BrickType) == 2 && g.session.stats().targetsOutOfPlay == 5,
          "reach: of the seven sharks the two the ball can get at count, five are out of play");
    check(g.session.isTarget(kInTheOpen, TargetRule::BrickType) && g.session.isTarget(kOnCrate, TargetRule::BrickType),
          "reach: one in the open, and one that stands on a crate, which falls if the crate is broken");
    for (const int out : {kBeyondSide, kBeyondFar, kPinned, kOnPlatform, kInSlot}) {
        const ActorStatus status = g.session.actor(static_cast<ActorIndex>(out));
        check(status.outOfPlay && !status.target && !g.session.isTarget(static_cast<ActorIndex>(out), TargetRule::BrickType) &&
                  g.session.isTarget(static_cast<ActorIndex>(out), TargetRule::Bricks) == false,
              "reach: actor " + std::to_string(out) + " is out of play and no target");
    }
    check(g.session.targetsLeft(TargetRule::Bricks) == 3, "reach: the other rule drops them too (the sharks, and the crate that holds one)");

    // The level ends with the two that can be reached; the others stand.
    g.session.hit(kInTheOpen, {});
    g.session.hit(kOnCrate, {});
    g.run(0.8);
    check(g.count(EventType::LevelComplete) == 1 && !g.session.actor(kOnPlatform).removed,
          "reach: the level is done when what can be reached is gone, with the shark on the platform still standing");

    // Switched off, every target counts.
    SessionOptions all = options(TargetRule::BrickType);
    all.ignoreUnreachable = false;
    Game strict(level(), all);
    check(strict.session.targetsLeft(TargetRule::BrickType) == 7 && strict.session.stats().targetsOutOfPlay == 0 &&
              !strict.session.actor(kBeyondSide).outOfPlay,
          "reach: with ignoreUnreachable off all seven count");

    // A loose target that leaves the board later is let go at the next look: thrown over the side wall.
    Actor flung = brick({18, 6, 10}, "Shark.jpg");
    flung.body->linearVelocity = {5, 7, 0};
    SessionOptions moving = options(TargetRule::BrickType);
    moving.physics.start = StartState::SavedVelocities;
    Game thrown(levelOf({board(), flung}), moving);
    check(thrown.session.targetsLeft(TargetRule::BrickType) == 1, "reach: a shark above the board counts");
    thrown.run(3);
    check(thrown.where(1).x > 22.5f && thrown.session.targetsLeft(TargetRule::BrickType) == 0 &&
              thrown.session.actor(1).outOfPlay && thrown.count(EventType::LevelComplete) == 1,
          "reach: one thrown beyond the wall is out of play at the next look, and the level ends");
}

// Targets nothing can touch that the look at the level did not find: the level stood still, so they are let go.
void testStalledLevel() {
    const auto pinned = [](float x) { return brick({x, 8, 10}, "Shark.jpg", kStatic); };  // above the ball for good
    SessionOptions blind = options(TargetRule::BrickType);
    blind.ignoreUnreachable = false;

    // One target left, out of reach: the ball flies for 90 s without touching a brick and the level ends.
    {
        Game g(levelOf({board(), pinned(0)}), blind);
        const PlayResult played = autoplay(g.session, 200);
        const double stalled = g.session.rules().stallSeconds;
        check(g.session.phase() == SessionPhase::Complete && g.session.stats().targetsLetGo == 1 &&
                  g.session.targetsLeft(TargetRule::BrickType) == 0,
              "stall: a level with a target nothing can touch is complete, and the target is let go");
        check(played.seconds > stalled && played.seconds < stalled + 5, "stall: after ninety seconds of play (and the swoop of the bat)");
        check(!g.session.actor(1).removed && g.session.actor(1).outOfPlay, "stall: it stays on the board");
    }

    // A brick that is hit starts the time again.
    {
        Game g(levelOf({board(), brick({-8, 1, 10}, "Shark.jpg"), pinned(5)}), blind);
        const PlayResult played = autoplay(g.session, 300);
        double lastTouched = 0;
        for (const GameEvent& event : played.events) {
            if (event.type == EventType::BrickHit || event.type == EventType::BrickDestroyed) {
                lastTouched = event.time;
            }
        }
        check(g.session.phase() == SessionPhase::Complete && g.session.stats().targetsLetGo == 1 && lastTouched > 1 &&
                  played.seconds > lastTouched + g.session.rules().stallSeconds - 1,
              "stall: ninety seconds after the last brick that was touched, not after the start");
    }

    // Many targets left: the level is not over however long it takes.
    {
        std::vector<Actor> actors = {board()};
        for (int i = 0; i < 20; ++i) {
            actors.push_back(pinned(static_cast<float>(i - 10)));
        }
        Game g(levelOf(std::move(actors)), blind);
        autoplay(g.session, 150);
        check(g.session.phase() != SessionPhase::Complete && g.session.stats().targetsLetGo == 0,
              "stall: too many targets left for it to be a stray one: the level goes on");
    }

    // Nothing to let go under Never.
    {
        SessionOptions never = options(TargetRule::Never);
        Game g(levelOf({board(), pinned(0)}), never);
        autoplay(g.session, 150);
        check(g.session.phase() != SessionPhase::Complete, "stall: a level that never ends does not end");
    }

    // A body that floats (no gravity) above the ball is out of play at once, with the look on.
    {
        Actor bird = brick({0, 5, 10}, "Shark.jpg");
        bird.body->flags = kDynamic | Body::kDisableGravity;
        Game g(levelOf({board(), bird}), options(TargetRule::BrickType));
        check(g.session.targetsLeft(TargetRule::BrickType) == 0 && g.session.actor(1).outOfPlay,
              "reach: a bird that hangs in the air above the ball is no target");
    }
}

// A loose target that the stall rule let go of is out of play for good: the look at the loose ones, which finds it beyond the
// board later, must not take it out of the counts a second time.
void testLetGoTargetIsCountedOnce() {
    // A shark high above the board, thrown out over the side wall at 3.5 units/s: in play for two seconds, beyond the wall at
    // three and a half. The level stands still from the launch, and after half a second it is let go.
    Actor flung = brick({14, 12, 10}, "Shark.jpg");
    flung.body->linearVelocity = {3.5f, 14, 0};
    SessionOptions thrown = options(TargetRule::Bricks);
    thrown.physics.start = StartState::SavedVelocities;
    thrown.rules.stallSeconds = 0.5;
    Game g(levelOf({board(), flung}), thrown);
    check(g.session.targetsLeft(TargetRule::Bricks) == 1 && g.session.stats().targetsOutOfPlay == 0,
          "let go: a shark high over the board is a target in play");
    g.ready();
    g.step({.launch = true});
    check(g.runUntil([&] { return g.session.stats().targetsLetGo == 1; }, 2) && g.session.phase() == SessionPhase::Complete &&
              g.session.targetsLeft(TargetRule::Bricks) == 0,
          "let go: after half a second of play the stalled level lets it go, and is complete");
    check(g.where(1).x < 22.0f, "let go: while it is still over the board");
    g.run(4);
    check(g.where(1).x > 22.5f, "let go: and later it is beyond the wall");
    check(g.session.targetsLeft(TargetRule::Bricks) == 0 && g.session.targetsLeft(TargetRule::BrickType) == 0 &&
              g.session.stats().targetsOutOfPlay == 0 && g.session.stats().targetsLetGo == 1,
          "let go: and was counted out once, not again when the look found it out of play");
}

// The ground of the ball's plane: the board's inside, around what stands in it.
void testBallReach() {
    const Level level = levelOf({board(), brick({10, 2, 10}, "Steel.jpg", kStatic, {3, 2, 3}),
                                 brick({-17.5f, 1, 9}, "Steel.jpg", kStatic, {1, 1, 3}),
                                 brick({-15.5f, 1, 9}, "Steel.jpg", kStatic, {1, 1, 3}),
                                 brick({0, 8, 10}, "Steel.jpg", kStatic)});  // out of the ball's height: no obstacle
    const PhysicsPlan plan = planPhysics(level, materials());
    const BallReach reach(plan, [&](ActorIndex actor) { return plan.actors[actor].motion == MotionType::Static; },
                          {0, 0.6f, -17.3f}, {});
    check(reach.contains(0, -17.3f) && reach.contains(0, 10) && reach.contains(-19.5f, 21.5f), "reach: the board's floor is ground, to a ball's radius from the walls");
    check(!reach.contains(-20.0f, 10) && !reach.contains(20.0f, 10) && !reach.contains(0, 22.5f),
          "reach: not within a ball's radius of a wall (the inner faces are at 20.4 and 22.6)");
    check(!reach.contains(25, 5) && !reach.contains(0, 28) && !reach.contains(-30, 0), "reach: nor beyond the walls");
    check(!reach.contains(10, 10) && !reach.contains(12.5f, 12.5f) && reach.contains(14.0f, 10),
          "reach: not on the platform, nor within a radius of it, and open ground beyond that");
    check(reach.contains(0, 8) && reach.contains(0, 10), "reach: what is over the ball's height blocks nothing");
    check(!reach.contains(-16.5f, 11.5f) && !reach.contains(-16.5f, 6) && reach.contains(-16.5f, 3),
          "reach: a slot 1 wide between two blocks is not ground, a ball does not fit");
    check(reach.touches(-3, 5, 3, 6) && !reach.touches(24, 5, 30, 8) && reach.touches(19, 5, 30, 8) &&
              !reach.touches(10.5f, 10.5f, 11, 11),
          "reach: a rectangle touches it where any ground is in it");
    check(reach.groundCells() > 0 && reach.groundCells() < reach.cells(), "reach: and the ground is part of the plane");

    // Where the ball begins is blocked: the nearest open ground is the start.
    const Level walled = levelOf({board(), brick({0, 1, -17.3f}, "Steel.jpg", kStatic, {2, 1, 2})});
    const PhysicsPlan walledPlan = planPhysics(walled, materials());
    const BallReach nearest(walledPlan, [&](ActorIndex actor) { return walledPlan.actors[actor].motion == MotionType::Static; },
                            {0, 0.6f, -17.3f}, {});
    check(nearest.contains(0, -13) && !nearest.contains(0, -17.3f), "reach: a start inside a block is moved to the nearest open ground");
}

void testHitOnOthers() {
    Game g(levelOf({board(), brick({0, 1, 10}, "Steel.jpg"), brick({4, 1, 10}, "Tough.jpg")}));
    check(!g.session.hit(1, {}) && !g.session.hit(0, {}) && !g.session.hit(99, {}), "hit: steel, the board and nothing cannot be hit");
    check(g.session.hit(2, {1, 0.6f, 9}) && g.session.actor(2).energy == 2, "hit: a brick can");
    g.step();
    const GameEvent* event = g.find(EventType::BrickHit);
    check(event != nullptr && event->ball == -1 && near(event->position, {1, 0.6f, 9}) && event->energy == 2,
          "hit: the event has no ball");
    g.session.hit(2, {});
    g.session.hit(2, {});
    check(!g.session.hit(2, {}) && g.session.actor(2).exploding, "hit: and not once it is out of energy");
}

void testRemovedBrickLetsGoOfWhatItHeld() {
    // A kinematic pillar with a crate on top, asleep. Left alone the crate stays; with the pillar gone it falls.
    const auto level = [] {
        return levelOf({board(), brick({0, 1, 6}, "Shark.jpg", kKinematic, {1, 1, 1}),
                        actor(ObjectType::Brick, {0, 2.5f, 6}, kDynamic, 10, {box({0.5f, 0.5f, 0.5f})}, "Steel.jpg")});
    };
    const auto lowest = [](Game& g, double seconds) {
        float low = 1e9f;
        for (int i = 0, steps = static_cast<int>(seconds * 60); i < steps; ++i) {
            g.step();
            low = std::min(low, g.where(2).y);
        }
        return low;
    };
    Game stays(level());
    stays.run(1.5);
    check(near(stays.where(2).y, 2.5f, 0.02f) && !stays.session.scene().world().isAwake(stays.session.scene().body(2)),
          "pillar: the crate rests on it, asleep");

    // The next step looks for sleepers with nothing under them, wherever the pillar went in the cycle of looks.
    bool wokenAtOnce = true;
    for (int phase = 0; phase < 6; ++phase) {
        Game w(level());
        w.run(0.5 + phase / 60.0);
        w.session.hit(1, {});
        w.runUntil([&] { return w.count(EventType::BrickDestroyed) == 1; }, 1);
        w.step();
        wokenAtOnce = wokenAtOnce && w.session.scene().world().isAwake(w.session.scene().body(2));
    }
    check(wokenAtOnce, "pillar: the crate is woken in the step after the pillar goes");

    Game g(level());
    g.run(0.5);
    g.session.hit(1, {});
    check(g.runUntil([&] { return g.count(EventType::BrickDestroyed) == 1; }, 1), "pillar: the pillar explodes");
    check(lowest(g, 2) < 0.55f, "pillar: and the crate falls to the floor");

    // What was joined to it wakes, too: a crate hung from the pillar by a fixed joint, asleep, hangs in the air.
    Joint hang;
    hang.type = NxJointType::Fixed;
    hang.actorA = 11;
    hang.actorB = 12;
    hang.localAxis[0] = hang.localAxis[1] = {0, 1, 0};
    hang.localNormal[0] = hang.localNormal[1] = {1, 0, 0};
    hang.localAnchor[0] = {0, 2.5f, 0};
    hang.localAnchor[1] = {0, -0.5f, 0};
    hang.maxForce = hang.maxTorque = 1e6f;
    Level hung = levelOf({board(), brick({0, 1, 6}, "Shark.jpg", kKinematic, {1, 1, 1}),
                          actor(ObjectType::Brick, {0, 4, 6}, kDynamic, 10, {box({0.5f, 0.5f, 0.5f})}, "Steel.jpg")},
                         {hang});
    hung.actors[1].object->id = 11;
    hung.actors[2].object->id = 12;
    Game h(hung);
    h.run(1);
    check(near(h.where(2).y, 4, 0.05f), "joint: the crate hangs from the pillar");
    h.session.hit(1, {});
    h.run(0.4);
    check(h.session.actor(1).removed && h.session.scene().world().isAwake(h.session.scene().body(2)),
          "joint: when the pillar goes, what it held is let go");
    check(lowest(h, 3) < 0.55f, "joint: and falls to the floor");

    // Two crates joined to each other, not to the pillar, rest on it. A joint holds both, so the look for hanging bodies
    // leaves them alone: the pillar's going has to wake them.
    Joint pair;
    pair.type = NxJointType::Fixed;
    pair.actorA = 21;
    pair.actorB = 22;
    pair.localAxis[0] = pair.localAxis[1] = {0, 1, 0};
    pair.localNormal[0] = pair.localNormal[1] = {1, 0, 0};
    pair.localAnchor[0] = {0.5f, 0, 0};
    pair.localAnchor[1] = {-0.5f, 0, 0};
    pair.maxForce = pair.maxTorque = 1e6f;
    Level stacked = levelOf({board(), brick({0, 1, 6}, "Shark.jpg", kKinematic, {3, 1, 1}),
                             actor(ObjectType::Brick, {-0.5f, 2.5f, 6}, kDynamic, 10, {box({0.5f, 0.5f, 0.5f})}, "Steel.jpg"),
                             actor(ObjectType::Brick, {0.5f, 2.5f, 6}, kDynamic, 10, {box({0.5f, 0.5f, 0.5f})}, "Steel.jpg")},
                            {pair});
    stacked.actors[2].object->id = 21;
    stacked.actors[3].object->id = 22;
    Game rests(stacked);
    rests.run(2);
    check(near(rests.where(2).y, 2.5f, 0.05f) && near(rests.where(3).y, 2.5f, 0.05f) && rests.session.scene().held(2) &&
              !rests.session.scene().world().isAwake(rests.session.scene().body(2)),
          "pair: two crates joined to each other rest on the pillar, asleep");
    rests.session.hit(1, {});
    rests.run(6);
    check(rests.session.actor(1).removed && rests.where(2).y < 0.55f && rests.where(3).y < 0.55f,
          "pair: when the pillar goes, both fall to the floor");
}

void testFallenActors() {
    // A floor ten wide, and beyond its edge a pillar with a crate on it. When the pillar goes the crate falls for ever,
    // and is taken out.
    Actor island = actor(ObjectType::Board, {0, 0, 0}, 0xFF, 1000, {box({10, 0.5f, 30}, {0, -0.5f, 0}, 4)}, "BoardMaterial");
    const auto level = [&](const char* crateMaterial) {
        return levelOf({island, brick({14, 1, 0}, "Crate.jpg", kKinematic), brick({14, 2.5f, 0}, crateMaterial, kDynamic, {1, 0.5f, 1}),
                        brick({0, 1, 8}, "Tough.jpg", kStatic)});
    };
    Game g(level("Shark.jpg"), options(TargetRule::BrickType));
    check(g.session.targetsLeft(TargetRule::BrickType) == 2, "fallen: two targets (the crate and the tough brick)");
    g.session.hit(1, {});
    check(g.runUntil([&] { return g.count(EventType::ActorFell) == 1; }, 4), "fallen: the crate that left the board is taken out");
    const GameEvent* fell = g.find(EventType::ActorFell);
    check(fell->actor == 2 && fell->position.y < -4.9f && fell->material == materials().find("Shark.jpg"),
          "fallen: the event says which, where and of what");
    check(fell->points == 0 && g.session.score() == 20 && g.count(EventType::BrickDestroyed) == 1,
          "fallen: it scores nothing, and there is no explosion for it (the 20 are the pillar's)");
    check(g.session.scene().body(2) == 0 && g.session.actor(2).removed && g.session.stats().actorsFallen == 1,
          "fallen: and is out of the world");
    check(g.session.targetsLeft(TargetRule::BrickType) == 1 && g.count(EventType::LevelComplete) == 0,
          "fallen: it is no longer a target to destroy");
    g.session.hit(3, {});
    g.session.hit(3, {});
    g.session.hit(3, {});
    g.run(0.5);
    check(g.count(EventType::LevelComplete) == 1, "fallen: and the level ends when the others are destroyed");

    // With fallScores on, the other reading, it scores its brickScore.
    SessionOptions scored = options();
    scored.rules.fallScores = true;
    Game paid(level("Shark.jpg"), scored);
    paid.session.hit(1, {});
    paid.runUntil([&] { return paid.count(EventType::ActorFell) == 1; }, 4);
    check(paid.find(EventType::ActorFell)->points == 91 && paid.session.score() == 20 + 91 && paid.count(EventType::BrickDestroyed) == 1,
          "fallen: with fallScores on, it scores its 91, still without an explosion");

    // A brick that has taken its last hit does not explode once it has fallen: here the board's edge is set at y = 2.4, so
    // that it leaves long before its half second is up. It is taken out as a fallen piece, once, with no points.
    SessionOptions low = options();
    low.rules.fallLimitY = 2.4f;
    Game dying(level("Slow.jpg"), low);
    dying.session.hit(2, {});
    dying.session.hit(1, {});
    dying.run(1);
    check(dying.count(EventType::ActorFell) == 1 && dying.count(EventType::BrickDestroyed) == 1 && dying.session.score() == 20 &&
              !dying.session.actor(2).exploding,
          "fallen: out of the world first, without points or an explosion");
    low.rules.fallScores = true;
    Game dyingPaid(level("Slow.jpg"), low);
    dyingPaid.session.hit(2, {});
    dyingPaid.session.hit(1, {});
    dyingPaid.run(1);
    check(dyingPaid.count(EventType::BrickDestroyed) == 1 && dyingPaid.session.score() == 20 + 77, "fallen: and with fallScores on, scored once");
}

void testPinnedBall() {
    // A ball shut into a pocket 1.5 wide has nowhere to go. It bounces between the walls, never farther than a unit from
    // where it was: every jamTime it is put back in the world, in the same place, and keeps its speed.
    const auto wall = [](Vec3 at, Vec3 half) {
        return actor(ObjectType::Brick, at, kStatic, 100, {box(half)}, "Steel.jpg");
    };
    const Level pocket = levelOf({board(), wall({-1.25f, 1, 10}, {0.5f, 1, 1.75f}), wall({1.25f, 1, 10}, {0.5f, 1, 1.75f}),
                                  wall({0, 1, 8.75f}, {0.75f, 1, 0.5f}), wall({0, 1, 11.25f}, {0.75f, 1, 0.5f})});
    Game g(pocket);
    g.run(3);
    check(g.count(EventType::BallFreed) == 0, "pinned: a ball held on the bat is not a pinned one");
    const double began = g.session.time();
    const int id = g.session.addBall({0, 0.6f, 10}, {0.5f, 0, 1});
    g.run(3.5);
    const GameEvent* freed = g.find(EventType::BallFreed);
    check(freed != nullptr && freed->ball == id && near(static_cast<float>(freed->time - began), 1.0f, 0.05f) &&
              near(freed->position, {0, 0.6f, 10}, 0.8f),
          "pinned: a ball that stays within a unit for a second is freed, at the place it was");
    check(g.count(EventType::BallFreed) == 3 && g.session.stats().ballsFreed == 3 && g.session.balls().size() == 2 &&
              g.count(EventType::BallLost) == 0,
          "pinned: and again every second while it is shut in, and it is not lost");
    const BallView* shut = g.ball(id);
    check(shut != nullptr && near(std::hypot(shut->velocity.x, shut->velocity.z), 15, 0.05f) &&
              near(shut->position.x, 0, 0.8f) && near(shut->position.z, 10, 0.8f),
          "pinned: with its speed, in the pocket");

    // Where it is put back: away from the wall it was against and towards the middle of the board. Made to fire in free
    // flight by a distance no ball covers in the time, a ball from the left half and the near half is sent on to the right
    // and the far side, and from the far half to the near side.
    SessionOptions eager = options();
    eager.rules.jamDistance = 1000;
    eager.rules.jamTime = 0.5;
    Game e(levelOf({board()}), eager);
    const int fromNear = e.session.addBall({-10, 0.6f, -12}, {0, 0, 1});
    const int fromFar = e.session.addBall({10, 0.6f, 5}, {0, 0, 1});
    e.runUntil([&] { return e.count(EventType::BallFreed) == 2; }, 1);
    e.step();
    const BallView* a = e.ball(fromNear);
    const BallView* b = e.ball(fromFar);
    check(a != nullptr && b != nullptr && e.count(EventType::BallFreed) == 2, "pinned: both are freed");
    if (a != nullptr && b != nullptr) {
        const Vec3 nearWay = normalize({0.3f, 0, 1.0f}), farWay = normalize({-0.3f, 0, -1.0f});
        check(near(a->velocity, nearWay * 15, 0.1f), "pinned: from the left and near half, to the right and on");
        check(near(b->velocity, farWay * 15, 0.1f), "pinned: from the right and far half, to the left and back");
    }
}

void testRefusedBall() {
    // A world with room for the board and the bat only cannot make a ball: the click and addBall() do nothing.
    SessionOptions tight = options();
    tight.physicsSettings.maxBodies = 2;
    Game g(levelOf({board()}), tight);
    check(g.ready(), "refused: the bat comes in with its ball");
    g.step({.launch = true});
    check(g.count(EventType::BallLaunched) == 0 && g.session.phase() == SessionPhase::Ready &&
              g.session.balls().size() == 1 && g.session.balls()[0].held && g.session.stats().ballsLaunched == 0,
          "refused: a click that cannot launch leaves the ball waiting on the bat");
    check(g.session.addBall({0, 0.6f, 0}, {0, 0, 1}) == -1 && g.session.balls().size() == 1 &&
              g.count(EventType::BallLaunched) == 0,
          "refused: addBall() says -1, and there is no ball");
}

void testLevelCompleteEndsTheLifeSequence() {
    // A level that is won while the bat is flying off costs no heart and brings no bat back.
    Game g(levelOf({board(false)}));
    loseTheBall(g);
    g.runUntil([&] { return g.session.bat().phase == BatPhase::Leaving; }, 2);
    g.session.forceLevelComplete();
    g.run(5);
    check(g.count(EventType::LevelComplete) == 1 && g.count(EventType::LifeLost) == 0 && g.count(EventType::BatEntered) == 1 &&
              g.session.hearts() == 2 && g.session.phase() == SessionPhase::Complete,
          "complete: winning while the bat leaves spends no heart and brings no new bat");
    check(g.session.bat().phase == BatPhase::Absent, "complete: the bat that was leaving is gone");

    Game entering(levelOf({board()}));
    entering.session.forceLevelComplete();
    check(entering.session.bat().phase == BatPhase::Present, "complete: a bat that was swooping in is there");
}

void testBatTargetIsKept() {
    // No target in a frame is no change: a cursor that left the board (batTargetX() has none) does not send the bat home.
    Game g(levelOf({board()}));
    g.run(1.5, {.batTargetX = 10});
    check(near(g.session.bat().position.x, 10, 0.05f), "target: the bat is where the cursor was");
    g.run(1.5, {});
    check(near(g.session.bat().position.x, 10, 0.05f), "target: and stays there when there is no target");
}

void testCursor() {
    Camera camera;
    constexpr float width = 800, height = 600;
    // The middle of the window looks at the camera's target, on the ground.
    const std::optional<float> middle = batTargetX(width / 2, height / 2, width, height, camera);
    check(middle && near(*middle, 0, 0.01f), "cursor: the middle of the window is x = 0");

    // Points of the ball's plane, projected to the window and back, at two aspect ratios.
    for (const float aspect : {4.0f / 3, 16.0f / 9}) {
        const float w = 600 * aspect, h = 600;
        const Mat4 view = camera.view(), projection = camera.projection(aspect);
        bool allBack = true;
        for (const Vec3 p : {Vec3{-18, 0.6f, -19}, Vec3{0, 0.6f, 0}, Vec3{17.5f, 0.6f, 21}, Vec3{-7, 0.6f, 10}, Vec3{12, 0.6f, -12}}) {
            const Vec4 clip = transform(Vec4{p.x, p.y, p.z, 1}, view * projection);
            const float cursorX = (clip.x / clip.w * 0.5f + 0.5f) * w, cursorY = (0.5f - clip.y / clip.w * 0.5f) * h;
            const std::optional<Vec3> back = cursorOnPlane(cursorX, cursorY, w, h, view, projection, 0.6f);
            allBack = allBack && back && near(*back, p, 0.01f);
            const std::optional<float> x = batTargetX(cursorX, cursorY, w, h, camera);
            allBack = allBack && x && near(*x, p.x, 0.01f);
        }
        check(allBack, "cursor: a point of the plane seen at a pixel is found again from it");
    }
    // Left of the window is left of the board.
    const std::optional<float> left = batTargetX(0, 500, width, height, camera), right = batTargetX(width, 500, width, height, camera);
    check(left && right && *left < -15 && *right > 15, "cursor: the window's edges are the board's sides");

    // A camera looking level at the horizon: a cursor above it points at the sky, and none at the ground.
    const Mat4 view = lookAtLH({0, 5, 0}, {0, 5, 10}, {0, 1, 0});
    const Mat4 projection = perspectiveFovLH(radians(40.0f), 4.0f / 3, 1, 500);
    check(!cursorOnPlane(400, 100, 800, 600, view, projection, 0.6f) && cursorOnPlane(400, 500, 800, 600, view, projection, 0.6f),
          "cursor: a cursor above the horizon finds no ground");
    check(!cursorOnPlane(1, 1, 0, 600, view, projection, 0.6f), "cursor: an empty window finds none");
}

void testCarriedState() {
    SessionOptions carried = options();
    carried.hearts = 5;
    carried.score = 12345;
    Game g(levelOf({board()}), carried);
    check(g.session.hearts() == 5 && g.session.score() == 12345, "carried: hearts and score come from the level before");
    Game fresh(levelOf({board()}));
    check(fresh.session.hearts() == 2 && fresh.session.score() == 0, "carried: a new game starts with 2 hearts");
    fresh.step();
    const GameEvent* entered = fresh.find(EventType::BatEntered);
    check(entered != nullptr && entered->time == 0, "carried: and the first bat enters at once");
}

void testEventQueueIsBounded() {
    // A game that never takes its events must not eat the memory: past 16,384 the new ones are dropped.
    Game g(levelOf({board(), brick({0, 1, 8}, "Huge.jpg", kStatic)}));
    for (int i = 0; i < 17000; ++i) {
        g.session.hit(1, {});
    }
    const std::vector<GameEvent> events = g.session.takeEvents();
    check(events.size() == 16384 && g.session.stats().bricksHit == 17000,
          "events: " + std::to_string(events.size()) + " of 17000 are kept");
    g.session.hit(1, {});
    check(g.session.takeEvents().size() == 1, "events: and the queue is open again once taken");
}

void testDeterminism() {
    const auto level = [] {
        return levelOf({board(), brick({-8, 1, 10}, "Shark.jpg"), brick({0, 1, 14}, "Tough.jpg"), brick({9, 1, 9}, "Crate.jpg"),
                        actor(ObjectType::Brick, {4, 1, 4}, kStatic, 100, {box({1, 1, 3})}, "Steel.jpg")});
    };
    std::vector<std::string> first, second;
    for (std::vector<std::string>* log : {&first, &second}) {
        GameSession session(level(), materials(), options(TargetRule::BrickType));
        for (const GameEvent& event : autoplay(session, 40).events) {
            log->push_back(describe(event));
        }
    }
    check(!first.empty() && first == second, "determinism: the same level played twice gives the same events");
}

void testAutoplay() {
    const Level level = levelOf({board(), brick({-8, 1, 10}, "Shark.jpg"), brick({2, 1, 14}, "Shark.jpg"), brick({9, 1, 9}, "Shark.jpg")});
    GameSession session(level, materials(), options(TargetRule::BrickType));
    int frames = 0;
    const PlayResult result = autoplay(session, 120, [&](const GameSession&) { ++frames; });
    check(!result.events.empty() && result.events.back().type == EventType::LevelComplete &&
              session.phase() == SessionPhase::Complete,
          "autoplay: the autopilot clears three bricks and the level is complete");
    check(result.seconds < 120 && frames == static_cast<int>(std::lround(result.seconds * 60)),
          "autoplay: it stops there, and reports the frames it played");
    int destroyed = 0;
    for (const GameEvent& event : result.events) {
        destroyed += event.type == EventType::BrickDestroyed;
    }
    check(destroyed == 3 && session.score() == 3 * 91, "autoplay: three bricks of 91 points");
    check(describe(result.events.back()).find("LevelComplete") != std::string::npos, "autoplay: events can be printed");
}

}  // namespace

int main() {
    testPredicates();
    testBallSpeedStaysAndStaysOnThePlane();
    testSlopesDoNotLiftTheBall();
    testLaunch();
    testWallsTurnTheBall();
    testStraightPathsAreTurnedOut();
    testHeldBallFollowsTheBat();
    testBatReflection();
    testLosingTheBall();
    testGameOver();
    testMultiball();
    testBallEscapes();
    testBrickDamage();
    testExplosionDelayAndScore();
    testKinematicBrick();
    testPathShark();
    testRollingIsNoHit();
    testCompoundBrickHitOnce();
    testLevelEndRules();
    testRequiredSet();
    testShrinkingAway();
    testUnreachableTargets();
    testStalledLevel();
    testLetGoTargetIsCountedOnce();
    testBallReach();
    testHitOnOthers();
    testRemovedBrickLetsGoOfWhatItHeld();
    testFallenActors();
    testPinnedBall();
    testRefusedBall();
    testLevelCompleteEndsTheLifeSequence();
    testBatTargetIsKept();
    testCursor();
    testCarriedState();
    testEventQueueIsBounded();
    testDeterminism();
    testAutoplay();
    if (failures == 0) {
        std::puts("all gameplay tests passed");
    }
    return failures == 0 ? 0 : 1;
}
