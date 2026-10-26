// The named physics profiles (mb/game/PhysicsProfile.h): lookup by name, the per-level override table, and the
// platform default. No game data and no GPU needed; see docs/PHYSICS.md "The Vita profile" for the numbers.

#include "mb/game/PhysicsProfile.h"

#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>

using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool sameSettings(const PhysicsSettings& a, const PhysicsSettings& b) {
    return a.fixedStep == b.fixedStep && a.maxStepsPerCall == b.maxStepsPerCall && a.collisionSteps == b.collisionSteps &&
           a.velocitySteps == b.velocitySteps && a.positionSteps == b.positionSteps && a.bounceThreshold == b.bounceThreshold &&
           a.maxBodies == b.maxBodies && a.maxBodyPairs == b.maxBodyPairs && a.maxContactConstraints == b.maxContactConstraints &&
           a.threads == b.threads && a.bodyMutexes == b.bodyMutexes && a.tempAllocatorBytes == b.tempAllocatorBytes;
}

void testNames() {
    const std::span<const std::string_view> names = physicsProfileNames();
    check(names.size() == 2, "there are two profiles");
    check(names.size() >= 2 && names[0] == "desktop" && names[1] == "vita", "the fixed order is desktop, vita");
}

void testLookupIsCaseInsensitive() {
    check(physicsProfile("desktop").has_value(), "\"desktop\" is a profile");
    check(physicsProfile("DESKTOP").has_value(), "\"DESKTOP\" finds it too");
    check(physicsProfile("Vita").has_value(), "\"Vita\" finds the vita profile");
    check(physicsProfile("VITA").has_value(), "\"VITA\" finds it too");
    check(physicsProfile("vita")->name == "vita", "the profile's own name is lower case, whatever the query's case was");
}

void testUnknownNameIsNullopt() {
    check(!physicsProfile("nonexistent").has_value(), "an unknown profile name is nothing");
    check(!physicsProfile("").has_value(), "an empty name is nothing either");
}

void testDesktopIsPlainDefaults() {
    const std::optional<PhysicsProfile> desktop = physicsProfile("desktop");
    check(desktop.has_value() && sameSettings(desktop->settings, PhysicsSettings{}),
          "the desktop profile's settings equal PhysicsSettings{}, with no overrides");
    check(desktop.has_value() && desktop->overrides.empty(), "and no per-level overrides");
}

// physicsSettingsFor()'s own mechanics (the override table, matched case-insensitively, with a fallback to the
// profile's own settings), checked against a profile made up for the test so the override is distinguishable
// from the base by a field that cannot coincide by accident.
void testSettingsForMechanics() {
    PhysicsProfile profile;
    profile.name = "test";
    profile.settings.maxBodies = 111;
    PhysicsSettings overridden = profile.settings;
    overridden.maxBodies = 222;
    profile.overrides = {{"Some-Level", overridden}};

    check(physicsSettingsFor(profile, "nothing-like-it").maxBodies == 111, "a level with no override gets the profile's settings");
    check(physicsSettingsFor(profile, "Some-Level").maxBodies == 222, "the exact-case override is found");
    check(physicsSettingsFor(profile, "some-level").maxBodies == 222, "lower case still finds it");
    check(physicsSettingsFor(profile, "SOME-LEVEL").maxBodies == 222, "upper case still finds it");
    check(physicsSettingsFor(profile, "SoMe-LeVeL").maxBodies == 222, "mixed case still finds it");
}

// The real "vita" profile's windmill override (docs/PHYSICS.md, "The solver"): the four levels it names, by the
// exact stems in assets/Data/Levels, matched in any case.
void testVitaWindmillOverride() {
    const std::optional<PhysicsProfile> vita = physicsProfile("vita");
    check(vita.has_value(), "the vita profile exists");
    if (!vita) {
        return;
    }
    check(vita->overrides.size() == 4, "the vita profile has exactly the four windmill overrides");
    for (const char* name : {"village-dinner", "village-na-melnice", "village-rinok", "Village-Dvorik"}) {
        const PhysicsSettings s = physicsSettingsFor(*vita, name);
        check(s.velocitySteps == 100, std::string("the windmill override for ") + name + " keeps 100 velocity iterations");
    }
    // Case does not matter for the lookup, whatever case the level file happens to be named in.
    check(physicsSettingsFor(*vita, "VILLAGE-DVORIK").velocitySteps == 100, "Village-Dvorik matches upper case too");
    check(physicsSettingsFor(*vita, "village-dvorik").velocitySteps == 100, "and lower case");
    // A level that is not one of the four just gets the profile's own (also 100, today, but not pinned there).
    check(sameSettings(physicsSettingsFor(*vita, "not-a-windmill-level"), vita->settings),
          "a level with no override gets the vita profile's own settings");
}

void testDefaultProfileNameOnHost() {
    // This test binary is never cross-compiled for the Vita (MB3_BUILD_TESTS is off there), so the host always
    // takes the #else branch of defaultPhysicsProfileName().
    check(defaultPhysicsProfileName() == "desktop", "the default profile on a host build is \"desktop\"");
}

}  // namespace

int main() {
    testNames();
    testLookupIsCaseInsensitive();
    testUnknownNameIsNullopt();
    testDesktopIsPlainDefaults();
    testSettingsForMechanics();
    testVitaWindmillOverride();
    testDefaultProfileNameOnHost();
    if (failures > 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::puts("physics profile tests passed");
    return 0;
}
