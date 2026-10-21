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

    // The pointer position (what the 2D UI hit-tests) is the window's pixels, and stays between polls.
    SDL_WarpMouseInWindow(window->handle(), 10, 20);
    window->pollEvents(in);
    check(in.mouseX == 10 && in.mouseY == 20, "pointer position after a warp");
    window->pollEvents(in);
    check(in.mouseX == 10 && in.mouseY == 20, "pointer position is kept by a poll with no events");
}

void pushMotionAt(float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = x;
    e.motion.y = y;
    SDL_PushEvent(&e);
}

void pushButtonAt(Uint8 button, bool down, float x, float y) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = button;
    e.button.down = down;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
}

void pushKey(SDL_Keycode key, bool repeat = false) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.down = true;
    e.key.repeat = repeat;
    SDL_PushEvent(&e);
}

void pushText(const char* text) {
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = text;
    SDL_PushEvent(&e);
}

void pushWindowEvent(SDL_EventType type) {
    SDL_Event e{};
    e.type = type;
    SDL_PushEvent(&e);
}

// What the menus need from the window: the pointer's events in order with their positions, Escape as a key, typed text.
void testMenuInput() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    std::unique_ptr<mb::Window> window;
    try {
        window = std::make_unique<mb::Window>("test", 64, 64);
    } catch (const std::exception& e) {
        std::printf("skipped the menu input test: %s\n", e.what());
        return;
    }
    mb::InputEvents in;

    // A click made inside one poll is a press and a release, each where it happened.
    pushMotionAt(10, 20);
    pushButtonAt(SDL_BUTTON_LEFT, true, 10, 20);
    pushMotionAt(11, 21);
    pushButtonAt(SDL_BUTTON_LEFT, false, 12, 22);
    pushButtonAt(SDL_BUTTON_RIGHT, true, 30, 30);  // not the left button: no sample
    window->pollEvents(in);
    const auto sample = [&](std::size_t i, float x, float y, bool down) {
        return i < in.pointer.size() && in.pointer[i].x == x && in.pointer[i].y == y && in.pointer[i].leftDown == down;
    };
    check(in.pointer.size() == 4 && sample(0, 10, 20, false) && sample(1, 10, 20, true) && sample(2, 11, 21, true) && sample(3, 12, 22, false),
          "pointer samples: moves and left-button changes, in order, with the button state just after each");
    window->pollEvents(in);
    check(in.pointer.empty(), "pointer samples: a poll with no events has none");

    // The pointer leaving the window hovers nothing; losing focus with the button down lets go of it.
    pushWindowEvent(SDL_EVENT_WINDOW_MOUSE_LEAVE);
    window->pollEvents(in);
    check(in.pointer.size() == 1 && sample(0, -1, -1, false), "pointer samples: leaving the window is a sample off the screen");
    pushMotionAt(5, 6);
    pushButtonAt(SDL_BUTTON_LEFT, true, 5, 6);
    window->pollEvents(in);
    check(in.leftButton, "pointer samples: the button is held");
    pushWindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST);
    window->pollEvents(in);
    check(in.pointer.size() == 1 && sample(0, 5, 6, false) && !in.leftButton, "pointer samples: losing focus releases the button at the last position");

    // Escape closes the window until the menus take it over; then it is an ordinary key.
    pushKey(SDLK_ESCAPE);
    check(!window->pollEvents(in) && in.keysPressed.empty(), "escape: quits by default");
    window->setEscapeQuits(false);
    pushKey(SDLK_A);
    pushKey(SDLK_ESCAPE);
    pushKey(SDLK_BACKSPACE, true);  // a repeat
    pushKey(SDLK_RETURN);
    check(window->pollEvents(in) && in.keysPressed == std::vector<std::uint32_t>({SDLK_A, SDLK_ESCAPE, SDLK_RETURN}),
          "escape: with setEscapeQuits(false) it arrives as a key, in order, and repeats are left out");
    window->setEscapeQuits(true);
    pushKey(SDLK_ESCAPE);
    check(!window->pollEvents(in), "escape: and quits again when asked to");

    // Typed text is collected, as UTF-8, from the text events SDL sends while text input is on.
    window->setTextInput(true);
    pushText("Ann");
    pushText("\xC3\xA9");
    window->pollEvents(in);
    check(in.text == "Ann\xC3\xA9", "text: typed characters, UTF-8, joined in order");
    window->pollEvents(in);
    check(in.text.empty(), "text: and cleared by the next poll");
    window->setTextInput(false);  // SDL then stops sending text events (a pushed one would still arrive)

    // The display this window is on has a size.
    int width = 0, height = 0;
    check(!window->displaySize(width, height) || (width > 0 && height > 0), "displaySize: a size, or none known");
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
    testMenuInput();

    if (failures == 0) {
        std::puts("all tests passed");
    }
    return failures == 0 ? 0 : 1;
}
