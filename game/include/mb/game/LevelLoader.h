#pragma once

#include "mb/game/Level.h"

#include <filesystem>
#include <string_view>

namespace mb {

// Loads a level converted by tools/mb3level.py (one JSON file per .mb3).
Level loadLevelJson(const std::filesystem::path& path);
Level parseLevelJson(std::string_view json);

}  // namespace mb
