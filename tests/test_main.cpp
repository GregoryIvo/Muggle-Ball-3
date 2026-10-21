// Minimal self-contained tests (no game data and no display needed).

#include "mb/engine/Window.h"
#include "mb/game/LevelLoader.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

constexpr const char* kLevel = R"({
  "gravity": [0, -9.81, 0],
  "actors": [
    {"pose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [0,0,0]},
     "shapes": [{"type": "box", "localPose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [0,1,0]},
                 "halfExtents": [3, 22, 15]}],
     "object": {"type": "board", "id": 2, "flags": 518, "material": "boardmaterial", "mesh": "boardsea.x", "extra": "03ff"}},
    {"pose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [4.7, 0.98, 3.29]},
     "body": "00",
     "shapes": [{"type": "sphere", "localPose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [0,0,0]},
                 "radius": 1.5},
                {"type": "capsule", "localPose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [0,0,0]},
                 "radiusHeight": [0.9, 9.8]}],
     "object": {"type": "cloth", "id": 7, "flags": 0,
                "cloth": {"width": 2, "height": 1, "params": [0,0,0], "segments": [
                  {"pose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [1,0,0]}, "shapes": [],
                   "object": {"type": "brick", "id": 8, "flags": 0}}]}}},
    {"pose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [0,0,0]}, "shapes": []}
  ],
  "paths": [{"name": "track/shark", "points": [{"pos": [1,2,3], "tng": [0,0,0], "dir": [1,0,0], "up": [0,1,0]}]}]
})";

// Counts the warnings logged while alive, and keeps the last one.
class WarningCapture {
public:
    WarningCapture() {
        SDL_GetLogOutputFunction(&previous_, &previousData_);
        SDL_SetLogOutputFunction(&WarningCapture::onLog, this);
    }
    ~WarningCapture() { SDL_SetLogOutputFunction(previous_, previousData_); }
    WarningCapture(const WarningCapture&) = delete;
    WarningCapture& operator=(const WarningCapture&) = delete;

    int count = 0;
    std::string last;

private:
    static void onLog(void* self, int, SDL_LogPriority priority, const char* message) {
        auto* capture = static_cast<WarningCapture*>(self);
        if (priority == SDL_LOG_PRIORITY_WARN) {
            ++capture->count;
            capture->last = message;
        }
    }

    SDL_LogOutputFunction previous_ = nullptr;
    void* previousData_ = nullptr;
};

void testLevelVersion() {
    check(mb::parseLevelJson(kLevel).version == 1, "a level without a version is version 1");
    check(mb::Level{}.version == mb::kLevelJsonVersion, "a new level is the current version");
    const mb::Level current = mb::parseLevelJson(R"({"version":3,"gravity":[0,0,0],"actors":[]})");
    check(current.version == 3 && current.version == mb::kLevelJsonVersion, "version 3 is read");

    bool threw = false;
    try {
        mb::parseLevelJson(R"({"version":"two","gravity":[0,0,0],"actors":[]})");
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "a version that is not a number is rejected");

    // Loading a file converted before versions existed warns once, naming the file; a current one does not.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "mb3_test_level_version";
    std::filesystem::create_directories(dir);
    const std::filesystem::path oldFile = dir / "old.json", newFile = dir / "new.json";
    std::ofstream(oldFile) << R"({"gravity":[0,0,0],"actors":[]})";
    std::ofstream(newFile) << R"({"version":3,"gravity":[0,0,0],"actors":[]})";
    {
        WarningCapture capture;
        check(mb::loadLevelJson(oldFile).version == 1, "an old file loads as version 1");
        check(capture.count == 1, "an old level file logs exactly one warning");
        check(capture.last.find(oldFile.string()) != std::string::npos &&
                  capture.last.find("convert_assets.py") != std::string::npos,
              "the warning names the file and the converter");
    }
    {
        WarningCapture capture;
        check(mb::loadLevelJson(newFile).version == mb::kLevelJsonVersion, "a current file loads as the current version");
        check(capture.count == 0, "a current level file logs no warning");
    }
    std::filesystem::remove_all(dir);
}

// Every field tools/mb3level.py writes for bodies, actors, shapes and joints (version 3).
constexpr const char* kPhysicsLevel = R"({
  "version": 3,
  "gravity": [0, -9.8, 0],
  "actors": [
    {"offset": 100, "pose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [1,2,3]},
     "density": 2.0, "actorFlags": 1, "group": 5,
     "body": {"massLocalPose": {"rotation": [0,1,0,-1,0,0,0,0,1], "position": [0.5,1.5,-2.5]},
              "massSpaceInertia": [4.5,5.5,6.5], "mass": 12.5,
              "linearVelocity": [0.25,-0.5,0.75], "angularVelocity": [1,2,3],
              "wakeUpCounter": 0, "linearDamping": 0.1, "angularDamping": 0.8, "maxAngularVelocity": 5,
              "flags": 384, "sleepLinearVelocity": 0.2, "sleepAngularVelocity": 0.3, "solverIterationCount": 6,
              "raw": "00"},
     "shapes": [
       {"type": "box", "localPose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [0,0,0]},
        "halfExtents": [1,2,3], "shapeFlags": 15, "group": 5, "materialIndex": 157, "skinWidth": 0.005, "raw": "00"},
       {"type": "convex", "localPose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [0,14.5,0]},
        "meshName": "rock.cnvx", "hullPoints": [[0,0,0],[2,0,0],[0,3,0],[0,0,4]], "shapeFlags": 8, "group": 2,
        "materialIndex": 7, "skinWidth": 0.005, "raw": "00"}],
     "object": {"type": "brick", "id": 10, "flags": 0}},
    {"offset": 200, "pose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [0,0,0]}, "body": "00",
     "shapes": [{"type": "sphere", "localPose": {"rotation": [1,0,0,0,1,0,0,0,1], "position": [0,0,0]}, "radius": 1}],
     "object": {"type": "brick", "id": 11, "flags": 0}}
  ],
  "joints": [
    {"actorA": 0, "actorB": 10, "type": 8,
     "localNormal": [[1,0,0],[0,1,0]], "localAxis": [[0,0,1],[0,0,-1]], "localAnchor": [[1.5,2.5,3.5],[-4.5,5.5,6.5]],
     "maxForce": 500, "maxTorque": 400, "jointFlags": 2, "raw": "0800"},
    {"actorA": 10, "actorB": 11, "type": 1,
     "localNormal": [[1,0,0],[1,0,0]], "localAxis": [[0,0,1],[0,0,1]], "localAnchor": [[0,0,0],[0,0,0]],
     "maxForce": 3.4028235e+38, "maxTorque": 3.4028235e+38, "jointFlags": 0,
     "limit": {"low": {"value": -0.5, "restitution": 0.25, "hardness": 0.75}, "high": {"value": 1.5, "restitution": 0.125, "hardness": 0.5}},
     "motor": {"velTarget": 3, "maxForce": 80, "freeSpin": true},
     "spring": {"spring": 10, "damper": 2, "targetValue": 0.5},
     "projectionDistance": 1, "projectionAngle": 0.0872, "flags": 7, "projectionMode": 1, "raw": "01"},
    {"actorA": 11, "actorB": 10, "type": 3,
     "localNormal": [[1,0,0],[1,0,0]], "localAxis": [[0,0,1],[0,0,1]], "localAnchor": [[0,0,0],[0,0,0]],
     "maxForce": 100, "maxTorque": 100, "jointFlags": 0,
     "swingAxis": [0,1,0], "projectionDistance": 1,
     "twistLimit": {"low": {"value": -1, "restitution": 0.1, "hardness": 0.9}, "high": {"value": 1, "restitution": 0.2, "hardness": 0.8}},
     "swingLimit": {"value": 0.7, "restitution": 0.3, "hardness": 0.6},
     "twistSpring": {"spring": 1, "damper": 2, "targetValue": 3}, "swingSpring": {"spring": 4, "damper": 5, "targetValue": 6},
     "jointSpring": {"spring": 7, "damper": 8, "targetValue": 9}, "flags": 31, "projectionMode": 0, "raw": "03ff"},
    {"actorA": 10, "actorB": 11, "type": 8, "raw": "08"}
  ]
})";

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

void testPhysicsFields() {
    const mb::Level level = mb::parseLevelJson(kPhysicsLevel);
    const mb::Actor& brick = level.actors.at(0);
    check(brick.density == 2.0f && brick.actorFlags == 1 && brick.group == 5, "actor density, flags and group");

    check(brick.body.has_value(), "body present");
    const mb::Body& body = *brick.body;
    check(body.massLocalPose.rotation[1] == 1.0f && body.massLocalPose.rotation[3] == -1.0f &&
              body.massLocalPose.position.y == 1.5f && body.massLocalPose.position.z == -2.5f,
          "body massLocalPose");
    check(body.massSpaceInertia.x == 4.5f && body.massSpaceInertia.z == 6.5f && body.mass == 12.5f, "body inertia and mass");
    check(body.linearVelocity.y == -0.5f && body.angularVelocity.z == 3.0f, "body velocities");
    check(body.wakeUpCounter == 0.0f && near(body.linearDamping, 0.1f) && near(body.angularDamping, 0.8f) &&
              body.maxAngularVelocity == 5.0f,
          "body wake counter, damping, max angular velocity");
    check(body.flags == 384 && body.kinematic() && near(body.sleepLinearVelocity, 0.2f) &&
              near(body.sleepAngularVelocity, 0.3f) && body.solverIterationCount == 6,
          "body flags, sleep thresholds, solver iterations");

    const mb::Shape& box = brick.shapes.at(0);
    check(box.shapeFlags == 15 && box.group == 5 && box.materialIndex == 157 && near(box.skinWidth, 0.005f),
          "shape flags, group, material, skin width");
    check(box.mass == 0 && box.density == 0 && box.hullPoints.empty(), "shape without mass, density or hull");
    const mb::Shape& convex = brick.shapes.at(1);
    check(convex.type == mb::ShapeType::Convex && convex.meshName == "rock.cnvx" && convex.hullPoints.size() == 4 &&
              convex.hullPoints[3].z == 4.0f && convex.hullPoints[1].x == 2.0f,
          "convex hull points");
    check(convex.localPose.position.y == 14.5f, "convex local pose");

    // The older form: a body that was only a hex string is a body with default mass properties.
    const mb::Actor& legacy = level.actors.at(1);
    check(legacy.body.has_value() && legacy.body->mass == 0 && legacy.body->angularDamping == 0.05f &&
              legacy.body->solverIterationCount == 4 && !legacy.body->kinematic(),
          "body given as a hex string");
    check(legacy.density == 0 && legacy.actorFlags == 0 && legacy.group == 0 && legacy.shapes[0].skinWidth == -1.0f,
          "actor and shape fields default when missing");

    check(level.joints.size() == 4, "joint count");
    const mb::Joint& fixed = level.joints[0];
    check(fixed.type == mb::NxJointType::Fixed && fixed.actorA == 0 && fixed.actorB == 10, "fixed joint actors");
    check(fixed.localNormal[1].y == 1.0f && fixed.localAxis[1].z == -1.0f && fixed.localAnchor[0].x == 1.5f &&
              fixed.localAnchor[1].z == 6.5f,
          "joint frames");
    check(fixed.maxForce == 500.0f && fixed.maxTorque == 400.0f && fixed.jointFlags == 2, "joint break thresholds and flags");
    check(fixed.raw == std::vector<std::uint8_t>{0x08, 0x00}, "joint raw bytes");

    const mb::Joint& hinge = level.joints[1];
    check(hinge.type == mb::NxJointType::Revolute && hinge.maxForce == std::numeric_limits<float>::max(), "unbreakable revolute joint");
    check(hinge.limitLow.value == -0.5f && hinge.limitLow.restitution == 0.25f && hinge.limitLow.hardness == 0.75f &&
              hinge.limitHigh.value == 1.5f && hinge.limitHigh.restitution == 0.125f && hinge.limitHigh.hardness == 0.5f,
          "revolute limits");
    check(hinge.motorVelTarget == 3.0f && hinge.motorMaxForce == 80.0f && hinge.motorFreeSpin, "revolute motor");
    check(hinge.spring.spring == 10.0f && hinge.spring.damper == 2.0f && hinge.spring.targetValue == 0.5f, "revolute spring");
    check(hinge.projectionDistance == 1.0f && near(hinge.projectionAngle, 0.0872f) && hinge.typeFlags == 7 &&
              hinge.projectionMode == 1,
          "revolute projection and flags");

    const mb::Joint& ball = level.joints[2];
    check(ball.type == mb::NxJointType::Spherical && ball.swingAxis.y == 1.0f, "spherical joint swing axis");
    check(ball.limitLow.value == -1.0f && near(ball.limitLow.hardness, 0.9f) && ball.limitHigh.value == 1.0f &&
              near(ball.limitHigh.restitution, 0.2f),
          "spherical twist limit");
    check(near(ball.swingLimit.value, 0.7f) && near(ball.swingLimit.restitution, 0.3f) && near(ball.swingLimit.hardness, 0.6f),
          "spherical swing limit");
    check(ball.twistSpring.targetValue == 3.0f && ball.swingSpring.spring == 4.0f && ball.spring.damper == 8.0f,
          "spherical springs");
    check(ball.typeFlags == 31 && ball.projectionMode == 0 && !ball.motorFreeSpin, "spherical flags");

    // The older form of a joint: actors, type and raw bytes only. It must not break on the first step.
    const mb::Joint& bare = level.joints[3];
    check(bare.maxForce == std::numeric_limits<float>::max() && bare.maxTorque == std::numeric_limits<float>::max() &&
              bare.limitLow.hardness == 1.0f && bare.raw.size() == 1,
          "joint without decoded fields");
}

void pushMotion(float dx, float dy) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.xrel = dx;
    e.motion.yrel = dy;
    SDL_PushEvent(&e);
}

void pushButton(Uint8 button, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = button;
    e.button.down = down;
    SDL_PushEvent(&e);
}

// Scripts mouse events into a Window on SDL's dummy video driver, so no display is needed.
void testDragAttribution() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    std::unique_ptr<mb::Window> window;
    try {
        window = std::make_unique<mb::Window>("test", 64, 64);
    } catch (const std::exception& e) {
        std::printf("skipped the drag attribution test: %s\n", e.what());
        return;
    }
    mb::InputEvents in;

    // Motion before a press and after a release in the same poll belongs to no drag.
    pushMotion(1, 10);
    pushButton(SDL_BUTTON_LEFT, true);
    pushMotion(2, 20);
    pushButton(SDL_BUTTON_LEFT, false);
    pushMotion(4, 40);
    window->pollEvents(in);
    check(in.mouseDX == 7 && in.mouseDY == 70, "total motion counts every event");
    check(in.leftDragDX == 2 && in.leftDragDY == 20, "left drag counts only motion while the button was down");
    check(!in.leftButton, "button released at the end of the poll");
    check(in.rightDragDX == 0 && in.middleDragDX == 0, "other buttons saw no drag");

    // A button held across polls keeps dragging; the deltas start from zero in every poll.
    pushButton(SDL_BUTTON_LEFT, true);
    pushMotion(3, 0);
    window->pollEvents(in);
    check(in.leftDragDX == 3 && in.leftButton, "drag starts in the poll with the press");
    window->pollEvents(in);
    check(in.leftDragDX == 0 && in.mouseDX == 0 && in.leftButton, "held button with no motion");
    pushMotion(5, 0);
    window->pollEvents(in);
    check(in.leftDragDX == 5 && in.leftButton, "drag continues in the next poll");
    pushMotion(1, 0);
    pushButton(SDL_BUTTON_LEFT, false);
    pushMotion(9, 0);
    window->pollEvents(in);
    check(in.leftDragDX == 1 && in.mouseDX == 10 && !in.leftButton, "drag ends at the release");

    // Each button gets its own deltas, and a motion while two are held counts for both.
    pushButton(SDL_BUTTON_RIGHT, true);
    pushMotion(1, 1);
    pushButton(SDL_BUTTON_MIDDLE, true);
    pushMotion(2, 2);
    pushButton(SDL_BUTTON_LEFT, true);
    pushMotion(4, 4);
    pushButton(SDL_BUTTON_RIGHT, false);
    pushMotion(8, 8);
    window->pollEvents(in);
    check(in.rightDragDX == 7 && in.rightDragDY == 7, "right drag");
    check(in.middleDragDX == 14 && in.middleDragDY == 14, "middle drag");
    check(in.leftDragDX == 12 && in.leftDragDY == 12, "left drag");
    check(in.leftButton && !in.rightButton && in.middleButton, "buttons held at the end of the poll");

    pushButton(SDL_BUTTON_LEFT, false);
    pushButton(SDL_BUTTON_MIDDLE, false);
    window->pollEvents(in);
    check(!in.leftButton && !in.middleButton, "all released");
}

}  // namespace

int main() {
    const mb::Level level = mb::parseLevelJson(kLevel);
    check(std::fabs(level.gravity[1] + 9.81f) < 1e-4f, "gravity");
    check(level.actors.size() == 3, "actor count");
    check(level.actors[0].object && level.actors[0].object->mesh == "boardsea.x", "board mesh");
    check(level.actors[0].shapes.at(0).halfExtents[1] == 22.0f, "box half extents");
    check(level.actors[0].object->extra == std::vector<std::uint8_t>{0x03, 0xff}, "extra bytes");
    check(!level.actors[0].body && level.actors[1].body, "body presence");
    check(level.actors[1].shapes.at(0).radius == 1.5f, "sphere radius");
    check(level.actors[1].shapes.at(1).type == mb::ShapeType::Capsule && std::fabs(level.actors[1].shapes.at(1).radius - 0.9f) < 1e-5f &&
              std::fabs(level.actors[1].shapes.at(1).height - 9.8f) < 1e-5f,
          "capsule radius and height");
    check(!level.actors[2].object, "actor without object");
    check(level.countObjects(mb::ObjectType::Brick) == 1, "brick nested in cloth is counted");
    check(level.countObjects(mb::ObjectType::Cloth) == 1, "cloth counted");
    check(level.paths.size() == 1 && level.paths[0].points[0].up[1] == 1.0f, "path point");
    check(mb::objectTypeFromName("teleportdest") == mb::ObjectType::TeleportDest, "type lookup");

    bool threw = false;
    try {
        mb::parseLevelJson(R"({"gravity":[0,0,0],"actors":[{"pose":{"rotation":[1,0,0,0,1,0,0,0,1],"position":[0,0,0]},"shapes":[{"type":"blob"}]}]})");
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "unknown shape type is rejected");

    testLevelVersion();
    testPhysicsFields();
    testDragAttribution();

    if (failures == 0) {
        std::puts("all tests passed");
    }
    return failures == 0 ? 0 : 1;
}
