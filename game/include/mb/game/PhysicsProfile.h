#pragma once

#include "mb/engine/Physics.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace mb {

// A named set of PhysicsSettings, with overrides for levels that need their own.
//
// "desktop" is what the game has always played with: PhysicsSettings' defaults (docs/PHYSICS.md, "The solver").
// "vita" is the PlayStation Vita's starting point from docs/VITA_PLAN.md section 5.5: the same fixed step and
// solver iterations (anything less waits for the device benchmark, M3b, and the 240 s stability check), the
// physics on the calling thread with explicit body mutexes, maxStepsPerCall 4 (two 30 fps frames' worth), and a
// world sized for the levels there are rather than for 32768 bodies. Its overrides keep the four windmill levels
// at 100 velocity iterations whatever the rest are tuned to, since those are the only settings measured to hold
// their towers up (docs/PHYSICS.md).
//
// Levels are named by their id, the stem of their file in Data/Levels (for example "village-dinner"), compared
// without regard to case, as the level catalogue does.
struct PhysicsProfile {
    struct LevelOverride {
        std::string_view level;
        PhysicsSettings settings;
    };

    std::string_view name;
    PhysicsSettings settings;
    std::vector<LevelOverride> overrides;
};

// The profile of that name, in any case; nothing for a name there is no profile of.
std::optional<PhysicsProfile> physicsProfile(std::string_view name);

// The names there are, in a fixed order, for --help and for error messages: "desktop", "vita".
std::span<const std::string_view> physicsProfileNames();

// What a level is simulated with under a profile: the level's override if it has one, else the profile's settings.
PhysicsSettings physicsSettingsFor(const PhysicsProfile& profile, std::string_view levelId);

// The profile a build plays with unless a command line says otherwise: "vita" when built for the Vita
// (MB3_PLATFORM_VITA), "desktop" everywhere else.
std::string_view defaultPhysicsProfileName();

}  // namespace mb
