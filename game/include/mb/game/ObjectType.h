#pragma once

#include <cstdint>
#include <string_view>

namespace mb {

// Object type ids exactly as stored in .mb3 level files (game32.img factory at 0x460380).
enum class ObjectType : std::uint32_t {
    Unknown = 0,
    Board = 1,
    Bat = 2,
    Ball = 3,
    Brick = 4,
    Prize = 5,
    ShadowCaster = 6,
    Background = 7,
    Light = 8,
    Bullet = 9,
    Wall = 10,
    Cannon = 11,
    Firework = 12,
    Spring = 13,
    Volcano = 14,
    TeleportSrc = 15,
    TeleportDest = 16,
    GeneratorGood = 17,
    GeneratorBad = 18,
    Tree = 19,
    Cloth = 20,
    FireRocket = 21,
    Vibro = 22,
    Flicker = 23,
    Metall = 24,
    Dynamite = 25,
    Train = 26,
};

// Name used by the converter's JSON ("brick", "teleportsrc", ...).
std::string_view objectTypeName(ObjectType type);
ObjectType objectTypeFromName(std::string_view name);

}  // namespace mb
