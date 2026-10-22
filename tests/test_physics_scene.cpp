// Level -> physics tests: what planPhysics() makes of actors, shapes, materials and joints, and a level made of a
// few bricks running in a PhysicsScene. Needs neither a GPU nor game data: levels and materials are made up here.

#include "mb/game/PhysicsScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

bool near(Vec3 a, Vec3 b, float tolerance = 1e-4f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

bool near(const Mat4& a, const Mat4& b, float tolerance = 1e-4f) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (!near(a.m[i][j], b.m[i][j], tolerance)) {
                return false;
            }
        }
    }
    return true;
}

// True when the rows of the 3x3 are orthonormal and right-handed.
bool isRigid(const Mat4& m, float tolerance = 1e-5f) {
    const Vec3 x{m.m[0][0], m.m[0][1], m.m[0][2]}, y{m.m[1][0], m.m[1][1], m.m[1][2]},
        z{m.m[2][0], m.m[2][1], m.m[2][2]};
    return near(dot(x, x), 1, tolerance) && near(dot(y, y), 1, tolerance) && near(dot(z, z), 1, tolerance) &&
           near(dot(x, y), 0, tolerance) && near(dot(x, z), 0, tolerance) && near(dot(y, z), 0, tolerance) &&
           dot(cross(x, y), z) > 0;
}

constexpr float kFltMax = std::numeric_limits<float>::max();

const MaterialLibrary& materials() {
    static const MaterialLibrary library = MaterialLibrary::parse(R"({"version": 1, "materials": [
      {"name": "Wood.jpg", "physics": {"staticFriction": 0.7, "dynamicFriction": 0.6, "restitution": 0.3,
                                        "frictionCombineMode": 1, "restitutionCombineMode": 3}},
      {"name": "Ice", "physics": {"staticFriction": 0.05, "dynamicFriction": 0.04, "restitution": 0.1}},
      {"name": "clothmat", "physics": {"staticFriction": 0.9, "dynamicFriction": 0.8, "restitution": 0.2}},
      {"name": "BallMaterial", "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 0.999}},
      {"name": "boardmaterial", "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 1.0}},
      {"name": "Dull", "physics": {"staticFriction": 0.6, "dynamicFriction": 0.5, "restitution": 0.0}}
    ]})");
    return library;
}

// ---- Building levels ----------------------------------------------------------------------------

Shape box(Vec3 half, Vec3 at = {}, std::uint16_t group = 2) {
    Shape s;
    s.type = ShapeType::Box;
    s.halfExtents = half;
    s.localPose.position = at;
    s.group = group;
    s.shapeFlags = 8;  // NX_SF_VISUALIZATION, as in every level
    return s;
}

Shape sphere(float radius, std::uint16_t group = 2) {
    Shape s;
    s.type = ShapeType::Sphere;
    s.radius = radius;
    s.group = group;
    return s;
}

// A brick-like actor: object, body and shapes. flags are NxBodyFlag.
Actor actor(ObjectType type, std::uint32_t id, Vec3 at, std::uint32_t flags, float mass, std::vector<Shape> shapes,
            const std::string& material = "") {
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

// The board: static (kinematic and frozen on every axis), its floor's top at y = 0.
Actor board(float half = 30, const std::string& material = "boardmaterial") {
    return actor(ObjectType::Board, 1, {0, 0, 0}, 0xFF, 1000, {box({half, 0.5f, half}, {0, -0.5f, 0}, 4)}, material);
}

Actor brick(std::uint32_t id, Vec3 at, std::uint32_t flags = 0, float mass = 10, std::vector<Shape> shapes = {},
            const std::string& material = "Wood.jpg") {
    if (shapes.empty()) {
        shapes.push_back(box({0.5f, 0.5f, 0.5f}));
    }
    return actor(ObjectType::Brick, id, at, flags, mass, std::move(shapes), material);
}

Joint fixedJoint(std::uint32_t a, std::uint32_t b, Vec3 anchorA = {}, Vec3 anchorB = {}) {
    Joint j;
    j.type = NxJointType::Fixed;
    j.actorA = a;
    j.actorB = b;
    j.localAnchor[0] = anchorA;
    j.localAnchor[1] = anchorB;
    j.localAxis[0] = j.localAxis[1] = {0, 1, 0};
    j.localNormal[0] = j.localNormal[1] = {1, 0, 0};
    j.maxForce = j.maxTorque = kFltMax;
    return j;
}

// A joint that breaks at `force`, so that it is not taken for a weld that holds a body to the world for good.
Joint breakable(Joint j, float force = 1e6f) {
    j.maxForce = j.maxTorque = force;
    return j;
}

Level levelOf(std::vector<Actor> actors, std::vector<Joint> joints = {}) {
    Level level;
    level.gravity = {0, -9.8f, 0};
    level.actors = std::move(actors);
    level.joints = std::move(joints);
    return level;
}

PhysicsOptions options(StartState start = StartState::Asleep, JointBreaking breaking = JointBreaking::Impact,
                       bool rebuild = true) {
    PhysicsOptions o;
    o.start = start;
    o.breaking = breaking;
    o.rebuildJointFrames = rebuild;
    return o;
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

// ---- Tests --------------------------------------------------------------------------------------

void testFlattenActors() {
    Actor cloth = actor(ObjectType::Cloth, 10, {}, 0, 1, {sphere(0.5f)});
    cloth.object->clothSegments = {Actor{}, Actor{}, Actor{}};
    const Level level = levelOf({brick(1, {}), cloth, brick(2, {})});
    const std::vector<ActorRef> actors = flattenActors(level);
    check(actors.size() == 6, "flatten: top-level actors and cloth segments");
    bool inOrder = true;
    for (std::size_t i = 0; i < actors.size(); ++i) {
        inOrder = inOrder && actors[i].index == i;
    }
    check(inOrder, "flatten: indices run 0, 1, 2, ...");
    check(actors[0].actor == &level.actors[0] && actors[1].actor == &level.actors[1] &&
              actors[2].actor == &level.actors[1].object->clothSegments[0] &&
              actors[4].actor == &level.actors[1].object->clothSegments[2] && actors[5].actor == &level.actors[2],
          "flatten: file order, each cloth followed by its segments");
    check(actors[0].owner->id == 1 && actors[1].owner->id == 10 && actors[2].owner->id == 10 &&
              actors[4].owner->id == 10 && actors[5].owner->id == 2,
          "flatten: a segment's owner is its cloth's object");
}

void testOrthonormalised() {
    const Mat4 rigid = Mat4::rotationX(0.4f) * Mat4::rotationY(-1.1f) * Mat4::translation({1, 2, 3});
    const Mat4 same = orthonormalised(rigid);
    check(std::memcmp(&same, &rigid, sizeof rigid) == 0, "orthonormalised: a rotation comes back untouched");

    // The saved poses are rotations scaled in a plane (1.081 along x and z): the nearest rotation is the one it was.
    Mat4 scaled = Mat4::rotationY(0.3f);
    for (int row = 0; row < 3; row += 2) {
        for (int column = 0; column < 3; ++column) {
            scaled.m[row][column] *= 1.081f;
        }
    }
    scaled.m[3][0] = 5;
    scaled.m[3][1] = 6;
    scaled.m[3][2] = 7;
    const Mat4 fixed = orthonormalised(scaled);
    check(isRigid(fixed) && near(fixed, Mat4::rotationY(0.3f) * Mat4::translation({5, 6, 7}), 1e-5f),
          "orthonormalised: a scaled rotation gives back the rotation, translation kept");

    Mat4 skewed = Mat4::rotationX(0.2f);
    skewed.m[0][1] += 0.06f;
    skewed.m[2][1] -= 0.04f;
    check(isRigid(orthonormalised(skewed)), "orthonormalised: a skewed one becomes rigid");

    Mat4 flat;
    flat.m[1][1] = 0;
    flat.m[3][0] = 9;
    const Mat4 none = orthonormalised(flat);
    check(near(none, Mat4::translation({9, 0, 0})), "orthonormalised: a singular matrix gives the identity rotation");
    Mat4 mirrored;
    mirrored.m[0][0] = -1;
    check(near(orthonormalised(mirrored), Mat4::identity()), "orthonormalised: a mirror gives the identity rotation");
    Mat4 broken = Mat4::rotationZ(1);
    broken.m[0][0] = std::numeric_limits<float>::quiet_NaN();
    check(near(orthonormalised(broken), Mat4::identity()), "orthonormalised: NaN gives the identity rotation");
}

void testMotionTypes() {
    const auto planFor = [](std::uint32_t flags, ObjectType type = ObjectType::Brick) {
        Actor a = actor(type, 5, {1, 2, 3}, flags, 10, {box({0.5f, 0.5f, 0.5f})}, "Wood.jpg");
        return planPhysics(levelOf({a}), materials()).actors.at(0);
    };
    check(planFor(0x100).motion == MotionType::Dynamic, "motion: no flags (just visualization) is dynamic");
    check(planFor(0x180).motion == MotionType::Kinematic, "motion: the kinematic flag makes it kinematic");
    check(planFor(0x1F0).motion == MotionType::Kinematic, "motion: kinematic with frozen rotation is still kinematic");
    check(planFor(0x170).motion == MotionType::Dynamic, "motion: frozen rotation alone is dynamic");
    check(planFor(0x17F, ObjectType::Light).motion == MotionType::Static, "motion: everything frozen and no gravity is static");
    check(planFor(0x1FF, ObjectType::Board).motion == MotionType::Static, "motion: the board (all frozen and kinematic) is static");
    check(planFor(0x17E).motion == MotionType::Static, "motion: all six frozen bits are static with gravity too");
    check(planFor(0x17D).motion == MotionType::Dynamic, "motion: five frozen axes of six stay dynamic");

    const ActorPlan rotationLocked = planFor(0x170);
    check(rotationLocked.body.lockRotation[0] && rotationLocked.body.lockRotation[1] &&
              rotationLocked.body.lockRotation[2] && !rotationLocked.body.lockTranslation[0] &&
              !rotationLocked.body.lockTranslation[1] && !rotationLocked.body.lockTranslation[2],
          "locks: 0x70 locks the rotation and nothing else");
    check(rotationLocked.body.gravity, "locks: gravity stays on");
    const ActorPlan floater = planFor(0x101);
    check(!floater.body.gravity && floater.motion == MotionType::Dynamic,
          "flags: disable gravity gives a dynamic body without gravity");
    const ActorPlan axes = planFor(0x10A);  // frozen position x and z
    check(axes.body.lockTranslation[0] && !axes.body.lockTranslation[1] && axes.body.lockTranslation[2],
          "locks: frozen position x and z");

    // One frozen axis at a time: each flag locks its own axis and no other.
    for (int axis = 0; axis < 6; ++axis) {
        const BodyDesc& d = planFor(0x100u | (2u << axis)).body;
        bool exact = true;
        for (int i = 0; i < 3; ++i) {
            exact = exact && d.lockTranslation[i] == (axis == i) && d.lockRotation[i] == (axis == 3 + i);
        }
        check(exact, "locks: each frozen bit locks its own axis (position x y z, then rotation x y z)");
    }

    Actor bare;
    bare.shapes = {box({1, 1, 1})};
    check(planPhysics(levelOf({bare}), materials()).actors.at(0).motion == MotionType::Static,
          "motion: an actor with no body is static");

    Actor cloth = actor(ObjectType::Cloth, 20, {}, 0x100, 0.005f, {sphere(0.5f)}, "clothmat");
    cloth.object->clothSegments = {actor(ObjectType::Brick, 0, {1, 0, 0}, 0x100, 0.005f, {sphere(0.5f)})};
    cloth.object->clothSegments[0].object.reset();
    const PhysicsPlan plan = planPhysics(levelOf({cloth}), materials());
    check(plan.actors.size() == 2 && plan.actors[0].motion == MotionType::Kinematic &&
              plan.actors[1].motion == MotionType::Kinematic,
          "motion: a cloth and its segments are kinematic, whatever their flags");
}

void testBodyValues() {
    Actor a = actor(ObjectType::Brick, 5, {1, 2, 3}, 0x100, 7.5f, {box({0.5f, 0.5f, 0.5f})}, "Wood.jpg");
    a.body->massSpaceInertia = {1, 2, 3};
    a.body->massLocalPose.position = {0, 0.25f, 0};
    a.body->linearDamping = 0.1f;
    a.body->angularDamping = 0.8f;
    a.body->maxAngularVelocity = 9;
    a.body->sleepLinearVelocity = 0.3f;
    a.body->sleepAngularVelocity = 0.2f;
    a.density = 2;
    const ActorPlan plan = planPhysics(levelOf({a}), materials()).actors.at(0);
    const BodyDesc& d = plan.body;
    check(plan.simulated && d.mass == 7.5f && d.inertia.y == 2 && near(d.massLocalPose.translationPart(), {0, 0.25f, 0}),
          "body: mass, inertia and the mass frame are the saved ones");
    check(d.linearDamping == 0.1f && d.angularDamping == 0.8f && d.maxAngularVelocity == 9 &&
              d.sleepLinearVelocity == 0.3f && d.sleepAngularVelocity == 0.2f,
          "body: damping, maximum spin and sleep thresholds are the saved ones");
    check(d.density == 2 && d.userData == 0 && near(d.pose.translationPart(), {1, 2, 3}) && near(plan.pose, d.pose),
          "body: density, the actor index as user data, the pose");
    Actor kinematic = actor(ObjectType::Brick, 6, {}, 0x180, 7.5f, {box({0.5f, 0.5f, 0.5f})}, "Wood.jpg");
    check(d.continuousCollision && !planPhysics(levelOf({kinematic}), materials()).actors.at(0).body.continuousCollision,
          "body: a dynamic body has swept collision (light pieces thrown by the ball would go through the floor), a kinematic one not");
}

// The PhysX matrix R * diag(scale, 1, scale) of a rotation about y: its rows and columns are `scale` long on two
// axes, as the saved rotations of 5 actors and 34 shapes are (0.93 to 1.21, docs/LEVEL_FORMAT.md).
Pose scaledRotationY(float angle, float scale) {
    Pose pose;
    const Mat4 r = Mat4::rotationY(angle);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const float scaleOfColumn = column == 1 ? 1.0f : scale;
            // PhysX's matrix is the transpose of ours.
            pose.rotation[static_cast<std::size_t>(row * 3 + column)] = r.m[column][row] * scaleOfColumn;
        }
    }
    return pose;
}

void testPoseIsRigid() {
    Actor a = brick(5, {4, 0.5f, 4});
    a.pose = scaledRotationY(0.5f, 1.081f);
    a.pose.position = {4, 0.5f, 4};
    a.shapes[0].localPose = scaledRotationY(-0.8f, 0.934f);
    a.shapes[0].localPose.position = {1, 0, 0};
    a.body->massLocalPose = scaledRotationY(0.2f, 1.05f);
    const ActorPlan plan = planPhysics(levelOf({a}), materials()).actors.at(0);
    check(isRigid(plan.pose) && isRigid(plan.body.pose) && near(plan.pose, plan.body.pose, 0),
          "rigid: the actor pose is made rigid, and the body gets that one");
    check(isRigid(plan.body.shapes.at(0).localPose) && isRigid(plan.body.massLocalPose),
          "rigid: so are the shape pose and the mass frame");
    check(near(plan.pose, Mat4::rotationY(0.5f) * Mat4::translation({4, 0.5f, 4}), 1e-4f),
          "rigid: and it is the rotation the scaled one came from");
    check(near(plan.body.shapes[0].localPose.translationPart(), {1, 0, 0}), "rigid: positions are kept");

    // A rigid pose goes through unchanged (the draw and the body agree to the bit).
    Actor exact = brick(6, {1, 2, 3});
    exact.pose = Pose{Mat3{0.8f, 0.6f, 0, -0.6f, 0.8f, 0, 0, 0, 1}, {1, 2, 3}};
    const Mat4 asSaved = Mat4::fromPhysX(exact.pose.rotation, exact.pose.position);
    const Mat4 rigid = rigidTransform(exact.pose);
    check(std::memcmp(&rigid, &asSaved, sizeof rigid) == 0, "rigid: a rotation that is rigid stays as it was");
}

void testShapes() {
    Shape capsule;
    capsule.type = ShapeType::Capsule;
    capsule.radius = 0.4f;
    capsule.height = 3.0f;
    capsule.localPose.position = {0, 2, 0};
    Shape hull;
    hull.type = ShapeType::Convex;
    hull.hullPoints = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Shape bare;  // a convex of a level file from before version 3: no points
    bare.type = ShapeType::Convex;
    Shape plane;
    plane.type = ShapeType::Plane;
    const Level level = levelOf({brick(5, {}, 0, 10, {box({1, 2, 3}), sphere(0.7f), capsule, hull}),
                                 brick(6, {}, 0, 10, {box({1, 1, 1}), bare, plane})});
    const PhysicsPlan plan = planPhysics(level, materials());
    const std::vector<PhysicsShape>& shapes = plan.actors.at(0).body.shapes;
    check(shapes.size() == 4 && shapes[0].type == PhysicsShapeType::Box && near(shapes[0].halfExtents, {1, 2, 3}),
          "shapes: a box keeps its half extents");
    check(shapes[1].type == PhysicsShapeType::Sphere && shapes[1].radius == 0.7f, "shapes: a sphere keeps its radius");
    check(shapes[2].type == PhysicsShapeType::Capsule && shapes[2].radius == 0.4f && shapes[2].halfHeight == 1.5f &&
              near(shapes[2].localPose.translationPart(), {0, 2, 0}),
          "shapes: a capsule's half height is half its cylinder length (the y axis)");
    check(shapes[3].type == PhysicsShapeType::ConvexHull && shapes[3].points.size() == 4,
          "shapes: a convex keeps its hull points");
    check(plan.actors.at(1).body.shapes.size() == 1 && plan.unsupportedShapes == 2,
          "shapes: a hull without points and a plane are left out and counted");
}

void testMaterials() {
    Actor ice = brick(5, {}, 0, 10, {}, "Ice");
    Actor wood = brick(6, {}, 0, 10, {}, "wood.JPG");  // names match in any case
    Actor none = brick(7, {}, 0, 10, {}, "");
    Actor unknown = brick(8, {}, 0, 10, {}, "not a material");
    Actor cloth = actor(ObjectType::Cloth, 20, {}, 0, 0.005f, {sphere(0.5f)}, "clothmat");
    Actor segment;
    segment.body = Body{};
    segment.body->mass = 0.005f;
    segment.shapes = {sphere(0.5f)};
    cloth.object->clothSegments = {segment};
    const PhysicsPlan plan = planPhysics(levelOf({ice, wood, none, unknown, cloth}), materials());

    const auto materialOf = [&](std::size_t actorIndex) { return plan.actors.at(actorIndex).body.shapes.at(0).material; };
    const PhysicsMaterial iceMaterial = materialOf(0);
    check(iceMaterial.staticFriction == 0.05f && iceMaterial.dynamicFriction == 0.04f && iceMaterial.restitution == 0.1f &&
              iceMaterial.frictionCombine == CombineMode::Average,
          "materials: friction, restitution and the default combine mode come from the object's game material");
    const PhysicsMaterial woodMaterial = materialOf(1);
    check(woodMaterial.staticFriction == 0.7f && woodMaterial.dynamicFriction == 0.6f && woodMaterial.restitution == 0.3f &&
              woodMaterial.frictionCombine == CombineMode::Min && woodMaterial.restitutionCombine == CombineMode::Max,
          "materials: combine modes are PhysX's numbers (1 = min, 3 = max)");
    for (const std::size_t i : {std::size_t{2}, std::size_t{3}}) {
        const PhysicsMaterial m = materialOf(i);
        check(m.staticFriction == 0.5f && m.dynamicFriction == 0.5f && m.restitution == 0 &&
                  m.frictionCombine == CombineMode::Average,
              i == 2 ? "materials: no material gives friction 0.5, 0.5 and restitution 0"
                     : "materials: a name the library does not have gives the same");
    }
    check(materialOf(4).dynamicFriction == 0.8f && materialOf(5).dynamicFriction == 0.8f &&
              materialOf(5).restitution == 0.2f,
          "materials: a cloth segment (which names none) uses its cloth's material");
    check(materialOf(0).dynamicFriction != materialOf(1).dynamicFriction, "materials: each object has its own");

    GameMaterial extreme;
    extreme.frictionCombineMode = 9;
    extreme.restitutionCombineMode = -2;
    check(physicsMaterial(extreme).frictionCombine == CombineMode::Max &&
              physicsMaterial(extreme).restitutionCombine == CombineMode::Average,
          "materials: a combine mode out of range is clamped");
}

void testCollisionGroups() {
    Shape marker = sphere(0.1f, kMarkerGroup);
    Shape zone = box({3, 22, 15}, {0, 15, -17}, kBatZoneGroup);
    Shape trigger = box({2, 1, 2}, {0, 1, 0});
    trigger.shapeFlags = 15;  // visualization and the three trigger flags
    check(!collides(marker) && !collides(zone) && !collides(trigger) && collides(box({1, 1, 1})) &&
              collides(box({1, 1, 1}, {}, 12)) && collides(box({1, 1, 1}, {}, 4)),
          "groups: 5, 11 and trigger shapes do not collide, everything else does");

    Actor lamp = actor(ObjectType::Light, 3, {0, 5, 0}, 0x17F, 1, {marker});
    Actor floor = actor(ObjectType::Board, 1, {}, 0xFF, 1000, {box({30, 0.5f, 30}, {0, -0.5f, 0}, 4), zone}, "boardmaterial");
    Actor teleport = actor(ObjectType::TeleportSrc, 4, {5, 0, 5}, 0x180, 5, {box({1, 1, 1}), trigger});
    const PhysicsPlan plan = planPhysics(levelOf({floor, lamp, teleport}), materials());

    check(!plan.actors[1].simulated && plan.actors[1].motion == MotionType::Static,
          "groups: a marker sphere on its own gets no body");
    check(plan.actors[0].simulated && plan.actors[0].body.shapes.size() == 1,
          "groups: the board's strip is not in its body");
    check(plan.actors[2].simulated && plan.actors[2].body.shapes.size() == 1,
          "groups: a trigger box is not in its body");
    check(plan.zones.size() == 2 && plan.zones[0].actor == 0 && plan.zones[0].group == kBatZoneGroup &&
              !plan.zones[0].trigger && near(plan.zones[0].halfExtents, {3, 22, 15}) &&
              near(plan.zones[0].local.translationPart(), {0, 15, -17}) && plan.zones[1].actor == 2 &&
              plan.zones[1].trigger,
          "groups: the strip and the trigger box are kept as zones, in the actor's frame");
    check(plan.actors[1].type == ObjectType::Light && near(plan.actors[1].pose.translationPart(), {0, 5, 0}),
          "groups: a marker's pose is kept");

    PhysicsScene scene(levelOf({floor, lamp, teleport}), materials(), options(), singleThreaded());
    check(scene.body(1) == 0 && scene.body(0) != 0 && scene.counts().withoutBody == 1 &&
              scene.counts().staticBodies == 1 && scene.counts().kinematicBodies == 1,
          "groups: the marker has no body in the world");
    check(near(scene.actorPose(1).translationPart(), {0, 5, 0}), "groups: and its pose is where it was saved");
}

void testJointResolution() {
    // ids: board 1, brick 2, 3, cloth 10 with segments 11 .. 13 (its own actor has 10), brick 12 (clashes with a
    // segment id), brick 20.
    Actor cloth = actor(ObjectType::Cloth, 10, {0, 5, 0}, 0, 0.005f, {sphere(0.5f)}, "clothmat");
    for (int i = 0; i < 3; ++i) {
        Actor segment;
        segment.pose.position = {static_cast<float>(i + 1), 5, 0};
        segment.body = Body{};
        segment.body->mass = 0.005f;
        segment.shapes = {sphere(0.5f)};
        cloth.object->clothSegments.push_back(segment);
    }
    // indices: board 0, brick 1, brick 2, cloth 3 (own), segments 4, 5, 6, brick (id 12) 7, brick 8
    const Level level = levelOf(
        {board(), brick(2, {0, 0.5f, 0}), brick(3, {3, 0.5f, 0}), cloth, brick(12, {6, 0.5f, 0}), brick(20, {9, 0.5f, 0})},
        {breakable(fixedJoint(0, 2)),      // world to object 2
         breakable(fixedJoint(2, 3)),      // object to object
         breakable(fixedJoint(10, 20)),    // the cloth's own actor (its id) to a brick
         breakable(fixedJoint(11, 20)),    // segment 1
         breakable(fixedJoint(13, 20)),    // segment 3
         breakable(fixedJoint(12, 20)),    // 12 is a brick as well as segment 2: the brick wins
         breakable(fixedJoint(999, 20)),   // nobody
         breakable(fixedJoint(2, 0)),      // the world as the second actor: not a thing the files do, but it resolves
         breakable(fixedJoint(0, 0))});    // world to world
    const PhysicsPlan plan = planPhysics(level, materials());
    check(plan.joints.size() == 7, "joints: the seven that resolve are planned");
    check(plan.unresolvedJoints == 2, "joints: an unknown id, and the world on both ends, are counted as unresolved");
    check(plan.joints[0].actorA == kNoActor && plan.joints[0].actorB == 1, "joints: 0 is the world, a number is an object id");
    check(plan.joints[1].actorA == 1 && plan.joints[1].actorB == 2, "joints: object to object");
    check(plan.joints[2].actorA == 3 && plan.joints[2].actorB == 8, "joints: a cloth's id is its own actor");
    check(plan.joints[3].actorA == 4 && plan.joints[4].actorA == 6,
          "joints: a cloth's id plus k is its k-th segment (nested actor k - 1)");
    check(plan.joints[5].actorA == 7, "joints: an object id wins over a cloth segment id");
    check(plan.joints[2].toCloth && plan.joints[3].toCloth && plan.joints[4].toCloth && !plan.joints[0].toCloth &&
              !plan.joints[5].toCloth,
          "joints: the ones that hold a cloth are marked");

    PhysicsScene scene(level, materials(), options(), singleThreaded());
    check(scene.counts().joints == 4 && scene.counts().clothJoints == 3 && scene.counts().jointsRefused == 0,
          "joints: the world gets the four that do not touch a cloth, and counts the rest");
}

void testJointMapping() {
    Joint hinge;
    hinge.type = NxJointType::Revolute;
    hinge.actorA = 2;
    hinge.actorB = 3;
    hinge.localAnchor[0] = {1, 0, 0};
    hinge.localAnchor[1] = {-1, 0, 0};
    hinge.localAxis[0] = hinge.localAxis[1] = {0, 0, 1};
    hinge.localNormal[0] = hinge.localNormal[1] = {1, 0, 0};
    hinge.maxForce = 150;
    hinge.maxTorque = 50;
    hinge.typeFlags = 2 | 1;  // motor and limit
    hinge.motorVelTarget = 2;
    hinge.motorMaxForce = 100;
    hinge.limitLow = {-0.5f, 0.25f, 1};
    hinge.limitHigh = {0.75f, 0.5f, 1};
    Joint ball;
    ball.type = NxJointType::Spherical;
    ball.actorA = 0;
    ball.actorB = 3;
    ball.localAxis[0] = ball.localAxis[1] = {0, 0, 1};
    ball.localNormal[0] = ball.localNormal[1] = {1, 0, 0};
    ball.maxForce = ball.maxTorque = kFltMax;
    ball.typeFlags = 2;
    ball.swingLimit = {0.6f, 0, 1};
    Joint spring = hinge;
    spring.typeFlags = 4;
    spring.spring = {30, 2, 0.5f};
    Joint distance = fixedJoint(2, 3);
    distance.type = NxJointType::Distance;
    const Level level = levelOf({board(), brick(2, {0, 0.5f, 0}), brick(3, {2, 0.5f, 0})},
                                {hinge, ball, fixedJoint(2, 3), spring, distance});
    const PhysicsPlan plan = planPhysics(level, materials());
    check(plan.joints.size() == 4 && plan.unsupportedJoints == 1, "joint map: a type the levels do not use is counted");

    const JointDesc& h = plan.joints[0].desc;
    check(h.type == JointType::Hinge && h.motorEnabled && h.motorVelocity == 2 && h.motorMaxForce == 100 &&
              h.limitEnabled && h.limitLow.value == -0.5f && h.limitHigh.value == 0.75f &&
              h.limitLow.restitution == 0.25f && !h.springEnabled,
          "joint map: a revolute joint is a hinge, with its motor and limits as flagged");
    check(h.breakForce == 150 && h.breakTorque == 50 && !h.collideConnected,
          "joint map: break thresholds are the saved ones, and joined bodies do not collide");
    const JointDesc& b = plan.joints[1].desc;
    check(b.type == JointType::Ball && b.swingLimitEnabled && b.swingLimit.value == 0.6f && !b.limitEnabled &&
              std::isinf(b.breakForce) && std::isinf(b.breakTorque),
          "joint map: a spherical joint is a ball joint with its swing limit; FLT_MAX means it never breaks");
    check(plan.joints[2].desc.type == JointType::Fixed, "joint map: a fixed joint");
    const JointDesc& s = plan.joints[3].desc;
    check(s.springEnabled && s.spring == 30 && s.damper == 2 && s.springTarget == 0.5f && !s.motorEnabled &&
              !s.limitEnabled,
          "joint map: a revolute spring");
}

void testJointFrames() {
    // Two bricks saved with their joint 0.3 apart: A's anchor at (1, 0, 0) local is (1, 0.5, 0) in the world, B's at
    // its origin (2.3, 0.5, 0.3). The axes disagree as well: B's is turned a quarter about y.
    Actor a = brick(2, {0, 0.5f, 0});
    Actor b = brick(3, {2.3f, 0.5f, 0.3f});
    const Mat4 turn = Mat4::rotationY(radians(90));
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            b.pose.rotation[static_cast<std::size_t>(i * 3 + j)] = turn.m[j][i];
        }
    }
    Joint j = fixedJoint(2, 3, {1, 0, 0}, {0, 0, 0});
    j.localAxis[0] = {0, 1, 0};
    j.localNormal[0] = {1, 0, 0};
    j.localAxis[1] = {0, 1, 0};
    j.localNormal[1] = {0, 0, 1};
    const Level level = levelOf({board(), a, b}, {j});

    const PhysicsPlan saved = planPhysics(level, materials(), options(StartState::Asleep, JointBreaking::Impact, false));
    check(near(saved.joints[0].desc.localAnchor[1], {0, 0, 0}) && near(saved.joints[0].desc.localAxis[1], {0, 1, 0}),
          "joint frames: as saved, they are the file's");

    const PhysicsPlan rebuilt = planPhysics(level, materials());
    const JointDesc& d = rebuilt.joints[0].desc;
    const Mat4 poseA = rebuilt.actors[1].pose, poseB = rebuilt.actors[2].pose;
    check(near(d.localAnchor[0], {1, 0, 0}) && near(d.localAxis[0], {0, 1, 0}),
          "joint frames: the first body's frame is as saved");
    check(near(transformPoint(d.localAnchor[1], poseB), transformPoint(d.localAnchor[0], poseA), 1e-4f),
          "joint frames: the second body's anchor is rebuilt to be where the first one's is in the world");
    check(near(transformVector(d.localAxis[1], poseB), transformVector(d.localAxis[0], poseA), 1e-4f) &&
              near(transformVector(d.localNormal[1], poseB), transformVector(d.localNormal[0], poseA), 1e-4f),
          "joint frames: and its axis and normal point the same way in the world");

    // A joint to the world: the world frame is in world space.
    Joint toWorld = breakable(fixedJoint(0, 3, {2.0f, 0.5f, 0.5f}, {0, 0, 0}));  // (an unbreakable one would weld b and go)
    const PhysicsPlan world = planPhysics(levelOf({board(), a, b}, {toWorld}), materials());
    check(world.joints.size() == 1 && near(transformPoint(world.joints[0].desc.localAnchor[1], poseB), {2.0f, 0.5f, 0.5f}, 1e-4f),
          "joint frames: for the world as the first end too");

    // Nothing is pulled together when the world starts: both bodies stay where they are.
    PhysicsScene scene(level, materials(), options(StartState::Still, JointBreaking::Impact, true), singleThreaded());
    run(scene, 1);
    check(near(scene.actorPose(1).translationPart(), {0, 0.5f, 0}, 0.02f) &&
              near(scene.actorPose(2).translationPart(), {2.3f, 0.5f, 0.3f}, 0.02f),
          "joint frames: rebuilt, a stretched joint does not move its bodies when awake");
}

void testStartStates() {
    Actor moving = brick(2, {0, 0.5f, 0});
    moving.body->linearVelocity = {1, 2, 3};
    moving.body->angularVelocity = {0.1f, 0.2f, 0.3f};
    Actor kinematic = brick(3, {3, 0.5f, 0}, 0x180);
    kinematic.body->linearVelocity = {5, 5, 5};
    const Level level = levelOf({board(), moving, kinematic});
    const auto bodyOf = [&](StartState start, std::size_t i) {
        return planPhysics(level, materials(), options(start)).actors.at(i).body;
    };
    check(bodyOf(StartState::Asleep, 1).startAsleep && bodyOf(StartState::Asleep, 2).startAsleep,
          "start: asleep puts every moving body to sleep");
    check(!bodyOf(StartState::Asleep, 0).startAsleep, "start: the static board has nothing to put to sleep");
    check(!bodyOf(StartState::SavedVelocities, 1).startAsleep && bodyOf(StartState::SavedVelocities, 1).linearVelocity.z == 3 &&
              bodyOf(StartState::SavedVelocities, 1).angularVelocity.y == 0.2f,
          "start: saved velocities keeps them, awake");
    check(!bodyOf(StartState::Still, 1).startAsleep && length(bodyOf(StartState::Still, 1).linearVelocity) == 0,
          "start: still is awake with none");
    check(length(bodyOf(StartState::SavedVelocities, 2).linearVelocity) == 0 &&
              length(bodyOf(StartState::Still, 2).linearVelocity) == 0,
          "start: a kinematic body gets no velocity, it would drift");

    PhysicsScene scene(level, materials(), options(), singleThreaded());
    check(!scene.world().isAwake(scene.body(1)) && scene.world().awakeBodyCount() == 0 &&
              length(scene.world().linearVelocity(scene.body(1))) == 0,
          "start: a level of sleeping bodies has none awake, and they hold no velocity");
    run(scene, 5);
    check(near(scene.actorPose(1).translationPart(), {0, 0.5f, 0}, 1e-6f) && scene.world().awakeBodyCount() == 0,
          "start: and nothing moves by itself");
}

void testBrickStandsAndFalls() {
    // Three bricks in a stack on the board, and one on a pedestal (a static box) near the edge.
    // (Dull: bricks that do not bounce, so that they come to rest in the time the test allows.)
    const auto dull = [](std::uint32_t id, Vec3 at) { return brick(id, at, 0, 300, {}, "Dull"); };
    Actor pedestal = actor(ObjectType::Wall, 9, {10, 0.5f, 10}, 0x17F, 100, {box({0.6f, 0.5f, 0.6f})}, "Dull");
    const Level level = levelOf({board(100, "Dull"), dull(2, {0, 0.5f, 10}), dull(3, {0, 1.5f, 10}),
                                 dull(4, {0, 2.5f, 10}), pedestal, dull(5, {10.45f, 1.5f, 10})});
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    run(scene, 3);
    check(near(scene.actorPose(2).translationPart(), {0, 1.5f, 10}, 1e-5f) &&
              near(scene.actorPose(3).translationPart(), {0, 2.5f, 10}, 1e-5f),
          "stack: a stack of sleeping bricks stands, to the bit, with nothing near");

    // The ball takes the bottom brick away; the two on it must come down.
    scene.launchBall({0, 0.6f, 0}, {0, 0, 10});
    run(scene, 8);
    const Vec3 bottom = scene.actorPose(1).translationPart();
    check(bottom.z > 12, "stack: the bottom brick is knocked well out of the stack");
    for (const ActorIndex brickIndex : {ActorIndex{2}, ActorIndex{3}}) {
        const Vec3 p = scene.actorPose(brickIndex).translationPart();
        check(p.y < (brickIndex == 2 ? 1.4f : 2.4f), "stack: a brick that rested on the one that was knocked away falls");
        // Nothing is left hanging: below the lowest point of the brick is a surface within a few millimetres.
        const auto hit = scene.world().raycast({p.x, p.y - 0.5f - 0.01f, p.z}, {0, -1, 0}, 5);
        check(hit && hit->distance < 0.05f && !scene.world().isAwake(scene.body(brickIndex)),
              "stack: and comes to rest on something, not in the air");
    }

    // The brick on the pedestal wakes when the ball hits it and falls to the board.
    PhysicsScene ledge(level, materials(), options(), singleThreaded());
    check(!ledge.world().isAwake(ledge.body(5)), "ledge: the brick on the pedestal sleeps");
    ledge.launchBall({10.4f, 1.5f, 8.5f}, {0, 0, 4});  // close, so that it has not dropped far when it arrives
    run(ledge, 5);
    check(ledge.actorPose(5).translationPart().y < 1.0f && ledge.actorPose(5).translationPart().z > 10.8f &&
              ledge.touchedByBalls().contains(5),
          "ledge: a sleeping brick that is hit wakes, is pushed off, and ends on the board");
}

void testFallsAsleepInTheAir() {
    // A brick on a pedestal that is taken away just as the brick would fall asleep (it has been still for 0.4 s at
    // step 24): left alone, Jolt puts it to sleep before it has moved far enough to matter, in mid-air.
    Actor pedestal = actor(ObjectType::Wall, 9, {0, 1, 10}, 0x17F, 100, {box({1, 1, 1})}, "Wood.jpg");
    const Level level = levelOf({board(), pedestal, brick(2, {0, 2.5f, 10}, 0, 20)});
    PhysicsScene scene(level, materials(), options(StartState::Still), singleThreaded());
    for (int i = 0; i < 22; ++i) {
        scene.step(1.0f / 60);
    }
    check(near(scene.actorPose(2).translationPart(), {0, 2.5f, 10}, 0.01f), "sleep: the brick stands on the pedestal");
    scene.world().removeBody(scene.body(1));
    float lowest = 99;
    for (int i = 0; i < 90; ++i) {
        scene.step(1.0f / 60);
        lowest = std::min(lowest, scene.actorPose(2).translationPart().y);
    }
    check(lowest < 0.6f, "sleep: a brick that falls asleep in the air as its support goes is woken, and falls");

    // A brick that gravity does not pull is meant to hover.
    Actor floater = brick(3, {5, 5, 10}, 0x101, 20);
    PhysicsScene hover(levelOf({board(), floater}), materials(), options(StartState::Still), singleThreaded());
    run(hover, 3);
    check(near(hover.actorPose(1).translationPart(), {5, 5, 10}, 1e-4f), "sleep: a body without gravity stays where it is");
}

void testJointedPiecesWakeTogether() {
    // Two bricks side by side welded by a joint that never breaks; and one far away on its own.
    Actor a = brick(2, {0, 0.5f, 10}), b = brick(3, {1, 0.5f, 10}), alone = brick(4, {-10, 0.5f, 10});
    const Level level = levelOf({board(), a, b, alone}, {fixedJoint(2, 3, {0.5f, 0, 0}, {-0.5f, 0, 0})});
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    scene.launchBall({0, 0.6f, 0}, {0, 0, 25});
    run(scene, 0.8f);
    check(scene.world().isAwake(scene.body(2)) && scene.world().isAwake(scene.body(1)),
          "joined: the brick that is hit and the one welded to it are awake");
    check(!scene.world().isAwake(scene.body(3)), "joined: a brick that is not touched or welded sleeps on");
    run(scene, 3);
    check(scene.brokenJoints().empty(), "joined: a joint that never breaks holds");
    const Vec3 pa = scene.actorPose(1).translationPart(), pb = scene.actorPose(2).translationPart();
    check(near(length(pb - pa), 1.0f, 0.02f) && pa.z > 11, "joined: the pair is shoved along together");
}

void testImpactBreaking() {
    // A brick welded to the world by a joint that breaks at 500, one at 5e5, one at FLT_MAX; hit by a ball each.
    const auto weld = [](std::uint32_t id, float force) {
        Joint j = fixedJoint(0, id, {0, 0.5f, 0}, {0, 0, 0});
        j.localAnchor[0] = {static_cast<float>(id) * 4, 0.5f, 10};
        j.maxForce = j.maxTorque = force;
        return j;
    };
    const Level level = levelOf({board(), brick(2, {8, 0.5f, 10}), brick(3, {12, 0.5f, 10}), brick(4, {16, 0.5f, 10})},
                                {weld(2, 500), weld(3, 5e5f), weld(4, kFltMax)});
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    run(scene, 2);
    check(scene.brokenJoints().empty() && scene.counts().joints == 2 && scene.plan().pinnedBodies == 1,
          "impact: joints weaker than the weight of what they hold do not break by themselves (the third is a weld)");
    for (const float x : {8.0f, 12.0f, 16.0f}) {
        scene.launchBall({x, 0.6f, 0}, {0, 0, 25});
    }
    run(scene, 3);
    check(scene.brokenJoints().size() == 1 && scene.brokenJoints()[0] == 0,
          "impact: a hit as hard as a ball's breaks the weak joint, and not the strong or the unbreakable ones");
    check(scene.actorPose(1).translationPart().z > 12 && scene.actorPose(2).translationPart().z < 10.5f &&
              scene.actorPose(3).translationPart().z < 10.5f,
          "impact: the brick that was let go flies off, the other two stay welded");

    // A slow touch is not a hit.
    PhysicsScene slow(level, materials(), options(), singleThreaded());
    slow.launchBall({8, 0.6f, 8}, {0, 0, 1.5f});
    run(slow, 3);
    check(slow.brokenJoints().empty() && slow.touchedByBalls().contains(1), "impact: a ball rolled into a brick slowly breaks nothing");

    // The world's own way, by load: the weak joint cannot hold what it holds even without a hit (the brick hangs
    // from it, 1 unit off the board).
    Actor hanging = brick(2, {8, 3, 10});
    hanging.body->flags = 0x100;
    Joint hold = fixedJoint(0, 2, {8, 3.5f, 10}, {0, 0.5f, 0});
    hold.maxForce = 20;
    hold.maxTorque = kFltMax;
    const Level hangLevel = levelOf({board(), hanging}, {hold});
    PhysicsScene byLoad(hangLevel, materials(), options(StartState::Still, JointBreaking::Load), singleThreaded());
    run(byLoad, 1);
    check(byLoad.brokenJoints().size() == 1, "load: a joint that carries more than its maxForce breaks, as PhysX's would");
    PhysicsScene byImpact(hangLevel, materials(), options(StartState::Still, JointBreaking::Impact), singleThreaded());
    run(byImpact, 1);
    check(byImpact.brokenJoints().empty(), "impact: the same joint does not break when nothing hits it");
}

void testPinnedBodies() {
    // ids: board 1; bricks 2 (welded to the world for good), 3 (by a joint that breaks at 500), 4 (twice for good),
    // 5 and 6 (welded to each other for good), 7 (kinematic, welded to the world for good).
    const Level level = levelOf({board(), brick(2, {0, 0.5f, 10}), brick(3, {4, 0.5f, 10}), brick(4, {8, 0.5f, 10}),
                                 brick(5, {12, 0.5f, 10}), brick(6, {13, 0.5f, 10}), brick(7, {16, 0.5f, 10}, 0x180)},
                                {fixedJoint(0, 2), breakable(fixedJoint(0, 3), 500), fixedJoint(0, 4), fixedJoint(4, 0),
                                 fixedJoint(5, 6, {0.5f, 0, 0}, {-0.5f, 0, 0}), fixedJoint(0, 7)});
    const PhysicsPlan plan = planPhysics(level, materials());
    check(plan.actors[1].motion == MotionType::Static && plan.actors[3].motion == MotionType::Static &&
              plan.actors[1].body.motion == MotionType::Static && plan.pinnedBodies == 2,
          "pinned: a body welded to the world by a joint that never breaks is static");
    check(plan.actors[2].motion == MotionType::Dynamic && plan.actors[4].motion == MotionType::Dynamic &&
              plan.actors[5].motion == MotionType::Dynamic && plan.actors[6].motion == MotionType::Kinematic,
          "pinned: a joint that can break, a weld between two bodies and a kinematic body are left alone");
    check(plan.joints.size() == 3 && plan.joints[0].actorB == 2 && plan.joints[1].actorA == 4 &&
              plan.joints[2].actorA == kNoActor && plan.joints[2].actorB == 6,
          "pinned: and the welds of the static ones (both of brick 4's) are dropped");

    // The fence post that a ball of 500 hits at 25 units per second stays where it is, and the breakable one goes.
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    for (const float x : {0.0f, 4.0f}) {
        scene.launchBall({x, 0.6f, 0}, {0, 0, 25});
    }
    run(scene, 3);
    check(near(scene.actorPose(1).translationPart(), {0, 0.5f, 10}, 1e-6f) && scene.touchedByBalls().contains(1),
          "pinned: a welded body hit by a ball does not move");
    check(scene.actorPose(2).translationPart().z > 12 && scene.brokenJoints().size() == 1,
          "pinned: and one on a joint that breaks does");
}

void testMachinesRun() {
    // A hub (heavy, with no gravity, so that it stays) and a sail on a hinge with a motor that turns at 2 rad/s; a brick
    // welded to the sail; a loose brick; and a wheel on a kinematic frame with a motor of its own.
    Joint motor;
    motor.type = NxJointType::Revolute;
    motor.actorA = 2;
    motor.actorB = 3;
    motor.localAnchor[0] = {0, 0, 1};
    motor.localAnchor[1] = {0, 0, -1};
    motor.localAxis[0] = motor.localAxis[1] = {0, 0, 1};
    motor.localNormal[0] = motor.localNormal[1] = {1, 0, 0};
    motor.maxForce = motor.maxTorque = 1e6f;
    motor.typeFlags = 2;
    motor.motorVelTarget = 2;
    motor.motorMaxForce = 1e4f;
    Joint wheelMotor = motor;
    wheelMotor.actorA = 6;  // the frame
    wheelMotor.actorB = 7;  // the wheel
    wheelMotor.motorVelTarget = 1;

    Actor hub = brick(2, {0, 5, 10}, 0x101, 1e5f);
    Actor sail = brick(3, {0, 5, 12}, 0x101, 5);
    sail.body->angularVelocity = {0, 0, -2};  // as the level was saved: already turning
    Actor weldedToSail = brick(4, {1.5f, 5, 12}, 0x101, 1);
    Actor loose = brick(5, {-8, 0.5f, 10});
    Actor frame = brick(6, {10, 5, 10}, 0x181, 1000);
    Actor wheel = brick(7, {10, 5, 12}, 0x101, 50);
    const Level level = levelOf({board(), hub, sail, weldedToSail, loose, frame, wheel},
                                {motor, breakable(fixedJoint(3, 4, {0.75f, 0, 0}, {-0.75f, 0, 0})), wheelMotor});
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    const PhysicsPlan& plan = scene.plan();
    check(!plan.actors[1].body.startAsleep && !plan.actors[2].body.startAsleep && !plan.actors[3].body.startAsleep &&
              plan.actors[4].body.startAsleep && !plan.actors[6].body.startAsleep,
          "machines: the bodies joined to a motor start awake (the hub, the sail, what is welded to it, the wheel), a loose one asleep");
    check(scene.world().isAwake(scene.body(1)) && scene.world().isAwake(scene.body(3)) &&
              !scene.world().isAwake(scene.body(4)),
          "machines: and so they are in the world");
    check(near(scene.world().angularVelocity(scene.body(2)).z, -2, 1e-4f),
          "machines: with the velocity they were saved with");

    run(scene, 2);
    // PhysX's motor turns body A forwards and body B backwards: the sail, B of the joint, spins at -2 about the axis.
    check(near(scene.world().angularVelocity(scene.body(2)).z, -2, 0.1f) &&
              near(scene.world().angularVelocity(scene.body(1)).z, 0, 0.05f),
          "machines: the motor keeps the sail turning the way it drives body B (-2 rad/s about the axis, the hub still)");
    check(near(scene.world().angularVelocity(scene.body(3)).z, -2, 0.1f), "machines: so does what is welded to it");
    check(near(scene.world().angularVelocity(scene.body(6)).z, -1, 0.1f) && scene.world().isAwake(scene.body(6)),
          "machines: a wheel on a kinematic frame turns at its motor's speed");
    check(!scene.world().isAwake(scene.body(4)) && near(scene.actorPose(4).translationPart(), {-8, 0.5f, 10}, 1e-6f),
          "machines: and a loose brick next to them sleeps on");
}

void testHoveringBrickFalls() {
    // A brick 5 cm above what holds it, as the levels have them: no contact joins them, so only a look under it finds
    // out that the support has gone.
    const auto dull = [](std::uint32_t id, Vec3 at) { return brick(id, at, 0, 100, {}, "Dull"); };
    Actor pedestal = actor(ObjectType::Wall, 9, {0, 0.5f, 10}, 0x17F, 100, {box({0.6f, 0.5f, 0.6f})}, "Dull");
    const Level onPedestal = levelOf({board(30, "Dull"), pedestal, dull(2, {0, 1.55f, 10})});
    PhysicsScene alone(onPedestal, materials(), options(), singleThreaded());
    run(alone, 3);
    check(near(alone.actorPose(2).translationPart(), {0, 1.55f, 10}, 1e-6f) && !alone.world().isAwake(alone.body(2)),
          "hover: left alone it stays where the level put it, asleep");
    alone.world().removeBody(alone.body(1));
    run(alone, 2);
    check(alone.actorPose(2).translationPart().y < 0.6f && !alone.world().isAwake(alone.body(2)),
          "hover: when what it hovers over is taken out of the world, it is woken, falls to the board and rests there");

    // The same with a brick: the ball pushes the lower one away, and the upper one comes down behind it.
    const Level stacked = levelOf({board(30, "Dull"), dull(2, {0, 0.5f, 10}), dull(3, {0, 1.55f, 10})});
    PhysicsScene scene(stacked, materials(), options(), singleThreaded());
    scene.launchBall({0, 0.4f, 0}, {0, 0, 12});
    run(scene, 10);
    check(scene.actorPose(1).translationPart().z > 11 && scene.actorPose(2).translationPart().y < 0.6f &&
              !scene.world().isAwake(scene.body(2)),
          "hover: a brick knocked away from under another one that hovers over it lets that one fall");
}

void testBreakingWakesPartners() {
    // Two bricks welded together; the ball hits one, and the weld breaks in that very step. The other brick must be
    // awake when it does, or it would stay asleep in the air wherever the weld left it.
    const Level level = levelOf({board(), brick(2, {0, 0.5f, 10}), brick(3, {2, 0.5f, 10}, 0, 10)},
                                {breakable(fixedJoint(2, 3, {1, 0, 0}, {-1, 0, 0}), 100)});
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    scene.launchBall({0, 0.6f, 0}, {0, 0, 25});
    for (int i = 0; i < 180 && scene.brokenJoints().empty(); ++i) {
        scene.step(1.0f / 60);
    }
    check(scene.brokenJoints().size() == 1, "break: the weld breaks under the ball");
    check(scene.world().isAwake(scene.body(1)) && scene.world().isAwake(scene.body(2)),
          "break: and both bricks it held are awake in the step it breaks in");
}

void testKinematicBodiesAreImmovable() {
    // A ball that hits a kinematic brick (a plank on a path, say) strikes with its own mass at its own speed; a weld
    // between it and a dynamic brick that is weaker than that breaks, however light the kinematic brick is.
    Actor plank = brick(2, {0, 0.5f, 10}, 0x180, 1);
    const Level level = levelOf({board(), plank, brick(3, {4, 0.5f, 10})}, {breakable(fixedJoint(2, 3), 100000)});
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    scene.launchBall({0, 0.6f, 0}, {0, 0, 25});
    run(scene, 2);
    check(scene.brokenJoints().size() == 1, "kinematic: a hit on a kinematic body counts it as immovable");
}

void testOldLevelsAreNotSimulated() {
    Level level = levelOf({board(), brick(2, {0, 0.5f, 10})}, {fixedJoint(0, 2)});
    level.version = 2;
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    check(scene.counts().dynamicBodies == 0 && scene.counts().withoutBody == 2 && scene.counts().joints == 0 &&
              scene.world().bodyCount() == 0,
          "version: a level from before the physics data was decoded is not simulated, instead of refused body by body");
    check(near(scene.actorPose(1).translationPart(), {0, 0.5f, 10}), "version: and its actors stay where they were saved");
}

void testRemoveActor() {
    // A pillar (actor 1) with a crate asleep on it (2), and a crate (3) hung in the air from a block (4) by a joint.
    Actor pillar = brick(1, {0, 0.5f, 10}, 0x180);
    Actor stacked = brick(2, {0, 1.5f, 10}, 0x100, 10, {box({0.5f, 0.5f, 0.5f})});
    Actor hanging = brick(12, {5, 3, 10}, 0x100, 10, {box({0.5f, 0.5f, 0.5f})});
    Actor holder = brick(11, {5, 1, 10}, 0x180);
    const Level level = levelOf({board(), pillar, stacked, hanging, holder}, {fixedJoint(11, 12, {0, 2, 0}, {0, 0, 0})});
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    run(scene, 0.5f);
    const std::size_t bodies = scene.world().bodyCount();
    check(!scene.world().isAwake(scene.body(2)) && scene.held(3), "remove: the crate on the pillar sleeps, the hanging one is held");

    scene.removeActor(1);
    check(scene.body(1) == 0 && scene.world().bodyCount() == bodies - 1, "remove: the actor's body is out of the world");
    scene.removeActor(1);
    check(scene.world().bodyCount() == bodies - 1, "remove: and removing it again does nothing");
    scene.step(1.0f / 60);
    check(scene.world().isAwake(scene.body(2)), "remove: what rested on it is woken by the very next step");
    // (Wood bounces on the board with a restitution of 1, so it is the lowest point that shows it fell.)
    const auto lowest = [&](ActorIndex actor, float seconds) {
        float low = 1e9f;
        for (int i = 0, steps = static_cast<int>(seconds * 60); i < steps; ++i) {
            scene.step(1.0f / 60);
            low = std::min(low, scene.actorPose(actor).translationPart().y);
        }
        return low;
    };
    check(lowest(2, 1.5f) < 0.6f, "remove: and falls to the floor");

    scene.removeActor(4);  // what holds the hanging crate
    check(!scene.held(3) && scene.world().isAwake(scene.body(3)) && scene.brokenJoints().empty(),
          "remove: a piece joined to the removed one is let go and woken, and the joint is not counted as broken");
    check(lowest(3, 1.5f) < 0.6f, "remove: and that one falls to the floor");

    // The contacts of a step are there to read.
    PhysicsScene shooting(levelOf({board(), brick(2, {0, 0.5f, 6})}), materials(), options(), singleThreaded());
    shooting.addGameBall({0, 0.6f, 0}, {0, 0, 15});
    bool contactWithBrick = false;
    for (int i = 0; i < 60 && !contactWithBrick; ++i) {
        shooting.step(1.0f / 60);
        for (const ContactEvent& contact : shooting.contacts()) {
            contactWithBrick = contactWithBrick || ((contact.userDataA == kGameBallData && contact.userDataB == 1) ||
                                                    (contact.userDataB == kGameBallData && contact.userDataA == 1));
        }
    }
    check(contactWithBrick && shooting.balls().empty() && shooting.touchedByBalls().empty(),
          "contacts: the game ball's contact with the brick is in contacts(), and it is no test ball");
}

// A straight path along +x at height y, and a binding of `speed` (units a millisecond) for the objects with these ids, each
// starting at its own x.
void addPath(Level& level, const std::string& name, float y, float z, float speed,
             std::vector<std::pair<std::uint32_t, float>> objects) {
    Path path;
    path.name = name;
    path.points = {PathPoint{{-10, y, z}, {}, {1, 0, 0}, {0, 1, 0}}, PathPoint{{30, y, z}, {}, {1, 0, 0}, {0, 1, 0}}};
    level.paths.push_back(path);
    PathTrack track;
    track.speed = speed;
    for (const auto& [id, x] : objects) {
        track.keys.push_back({{x, y, z}, {1, 0, 0}, {0, 1, 0}, id});
    }
    level.pathBindings.push_back({name, {track}});
}

void testPathActors() {
    // A shark (a kinematic box) swims along +x at 5 units a second from x = -10, with a light brick 6 units ahead of its
    // nose, and a second one on a path of its own further along.
    const auto shark = [](std::uint32_t id, Vec3 at) {
        return actor(ObjectType::Brick, id, at, 0x180, 50, {box({1, 0.5f, 1})}, "Wood.jpg");
    };
    Level level = levelOf({board(100), shark(2, {-10, 0.5f, 0}), brick(3, {-3, 0.5f, 0}, 0, 10), shark(4, {-10, 0.5f, 10})});
    addPath(level, "track/shark", 0.5f, 0, 0.005f, {{2, -10}});
    addPath(level, "track/other", 0.5f, 10, 0.004f, {{4, -10}});
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    check(scene.pathMovers().size() == 2 && scene.counts().pathMovers == 2 && scene.counts().kinematicBodies == 2,
          "paths: the two kinematic actors of the bindings are movers");
    check(scene.pathMovers()[0].actor == 1 && scene.pathMovers()[1].actor == 3, "paths: by actor index");
    check(near(scene.actorPose(1).translationPart(), {-10, 0.5f, 0}, 1e-5f) && scene.pathTime() == 0,
          "paths: the shark starts where its path starts");

    run(scene, 0.5f);
    check(near(scene.actorPose(1).translationPart(), {-7.5f, 0.5f, 0}, 2e-3f) &&
              near(scene.actorPose(3).translationPart(), {-8, 0.5f, 10}, 2e-3f) && near(scene.pathTime(), 0.5, 1e-4),
          "paths: it swims at the track's speed, five units a second (0.005 a millisecond), the other at four");
    const Mat4 pose = scene.actorPose(1);
    check(near(pose.m[2][0], 1, 1e-4f) && near(pose.m[1][1], 1, 1e-4f) && near(pose.m[0][2], -1, 1e-4f),
          "paths: it faces along the path, upright");
    check(near(scene.actorPose(2).translationPart(), {-3, 0.5f, 0}, 1e-5f), "paths: the brick lies untouched until the shark comes");

    run(scene, 1.5f);  // t = 2: the shark's middle is at x = 0
    const Vec3 shoved = scene.actorPose(2).translationPart();
    check(near(scene.actorPose(1).translationPart(), {0, 0.5f, 0}, 2e-3f), "paths: and goes on: x = 0 at two seconds");
    check(shoved.x > scene.actorPose(1).translationPart().x + 1.4f && shoved.x < 12,
          "paths: the shark pushes the brick ahead of it, " + std::to_string(shoved.x));

    // A body is where the path has it at the end of every fixed step, however many a call takes.
    PhysicsScene coarse(level, materials(), options(), singleThreaded());
    coarse.step(8.0f / 60);
    check(near(coarse.pathTime(), 8.0 / 60, 1e-6) && near(coarse.actorPose(1).translationPart(), {-10 + 5 * 8.0f / 60, 0.5f, 0}, 2e-3f),
          "paths: one call of eight fixed steps gives each its own target");

    // An actor that is taken out of the world stops; the other goes on.
    PhysicsScene removed(level, materials(), options(), singleThreaded());
    run(removed, 0.5f);
    removed.removeActor(1);
    run(removed, 1);
    check(removed.body(1) == 0 && near(removed.actorPose(1).translationPart(), {-10, 0.5f, 0}, 1e-5f) &&
              near(removed.actorPose(3).translationPart(), {-4, 0.5f, 10}, 2e-3f),
          "paths: a destroyed shark is gone and no longer driven, the other swims on");

    // The saved pose is where it was a frame or two before; the actor begins on its path instead, and that is not a push.
    Level astray = levelOf({board(100), shark(2, {-9, 0.5f, 3}), brick(3, {-8.5f, 0.5f, 3}, 0, 10)});
    addPath(astray, "track/shark", 0.5f, 0, 0.005f, {{2, -10}});
    PhysicsScene start(astray, materials(), options(), singleThreaded());
    check(near(start.actorPose(1).translationPart(), {-10, 0.5f, 0}, 1e-5f), "paths: the actor starts on its path, not where it was saved");
    run(start, 0.1f);
    check(near(start.actorPose(2).translationPart(), {-8.5f, 0.5f, 3}, 1e-4f),
          "paths: it was put there before anything ran, so it pushed nothing out of the way");

    // A path actor that is not kinematic is left alone.
    Level loose = levelOf({board(100), brick(2, {-10, 5, 0}, 0, 10)});
    addPath(loose, "track/shark", 0.5f, 0, 0.005f, {{2, -10}});
    PhysicsScene falling(loose, materials(), options(StartState::Still), singleThreaded());
    run(falling, 0.5f);
    check(falling.pathMovers().empty() && falling.counts().pathMovers == 0 &&
              falling.actorPose(1).translationPart().y < 4.5f && near(falling.actorPose(1).translationPart().x, -10, 1e-3f),
          "paths: a dynamic body bound to a path is no mover, it just falls");
}

void testBallAndBodies() {
    const Level level = levelOf({board(), brick(2, {0, 0.5f, 10})});
    PhysicsScene scene(level, materials(), options(), singleThreaded());
    const BodyId ball = scene.launchBall({0, 1.2f, 0}, {0, 0, 25});
    check(ball != 0 && scene.balls().size() == 1 && scene.balls()[0].body == ball, "ball: launched");
    check(near(scene.world().pose(ball).translationPart(), {0, 1.2f, 0}) &&
              near(scene.world().linearVelocity(ball), {0, 0, 25}),
          "ball: at its position, at its speed");
    run(scene, 0.2f);
    check(scene.world().pose(ball).translationPart().y < 1.15f, "ball: gravity pulls it down");
    run(scene, 1);
    check(scene.touchedByBalls().contains(1), "ball: it touched the brick");
    check(!scene.touchedByBalls().contains(kNoActor), "ball: and never counts itself");

    // Swept collision: a ball that moves 2.5 units in a step does not go through a wall 2 cm thick.
    Actor wall = actor(ObjectType::Wall, 5, {0, 2, 10}, 0x17F, 100, {box({5, 2, 0.01f})}, "Wood.jpg");
    PhysicsScene fast(levelOf({board(), wall}), materials(), options(), singleThreaded());
    fast.launchBall({0, 1.0f, 0}, {0, 0, 150});
    run(fast, 0.5f);
    check(fast.world().pose(fast.balls()[0].body).translationPart().z < 10, "ball: a fast one is stopped by a thin wall");

    // A ball that is old is taken out.
    PhysicsScene old(levelOf({board()}), materials(), options(), singleThreaded());
    old.launchBall({0, 0.6f, 0}, {0, 0, 0});
    run(old, 19);
    check(old.balls().size() == 1, "ball: it stays for 19 s");
    run(old, 2);
    check(old.balls().empty() && old.world().bodyCount() == 1, "ball: and is removed after 20 s, body and all");

    // One that leaves the board and falls out of the world goes too.
    Actor island = actor(ObjectType::Board, 1, {}, 0xFF, 1000, {box({5, 0.5f, 5}, {0, -0.5f, 0}, 4)});
    PhysicsScene falling(levelOf({island}), materials(), options(), singleThreaded());
    falling.launchBall({0, 0.6f, 0}, {0, 0, 25});
    run(falling, 1);
    check(falling.balls().size() == 1 && falling.world().pose(falling.balls()[0].body).translationPart().y < 0,
          "ball: it rolls off the edge of the board and drops");
    run(falling, 3);
    check(falling.balls().empty(), "ball: and is removed once it is below y = -30");
}

void testGameBallAndBat() {
    // The ball of the game stays on its plane and turns only about the vertical, whatever it hits: a wedge turned 35 degrees
    // about x pushes it up and back, and it leaves the plane neither in position nor in spin.
    Actor wedge = actor(ObjectType::Wall, 5, {0, 1, 10}, 0x17F, 100, {box({3, 1.5f, 1})}, "Wood.jpg");
    const float c = std::cos(radians(35.0f)), sn = std::sin(radians(35.0f));
    wedge.pose.rotation = {1, 0, 0, 0, c, -sn, 0, sn, c};
    PhysicsScene scene(levelOf({board(), wedge}), materials(), options(), singleThreaded());
    const BodyId ball = scene.addGameBall({0.5f, 0.6f, 0}, {0, 0, 15});
    bool pushedUp = false, flat = true;
    for (int i = 0; i < 90; ++i) {
        scene.step(1.0f / 60);
        for (const ContactEvent& contact : scene.contacts()) {
            pushedUp = pushedUp || ((contact.userDataA == 1 || contact.userDataB == 1) && std::fabs(contact.normal.y) > 0.3f);
        }
        const Vec3 spin = scene.world().angularVelocity(ball);
        flat = flat && near(scene.world().pose(ball).translationPart().y, 0.6f, 1e-4f) && near(spin.x, 0, 1e-4f) &&
               near(spin.z, 0, 1e-4f);
    }
    check(pushedUp && flat, "game ball: a face that leans does not lift it or tip it over");
    check(scene.world().linearVelocity(ball).z < 0, "game ball: it bounced back off the wedge");

    // Swept collision: at 150 units/s it moves 2.5 units in a step, and does not go through a wall 2 cm thick.
    Actor thin = actor(ObjectType::Wall, 5, {0, 2, 10}, 0x17F, 100, {box({5, 2, 0.01f})}, "Wood.jpg");
    PhysicsScene fast(levelOf({board(), thin}), materials(), options(), singleThreaded());
    const BodyId shot = fast.addGameBall({0, 0.6f, 0}, {0, 0, 150});
    run(fast, 0.5f);
    check(fast.world().pose(shot).translationPart().z < 10, "game ball: a fast one is stopped by a thin wall");

    // The bat puts no spin on the ball, however it grazes it, and a bat that is held still does not hold the ball: friction
    // would turn it (and keep it).
    PhysicsScene bat(levelOf({board()}), materials(), options(), singleThreaded());
    const BodyId batBody = bat.addBat({0, 0.6f, -21}, 3);
    const BodyId grazed = bat.addGameBall({3.2f, 0.6f, -10}, {0, 0, -15});
    float spin = 0;
    for (int i = 0; i < 90; ++i) {
        bat.step(1.0f / 60);
        spin = std::max(spin, std::fabs(bat.world().angularVelocity(grazed).y));
    }
    check(batBody != 0 && bat.world().linearVelocity(grazed).x > 1 && near(spin, 0, 1e-3f),
          "bat: a ball that grazes it is turned aside and gets no spin, " + std::to_string(spin));
}

}  // namespace

int main() {
    testFlattenActors();
    testOrthonormalised();
    testMotionTypes();
    testBodyValues();
    testPoseIsRigid();
    testShapes();
    testMaterials();
    testCollisionGroups();
    testJointResolution();
    testJointMapping();
    testJointFrames();
    testStartStates();
    testBrickStandsAndFalls();
    testFallsAsleepInTheAir();
    testJointedPiecesWakeTogether();
    testImpactBreaking();
    testPinnedBodies();
    testMachinesRun();
    testHoveringBrickFalls();
    testBreakingWakesPartners();
    testKinematicBodiesAreImmovable();
    testOldLevelsAreNotSimulated();
    testRemoveActor();
    testPathActors();
    testBallAndBodies();
    testGameBallAndBat();
    if (failures == 0) {
        std::puts("all physics scene tests passed");
    }
    return failures == 0 ? 0 : 1;
}
