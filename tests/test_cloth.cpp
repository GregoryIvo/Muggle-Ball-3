// Sails (docs/GAMEPLAY.md section 20) on made-up levels: what planPhysics() makes of a cloth object and the joints that hold its
// corners, the sail running in a PhysicsScene (pinned to what it hangs from, blown by the wind, let go when the holder is destroyed or
// hit), the rules (what hurts it, what it is worth) in a GameSession, and the mesh of its surface. Needs neither a GPU nor game
// data. The same sails in the real levels: mb_cloth_smoke.

#include "mb/game/ClothDraw.h"
#include "mb/game/Gameplay.h"
#include "mb/game/PhysicsScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
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

std::string fmt(const char* format, float a, float b = 0, float c = 0) {
    char text[200];
    std::snprintf(text, sizeof text, format, static_cast<double>(a), static_cast<double>(b), static_cast<double>(c));
    return text;
}

// ---- Materials and levels ----------------------------------------------------------------------------------------

// Sail: the real Clothmat's numbers (docs/GAMEPLAY.md section 20); Yard.jpg: what a sail hangs from; Shark.jpg: a brick to stand about.
const MaterialLibrary& materials() {
    static const MaterialLibrary library = MaterialLibrary::parse(R"({"version": 1, "materials": [
      {"name": "BallMaterial", "density": 1, "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 0.999},
       "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "BoardMaterial", "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 1.0},
       "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "Sail", "density": 0.01, "textures": ["Parus"],
       "physics": {"staticFriction": 0.9, "dynamicFriction": 0.9, "restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 60, "exploseType": 3, "exploseDelay": 10},
       "sounds": {"hit": "sea_cloth_hit", "dead": "sea_cloth_dead"}},
      {"name": "Yard.jpg", "physics": {"restitution": 0.3}, "brick": {"type": 2, "energy": 1, "score": 5}},
      {"name": "Shark.jpg", "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 91, "exploseType": 3, "exploseDelay": 250}},
      {"name": "Dinamit.jpg", "density": 10, "physics": {"restitution": 0.5},
       "brick": {"type": 0, "energy": 1, "score": 120, "exploseType": 8, "exploseDelay": 100, "explosePower": 1.0}}
    ]})");
    return library;
}

constexpr std::uint32_t kDynamic = 0x100, kStatic = 0x17E;  // NxBodyFlag, with the visualization bit
constexpr std::uint32_t kNoGravity = 1;                     // NX_BF_DISABLE_GRAVITY: a yard that hangs where it is
constexpr float kFltMax = std::numeric_limits<float>::max();

Shape box(Vec3 half, Vec3 at = {}, std::uint16_t group = 2) {
    Shape s;
    s.type = ShapeType::Box;
    s.halfExtents = half;
    s.localPose.position = at;
    s.group = group;
    s.shapeFlags = 8;
    return s;
}

Shape sphere(float radius, Vec3 at) {
    Shape s;
    s.type = ShapeType::Sphere;
    s.radius = radius;
    s.localPose.position = at;
    s.group = 2;
    s.shapeFlags = 8;
    return s;
}

Actor actor(ObjectType type, std::uint32_t id, Vec3 at, std::uint32_t flags, float mass, std::vector<Shape> shapes,
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
    object.id = id;
    object.material = material;
    a.object = object;
    return a;
}

Actor board() {
    return actor(ObjectType::Board, 1, {0, 0, 0}, 0xFF, 1000,
                 {box({30, 0.5f, 30}, {0, -0.5f, 0}, 4), box({1, 2, 30}, {-21.4f, 2, 0}, 4), box({1, 2, 30}, {21.4f, 2, 0}, 4),
                  box({30, 2, 1}, {0, 2, 23.6f}, 4), box({30, 2, 1}, {0, 2, -21.3f}, 4)},
                 "BoardMaterial");
}

// Which way a sail faces: the player (it stands in a plane of constant z, as the levels' do), the sky (it lies flat) or the side (a plane of
// constant x, which a wind along x is square to).
enum class Facing { Player, Up, Side };

// The sail the levels have: a grid of `width` by `height` particles `gap` apart in the plane z = `z`, its first particle at x =
// `left` and its lowest row at height `base`, as a cloth object with id `id` (its own actor is the first particle) and its segments
// nested in it, each a sphere of 0.5 weighing what the real ones do.
Actor sail(std::uint32_t id, int width, int height, float gap, float left, float base, float z, Vec3 wind = {},
           const std::string& material = "Sail", Facing facing = Facing::Player) {
    const float mass = 0.0052359877f;
    const auto at = [&](int k) {
        const float across = gap * static_cast<float>(k % width), along = gap * static_cast<float>(k / width);
        switch (facing) {
            case Facing::Up: return Vec3{left + across, base, z + along};  // lying in the plane y = base, its rows going away from the player
            case Facing::Side: return Vec3{left, base + along, z + across};  // standing in the plane x = left
            default: return Vec3{left + across, base + along, z};
        }
    };
    Actor lead = actor(ObjectType::Cloth, id, {0, 0, 0}, kDynamic, mass, {sphere(0.5f, at(0))}, material);
    lead.object->cloth = {width, height, wind};
    for (int k = 1; k < width * height; ++k) {
        Actor segment;
        Body body;
        body.mass = mass;
        body.massSpaceInertia = {mass / 6, mass / 6, mass / 6};
        body.flags = kDynamic;
        segment.body = body;
        segment.shapes = {sphere(0.5f, at(k))};
        lead.object->clothSegments.push_back(segment);
    }
    return lead;
}

Vec3 particleAt(const Actor& sail, int k) {
    return k == 0 ? sail.shapes[0].localPose.position : sail.object->clothSegments[static_cast<std::size_t>(k - 1)].shapes[0].localPose.position;
}

// A ball joint of the level: the sail's particle (id + k) to what holds it, anchored at the particle.
Joint pin(const Actor& sail, int k, std::uint32_t holderId, Vec3 holderAt, float breakForce = 100.0f) {
    Joint j;
    j.type = NxJointType::Spherical;
    j.actorA = sail.object->id + static_cast<std::uint32_t>(k);
    j.actorB = holderId;
    j.localAnchor[0] = {};
    j.localAnchor[1] = particleAt(sail, k) - holderAt;
    j.localAxis[0] = j.localAxis[1] = {0, 0, 1};
    j.localNormal[0] = j.localNormal[1] = {1, 0, 0};
    j.maxForce = j.maxTorque = breakForce;
    return j;
}

// The pieces of one sail hung from a yard, as a ship has them: the yard (a long box, no gravity: it hangs where it is) and a sail
// of `width` by `height` particles, pinned at the corners of its top row (and, with `bottom`, of its lowest). Actors: board 0, yard 1,
// sail 2 (its own actor), its segments 3 ...
struct Rig {
    Level level;
    Actor sailActor;
    Vec3 yardAt;
};

Rig rig(int width = 3, int height = 3, float gap = 2.0f, float base = 4.0f, float z = 10.0f, Vec3 wind = {}, bool bottom = false,
        std::uint32_t yardFlags = kDynamic | kNoGravity, float x = 0, const std::string& yardMaterial = "Yard.jpg") {
    Rig r;
    const float left = x - 0.5f * gap * static_cast<float>(width - 1);
    r.yardAt = {x, base + gap * static_cast<float>(height - 1), z};
    Actor yard = actor(ObjectType::Brick, 10, r.yardAt, yardFlags, 100, {box({0.5f * gap * static_cast<float>(width) + 0.5f, 0.15f, 0.15f})}, yardMaterial);
    r.sailActor = sail(20, width, height, gap, left, base, z, wind);
    std::vector<Joint> joints;
    const int top = (height - 1) * width;
    joints.push_back(pin(r.sailActor, top, 10, r.yardAt));
    joints.push_back(pin(r.sailActor, top + width - 1, 10, r.yardAt));
    if (bottom) {
        joints.push_back(pin(r.sailActor, 0, 10, r.yardAt));
        joints.push_back(pin(r.sailActor, width - 1, 10, r.yardAt));
    }
    r.level.gravity = {0, -9.8f, 0};
    r.level.actors = {board(), yard, r.sailActor};
    r.level.joints = std::move(joints);
    return r;
}

PhysicsSettings singleThreaded() {
    PhysicsSettings s;
    s.threads = 0;
    return s;
}

void run(PhysicsScene& scene, float seconds) {
    for (int i = 0, steps = static_cast<int>(std::lround(seconds * 60)); i < steps; ++i) {
        scene.step(1.0f / 60);
    }
}

bool finite(const std::vector<Vec3>& points) {
    return std::ranges::all_of(points, [](Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); });
}

constexpr ActorIndex kYard = 1, kSail = 2;

// ---- Plan -------------------------------------------------------------------------------------------------------

void testPlan() {
    const Rig r = rig(4, 3, 2.0f, 4.0f, 10.0f, {6, 0, -8}, true);
    const PhysicsPlan plan = planPhysics(r.level, materials());
    check(plan.cloths.size() == 1 && plan.unsupportedCloths == 0, "plan: one sail, all of it understood");
    if (plan.cloths.size() != 1) {
        return;
    }
    const ClothPlan& cloth = plan.cloths[0];
    check(cloth.lead == kSail && cloth.width == 4 && cloth.height == 3 && cloth.actors.size() == 12, "plan: the lead, the grid and the actors");
    check(cloth.actors.front() == kSail && cloth.actors.back() == kSail + 11, "plan: the lead's segments follow it in order");
    check(near(cloth.wind, {6, 0, -8}), "plan: the wind is the level's");
    check(near(cloth.particleRadius, 0.5f) && near(cloth.particleMass, 0.0052359877f, 1e-6f), "plan: a particle is a segment's sphere and body");
    check(near(cloth.friction, 0.9f) && near(cloth.restitution, 0.5f), "plan: friction and bounce are the material's");
    bool placed = cloth.rest.size() == 12;
    for (int k = 0; placed && k < 12; ++k) {
        placed = near(cloth.rest[static_cast<std::size_t>(k)], particleAt(r.sailActor, k));
    }
    check(placed, "plan: the particles are where the level saved them (the sphere's place in the actor's pose)");

    check(cloth.pins.size() == 4, "plan: the four corners are held");
    bool pinsRight = cloth.pins.size() == 4;
    for (const ClothPin& p : cloth.pins) {
        pinsRight = pinsRight && p.holder == kYard && (p.particle == 0 || p.particle == 3 || p.particle == 8 || p.particle == 11) &&
                    near(p.local, cloth.rest[static_cast<std::size_t>(p.particle)] - r.yardAt) && near(p.breakForce, 100);
    }
    check(pinsRight, "plan: each pin names its particle, the yard, where it is tied in the yard's frame and the joint's strength");
    check(plan.joints.size() == 4 && std::ranges::all_of(plan.joints, [](const JointPlan& j) { return j.toCloth; }),
          "plan: the joints stay in the plan, marked as the sail's");

    // The lead's body measures the whole sail.
    const BodyDesc& body = plan.actors[kSail].body;
    check(body.shapes.size() == 12, "plan: the lead's body has a sphere for each particle");
    const Bounds box = boundsOf(body, plan.actors[kSail].pose);
    check(near(box.low, {-3.5f, 3.5f, 9.5f}) && near(box.high, {3.5f, 8.5f, 10.5f}),
          "plan: so the actor's bounds are the sail's, a particle's radius out");
}

void testPlanPinsToTheWorldAndSkipsWhatItCannotUse() {
    Rig r = rig();
    Joint toWorld = pin(r.sailActor, 6, 10, r.yardAt);
    toWorld.actorB = 0;                                // the world
    toWorld.localAnchor[1] = particleAt(r.sailActor, 6);  // in world space
    r.level.joints.push_back(toWorld);
    const PhysicsPlan plan = planPhysics(r.level, materials());
    check(plan.cloths.size() == 1 && plan.cloths[0].pins.size() == 3, "plan: a joint to the world holds a particle too");
    if (plan.cloths.size() == 1 && plan.cloths[0].pins.size() == 3) {
        const ClothPin& held = plan.cloths[0].pins[2];
        check(held.particle == 6 && held.holder == kNoActor && near(held.local, particleAt(r.sailActor, 6)), "plan: the world's pin is in world space");
    }

    // A cloth whose segments are not its grid is left as it was: kinematic spheres, no sail.
    Rig broken = rig();
    broken.level.actors[kSail].object->clothSegments.pop_back();
    const PhysicsPlan bad = planPhysics(broken.level, materials());
    check(bad.cloths.empty() && bad.unsupportedCloths == 1, "plan: a cloth that is not its grid is counted and left out");
    check(bad.actors[kSail].motion == MotionType::Kinematic && bad.actors[kSail].body.shapes.size() == 1, "plan: and its actors stay as they were");
    PhysicsScene scene(broken.level, materials(), {}, singleThreaded());
    check(scene.cloths().size() == 0 && scene.body(kSail) != 0, "plan: the scene makes them bodies, as before");

    Rig none = rig();
    none.level.actors[kSail].object->cloth = {};
    check(planPhysics(none.level, materials()).unsupportedCloths == 1, "plan: a cloth with no grid is not a sail either");
}

// ---- The scene --------------------------------------------------------------------------------------------------

void testSceneBodies() {
    const Rig r = rig(3, 3, 2.0f, 4.0f, 10.0f, {}, false);
    PhysicsScene scene(r.level, materials(), {}, singleThreaded());
    const PhysicsScene::Counts& counts = scene.counts();
    check(counts.cloths == 1 && counts.clothParticles == 9 && counts.clothJoints == 2 && counts.joints == 0,
          "scene: a sail is one body, its actors are particles, its joints are pins and no joints of the world");
    check(scene.body(kSail) != 0 && scene.world().userData(scene.body(kSail)) == kSail, "scene: the lead has the cloth's body, which names it");
    check(scene.body(kSail + 1) == 0 && scene.body(kSail + 8) == 0, "scene: the segments have none");
    check(scene.cloths().alive(0) && scene.cloths().body(0) == scene.body(kSail) && scene.cloths().clothOf(kSail + 4) == 0 &&
              scene.cloths().clothOf(kYard) == -1,
          "scene: the set knows which actors are a sail's");
    check(near(scene.actorPose(kSail).translationPart(), {0, 0, 0}), "scene: the lead's pose is where it was saved, not the cloth body's");
    check(scene.cloths().positions(0).size() == 9, "scene: a position a particle");
    check(scene.world().motionType(scene.body(kYard)) == MotionType::Dynamic, "scene: the yard is a body as ever");
    run(scene, 0.5f);

    const Bounds box = scene.bounds(kSail);
    check(near(box.low, {-2.5f, 3.5f, 9.5f}, 0.3f) && near(box.high, {2.5f, 8.5f, 10.5f}, 0.3f), "scene: the sail's bounds are its particles' and their radius");
    const Vec3 centre = scene.centreOf(kSail);
    check(near(centre, {0, 6, 10}, 0.5f), fmt("scene: its centre is the middle of the particles (%.2f %.2f %.2f)", centre.x, centre.y, centre.z));
    check(near(scene.centreOf(kYard), r.yardAt, 0.05f), "scene: a rigid body's centre is as before");
    check(near(scene.bounds(kYard).low, {-3.5f, r.yardAt.y - 0.15f, r.yardAt.z - 0.15f}, 0.02f), "scene: and its bounds");
}

void testPinsHoldAndFollowTheHolder() {
    const Rig r = rig(3, 3, 2.0f, 4.0f, 10.0f, {}, true);
    PhysicsScene scene(r.level, materials(), {}, singleThreaded());
    run(scene, 1);
    const ClothSet& cloths = scene.cloths();
    const ClothPlan& cloth = cloths.plan(0);
    const auto held = [&](const Vec3 yard) {
        bool all = true;
        const std::vector<Vec3> now = cloths.positions(0);
        for (const ClothPin& p : cloth.pins) {
            all = all && near(now[static_cast<std::size_t>(p.particle)], yard + p.local, 0.02f);
        }
        return all;
    };
    check(cloths.pinsHolding(0) == 4 && held(r.yardAt), "pins: the corners stay where the yard has them");

    // The ship is carried 3 units sideways and turned: the corners go with it.
    Mat4 moved = Mat4::rotationY(0.4f);
    moved.m[3][0] = 3;
    moved.m[3][1] = r.yardAt.y;
    moved.m[3][2] = r.yardAt.z;
    scene.world().setPose(scene.body(kYard), moved);
    run(scene, 0.2f);
    bool follows = true;
    const std::vector<Vec3> now = cloths.positions(0);
    for (const ClothPin& p : cloth.pins) {
        follows = follows && near(now[static_cast<std::size_t>(p.particle)], transformPoint(p.local, moved), 0.05f);
    }
    check(follows, "pins: and turn with it, however the yard stands now");
    check(finite(now) && length(now[4] - Vec3{3, 6, 10}) < 5, "pins: the rest of the sail comes after it");
}

void testWindBlowsTheSail() {
    const Rig windy = rig(3, 4, 2.0f, 3.0f, 10.0f, {0, 0, 10});
    const Rig calm = rig(3, 4, 2.0f, 3.0f, 10.0f, {});
    PhysicsOptions taut;
    taut.cloth.slack = 0;  // (a sail that starts flat and compressed buckles whichever way the least push says)
    PhysicsScene a(windy.level, materials(), taut, singleThreaded()), b(calm.level, materials(), taut, singleThreaded());
    run(a, 4);
    run(b, 4);
    const auto bottom = static_cast<std::size_t>(1);  // the middle of the lowest row
    const float blown = a.cloths().positions(0)[bottom].z, still = b.cloths().positions(0)[bottom].z;
    check(blown > 10.5f && std::fabs(still - 10) < 0.3f,
          fmt("wind: the data's wind blows the sail along it (z %.2f, against %.2f without)", blown, still));
}

void testASailSleeps() {
    const Rig r = rig(3, 3, 2.0f, 4.0f, 10.0f, {0, 0, 10}, true);
    PhysicsScene scene(r.level, materials(), {}, singleThreaded());
    run(scene, 0.5f);
    check(scene.world().isAwake(scene.body(kSail)), "sleep: a sail that is filling out is awake");
    run(scene, 12);
    check(!scene.world().isAwake(scene.body(kSail)), "sleep: one the wind has settled costs nothing");
    scene.world().setPose(scene.body(kYard), Mat4::translation({1, r.yardAt.y, r.yardAt.z}));
    run(scene, 0.1f);
    check(scene.world().isAwake(scene.body(kSail)), "sleep: and wakes when its yard moves");
}

void testHolderGoneLetsTheSailFall() {
    const Rig r = rig(3, 3, 2.0f, 2.0f, 10.0f, {}, true);
    PhysicsScene scene(r.level, materials(), {}, singleThreaded());
    run(scene, 0.5f);
    const ClothSet& cloths = scene.cloths();
    check(cloths.pinsHolding(0) == 4, "holder: four pins hold it");
    scene.removeActor(kYard);
    check(cloths.pinsHolding(0) == 0 && scene.body(kYard) == 0, "holder: taking the yard out of the scene lets every pin go");
    run(scene, 4);
    // (Standing on its lowest row is as far as a flat sail with no wind goes: it falls like a card and stays up like one.)
    const Vec3 centre = cloths.centre(0);
    float lowest = 1e9f;
    for (const Vec3 p : cloths.positions(0)) {
        lowest = std::min(lowest, p.y);
    }
    check(centre.y < 3.0f && lowest < 0.7f && finite(cloths.positions(0)),
          fmt("holder: the sail falls to the floor (centre at y %.2f, lowest particle %.2f)", centre.y, lowest));
    scene.removeActor(kYard);  // twice is nothing
}

void testAHardHitOnTheHolderBreaksItsPins() {
    for (const float strength : {100.0f, kFltMax}) {
        Rig r = rig(3, 3, 2.0f, 4.0f, 10.0f, {}, false);
        for (Joint& j : r.level.joints) {
            j.maxForce = j.maxTorque = strength;
        }
        PhysicsScene scene(r.level, materials(), {}, singleThreaded());
        run(scene, 0.5f);
        scene.launchBall({0, r.yardAt.y, r.yardAt.z - 6}, {0, 0, 25});  // into the middle of the yard
        run(scene, 1);
        const std::size_t holding = scene.cloths().pinsHolding(0);
        check(strength < 1e9f ? holding == 0 : holding == 2,
              fmt("hit: a ball on the yard lets go of pins of 100 and not of unbreakable ones (%.0f left)", static_cast<float>(holding)));
    }
}

void testASailRemovedLeavesTheWorld() {
    const Rig r = rig(3, 3, 2.0f, 4.0f, 10.0f, {0, 0, 10}, false);
    PhysicsScene scene(r.level, materials(), {}, singleThreaded());
    run(scene, 0.5f);
    const std::size_t bodies = scene.world().bodyCount();
    scene.removeActor(kSail + 3);  // a segment: nothing to take out
    check(scene.cloths().alive(0) && scene.world().bodyCount() == bodies, "removed: a segment is not a body");
    scene.removeActor(kSail);
    check(!scene.cloths().alive(0) && scene.body(kSail) == 0 && scene.world().bodyCount() == bodies - 1 && scene.cloths().positions(0).empty(),
          "removed: the lead takes the whole sail out of the world");
    check(near(scene.bounds(kSail).low, {-2.5f, 3.5f, 9.5f}, 0.01f), "removed: its bounds are where it was saved");
    run(scene, 1);
    scene.removeActor(kSail);  // twice is nothing
}

void testASailGoesThroughItsOwnShip() {
    // A boom welded to the yard hangs through the sail's middle row; a plank of its own, joined to nothing, lies across its lowest.
    Rig r = rig(3, 3, 2.0f, 2.0f, 10.0f, {}, true);
    Actor boom = actor(ObjectType::Brick, 11, {0, 4.0f, 10.0f}, kDynamic | kNoGravity, 50, {box({4, 0.2f, 0.2f})}, "Yard.jpg");
    Actor plank = actor(ObjectType::Brick, 12, {0, 2.0f, 10.0f}, kStatic, 50, {box({4, 0.2f, 0.2f})}, "Yard.jpg");
    Joint weld;
    weld.type = NxJointType::Fixed;
    weld.actorA = 10;
    weld.actorB = 11;
    weld.localAnchor[0] = {0, -2.0f, 0};
    weld.localAnchor[1] = {0, 0, 0};
    weld.localAxis[0] = weld.localAxis[1] = {0, 1, 0};
    weld.localNormal[0] = weld.localNormal[1] = {1, 0, 0};
    weld.maxForce = weld.maxTorque = 1000;
    r.level.actors.push_back(boom);
    r.level.actors.push_back(plank);
    r.level.joints.push_back(weld);
    PhysicsScene scene(r.level, materials(), {}, singleThreaded());
    const ActorIndex boomIndex = kSail + 9, plankIndex = kSail + 10;  // after the sail's own actor and its 8 segments
    bool touchedBoom = false, touchedPlank = false;
    for (int s = 0; s < 120; ++s) {
        scene.step(1.0f / 60);
        for (const ContactEvent& e : scene.contacts()) {
            const bool sailInvolved = e.userDataA == kSail || e.userDataB == kSail;
            const std::uint64_t other = e.userDataA == kSail ? e.userDataB : e.userDataA;
            touchedBoom = touchedBoom || (sailInvolved && other == boomIndex);
            touchedPlank = touchedPlank || (sailInvolved && other == plankIndex);
        }
    }
    check(!touchedBoom, "ship: a sail does not touch what is joined to what it hangs from");
    check(touchedPlank, "ship: and touches what is not");
}

void testASailThatTouchesIsReported() {
    Rig r = rig(3, 3, 2.0f, 4.0f, 10.0f, {}, true);
    PhysicsScene scene(r.level, materials(), {}, singleThreaded());
    run(scene, 0.5f);
    const Vec3 middle = scene.cloths().positions(0)[4];
    scene.launchBall(middle - Vec3{0, 0, 2}, {0, 0, 12});
    bool touched = false;
    float speed = 0;
    for (int s = 0; s < 60; ++s) {
        scene.step(1.0f / 60);
        for (const ContactEvent& e : scene.contacts()) {
            if (e.userDataA == kSail || e.userDataB == kSail) {
                touched = true;
                speed = std::max(speed, e.normalSpeed);
            }
        }
    }
    check(touched && speed > 5, fmt("touch: a ball that reaches the sail is reported by the lead actor (closing at %.1f)", speed));
    check(scene.touchedByBalls().contains(kSail), "touch: and the scene notes the sail among what the balls touched");
}

// ---- The rules --------------------------------------------------------------------------------------------------

SessionOptions options() {
    SessionOptions o;
    o.levelEnd = TargetRule::Bricks;
    o.ignoreUnreachable = false;
    o.physicsSettings.threads = 0;
    o.rules.prizes.drops = false;
    return o;
}

struct Game {
    GameSession session;
    std::vector<GameEvent> log;

    explicit Game(const Level& level, const SessionOptions& o = options()) : session(level, materials(), o) {}

    void run(double seconds) {
        for (int i = 0, steps = static_cast<int>(std::lround(seconds * 60)); i < steps; ++i) {
            session.update(1.0 / 60, {});
            for (const GameEvent& event : session.takeEvents()) {
                log.push_back(event);
            }
        }
    }
    const GameEvent* find(EventType type) const {
        const auto it = std::ranges::find(log, type, &GameEvent::type);
        return it == log.end() ? nullptr : &*it;
    }
    int count(EventType type) const { return static_cast<int>(std::ranges::count(log, type, &GameEvent::type)); }
};

// A sail low enough for the ball: standing on the floor in the plane z = 10, its lowest row at the ball's height, held by a static yard.
Level lowSail(bool withShark = false) {
    Rig r = rig(3, 3, 2.0f, 0.6f, 10.0f, {}, true, kStatic);
    if (withShark) {
        Actor shark = actor(ObjectType::Brick, 30, {10, 1, 5}, kStatic, 50, {box({1, 1, 1})}, "Shark.jpg");
        r.level.actors.push_back(shark);
    }
    return r.level;
}

void testWhatHurtsASail() {
    const GameMaterial& sail = *materials().find("Sail");
    check(canBeDamaged(ObjectType::Cloth, &sail) && !canBeDamaged(ObjectType::Cloth, nullptr), "rules: a sail with a brick material can be hurt");
    check(!isTarget(TargetRule::Bricks, ObjectType::Cloth, &sail) && !isTarget(TargetRule::BrickType, ObjectType::Cloth, &sail),
          "rules: but it is no target: no level waits for it");

    Game g(lowSail(true));
    const GameSession& s = g.session;
    check(s.actor(kSail).damageable && !s.actor(kSail + 1).damageable && !s.actor(kSail + 8).damageable,
          "rules: the lead is the sail; its segments are not objects of their own");
    check(!s.actor(kSail).target && s.targetsLeft(TargetRule::Bricks) == 1 && s.targetsLeft(TargetRule::BrickType) == 1,
          "rules: only the shark is left to destroy");
    check(!g.session.hit(kSail + 3, {0, 0, 0}), "rules: a hit on a particle is nothing: only the sail has energy");
}

void testTheBallHurtsASail() {
    Game g(lowSail(true));
    // Level begins: the ball is held; add one in flight at the sail's middle column (x = 0), along +z.
    check(g.session.addBall({0, 0, 3}, {0, 0, 1}) >= 0, "ball: a ball is in play");
    g.run(2);
    const GameEvent* hit = g.find(EventType::BrickHit);
    const GameEvent* dead = g.find(EventType::BrickDestroyed);
    check(hit != nullptr && hit->actor == kSail && hit->ball >= 0 && hit->material != nullptr && hit->material->name == "Sail" && hit->energy == 0,
          "ball: it hits the sail, by the lead's id, which has one energy");
    check(dead != nullptr && dead->actor == kSail && dead->points == 60 && g.session.score() == 60,
          "ball: and destroys it for its material's 60");
    if (dead != nullptr) {
        check(dead->position.z > 9 && dead->position.z < 11 && dead->position.y > 0.5f && dead->position.y < 3, "ball: the blast is at the sail's middle");
        check(dead->time - hit->time > 0.005 && dead->time - hit->time < 0.05, "ball: 10 ms later, as the material's exploseDelay says");
    }
    check(g.session.actor(kSail).removed && !g.session.scene().cloths().alive(0) && g.session.scene().body(kSail) == 0,
          "ball: a destroyed sail is out of the scene");
    check(g.session.targetsLeft(TargetRule::Bricks) == 1, "ball: and the level still waits for the shark");
    check(g.count(EventType::BrickDestroyed) == 1, "ball: once");
}

void testASailThatFallsOffTheBoardIsTakenOut() {
    // Hung from a yard off the board's edge (its floor ends at x = 30), the sail falls for ever when the yard is destroyed.
    Game g(rig(3, 3, 2.0f, 4.0f, 10.0f, {}, true, kStatic, 45, "Shark.jpg").level);
    check(g.session.hit(kYard, {45, 8, 10}), "off the board: the yard is hit");
    g.run(8);
    const GameEvent* fell = g.find(EventType::ActorFell);
    check(fell != nullptr && fell->actor == kSail && fell->points == 0 && fell->position.y < -4,
          "off the board: the sail that has fallen below the board is reported as one that left it, for nothing");
    check(g.session.actor(kSail).removed && !g.session.scene().cloths().alive(0) && g.session.score() == 91 && g.count(EventType::ActorFell) == 1,
          "off the board: and is taken out of the scene, once, the score the yard's (91)");
}

void testAWeaponAndABlastHurtASail() {
    Game g(lowSail());
    check(g.session.hit(kSail, {0, 1, 10}), "weapon: a hit destroys a sail of one energy");
    check(!g.session.hit(kSail, {0, 1, 10}), "weapon: a second hit on the same one does nothing");
    g.run(0.2);
    check(g.session.score() == 60 && g.session.actor(kSail).removed, "weapon: it scores 60 and is gone");

    // Dynamite: a charge 4 units in front of the sail's middle, radius 5, takes it; one 12 units off does not.
    for (const float z : {6.0f, -2.0f}) {
        Level level = lowSail();
        Actor charge = actor(ObjectType::Dynamite, 40, {0, 0, z}, kStatic, 85, {box({0.9f, 1.3f, 0.9f}, {0, 1.3f, 0})}, "Dinamit.jpg");
        charge.object->param = 0x40A00000;  // 5.0
        charge.object->target = "explosebig";
        level.actors.push_back(charge);
        Game b(level);
        const ActorIndex chargeIndex = kSail + 9;  // after the sail's own actor and its 8 segments
        check(b.session.hit(chargeIndex, {0, 0, z}), "blast: the charge is hit");
        b.run(1);
        const bool taken = b.session.actor(kSail).removed;
        check(taken == (z > 0), std::string("blast: a charge ") + (z > 0 ? "4 units from the sail takes it" : "12 units away leaves it") +
                                    ", by the distance to its particles, not to the lead actor's");
    }
}

// A blast takes a sail by where its particles are, not where the level saved it: the sail's yard is thrown up the sky (a saved velocity
// with some damping, so that it stops about 9 units higher), and the charge goes off where the sail came to rest, 4 in front of it.
void testABlastTakesASailWhereItIsNow() {
    const auto level = [](bool withCharge, Vec3 chargeAt) {
        Rig r = rig(3, 3, 1.0f, 1.0f, 2.0f, {}, true);
        r.level.actors[kYard].body->linearVelocity = {0, 40, 0};
        r.level.actors[kYard].body->linearDamping = 4;
        if (withCharge) {
            Actor charge = actor(ObjectType::Dynamite, 40, chargeAt, kStatic, 85, {box({0.9f, 1.3f, 0.9f}, {0, 1.3f, 0})}, "Dinamit.jpg");
            charge.object->param = 0x40A00000;  // 5.0
            charge.object->target = "explosebig";
            r.level.actors.push_back(charge);
        }
        return r.level;
    };
    SessionOptions o = options();
    o.physics.start = StartState::SavedVelocities;
    Game probe(level(false, {}), o);
    probe.run(3);
    const Vec3 there = probe.session.centreOf(kSail);
    check(there.y > 9.0f, fmt("carried: the sail went up with its yard (to y %.1f from 2)", there.y));
    for (const bool where : {true, false}) {
        // Dynamite's centre is its pose; the charge stands 4 in front of the sail where it is now, or where it was saved
        const Vec3 at = where ? Vec3{there.x, there.y, there.z - 4} : Vec3{0, 2.0f, -2};
        Game g(level(true, at), o);
        g.run(3);
        const ActorIndex charge = kSail + 9;
        check(g.session.hit(charge, at), "carried: the charge is hit");
        g.run(1);
        check(g.session.actor(kSail).removed == where,
              std::string("carried: a charge ") + (where ? "4 units from where the sail is takes it, though it is 9 from where it was saved" : "4 units from where the sail was saved leaves it, 7 from where it is"));
    }
}

// A body that lies on a sail is not held up by it: the rays that look for what is under a sleeping body hit the sail's faces.
void testASailHoldsNothingUp() {
    const auto level = [](bool slab) {
        Level l = rig(3, 3, 2.0f, 0.0f, 0.0f).level;  // (the rig's board; its yard and sail are replaced)
        l.actors.resize(1);
        l.joints.clear();
        Actor flat = sail(20, 3, 3, 2.0f, -2.0f, 2.0f, 8.0f, {}, "Sail", Facing::Up);
        for (const int k : {0, 2, 6, 8}) {  // the four corners are tied to the world
            Joint j;
            j.type = NxJointType::Spherical;
            j.actorA = 20 + static_cast<std::uint32_t>(k);
            j.actorB = 0;
            j.localAnchor[1] = particleAt(flat, k);
            j.localAxis[0] = j.localAxis[1] = {0, 0, 1};
            j.localNormal[0] = j.localNormal[1] = {1, 0, 0};
            j.maxForce = j.maxTorque = kFltMax;
            l.joints.push_back(j);
        }
        l.actors.push_back(flat);
        // A block 0.06 above the sail's plane (y 2) over its middle, and one 0.06 above a slab of its own
        l.actors.push_back(actor(ObjectType::Brick, 30, {0, 2.56f, 10.0f}, kDynamic, 10, {box({0.5f, 0.5f, 0.5f})}, "Shark.jpg"));
        l.actors.push_back(actor(ObjectType::Brick, 31, {10, 5.56f, 10.0f}, kDynamic, 10, {box({0.5f, 0.5f, 0.5f})}, "Shark.jpg"));
        if (slab) {
            l.actors.push_back(actor(ObjectType::Brick, 32, {10, 4.5f, 10.0f}, kStatic, 100, {box({2, 0.5f, 2})}, "Yard.jpg"));
        }
        return l;
    };
    const ActorIndex onSail = 10, onSlab = 11;  // after the board (0) and the sail's 9 actors (1 to 9)
    PhysicsScene scene(level(true), materials(), {}, singleThreaded());
    check(scene.cloths().size() == 1 && scene.body(onSail) != 0 && scene.body(onSlab) != 0, "support: a flat sail with a block above it");
    check(!scene.supported(onSail), "support: a block that lies 0.06 over a sail is not held up by it");
    check(scene.supported(onSlab), "support: and one that lies 0.06 over a slab is");
}

// A ship that closes on a free sail does not hit a wall: the sail is a few grams of cloth, so the hit has next to no force, and the joints that
// hold the ship together stay. (Its mass was the kinematic actor's, which every other body takes for immovable.)
void testAShipThatTouchesASailBreaksNothing() {
    Rig r = rig(3, 3, 2.0f, 4.0f, 10.0f, {}, false);
    r.level.joints.clear();
    r.level.actors.resize(2);  // the board and the yard: the sail is rebuilt below, hung from the world
    Actor free = sail(20, 3, 3, 2.0f, -2.0f, 4.0f, 10.0f);
    for (const int k : {6, 8}) {
        Joint j;
        j.type = NxJointType::Spherical;
        j.actorA = 20 + static_cast<std::uint32_t>(k);
        j.actorB = 0;
        j.localAnchor[1] = particleAt(free, k);
        j.localAxis[0] = j.localAxis[1] = {0, 0, 1};
        j.localNormal[0] = j.localNormal[1] = {1, 0, 0};
        j.maxForce = j.maxTorque = kFltMax;
        r.level.joints.push_back(j);
    }
    r.level.actors.push_back(free);
    // A boom and a spar welded together (a joint that breaks at 100) fly at the sail's middle row at 5 units a second.
    Actor boom = actor(ObjectType::Brick, 30, {0, 6, 4}, kDynamic | kNoGravity, 100, {box({3, 0.2f, 0.2f})}, "Yard.jpg");
    Actor spar = actor(ObjectType::Brick, 31, {0, 6, 3}, kDynamic | kNoGravity, 100, {box({3, 0.2f, 0.2f})}, "Yard.jpg");
    boom.body->linearVelocity = spar.body->linearVelocity = {0, 0, 5};
    Joint weld;
    weld.type = NxJointType::Fixed;
    weld.actorA = 30;
    weld.actorB = 31;
    weld.localAnchor[0] = {0, 0, -0.5f};
    weld.localAnchor[1] = {0, 0, 0.5f};
    weld.localAxis[0] = weld.localAxis[1] = {0, 1, 0};
    weld.localNormal[0] = weld.localNormal[1] = {1, 0, 0};
    weld.maxForce = weld.maxTorque = 100;
    r.level.actors.push_back(boom);
    r.level.actors.push_back(spar);
    r.level.joints.push_back(weld);
    PhysicsOptions options;
    options.start = StartState::SavedVelocities;
    PhysicsScene scene(r.level, materials(), options, singleThreaded());
    const ActorIndex boomIndex = kSail + 9;
    bool touched = false;
    for (int s = 0; s < 150; ++s) {
        scene.step(1.0f / 60);
        for (const ContactEvent& e : scene.contacts()) {
            touched = touched || ((e.userDataA == kSail || e.userDataB == kSail) && (e.userDataA == boomIndex || e.userDataB == boomIndex));
        }
    }
    check(touched, "weld: the boom reaches the sail");
    check(scene.brokenJoints().empty(), fmt("weld: and the weld that holds it to its spar is whole (%.0f of 1 broken)", static_cast<float>(scene.brokenJoints().size())));
}

// The Wind prize and the Earthquake move a sail as they move the loose bodies of the level (the wind of the data, which the sail has of its
// own, is across it only).
void testWindAndQuakeMoveASail() {
    // A sail that stands across x, hung from the middle of its top row by a yard that stays: the wind along x is square to it. It has hung
    // for 14 s when the prize comes, which is long enough for it to have settled and gone to sleep.
    struct Hung {
        std::vector<Vec3> at;
        bool asleepBefore = false;
    };
    const auto hung = [](PrizeKind kind, bool give) {
        Level level;
        level.gravity = {0, -9.8f, 0};
        Actor yard = actor(ObjectType::Brick, 10, {0, 8, 10}, kStatic, 100, {box({0.15f, 0.15f, 3})}, "Yard.jpg");
        Actor side = sail(20, 3, 4, 2.0f, 0.0f, 2.0f, 8.0f, {}, "Sail", Facing::Side);
        level.actors = {board(), yard, side};
        level.joints = {pin(side, 10, 10, {0, 8, 10})};  // the middle of the top row
        SessionOptions o = options();
        o.levelEnd = TargetRule::Never;  // (a level with no brick is won at once, and a catch then does nothing)
        Game g(level, o);
        g.run(14);
        Hung result;
        result.asleepBefore = !g.session.scene().world().isAwake(g.session.scene().cloths().body(0));
        if (give) {
            g.session.givePrize(kind);
        }
        g.run(1.2);
        result.at = g.session.scene().cloths().positions(0);
        return result;
    };
    const Hung still = hung(PrizeKind::Wind, false);
    check(still.asleepBefore, "prizes: a sail that has hung for 14 s is asleep");
    for (const PrizeKind kind : {PrizeKind::Wind, PrizeKind::Quake}) {
        const Hung moved = hung(kind, true);
        float most = 0;
        for (std::size_t i = 0; i < moved.at.size() && i < still.at.size(); ++i) {
            most = std::max(most, length(moved.at[i] - still.at[i]));
        }
        check(finite(moved.at) && most > 0.2f,
              fmt(kind == PrizeKind::Wind ? "prizes: the Wind wakes a sail and blows it (it moves %.2f from where it hangs)"
                                          : "prizes: the Earthquake wakes a sail and shakes it (it moves %.2f from where it hangs)", most));
    }
}

// ---- The mesh ---------------------------------------------------------------------------------------------------

void testTheMesh() {
    // A flat sail in the plane z = 0, 3 across and 2 up, the first row the lowest.
    std::vector<Vec3> flat;
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 3; ++c) {
            flat.push_back({static_cast<float>(c) * 2.0f, static_cast<float>(r) * 3.0f, 0});
        }
    }
    const std::vector<Vertex> vertices = clothVertices(3, 2, true, flat);
    const std::vector<std::uint32_t> indices = clothIndices(3, 2);
    check(vertices.size() == 12 && indices.size() == 24, "mesh: two halves of a vertex a particle; two triangles a cell and half");
    check(std::ranges::all_of(indices, [&](std::uint32_t i) { return i < vertices.size(); }), "mesh: every index is a vertex");
    bool unit = true, opposite = true, placed = true, inside = true;
    for (std::size_t i = 0; i < 6; ++i) {
        unit = unit && near(length(vertices[i].normal), 1) && near(length(vertices[i + 6].normal), 1);
        opposite = opposite && near(vertices[i].normal, -vertices[i + 6].normal);
        placed = placed && near(vertices[i].position, flat[i]) && near(vertices[i + 6].position, flat[i]);
        inside = inside && vertices[i].uv.x >= 0 && vertices[i].uv.x <= 1 && vertices[i].uv.y >= 0 && vertices[i].uv.y <= 1;
    }
    check(unit && opposite && placed && inside, "mesh: unit normals, the back's turned round, on the particles, texture coordinates inside the texture");
    check(near(std::fabs(vertices[0].normal.z), 1) && near(vertices[0].normal.z, vertices[5].normal.z), "mesh: a flat sail's normals are square to it, all the same way");
    check(near(vertices[0].uv.x, 0) && near(vertices[2].uv.x, 1) && near(vertices[0].uv.y, 1) && near(vertices[3].uv.y, 0),
          "mesh: the texture's top is the sail's highest row, its left the first column");
    check(near(clothVertices(3, 2, false, flat)[0].uv.y, 0) && near(clothVertices(3, 2, false, flat)[3].uv.y, 1),
          "mesh: or the first row's, for a grid that is the other way up");

    // Front faces of the two halves point opposite ways: each half's triangle is wound the other way round.
    const auto faceNormal = [&](std::size_t first) {
        const Vec3 a = vertices[indices[first]].position, b = vertices[indices[first + 1]].position, c = vertices[indices[first + 2]].position;
        return normalize(cross(b - a, c - a));
    };
    check(dot(faceNormal(0), vertices[indices[0]].normal) > 0.99f && dot(faceNormal(12), vertices[indices[12]].normal) > 0.99f &&
              dot(faceNormal(0), faceNormal(12)) < -0.99f,
          "mesh: each half is wound to face along its own normals, the two the opposite way");

    // A fold: smooth normals between the faces.
    std::vector<Vec3> bent = flat;
    bent[1].z = -1;  // the middle of the lowest row pushed back
    bent[4].z = -1;
    const std::vector<Vertex> folded = clothVertices(3, 2, true, bent);
    check(std::fabs(folded[1].normal.x) < 0.3f && folded[1].normal.z > 0.9f,
          "mesh: a particle on the fold takes the mean of the faces round it, between the two slopes");
    check(folded[0].normal.x != 0 && std::fabs(folded[0].normal.z) < 1, "mesh: a particle on the slope is lit by the slope");
    check(topIsLastRow(ClothPlan{.width = 3, .height = 2, .rest = flat}) && !topIsLastRow(ClothPlan{.width = 3, .height = 2, .rest = {flat[3], flat[4], flat[5], flat[0], flat[1], flat[2]}}),
          "mesh: the last row is the top when it is the higher");
}

}  // namespace

int main() {
    testPlan();
    testPlanPinsToTheWorldAndSkipsWhatItCannotUse();
    testSceneBodies();
    testPinsHoldAndFollowTheHolder();
    testWindBlowsTheSail();
    testASailSleeps();
    testHolderGoneLetsTheSailFall();
    testAHardHitOnTheHolderBreaksItsPins();
    testASailRemovedLeavesTheWorld();
    testASailGoesThroughItsOwnShip();
    testASailThatTouchesIsReported();
    testWhatHurtsASail();
    testTheBallHurtsASail();
    testAWeaponAndABlastHurtASail();
    testASailThatFallsOffTheBoardIsTakenOut();
    testABlastTakesASailWhereItIsNow();
    testASailHoldsNothingUp();
    testAShipThatTouchesASailBreaksNothing();
    testWindAndQuakeMoveASail();
    testTheMesh();
    if (failures == 0) {
        std::puts("all cloth tests passed");
    }
    return failures == 0 ? 0 : 1;
}
