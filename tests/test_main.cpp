// Minimal self-contained tests (no game data and no display needed).

#include "mb/engine/Window.h"
#include "mb/game/LevelLoader.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
    const mb::Level current = mb::parseLevelJson(R"({"version":2,"gravity":[0,0,0],"actors":[]})");
    check(current.version == 2 && current.version == mb::kLevelJsonVersion, "version 2 is read");

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
    std::ofstream(newFile) << R"({"version":2,"gravity":[0,0,0],"actors":[]})";
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
        check(mb::loadLevelJson(newFile).version == 2, "a current file loads as version 2");
        check(capture.count == 0, "a current level file logs no warning");
    }
    std::filesystem::remove_all(dir);
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
    check(!level.actors[0].dynamic && level.actors[1].dynamic, "dynamic flag");
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
    testDragAttribution();

    if (failures == 0) {
        std::puts("all tests passed");
    }
    return failures == 0 ? 0 : 1;
}
