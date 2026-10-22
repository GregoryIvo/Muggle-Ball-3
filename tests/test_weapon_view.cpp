// What the weapons look like (docs/GAMEPLAY.md section 18.11 and 18.12) without a GPU or game data: the clock that times each barrel's take
// of bat.x's "Common" animation, the particle effects the events and the flying projectiles call for (on made-up particle files with the real
// numbers), and that the HUD's shot count falls with the shot. The same on the real data, with the models and the GPU: mb_weapon_view_smoke.

#include "mb/game/BatBarrels.h"
#include "mb/game/Gameplay.h"
#include "mb/game/ParticleDef.h"
#include "mb/game/ParticleSystem.h"
#include "mb/game/PlayScene.h"
#include "mb/game/PrizeHud.h"
#include "mb/game/WeaponEffects.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using namespace mb;

// The parts of a PlayScene draw into its own particle system through references: a copy or a move would leave them drawing into the original's.
static_assert(!std::is_copy_constructible_v<PlayScene> && !std::is_move_constructible_v<PlayScene> && !std::is_copy_assignable_v<PlayScene> &&
                  !std::is_move_assignable_v<PlayScene>,
              "a PlayScene is not copied or moved");

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) <= tolerance;
}

bool near(Vec3 a, Vec3 b, float tolerance = 1e-3f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

constexpr double kStep = 1.0 / 60;

void run(BarrelClock& clock, bool wanted, double seconds) {
    for (double left = seconds; left > 1e-9; left -= kStep) {
        clock.update(wanted, std::min(kStep, left));
    }
}

// ---- The barrels' clock ------------------------------------------------------------------------------------------------------

void testClips() {
    for (int i = 0; i < kWeaponTypeCount; ++i) {
        const BarrelClip clip = barrelClip(static_cast<WeaponType>(i));
        check(clip.grown > 0.8 && clip.grown <= clip.recoilFrom && clip.recoilFrom < clip.recoilTo && clip.recoilTo <= clip.shrinkFrom &&
                  clip.shrinkFrom < clip.shrinkTo,
              "clips: the times of weapon " + std::to_string(i) + " run in order: growing, recoil, shrinking");
        check(near(static_cast<float>(clip.grown), 4000.0f / 4800, 1e-4f) && near(static_cast<float>(clip.recoilTo), 11200.0f / 4800, 1e-4f) &&
                  near(static_cast<float>(clip.shrinkTo), 16800.0f / 4800, 1e-4f),
              "clips: and are the keys' (4000, 11200 and 16800 ticks of 4800 a second)");
    }
    check(near(static_cast<float>(barrelClip(WeaponType::Gun).recoilFrom), 7040.0f / 4800, 1e-4f) &&
              near(static_cast<float>(barrelClip(WeaponType::Bazooka).recoilFrom), 5440.0f / 4800, 1e-4f) &&
              near(static_cast<float>(barrelClip(WeaponType::Gun).shrinkFrom), 12800.0f / 4800, 1e-4f) &&
              near(static_cast<float>(barrelClip(WeaponType::Cannon).shrinkFrom), 12000.0f / 4800, 1e-4f),
          "clips: each weapon's recoil starts where its barrel starts to move (the Gun's 7040, the Bazooka's 5440), the Gun's shrinking where its bodies start to tilt, at 12800");
}

void testClockLife() {
    BarrelClock clock(WeaponType::Cannon);
    const BarrelClip clip = barrelClip(WeaponType::Cannon);
    check(clock.phase() == BarrelPhase::Hidden, "clock: a barrel that was not asked for is hidden");
    run(clock, false, 1.0);
    check(clock.phase() == BarrelPhase::Hidden, "clock: and stays so");
    clock.fired();
    check(clock.phase() == BarrelPhase::Hidden, "clock: a shot of a weapon that is not shown does not show it");

    clock.update(true, kStep);
    check(clock.phase() == BarrelPhase::Growing && clock.time() == 0, "clock: when it is asked for it starts to grow from the start of the take");
    clock.update(true, kStep);
    check(near(static_cast<float>(clock.time()), static_cast<float>(kStep), 1e-4f), "clock: and goes on at the take's rate");
    clock.fired();
    check(clock.phase() == BarrelPhase::Growing, "clock: a shot while it grows is no recoil");
    run(clock, true, clip.grown * 0.5);
    check(clock.phase() == BarrelPhase::Growing && clock.time() > clip.grown * 0.4 && clock.time() < clip.grown * 0.6, "clock: it grows at the rate of the take");
    run(clock, true, clip.grown * 0.6);
    check(clock.phase() == BarrelPhase::Ready && clock.time() == clip.grown, "clock: and waits in the pose the take reaches at its end");
    run(clock, true, 5);
    check(clock.phase() == BarrelPhase::Ready && clock.time() == clip.grown, "clock: as long as the weapon is the one that fires");

    clock.fired();
    check(clock.phase() == BarrelPhase::Recoiling && clock.time() == clip.recoilFrom, "clock: a shot makes it recoil from where the recoil starts");
    run(clock, true, 0.3);
    check(clock.phase() == BarrelPhase::Recoiling && near(static_cast<float>(clock.time() - clip.recoilFrom), 0.3f, 0.02f), "clock: at the take's rate");
    clock.fired();
    check(clock.phase() == BarrelPhase::Recoiling && clock.time() == clip.recoilFrom, "clock: another shot starts it over");
    run(clock, true, clip.recoilTo - clip.recoilFrom + 0.05);
    check(clock.phase() == BarrelPhase::Ready && clock.time() == clip.grown, "clock: a recoil ends where it waits");

    // The weapon ends: it draws back and shrinks, and is hidden when the take is over.
    clock.update(false, kStep);
    check(clock.phase() == BarrelPhase::Shrinking && clock.time() == clip.shrinkFrom, "clock: when the weapon ends it starts to shrink");
    clock.fired();
    check(clock.phase() == BarrelPhase::Shrinking, "clock: a barrel that is shrinking does not recoil");
    run(clock, false, clip.shrinkTo - clip.shrinkFrom - 0.1);
    check(clock.phase() == BarrelPhase::Shrinking, "clock: it takes the take's time");
    run(clock, false, 0.2);
    check(clock.phase() == BarrelPhase::Hidden, "clock: and is hidden when it is over");
}

void testClockInterruptions() {
    const BarrelClip clip = barrelClip(WeaponType::Gun);
    // The last shot of a weapon is seen: its recoil is played out before the barrel draws back.
    BarrelClock last(WeaponType::Gun);
    run(last, true, clip.grown + 0.1);
    last.fired();
    run(last, false, 0.2);  // the weapon ended with that shot
    check(last.phase() == BarrelPhase::Recoiling && last.time() > clip.recoilFrom, "interruptions: the recoil of the last shot is played to its end");
    run(last, false, clip.recoilTo - clip.recoilFrom - 0.2 + 0.01);
    check(last.phase() == BarrelPhase::Ready, "interruptions: and then the barrel stands one step");
    last.update(false, kStep);
    check(last.phase() == BarrelPhase::Shrinking, "interruptions: and shrinks");

    // Another weapon was caught while this one grew: it grows backwards from where it is, so that nothing jumps.
    BarrelClock grow(WeaponType::Gun);
    run(grow, true, 0.3);
    const double reached = grow.time();
    grow.update(false, kStep);
    check(grow.phase() == BarrelPhase::Receding && grow.time() == reached, "interruptions: a barrel that is growing and not wanted starts to recede, from the pose it has");
    run(grow, false, 0.1);
    check(grow.phase() == BarrelPhase::Receding && near(static_cast<float>(reached - grow.time()), 0.1f, 0.02f), "interruptions: at the take's rate");
    grow.fired();
    check(grow.phase() == BarrelPhase::Receding, "interruptions: a barrel that recedes does not recoil");
    // and if the weapon is back, it grows on from there
    const double back = grow.time();
    grow.update(true, kStep);
    check(grow.phase() == BarrelPhase::Growing && grow.time() == back, "interruptions: a barrel that recedes and is wanted again grows on from where it is");
    run(grow, true, 0.1);
    check(grow.time() > back + 0.08, "interruptions: at the take's rate");
    // or it is gone when it is all the way back
    grow.update(false, kStep);
    run(grow, false, 0.6);
    check(grow.phase() == BarrelPhase::Hidden && grow.time() == 0, "interruptions: and when it is all the way back it is hidden");

    // The weapon came back while a barrel that had grown shrank: it grows from the beginning (a jump).
    BarrelClock shrink(WeaponType::Gun);
    run(shrink, true, clip.grown + 0.1);
    shrink.update(false, kStep);
    run(shrink, false, 0.2);
    shrink.update(true, kStep);
    check(shrink.phase() == BarrelPhase::Growing && near(static_cast<float>(shrink.time()), 0, 1e-6f), "interruptions: a barrel that is shrinking and wanted again grows from the start");
}

// The frame's order (BarrelClocks): the shots of its events first, then the update with the weapon the session has now. The last shot
// of a weapon is in an event of the very frame in which the session has dropped the weapon, so the other order would see the weapon
// gone, send the barrel to its shrinking, and then have no barrel to recoil.
void testClocksOrder() {
    const auto shot = [](PrizeKind prize) {
        GameEvent e;
        e.type = EventType::ShotFired;
        e.prize = prize;
        return std::vector<GameEvent>{e};
    };
    const auto ready = [](BarrelClocks& clocks, WeaponType type) {
        for (int i = 0; i < 90; ++i) {
            clocks.update(type, {}, kStep);
        }
    };

    BarrelClocks last;
    ready(last, WeaponType::Cannon);
    check(last[WeaponType::Cannon].phase() == BarrelPhase::Ready, "order: the cannon's barrel stands");
    last.update(std::nullopt, shot(PrizeKind::Cannon), kStep);
    check(last[WeaponType::Cannon].phase() == BarrelPhase::Recoiling, "order: its last shot, in the frame the weapon is gone, makes it recoil");
    for (int i = 0; i < 20; ++i) {
        last.update(std::nullopt, {}, kStep);
    }
    check(last[WeaponType::Cannon].phase() == BarrelPhase::Recoiling && last[WeaponType::Cannon].time() > barrelClip(WeaponType::Cannon).recoilFrom + 0.3,
          "order: and the recoil is played on while no weapon is wanted");
    for (int i = 0; i < 60; ++i) {
        last.update(std::nullopt, {}, kStep);
    }
    check(last[WeaponType::Cannon].phase() == BarrelPhase::Shrinking, "order: then the barrel draws back");

    // The Machine Gun's pair of bullets is one event, and a shot of one weapon moves no other barrel.
    BarrelClocks gun;
    ready(gun, WeaponType::Gun);
    gun.update(WeaponType::Gun, shot(PrizeKind::Gun), kStep);
    check(gun[WeaponType::Gun].phase() == BarrelPhase::Recoiling && gun[WeaponType::Cannon].phase() == BarrelPhase::Hidden, "order: a shot of the gun makes the gun recoil and nothing else");

    // The weapon caught in the same frame as the last shot of the old one: the old recoils while the new one grows.
    BarrelClocks swap;
    ready(swap, WeaponType::Cannon);
    swap.update(WeaponType::Bazooka, shot(PrizeKind::Cannon), kStep);
    check(swap[WeaponType::Cannon].phase() == BarrelPhase::Recoiling && swap[WeaponType::Bazooka].phase() == BarrelPhase::Growing,
          "order: the old weapon's last shot recoils as the new one's barrel grows");

    // A shot in the frame a weapon is first the one to fire (the click came in the frame it was caught) has no barrel to kick yet.
    BarrelClocks fresh;
    fresh.update(WeaponType::Laser, shot(PrizeKind::Laser), kStep);
    check(fresh[WeaponType::Laser].phase() == BarrelPhase::Growing, "order: a shot before the barrel has grown does not kick it");
}

// ---- What the events call for -------------------------------------------------------------------------------------------------

ParticleLibrary::Source file(const std::string& stem, const std::string& attrs) {
    return {stem, R"({"version": 1, "root": {"tag": "particles", "children": [{"tag": "particle", "attrs": {)" + attrs + "}}]}}"};
}

// The real definitions' numbers (Data/Particles) of the files the weapons ask for.
const ParticleLibrary& library() {
    static const std::vector<ParticleLibrary::Source> sources{
        file("Laser", R"("name": "SimpleFirework", "texture": "Particles/ParticleLaser1.png", "type": "Laser", "fade_time_msec": 1500.0, "size": 1.0, "colour1": 4294967040, "colour2": 4294901760, "modulate_colour": false, "speed": 150.0)"),
        file("Lightning", R"("name": "SimpleFirework", "texture": "Particles/ParticleLaser1.png", "type": "Lightning", "fade_time_msec": 500, "size": 1.0, "colour": 15658751, "modulate_colour": false, "num": 10, "shift_coeff": 0.5, "shake_coeff": 0.75)"),
        file("Trail", R"("name": "TrailBlue", "texture": "Particles/ParticleTrail(orange).png", "type": "Trail", "num": 30, "fade_time_msec": 500.0, "size": 2.5, "radius": 0.0, "colour1": 14869247, "colour2": 7368959, "modulate_colour": false)"),
        file("TrailMeteor", R"("name": "TrailBlue", "texture": "Particles/ParticleTrail(orange).png", "type": "Trail", "num": 30, "fade_time_msec": 500.0, "size": 5.0, "radius": 0.0, "colour1": 15632520, "colour2": 16711680, "modulate_colour": false)"),
        file("exploseyadro", R"("name": "Flash 2", "texture": "Oreols/11.png", "type": "Flash", "fade_time_msec": 500.0, "size": 10.0, "speed": 150.0)"),
        file("explosemeteor", R"("name": "Flash 2", "texture": "Particles/boom.png", "type": "Flash", "fade_time_msec": 200.0, "size": 10.0, "speed": 50.0, "direction": [0.0, 1.0, 0.0])"),
    };
    static const ParticleLibrary lib = ParticleLibrary::parse(sources);
    return lib;
}

const ParticleView kView{};

std::vector<ParticleQuad> quadsOf(const ParticleSystem& system) {
    std::vector<ParticleQuad> quads;
    system.appendQuads(kView, quads);
    return quads;
}

int quadsOfFile(const ParticleSystem& system, const std::string& name) {
    const std::vector<ParticleQuad> quads = quadsOf(system);
    return static_cast<int>(std::ranges::count_if(quads, [&](const ParticleQuad& q) { return q.def->file == name; }));
}

GameEvent event(EventType type, PrizeKind prize, Vec3 at, Vec3 end = {}) {
    GameEvent e;
    e.type = type;
    e.prize = prize;
    e.position = at;
    e.end = end;
    return e;
}

struct Show {
    ParticleSystem particles{library()};
    WeaponRules rules;
    WeaponEffects effects{particles, rules};

    void step(std::vector<GameEvent> events = {}, std::vector<Projectile> projectiles = {}, double seconds = kStep, double lead = 0) {
        effects.update(events, projectiles, lead);
        particles.update(seconds);
    }
    void run(double seconds, const std::vector<Projectile>& projectiles = {}) {
        for (double left = seconds; left > 1e-9; left -= kStep) {
            step({}, projectiles, std::min(kStep, left));
        }
    }
};

Projectile projectile(int id, ProjectileKind kind, Vec3 at, Vec3 velocity) {
    Projectile p;
    p.id = id;
    p.kind = kind;
    p.position = at;
    p.velocity = velocity;
    return p;
}

void testBeamsAndBolts() {
    Show laser;
    laser.step({event(EventType::LaserBeam, PrizeKind::Laser, {0, 1, 2.9f}, {0, 1, 30})});
    check(quadsOfFile(laser.particles, "Laser") == 3, "laser: a beam of the Laser particle (a strip and two ends) as the shot is fired");
    laser.run(0.24);
    check(near(quadsOf(laser.particles)[0].colourBegin.a, 1, 1e-3f), "laser: at full strength for a quarter of a second");
    laser.run(0.15);
    const float dim = quadsOf(laser.particles).empty() ? 0.0f : quadsOf(laser.particles)[0].colourBegin.a;
    check(dim > 0 && dim < 0.6f, "laser: and fading after that (" + std::to_string(dim) + " at 0.4 s)");
    laser.run(0.16);
    check(quadsOfFile(laser.particles, "Laser") == 0, "laser: gone by 0.55 s, not at the particle file's 1.5 s fade");
    check(quadsOf(laser.particles).empty(), "laser: and nothing else is left");

    // The front runs out to the tip in WeaponRules::laserGrow, 0.1 s: the file's own 150 units a second would take 0.2 s for 30 units.
    Show grow;
    grow.step({event(EventType::LaserBeam, PrizeKind::Laser, {0, 1, 2.9f}, {0, 1, 32.9f})});
    const auto out = [&] { return length(quadsOf(grow.particles)[0].halfRight) * 2; };
    check(near(out(), 5, 0.05f), "laser: a sixth of the way out after one frame at 60 a second (" + std::to_string(out()) + ")");
    for (int i = 0; i < 2; ++i) {
        grow.step();
    }
    check(near(out(), 15, 0.05f), "laser: half of the way at 0.05 s (" + std::to_string(out()) + ")");
    for (int i = 0; i < 3; ++i) {
        grow.step();
    }
    check(near(out(), 30, 0.05f), "laser: all of it at 0.1 s (" + std::to_string(out()) + ")");
    check(quadsOf(grow.particles)[0].colourEnd.g < 0.05f, "laser: and the tip is red");
    Show young;
    young.step({event(EventType::LaserBeam, PrizeKind::Laser, {0, 1, 2.9f}, {0, 1, 32.9f})});
    check(quadsOf(young.particles)[0].colourEnd.g < 0.05f && quadsOf(young.particles)[0].colourBegin.g > 0.95f,
          "laser: yellow at the barrel and red at the front from the first frame, as in the footage");

    // Where the beam starts and ends is the event's.
    Show where;
    where.step({event(EventType::LaserBeam, PrizeKind::Laser, {2, 1, 3}, {2, 1, 23})});
    where.run(0.2);
    const std::vector<ParticleQuad> quads = where.particles.library().find("Laser") ? quadsOf(where.particles) : std::vector<ParticleQuad>{};
    check(quads.size() == 3 && near(quads[0].centre, {2, 1, 13}, 0.05f) && near(length(quads[0].halfRight) * 2, 20, 0.05f), "laser: from the barrel to the tip");

    Show bolt;
    bolt.step({event(EventType::BoltStruck, PrizeKind::Thunderbolt, {3, 1, 4}, {3, 25, 4})});
    check(quadsOfFile(bolt.particles, "Lightning") > 10, "bolt: a lightning bolt of the Lightning particle (the pieces of it) where it strikes");
    check(std::ranges::all_of(quadsOf(bolt.particles), [](const ParticleQuad& q) { return q.centre.y > 0.5f && q.centre.y < 26; }),
          "bolt: between the target and the top edge of the screen above it");
    bolt.run(0.1);
    check(quadsOfFile(bolt.particles, "Lightning") > 0, "bolt: it is there at 0.1 s");
    bolt.run(0.15);
    check(quadsOfFile(bolt.particles, "Lightning") > 10, "bolt: and at 0.25 s, still whole (the footage's bolts are on show about 0.3 s, GAMEPLAY.md 18.12)");
    bolt.run(0.15);
    check(quadsOf(bolt.particles).empty(), "bolt: and gone by 0.4 s (0.25 s full and a few frames of fading)");
    Show above;
    above.step({event(EventType::BoltStruck, PrizeKind::Thunderbolt, {3, 30, 4}, {3, 25, 4})});
    check(quadsOf(above.particles).empty(), "bolt: a target above the top edge of the screen has no lightning to see");
}

void testBurstsAndRings() {
    const auto burst = [](PrizeKind prize) {
        Show show;
        show.step({event(EventType::ProjectileExploded, prize, {1, 2, 3})});
        return quadsOf(show.particles);
    };
    const std::vector<ParticleQuad> kernel = burst(PrizeKind::Cannon);
    check(kernel.size() == 1 && kernel[0].def->file == "exploseyadro" && near(kernel[0].centre, {1, 2, 3}), "burst: a cannonball bursts in exploseyadro's flash where it hit");
    const std::vector<ParticleQuad> rocket = burst(PrizeKind::Bazooka);
    check(rocket.size() == 1 && rocket[0].def->file == "exploseyadro", "burst: a rocket in the same");
    // The flash is 10 across at full size; the star of the footage is 4 across for the cannonball and 4.5 for the rocket.
    Show kernelFlash, rocketFlash;
    kernelFlash.step({event(EventType::ProjectileExploded, PrizeKind::Cannon, {})});
    rocketFlash.step({event(EventType::ProjectileExploded, PrizeKind::Bazooka, {})});
    kernelFlash.run(0.2);
    rocketFlash.run(0.2);
    check(near(length(quadsOf(kernelFlash.particles)[0].halfRight) * 2, 4, 0.05f) && near(length(quadsOf(rocketFlash.particles)[0].halfRight) * 2, 4.5f, 0.05f),
          "burst: the star is 4 units across for a cannonball and 4.5 for a rocket");
    check(burst(PrizeKind::Meteor).empty() && burst(PrizeKind::Gun).empty() && burst(PrizeKind::Laser).empty(),
          "burst: a meteor has its ring and a bullet no burst of its own");

    Show ring;
    ring.step({event(EventType::MeteorImpact, PrizeKind::Meteor, {4, 1.3f, -2})});
    const std::vector<ParticleQuad> quads = quadsOf(ring.particles);
    check(quads.size() == 1 && quads[0].def->file == "explosemeteor" && near(quads[0].centre.x, 4) && near(quads[0].centre.z, -2) && near(quads[0].centre.y, 0.1f, 0.02f),
          "ring: a meteor leaves the ring of explosemeteor on the water below where it hit, whatever it hit");
    check(std::fabs(quads[0].halfUp.y) < 1e-3f && std::fabs(quads[0].halfRight.y) < 1e-3f, "ring: lying flat");
    ring.run(0.3);
    check(quadsOfFile(ring.particles, "explosemeteor") == 1, "ring: still there at 0.3 s");
    ring.run(0.15);
    check(quadsOf(ring.particles).empty(), "ring: and gone at about 0.4 s");
}

void testTrails() {
    Show show;
    const Vec3 velocity{0, 0, 52.5f};
    show.step({}, {projectile(1, ProjectileKind::Rocket, {0, 1.4f, 5}, velocity)});
    check(show.particles.effectCount() == 1, "trails: a rocket in the air leaves a trail");
    for (int i = 1; i <= 30; ++i) {
        show.step({}, {projectile(1, ProjectileKind::Rocket, {0, 1.4f, 5 + 52.5f * static_cast<float>(i * kStep)}, velocity)});
    }
    check(show.particles.effectCount() == 1 && quadsOfFile(show.particles, "Trail") >= 10, "trails: one trail that goes on, with its particles (a short glow of 30)");
    std::vector<ParticleQuad> quads = quadsOf(show.particles);
    float nearest = 1e9f, farthest = -1e9f;
    for (const ParticleQuad& q : quads) {
        nearest = std::min(nearest, q.centre.z);
        farthest = std::max(farthest, q.centre.z);
    }
    check(farthest < 5 + 52.5f * 0.5f && farthest > 5 + 52.5f * 0.5f - 1.5f,
          "trails: out of its tail, which is a little behind its middle, along the way it went: " + std::to_string(nearest) + " to " + std::to_string(farthest));
    // The footage has a small glow behind the rocket and no streak: the file's half a second of life would be a line 26 units long at 52.5 units
    // a second, so the glow is out for a twentieth of that, about 3 units.
    check(farthest - nearest > 1.5f && farthest - nearest < 5.0f, "trails: a short glow, not a line (" + std::to_string(farthest - nearest) + " units long)");
    check(std::ranges::all_of(quads, [](const ParticleQuad& q) { return length(q.halfRight) * 2 < 1.0f; }), "trails: and small (35 % of the file's 2.5)");
    // Carried on with the lead.
    Show lead;
    lead.step({}, {projectile(1, ProjectileKind::Rocket, {0, 1.4f, 5}, velocity)}, kStep, 0.01);
    lead.step({}, {projectile(1, ProjectileKind::Rocket, {0, 1.4f, 5}, velocity)}, kStep, 0.01);
    float front = -1e9f;
    for (const ParticleQuad& q : quadsOf(lead.particles)) {
        front = std::max(front, q.centre.z);
    }
    check(front > 5 + 0.5f - 1.0f && front < 5 + 0.525f, "trails: it is where the picture has the rocket, ahead of the session's step by the lead");

    // It stops when the rocket is gone, and what it left fades out.
    show.step({}, {});
    show.run(0.7);
    check(show.particles.effectCount() == 0 && show.particles.particleCount() == 0, "trails: the trail stops with the rocket and fades out in half a second");

    Show meteor;
    meteor.step({}, {projectile(7, ProjectileKind::Meteor, {5, 20, 5}, {-10, -30, 10})});
    meteor.step({}, {projectile(7, ProjectileKind::Meteor, {5, 19.5f, 5.2f}, {-10, -30, 10})});
    const std::vector<ParticleQuad> meteorQuads = quadsOf(meteor.particles);
    check(!meteorQuads.empty() && meteorQuads[0].def->file == "TrailMeteor" && length(meteorQuads[0].halfRight) * 2 > 4.5f && length(meteorQuads[0].halfRight) * 2 <= 5.001f,
          "trails: a meteor leaves TrailMeteor's, 5 across");
    for (const ProjectileKind kind : {ProjectileKind::Bullet, ProjectileKind::Kernel, ProjectileKind::Bolt}) {
        Show none;
        none.step({}, {projectile(1, kind, {0, 5, 5}, {0, 0, 30})});
        check(none.particles.effectCount() == 0, "trails: a bullet, a cannonball and a bolt leave none");
    }
    // Two rockets, two trails; one gone, one trail stops.
    Show two;
    two.step({}, {projectile(1, ProjectileKind::Rocket, {-5, 1.4f, 5}, velocity), projectile(2, ProjectileKind::Rocket, {5, 1.4f, 5}, velocity)});
    check(two.particles.effectCount() == 2, "trails: a trail for each rocket");
    two.step({}, {projectile(2, ProjectileKind::Rocket, {5, 1.4f, 6}, velocity)});
    two.run(0.7, {projectile(2, ProjectileKind::Rocket, {5, 1.4f, 6}, velocity)});
    check(two.particles.effectCount() == 1, "trails: the one whose rocket is gone stops and fades");
}

// ---- The HUD's count and the last shot --------------------------------------------------------------------------------------

// A bat on a bare board with its walls (the side walls' inner faces at +-20.4 and the far wall's at 22.6, as the levels have them, and
// one behind the bat's line, which the levels leave open for the ball to be lost through), a ball in play and no bricks, no drops and no level end: a weapon can be given and clicked, and the ball stays in play.
struct Arena {
    MaterialLibrary materials = MaterialLibrary::parse(R"({"version": 1, "materials": [
      {"name": "BallMaterial", "density": 1, "physics": {"restitution": 0.999}, "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "BoardMaterial", "physics": {"restitution": 1.0}, "brick": {"type": 2, "energy": 3, "score": 4}},
      {"name": "One.jpg", "density": 5, "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 1, "score": 10, "exploseType": 3, "exploseDelay": 100}},
      {"name": "bat_bullet", "group": "Bats", "brick": {"type": 0, "energy": 3, "score": 4}, "sounds": {"period": "bullet_shoot", "dead": "bullet_dead"}}]})");
    Level level;
    std::unique_ptr<GameSession> session;

    Arena() {
        const auto box = [](Vec3 half, Vec3 at) {
            Shape shape;
            shape.type = ShapeType::Box;
            shape.halfExtents = half;
            shape.localPose.position = at;
            shape.group = 4;
            shape.shapeFlags = 8;
            return shape;
        };
        Actor board;
        Body fixed;
        fixed.mass = 1000;
        fixed.massSpaceInertia = {100, 100, 100};
        fixed.flags = 0xFF;
        board.body = fixed;
        board.shapes = {box({30, 0.5f, 30}, {0, -0.5f, 0}), box({1, 2, 30}, {-21.4f, 2, 0}), box({1, 2, 30}, {21.4f, 2, 0}), box({30, 2, 1}, {0, 2, 23.6f}), box({30, 2, 1}, {0, 2, -21.8f})};
        GameObject boardObject;
        boardObject.type = ObjectType::Board;
        boardObject.id = 1;
        boardObject.material = "BoardMaterial";
        board.object = boardObject;
        level.gravity = {0, -9.8f, 0};
        level.actors = {board};
        SessionOptions options;
        options.levelEnd = TargetRule::Never;
        options.ignoreUnreachable = false;
        options.physicsSettings.threads = 0;
        options.rules.prizes.drops = false;
        session = std::make_unique<GameSession>(level, materials, options);
        for (int i = 0; i < 300 && session->phase() != SessionPhase::Ready; ++i) {
            step(false);
        }
        step(true);
    }

    std::vector<GameEvent> step(bool launch) {
        GameInput input;
        input.batTargetX = 0;
        input.launch = launch;
        session->update(kStep, input);
        return session->takeEvents();
    }
};

void testHudCount() {
    Arena arena;
    GameSession& session = *arena.session;
    session.givePrize(PrizeKind::Gun);
    session.takeEvents();
    const auto shown = [&] {
        const std::vector<PowerUp> icons = powerUpsOf(session.activeEffects());
        return icons.size() == 1 ? icons[0].number : -1;
    };
    check(shown() == 12, "hud: the Machine Gun's icon counts its 12 shots");
    for (int i = 0; i < 20; ++i) {
        arena.step(false);
    }
    check(shown() == 12, "hud: and keeps the count while it is not fired");
    const std::vector<GameEvent> shot = arena.step(true);
    const auto fired = std::ranges::find(shot, EventType::ShotFired, &GameEvent::type);
    check(fired != shot.end() && fired->count == 11, "hud: the shot reports the shots left");
    check(shown() == 11, "hud: and the count the HUD shows has fallen in the step of the shot itself");
    for (int i = 0; i < 20; ++i) {
        arena.step(false);
    }
    arena.step(true);
    check(shown() == 10, "hud: and again with the next");
}

// The real session's frames through the clocks, as PlayScene drives them: every shot of the Cannon, the last one included (the session
// has ended the weapon in that very step), makes its barrel recoil, which is then played to its end.
void testLastShot() {
    Arena arena;
    GameSession& session = *arena.session;
    session.givePrize(PrizeKind::Cannon);
    session.takeEvents();
    BarrelClocks clocks;
    const auto frame = [&](bool click) {
        const std::vector<GameEvent> events = arena.step(click);
        const std::optional<WeaponState> armed = session.weapon();
        clocks.update(armed ? std::optional<WeaponType>(armed->type) : std::nullopt, events, kStep);
        return events;
    };
    for (int i = 0; i < 90; ++i) {
        frame(false);
    }
    check(clocks[WeaponType::Cannon].phase() == BarrelPhase::Ready, "last shot: the cannon's barrel has grown");
    int left = -1;
    for (int shots = 1; shots <= 6; ++shots) {
        const std::vector<GameEvent> events = frame(true);
        const auto fired = std::ranges::find(events, EventType::ShotFired, &GameEvent::type);
        check(fired != events.end() && fired->count == 6 - shots, "last shot: shot " + std::to_string(shots) + " leaves " + std::to_string(6 - shots));
        left = fired != events.end() ? fired->count : -1;
        check(clocks[WeaponType::Cannon].phase() == BarrelPhase::Recoiling, "last shot: shot " + std::to_string(shots) + " makes the barrel recoil");
        if (shots < 6) {
            for (int i = 0; i < 80; ++i) {
                frame(false);
            }
            check(clocks[WeaponType::Cannon].phase() == BarrelPhase::Ready, "last shot: and the barrel stands again before the next");
        }
    }
    check(left == 0 && !session.weapon(), "last shot: the sixth ended the weapon in the step of the shot");
    for (int i = 0; i < 20; ++i) {
        frame(false);
    }
    check(clocks[WeaponType::Cannon].phase() == BarrelPhase::Recoiling, "last shot: its recoil goes on after the weapon is gone");
    for (int i = 0; i < 40; ++i) {
        frame(false);
    }
    check(clocks[WeaponType::Cannon].phase() == BarrelPhase::Shrinking, "last shot: and then the barrel draws back");
    for (int i = 0; i < 90; ++i) {
        frame(false);
    }
    check(clocks[WeaponType::Cannon].phase() == BarrelPhase::Hidden, "last shot: and is gone");
}

}  // namespace

int main() {
    testClips();
    testClockLife();
    testClockInterruptions();
    testClocksOrder();
    testBeamsAndBolts();
    testBurstsAndRings();
    testTrails();
    testHudCount();
    testLastShot();
    if (failures > 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("all weapon view checks passed");
    return 0;
}
