#include "mb/game/ObjectType.h"

#include <array>
#include <utility>

namespace mb {

namespace {

constexpr std::array<std::pair<ObjectType, std::string_view>, 26> kNames{{
    {ObjectType::Board, "board"},
    {ObjectType::Bat, "bat"},
    {ObjectType::Ball, "ball"},
    {ObjectType::Brick, "brick"},
    {ObjectType::Prize, "prize"},
    {ObjectType::ShadowCaster, "shadowcaster"},
    {ObjectType::Background, "background"},
    {ObjectType::Light, "light"},
    {ObjectType::Bullet, "bullet"},
    {ObjectType::Wall, "wall"},
    {ObjectType::Cannon, "cannon"},
    {ObjectType::Firework, "firework"},
    {ObjectType::Spring, "spring"},
    {ObjectType::Volcano, "volcano"},
    {ObjectType::TeleportSrc, "teleportsrc"},
    {ObjectType::TeleportDest, "teleportdest"},
    {ObjectType::GeneratorGood, "generatorgood"},
    {ObjectType::GeneratorBad, "generatorbad"},
    {ObjectType::Tree, "tree"},
    {ObjectType::Cloth, "cloth"},
    {ObjectType::FireRocket, "firerocket"},
    {ObjectType::Vibro, "vibro"},
    {ObjectType::Flicker, "flicker"},
    {ObjectType::Metall, "metall"},
    {ObjectType::Dynamite, "dynamite"},
    {ObjectType::Train, "train"},
}};

}  // namespace

std::string_view objectTypeName(ObjectType type) {
    for (const auto& [t, name] : kNames) {
        if (t == type) {
            return name;
        }
    }
    return "unknown";
}

ObjectType objectTypeFromName(std::string_view name) {
    for (const auto& [t, n] : kNames) {
        if (n == name) {
            return t;
        }
    }
    return ObjectType::Unknown;
}

}  // namespace mb
