// Tests of the weapons and the strikes (Weapons, docs/GAMEPLAY.md section 18) on made-up levels: the click that fires, the shot count, what each
// weapon throws and where from, what the projectiles do to bricks of one to four energy and to each special object, the laser's line, the
// two strike prizes and the top of the screen they come from, the prizes a kill may leave, and that a game repeats. Run in a GameSession
// with the physics on the calling thread; needs neither a GPU nor game data. The same weapons in the real levels: mb_weapon_smoke.

#include "mb/game/Autopilot.h"
#include "mb/game/GameCamera.h"
#include "mb/game/Gameplay.h"

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <set>
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

bool nearTo(Vec3 a, Vec3 b, float tolerance = 0.02f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

std::string num(double value) {
    return std::to_string(value);
}

// ---- Materials and levels ----------------------------------------------------------------------------------------

// One.jpg to Four.jpg are bricks of one to four energy. Steel.jpg is of material type 2: nothing damages it. The rest are the numbers of
// the real materials (Data/Materials/materials.json) of the special objects, and of the projectiles for the sounds the events carry.
const MaterialLibrary& materials() {
    static const MaterialLibrary library = MaterialLibrary::parse(R"({"version": 1, "materials": [
      {"name": "BallMaterial", "density": 1, "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 0.999},
       "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "BoardMaterial", "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 1.0},
       "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "One.jpg", "density": 5, "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 3, "exploseDelay": 100}},
      {"name": "Two.jpg", "density": 5, "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 2, "score": 20, "exploseType": 3, "exploseDelay": 100}},
      {"name": "Three.jpg", "density": 5, "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 3, "score": 30, "exploseType": 3, "exploseDelay": 100}},
      {"name": "Four.jpg", "density": 5, "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 4, "score": 40, "exploseType": 3, "exploseDelay": 100}},
      {"name": "Steel.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 2, "energy": 1, "score": 10}},
      {"name": "Dinamit.jpg", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 120, "exploseType": 8, "exploseDelay": 100, "explosePower": 1.0},
       "sounds": {"dead": "dynamite_dead"}},
      {"name": "int_cannon", "density": 50, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 2, "score": 100, "exploseType": 5, "exploseDelay": 100}},
      {"name": "Fireworks", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 120, "exploseType": 5, "exploseDelay": 100}},
      {"name": "spring", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 5, "exploseDelay": 0}},
      {"name": "volcano", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 5, "exploseDelay": 0}},
      {"name": "gen-good", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 77, "exploseType": 5, "exploseDelay": 0}},
      {"name": "Metall.jpg", "density": 20, "physics": {"restitution": 0.1},
       "brick": {"type": 0, "energy": 1, "score": 50, "exploseType": 6, "exploseDelay": 100}},
      {"name": "portal-in", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 5, "exploseDelay": 100}},
      {"name": "portal-out", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 5, "exploseDelay": 100}},
      {"name": "bat_bullet", "group": "Bats", "brick": {"type": 0, "energy": 3, "score": 4}, "sounds": {"period": "bullet_shoot", "dead": "bullet_dead"}},
      {"name": "bat_kernel", "group": "Bats", "brick": {"type": 0, "energy": 3, "score": 4}, "sounds": {"period": "kernel_shoot", "dead": "kernel_dead"}},
      {"name": "bat_rocket", "group": "Bats", "brick": {"type": 0, "energy": 3, "score": 4}, "sounds": {"period": "rocket_shoot", "dead": "rocket_dead"}},
      {"name": "bat_lazer", "group": "Bats", "brick": {"type": 0, "energy": 3, "score": 4}, "sounds": {"period": "lazer_shoot", "dead": "lazer_dead"}},
      {"name": "thunderbolt", "group": "temp", "brick": {"type": 0, "energy": 1, "score": 10}, "sounds": {"period": "thunderbolt", "dead": "thunderbolt_dead"}},
      {"name": "meteor", "group": "temp", "brick": {"type": 0, "energy": 1, "score": 250}, "sounds": {"period": "meteor", "dead": "meteor_dead"}},
      {"name": "gun", "group": "Prizes", "brick": {"type": 0, "energy": 3, "score": 300, "exploseType": 3, "exploseDelay": 500}},
      {"name": "cannon", "group": "Prizes", "brick": {"type": 0, "energy": 3, "score": 320, "exploseType": 3, "exploseDelay": 500}}
    ]})");
    return library;
}

constexpr std::uint32_t kDynamic = 0x100, kKinematic = 0x180, kStatic = 0x17E;  // NxBodyFlag, with the visualization bit
constexpr std::uint32_t kDynamicNoTurn = 0x170;                                  // a cannon, a firework crate: it cannot turn
constexpr std::uint32_t kTriggerFlags = 15;                                      // enter, leave, stay and visualization

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

// A column of a brick material standing on the floor, `at` its foot's middle (x, z): static unless said, 3 tall and 1 across, so that
// every projectile at the height of the bat's barrels meets it.
Actor brick(float x, float z, const std::string& material, std::uint32_t flags = kStatic, Vec3 half = {0.5f, 1.5f, 0.5f}, float mass = 50) {
    return actor(ObjectType::Brick, {x, half.y, z}, flags, mass, {box(half)}, material);
}

Actor dynamite(Vec3 at, float radius = 5.0f) {
    Actor a = actor(ObjectType::Dynamite, at, kDynamic, 85, {box({0.9f, 1.3f, 0.9f}, {0, 1.3f, 0})}, "Dinamit.jpg");
    a.object->target = radius > 4 ? "explosebig" : "explosesmall";
    a.object->param = std::bit_cast<std::uint32_t>(radius);
    return a;
}

Actor cannon(Vec3 at) {
    return actor(ObjectType::Cannon, at, kDynamicNoTurn, 495, {box({1.25f, 0.99f, 1.0f}, {-0.41f, 0.99f, 0})}, "int_cannon");
}

Actor firework(Vec3 at) {
    return actor(ObjectType::Firework, at, kDynamicNoTurn, 90, {box({1.0f, 1.6f, 0.7f}, {0, 1.6f, 0})}, "Fireworks");
}

// An object of a type the ball does not hurt, standing 2 tall.
Actor thing(ObjectType type, Vec3 at, const char* material) {
    return actor(type, at, kDynamic, 100, {box({1, 1, 1}, {0, 1, 0})}, material);
}

Actor portal(ObjectType type, Vec3 at) {
    std::vector<Shape> shapes = {box({0.2f, 1.9f, 0.9f}, {-1.3f, 1.75f, 0}), box({0.2f, 1.9f, 0.9f}, {1.3f, 1.75f, 0})};
    if (type == ObjectType::TeleportSrc) {
        shapes.push_back(box({1.0f, 1.0f, 0.8f}, {0, 1.0f, 0}, 2, kTriggerFlags));
    }
    return actor(type, at, kKinematic, 48, std::move(shapes), type == ObjectType::TeleportSrc ? "portal-in" : "portal-out");
}

// The board as the levels have it: the floor with its top at y 0, side walls with their inner faces at +-20.4 and the far wall's at 22.6.
Actor board(bool farWall = true) {
    std::vector<Shape> shapes = {box({30, 0.5f, 30}, {0, -0.5f, 0}, 4), box({1, 2, 30}, {-21.4f, 2, 0}, 4),
                                 box({1, 2, 30}, {21.4f, 2, 0}, 4)};
    if (farWall) {
        shapes.push_back(box({30, 2, 1}, {0, 2, 23.6f}, 4));
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
    o.ignoreUnreachable = false;
    o.physicsSettings.threads = 0;  // on the caller, so that a run repeats exactly
    o.rules.prizes.drops = false;   // the prizes are given by hand
    return o;
}

// A chance of 1 for every object, whatever it was worth: every kill leaves a prize (the good ones: level 1).
SessionOptions dropsAlways() {
    SessionOptions o = options();
    o.rules.prizes.drops = true;
    for (PrizeRules::Tier& tier : o.rules.prizes.tiers) {
        tier.chance = 1.0f;
    }
    return o;
}

// A session played with the bat at x: the ball is launched at the first click, away to the left of the bat.
struct Game {
    GameSession session;
    std::vector<GameEvent> log;
    float batX;

    explicit Game(const Level& level, const SessionOptions& o = options(), float x = 8) : session(level, materials(), o), batX(x) {
        step();
    }

    void step(GameInput input = {}) {
        input.batTargetX = batX;
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
    // The bat has landed and the ball waits on it.
    bool ready() {
        return runUntil([&] { return session.phase() == SessionPhase::Ready; }, 3);
    }
    void click() { step({.launch = true}); }
    // The ball is launched: balls in flight, and a click now can fire.
    bool play() {
        if (!ready()) {
            return false;
        }
        click();
        return session.phase() == SessionPhase::Playing;
    }
    void give(PrizeKind kind) {
        session.givePrize(kind);
        flush();
    }
    void flush() {
        for (const GameEvent& event : session.takeEvents()) {
            log.push_back(event);
        }
    }
    // Some time after the last shot, so that the next click is not too soon.
    void wait(double seconds = 0.25) { run(seconds); }

    int count(EventType type) const { return static_cast<int>(std::ranges::count(log, type, &GameEvent::type)); }
    std::vector<GameEvent> all(EventType type) const {
        std::vector<GameEvent> found;
        for (const GameEvent& event : log) {
            if (event.type == type) {
                found.push_back(event);
            }
        }
        return found;
    }
    std::optional<GameEvent> first(EventType type) const {
        const auto found = std::ranges::find(log, type, &GameEvent::type);
        return found == log.end() ? std::nullopt : std::optional<GameEvent>(*found);
    }
    bool destroyed(ActorIndex actor) const {
        return std::ranges::any_of(log, [&](const GameEvent& e) { return e.type == EventType::BrickDestroyed && e.actor == actor; });
    }
    Vec3 where(ActorIndex actor) const { return session.scene().actorPose(actor).translationPart(); }
    std::vector<Projectile> projectiles() const { return {session.projectiles().begin(), session.projectiles().end()}; }
};

// ---- Camera ------------------------------------------------------------------------------------------------------

// The height of the vertical coordinate on screen, -1 at the bottom edge to 1 at the top, of a point seen by the game's camera.
float screenY(Vec3 p) {
    const Camera camera;
    const Vec4 clip = transform(Vec4{p.x, p.y, p.z, 1}, camera.view() * camera.projection(16.0f / 9.0f));
    return clip.y / clip.w;
}

void testTopEdge() {
    // The vertical line through a point leaves the picture at its top edge at screenTopHeight(), at any aspect.
    for (const Vec3 below : {Vec3{0, 0, 0}, Vec3{-15, 1, 20}, Vec3{15, 0.5f, -15}, Vec3{5, 3, 10}}) {
        const Vec3 top{below.x, screenTopHeight(below), below.z};
        check(near(screenY(top), 1.0f, 2e-3f), "camera: the top edge of the screen above (" + num(below.x) + ", " + num(below.z) + ") is at " + num(top.y) +
                                                   ", which the camera sees at " + num(screenY(top)));
        check(top.y > below.y + 10, "camera: and it is high up");
    }
    // Farther from the camera, the top edge is lower on the board: it is 28 up at the near end and 16 up at the far end.
    check(screenTopHeight({0, 0, -15}) > screenTopHeight({0, 0, 0}) && screenTopHeight({0, 0, 0}) > screenTopHeight({0, 0, 20}),
          "camera: the top edge is higher over the near end of the board than over the far end");
    // The point where a meteor on its way to `target` was at the top edge.
    const Vec3 slant{-10, -30, 10};
    for (const Vec3 target : {Vec3{0, 0, 0}, Vec3{-12, 0, 18}, Vec3{14, 0, -8}}) {
        const Vec3 top = screenTopPoint(target, slant);
        check(near(screenY(top), 1.0f, 2e-3f), "camera: a meteor's top point is on the top edge");
        const Vec3 along = normalize(target - top);
        check(nearTo(along, normalize(slant), 1e-3f) && top.y > target.y, "camera: and on the meteor's line, above the target");
    }
}

// ---- The engine's kinds of body and rays -------------------------------------------------------------------------

void testKindsOfBody() {
    // Two bricks one behind the other, a ball and the bat in the line of fire, and a teleport.
    const Level level = levelOf({board(), brick(0, 8, "One.jpg"), brick(0, 14, "One.jpg"), portal(ObjectType::TeleportSrc, {6, 0, 4})});
    PhysicsScene scene(level, materials(), {}, [] {
        PhysicsSettings settings;
        settings.threads = 0;
        return settings;
    }());
    PhysicsShape sphere;
    sphere.type = PhysicsShapeType::Sphere;
    sphere.radius = 0.4f;
    // A projectile from z 0 at 30 along +z, over a ball sitting at z 2 and the bat at z 4, and a teleport at x 6.
    const BodyId ball = scene.addGameBall({0, 1.2f, 2}, {0, 0, 0});
    const BodyId bat = scene.addBat({0, 1.2f, 4}, 1.0f);
    check(ball != 0 && bat != 0, "kinds: the ball and the bat exist");
    const BodyId shot = scene.addProjectile(Mat4::translation({0, 1.2f, 0}), sphere, {0, 0, 30}, false, 5);
    const BodyId aside = scene.addProjectile(Mat4::translation({6, 1.2f, 0}), sphere, {0, 0, 30}, false, 5);
    check(shot != 0 && aside != 0 && near(scene.world().linearVelocity(shot).z, 30) && near(length(scene.world().pose(shot).translationPart() - Vec3{0, 1.2f, 0}), 0),
          "kinds: a projectile is made where and as it is told");
    bool touchedBallOrBat = false, touchedBrick = false, touchedPortal = false;
    std::vector<ContactEvent> seen;
    for (int i = 0; i < 40 && scene.world().bodyCount() > 0; ++i) {
        scene.step(1.0f / 60);
        for (const ContactEvent& contact : scene.contacts()) {
            const bool mine = contact.userDataA == kProjectileData || contact.userDataB == kProjectileData;
            const std::uint64_t other = contact.userDataA == kProjectileData ? contact.userDataB : contact.userDataA;
            if (mine && (other == kGameBallData || other == kBatData)) {
                touchedBallOrBat = true;
            }
            if (mine && other == 1) {
                touchedBrick = true;
            }
            if (mine && other == 3) {
                touchedPortal = true;
            }
        }
        if (touchedBrick) {
            break;
        }
    }
    check(!touchedBallOrBat, "kinds: a projectile goes through the ball and the bat");
    check(touchedBrick, "kinds: and meets the brick behind them (the contact is reported with the actor in the other slot)");
    check(!touchedPortal, "kinds: a teleport is not there for it");
    scene.removeProjectile(shot);
    scene.removeProjectile(aside);

    // Rays: raycastAll gives every body on the line once, nearest first.
    const std::vector<RayHit> hits = scene.world().raycastAll({0, 1, -5}, {0, 0, 1}, 40);
    const std::vector<std::uint64_t> order = [&] {
        std::vector<std::uint64_t> found;
        for (const RayHit& hit : hits) {
            found.push_back(scene.world().userData(hit.body));
        }
        return found;
    }();
    // The ball sits at z 2 (+-0.6), the bat at z 4 (+-1), the bricks at 8 and 14 (+-0.5), the far wall at 22.6: five bodies in this order.
    check(hits.size() == 5, "rays: five bodies are on the line (" + std::to_string(hits.size()) + ")");
    bool sorted = true;
    for (std::size_t i = 1; i < hits.size(); ++i) {
        sorted = sorted && hits[i].distance >= hits[i - 1].distance;
    }
    check(sorted, "rays: nearest first");
    if (hits.size() == 5) {
        check(near(hits[0].distance, 5 + 2 - 0.6f, 0.05f) && near(hits[2].distance, 5 + 8 - 0.5f, 0.05f) && near(hits[4].distance, 5 + 22.6f, 0.05f),
              "rays: each is hit where the ray enters it");
        check(order == std::vector<std::uint64_t>{kGameBallData, kBatData, 1, 2, 0}, "rays: the balls, the bat, the bricks and the board are told apart");
    }
    check(scene.world().raycastAll({0, 1, -5}, {0, 0, 1}, 0).empty() && scene.world().raycastAll({0, 1, -5}, {0, 0, 0}, 5).empty(),
          "rays: no length or no direction is no hit");
}

// ---- Firing ------------------------------------------------------------------------------------------------------

void testNoWeaponNoShot() {
    Game g(levelOf({board()}));
    check(g.play(), "no weapon: the ball is launched");
    g.click();
    check(g.count(EventType::ShotFired) == 0 && g.projectiles().empty(), "no weapon: a click fires nothing");
}

void testClickPriority() {
    // A ball held on the bat is launched by the click, not a shot fired.
    Game g(levelOf({board()}));
    g.give(PrizeKind::Gun);
    check(g.ready(), "click: the bat lands");
    g.click();
    check(g.count(EventType::BallLaunched) == 1 && g.count(EventType::ShotFired) == 0 && g.session.shotsLeft(WeaponType::Gun) == 12,
          "click: the click that launches the held ball fires no shot");
    g.wait();
    g.click();
    check(g.count(EventType::ShotFired) == 1 && g.session.shotsLeft(WeaponType::Gun) == 11, "click: the next click, with the ball in play, does");

    // While the bat is still swooping in with the ball on it, a click does nothing.
    Game h(levelOf({board()}));
    h.give(PrizeKind::Gun);
    h.click();
    check(h.session.phase() == SessionPhase::Entering && h.count(EventType::ShotFired) == 0 && h.count(EventType::BallLaunched) == 0,
          "click: the bat is swooping in: the click launches nothing and fires nothing");

    // The Magnet: a click lets go of the stuck ball, and only the next fires.
    Game m(levelOf({board()}));
    check(m.play(), "magnet: a ball in play");
    m.give(PrizeKind::Magnet);
    m.give(PrizeKind::Gun);
    // A ball back at the bat sticks to it: send one down on the bat.
    m.session.addBall({m.batX, 0.6f, -12}, {0, 0, -1});
    check(m.runUntil([&] { return m.count(EventType::BallStuck) > 0; }, 2), "magnet: the ball sticks to the bat");
    const int shotsBefore = m.session.shotsLeft(WeaponType::Gun);
    m.click();
    check(m.count(EventType::ShotFired) == 0 && m.session.shotsLeft(WeaponType::Gun) == shotsBefore,
          "magnet: the click lets the ball go and fires no shot");
    m.wait();
    m.click();
    check(m.count(EventType::ShotFired) == 1 && m.session.shotsLeft(WeaponType::Gun) == shotsBefore - 1, "magnet: the next click fires");
}

void testGunShots() {
    Game g(levelOf({board()}));
    check(g.play(), "gun: the ball is launched");
    g.give(PrizeKind::Gun);
    check(g.session.shotsLeft(WeaponType::Gun) == 12, "gun: twelve shots");
    g.click();
    const std::optional<GameEvent> fired = g.first(EventType::ShotFired);
    check(fired && fired->prize == PrizeKind::Gun && fired->count == 11 && g.session.shotsLeft(WeaponType::Gun) == 11,
          "gun: one click, one shot, shots left 11 (the shot count falls at the shot, M)");
    check(fired && fired->material == materials().find("bat_bullet") && nearTo(fired->velocity, {0, 0, 30}),
          "gun: the shot carries its material for the sound and its speed");
    const std::vector<Projectile> bullets = g.projectiles();
    check(bullets.size() == 2, "gun: two bullets a shot, the HUD counts shots (" + std::to_string(bullets.size()) + ")");
    if (bullets.size() == 2) {
        const float spread = std::fabs(bullets[0].position.x - bullets[1].position.x);
        check(near(spread, 2 * 2.1556f, 0.01f) && near((bullets[0].position.x + bullets[1].position.x) / 2, g.session.bat().position.x, 0.05f),
              "gun: the two bullets leave the two barrels 4.3 apart, either side of the bat");
        check(bullets[0].kind == ProjectileKind::Bullet && bullets[0].prize == PrizeKind::Gun && bullets[0].barrel == 0 && bullets[1].barrel == 1 &&
                  bullets[0].position.x < bullets[1].position.x,
              "gun: the left one is barrel 0");
        // One step on: from (x, 1.198, bat z + 3.411) at 30 along +z, with no gravity.
        const float z = g.session.bat().position.z + 3.41114f + 30.0f / 60;
        check(near(bullets[0].position.y, 1.19839f, 0.01f) && near(bullets[0].position.z, z, 0.05f) && nearTo(bullets[0].velocity, {0, 0, 30}, 0.01f),
              "gun: from y 1.198, z up the board at 30 units/s");
    }
    // No gravity: it flies level.
    g.run(0.3);
    for (const Projectile& bullet : g.projectiles()) {
        check(near(bullet.position.y, 1.19839f, 0.01f) && near(bullet.velocity.y, 0, 0.01f), "gun: a bullet is not pulled down (disableGravity)");
    }
}

void testShotInterval() {
    Game g(levelOf({board()}));
    check(g.play(), "interval: the ball is launched");
    g.give(PrizeKind::Gun);
    g.click();
    g.click();  // the very next step
    g.run(0.1);
    g.click();  // 0.12 s after the shot
    check(g.count(EventType::ShotFired) == 1 && g.session.shotsLeft(WeaponType::Gun) == 11,
          "interval: clicks within 0.2 s of the shot fire nothing and cost no shot");
    g.run(0.1);
    g.click();  // 0.25 s after
    check(g.count(EventType::ShotFired) == 2 && g.session.shotsLeft(WeaponType::Gun) == 10, "interval: one after it does");
}

void testWeaponsRunOut() {
    Game g(levelOf({board()}));
    check(g.play(), "run out: the ball is launched");
    g.give(PrizeKind::Laser);
    check(g.session.shotsLeft(WeaponType::Laser) == 5, "run out: five laser shots");
    for (int shot = 1; shot <= 5; ++shot) {
        g.click();
        check(g.session.shotsLeft(WeaponType::Laser) == 5 - shot, "run out: shot " + std::to_string(shot) + " leaves " + std::to_string(5 - shot));
        g.wait();
    }
    check(g.count(EventType::EffectEnded) == 1 && !g.session.weapon() && g.session.effect(PrizeKind::Laser) == nullptr,
          "run out: the effect ends with the last shot");
    g.click();
    check(g.count(EventType::ShotFired) == 5, "run out: a sixth click fires nothing");
    // The event of the last shot comes before the end of the effect.
    int lastFired = -1, ended = -1;
    for (std::size_t i = 0; i < g.log.size(); ++i) {
        lastFired = g.log[i].type == EventType::ShotFired ? static_cast<int>(i) : lastFired;
        ended = g.log[i].type == EventType::EffectEnded ? static_cast<int>(i) : ended;
    }
    check(lastFired >= 0 && ended > lastFired, "run out: ShotFired, then EffectEnded");
}

// 15.3: the weapon caught last is the one a click fires; the others keep their shots; a second catch of the same adds its shots.
void testWeaponReplacement() {
    Game g(levelOf({board()}));
    check(g.play(), "replace: the ball is launched");
    g.give(PrizeKind::Gun);
    g.give(PrizeKind::Cannon);
    check(g.session.weapon() && g.session.weapon()->type == WeaponType::Cannon && g.session.shotsLeft(WeaponType::Gun) == 12 &&
              g.session.shotsLeft(WeaponType::Cannon) == 6,
          "replace: the Cannon, caught after the Gun, is the weapon; the Gun keeps its twelve");
    g.click();
    check(g.projectiles().size() == 1 && g.projectiles()[0].kind == ProjectileKind::Kernel, "replace: a click fires the cannon");
    for (int i = 0; i < 5; ++i) {
        g.wait();
        g.click();
    }
    check(g.session.shotsLeft(WeaponType::Cannon) == 0 && g.session.weapon() && g.session.weapon()->type == WeaponType::Gun &&
              g.session.weapon()->shots == 12,
          "replace: when the Cannon is spent the Gun is next, with its twelve");
    g.give(PrizeKind::Bazooka);
    g.give(PrizeKind::Gun);  // a second Gun: 24 shots, and the weapon again
    check(g.session.shotsLeft(WeaponType::Gun) == 24 && g.session.weapon()->type == WeaponType::Gun && g.session.shotsLeft(WeaponType::Bazooka) == 7,
          "replace: a second catch of the Gun adds twelve and makes it the weapon again");
}

void testNothingFiresWhenOver() {
    // After the last ball is lost nothing fires, though a weapon is caught.
    Game lost(levelOf({board()}), options(), 8);
    check(lost.play(), "over: the ball is launched");
    // Take the ball out of play by leaving the bat far from it: the Skull ends the bat at once.
    lost.give(PrizeKind::Skull);
    check(lost.session.phase() == SessionPhase::Lost, "over: the Skull kills the bat");
    lost.give(PrizeKind::Gun);
    lost.click();
    check(lost.count(EventType::ShotFired) == 0 && lost.session.shotsLeft(WeaponType::Gun) == 12 && lost.projectiles().empty(),
          "over: no shot in the Lost phase");

    // Once the level is won, what is in the air is taken out and nothing more is fired.
    Game won(levelOf({board(), brick(8, 10, "One.jpg")}));
    check(won.play(), "over: the ball is launched again");
    won.give(PrizeKind::Cannon);
    won.click();
    check(won.projectiles().size() == 1, "over: a kernel is in the air");
    won.session.forceLevelComplete();
    won.run(0.05);
    check(won.projectiles().empty() && won.count(EventType::ProjectileGone) == 1 && won.session.weapons().strikesPending() == 0,
          "over: the level is won, the kernel is taken out and reported gone");
    won.wait();
    won.click();
    check(won.count(EventType::ShotFired) == 1, "over: and a click fires nothing");
}

// ---- What the projectiles are ------------------------------------------------------------------------------------

void testSpawnsAndVelocities() {
    {  // the cannon: from (0, 1.704, 3.067) in the bat's frame, (0, 3, 40), lobbed
        Game g(levelOf({board()}));
        check(g.play(), "kernel: the ball is launched");
        g.give(PrizeKind::Cannon);
        g.click();
        const std::vector<Projectile> shots = g.projectiles();
        check(shots.size() == 1 && shots[0].kind == ProjectileKind::Kernel, "kernel: one kernel");
        const Vec3 bat = g.session.bat().position;
        if (!shots.empty()) {
            const float dt = 1.0f / 60;
            check(nearTo(shots[0].position, {bat.x, 1.70413f + 3 * dt - 0.5f * 9.8f * dt * dt, bat.z + 3.06672f + 40 * dt}, 0.03f),
                  "kernel: it leaves from (0, 1.704, 3.067) of the bat's frame, a step on");
            check(near(shots[0].velocity.z, 40, 0.01f) && near(shots[0].velocity.y, 3 - 9.8f * dt, 0.01f), "kernel: at (0, 3, 40), pulled by gravity");
        }
        g.run(0.5);
        const std::vector<Projectile> later = g.projectiles();
        check(!later.empty() && later[0].velocity.y < -1.5f && near(later[0].velocity.z, 40, 0.1f),
              "kernel: half a second on it falls (a lob) and has lost none of its speed along the board");
        const std::optional<GameEvent> fired = g.first(EventType::ShotFired);
        check(fired && fired->material == materials().find("bat_kernel") && near(fired->velocity.z, 40), "kernel: the event says what was fired");
    }
    {  // the bazooka: (0.006, 1.406, 4.791), (0, 0, 52.5), straight
        Game g(levelOf({board()}));
        g.play();
        g.give(PrizeKind::Bazooka);
        g.click();
        const std::vector<Projectile> shots = g.projectiles();
        check(shots.size() == 1 && shots[0].kind == ProjectileKind::Rocket, "rocket: one rocket");
        const Vec3 bat = g.session.bat().position;
        if (!shots.empty()) {
            check(nearTo(shots[0].position, {bat.x + 0.00554f, 1.40648f, bat.z + 4.79101f + 52.5f / 60}, 0.05f) && nearTo(shots[0].velocity, {0, 0, 52.5f}, 0.05f),
                  "rocket: from (0.006, 1.406, 4.791) at 52.5 up the board");
        }
        g.run(0.3);
        check(!g.projectiles().empty() && near(g.projectiles()[0].position.y, 1.40648f, 0.02f), "rocket: it flies level (disableGravity)");
    }
    {  // the barrels are on the bat model, which is drawn batScale() times its size
        Game g(levelOf({board()}));
        g.play();
        g.give(PrizeKind::IncBat);
        g.give(PrizeKind::Gun);
        g.run(0.5);  // the bat is rebuilt at the new size
        g.click();
        const std::vector<Projectile> bullets = g.projectiles();
        check(bullets.size() == 2, "scale: two bullets");
        if (bullets.size() == 2) {
            check(near(std::fabs(bullets[1].position.x - bullets[0].position.x), 2 * 2.1556f * 1.24f, 0.05f) &&
                      near(bullets[0].position.z, g.session.bat().position.z + 3.41114f * 1.24f + 0.5f, 0.1f),
                  "scale: a wider bat's barrels are farther apart and farther out, 1.24 times");
        }
    }
}

void testProjectileGone() {
    // With no far wall, a bullet leaves the world and is reported gone, once.
    Game g(levelOf({board(false)}));
    check(g.play(), "gone: the ball is launched");
    g.give(PrizeKind::Gun);
    g.click();
    g.run(4.0);
    check(g.count(EventType::ProjectileGone) == 2 && g.projectiles().empty() && g.count(EventType::ProjectileHit) == 0,
          "gone: two bullets that hit nothing leave the board and are reported gone, and the list is empty");
    check(g.session.weapons().stats().gone == 2, "gone: the statistics count them");
}

// ---- What they do to bricks --------------------------------------------------------------------------------------

// The energy a brick of each of one to four has left after the one projectile of each kind meets it.
void testDamageTable() {
    struct Row {
        const char* name;
        PrizeKind weapon;
        int damage;
    };
    const std::array<Row, 4> rows = {{{"bullet", PrizeKind::Gun, 1}, {"kernel", PrizeKind::Cannon, 3}, {"rocket", PrizeKind::Bazooka, 2}, {"beam", PrizeKind::Laser, 1}}};
    for (const Row& row : rows) {
        for (int energy = 1; energy <= 4; ++energy) {
            const char* material = energy == 1 ? "One.jpg" : (energy == 2 ? "Two.jpg" : (energy == 3 ? "Three.jpg" : "Four.jpg"));
            // In the lane of the bat's centre (a gun's bullets fly either side of it: the brick is 2.1556 to the left).
            const float x = row.weapon == PrizeKind::Gun ? 8 - 2.1556f : 8.0f;
            Game g(levelOf({board(), brick(x, 8, material)}));
            check(g.play(), std::string(row.name) + ": the ball is launched");
            g.give(row.weapon);
            g.click();
            g.run(1.2);
            const int left = std::max(0, energy - row.damage);
            const ActorStatus status = g.session.actor(1);
            check(g.session.stats().bricksHit == std::min(energy, row.damage) && (left == 0 ? g.destroyed(1) : (status.energy == left && !g.destroyed(1))),
                  std::string(row.name) + " against energy " + std::to_string(energy) + ": takes " + std::to_string(row.damage) + ", leaves " +
                      std::to_string(left) + " (is " + std::to_string(status.energy) + ", hit " + std::to_string(g.session.stats().bricksHit) + ")");
        }
    }
}


// ---- Cannon and Bazooka: the burst ---------------------------------------------------------------------------------

void testKernel() {
    // Over level ground it comes down about 15 units on (a lob of 0.82 s from 1.7 up), and bursts there.
    {
        Game g(levelOf({board()}));
        check(g.play(), "kernel: the ball is launched");
        g.give(PrizeKind::Cannon);
        g.click();
        g.run(1.5);
        const std::optional<GameEvent> fired = g.first(EventType::ShotFired), hit = g.first(EventType::ProjectileHit),
                                       burst = g.first(EventType::ProjectileExploded);
        check(hit && burst && fired && hit->actor == 0 && near(hit->position.z, 14.9f, 0.7f) && near(hit->position.y, 0, 0.1f),
              "kernel: with nothing in the way it lands on the board about 15 units up it (a lob under gravity)");
        check(hit && fired && near(static_cast<float>(hit->time - fired->time), 0.82f, 0.05f), "kernel: after about 0.82 s");
        check(burst && near(burst->speed, 2.0f) && hit && nearTo(burst->position, hit->position, 1e-3f) && g.projectiles().empty(),
              "kernel: the burst is where it hit, radius 2, and the kernel is spent");
    }
    // A kernel on a column of four energy: three off it, one off what is within 2 units, none off what is beyond, and loose things pushed away.
    {
        Game g(levelOf({board(), brick(8, 8, "Four.jpg"), brick(10.2f, 8, "Two.jpg"), brick(11.0f + 0.5f, 8, "Two.jpg"),
                        actor(ObjectType::Brick, {6.0f, 0.5f, 8.0f}, kDynamic, 30, {box({0.5f, 0.5f, 0.5f})}, "Four.jpg")}));
        check(g.play(), "kernel: the ball is launched for the burst");
        g.give(PrizeKind::Cannon);
        g.click();
        check(g.runUntil([&] { return g.count(EventType::ProjectileExploded) > 0; }, 1.5), "kernel: it bursts");
        g.step();
        const Vec3 pushed = g.session.scene().world().linearVelocity(g.session.scene().body(4));
        g.run(0.2);
        const ActorStatus target = g.session.actor(1), near1 = g.session.actor(2), far1 = g.session.actor(3), loose = g.session.actor(4);
        check(target.energy == 1 && near1.energy == 1 && far1.energy == 2 && loose.energy == 3,
              "kernel: the column of four is left with 1, the one 1.7 units off with 1, the one 2.5 off with 2, the loose one (1.5 off) with 3 (" +
                  std::to_string(target.energy) + " " + std::to_string(near1.energy) + " " + std::to_string(far1.energy) + " " +
                  std::to_string(loose.energy) + ")");
        check(pushed.x < -1.0f && pushed.y > 0.3f, "kernel: the loose block 1.5 units off the burst is pushed away from it and up (" + num(pushed.x) + ", " + num(pushed.y) + ")");
    }
}

void testRocket() {
    Game g(levelOf({board(), brick(8, 8, "Three.jpg"), brick(10.5f, 8, "Two.jpg"), brick(11.2f, 8, "Two.jpg"), brick(8, 14, "One.jpg")}));
    check(g.play(), "rocket: the ball is launched");
    g.give(PrizeKind::Bazooka);
    g.click();
    g.run(0.7);
    const std::optional<GameEvent> hit = g.first(EventType::ProjectileHit), burst = g.first(EventType::ProjectileExploded);
    check(hit && burst && hit->actor == 1 && near(hit->position.z, 7.5f, 0.1f) && near(burst->speed, 2.25f) && burst->material == materials().find("bat_rocket"),
          "rocket: it explodes on the first thing it meets, with a burst of radius 2.25 (a star 4.5 wide)");
    // Two off the column, one off what is within 2.25 of the burst (the box edge at 2.0 is), none off the one at 2.7.
    check(g.session.actor(1).energy == 1 && g.session.actor(2).energy == 1 && g.session.actor(3).energy == 2,
          "rocket: two off the column of three, one off the brick whose edge is 2.0 from the burst, none off the one 2.7 off");
    check(g.session.actor(4).energy == 1 && !g.destroyed(4), "rocket: the brick behind the column, 6 units on, is left alone");
    check(g.count(EventType::ShotFired) == 1 && g.session.shotsLeft(WeaponType::Bazooka) == 6, "rocket: one shot spent");
}

void testHitsThatDamageNothing() {
    // Steel and the far wall are no damageable thing: a bullet that meets them is spent, with a ProjectileHit and nothing else.
    Game g(levelOf({board(), brick(8 - 2.1556f, 6, "Steel.jpg"), brick(8 - 2.1556f, 12, "One.jpg")}));
    check(g.play(), "wall: the ball is launched");
    g.give(PrizeKind::Gun);
    g.click();
    g.run(2.0);
    const std::vector<GameEvent> hits = g.all(EventType::ProjectileHit);
    check(hits.size() == 2 && hits[0].actor == 1 && hits[1].actor == 0 && near(hits[1].position.z, 22.6f, 0.1f),
          "wall: the left bullet stops at the steel, the right one at the far wall");
    check(g.count(EventType::BrickHit) == 0 && !g.destroyed(2) && g.projectiles().empty(), "wall: neither hurts anything; the brick behind the steel is untouched");
    check(!g.session.actor(1).damageable, "wall: and the steel is not damageable");
}

// ---- The laser -----------------------------------------------------------------------------------------------------

void testLaser() {
    {  // it goes through everything it can hurt, once each, 0.3 wide: a pirate of three survives, the planks die
        Game g(levelOf({board(), brick(8, 4, "One.jpg"), brick(8, 8, "One.jpg"), brick(8, 12, "One.jpg"), brick(8, 16, "Three.jpg"),
                        brick(8 + 1.0f, 6, "One.jpg"),    // 0.5 clear of the beam's width
                        brick(8 + 0.6f, 10, "One.jpg"),   // its edge 0.1 from the axis: the beam's side rays take it
                        brick(8 - 0.7f, 14, "One.jpg")}));  // its edge 0.2 from the axis: out
        check(g.play(), "laser: the ball is launched");
        g.give(PrizeKind::Laser);
        g.click();
        const std::optional<GameEvent> beam = g.first(EventType::LaserBeam);
        check(beam && beam->count == 6 && beam->material == materials().find("bat_lazer") && near(beam->end.z, 22.6f, 0.1f) && near(beam->position.y, 1.0f) &&
                  near(beam->position.x, 8, 0.05f) && near(beam->position.z, -21 + 2.91f, 0.05f),
              "laser: from the barrel up the board to the far wall, reaching the four in its line, the one it grazes and the wall");
        g.run(0.3);
        check(g.destroyed(1) && g.destroyed(2) && g.destroyed(3) && g.destroyed(6) && !g.destroyed(4) && !g.destroyed(5) && !g.destroyed(7),
              "laser: it kills the planks along its line, and not the ones beside it or the pirate");
        check(g.session.actor(4).energy == 2 && !g.session.actor(4).removed, "laser: a pirate of three energy survives one beam with two [M]");
        check(g.count(EventType::BrickHit) == 5 && g.session.stats().bricksHit == 5, "laser: one hit for each of the five");
        check(g.count(EventType::ShotFired) == 1 && g.session.shotsLeft(WeaponType::Laser) == 4 && g.projectiles().empty(),
              "laser: one shot, and no body flies");
    }
    {  // it stops at what it cannot hurt
        Game g(levelOf({board(), brick(8, 5, "One.jpg"), actor(ObjectType::Brick, {8, 1.5f, 10}, kStatic, 1000, {box({4, 1.5f, 0.5f})}, "Steel.jpg"),
                        brick(8, 16, "One.jpg")}));
        g.play();
        g.give(PrizeKind::Laser);
        g.click();
        g.run(0.3);
        const std::optional<GameEvent> beam = g.first(EventType::LaserBeam);
        check(beam && near(beam->end.z, 9.5f, 0.05f) && beam->count == 2, "laser: a wall of steel stops it at its near face, having reached the brick before it and the steel");
        check(g.destroyed(1) && !g.destroyed(3) && g.session.actor(3).energy == 1, "laser: and the brick behind the steel is not touched");
    }
    {  // one hit for a body of several shapes
        Game g(levelOf({board(), actor(ObjectType::Brick, {8, 0, 8}, kStatic, 100,
                                       {box({0.5f, 1.5f, 0.5f}, {0, 1.5f, 0}), box({0.5f, 1.5f, 0.5f}, {0, 1.5f, 2}), box({0.5f, 1.5f, 0.5f}, {0, 1.5f, 4})},
                                       "Three.jpg")}));
        g.play();
        g.give(PrizeKind::Laser);
        g.click();
        g.run(0.2);
        check(g.session.actor(1).energy == 2 && g.session.stats().bricksHit == 1 && g.first(EventType::LaserBeam)->count == 2,
              "laser: a thing of three shapes in line is hurt once, not three times");
    }
    {  // the balls, the bat and the teleports are not in its way
        Game g(levelOf({board(), portal(ObjectType::TeleportSrc, {8 + 1.3f, 0, 6}), brick(8, 12, "One.jpg")}));
        g.play();
        g.give(PrizeKind::Laser);
        g.session.addBall({8, 0.6f, 3}, {1, 0, 0});  // right in the line, at the height of the barrel
        g.flush();
        g.click();
        g.run(0.3);
        check(g.destroyed(2) && g.session.actor(1).removed == false, "laser: it goes through a ball in its line and the plate of a teleport and kills the brick behind");
        const std::optional<GameEvent> beam = g.first(EventType::LaserBeam);
        check(beam && beam->count == 2, "laser: it reached the brick and the wall, not the teleport (" + std::to_string(beam ? beam->count : -1) + ")");
    }
    {  // it fades
        Game g(levelOf({board()}));
        g.play();
        g.give(PrizeKind::Laser);
        g.click();
        check(g.session.laserBeams().size() == 1 && near(g.session.laserBeams()[0].to.z, 22.6f, 0.1f) && g.session.laserBeams()[0].hits == 1,
              "laser: a beam is on show after the shot");
        g.run(0.3);
        check(g.session.laserBeams().size() == 1, "laser: still at 0.3 s");
        g.run(0.2);
        check(g.session.laserBeams().empty(), "laser: gone by 0.5 s (it fades in about 0.45 s)");
    }
    {  // with no wall it reaches the end of its range
        Game g(levelOf({board(false)}));
        g.play();
        g.give(PrizeKind::Laser);
        g.click();
        const std::optional<GameEvent> beam = g.first(EventType::LaserBeam);
        check(beam && beam->count == 0 && near(beam->end.z, beam->position.z + 60, 0.01f), "laser: with nothing in the way it goes as far as it reaches, 60 units");
    }
}

// ---- The special objects -----------------------------------------------------------------------------------------

void testSpecialObjects() {
    {  // dynamite is set off by a bullet: 0.1 s after it, and takes the next charge and a brick in its reach
        Game g(levelOf({board(), dynamite({8 - 2.1556f, 0, 8}), dynamite({8 - 2.1556f, 0, 11.5f}), brick(8 - 2.1556f - 3, 8, "One.jpg"),
                        brick(8 - 2.1556f, 20, "One.jpg")}));
        g.play();
        g.give(PrizeKind::Gun);
        g.click();
        g.run(1.6);
        const auto blasts = g.all(EventType::DynamiteBlast);
        check(blasts.size() == 2 && blasts[0].actor == 1 && blasts[1].actor == 2, "dynamite: a bullet sets off the charge, and the chain takes the next one");
        const std::optional<GameEvent> hit = g.first(EventType::BrickHit);
        check(hit && !blasts.empty() && near(static_cast<float>(blasts[0].time - hit->time), 0.1f, 0.02f),
              "dynamite: it goes 0.1 s after the bullet (its exploseDelay), as for the ball");
        check(g.destroyed(3) && !g.destroyed(4), "dynamite: the blast takes the brick in its reach, not the one 12 units on");
        check(g.session.score() == 120 + 120 + 10, "dynamite: the score is the charges' and the brick's");
    }
    {  // a cannon has two energy: two bullets, or a kernel; it is not fired by them
        Game g(levelOf({board(), cannon({8 - 2.1556f, 0, 8})}));
        g.play();
        g.give(PrizeKind::Gun);
        g.click();
        g.run(1.0);
        check(g.session.actor(1).energy == 1 && !g.session.actor(1).removed, "cannon: a bullet takes one of its two energy");
        g.wait();
        g.click();
        g.run(1.0);
        check(g.destroyed(1) && g.session.score() == 100, "cannon: the second destroys it, for 100 points");
        check(g.count(EventType::CannonFired) == 0, "cannon: a weapon does not set it off");
        Game h(levelOf({board(), cannon({8, 0, 8})}));
        h.play();
        h.give(PrizeKind::Cannon);
        h.click();
        h.run(1.2);
        check(h.destroyed(1), "cannon: a kernel destroys it at once");
    }
    {  // a firework crate a weapon destroys gives no rocket
        Game g(levelOf({board(), firework({8 - 2.1556f, 0, 8})}));
        g.play();
        g.give(PrizeKind::Gun);
        g.click();
        g.run(2.0);
        check(g.destroyed(1) && g.count(EventType::RocketLaunched) == 0 && g.session.score() == 120, "firework: a bullet destroys the crate (120 points) and no rocket comes");
    }
    {  // metal, a spring, a volcano and a generator, in one line: a beam takes them all
        Game g(levelOf({board(), thing(ObjectType::Metall, {8, 0, 4}, "Metall.jpg"), thing(ObjectType::Spring, {8, 0, 8}, "spring"),
                        thing(ObjectType::Volcano, {8, 0, 12}, "volcano"), thing(ObjectType::GeneratorGood, {8, 0, 16}, "gen-good")}));
        g.play();
        g.give(PrizeKind::Laser);
        g.click();
        g.run(0.5);
        check(g.session.score() == 50 + 10 + 10 + 77, "special: a beam takes metal, a spring, a volcano and a generator for their points");
        for (ActorIndex i = 1; i <= 4; ++i) {
            check(g.destroyed(i), "special: actor " + std::to_string(i) + " is destroyed");
        }
    }
    {  // the teleports: nothing hurts them and a bullet goes through the plate in its line to what is behind
        Game g(levelOf({board(), portal(ObjectType::TeleportSrc, {8 - 2.1556f + 1.3f, 0, 6}), portal(ObjectType::TeleportDest, {-10, 0, 10}),
                        brick(8 - 2.1556f, 14, "One.jpg")}));
        g.play();
        g.give(PrizeKind::Gun);
        g.click();
        g.run(1.6);
        const std::vector<GameEvent> hits = g.all(EventType::ProjectileHit);
        check(g.destroyed(3), "teleport: a bullet goes through the plate of a teleport and destroys the brick behind it");
        check(std::ranges::none_of(hits, [](const GameEvent& e) { return e.actor == 1 || e.actor == 2; }) && !g.session.actor(1).damageable &&
                  !g.session.actor(1).removed,
              "teleport: no projectile hit it, and nothing damages it");
    }
}

// ---- The prizes a kill may leave ---------------------------------------------------------------------------------

void testPrizeDrops() {
    // With a chance of one, a kill by any weapon leaves a prize; a bolt and a meteor leave none (disablePrize [D]).
    for (const PrizeKind weapon : {PrizeKind::Gun, PrizeKind::Cannon, PrizeKind::Bazooka, PrizeKind::Laser}) {
        Game g(levelOf({board(), brick(8 - 2.1556f, 8, "One.jpg"), brick(8, 8, "One.jpg")}), dropsAlways());
        g.play();
        g.give(weapon);
        g.click();
        g.run(1.5);
        const std::string name(prizeInfo(weapon).stem);
        check(g.count(EventType::BrickDestroyed) >= 1 && g.count(EventType::PrizeDropped) == g.count(EventType::BrickDestroyed),
              "drops: every brick a " + name + " kills leaves a prize (" + std::to_string(g.count(EventType::PrizeDropped)) + " for " +
                  std::to_string(g.count(EventType::BrickDestroyed)) + ")");
    }
    for (const PrizeKind strike : {PrizeKind::Thunderbolt, PrizeKind::Meteor}) {
        std::vector<Actor> actors = {board(), brick(0, 5, "Two.jpg", kStatic, {19, 0.5f, 16})};  // under every meteor
        for (int i = 0; i < 8; ++i) {
            actors.push_back(brick(10.0f + static_cast<float>(i), 4.0f + 2.0f * static_cast<float>(i), "One.jpg"));
        }
        Game g(levelOf(actors), dropsAlways(), 8);
        g.ready();  // (the ball stays on the bat: strikes need no ball in play, and none may hurt a brick)
        g.give(strike);
        g.run(14.0);
        const std::string name(prizeInfo(strike).stem);
        check(g.count(EventType::BrickDestroyed) >= 1 && g.count(EventType::PrizeDropped) == 0,
              "drops: what a " + name + " destroys leaves no prize (" + std::to_string(g.count(EventType::BrickDestroyed)) + " destroyed, " +
                  std::to_string(g.count(EventType::PrizeDropped)) + " prizes)");
    }
}

// ---- The strikes -------------------------------------------------------------------------------------------------

// A field of one-energy columns out of the ball's way (the ball goes left of the bat at x 8).
Level boltField() {
    std::vector<Actor> actors = {board()};
    for (int i = 0; i < 12; ++i) {
        actors.push_back(brick(10.0f + 0.7f * static_cast<float>(i), 3.0f + 1.5f * static_cast<float>(i), "One.jpg"));
    }
    return levelOf(std::move(actors));
}

void testThunderbolt() {
    const SessionOptions drops = dropsAlways();
    Game g(boltField(), drops, 8);
    check(g.ready(), "bolt: the bat lands");
    g.log.clear();
    g.give(PrizeKind::Thunderbolt);
    const double caughtAt = g.session.time();
    check(g.count(EventType::StrikeGranted) == 1, "bolt: the catch grants the strikes");
    const int pending = g.session.weapons().strikesPending();
    check(pending >= 2 + 2 && pending <= 3 + 2, "bolt: a burst of 2 or 3 and two more are due (" + std::to_string(pending) + ")");
    g.run(13.5);
    const std::vector<GameEvent> struck = g.all(EventType::BoltStruck);
    check(static_cast<int>(struck.size()) == pending && g.session.weapons().strikesPending() == 0, "bolt: every strike is thrown and strikes");
    const int burst = pending - 2;
    // The burst: its bolts strike within 0.7 s of each other; the rest come later, up to about 12 s after the catch.
    if (static_cast<int>(struck.size()) == pending) {
        std::vector<double> at;
        for (const GameEvent& event : struck) {
            at.push_back(event.time - caughtAt);
        }
        std::ranges::sort(at);
        check(at[static_cast<std::size_t>(burst) - 1] - at[0] <= 0.7, "bolt: the burst strikes within 0.7 s (" + num(at[static_cast<std::size_t>(burst) - 1] - at[0]) + ")");
        check(at[0] < 1.0, "bolt: the first strike comes at once (within a second of the catch)");
        check(at[static_cast<std::size_t>(burst)] > 2.0 && at.back() < 12.0 + 1.0, "bolt: the later strikes come after 2 s and before about 12 s (" +
                                                                                      num(at[static_cast<std::size_t>(burst)]) + " to " + num(at.back()) + ")");
    }
    std::set<ActorIndex> targets;
    bool onBricks = true, fromTop = true, aboveTarget = true;
    for (const GameEvent& event : struck) {
        targets.insert(event.actor);
        onBricks = onBricks && event.actor >= 1 && event.actor <= 12;
        fromTop = fromTop && near(screenY(event.end), 1.0f, 2e-3f) && near(event.end.x, event.position.x) && near(event.end.z, event.position.z);
        aboveTarget = aboveTarget && event.end.y > event.position.y + 10;
        check(event.material == materials().find("thunderbolt") && event.prize == PrizeKind::Thunderbolt, "bolt: the event carries its material");
    }
    check(onBricks && targets.size() >= 3, "bolt: the bolts strike the standing bricks, at different places (" + std::to_string(targets.size()) + " of them)");
    check(fromTop && aboveTarget, "bolt: each is drawn from the top edge of the screen straight above where it struck");
    check(g.count(EventType::BrickDestroyed) >= 3 && g.count(EventType::PrizeDropped) == 0, "bolt: what it destroys leaves no prize");
    check(g.session.stats().bricksDestroyed >= 3 && g.session.score() == 10 * g.session.stats().bricksDestroyed, "bolt: and scores the brick's points");

    // A bolt takes the one energy of a brick, not metal.
    Game m(levelOf({board(), thing(ObjectType::Metall, {12, 0, 6}, "Metall.jpg"), brick(14, 9, "One.jpg")}), options(), 8);
    m.ready();
    m.give(PrizeKind::Thunderbolt);
    m.run(3);
    const std::vector<GameEvent> first = m.all(EventType::BoltStruck);
    check(!first.empty() && first[0].actor == 2 && !m.session.actor(1).removed, "bolt: the first bolt falls on the brick, and the metal is spared");

    // The same game again: the same targets at the same times; another seed, other targets.
    const auto run = [](std::uint64_t seed) {
        SessionOptions o = options();
        o.prizeSeed = seed;
        Game game(boltField(), o, 8);
        game.ready();
        game.give(PrizeKind::Thunderbolt);
        game.run(13);
        std::vector<std::string> lines;
        for (const GameEvent& event : game.log) {
            if (event.type == EventType::BoltStruck) {
                lines.push_back(describe(event));
            }
        }
        return lines;
    };
    check(!run(1).empty() && run(1) == run(1), "bolt: a game repeats");
    check(run(1) != run(2), "bolt: another seed, other targets");
}

void testMeteors() {
    {
        Game g(levelOf({board()}), options(), 8);
        check(g.ready(), "meteor: the bat lands");
        g.log.clear();
        g.give(PrizeKind::Meteor);
        const double caughtAt = g.session.time();
        check(g.session.weapons().strikesPending() == 5, "meteor: five meteors are due");
        g.run(9.0);
        const std::vector<GameEvent> falling = g.all(EventType::MeteorFalling), impacts = g.all(EventType::MeteorImpact);
        check(falling.size() == 5 && impacts.size() == 5 && g.session.weapons().strikesPending() == 0 && g.projectiles().empty(),
              "meteor: five fall and five strike (" + std::to_string(falling.size()) + ", " + std::to_string(impacts.size()) + ")");
        bool slot = falling.size() == 5, slanted = true, high = true, inField = true, landed = true, onWater = true;
        for (std::size_t i = 0; i < falling.size(); ++i) {
            const double at = falling[i].time - caughtAt;
            slot = slot && at >= static_cast<double>(i) - 0.05 && at < static_cast<double>(i) + 1.05;  // one in each fifth of the five seconds
            slanted = slanted && nearTo(falling[i].velocity, {-10, -30, 10}, 1e-3f);
            high = high && screenY(falling[i].position) > 1.0f && screenY(falling[i].position) < 1.4f;
            inField = inField && std::fabs(falling[i].end.x) <= 18.01f && falling[i].end.z >= -10.01f && falling[i].end.z <= 20.01f && falling[i].end.y == 0;
        }
        for (std::size_t i = 0; i < impacts.size() && i < falling.size(); ++i) {
            // It comes down on the point it was aimed at, along its slant, on the water (the board's actor 0).
            landed = landed && near(impacts[i].position.x, falling[i].end.x, 0.1f) && near(impacts[i].position.z, falling[i].end.z, 0.1f);
            onWater = onWater && impacts[i].actor == 0 && near(impacts[i].position.y, 0, 0.05f) && impacts[i].material == materials().find("meteor");
        }
        check(slot, "meteor: one in each fifth of five seconds");
        check(slanted && high, "meteor: each starts just above the top edge of the screen, with the velocity (-10, -30, 10) of meteor.XML");
        check(inField && landed && onWater, "meteor: and comes down on a point of the field, on the water");
        check(g.count(EventType::ProjectileExploded) == 5 && near(g.all(EventType::ProjectileExploded)[0].speed, 2.0f), "meteor: each bursts, radius 2");
    }
    {  // in flight a meteor is a body with that velocity and no gravity
        Game g(levelOf({board()}), options(), 8);
        g.ready();
        g.give(PrizeKind::Meteor);
        std::optional<Projectile> seen;
        for (int i = 0; i < 6 * 60 && !seen; ++i) {
            g.step();
            if (!g.projectiles().empty()) {
                seen = g.projectiles()[0];
            }
        }
        check(seen && seen->kind == ProjectileKind::Meteor && seen->prize == PrizeKind::Meteor && nearTo(seen->velocity, {-10, -30, 10}, 0.05f),
              "meteor: a meteor in flight is a projectile with the data's velocity");
        g.run(0.1);
        check(!g.projectiles().empty() && nearTo(g.projectiles()[0].velocity, {-10, -30, 10}, 0.05f), "meteor: which gravity does not change (disableGravity)");
    }
    {  // it hurts what it hits: a slab under the whole field of energy two
        Game g(levelOf({board(), brick(0, 5, "Two.jpg", kStatic, {19, 0.5f, 16})}), options(), 8);
        g.ready();
        g.give(PrizeKind::Meteor);
        g.run(9.0);
        const std::vector<GameEvent> impacts = g.all(EventType::MeteorImpact);
        check(!impacts.empty() && impacts[0].actor == 1 && g.destroyed(1), "meteor: the first, which falls on the slab, destroys it (it takes two energy)");
    }
}

// A bolt takes one energy of what it hits, a meteor two.
void testStrikeDamage() {
    {  // the only brick on the board, of four energy: every bolt falls on it, one energy a bolt, while it stands
        Game g(levelOf({board(), brick(14, 9, "Four.jpg")}), options(), 8);
        g.ready();
        g.give(PrizeKind::Thunderbolt);
        g.run(13.5);
        const std::vector<GameEvent> struck = g.all(EventType::BoltStruck);
        const int onIt = static_cast<int>(std::ranges::count(struck, ActorIndex{1}, &GameEvent::actor));
        const int taken = 4 - std::max(0, g.session.actor(1).energy);
        check(onIt >= 4 && g.session.stats().bricksHit == std::min(4, onIt) && taken == std::min(4, onIt) && g.destroyed(1),
              "bolt damage: one energy off a brick of four for each bolt that falls on it (" + std::to_string(onIt) + " bolts, " +
                  std::to_string(g.session.stats().bricksHit) + " hits)");
        for (const GameEvent& event : g.log) {
            if (event.type == EventType::BrickHit) {
                check(event.ball == -1, "bolt damage: the hit is no ball's");
            }
        }
    }
    {  // a slab under the whole field: the first meteor takes two energy of its four, the second the rest
        Game g(levelOf({board(), brick(0, 5, "Four.jpg", kStatic, {19, 0.5f, 16})}), options(), 8);
        g.ready();
        g.give(PrizeKind::Meteor);
        g.run(9.0);
        const std::vector<GameEvent> impacts = g.all(EventType::MeteorImpact);
        std::vector<int> left;
        for (const GameEvent& event : g.all(EventType::BrickHit)) {
            left.push_back(event.energy);
        }
        check(impacts.size() >= 2 && impacts[0].actor == 1 && impacts[1].actor == 1 && left == std::vector<int>{3, 2, 1, 0} && g.destroyed(1),
              "meteor damage: two energy off the slab for each of the first two meteors");
    }
}

// What a bullet does to a loose body: it is pushed up the board by the bullet's momentum, in the bullet's own physics.
void testBulletsPush() {
    Game g(levelOf({board(), actor(ObjectType::Brick, {8 - 2.1556f, 1.5f, 10}, kDynamic, 30, {box({1, 1.5f, 1})}, "Four.jpg")}));
    check(g.play(), "push: the ball is launched");
    g.give(PrizeKind::Gun);
    g.click();
    check(g.runUntil([&] { return g.count(EventType::ProjectileHit) > 0; }, 2), "push: the bullet reaches the block");
    g.step();
    const Vec3 v = g.session.scene().world().linearVelocity(g.session.scene().body(1));
    // A bullet of 2.5 and a block of 30: the block leaves with about 2 units/s along the board.
    check(v.z > 1.0f && v.z < 6.0f && g.session.actor(1).energy == 3, "push: a block of 30 is sent off at " + num(v.z) + " units/s and hurt for one energy");
}

void testStrikesStopWhenOver() {
    Game g(boltField(), options(), 8);
    g.ready();
    g.give(PrizeKind::Thunderbolt);
    g.give(PrizeKind::Meteor);
    check(g.session.weapons().strikesPending() >= 9, "strikes: nine or ten are due");
    g.run(0.6);
    g.session.forceLevelComplete();
    g.run(0.1);
    const int before = g.count(EventType::BoltStruck) + g.count(EventType::MeteorImpact);
    g.run(14.0);
    check(g.session.weapons().strikesPending() == 0 && g.count(EventType::BoltStruck) + g.count(EventType::MeteorImpact) == before && g.projectiles().empty(),
          "strikes: once the level is won none is thrown, and what was falling is taken out");
    g.give(PrizeKind::Thunderbolt);
    check(g.session.weapons().strikesPending() == 0, "strikes: and a catch during the fade starts none");
}

void testDeterminism() {
    // An autopilot game with a gun and both strikes, twice: every event the same.
    const auto play = [] {
        std::vector<Actor> actors = {board()};
        for (int i = 0; i < 20; ++i) {
            actors.push_back(brick(-12.0f + 1.3f * static_cast<float>(i), 4.0f + 2.0f * static_cast<float>(i % 7), i % 3 == 0 ? "Two.jpg" : "One.jpg"));
        }
        SessionOptions o = dropsAlways();
        GameSession session(levelOf(std::move(actors)), materials(), o);
        session.givePrize(PrizeKind::Gun);
        session.givePrize(PrizeKind::Cannon);
        session.givePrize(PrizeKind::Thunderbolt);
        session.givePrize(PrizeKind::Meteor);
        const PlayResult result = autoplay(session, 14);
        std::vector<std::string> lines;
        for (const GameEvent& event : result.events) {
            lines.push_back(describe(event));
        }
        return lines;
    };
    const std::vector<std::string> a = play(), b = play();
    check(a == b && a.size() > 20, "determinism: two games with the same weapons and strikes play the same");
    check(std::ranges::any_of(a, [](const std::string& line) { return line.find("ShotFired") != std::string::npos; }) &&
              std::ranges::any_of(a, [](const std::string& line) { return line.find("BoltStruck") != std::string::npos; }) &&
              std::ranges::any_of(a, [](const std::string& line) { return line.find("MeteorImpact") != std::string::npos; }),
          "determinism: and the game had shots, bolts and meteors in it");
}

void testAutopilot() {
    const Autopilot pilot;
    {  // a brick in line with the barrels: it clicks
        Game g(levelOf({board(), brick(8 - 2.1556f, 10, "One.jpg")}));
        g.play();
        g.give(PrizeKind::Gun);
        check(pilot.decide(g.session).launch, "autopilot: with a gun and a brick ahead of a barrel it fires");
    }
    {  // nothing ahead, or a wall in the way, or no weapon: it does not
        Game empty(levelOf({board()}));
        empty.play();
        empty.give(PrizeKind::Gun);
        check(!pilot.decide(empty.session).launch, "autopilot: with a gun and only the far wall ahead it holds its fire");
        Game blocked(levelOf({board(), brick(8 - 2.1556f, 6, "Steel.jpg"), brick(8 - 2.1556f, 12, "One.jpg")}));
        blocked.play();
        blocked.give(PrizeKind::Gun);
        check(!pilot.decide(blocked.session).launch, "autopilot: with a wall of steel in front of the brick it holds its fire");
        Game unarmed(levelOf({board(), brick(8, 10, "One.jpg")}));
        unarmed.play();
        check(!pilot.decide(unarmed.session).launch, "autopilot: with no weapon it does not click");
    }
    {  // each weapon's own line: the laser and the cannon down the middle
        for (const PrizeKind weapon : {PrizeKind::Cannon, PrizeKind::Bazooka, PrizeKind::Laser}) {
            Game g(levelOf({board(), brick(8, 10, "One.jpg")}));
            g.play();
            g.give(weapon);
            check(pilot.decide(g.session).launch, "autopilot: it fires the " + std::string(prizeInfo(weapon).stem) + " at a brick in the middle of its line");
        }
    }
    {  // and plays: a gun's shots are used, up to the interval
        Game g(levelOf({board(), brick(0, 10, "Four.jpg", kStatic, {20, 1.5f, 0.5f})}));  // wherever the bat goes there is something ahead
        g.play();
        g.give(PrizeKind::Gun);
        for (int i = 0; i < 60; ++i) {
            g.session.update(1.0 / 60, pilot.decide(g.session));
        }
        g.flush();
        check(g.session.weapons().stats().shots >= 3 && g.session.weapons().stats().shots <= 5,
              "autopilot: held to the shot interval it fires four or five shots in a second (" + std::to_string(g.session.weapons().stats().shots) + ")");
    }
}

}  // namespace

int main() {
    testTopEdge();
    testKindsOfBody();
    testNoWeaponNoShot();
    testClickPriority();
    testGunShots();
    testShotInterval();
    testWeaponsRunOut();
    testWeaponReplacement();
    testNothingFiresWhenOver();
    testSpawnsAndVelocities();
    testProjectileGone();
    testDamageTable();
    testKernel();
    testRocket();
    testHitsThatDamageNothing();
    testLaser();
    testSpecialObjects();
    testPrizeDrops();
    testThunderbolt();
    testMeteors();
    testStrikeDamage();
    testBulletsPush();
    testStrikesStopWhenOver();
    testDeterminism();
    testAutopilot();
    if (failures > 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::puts("all weapon tests passed");
    return 0;
}
