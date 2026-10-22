// PhysicsWorld tests: the Jolt back end behaves as the PhysX 2.x model in Physics.h says. CPU only,
// no game data. Worlds run on the calling thread (threads = 0) unless a test is about threads.

#include "mb/engine/Math.h"
#include "mb/engine/Physics.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace mb;

namespace {

int failures = 0;

// MB3_PHYSICS_VERBOSE=1 also lists the checks that pass, with the numbers some of them carry.
void check(bool ok, const std::string& what) {
    static const bool verbose = std::getenv("MB3_PHYSICS_VERBOSE") != nullptr;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    } else if (verbose) {
        std::printf("  ok: %s\n", what.c_str());
    }
}

bool near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

bool near(Vec3 a, Vec3 b, float tolerance) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

bool near(const Mat4& a, const Mat4& b, float tolerance) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (!near(a.m[i][j], b.m[i][j], tolerance)) {
                return false;
            }
        }
    }
    return true;
}

// Within a relative tolerance of an expected value.
bool within(float value, float expected, float fraction) {
    return std::fabs(value - expected) <= fraction * std::fabs(expected);
}

std::string str(const char* format, float a, float b = 0, float c = 0) {
    char text[256];
    std::snprintf(text, sizeof text, format, static_cast<double>(a), static_cast<double>(b), static_cast<double>(c));
    return text;
}

constexpr Vec3 kGravity{0, -9.8f, 0};
constexpr float kStep = 1.0f / 60.0f;

PhysicsSettings settingsFor(int threads = 0) {
    PhysicsSettings s;
    s.threads = threads;
    return s;
}

// A rigid transform that rotates first, then moves to the position.
Mat4 placed(const Mat4& rotation, Vec3 position) {
    Mat4 m = rotation;
    m.m[3][0] = position.x;
    m.m[3][1] = position.y;
    m.m[3][2] = position.z;
    return m;
}

Mat4 at(Vec3 position) {
    return Mat4::translation(position);
}

PhysicsShape boxShape(Vec3 half, PhysicsMaterial material = {}) {
    PhysicsShape s;
    s.type = PhysicsShapeType::Box;
    s.halfExtents = half;
    s.material = material;
    return s;
}

PhysicsShape sphereShape(float radius, PhysicsMaterial material = {}) {
    PhysicsShape s;
    s.type = PhysicsShapeType::Sphere;
    s.radius = radius;
    s.material = material;
    return s;
}

PhysicsMaterial materialWith(float friction, float restitution, CombineMode frictionMode = CombineMode::Average,
                             CombineMode restitutionMode = CombineMode::Average) {
    PhysicsMaterial m;
    m.staticFriction = m.dynamicFriction = friction;
    m.restitution = restitution;
    m.frictionCombine = frictionMode;
    m.restitutionCombine = restitutionMode;
    return m;
}

BodyDesc staticBody(const Mat4& pose, PhysicsShape shape) {
    BodyDesc d;
    d.pose = pose;
    d.motion = MotionType::Static;
    d.shapes = {std::move(shape)};
    return d;
}

BodyDesc dynamicBody(const Mat4& pose, PhysicsShape shape) {
    BodyDesc d;
    d.pose = pose;
    d.shapes = {std::move(shape)};
    return d;
}

// A big floor whose top is at y = 0.
BodyId addGround(PhysicsWorld& world, PhysicsMaterial material = {}) {
    return world.addBody(staticBody(at({0, -0.5f, 0}), boxShape({50, 0.5f, 50}, material)));
}

Vec3 position(const PhysicsWorld& world, BodyId body) {
    return world.pose(body).translationPart();
}

void run(PhysicsWorld& world, int steps) {
    for (int i = 0; i < steps; ++i) {
        world.step(kStep);
    }
}

void runSeconds(PhysicsWorld& world, float seconds) {
    run(world, static_cast<int>(std::lround(seconds / kStep)));
}

// ---- Conventions ---------------------------------------------------------------------------------

void testPoseConvention() {
    PhysicsWorld world(kGravity, settingsFor());

    // The pose that went in comes out of pose(): rotation first, then translation, row vectors.
    for (const Mat4& wanted : {Mat4::rotationY(0.3f) * Mat4::translation({1, 2, 3}),
                               Mat4::rotationX(0.4f) * Mat4::rotationY(-1.1f) * Mat4::rotationZ(2.0f) *
                                   Mat4::translation({-4, 5, 0.5f}),
                               Mat4::identity()}) {
        BodyDesc d = dynamicBody(wanted, boxShape({0.5f, 0.5f, 0.5f}));
        d.gravity = false;
        const BodyId body = world.addBody(d);
        check(body != 0, "pose: body added");
        check(near(world.pose(body), wanted, 1e-5f), "pose: pose() returns the pose it was created with");
    }

    // Spinning about +y at 1 rad/s turns the pose by rotationY(t): the same numbers as omega x p.
    for (const float t : {0.5f, 1.0f, 2.0f}) {
        PhysicsWorld w(kGravity, settingsFor());
        BodyDesc d = dynamicBody(at({0, 3, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.gravity = false;
        d.angularDamping = 0;
        d.angularVelocity = {0, 1, 0};
        const BodyId body = w.addBody(d);
        runSeconds(w, t);
        check(near(w.pose(body), placed(Mat4::rotationY(t), {0, 3, 0}), 2e-4f),
              str("pose: after %.1f s the pose is rotationY(t)", t));
        const Vec3 x = transformVector({1, 0, 0}, w.pose(body));
        check(near(x, {std::cos(t), 0, -std::sin(t)}, 2e-4f),
              "pose: (1,0,0) * pose = (cos t, 0, -sin t), the direction omega x p points");
        check(near(w.angularVelocity(body), {0, 1, 0}, 1e-5f), "pose: angular velocity is reported as given");
    }

    // Linear velocity is plain numbers too.
    BodyDesc d = dynamicBody(at({0, 3, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    d.gravity = false;
    d.linearDamping = 0;
    d.linearVelocity = {1, 2, -3};
    const BodyId body = world.addBody(d);
    runSeconds(world, 1);
    check(near(position(world, body), {1, 5, -3}, 1e-3f),
          "pose: linear velocity moves the body along x, y, z as given");
}

// ---- Materials -----------------------------------------------------------------------------------

// How high the first bounce goes, as a fraction of the height the ball was dropped from.
float bounceRatio(const PhysicsMaterial& ball, const PhysicsMaterial& ground, float dropHeight,
                  float* risenAfterImpact = nullptr) {
    PhysicsSettings s = settingsFor();
    s.fixedStep = 1.0f / 240.0f;  // fine enough that the step does not show in the heights
    PhysicsWorld world(kGravity, s);
    addGround(world, ground);
    constexpr float radius = 0.25f;
    const BodyId body = world.addBody(dynamicBody(at({0, radius + dropHeight, 0}), sphereShape(radius, ball)));
    bool rising = false;
    float apex = radius;
    for (int i = 0; i < 4 * 240; ++i) {
        world.step(s.fixedStep);
        const float vy = world.linearVelocity(body).y;
        rising = rising || vy > 0.05f;
        if (rising) {
            apex = std::max(apex, position(world, body).y);
            if (vy < 0) {
                break;
            }
        }
    }
    if (risenAfterImpact != nullptr) {
        *risenAfterImpact = apex - radius;
    }
    return (apex - radius) / dropHeight;
}

void testRestitution() {
    const float height = 2.0f;
    for (const float e : {0.5f, 0.8f}) {
        const float ratio = bounceRatio(materialWith(0.5f, e), materialWith(0.5f, e), height);
        check(within(ratio, e * e, 0.10f),
              str("restitution %.2f: the bounce reaches about e^2 of the drop (%.3f)", e, ratio));
    }

    // Slower than the bounce threshold (2 m/s; this lands at 1 m/s) the ball does not bounce at all.
    float risen = 1;
    bounceRatio(materialWith(0.5f, 0.9f), materialWith(0.5f, 0.9f), 0.05f, &risen);
    check(risen < 0.01f, str("restitution: an impact below the bounce threshold does not bounce (%.4f m)", risen));
}

float combined(CombineMode mode, float a, float b) {
    switch (mode) {
        case CombineMode::Average: return 0.5f * (a + b);
        case CombineMode::Min: return std::min(a, b);
        case CombineMode::Multiply: return a * b;
        case CombineMode::Max: return std::max(a, b);
    }
    return 0;
}

void testRestitutionCombine() {
    const std::array<CombineMode, 4> modes = {CombineMode::Average, CombineMode::Min, CombineMode::Multiply,
                                               CombineMode::Max};
    const char* names[] = {"average", "min", "multiply", "max"};
    const float ballE = 0.5f, groundE = 0.9f;
    for (const CombineMode a : modes) {
        for (const CombineMode b : modes) {
            // The mode that wins is the higher-numbered one, whichever body has it.
            const CombineMode winner = std::max(a, b);
            const float e = combined(winner, ballE, groundE);
            const float ratio = bounceRatio(materialWith(0.5f, ballE, CombineMode::Average, a),
                                            materialWith(0.5f, groundE, CombineMode::Average, b), 2.0f);
            check(within(ratio, e * e, 0.10f),
                  std::string("restitution combine: ball ") + names[static_cast<int>(a)] + ", ground " +
                      names[static_cast<int>(b)] + " -> " + names[static_cast<int>(winner)] +
                      str(": height %.3f vs expected %.3f", ratio, e * e));
        }
    }
}

// How far a flat slab slides across the floor from a push of 3 m/s before friction stops it.
float slideDistance(const PhysicsMaterial& slab, const PhysicsMaterial& ground) {
    PhysicsWorld world(kGravity, settingsFor());
    addGround(world, ground);
    BodyDesc d = dynamicBody(at({0, 0.1f + 0.001f, 0}), boxShape({0.3f, 0.1f, 0.3f}, slab));
    d.linearVelocity = {3, 0, 0};
    const BodyId body = world.addBody(d);
    runSeconds(world, 5);
    return position(world, body).x;
}

void testFrictionCombine() {
    const std::array<CombineMode, 4> modes = {CombineMode::Average, CombineMode::Min, CombineMode::Multiply,
                                               CombineMode::Max};
    const char* names[] = {"average", "min", "multiply", "max"};
    const float slabMu = 0.2f, groundMu = 0.8f;
    for (const CombineMode a : modes) {
        for (const CombineMode b : modes) {
            const CombineMode winner = std::max(a, b);
            const float mu = combined(winner, slabMu, groundMu);
            PhysicsMaterial slab = materialWith(slabMu, 0, a), ground = materialWith(groundMu, 0, b);
            // Jolt knows one coefficient; the dynamic one is it. A wild static value must change nothing.
            slab.staticFriction = 5;
            ground.staticFriction = 0.01f;
            const float expected = 3.0f * 3.0f / (2 * mu * 9.8f);
            const float distance = slideDistance(slab, ground);
            check(within(distance, expected, 0.10f),
                  std::string("friction combine: slab ") + names[static_cast<int>(a)] + ", ground " +
                      names[static_cast<int>(b)] + " -> " + names[static_cast<int>(winner)] +
                      str(": slid %.3f m vs expected %.3f m", distance, expected));
        }
    }
}

// ---- Resting, stacking, slopes -------------------------------------------------------------------

void testStackStandsAndSleeps() {
    PhysicsWorld world(kGravity, settingsFor());
    addGround(world);
    std::vector<BodyId> boxes;
    std::vector<Vec3> start;
    for (int i = 0; i < 5; ++i) {
        const Vec3 p{0, 0.5f + static_cast<float>(i), 0};
        boxes.push_back(world.addBody(dynamicBody(at(p), boxShape({0.5f, 0.5f, 0.5f}))));
        start.push_back(p);
    }
    float worst = 0;
    for (int i = 0; i < 600; ++i) {
        world.step(kStep);
        for (std::size_t b = 0; b < boxes.size(); ++b) {
            worst = std::max(worst, length(position(world, boxes[b]) - start[b]));
        }
    }
    check(worst < 0.01f, str("stack: five boxes move %.4f m at most in 10 s", worst));
    bool allAsleep = true;
    for (BodyId b : boxes) {
        allAsleep = allAsleep && !world.isAwake(b);
    }
    check(allAsleep && world.awakeBodyCount() == 0, "stack: the boxes fall asleep");
}

// A thin plate keeps its thickness: the convex radius must not round it away.
void testThinPlateAndSmallBox() {
    PhysicsWorld world(kGravity, settingsFor());
    addGround(world);
    const BodyId plate = world.addBody(dynamicBody(at({0, 1.0f, 0}), boxShape({0.5f, 0.0325f, 0.5f})));
    runSeconds(world, 3);
    check(near(position(world, plate).y, 0.0325f, 0.006f),
          str("plate: a 6.5 cm plate rests at y = %.4f", position(world, plate).y));

    // On a gentle slope a small cube stays where it is; a ball of that size would roll away.
    PhysicsWorld slope(kGravity, settingsFor());
    const Mat4 tilt = Mat4::rotationZ(radians(10));
    slope.addBody(staticBody(placed(tilt, {0, 0, 0}), boxShape({10, 0.5f, 5})));
    const Vec3 onTop = transformVector({0, 0.5f + 0.03f + 0.001f, 0}, tilt);
    const BodyId cube = slope.addBody(dynamicBody(placed(tilt, onTop), boxShape({0.03f, 0.03f, 0.03f})));
    runSeconds(slope, 2);
    check(length(position(slope, cube) - onTop) < 0.01f, "plate: a 6 cm cube stays put on a 10 degree slope");
}

float slopeSlide(float friction) {
    PhysicsWorld world(kGravity, settingsFor());
    const Mat4 tilt = Mat4::rotationZ(radians(30));
    world.addBody(staticBody(placed(tilt, {0, 0, 0}), boxShape({20, 0.5f, 5}, materialWith(friction, 0))));
    const Vec3 onTop = transformVector({0, 1.0f + 0.002f, 0}, tilt);
    const BodyId box =
        world.addBody(dynamicBody(placed(tilt, onTop), boxShape({0.5f, 0.5f, 0.5f}, materialWith(friction, 0))));
    runSeconds(world, 2);
    return length(position(world, box) - onTop);
}

// Levels were saved with bodies overlapping (a pile of logs, a ship's planks): woken, they must part slowly, and
// not through the floor.
void testDeepOverlap() {
    PhysicsWorld world(kGravity, settingsFor());
    world.addBody(staticBody(at({0, -0.42f, 0}), boxShape({50, 0.42f, 50}, materialWith(0.5f, 1.0f))));
    std::vector<BodyId> column;
    for (int i = 0; i < 6; ++i) {  // each box 0.8 of its height inside the one under it
        BodyDesc d = dynamicBody(at({0, 0.5f + 0.2f * static_cast<float>(i), 0}), boxShape({0.5f, 0.5f, 0.5f}, materialWith(0.6f, 0.3f)));
        d.mass = 50;
        d.inertia = {8, 8, 8};
        column.push_back(world.addBody(d));
    }
    float fastest = 0, lowest = 1e9f;
    for (int i = 0; i < 120; ++i) {
        world.step(kStep);
        for (const BodyId body : column) {
            fastest = std::max(fastest, length(world.linearVelocity(body)));
            lowest = std::min(lowest, position(world, body).y - 0.5f);
        }
    }
    check(fastest < 0.08f, str("overlap: boxes saved 80%% inside each other part at %.3f units per second at most", fastest));
    check(lowest > -0.3f, str("overlap: and none goes more than 0.3 under the floor (lowest %.3f)", lowest));
}

// A tower whose top weighs fifty times a segment (a windmill's roof on its stack of stones) stands for 10 s awake.
// Jolt's own 10 velocity and 2 position iterations let it sink and tilt; PhysicsSettings' defaults hold it.
float towerDrift(int velocitySteps, int positionSteps) {
    PhysicsSettings settings = settingsFor();
    settings.velocitySteps = velocitySteps;
    settings.positionSteps = positionSteps;
    PhysicsWorld world(kGravity, settings);
    world.addBody(staticBody(at({0, -0.42f, 0}), boxShape({50, 0.42f, 50}, materialWith(0.5f, 0.5f))));
    std::vector<BodyId> tower;
    std::vector<Vec3> start;
    for (int i = 0; i < 9; ++i) {
        const bool top = i == 8;
        const Vec3 p{0, top ? 8.0f + 1.5f : 0.5f + static_cast<float>(i), 0};
        BodyDesc d = dynamicBody(at(p), boxShape(top ? Vec3{1.5f, 1.5f, 1.5f} : Vec3{0.5f, 0.5f, 0.5f},
                                                 materialWith(0.5f, 0.1f)));
        d.mass = top ? 5000.0f : 100.0f;
        d.inertia = Vec3{1, 1, 1} * (d.mass / 6);
        d.sleepLinearVelocity = 0;  // stays awake, so that the solver is at work all the time
        tower.push_back(world.addBody(d));
        start.push_back(p);
    }
    float worst = 0;
    for (int i = 0; i < 600; ++i) {
        world.step(kStep);
        for (std::size_t b = 0; b < tower.size(); ++b) {
            worst = std::max(worst, length(position(world, tower[b]) - start[b]));
        }
    }
    return worst;
}

void testHeavyTowerStands() {
    const float shipped = towerDrift(PhysicsSettings{}.velocitySteps, PhysicsSettings{}.positionSteps);
    check(shipped < 0.25f, str("tower: a 5000 roof on eight 100 stones, awake for 10 s, moves %.3f at most", shipped));
    const float joltDefault = towerDrift(10, 2);
    check(joltDefault > 10 * shipped, str("tower: and Jolt's own iteration counts do worse (%.3f)", joltDefault));
}

// Jolt's capsule with a cylinder of a few millimetres sinks into a floor; PhysicsWorld makes it a sphere (the levels
// have bags and rocks like that, 1.4 across).
void testShortCapsuleStaysOnTheFloor() {
    for (const float halfHeight : {0.005f, 0.02f, 0.045f}) {
        float lowestCentre = 1e9f;
        for (int trial = 0; trial < 12; ++trial) {
            PhysicsWorld world(kGravity, settingsFor());
            world.addBody(staticBody(at({0, -0.42f, 0}), boxShape({30, 0.42f, 30}, materialWith(0.5f, 1.0f))));
            PhysicsShape capsule;
            capsule.type = PhysicsShapeType::Capsule;
            capsule.radius = 1.4f;
            capsule.halfHeight = halfHeight;
            capsule.material = materialWith(0.5f, 0.3f);
            const float t = static_cast<float>(trial);
            BodyDesc d = dynamicBody(placed(Mat4::rotationX(t * 0.5f) * Mat4::rotationZ(t * 0.9f), {0, 1.7f, 0}), capsule);
            d.mass = 20;
            d.inertia = {10, 10, 10};
            d.linearVelocity = {std::sin(t), 0, std::cos(t)};
            const BodyId body = world.addBody(d);
            runSeconds(world, 10);
            lowestCentre = std::min(lowestCentre, position(world, body).y);
        }
        check(lowestCentre > 1.4f - 0.05f, str("capsule: a radius 1.4 capsule with a %.3f half height rests on the floor "
                                                "(lowest centre %.3f)", halfHeight, lowestCentre));
    }
}

void testSlope() {
    const float holds = slopeSlide(0.7f);
    check(holds < 0.01f, str("slope: a box holds on 30 degrees with friction 0.7 (moved %.4f m)", holds));
    const float slides = slopeSlide(0.3f);
    const float expected = 0.5f * 9.8f * (0.5f - 0.3f * std::cos(radians(30))) * 4;  // a = g (sin a - mu cos a), 2 s
    check(within(slides, expected, 0.15f),
          str("slope: with friction 0.3 it slides %.2f m, expected %.2f m", slides, expected));
}

// ---- Mass ----------------------------------------------------------------------------------------

BodyDesc floating(const Mat4& pose, PhysicsShape shape) {
    BodyDesc d = dynamicBody(pose, std::move(shape));
    d.gravity = false;
    d.angularDamping = 0;
    d.linearDamping = 0;
    return d;
}

void testMassProperties() {
    {
        // mass 5, inertia (1,2,3): an impulse of (0,0,1) at (1,0,0) gives v = 0.2 and, with
        // L = r x J = (0,-1,0), omega_y = -L / Iy.
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.mass = 5;
        d.inertia = {1, 2, 3};
        const BodyId body = world.addBody(d);
        world.addImpulse(body, {0, 0, 1}, {1, 0, 0});
        check(near(world.linearVelocity(body), {0, 0, 0.2f}, 1e-5f), "mass: v = J / m");
        check(near(world.angularVelocity(body), {0, -0.5f, 0}, 1e-5f), "mass: omega = L / I about the body's y");
    }
    {
        // The inertia is given in the mass frame: turned a quarter about z, the mass frame's x axis
        // is the body's y and its y axis the body's -x.
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.mass = 5;
        d.inertia = {1, 2, 3};
        d.massLocalPose = Mat4::rotationZ(kPi / 2);
        const BodyId body = world.addBody(d);
        world.addImpulse(body, {0, 0, 1}, {1, 0, 0});
        check(near(world.angularVelocity(body), {0, -1.0f, 0}, 1e-4f),
              "mass: the inertia follows massLocalPose's rotation (y)");
        world.setAngularVelocity(body, {0, 0, 0});
        world.addImpulse(body, {0, 1, 0}, {0, 0, 1});  // L = (-1,0,0)
        check(near(world.angularVelocity(body), {-0.5f, 0, 0}, 1e-4f),
              "mass: the inertia follows massLocalPose's rotation (x)");
    }
    {
        // The centre of mass sits at massLocalPose's translation; the frame stays where it was put.
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.mass = 5;
        d.inertia = {1, 2, 3};
        d.massLocalPose = Mat4::translation({1, 0, 0});
        const BodyId body = world.addBody(d);
        world.addImpulse(body, {0, 0, 1}, {1, 0, 0});  // through the centre of mass: no spin
        check(near(world.angularVelocity(body), {0, 0, 0}, 1e-5f),
              "mass: an impulse through the centre of mass does not spin");
        world.addImpulse(body, {0, 0, 1}, {0, 0, 0});  // at the frame's origin, 1 m from it: L = (0,1,0)
        check(near(world.angularVelocity(body), {0, 0.5f, 0}, 1e-5f),
              "mass: an impulse at the frame spins about the centre of mass");
        check(near(world.pose(body), at({0, 0, 0}), 1e-6f), "mass: pose() is still the frame, not the centre of mass");
    }
    {
        // In free flight the centre of mass holds still and the frame circles it.
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.mass = 5;
        d.inertia = {1, 1, 1};
        d.massLocalPose = Mat4::translation({1, 0, 0});
        d.angularVelocity = {0, 1, 0};
        const BodyId body = world.addBody(d);
        runSeconds(world, 1);
        const Vec3 centre = transformPoint({1, 0, 0}, world.pose(body));
        check(near(centre, {1, 0, 0}, 2e-3f), "mass: the centre of mass does not move while the body spins");
        check(near(position(world, body), {1 - std::cos(1.0f), 0, std::sin(1.0f)}, 3e-3f),
              "mass: the frame circles the centre of mass");
    }
    {
        // Mass from density: a 1 m cube at density 2 weighs 2.
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.density = 2;
        const BodyId body = world.addBody(d);
        world.addImpulse(body, {1, 0, 0}, {0, 0, 0});
        check(near(world.linearVelocity(body).x, 0.5f, 1e-5f), "mass: density 2 on a unit cube gives mass 2");
        check(near(world.mass(body), 2.0f, 1e-4f), "mass: mass() says so");
        const BodyId ball = world.addBody(floating(at({5, 0, 0}), sphereShape(1.0f)));
        const BodyId given = [&] {
            BodyDesc heavy = floating(at({10, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
            heavy.mass = 5;
            return world.addBody(heavy);
        }();
        check(near(world.mass(ball), 4.0f * kPi / 3.0f, 1e-3f) && near(world.mass(given), 5.0f, 1e-4f),
              "mass: a sphere of radius 1 and density 1 is 4/3 pi, and a given mass is the mass");
        BodyDesc kinematic = floating(at({15, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        kinematic.motion = MotionType::Kinematic;
        check(world.mass(world.addBody(staticBody(at({20, 0, 0}), boxShape({0.5f, 0.5f, 0.5f})))) == 0 &&
                  world.mass(world.addBody(kinematic)) == 0 && world.mass(0) == 0,
              "mass: none for a static or kinematic body, or none at all");
    }
    {
        // Mass given, inertia zero: the inertia of the shape scaled to that mass: 4/12 * (1 + 1).
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.mass = 4;
        const BodyId body = world.addBody(d);
        world.addImpulse(body, {0, 0, 1}, {1, 0, 0});
        check(near(world.linearVelocity(body).z, 0.25f, 1e-5f), "mass: the given mass is used");
        check(near(world.angularVelocity(body).y, -1.5f, 1e-3f),
              "mass: a zero inertia comes from the shape, scaled to the mass");
    }
    {
        // Two cubes at x = -1 and x = 3: the centre of mass is at x = 1, I_y = 2 (1/6 + 4).
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.shapes[0].localPose = at({-1, 0, 0});
        d.shapes.push_back(boxShape({0.5f, 0.5f, 0.5f}));
        d.shapes[1].localPose = at({3, 0, 0});
        const BodyId body = world.addBody(d);
        world.addImpulse(body, {0, 0, 1}, {0, 0, 0});
        check(near(world.linearVelocity(body).z, 0.5f, 1e-4f), "compound: mass is the sum of the shapes'");
        check(near(world.angularVelocity(body).y, 1.0f / (2 * (1.0f / 6 + 4)), 2e-3f),
              "compound: the inertia is taken about the centre of mass of all shapes");
        check(near(world.pose(body), at({0, 0, 0}), 1e-6f), "compound: pose() is the frame");
    }
}

// A box set on a narrow ledge tips towards its centre of mass.
float tipSpin(float comX) {
    PhysicsWorld world(kGravity, settingsFor());
    world.addBody(staticBody(at({0, 0.5f, 0}), boxShape({0.1f, 0.5f, 2})));
    BodyDesc d = dynamicBody(at({0, 1.5f + 0.002f, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    d.mass = 1;
    d.inertia = {0.2f, 0.2f, 0.2f};
    d.massLocalPose = Mat4::translation({comX, 0, 0});
    const BodyId body = world.addBody(d);
    runSeconds(world, 0.6f);
    return world.angularVelocity(body).z;
}

void testCentreOfMassTips() {
    check(tipSpin(0.35f) < -0.5f, "tip: a centre of mass beyond the ledge on +x tips the box towards +x (omega_z < 0)");
    check(tipSpin(-0.35f) > 0.5f, "tip: and on -x towards -x (omega_z > 0)");
    check(std::fabs(tipSpin(0.0f)) < 0.05f, "tip: a centred one stays balanced");
}

void testShapeFrames() {
    // A shape set 2 m off the frame: the frame stays level and where it is, the shape lands on the floor.
    PhysicsWorld world(kGravity, settingsFor());
    addGround(world);
    BodyDesc d = dynamicBody(at({0, 3, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    d.shapes[0].localPose = at({2, 0, 0});
    const BodyId body = world.addBody(d);
    runSeconds(world, 2);
    const Mat4 pose = world.pose(body);
    check(near(pose.translationPart(), {0, 0.5f, 0}, 0.02f),
          "frames: a body with an offset shape rests at its frame, not its shape");
    check(near(pose, placed(Mat4::identity(), pose.translationPart()), 0.01f), "frames: and stays level");

    // Each kind of shape rests on the floor at its own size.
    PhysicsWorld rest(kGravity, settingsFor());
    addGround(rest);
    PhysicsShape cube;
    cube.type = PhysicsShapeType::ConvexHull;
    for (const float x : {-0.5f, 0.5f}) {
        for (const float y : {-0.5f, 0.5f}) {
            for (const float z : {-0.5f, 0.5f}) {
                cube.points.push_back({x, y, z});
            }
        }
    }
    PhysicsShape lying;
    lying.type = PhysicsShapeType::Capsule;
    lying.radius = 0.3f;
    lying.halfHeight = 0.5f;
    const BodyId hullBody = rest.addBody(dynamicBody(at({-4, 1, 0}), cube));
    const BodyId ballBody = rest.addBody(dynamicBody(at({0, 1, 0}), sphereShape(0.4f)));
    const BodyId capsuleBody = rest.addBody(dynamicBody(placed(Mat4::rotationZ(kPi / 2), {4, 1, 0}), lying));
    runSeconds(rest, 1.5f);
    check(near(position(rest, hullBody).y, 0.5f, 0.01f),
          str("hull: a cube-shaped hull rests at y = %.4f", position(rest, hullBody).y));
    check(near(position(rest, ballBody).y, 0.4f, 0.01f), str("sphere: rests at y = %.4f", position(rest, ballBody).y));
    check(near(position(rest, capsuleBody).y, 0.3f, 0.01f),
          str("capsule: on its side rests at y = %.4f", position(rest, capsuleBody).y));

    // Shapes are where the descriptions put them: ray casts along the axes read their extents off.
    PhysicsWorld shapes(kGravity, settingsFor());
    PhysicsShape capsule;
    capsule.type = PhysicsShapeType::Capsule;
    capsule.radius = 0.3f;
    capsule.halfHeight = 0.5f;
    // Rotated a quarter about z, the capsule's y axis lies along x: it reaches 0.8 either way.
    BodyDesc c = staticBody(placed(Mat4::rotationZ(kPi / 2), {0, 0, 0}), capsule);
    shapes.addBody(c);
    const auto fromLeft = shapes.raycast({-5, 0, 0}, {1, 0, 0}, 20);
    check(fromLeft && near(fromLeft->distance, 5 - 0.8f, 1e-3f),
          "capsule: runs along its local y, half height 0.5 plus radius 0.3");
    const auto fromAbove = shapes.raycast({0, 5, 0}, {0, -1, 0}, 20);
    check(fromAbove && near(fromAbove->distance, 5 - 0.3f, 1e-3f), "capsule: radius 0.3 across");

    PhysicsShape hull;
    hull.type = PhysicsShapeType::ConvexHull;
    for (const float x : {-0.5f, 0.5f}) {
        for (const float y : {-1.0f, 1.0f}) {
            for (const float z : {-0.25f, 0.25f}) {
                hull.points.push_back({x, y, z});
            }
        }
    }
    shapes.addBody(staticBody(at({10, 0, 0}), hull));
    const auto hullHit = shapes.raycast({5, 0, 0}, {1, 0, 0}, 20);
    check(hullHit && near(hullHit->distance, 4.5f, 2e-3f), "hull: points are in the shape's space");
    const auto hullTop = shapes.raycast({10, 5, 0}, {0, -1, 0}, 20);
    check(hullTop && near(hullTop->distance, 4.0f, 2e-3f), "hull: a box-shaped hull is 2 m tall");

    // A sphere and a compound.
    shapes.addBody(staticBody(at({0, 0, 10}), sphereShape(1.5f)));
    const auto sphereHit = shapes.raycast({0, 0, 5}, {0, 0, 1}, 20);
    check(sphereHit && near(sphereHit->distance, 3.5f, 1e-3f), "sphere: radius");
    BodyDesc two = staticBody(at({0, 0, -10}), boxShape({1, 1, 1}));
    two.shapes[0].localPose = at({-3, 0, 0});
    two.shapes.push_back(boxShape({1, 1, 1}));
    two.shapes[1].localPose = at({3, 0, 0});
    shapes.addBody(two);
    const auto along = [&](float x) { return shapes.raycast({x, 0.9f, -15}, {0, 0, 1}, 20).has_value(); };
    check(along(-3) && along(3) && !along(0), "compound: both shapes are there, and nothing between them");
}

// ---- Locks, sleep, kinematic ---------------------------------------------------------------------

void testLocks() {
    {
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.lockRotation[0] = d.lockRotation[1] = d.lockRotation[2] = true;
        const BodyId locked = world.addBody(d);
        const BodyId free = world.addBody(floating(at({5, 0, 0}), boxShape({0.5f, 0.5f, 0.5f})));
        world.addImpulse(locked, {0, 0, 5}, {0.5f, 0, 0});
        world.addImpulse(free, {0, 0, 5}, {5.5f, 0, 0});
        runSeconds(world, 0.5f);
        check(length(world.angularVelocity(locked)) < 1e-4f &&
                  near(world.pose(locked), placed(Mat4::identity(), position(world, locked)), 1e-4f),
              "locks: a rotation-locked body hit off-centre does not spin");
        check(position(world, locked).z > 1, "locks: but it moves");
        check(length(world.angularVelocity(free)) > 0.5f, "locks: an unlocked one does spin");
    }
    {
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = dynamicBody(at({0, 5, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.lockTranslation[1] = true;
        d.linearVelocity = {1, 0, 0};
        d.linearDamping = 0;
        const BodyId body = world.addBody(d);
        runSeconds(world, 1);
        check(near(position(world, body).y, 5, 1e-4f), "locks: a translation-locked axis stays despite gravity");
        check(near(position(world, body).x, 1, 0.01f), "locks: the others are free");
    }
}

void testStartAsleep() {
    PhysicsWorld world(kGravity, settingsFor());
    BodyDesc d = dynamicBody(at({0, 5, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    d.startAsleep = true;
    const BodyId sleeper = world.addBody(d);
    check(!world.isAwake(sleeper), "asleep: starts asleep");
    runSeconds(world, 1);
    check(near(position(world, sleeper), {0, 5, 0}, 1e-6f) && !world.isAwake(sleeper), "asleep: and stays where it is");

    // Something falls on it: it wakes and moves.
    BodyDesc ball = dynamicBody(at({0, 7, 0}), sphereShape(0.3f));
    ball.density = 20;
    const BodyId hitter = world.addBody(ball);
    runSeconds(world, 1.5f);
    check(world.isAwake(sleeper) || position(world, sleeper).y < 4.9f, "asleep: a hit wakes it");
    check(position(world, sleeper).y < 4.9f && position(world, hitter).y < 6.5f, "asleep: and it falls");

    // wake() works too.
    PhysicsWorld other(kGravity, settingsFor());
    const BodyId second = other.addBody(d);
    other.wake(second);
    check(other.isAwake(second), "asleep: wake() wakes");
    runSeconds(other, 0.5f);
    check(position(other, second).y < 4.5f, "asleep: and the body then falls");

    // Sleep threshold zero: never sleeps.
    PhysicsWorld sleepless(kGravity, settingsFor());
    addGround(sleepless);
    BodyDesc rest = dynamicBody(at({0, 0.5f, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    rest.sleepLinearVelocity = 0;
    const BodyId awake = sleepless.addBody(rest);
    const BodyId normal = sleepless.addBody(dynamicBody(at({5, 0.5f, 0}), boxShape({0.5f, 0.5f, 0.5f})));
    runSeconds(sleepless, 5);
    check(sleepless.isAwake(awake) && !sleepless.isAwake(normal),
          "asleep: a body asking for no sleep stays awake, one at rest otherwise falls asleep");
}

// A level is saved with velocities on its bodies, and asleep it must not move: the velocity it is
// given is dropped, not carried out the moment it wakes (Jolt asserts on a sleeping body that has one).
void testStartAsleepDropsVelocity() {
    PhysicsWorld world(kGravity, settingsFor());
    BodyDesc d = floating(at({0, 5, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    d.linearVelocity = {5, 0, 0};
    d.angularVelocity = {0, 3, 0};
    d.startAsleep = true;
    const BodyId body = world.addBody(d);
    check(!world.isAwake(body), "asleep with velocity: starts asleep");
    check(length(world.linearVelocity(body)) == 0 && length(world.angularVelocity(body)) == 0,
          "asleep with velocity: the velocities it was given are zero");
    runSeconds(world, 0.5f);
    check(near(world.pose(body), at({0, 5, 0}), 1e-6f), "asleep with velocity: it stays put");
    world.wake(body);
    runSeconds(world, 0.5f);
    check(near(world.pose(body), at({0, 5, 0}), 1e-6f),
          "asleep with velocity: and wakes with nothing to move it (no gravity here)");

    // A body that is awake keeps what it was given.
    BodyDesc moving = d;
    moving.startAsleep = false;
    const BodyId other = world.addBody(moving);
    runSeconds(world, 0.5f);
    check(position(world, other).x > 2, "asleep with velocity: an awake body does move on its velocity");
}

void testFullyLockedIsStatic() {
    PhysicsWorld world(kGravity, settingsFor());
    addGround(world);
    BodyDesc d = dynamicBody(at({0, 3, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    d.mass = 2;
    d.inertia = {1, 1, 1};
    std::fill(std::begin(d.lockTranslation), std::end(d.lockTranslation), true);
    std::fill(std::begin(d.lockRotation), std::end(d.lockRotation), true);
    const BodyId pinned = world.addBody(d);
    check(pinned != 0 && world.motionType(pinned) == MotionType::Static,
          "locked: a dynamic body locked on all six axes is static");

    BodyDesc k = d;
    k.motion = MotionType::Kinematic;
    k.pose = at({3, 3, 0});
    const BodyId pinnedKinematic = world.addBody(k);
    check(pinnedKinematic != 0 && world.motionType(pinnedKinematic) == MotionType::Static,
          "locked: so is a kinematic one");

    BodyDesc half = d;  // five of six is still dynamic, and free to fall along y
    half.pose = at({-3, 3, 0});
    half.lockTranslation[1] = false;
    const BodyId almost = world.addBody(half);
    check(world.motionType(almost) == MotionType::Dynamic, "locked: five axes of six leave it dynamic");

    world.addImpulse(pinned, {10, 10, 10}, {0, 3, 0});
    world.setMotionType(pinned, MotionType::Dynamic);  // refused: it is locked
    runSeconds(world, 1);
    check(world.motionType(pinned) == MotionType::Static && near(position(world, pinned), {0, 3, 0}, 1e-6f) &&
              near(position(world, pinnedKinematic), {3, 3, 0}, 1e-6f),
          "locked: it does not move, and setMotionType does not free it");
    check(position(world, almost).y < 1, "locked: the one with a free axis falls");
}

void testKinematicStopsWhereItWas() {
    PhysicsWorld world(kGravity, settingsFor());
    BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    d.linearVelocity = {3, 0, 0};
    d.angularVelocity = {0, 2, 0};
    const BodyId body = world.addBody(d);
    runSeconds(world, 0.25f);
    check(position(world, body).x > 0.5f, "kinematic switch: the dynamic body was moving");
    world.setMotionType(body, MotionType::Kinematic);
    check(length(world.linearVelocity(body)) == 0 && length(world.angularVelocity(body)) == 0,
          "kinematic switch: becoming kinematic zeroes the velocities");
    const Mat4 held = world.pose(body);
    runSeconds(world, 1);
    check(near(world.pose(body), held, 1e-6f), "kinematic switch: and the body stays where it was");
}

void testKinematic() {
    PhysicsWorld world(kGravity, settingsFor());
    BodyDesc k = dynamicBody(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    k.motion = MotionType::Kinematic;
    const BodyId pusher = world.addBody(k);
    BodyDesc d = floating(at({1.5f, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    const BodyId load = world.addBody(d);

    // Two metres per second for 1.5 s, a target per frame.
    float worstError = 0;
    for (int i = 1; i <= 90; ++i) {
        const Mat4 target = at({2.0f * kStep * static_cast<float>(i), 0, 0});
        world.setKinematicTarget(pusher, target);
        world.step(kStep);
        worstError = std::max(worstError, length(position(world, pusher) - target.translationPart()));
    }
    check(worstError < 1e-4f, str("kinematic: the body reaches each target in the step (%.5f off)", worstError));
    check(position(world, load).x > 3.5f,
          str("kinematic: it pushes the box in its way (now at %.2f)", position(world, load).x));
    const float stoppedAt = position(world, pusher).x;
    runSeconds(world, 1);
    check(near(position(world, pusher).x, stoppedAt, 1e-4f), "kinematic: with no more targets it stops");

    // A target is a pose of the frame, with a rotation, also when the centre of mass is elsewhere.
    BodyDesc offset = k;
    offset.mass = 2;
    offset.inertia = {1, 1, 1};
    offset.massLocalPose = at({1, 0, 0});
    const BodyId swung = world.addBody(offset);
    const Mat4 target = placed(Mat4::rotationY(0.5f) * Mat4::rotationX(-0.3f), {3, 1, 2});
    world.setKinematicTarget(swung, target);
    world.step(kStep);
    check(near(world.pose(swung), target, 1e-4f),
          "kinematic: the frame reaches a target with rotation, centre of mass offset or not");

    // A target that arrives between fixed steps waits for the next one.
    PhysicsWorld slow(kGravity, settingsFor());
    const BodyId moved = slow.addBody(k);
    slow.setKinematicTarget(moved, at({1, 0, 0}));
    check(slow.step(kStep * 0.5f) == 0 && near(position(slow, moved), {0, 0, 0}, 1e-6f),
          "kinematic: the target is not applied before a fixed step runs");
    slow.step(kStep * 0.5f);
    check(near(position(slow, moved), {1, 0, 0}, 1e-4f), "kinematic: and it is applied by the step that runs");

    // A target on a body that is not kinematic is ignored.
    const BodyId notKinematic = slow.addBody(floating(at({0, 10, 0}), boxShape({0.5f, 0.5f, 0.5f})));
    slow.setKinematicTarget(notKinematic, at({5, 10, 0}));
    runSeconds(slow, 0.2f);
    check(near(position(slow, notKinematic), {0, 10, 0}, 1e-4f),
          "kinematic: setKinematicTarget on a dynamic body does nothing");
}

void testMotionTypeAndTeleport() {
    PhysicsWorld world(kGravity, settingsFor());
    BodyDesc k = dynamicBody(at({0, 10, 0}), boxShape({0.5f, 0.5f, 0.5f}));
    k.motion = MotionType::Kinematic;
    const BodyId brick = world.addBody(k);
    const BodyId fixedBrick = world.addBody(staticBody(at({5, 10, 0}), boxShape({0.5f, 0.5f, 0.5f})));
    runSeconds(world, 0.5f);
    check(near(position(world, brick), {0, 10, 0}, 1e-5f) && near(position(world, fixedBrick), {5, 10, 0}, 1e-5f),
          "motion: kinematic and static bodies do not fall");
    check(world.motionType(brick) == MotionType::Kinematic && world.motionType(fixedBrick) == MotionType::Static,
          "motion: types are reported");

    world.setMotionType(brick, MotionType::Dynamic);
    world.setMotionType(fixedBrick, MotionType::Dynamic);
    runSeconds(world, 1);
    check(world.motionType(brick) == MotionType::Dynamic && position(world, brick).y < 6,
          "motion: a kinematic brick that turns dynamic falls");
    check(position(world, fixedBrick).y < 6, "motion: so does a static one");

    world.setMotionType(brick, MotionType::Static);
    const Vec3 frozen = position(world, brick);
    runSeconds(world, 0.5f);
    check(near(position(world, brick), frozen, 1e-5f) && near(world.linearVelocity(brick), {0, 0, 0}, 1e-6f),
          "motion: a body made static stops");

    // setPose teleports (and wakes), velocity is kept.
    const BodyId ball = world.addBody(dynamicBody(at({20, 3, 0}), sphereShape(0.5f)));
    world.setPose(ball, placed(Mat4::rotationY(1), {-20, 8, 4}));
    check(near(world.pose(ball), placed(Mat4::rotationY(1), {-20, 8, 4}), 1e-5f),
          "teleport: setPose moves the body at once");
    runSeconds(world, 0.1f);
    check(near(position(world, ball).x, -20, 1e-3f) && position(world, ball).y < 8,
          "teleport: and it carries on from there");
}

// ---- Joints --------------------------------------------------------------------------------------

JointDesc jointBetween(JointType type, BodyId a, BodyId b, Vec3 anchorA, Vec3 anchorB) {
    JointDesc j;
    j.type = type;
    j.bodyA = a;
    j.bodyB = b;
    j.localAnchor[0] = anchorA;
    j.localAnchor[1] = anchorB;
    return j;
}

void testFixedJointBreaks() {
    // A box of the given mass on a fixed joint under a ceiling that breaks at the given force.
    struct Outcome {
        bool broke = false;
        float dropped = 0;
    };
    const auto hang = [](float mass, float breakForce, int collisionSteps) {
        PhysicsSettings s = settingsFor();
        s.collisionSteps = collisionSteps;
        PhysicsWorld world(kGravity, s);
        const BodyId ceiling = world.addBody(staticBody(at({0, 10, 0}), boxShape({1, 0.25f, 1})));
        BodyDesc d = dynamicBody(at({0, 9.5f, 0}), boxShape({0.25f, 0.25f, 0.25f}));
        d.mass = mass;
        const BodyId load = world.addBody(d);
        JointDesc j = jointBetween(JointType::Fixed, ceiling, load, {0, -0.25f, 0}, {0, 0.25f, 0});
        j.breakForce = breakForce;
        const JointId joint = world.addJoint(j);
        check(joint != 0 && world.jointCount() == 1, "fixed joint: added");
        std::vector<JointId> reported;
        for (int i = 0; i < 180; ++i) {
            world.step(kStep);
            for (JointId id : world.takeBrokenJoints()) {
                reported.push_back(id);
            }
        }
        check(reported.empty() || (reported.size() == 1 && reported[0] == joint),
              "fixed joint: a break is reported once, with its id");
        check(world.takeBrokenJoints().empty(), "fixed joint: and not again");
        check(world.jointCount() == (reported.empty() ? 1u : 0u), "fixed joint: a broken joint is gone");
        return Outcome{!reported.empty(), 9.5f - position(world, load).y};
    };
    Outcome light = hang(1, 60, 1);
    check(!light.broke && light.dropped < 0.01f,
          str("fixed joint: holds a 10 N load against 60 N (sagged %.4f m)", light.dropped));
    Outcome heavy = hang(20, 60, 1);
    check(heavy.broke && heavy.dropped > 5,
          str("fixed joint: breaks under a 196 N load, which then falls (%.1f m)", heavy.dropped));
    // The impulse of a solver step is divided by the length of that step, however many there are in a fixed one.
    for (const int collisionSteps : {2, 4}) {
        check(!hang(12, 150, collisionSteps).broke,
              "fixed joint: holds 118 N against 150 N with several collision steps");
        check(hang(20, 150, collisionSteps).broke,
              "fixed joint: breaks under 196 N against 150 N with several collision steps");
    }

    // Torque: a beam out of a wall carries 1 kg at 1 m (about 10 N m) but not 10 kg against 30 N m.
    const auto beam = [](float mass, bool& snapped) {
        PhysicsWorld world(kGravity, settingsFor());
        const BodyId wall = world.addBody(staticBody(at({-0.5f, 0, 0}), boxShape({0.5f, 1, 1})));
        BodyDesc d = dynamicBody(at({1, 0, 0}), boxShape({1, 0.1f, 0.1f}));
        d.mass = mass;
        const BodyId arm = world.addBody(d);
        JointDesc j = jointBetween(JointType::Fixed, wall, arm, {0.5f, 0, 0}, {-1, 0, 0});
        j.breakTorque = 30;
        const JointId joint = world.addJoint(j);
        runSeconds(world, 2);
        const auto broken = world.takeBrokenJoints();
        snapped = !broken.empty();
        check(broken.empty() || broken[0] == joint, "fixed joint: torque break reports the joint");
    };
    bool snapped = false;
    beam(1, snapped);
    check(!snapped, "fixed joint: holds a light beam against 30 N m");
    beam(10, snapped);
    check(snapped, "fixed joint: breaks under a heavy beam");
}

// The angle of a bar about z, from the direction its local x axis points in.
float barAngle(const PhysicsWorld& world, BodyId bar) {
    const Mat4 m = world.pose(bar);
    return std::atan2(m.m[0][1], m.m[0][0]);
}

struct Pendulum {
    PhysicsWorld world;
    BodyId pivot = 0, bar = 0;
    JointId joint = 0;

    // A 2 m bar hinged at its left end to a pivot at (0,5,0), swinging about z. angle0 is where it starts. The bar is
    // the joint's body A (the hinge angle is the bar's, as in PhysX), or its body B with barIsB.
    // Not "const JointDesc& hinge = {}": GCC 13 cannot list-initialise JointDesc in a default argument.
    Pendulum() : Pendulum(JointDesc()) {}
    explicit Pendulum(const JointDesc& hinge, float angle0 = 0, bool gravity = true, bool canSleep = true,
                      bool barIsB = false)
        : world(kGravity, settingsFor()) {
        pivot = world.addBody(staticBody(at({0, 5, 0}), boxShape({0.2f, 0.2f, 0.2f})));
        const Mat4 rotation = Mat4::rotationZ(angle0);
        const Vec3 centre = transformVector({1, 0, 0}, rotation) + Vec3{0, 5, 0};
        BodyDesc d = dynamicBody(placed(rotation, centre), boxShape({1, 0.05f, 0.05f}));
        d.mass = 1;
        d.gravity = gravity;
        d.angularDamping = 0;
        d.sleepLinearVelocity = canSleep ? 0.15f : 0;
        bar = world.addBody(d);
        JointDesc j = hinge;
        j.type = JointType::Hinge;
        j.bodyA = barIsB ? pivot : bar;
        j.bodyB = barIsB ? bar : pivot;
        j.localAnchor[barIsB ? 0 : 1] = {0, 0, 0};
        j.localAnchor[barIsB ? 1 : 0] = {-1, 0, 0};
        j.localAxis[0] = j.localAxis[1] = {0, 0, 1};
        j.localNormal[0] = j.localNormal[1] = {1, 0, 0};
        joint = world.addJoint(j);
    }
};

void testHinge() {
    {
        Pendulum p;
        check(p.joint != 0, "hinge: added");
        float worstPivotError = 0;
        for (int i = 0; i < 120; ++i) {
            p.world.step(kStep);
            const Vec3 pivotEnd = transformPoint({-1, 0, 0}, p.world.pose(p.bar));
            worstPivotError = std::max(worstPivotError, length(pivotEnd - Vec3{0, 5, 0}));
        }
        check(worstPivotError < 0.01f, str("hinge: the bar stays on its pivot (%.4f m off)", worstPivotError));
        const Vec3 w = p.world.angularVelocity(p.bar);
        check(std::fabs(w.x) < 0.01f && std::fabs(w.y) < 0.01f && std::fabs(w.z) > 0.5f,
              "hinge: it swings about the z axis only");
    }
    {
        // Released from horizontal it falls towards -y: a negative turn about +z.
        Pendulum p;
        runSeconds(p.world, 0.3f);
        check(p.world.angularVelocity(p.bar).z < -1 && barAngle(p.world, p.bar) < -0.1f,
              "hinge: a bar on +x falls with omega_z < 0");
    }
    {
        // Low limit at -0.8 rad: the bar falls to it and stays.
        JointDesc h;
        h.limitEnabled = true;
        h.limitLow = {-0.8f, 0};
        h.limitHigh = {0.8f, 0};
        Pendulum p(h);
        float lowest = 0;
        for (int i = 0; i < 240; ++i) {
            p.world.step(kStep);
            lowest = std::min(lowest, barAngle(p.world, p.bar));
        }
        check(near(barAngle(p.world, p.bar), -0.8f, 0.05f) && lowest > -0.88f,
              str("hinge: stops at its low limit -0.8 (at %.3f, lowest %.3f)", barAngle(p.world, p.bar), lowest));
        check(std::fabs(p.world.angularVelocity(p.bar).z) < 0.3f, "hinge: and rests there");
    }
    {
        // Thrown the other way with no gravity it stops at the high limit.
        JointDesc h;
        h.limitEnabled = true;
        h.limitLow = {-0.8f, 0};
        h.limitHigh = {0.8f, 0};
        Pendulum p(h, 0, false);
        p.world.setAngularVelocity(p.bar, {0, 0, 3});
        float highest = 0;
        for (int i = 0; i < 180; ++i) {
            p.world.step(kStep);
            highest = std::max(highest, barAngle(p.world, p.bar));
        }
        check(near(barAngle(p.world, p.bar), 0.8f, 0.05f) && highest < 0.88f,
              str("hinge: stops at its high limit +0.8 (at %.3f)", barAngle(p.world, p.bar)));
    }
    {
        // Limits that do not straddle zero: 0.3 .. 0.9, the bar starting at 0.6 and falling to the low end.
        JointDesc h;
        h.limitEnabled = true;
        h.limitLow = {0.3f, 0};
        h.limitHigh = {0.9f, 0};
        Pendulum p(h, 0.6f);
        float lowest = 1;
        for (int i = 0; i < 240; ++i) {
            p.world.step(kStep);
            lowest = std::min(lowest, barAngle(p.world, p.bar));
        }
        check(near(barAngle(p.world, p.bar), 0.3f, 0.05f) && lowest > 0.22f,
              str("hinge: limits 0.3..0.9 are measured from the normals too (stopped at %.3f)",
                  barAngle(p.world, p.bar)));
        Pendulum up(h, 0.6f, false);
        up.world.setAngularVelocity(up.bar, {0, 0, 2});
        runSeconds(up.world, 3);
        check(near(barAngle(up.world, up.bar), 0.9f, 0.05f),
              str("hinge: and to the high end (%.3f)", barAngle(up.world, up.bar)));
    }
    {
        // Motor at 1.5 rad/s with plenty of torque.
        JointDesc h;
        h.motorEnabled = true;
        h.motorVelocity = 1.5f;
        h.motorMaxForce = 1e4f;
        Pendulum p(h, 0, false);
        runSeconds(p.world, 1);
        check(near(p.world.angularVelocity(p.bar).z, 1.5f, 0.08f),
              str("hinge: the motor turns it at 1.5 rad/s (%.3f)", p.world.angularVelocity(p.bar).z));
        check(near(barAngle(p.world, p.bar), 1.5f, 0.12f),
              str("hinge: so after a second it is at 1.5 rad (%.3f)", barAngle(p.world, p.bar)));
        h.motorVelocity = -1.5f;
        Pendulum back(h, 0, false);
        runSeconds(back.world, 1);
        check(near(back.world.angularVelocity(back.bar).z, -1.5f, 0.08f), "hinge: and a negative target the other way");
        // The motor turns body A forwards: with the bar as body B it turns the other way (PhysX's windmills do).
        h.motorVelocity = 1.5f;
        Pendulum asB(h, 0, false, true, true);
        runSeconds(asB.world, 1);
        check(near(asB.world.angularVelocity(asB.bar).z, -1.5f, 0.08f),
              str("hinge: a positive target turns body A forwards and body B backwards (%.3f)",
                  asB.world.angularVelocity(asB.bar).z));
    }
    {
        // A weak motor cannot lift the bar against gravity.
        JointDesc h;
        h.motorEnabled = true;
        h.motorVelocity = 3;
        h.motorMaxForce = 1;
        Pendulum p(h);
        runSeconds(p.world, 1);
        check(barAngle(p.world, p.bar) < 0, "hinge: a motor limited to 1 N m loses to a bar that pulls with 9.8 N m");
    }
    {
        // Spring towards 0.5 rad.
        JointDesc h;
        h.springEnabled = true;
        h.spring = 50;
        h.damper = 10;
        h.springTarget = 0.5f;
        Pendulum p(h, 0, false, false);
        runSeconds(p.world, 5);
        check(near(barAngle(p.world, p.bar), 0.5f, 0.005f),
              str("hinge: the spring settles at its target (%.4f)", barAngle(p.world, p.bar)));
        Pendulum drooping(h, 0, true, false);
        runSeconds(drooping.world, 5);
        float balance = 0.5f;  // k (0.5 - a) = m g L cos(a), with k = 50, m g = 9.8 and the bar's centre 1 m out
        for (int i = 0; i < 50; ++i) {
            balance = 0.5f - 9.8f * std::cos(balance) / 50;
        }
        check(near(barAngle(drooping.world, drooping.bar), balance, 0.01f),
              str("hinge: with gravity pulling, the spring balances it at %.3f",
                  barAngle(drooping.world, drooping.bar)));
    }
}

void testBallJoint() {
    // A rod hanging from a pivot at (0,5,0); the swing cone is about the rod's axis, straight down.
    const auto make = [](PhysicsWorld& world, bool swing, bool twist, BodyId& rod) {
        const BodyId pivot = world.addBody(staticBody(at({0, 5, 0}), boxShape({0.2f, 0.2f, 0.2f})));
        BodyDesc d = dynamicBody(at({0, 4, 0}), boxShape({0.1f, 1, 0.1f}));
        d.mass = 1;
        d.angularDamping = 0;
        d.maxAngularVelocity = 30;
        rod = world.addBody(d);
        JointDesc j = jointBetween(JointType::Ball, pivot, rod, {0, 0, 0}, {0, 1, 0});
        j.localAxis[0] = j.localAxis[1] = {0, -1, 0};
        j.localNormal[0] = j.localNormal[1] = {1, 0, 0};
        j.swingLimitEnabled = swing;
        j.swingLimit = {0.5f, 0};
        j.limitEnabled = twist;
        j.limitLow = {-0.3f, 0};
        j.limitHigh = {0.3f, 0};
        return world.addJoint(j);
    };
    const auto swingAngle = [](const PhysicsWorld& world, BodyId rod) {
        const Vec3 axis = transformVector({0, -1, 0}, world.pose(rod));
        return std::acos(std::clamp(-axis.y, -1.0f, 1.0f));
    };
    {
        PhysicsWorld world(kGravity, settingsFor());
        BodyId rod = 0;
        check(make(world, true, false, rod) != 0, "ball: swing-limited joint added");
        world.setAngularVelocity(rod, {0, 0, 12});
        float widest = 0, drift = 0;
        for (int i = 0; i < 240; ++i) {
            world.step(kStep);
            widest = std::max(widest, swingAngle(world, rod));
            drift = std::max(drift, length(transformPoint({0, 1, 0}, world.pose(rod)) - Vec3{0, 5, 0}));
        }
        check(widest < 0.56f && widest > 0.42f,
              str("ball: the swing stays inside the 0.5 rad cone (widest %.3f)", widest));
        check(drift < 0.02f, str("ball: and the rod stays on its pivot (%.4f m off)", drift));
    }
    {
        // No limit at all: a point joint, free to swing wide.
        PhysicsWorld world(kGravity, settingsFor());
        BodyId rod = 0;
        make(world, false, false, rod);
        world.setAngularVelocity(rod, {0, 0, 12});
        float widest = 0, drift = 0;
        for (int i = 0; i < 120; ++i) {
            world.step(kStep);
            widest = std::max(widest, swingAngle(world, rod));
            drift = std::max(drift, length(transformPoint({0, 1, 0}, world.pose(rod)) - Vec3{0, 5, 0}));
        }
        check(widest > 1.0f, str("ball: without limits it swings freely (%.3f)", widest));
        check(drift < 0.01f, str("ball: and still hangs from its pivot (%.4f m off)", drift));
    }
    {
        // Twist limit +-0.3 about the rod's axis; spun about it, the rod stops.
        PhysicsWorld world(kGravity, settingsFor());
        BodyId rod = 0;
        make(world, false, true, rod);
        world.setAngularVelocity(rod, {0, 2, 0});
        float most = 0;
        for (int i = 0; i < 120; ++i) {
            world.step(kStep);
            const Vec3 x = transformVector({1, 0, 0}, world.pose(rod));
            most = std::max(most, std::fabs(std::atan2(-x.z, x.x)));
        }
        check(most > 0.25f && most < 0.38f, str("ball: twist is limited to 0.3 rad (most %.3f)", most));
    }
}

void testJointOnOffsetCentreOfMass() {
    // The frame is the hinge's, the centre of mass 0.7 m along the bar: the anchor must stay put anyway.
    PhysicsWorld world(kGravity, settingsFor());
    const BodyId pivot = world.addBody(staticBody(at({0, 5, 0}), boxShape({0.2f, 0.2f, 0.2f})));
    BodyDesc d = dynamicBody(at({0, 5, 0}), boxShape({0.5f, 0.05f, 0.05f}));
    d.shapes[0].localPose = at({0.5f, 0, 0});
    d.mass = 2;
    d.inertia = {0.1f, 0.1f, 0.5f};
    d.massLocalPose = at({0.7f, 0, 0});
    d.angularDamping = 0;
    const BodyId bar = world.addBody(d);
    check(near(world.pose(bar), at({0, 5, 0}), 1e-6f), "offset COM joint: pose() is the frame");
    world.setPose(bar, placed(Mat4::rotationZ(0.4f), {0, 5, 0}));
    check(near(world.pose(bar), placed(Mat4::rotationZ(0.4f), {0, 5, 0}), 1e-5f),
          "offset COM joint: setPose takes and pose() returns the frame too");
    world.setPose(bar, at({0, 5, 0}));
    // The anchor is 0.2 m along the bar's x, so 0.5 m from the centre of mass; in the world it is (0.2,5,0).
    JointDesc j = jointBetween(JointType::Hinge, pivot, bar, {0.2f, 0, 0}, {0.2f, 0, 0});
    j.localAxis[0] = j.localAxis[1] = {0, 0, 1};
    j.localNormal[0] = j.localNormal[1] = {1, 0, 0};
    check(world.addJoint(j) != 0, "offset COM joint: added");
    const Vec3 anchorWorld0 = transformPoint({0.2f, 0, 0}, world.pose(bar));
    // Angular acceleration about the pivot: torque m g d / (I_com + m d^2), d = 0.5.
    runSeconds(world, 0.1f);
    const float alpha = 2 * 9.8f * 0.5f / (0.5f + 2 * 0.25f);
    check(within(-world.angularVelocity(bar).z, alpha * 0.1f, 0.10f),
          str("offset COM joint: swings about the centre of mass's lever (omega %.3f, expected %.3f)",
              world.angularVelocity(bar).z, -alpha * 0.1f));
    float drift = 0;
    for (int i = 0; i < 90; ++i) {
        world.step(kStep);
        drift = std::max(drift, length(transformPoint({0.2f, 0, 0}, world.pose(bar)) - anchorWorld0));
    }
    check(drift < 0.01f, str("offset COM joint: the anchor stays where it was given (%.4f m off)", drift));
}

void testJointsWithWorldAndCollisions() {
    {
        // A joint to the world (body 0): the frame is in world space.
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = dynamicBody(at({1, 5, 0}), boxShape({0.5f, 0.1f, 0.1f}));
        d.mass = 1;
        const BodyId bar = world.addBody(d);
        JointDesc j = jointBetween(JointType::Hinge, 0, bar, {0.5f, 5, 0}, {-0.5f, 0, 0});
        j.localAxis[0] = j.localAxis[1] = {0, 0, 1};
        j.localNormal[0] = j.localNormal[1] = {1, 0, 0};
        check(world.addJoint(j) != 0, "world joint: added");
        runSeconds(world, 0.2f);
        check(world.angularVelocity(bar).z < -0.5f, "world joint: the bar falls about z");
        float drift = 0;
        for (int i = 0; i < 60; ++i) {
            world.step(kStep);
            drift = std::max(drift, length(transformPoint({-0.5f, 0, 0}, world.pose(bar)) - Vec3{0.5f, 5, 0}));
        }
        check(drift < 0.01f, str("world joint: and it stays on a point fixed in the world (%.4f m off)", drift));
    }
    {
        // collideConnected: two overlapping boxes. Off: no contacts. On: contacts.
        for (const bool collide : {false, true}) {
            PhysicsWorld world(kGravity, settingsFor());
            const BodyId a = world.addBody(floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f})));
            const BodyId b = world.addBody(floating(at({0.6f, 0, 0}), boxShape({0.5f, 0.5f, 0.5f})));
            JointDesc j = jointBetween(JointType::Ball, a, b, {0.3f, 0, 0}, {-0.3f, 0, 0});
            j.collideConnected = collide;
            check(world.addJoint(j) != 0, "collideConnected: joint added");
            runSeconds(world, 0.2f);
            bool touched = false;
            for (const ContactEvent& e : world.takeContacts()) {
                touched = touched || (e.a == a && e.b == b) || (e.a == b && e.b == a);
            }
            check(touched == collide, collide ? "collideConnected: true lets the pair collide"
                                              : "collideConnected: false keeps the pair from colliding");
        }
    }
    {
        // Removing the joint lets the pair collide again.
        PhysicsWorld world(kGravity, settingsFor());
        const BodyId a = world.addBody(floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f})));
        const BodyId b = world.addBody(floating(at({0.6f, 0, 0}), boxShape({0.5f, 0.5f, 0.5f})));
        const JointId joint = world.addJoint(jointBetween(JointType::Ball, a, b, {0.3f, 0, 0}, {-0.3f, 0, 0}));
        runSeconds(world, 0.1f);
        world.removeJoint(joint);
        check(world.jointCount() == 0, "removeJoint: gone");
        // (One position iteration per step pushes an overlap of 0.4 apart at 0.01 per step, and they fall asleep first.)
        runSeconds(world, 0.3f);
        check(length(position(world, b) - position(world, a)) > 0.7f,
              "removeJoint: the boxes now push each other apart");
    }
}

void testJointsWakeTheirPartners() {
    PhysicsWorld world(kGravity, settingsFor());
    std::vector<BodyId> chain;
    for (int i = 0; i < 6; ++i) {
        BodyDesc d = dynamicBody(at({static_cast<float>(i) * 1.2f, 10, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.startAsleep = true;
        d.gravity = false;
        chain.push_back(world.addBody(d));
    }
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        world.addJoint(jointBetween(JointType::Fixed, chain[i], chain[i + 1], {0.6f, 0, 0}, {-0.6f, 0, 0}));
    }
    bool allAsleep = true;
    for (BodyId b : chain) {
        allAsleep = allAsleep && !world.isAwake(b);
    }
    check(allAsleep, "wake chain: starts asleep");
    world.wake(chain[0]);
    bool allAwake = true;
    for (BodyId b : chain) {
        allAwake = allAwake && world.isAwake(b);
    }
    check(allAwake, "wake chain: waking one body wakes everything welded to it");

    // And a hit on the end of a chain moves the whole thing.
    PhysicsWorld hit(kGravity, settingsFor());
    BodyId first = 0, last = 0;
    for (int i = 0; i < 3; ++i) {
        BodyDesc d = dynamicBody(at({static_cast<float>(i) * 1.2f, 10, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.startAsleep = true;
        d.gravity = false;
        const BodyId b = hit.addBody(d);
        if (i == 0) {
            first = b;
        } else {
            hit.addJoint(jointBetween(JointType::Fixed, last, b, {0.6f, 0, 0}, {-0.6f, 0, 0}));
        }
        last = b;
    }
    BodyDesc ball = dynamicBody(at({-3, 10, 0}), sphereShape(0.3f));
    ball.gravity = false;
    ball.density = 50;
    ball.linearVelocity = {8, 0, 0};
    hit.addBody(ball);
    runSeconds(hit, 1);
    check(position(hit, last).x > 2.4f + 0.2f && position(hit, first).x > 0.2f,
          "wake chain: a hit on one end moves the chain");
}

// ---- Contacts, queries ---------------------------------------------------------------------------

void testContactEvents() {
    PhysicsWorld world(kGravity, settingsFor());
    BodyDesc g = staticBody(at({0, -0.5f, 0}), boxShape({10, 0.5f, 10}));
    g.userData = 99;
    const BodyId ground = world.addBody(g);
    BodyDesc b = dynamicBody(at({0, 2.25f, 0}), sphereShape(0.25f));
    b.userData = 77;
    const BodyId ball = world.addBody(b);

    std::optional<ContactEvent> hit;
    for (int i = 0; i < 90 && !hit; ++i) {
        world.step(kStep);
        for (const ContactEvent& e : world.takeContacts()) {
            if ((e.a == ball && e.b == ground) || (e.a == ground && e.b == ball)) {
                hit = e;
                break;
            }
        }
    }
    check(hit.has_value(), "contact: the landing is reported");
    if (!hit) {
        return;
    }
    check(hit->userDataA == (hit->a == ball ? 77u : 99u) && hit->userDataB == (hit->b == ball ? 77u : 99u),
          "contact: user data of both bodies");
    const Vec3 fromAToB = position(world, hit->b) - position(world, hit->a);
    check(dot(hit->normal, fromAToB) > 0 && near(std::fabs(hit->normal.y), 1, 1e-3f),
          "contact: the normal points from a to b");
    check(hit->a == ball ? hit->normal.y < 0 : hit->normal.y > 0, "contact: down from the ball, up from the floor");
    const float impact = std::sqrt(2 * 9.8f * 2.0f);
    check(within(hit->normalSpeed, impact, 0.08f),
          str("contact: closing speed %.2f vs impact speed %.2f", hit->normalSpeed, impact));
    check(near(hit->point.y, 0, 0.03f) && std::fabs(hit->point.x) < 0.05f, "contact: the point is where they touch");
    check(world.takeContacts().empty(), "contact: events are handed over once");
}

void testRaycast() {
    PhysicsWorld world(kGravity, settingsFor());
    BodyDesc d = staticBody(at({0, 0, 10}), boxShape({1, 1, 1}));
    d.userData = 5;
    const BodyId wall = world.addBody(d);
    const auto hit = world.raycast({0, 0, 0}, {0, 0, 3}, 20);  // the direction need not be a unit vector
    check(hit.has_value() && hit->body == wall, "raycast: hits the box");
    if (hit) {
        check(near(hit->distance, 9, 1e-3f) && near(hit->point, {0, 0, 9}, 1e-3f) &&
                  near(hit->normal, {0, 0, -1}, 1e-3f),
              "raycast: distance, point and normal of the face");
    }
    check(world.userData(wall) == 5, "raycast: user data of the body hit");
    check(!world.raycast({0, 0, 0}, {0, 0, 1}, 5).has_value(), "raycast: too short to reach");
    check(!world.raycast({0, 0, 0}, {0, 1, 0}, 50).has_value(), "raycast: misses");
    check(!world.raycast({0, 0, 0}, {0, 0, 0}, 50).has_value(), "raycast: no direction, no hit");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(!world.raycast({nan, 0, 0}, {0, 0, 1}, 50).has_value() &&
              !world.raycast({0, 0, 0}, {0, 0, 1}, nan).has_value(),
          "raycast: NaN gives no hit");

    const BodyId ball = world.addBody(floating(at({0, 0, 5}), sphereShape(1)));
    const auto nearest = world.raycast({0, 0, 0}, {0, 0, 1}, 50);
    check(nearest.has_value() && nearest->body == ball && near(nearest->distance, 4, 1e-3f),
          "raycast: the closest of two is the one hit");
    world.removeBody(ball);
    const auto after = world.raycast({0, 0, 0}, {0, 0, 1}, 50);
    check(after.has_value() && after->body == wall, "raycast: a removed body is not hit");
}

// ---- Safety --------------------------------------------------------------------------------------

void testUnknownAndRemovedIds() {
    PhysicsWorld world(kGravity, settingsFor());
    const BodyId a = world.addBody(floating(at({0, 5, 0}), boxShape({0.5f, 0.5f, 0.5f})));
    const BodyId b = world.addBody(floating(at({2, 5, 0}), boxShape({0.5f, 0.5f, 0.5f})));
    const JointId joint = world.addJoint(jointBetween(JointType::Fixed, a, b, {1, 0, 0}, {-1, 0, 0}));
    check(world.bodyCount() == 2 && world.jointCount() == 1, "ids: counts");

    // Removing a body takes its joints with it.
    world.removeBody(a);
    check(world.bodyCount() == 1 && world.jointCount() == 0, "ids: removing a body removes its joints");
    world.removeJoint(joint);  // already gone
    world.removeBody(a);       // already gone
    runSeconds(world, 0.1f);
    check(near(position(world, b), {2, 5, 0}, 0.01f), "ids: the other body carries on");

    const BodyId none = 0, unknown = 424242;
    for (const BodyId id : {none, unknown, a}) {
        world.removeBody(id);
        world.setLinearVelocity(id, {1, 2, 3});
        world.setAngularVelocity(id, {1, 2, 3});
        world.addImpulse(id, {1, 0, 0}, {0, 0, 0});
        world.setPose(id, at({1, 1, 1}));
        world.setKinematicTarget(id, at({1, 1, 1}));
        world.setMotionType(id, MotionType::Dynamic);
        world.wake(id);
        check(near(world.pose(id), Mat4::identity(), 0) && near(world.linearVelocity(id), {0, 0, 0}, 0) &&
                  near(world.angularVelocity(id), {0, 0, 0}, 0) && !world.isAwake(id) && world.userData(id) == 0 &&
                  world.motionType(id) == MotionType::Static && world.mass(id) == 0,
              "ids: reading an unknown body gives zeros");
    }
    world.removeJoint(0);
    world.removeJoint(777);
    check(world.addJoint(jointBetween(JointType::Fixed, a, b, {}, {})) == 0,
          "ids: a joint to a removed body is refused");
    check(world.addJoint(jointBetween(JointType::Fixed, 0, 0, {}, {})) == 0, "ids: a joint to nothing is refused");
    check(world.addJoint(jointBetween(JointType::Fixed, b, b, {}, {})) == 0, "ids: a joint to itself is refused");
    check(world.bodyCount() == 1 && world.jointCount() == 0, "ids: and nothing was added");

    // Joint on a body that is removed while the joint is the only thing between it and a pivot.
    const BodyId c = world.addBody(floating(at({6, 5, 0}), boxShape({0.5f, 0.5f, 0.5f})));
    world.addJoint(jointBetween(JointType::Ball, b, c, {1, 0, 0}, {-1, 0, 0}));
    world.removeBody(b);
    world.step(kStep);
    check(world.bodyCount() == 1 && world.jointCount() == 0, "ids: removing the first body of a joint, too");

    // Ids are not reused.
    const BodyId d = world.addBody(floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f})));
    check(d != a && d != b && d != c, "ids: a removed body's id is not handed out again");

    // A world with live bodies and joints goes away without ceremony.
    PhysicsWorld busy(kGravity, settingsFor());
    BodyId prev = 0;
    for (int i = 0; i < 50; ++i) {
        const BodyId body = busy.addBody(dynamicBody(at({static_cast<float>(i), 5, 0}), boxShape({0.4f, 0.4f, 0.4f})));
        if (prev != 0) {
            busy.addJoint(jointBetween(JointType::Hinge, prev, body, {0.5f, 0, 0}, {-0.5f, 0, 0}));
        }
        prev = body;
    }
    runSeconds(busy, 0.2f);
}

void testDegenerateInput() {
    PhysicsWorld world(kGravity, settingsFor());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::pair<const char*, BodyDesc>> bad;
    bad.push_back({"zero box", dynamicBody(at({0, 0, 0}), boxShape({0, 1, 1}))});
    bad.push_back({"negative box", dynamicBody(at({0, 0, 0}), boxShape({1, -1, 1}))});
    bad.push_back({"NaN box", dynamicBody(at({0, 0, 0}), boxShape({1, nan, 1}))});
    bad.push_back({"zero sphere", dynamicBody(at({0, 0, 0}), sphereShape(0))});
    bad.push_back({"NaN sphere", dynamicBody(at({0, 0, 0}), sphereShape(nan))});
    PhysicsShape capsule;
    capsule.type = PhysicsShapeType::Capsule;
    capsule.radius = 0.5f;
    capsule.halfHeight = -1;
    bad.push_back({"negative capsule", dynamicBody(at({0, 0, 0}), capsule)});
    PhysicsShape hull;
    hull.type = PhysicsShapeType::ConvexHull;
    hull.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    bad.push_back({"three-point hull", dynamicBody(at({0, 0, 0}), hull)});
    hull.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    bad.push_back({"flat hull", dynamicBody(at({0, 0, 0}), hull)});
    hull.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, nan}};
    bad.push_back({"NaN hull", dynamicBody(at({0, 0, 0}), hull)});
    bad.push_back({"NaN pose", dynamicBody(at({nan, 0, 0}), boxShape({1, 1, 1}))});
    bad.push_back({"scaled pose", dynamicBody(Mat4::scaling({2, 2, 2}), boxShape({1, 1, 1}))});
    bad.push_back({"mirrored pose", dynamicBody(Mat4::scaling({-1, 1, 1}), boxShape({1, 1, 1}))});
    BodyDesc empty;
    bad.push_back({"no shapes", empty});
    BodyDesc nanVelocity = dynamicBody(at({0, 0, 0}), boxShape({1, 1, 1}));
    nanVelocity.linearVelocity = {0, nan, 0};
    bad.push_back({"NaN velocity", nanVelocity});
    BodyDesc nanMass = dynamicBody(at({0, 0, 0}), boxShape({1, 1, 1}));
    nanMass.mass = nan;
    bad.push_back({"NaN mass", nanMass});
    BodyDesc noMass = dynamicBody(at({0, 0, 0}), boxShape({1, 1, 1}));
    noMass.density = 0;
    bad.push_back({"neither mass nor density", noMass});
    BodyDesc goodAndBad = dynamicBody(at({0, 0, 0}), boxShape({1, 1, 1}));
    goodAndBad.shapes.push_back(boxShape({0, 0, 0}));
    bad.push_back({"one bad shape among good ones", goodAndBad});
    for (const auto& [name, desc] : bad) {
        check(world.addBody(desc) == 0, std::string("degenerate: ") + name + " is refused");
    }
    check(world.bodyCount() == 0, "degenerate: nothing was added");

    // And the world still works.
    const BodyId ok = world.addBody(dynamicBody(at({0, 0, 0}), boxShape({1, 1, 1})));
    check(ok != 0 && world.bodyCount() == 1, "degenerate: a good body is accepted afterwards");
    world.setLinearVelocity(ok, {nan, 0, 0});
    world.setPose(ok, at({0, nan, 0}));
    world.addImpulse(ok, {nan, 0, 0}, {0, 0, 0});
    runSeconds(world, 0.1f);
    check(std::isfinite(position(world, ok).y), "degenerate: NaN set later is ignored");

    // Joints with nothing usable in them.
    const BodyId other = world.addBody(dynamicBody(at({3, 0, 0}), boxShape({1, 1, 1})));
    const JointDesc hinge = jointBetween(JointType::Hinge, ok, other, {1.5f, 0, 0}, {-1.5f, 0, 0});
    JointDesc zeroAxis = hinge, nanAnchor = hinge, nanMotor = hinge, nanLimit = hinge, nanSwing = hinge;
    zeroAxis.localAxis[0] = {0, 0, 0};
    nanAnchor.localAnchor[1] = {nan, 0, 0};
    nanMotor.motorEnabled = true;
    nanMotor.motorVelocity = nan;
    nanLimit.limitEnabled = true;
    nanLimit.limitHigh = {nan, 0};
    nanSwing.type = JointType::Ball;
    nanSwing.swingLimitEnabled = true;
    nanSwing.swingLimit = {nan, 0};
    for (const JointDesc& j : {zeroAxis, nanAnchor, nanMotor, nanLimit, nanSwing}) {
        check(world.addJoint(j) == 0, "degenerate: a joint with an unusable value is refused");
    }
    check(world.jointCount() == 0 && world.addJoint(hinge) != 0,
          "degenerate: and a good joint between the same bodies is accepted");
}

void testLimitsAndLifetime() {
    PhysicsSettings small = settingsFor();
    small.maxBodies = 4;
    PhysicsWorld world(kGravity, small);
    BodyId ids[5];
    for (BodyId& id : ids) {
        id = world.addBody(floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f})));
    }
    check(ids[0] != 0 && ids[3] != 0 && ids[4] == 0 && world.bodyCount() == 4,
          "limits: the body past maxBodies is refused");
    world.removeBody(ids[0]);
    check(world.addBody(floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}))) != 0,
          "limits: and room comes back when one is removed");

    // Worlds come and go: the process-wide setup outlives any one of them and starts over when none is left.
    {
        auto first = std::make_unique<PhysicsWorld>(kGravity, settingsFor());
        auto second = std::make_unique<PhysicsWorld>(kGravity, settingsFor(2));
        const BodyId a = first->addBody(dynamicBody(at({0, 5, 0}), boxShape({0.5f, 0.5f, 0.5f})));
        first.reset();
        const BodyId b = second->addBody(dynamicBody(at({0, 5, 0}), boxShape({0.5f, 0.5f, 0.5f})));
        runSeconds(*second, 0.5f);
        check(a != 0 && b != 0 && position(*second, b).y < 4.5f,
              "lifetime: a world keeps working when another is destroyed");
    }
    for (int round = 0; round < 3; ++round) {
        PhysicsWorld again(kGravity, settingsFor());
        const BodyId body = again.addBody(dynamicBody(at({0, 5, 0}), boxShape({0.5f, 0.5f, 0.5f})));
        runSeconds(again, 0.5f);
        check(body != 0 && position(again, body).y < 4.5f, "lifetime: a world can be made after all others are gone");
    }
}

void testStepping() {
    PhysicsSettings s = settingsFor();
    s.maxStepsPerCall = 3;
    PhysicsWorld world(kGravity, s);
    check(world.step(kStep) == 1, "step: one fixed step of time is one step");
    check(world.step(kStep * 0.5f) == 0 && world.step(kStep * 0.5f) == 1, "step: half steps add up");
    check(world.step(kStep * 2.5f) == 2, "step: two and a half are two steps");
    check(world.step(kStep * 0.5f) == 1, "step: with the half carried over");
    check(world.step(kStep * 4.5f) == 3, "step: at most maxStepsPerCall steps");
    check(world.step(kStep * 0.25f) == 0 && world.step(kStep * 0.25f) == 1,
          "step: the time beyond that is dropped, the fraction of a step left over is kept");
    check(world.step(0) == 0 && world.step(-1) == 0 && world.step(std::numeric_limits<float>::quiet_NaN()) == 0,
          "step: nothing for no time");

    // Free fall for one second is a second of free fall, however the time is cut up.
    for (const float chunk : {kStep, kStep * 0.25f, kStep * 3.3f}) {
        PhysicsWorld w(kGravity, settingsFor());
        BodyDesc d = dynamicBody(at({0, 100, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.linearDamping = 0;
        const BodyId body = w.addBody(d);
        int steps = 0;
        for (float t = 0; t < 1.0f; t += chunk) {
            steps += w.step(chunk);
        }
        const float vy = w.linearVelocity(body).y;
        check(near(vy, -9.8f * kStep * static_cast<float>(steps), 1e-3f),
              "step: velocity follows the fixed steps taken");
    }
}

void testBodyFlags() {
    {
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = dynamicBody(at({0, 5, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.gravity = false;
        const BodyId floater = world.addBody(d);
        const BodyId faller = world.addBody(dynamicBody(at({3, 5, 0}), boxShape({0.5f, 0.5f, 0.5f})));
        runSeconds(world, 1);
        check(near(position(world, floater).y, 5, 1e-5f) && position(world, faller).y < 1,
              "flags: gravity off holds a body, on drops it");
    }
    {
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.linearDamping = 1;
        d.linearVelocity = {10, 0, 0};
        const BodyId body = world.addBody(d);
        runSeconds(world, 1);
        check(within(world.linearVelocity(body).x, 10 * std::exp(-1.0f), 0.05f),
              "flags: linear damping slows the body like exp(-c t)");
    }
    {
        PhysicsWorld world(kGravity, settingsFor());
        BodyDesc d = floating(at({0, 0, 0}), boxShape({0.5f, 0.5f, 0.5f}));
        d.maxAngularVelocity = 7;
        const BodyId body = world.addBody(d);
        world.setAngularVelocity(body, {0, 20, 0});
        runSeconds(world, 0.1f);
        check(length(world.angularVelocity(body)) <= 7.001f, "flags: angular velocity is capped at maxAngularVelocity");
    }
    {
        // A fast ball and a thin wall: only the body asking for continuous collision stops at it.
        for (const bool continuous : {false, true}) {
            PhysicsWorld world(kGravity, settingsFor());
            world.addBody(staticBody(at({10, 0, 0}), boxShape({0.02f, 5, 5})));
            BodyDesc d = floating(at({0, 0, 0}), sphereShape(0.05f));
            d.linearVelocity = {150, 0, 0};
            d.continuousCollision = continuous;
            d.density = 100;
            const BodyId ball = world.addBody(d);
            runSeconds(world, 0.5f);
            const bool through = position(world, ball).x > 10.1f;
            check(through != continuous, continuous ? "flags: continuous collision stops a fast ball at a thin wall"
                                                    : "flags: without it the same ball goes through");
        }
    }
}

// ---- Threads, scale ------------------------------------------------------------------------------

std::uint32_t nextRandom(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float unit(std::uint32_t& state) {
    return static_cast<float>(nextRandom(state) & 0xffff) / 65535.0f;
}

std::vector<float> runScene(int threads) {
    PhysicsWorld world(kGravity, settingsFor(threads));
    addGround(world, materialWith(0.6f, 0.2f));
    std::uint32_t seed = 12345;
    std::vector<BodyId> bodies;
    // A wall of boxes, loose boxes and balls dropped from different heights, a hinged pair.
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 8; ++col) {
            const Vec3 p{static_cast<float>(col) * 1.05f - 4, 0.5f + static_cast<float>(row), -3};
            bodies.push_back(world.addBody(dynamicBody(at(p), boxShape({0.5f, 0.5f, 0.5f}, materialWith(0.5f, 0.1f)))));
        }
    }
    for (int i = 0; i < 40; ++i) {
        const Vec3 p{unit(seed) * 8 - 4, 4 + unit(seed) * 10, unit(seed) * 6 - 1};
        const Mat4 rotation = Mat4::rotationX(unit(seed) * 3) * Mat4::rotationY(unit(seed) * 3);
        if (i % 2 == 0) {
            const PhysicsShape shape = boxShape({0.3f, 0.2f, 0.4f}, materialWith(0.4f, 0.3f));
            bodies.push_back(world.addBody(dynamicBody(placed(rotation, p), shape)));
        } else {
            bodies.push_back(world.addBody(dynamicBody(at(p), sphereShape(0.3f, materialWith(0.4f, 0.5f)))));
        }
    }
    BodyDesc bat = dynamicBody(at({0, 3, 5}), boxShape({1, 0.1f, 0.1f}));
    bat.mass = 2;
    const BodyId swing = world.addBody(bat);
    JointDesc hinge = jointBetween(JointType::Hinge, 0, swing, {-1, 3, 5}, {-1, 0, 0});
    hinge.localAxis[0] = hinge.localAxis[1] = {0, 0, 1};
    hinge.localNormal[0] = hinge.localNormal[1] = {1, 0, 0};
    world.addJoint(hinge);
    bodies.push_back(swing);
    runSeconds(world, 5);

    std::vector<float> state;
    for (BodyId b : bodies) {
        const Mat4 pose = world.pose(b);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 3; ++j) {
                state.push_back(pose.m[i][j]);
            }
        }
        const Vec3 v = world.linearVelocity(b);
        state.insert(state.end(), {v.x, v.y, v.z});
    }
    return state;
}

void testThreadsGiveTheSameResult() {
    const std::vector<float> single = runScene(0);
    const std::vector<float> pooled = runScene(4);
    const std::vector<float> again = runScene(4);
    const std::size_t bytes = single.size() * sizeof(float);
    check(single.size() == pooled.size() && std::memcmp(single.data(), pooled.data(), bytes) == 0,
          "threads: a scene ends up the same on the caller's thread and on four workers, bit for bit");
    check(pooled.size() == again.size() && std::memcmp(pooled.data(), again.data(), pooled.size() * sizeof(float)) == 0,
          "threads: and the same on a second run");
    float moved = 0;
    for (std::size_t i = 0; i < single.size(); ++i) {
        moved = std::max(moved, std::fabs(single[i] - pooled[i]));
    }
    std::printf("threads: largest difference between 0 and 4 threads: %g\n", static_cast<double>(moved));
}

// Not asserted, only printed: what a step costs with a level's worth of sleeping bodies and some action.
void reportPerformance(int threads, bool welded) {
    PhysicsWorld world(kGravity, settingsFor(threads));
    addGround(world);
    const auto start = std::chrono::steady_clock::now();
    // 5000 boxes asleep on the floor in a grid, 200 more falling in above them.
    std::vector<BodyId> grid;
    for (int i = 0; i < 5000; ++i) {
        const Vec3 p{static_cast<float>(i % 100) * 0.9f - 44.5f, 0.4f, static_cast<float>(i / 100) * 0.9f - 22.5f};
        BodyDesc d = dynamicBody(at(p), boxShape({0.4f, 0.4f, 0.4f}));
        d.startAsleep = true;
        grid.push_back(world.addBody(d));
    }
    if (welded) {
        // Each box fixed to its neighbour in the row, breakable: 4900 joints.
        for (int i = 0; i < 5000; ++i) {
            if (i % 100 != 99) {
                const auto next = static_cast<std::size_t>(i);
                JointDesc j = jointBetween(JointType::Fixed, grid[next], grid[next + 1], {0.45f, 0, 0}, {-0.45f, 0, 0});
                j.breakForce = 1e6f;
                world.addJoint(j);
            }
        }
    }
    std::uint32_t seed = 99;
    for (int i = 0; i < 200; ++i) {
        const Vec3 p{unit(seed) * 90 - 45, 3 + unit(seed) * 20, unit(seed) * 44 - 22};
        world.addBody(dynamicBody(placed(Mat4::rotationX(unit(seed) * 3), p), boxShape({0.4f, 0.4f, 0.4f})));
    }
    const std::chrono::duration<double, std::milli> built = std::chrono::steady_clock::now() - start;
    const auto timeSteps = [&](int steps) {
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < steps; ++i) {
            world.step(kStep);
        }
        const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - begin;
        return elapsed.count() / steps;
    };
    const double falling = timeSteps(120);
    const std::size_t awakeAfterFall = world.awakeBodyCount();
    const double settling = timeSteps(480);
    const std::size_t awakeAtEnd = world.awakeBodyCount();
    const double resting = timeSteps(120);
    std::printf("performance, %d threads, %zu joints: 5000 sleeping + 200 falling boxes built in %.0f ms; "
                "per fixed step: %.3f ms for the first 2 s (%zu awake after), %.3f ms over the next 8 s, "
                "%.3f ms once all asleep (%zu awake)\n",
                threads, world.jointCount(), built.count(), falling, awakeAfterFall, settling, resting, awakeAtEnd);
}

}  // namespace

int main() {
    testPoseConvention();
    testRestitution();
    testRestitutionCombine();
    testFrictionCombine();
    testStackStandsAndSleeps();
    testThinPlateAndSmallBox();
    testDeepOverlap();
    testHeavyTowerStands();
    testShortCapsuleStaysOnTheFloor();
    testSlope();
    testMassProperties();
    testCentreOfMassTips();
    testShapeFrames();
    testLocks();
    testStartAsleep();
    testStartAsleepDropsVelocity();
    testFullyLockedIsStatic();
    testKinematicStopsWhereItWas();
    testKinematic();
    testMotionTypeAndTeleport();
    testFixedJointBreaks();
    testHinge();
    testBallJoint();
    testJointOnOffsetCentreOfMass();
    testJointsWithWorldAndCollisions();
    testJointsWakeTheirPartners();
    testContactEvents();
    testRaycast();
    testUnknownAndRemovedIds();
    testDegenerateInput();
    testLimitsAndLifetime();
    testStepping();
    testBodyFlags();
    testThreadsGiveTheSameResult();
    reportPerformance(0, false);
    reportPerformance(-1, false);
    reportPerformance(0, true);
    reportPerformance(-1, true);
    if (failures == 0) {
        std::puts("all physics tests passed");
    }
    return failures == 0 ? 0 : 1;
}
