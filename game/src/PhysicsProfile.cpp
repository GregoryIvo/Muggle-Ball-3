#include "mb/game/PhysicsProfile.h"

#include "StringUtil.h"

#include <array>

namespace mb {

namespace {

// "desktop": PhysicsSettings' own defaults, unchanged. "vita": docs/VITA.md, "Decisions and findings" and
// docs/PHYSICS.md, "The Vita profile" explain every number below; both were sized from mb_physics_smoke --measure
// run against the shipped levels (83 of them, 2026-10-23).
//   - fixedStep, velocitySteps, positionSteps, collisionSteps: the same as the desktop (docs/PHYSICS.md, "The
//     solver"): a lighter solver is only a candidate for the non-windmill levels, and that retuning is M3b's, once
//     the device benchmark exists to check the 240 s stability test on real hardware.
//   - threads 0, bodyMutexes 1: physics runs on the caller's one thread, with one lock stripe (nothing else ever
//     touches the world, so striping it would only add overhead for no concurrency to protect against).
//   - maxStepsPerCall 4: two 30 fps frames' worth, so a missed vblank still lets the simulation catch up within a
//     couple of frames instead of spiralling; droppedSteps() reports whenever it does not.
//   - maxBodies, maxBodyPairs, maxContactConstraints, tempAllocatorBytes: sized from the measured peaks, see
//     docs/PHYSICS.md.
constexpr PhysicsSettings kVitaSettings{
    .fixedStep = 1.0f / 60.0f,
    .maxStepsPerCall = 4,
    .collisionSteps = 1,
    .velocitySteps = 100,
    .positionSteps = 1,
    .maxBodies = 1024,
    .maxBodyPairs = 8192,
    .maxContactConstraints = 4096,
    .threads = 0,
    .bodyMutexes = 1,
    .tempAllocatorBytes = 4u << 20,
};

// The four windmill levels (docs/PHYSICS.md, "The solver"): the only levels whose towers were measured to need
// 100 velocity iterations to stay up. Pinned here, separately from the base "vita" settings above, so that a
// later retuning of the base profile (fewer iterations for the other 79 levels, once the device benchmark of
// M3b exists) cannot silently drop these four below what was actually measured to hold.
constexpr PhysicsSettings kWindmillSettings = [] {
    PhysicsSettings s = kVitaSettings;
    s.velocitySteps = 100;
    return s;
}();

constexpr std::array<PhysicsProfile::LevelOverride, 4> kVitaOverrides{{
    {"village-dinner", kWindmillSettings},
    {"village-na-melnice", kWindmillSettings},
    {"village-rinok", kWindmillSettings},
    {"Village-Dvorik", kWindmillSettings},
}};

const PhysicsProfile kProfiles[] = {
    {"desktop", PhysicsSettings{}, {}},
    {"vita", kVitaSettings, {kVitaOverrides.begin(), kVitaOverrides.end()}},
};

}  // namespace

std::optional<PhysicsProfile> physicsProfile(std::string_view name) {
    const std::string wanted = lowerAscii(name);
    for (const PhysicsProfile& profile : kProfiles) {
        if (lowerAscii(profile.name) == wanted) {
            return profile;
        }
    }
    return std::nullopt;
}

std::span<const std::string_view> physicsProfileNames() {
    static constexpr std::array<std::string_view, std::size(kProfiles)> names = {"desktop", "vita"};
    return names;
}

PhysicsSettings physicsSettingsFor(const PhysicsProfile& profile, std::string_view levelId) {
    const std::string wanted = lowerAscii(levelId);
    for (const PhysicsProfile::LevelOverride& override_ : profile.overrides) {
        if (lowerAscii(override_.level) == wanted) {
            return override_.settings;
        }
    }
    return profile.settings;
}

std::string_view defaultPhysicsProfileName() {
#ifdef MB3_PLATFORM_VITA
    return "vita";
#else
    return "desktop";
#endif
}

}  // namespace mb
