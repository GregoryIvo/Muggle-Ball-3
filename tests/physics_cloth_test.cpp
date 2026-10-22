// PhysicsWorld's cloth: a grid of particles as a Jolt soft body, pinned, blown by wind and pushed by what it meets. CPU only,
// no game data. Worlds run on the calling thread (threads = 0).

#include "mb/engine/Math.h"
#include "mb/engine/Physics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace mb;

namespace {

int failures = 0;

// MB3_PHYSICS_VERBOSE=1 also lists the checks that pass.
void check(bool ok, const std::string& what) {
    static const bool verbose = std::getenv("MB3_PHYSICS_VERBOSE") != nullptr;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    } else if (verbose) {
        std::printf("  ok: %s\n", what.c_str());
    }
}

std::string str(const char* format, float a, float b = 0, float c = 0) {
    char text[256];
    std::snprintf(text, sizeof text, format, static_cast<double>(a), static_cast<double>(b), static_cast<double>(c));
    return text;
}

constexpr Vec3 kGravity{0, -9.8f, 0};
constexpr float kStep = 1.0f / 60.0f;

PhysicsWorld makeWorld(Vec3 gravity = kGravity) {
    PhysicsSettings settings;
    settings.threads = 0;
    return PhysicsWorld(gravity, settings);
}

void runSeconds(PhysicsWorld& world, float seconds) {
    for (int i = 0; i < static_cast<int>(std::lround(seconds / kStep)); ++i) {
        world.step(kStep);
    }
}

// A sheet standing in the plane z = 0, `across` wide at the bottom row and `up` high, its lowest row at height `base`; row 0 is the
// lowest. The particle at column c of row r is r * width + c.
ClothDesc sheet(int width, int height, float spacing, float base) {
    ClothDesc desc;
    desc.width = width;
    desc.height = height;
    desc.particleMass = 0.005f;
    desc.particleRadius = 0.25f;
    const float left = -0.5f * spacing * static_cast<float>(width - 1);
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            desc.positions.push_back({left + spacing * static_cast<float>(c), base + spacing * static_cast<float>(r), 0});
        }
    }
    return desc;
}

int particle(const ClothDesc& desc, int row, int column) {
    return row * desc.width + column;
}

// Pins the top row's two corners where they are, or `inwards` nearer to each other.
void pinTopCorners(PhysicsWorld& world, BodyId cloth, const ClothDesc& desc, float inwards = 0) {
    for (const int column : {0, desc.width - 1}) {
        const int top = particle(desc, desc.height - 1, column);
        Vec3 at = desc.positions[static_cast<std::size_t>(top)];
        at.x += column == 0 ? inwards : -inwards;
        world.pinClothParticle(cloth, top, at);
    }
}

bool finite(const std::vector<Vec3>& points) {
    return std::ranges::all_of(points, [](Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); });
}

// How far the springs along the grid are stretched, as a share of their length at rest: the most of them.
float mostStretch(const ClothDesc& desc, const std::vector<Vec3>& now) {
    float most = 0;
    for (int r = 0; r < desc.height; ++r) {
        for (int c = 0; c < desc.width; ++c) {
            for (const auto [dr, dc] : {std::pair{0, 1}, std::pair{1, 0}}) {
                if (r + dr >= desc.height || c + dc >= desc.width) {
                    continue;
                }
                const auto a = static_cast<std::size_t>(particle(desc, r, c));
                const auto b = static_cast<std::size_t>(particle(desc, r + dr, c + dc));
                const float rest = length(desc.positions[a] - desc.positions[b]);
                most = std::max(most, std::fabs(length(now[a] - now[b]) - rest) / rest);
            }
        }
    }
    return most;
}

PhysicsShape boxShape(Vec3 half) {
    PhysicsShape s;
    s.type = PhysicsShapeType::Box;
    s.halfExtents = half;
    return s;
}

PhysicsShape sphereShape(float radius) {
    PhysicsShape s;
    s.type = PhysicsShapeType::Sphere;
    s.radius = radius;
    return s;
}

BodyDesc bodyAt(Vec3 position, PhysicsShape shape, MotionType motion = MotionType::Dynamic) {
    BodyDesc d;
    d.pose = Mat4::translation(position);
    d.motion = motion;
    d.shapes = {std::move(shape)};
    return d;
}

// ---- The tests -----------------------------------------------------------------------------------

void testPinnedCornersHold() {
    PhysicsWorld world = makeWorld();
    ClothDesc desc = sheet(5, 6, 2.0f, 5.0f);
    for (std::size_t i = 0; i < desc.positions.size(); ++i) {
        desc.positions[i].z = 0.02f * static_cast<float>(i % 2);  // not quite flat, or a sheet pushed in its own plane could only be squeezed
    }
    const BodyId cloth = world.addCloth(desc);
    check(cloth != 0, "cloth: a sheet is made");
    check(world.clothPositions(cloth).size() == 30, "cloth: one position a particle");
    pinTopCorners(world, cloth, desc, 1.5f);  // nearer each other than the top row is wide: the sheet has to hang
    runSeconds(world, 4);

    const std::vector<Vec3> now = world.clothPositions(cloth);
    check(finite(now), "pinned: every particle is finite");
    const int topLeft = particle(desc, 5, 0), topRight = particle(desc, 5, 4);
    check(length(now[static_cast<std::size_t>(topLeft)] - (desc.positions[static_cast<std::size_t>(topLeft)] + Vec3{1.5f, 0, 0})) < 1e-3f &&
              length(now[static_cast<std::size_t>(topRight)] - (desc.positions[static_cast<std::size_t>(topRight)] - Vec3{1.5f, 0, 0})) < 1e-3f,
          "pinned: the corners stay where they were put");
    const Vec3 bottomMiddle = now[static_cast<std::size_t>(particle(desc, 0, 2))];
    check(bottomMiddle.y < 12 && bottomMiddle.y > 4.5f,
          str("pinned: the rest hangs from the corners, under them (the bottom row at %.2f, the corners at 15)", bottomMiddle.y));
    check(mostStretch(desc, now) < 0.05f, str("pinned: the springs stretch at most %.3f of their length", mostStretch(desc, now)));
    check(world.takeContacts().empty(), "pinned: a cloth in the air touches nothing");

    // Pinned where they were, a sheet that is taut has nowhere to go.
    PhysicsWorld taut = makeWorld();
    const BodyId held = taut.addCloth(desc);
    pinTopCorners(taut, held, desc);
    runSeconds(taut, 2);
    const Vec3 hanging = taut.clothPositions(held)[static_cast<std::size_t>(particle(desc, 0, 2))];
    check(std::fabs(hanging.y - desc.positions[static_cast<std::size_t>(particle(desc, 0, 2))].y) < 0.05f,
          "pinned: a sheet held at its own width hangs flat");
}

void testFallsAndRestsOnTheFloor() {
    PhysicsWorld world = makeWorld();
    world.addBody(bodyAt({0, -0.5f, 0}, boxShape({20, 0.5f, 20}), MotionType::Static));
    ClothDesc desc = sheet(4, 4, 1.0f, 0);
    for (Vec3& p : desc.positions) {  // laid flat, 3 units up
        p = {p.x, 3.0f + 0.05f * p.x, p.y};
    }
    const BodyId cloth = world.addCloth(desc);
    runSeconds(world, 3);
    const std::vector<Vec3> now = world.clothPositions(cloth);
    check(finite(now), "falls: every particle is finite");
    float lowest = 1e9f, highest = -1e9f;
    for (const Vec3 p : now) {
        lowest = std::min(lowest, p.y);
        highest = std::max(highest, p.y);
    }
    check(lowest > 0.25f - 0.05f && highest < 1.5f, str("falls: it lies on the floor, a radius off it (%.2f to %.2f)", lowest, highest));
}

void testWindBlowsTheSheet() {
    const ClothDesc desc = sheet(5, 6, 2.0f, 5.0f);
    PhysicsWorld calm = makeWorld(), windy = makeWorld();
    const BodyId a = calm.addCloth(desc), b = windy.addCloth(desc);
    pinTopCorners(calm, a, desc);
    pinTopCorners(windy, b, desc);
    windy.setClothWind(b, {0, 0, 10});
    runSeconds(calm, 4);
    runSeconds(windy, 4);
    const auto middle = static_cast<std::size_t>(particle(desc, 2, 2));
    const Vec3 still = calm.clothPositions(a)[middle], blown = windy.clothPositions(b)[middle];
    check(std::fabs(still.z) < 0.05f, str("wind: without it the sheet hangs in its plane (z %.3f)", still.z));
    check(blown.z > 1.0f, str("wind: with it the middle is blown downwind (z %.2f)", blown.z));
    check(finite(windy.clothPositions(b)) && mostStretch(desc, windy.clothPositions(b)) < 0.25f, "wind: it stays whole");

    // A sheet edge-on to the wind feels none: this one stands in the plane z = 0, the wind blows along x.
    PhysicsWorld edgeOn = makeWorld();
    const BodyId c = edgeOn.addCloth(desc);
    pinTopCorners(edgeOn, c, desc);
    edgeOn.setClothWind(c, {10, 0, 0});
    runSeconds(edgeOn, 4);
    check(std::fabs(edgeOn.clothPositions(c)[middle].x - desc.positions[middle].x) < 0.1f &&
              std::fabs(edgeOn.clothPositions(c)[middle].z) < 0.1f,
          "wind: across the sheet's plane it pushes nothing");
}

void testBodiesPushTheSheetAndTouchesAreReported() {
    PhysicsWorld world = makeWorld();
    const ClothDesc desc = sheet(5, 6, 2.0f, 5.0f);
    ClothDesc owned = desc;
    owned.userData = 41;
    const BodyId cloth = world.addCloth(owned);
    pinTopCorners(world, cloth, desc);
    runSeconds(world, 2);
    const auto middle = static_cast<std::size_t>(particle(desc, 2, 2));
    const float before = world.clothPositions(cloth)[middle].z;

    BodyDesc ballDesc = bodyAt({0, world.clothPositions(cloth)[middle].y, -4}, sphereShape(0.6f));
    ballDesc.gravity = false;
    ballDesc.mass = 20;
    ballDesc.linearVelocity = {0, 0, 12};
    ballDesc.userData = 77;
    const BodyId ball = world.addBody(ballDesc);

    std::optional<ContactEvent> touch;
    float pushed = before;
    for (int i = 0; i < 60; ++i) {
        world.step(kStep);
        for (const ContactEvent& e : world.takeContacts()) {
            if (e.a == cloth && e.b == ball && !touch) {
                touch = e;
            }
        }
        pushed = std::max(pushed, world.clothPositions(cloth)[middle].z);
    }
    check(touch.has_value(), "bodies: the ball's touch is reported");
    if (touch) {
        check(touch->userDataA == 41 && touch->userDataB == 77, "bodies: the user data of both");
        check(touch->normal.z < -0.5f, "bodies: the normal points from the cloth to the ball, back the way it came");
        check(touch->normalSpeed > 6, str("bodies: it closed at %.1f units a second (the ball went at 12)", touch->normalSpeed));
    }
    check(pushed > before + 0.5f, str("bodies: the ball pushes the sheet (%.2f to %.2f)", before, pushed));
    check(world.linearVelocity(ball).z > 10, "bodies: a sheet that light hardly slows the ball");
}

void testATouchIsReportedOnce() {
    // A sheet falls on a box and lies there: a long stay is one report, not one a step.
    PhysicsWorld world = makeWorld();
    const BodyId box = world.addBody(bodyAt({0, 0, 0}, boxShape({4, 0.5f, 4}), MotionType::Static));
    ClothDesc desc = sheet(4, 4, 1.0f, 0);
    for (Vec3& p : desc.positions) {  // laid flat, 2 units up
        p = {p.x, 2.0f, p.y - 1.5f};
    }
    const BodyId cloth = world.addCloth(desc);
    int reports = 0;
    for (int i = 0; i < 240; ++i) {
        world.step(kStep);
        for (const ContactEvent& e : world.takeContacts()) {
            reports += (e.a == cloth && e.b == box) ? 1 : 0;
        }
    }
    check(reports == 1, str("touches: a sheet lying on a box reports it once (%.0f reports in 4 s)", static_cast<float>(reports)));
    check(world.clothPositions(cloth)[5].y > 0.5f + 0.2f, "touches: and does lie on it");
}

void pinCorners(PhysicsWorld& world, BodyId cloth, const ClothDesc& desc) {
    for (const int row : {0, desc.height - 1}) {
        for (const int column : {0, desc.width - 1}) {
            const int p = particle(desc, row, column);
            world.pinClothParticle(cloth, p, desc.positions[static_cast<std::size_t>(p)]);
        }
    }
}

void testSlackFillsTheSheetOut() {
    ClothDesc desc = sheet(5, 6, 2.0f, 5.0f);
    desc.bendCompliance = 0.05f;
    desc.slack = 0.05f;
    desc.slackSeconds = 1.5f;
    PhysicsWorld world = makeWorld();
    const BodyId cloth = world.addCloth(desc);
    pinCorners(world, cloth, desc);
    world.setClothWind(cloth, {0, 0, 4});
    const auto middle = static_cast<std::size_t>(particle(desc, 3, 2));

    runSeconds(world, 0.1f);
    float soon = 0;
    for (std::size_t i = 0; i < desc.positions.size(); ++i) {
        soon = std::max(soon, length(world.clothPositions(cloth)[i] - desc.positions[i]));
    }
    check(soon < 0.25f, str("slack: a tenth of a second in, nothing has moved by more than %.3f (it fills out, it does not snap)", soon));
    runSeconds(world, 4);
    const std::vector<Vec3> now = world.clothPositions(cloth);
    check(now[middle].z > 0.5f, str("slack: pinned where it lies, the sheet bows out downwind (%.2f)", now[middle].z));
    check(finite(now) && mostStretch(desc, now) < 0.12f, "slack: and nothing is torn");

    // The same sheet with no slack lies taut, whatever the wind.
    ClothDesc cut = desc;
    cut.slack = 0;
    PhysicsWorld taut = makeWorld();
    const BodyId flat = taut.addCloth(cut);
    pinCorners(taut, flat, cut);
    taut.setClothWind(flat, {0, 0, 4});
    runSeconds(taut, 4);
    check(taut.clothPositions(flat)[middle].z < 0.3f, "slack: with none it stays flat but for a shiver");
}

// A push is a velocity added to every particle that is free, along any direction (the wind only takes what is square to the sheet), and it
// wakes a sheet that lies asleep.
void testPushCloth() {
    ClothDesc desc = sheet(5, 6, 2.0f, 5.0f);
    desc.bendCompliance = 0.05f;
    desc.slack = 0.05f;
    desc.slackSeconds = 1.0f;
    desc.damping = 0.8f;
    // No gravity: a sheet with one corner pinned and the rest pushed along x, which lies in its plane.
    PhysicsWorld still = makeWorld({0, 0, 0}), pushed = makeWorld({0, 0, 0});
    const BodyId a = still.addCloth(desc), b = pushed.addCloth(desc);
    const int topLeft = particle(desc, desc.height - 1, 0);
    for (PhysicsWorld* world : {&still, &pushed}) {
        world->pinClothParticle(world == &still ? a : b, topLeft, desc.positions[static_cast<std::size_t>(topLeft)]);
    }
    runSeconds(still, 1);
    runSeconds(pushed, 1);
    pushed.pushCloth(b, {3, 0, 0});
    runSeconds(still, 0.5f);
    runSeconds(pushed, 0.5f);
    const auto far = static_cast<std::size_t>(particle(desc, 0, 4));  // the corner farthest from the pin
    const Vec3 was = still.clothPositions(a)[far], now = pushed.clothPositions(b)[far];
    check(now.x - was.x > 0.3f, str("push: the sheet is carried along x, though x lies in its plane (%.2f units in half a second)", now.x - was.x));
    check(length(pushed.clothPositions(b)[static_cast<std::size_t>(topLeft)] - desc.positions[static_cast<std::size_t>(topLeft)]) < 1e-3f,
          "push: but what is pinned stays where its pin is");
    check(finite(pushed.clothPositions(b)), "push: and the sheet stays whole");

    // Asleep, it wakes.
    runSeconds(pushed, 14);
    check(!pushed.isAwake(b), "push: a sheet that has settled is asleep");
    pushed.pushCloth(b, {0, 0, 1});
    check(pushed.isAwake(b), "push: and a push wakes it");
    runSeconds(pushed, 0.3f);
    check(pushed.clothPositions(b)[far].z > 0.05f, str("push: and moves it (z %.2f)", pushed.clothPositions(b)[far].z));

    // Bad input changes nothing.
    pushed.pushCloth(99, {1, 0, 0});
    pushed.pushCloth(b, {std::numeric_limits<float>::quiet_NaN(), 0, 0});
    runSeconds(pushed, 0.5f);
    check(finite(pushed.clothPositions(b)), "push: a body that is no cloth, or a velocity that is NaN, is refused");
}

void testASettledClothSleepsAndWakes() {
    ClothDesc desc = sheet(5, 6, 2.0f, 5.0f);
    desc.bendCompliance = 0.05f;
    desc.slack = 0.05f;
    desc.slackSeconds = 1.0f;
    desc.damping = 0.8f;
    PhysicsWorld world = makeWorld();
    const BodyId cloth = world.addCloth(desc);
    pinCorners(world, cloth, desc);
    world.setClothWind(cloth, {0, 0, 4});
    runSeconds(world, 0.5f);
    check(world.isAwake(cloth), "sleep: a cloth that is still filling out is awake");
    runSeconds(world, 8);
    check(!world.isAwake(cloth), "sleep: one that has settled lies still, and costs nothing");

    // A pin that goes somewhere else wakes it, and the sheet follows.
    const int topLeft = particle(desc, desc.height - 1, 0);
    Vec3 moved = desc.positions[static_cast<std::size_t>(topLeft)];
    moved.x += 1.0f;
    world.pinClothParticle(cloth, topLeft, moved);
    world.step(kStep);
    check(world.isAwake(cloth), "sleep: a pin that moves wakes it");
    runSeconds(world, 1);
    check(length(world.clothPositions(cloth)[static_cast<std::size_t>(topLeft)] - moved) < 1e-3f, "sleep: and the particle goes where the pin is");
    runSeconds(world, 8);
    check(!world.isAwake(cloth), "sleep: it lies still again");

    // A body that touches it wakes it: a ball through the middle of the sheet, with the wind and the pins still.
    BodyDesc ballDesc = bodyAt({-1.0f, 9.0f, -4}, sphereShape(0.6f));
    ballDesc.gravity = false;
    ballDesc.mass = 20;
    ballDesc.linearVelocity = {0, 0, 10};
    ballDesc.continuousCollision = true;
    world.addBody(ballDesc);
    bool woke = false;
    for (int i = 0; i < 60 && !woke; ++i) {
        world.step(kStep);
        woke = world.isAwake(cloth);
    }
    check(woke, "sleep: a ball that reaches a sleeping cloth wakes it");

    // Let go of a pin that holds it, and it falls.
    runSeconds(world, 6);
    world.releaseClothParticle(cloth, topLeft);
    check(world.isAwake(cloth), "sleep: a particle let go wakes it");
}

void testAClothThatFliesApartIsPutBack() {
    PhysicsWorld world = makeWorld();
    const ClothDesc desc = sheet(5, 6, 2.0f, 5.0f);
    const BodyId cloth = world.addCloth(desc);
    pinTopCorners(world, cloth, desc);
    runSeconds(world, 1);
    check(world.clothResets(cloth) == 0, "guard: a cloth that is well is not touched");
    const std::vector<Vec3> before = world.clothPositions(cloth);
    // A holder thrown out of the world: the pin is a million units away.
    const int topLeft = particle(desc, desc.height - 1, 0);
    world.pinClothParticle(cloth, topLeft, {1e6f, 1e6f, 1e6f});
    world.step(kStep);
    std::vector<Vec3> now = world.clothPositions(cloth);
    check(world.clothResets(cloth) == 1 && finite(now) && length(now[static_cast<std::size_t>(topLeft)] - before[static_cast<std::size_t>(topLeft)]) < 1.0f,
          "guard: the particles are put back where they were");
    runSeconds(world, 0.2f);
    check(world.clothResets(cloth) == 3, "guard: a pin that keeps pulling it away sends it off three times");
    now = world.clothPositions(cloth);
    check(finite(now), "guard: and it is held still, where it is, after that");
    const Vec3 held = now[7];
    world.pinClothParticle(cloth, particle(desc, 2, 2), {0, 0, 0});
    world.setClothWind(cloth, {0, 0, 50});
    runSeconds(world, 1);
    check(length(world.clothPositions(cloth)[7] - held) < 1e-3f, "guard: nothing moves it now");
    check(world.clothResets(9999) == 0, "guard: and an id that is none has no resets");
}

void testPinsCarryTheSheetAndLetGo() {
    PhysicsWorld world = makeWorld();
    const ClothDesc desc = sheet(5, 6, 2.0f, 5.0f);
    const BodyId cloth = world.addCloth(desc);
    pinTopCorners(world, cloth, desc);
    runSeconds(world, 1);
    // The pins move along x by 5 units a second for 2 s: the sheet comes along.
    for (int i = 0; i < 120; ++i) {
        const float shift = 5.0f * kStep * static_cast<float>(i + 1);
        for (const int column : {0, desc.width - 1}) {
            const int top = particle(desc, desc.height - 1, column);
            Vec3 at = desc.positions[static_cast<std::size_t>(top)];
            at.x += shift;
            world.pinClothParticle(cloth, top, at);
        }
        world.step(kStep);
    }
    std::vector<Vec3> now = world.clothPositions(cloth);
    const int topLeft = particle(desc, desc.height - 1, 0);
    check(std::fabs(now[static_cast<std::size_t>(topLeft)].x - (desc.positions[static_cast<std::size_t>(topLeft)].x + 10.0f)) < 0.05f,
          "pins: a pin that moves takes its particle to where it is put");
    check(now[static_cast<std::size_t>(particle(desc, 0, 2))].x > 6.0f, "pins: the rest of the sheet comes after it");
    check(finite(now) && mostStretch(desc, now) < 0.3f, "pins: nothing is torn by it");

    world.releaseClothParticle(cloth, topLeft);
    world.releaseClothParticle(cloth, particle(desc, desc.height - 1, desc.width - 1));
    runSeconds(world, 2);
    now = world.clothPositions(cloth);
    check(now[static_cast<std::size_t>(topLeft)].y < desc.positions[static_cast<std::size_t>(topLeft)].y - 3.0f,
          "pins: a particle let go falls");
}

void testAViolentPinDoesNotBlowTheSheetUp() {
    PhysicsWorld world = makeWorld();
    const ClothDesc desc = sheet(7, 8, 1.5f, 6.0f);
    const BodyId cloth = world.addCloth(desc);
    pinTopCorners(world, cloth, desc);
    runSeconds(world, 1);
    // A ship thrown about: the corners jump 3 units a step for a third of a second, back and forth.
    for (int i = 0; i < 20; ++i) {
        const float jump = (i % 2 == 0) ? 3.0f : -3.0f;
        for (const int column : {0, desc.width - 1}) {
            const int top = particle(desc, desc.height - 1, column);
            Vec3 at = desc.positions[static_cast<std::size_t>(top)];
            at.x += jump;
            world.pinClothParticle(cloth, top, at);
        }
        world.step(kStep);
    }
    pinTopCorners(world, cloth, desc);
    runSeconds(world, 3);
    const std::vector<Vec3> now = world.clothPositions(cloth);
    check(finite(now), "violent: every particle is finite");
    float far = 0;
    for (const Vec3 p : now) {
        far = std::max(far, length(p - Vec3{0, 8, 0}));
    }
    check(far < 20, str("violent: it comes back together within reach of where it hangs (%.1f)", far));
}

// A cloth pinned all over in the air, with a box through its middle: whether they touch is for the order to ignore it to decide.
bool touchesTheBox(bool ignoreIt, float& boxSidePush) {
    PhysicsWorld world = makeWorld();
    const ClothDesc desc = sheet(3, 3, 1.0f, 5.0f);
    const BodyId box = world.addBody(bodyAt({0, 6, 0}, boxShape({3, 0.2f, 0.3f}), MotionType::Static));
    const BodyId cloth = world.addCloth(desc);
    if (ignoreIt) {
        world.clothIgnores(cloth, box);
    }
    runSeconds(world, 2);
    bool touched = false;
    for (const ContactEvent& e : world.takeContacts()) {
        touched = touched || (e.a == cloth && e.b == box);
    }
    boxSidePush = world.clothPositions(cloth)[static_cast<std::size_t>(particle(desc, 1, 1))].y;
    return touched;
}

void testIgnoredBodies() {
    float resting = 0, falling = 0;
    check(touchesTheBox(false, resting), "ignores: a cloth that falls on a box touches it");
    check(!touchesTheBox(true, falling), "ignores: a body it was told to go through is not touched");
    check(falling < resting - 1.0f, str("ignores: and it falls past it (%.2f, against %.2f resting on it)", falling, resting));
}

void testRaycastHitsTheSurfaceFromBothSides() {
    PhysicsWorld world = makeWorld(Vec3{});
    ClothDesc desc = sheet(4, 4, 2.0f, 0.0f);
    desc.userData = 9;
    const BodyId cloth = world.addCloth(desc);
    for (int i = 0; i < 16; ++i) {
        world.pinClothParticle(cloth, i, desc.positions[static_cast<std::size_t>(i)]);
    }
    world.step(kStep);
    // Between particles: a ray finds the face, not only the particles.
    const auto front = world.raycast({0.4f, 2.7f, -5}, {0, 0, 1}, 20);
    const auto back = world.raycast({0.4f, 2.7f, 5}, {0, 0, -1}, 20);
    check(front.has_value() && front->body == cloth && std::fabs(front->distance - 5) < 0.05f, "ray: it hits the cloth from the front");
    check(back.has_value() && back->body == cloth && std::fabs(back->distance - 5) < 0.05f, "ray: and from behind");
    check(world.userData(cloth) == 9, "ray: user data of the cloth");
    check(!world.raycast({20, 2.7f, -5}, {0, 0, 1}, 20).has_value(), "ray: and misses beside it");
}

void testLifetimeAndBadInput() {
    PhysicsWorld world = makeWorld();
    const ClothDesc good = sheet(3, 3, 1.0f, 5.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ClothDesc narrow = good;
    narrow.width = 1;
    ClothDesc mismatched = good;
    mismatched.positions.pop_back();
    ClothDesc notFinite = good;
    notFinite.positions[4].y = nan;
    ClothDesc together = good;
    together.positions[1] = together.positions[0];
    ClothDesc massless = good;
    massless.particleMass = 0;
    ClothDesc oversized = sheet(70, 70, 1.0f, 0);
    check(world.addCloth(narrow) == 0 && world.addCloth(mismatched) == 0 && world.addCloth(notFinite) == 0 &&
              world.addCloth(together) == 0 && world.addCloth(massless) == 0 && world.addCloth(oversized) == 0,
          "bad input: a cloth that cannot be made is refused");
    check(world.bodyCount() == 0, "bad input: and leaves nothing behind");

    const BodyId cloth = world.addCloth(good);
    check(world.bodyCount() == 1 && world.clothPositions(cloth).size() == 9, "lifetime: a cloth is a body");
    world.pinClothParticle(cloth, 99, {0, 0, 0});
    world.pinClothParticle(cloth, 0, {nan, 0, 0});
    world.setClothWind(cloth, {nan, 0, 0});
    world.releaseClothParticle(cloth, 3);  // never pinned
    runSeconds(world, 1);
    check(finite(world.clothPositions(cloth)), "bad input: a pin at a particle that is not there, or NaN, changes nothing");
    world.removeBody(cloth);
    check(world.bodyCount() == 0 && world.clothPositions(cloth).empty(), "lifetime: a removed cloth is gone");
    world.pinClothParticle(cloth, 0, {0, 0, 0});
    world.setClothWind(cloth, {1, 0, 0});
    runSeconds(world, 1);
    check(world.takeContacts().empty(), "lifetime: calls for a cloth that is gone do nothing");
}

void testTwoRunsAgree() {
    const ClothDesc desc = sheet(5, 6, 2.0f, 5.0f);
    std::vector<Vec3> runs[2];
    for (auto& run : runs) {
        PhysicsWorld world = makeWorld();
        world.addBody(bodyAt({0, 1.0f, 0}, boxShape({3, 0.2f, 3}), MotionType::Static));
        const BodyId cloth = world.addCloth(desc);
        pinTopCorners(world, cloth, desc);
        world.setClothWind(cloth, {0, 0, 10});
        runSeconds(world, 3);
        run = world.clothPositions(cloth);
    }
    bool same = runs[0].size() == runs[1].size();
    for (std::size_t i = 0; same && i < runs[0].size(); ++i) {
        same = length(runs[0][i] - runs[1][i]) < 1e-5f;
    }
    check(same, "determinism: the same cloth in the same world does the same thing");
}

}  // namespace

int main() {
    testPinnedCornersHold();
    testFallsAndRestsOnTheFloor();
    testWindBlowsTheSheet();
    testPushCloth();
    testBodiesPushTheSheetAndTouchesAreReported();
    testATouchIsReportedOnce();
    testSlackFillsTheSheetOut();
    testASettledClothSleepsAndWakes();
    testAClothThatFliesApartIsPutBack();
    testPinsCarryTheSheetAndLetGo();
    testAViolentPinDoesNotBlowTheSheetUp();
    testIgnoredBodies();
    testRaycastHitsTheSurfaceFromBothSides();
    testLifetimeAndBadInput();
    testTwoRunsAgree();
    if (failures == 0) {
        std::puts("all cloth physics tests passed");
    }
    return failures == 0 ? 0 : 1;
}
