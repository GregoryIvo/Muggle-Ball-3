#include "mb/game/PhysicsProfile.h"

#include "StringUtil.h"

#include <array>
#include <vector>

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
//   - maxBodies, maxBodyPairs, maxContactConstraints: sized from the measured peak body count by a rule of
//     thumb, not themselves separately measured; tempAllocatorBytes is sized the same way from its own
//     measured peak, 2,187,184 bytes (measured with these settings already in force, in particular
//     threads = 0: Jolt holds less of the step allocator live at once with one worker than with several,
//     see docs/PHYSICS.md, "The Vita profile"). Re-measure both if threads or bodyMutexes ever change.
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
    .tempAllocatorBytes = 5u << 20,
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

// A function-local static, built the first time anything asks for it rather than at program static-init time: a
// plain global here (a `PhysicsProfile[]`, said non-constexpr by its vector members) would have its construction
// order relative to every other translation unit's globals left unspecified, so another TU's own global whose
// initializer reaches into this table first could see it unbuilt (the three smokes' gProfile globals hit exactly
// this as a bad_alloc, before their own initializers were changed to call physicsProfile() directly, which this
// fix makes safe at any time). A function-local static has no such race: it is guaranteed built, thread-safely,
// the first time control reaches here, whoever calls first and whenever that is.
const std::vector<PhysicsProfile>& profileTable() {
    static const std::vector<PhysicsProfile> profiles = {
        {"desktop", PhysicsSettings{}, {}},
        {"vita", kVitaSettings, {kVitaOverrides.begin(), kVitaOverrides.end()}},
    };
    return profiles;
}

}  // namespace

std::optional<PhysicsProfile> physicsProfile(std::string_view name) {
    const std::string wanted = lowerAscii(name);
    for (const PhysicsProfile& profile : profileTable()) {
        if (lowerAscii(profile.name) == wanted) {
            return profile;
        }
    }
    return std::nullopt;
}

std::span<const std::string_view> physicsProfileNames() {
    // Derived from the same table, rather than a second, separately maintained list, so the two cannot disagree.
    static const std::vector<std::string_view> names = [] {
        std::vector<std::string_view> result;
        for (const PhysicsProfile& profile : profileTable()) {
            result.push_back(profile.name);
        }
        return result;
    }();
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
