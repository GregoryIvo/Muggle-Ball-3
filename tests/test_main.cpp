// Minimal self-contained tests (no game data needed).

#include "mb/game/LevelLoader.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>
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

    if (failures == 0) {
        std::puts("all tests passed");
    }
    return failures == 0 ? 0 : 1;
}
