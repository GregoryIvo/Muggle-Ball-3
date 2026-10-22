// The particle definitions and the simulation (docs/GAMEPLAY.md section 19, docs/OBJECTS.md section 8) on made-up files that have the
// real ones' numbers: the parser and what it reports, then each type's behaviour, the system's handles, and the batches the quads
// become. Needs neither a GPU nor game data; the same code on the real files: mb_particle_smoke.

#include "mb/game/BrickEffects.h"
#include "mb/game/ParticleDef.h"
#include "mb/game/ParticleDraw.h"
#include "mb/game/ParticleSystem.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace mb;

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

// Counts the warnings logged while alive.
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

private:
    static void onLog(void* self, int, SDL_LogPriority priority, const char*) {
        if (priority == SDL_LOG_PRIORITY_WARN) {
            ++static_cast<WarningCapture*>(self)->count;
        }
    }

    SDL_LogOutputFunction previous_ = nullptr;
    void* previousData_ = nullptr;
};

// A file in the converter's JSON with the given <particle> attribute objects.
ParticleLibrary::Source file(const std::string& stem, const std::vector<std::string>& particles) {
    std::string children;
    for (const std::string& attrs : particles) {
        children += std::string(children.empty() ? "" : ",") + R"({"tag": "particle", "attrs": {)" + attrs + "}}";
    }
    return {stem, R"({"version": 1, "root": {"tag": "particles", "children": [)" + children + "]}}"};
}

// The real definitions' numbers (Data/Particles), one file each, but for the Fireworks' two variants.
const ParticleLibrary& library() {
    static const std::vector<ParticleLibrary::Source> sources{
        file("Flash", {R"("name": "Flash 2", "texture": "Oreols/02.png", "type": "Flash", "fade_time_msec": 200.0, "size": 30.0, "speed": 500.0)"}),
        file("explosemeteor", {R"("name": "Flash 2", "texture": "Particles/boom.png", "type": "Flash", "fade_time_msec": 200.0, "size": 10.0, "speed": 50.0, "direction": [0.0, 1.0, 0.0])"}),
        file("Fireworks", {R"("name": "SimpleFirework", "texture": "Particles/ParticleTrail(orange).png", "type": "Firework", "num": 30, "radius": 10, "fade_time_msec": 500.0, "speed": 30.0, "size": 3.5, "direction": [0.0, -1.0, 0.0], "colour1": 4294901760, "colour2": 4294967040)",
                           R"("name": "one", "texture": "Particles/ParticleTrail(orange).png", "type": "Firework", "num": 1, "radius": 10, "fade_time_msec": 500.0, "speed": 30.0, "size": 3.5, "direction": [0.0, -1.0, 0.0], "colour1": 4278190335, "colour2": 4278255615)",
                           R"("name": "blue", "texture": "Particles/ParticleTrail(orange).png", "type": "Firework", "num": 30, "radius": 10, "fade_time_msec": 500.0, "speed": 30.0, "size": 3.5, "direction": [0.0, -1.0, 0.0], "colour1": 4278190335, "colour2": 4278255615)"}),
        file("Trail", {R"("name": "TrailBlue", "texture": "Particles/ParticleTrail(orange).png", "type": "Trail", "num": 30, "fade_time_msec": 500.0, "size": 2.5, "radius": 0.0, "colour1": 14869247, "colour2": 7368959, "modulate_colour": false)",
                       R"("name": "TrailOrange", "texture": "Particles/ParticleTrail(orange).png", "type": "Trail", "num": 30, "fade_time_msec": 500.0, "size": 2.5, "radius": 1.0, "colour1": 14869247, "colour2": 16744512, "modulate_colour": false)"}),
        file("Smoke", {R"("name": "Smoke", "texture": "Particles/ParticleBubble.png", "type": "Smoke", "num": 50, "angle": 10.5, "speed": 2.0, "fade_time_msec": 500.0, "size": 1.5, "height": 3.0, "width": 0.1, "direction": [0.0, 1.0, 0.0])"}),
        file("SparkKoster", {R"("name": "SparkKoster", "texture": "Particles/Particlered.png", "type": "WaterBubble", "num": 10, "speed": 1.0, "radius": 2.0, "fade_time_msec": 50, "direction": [0.0, 1.0, 0.0], "size": 1.0)"}),
        file("LeafFall", {R"("name": "TreeLeafFall", "texture": "Leaf.png", "type": "LeafFall", "num": 15, "angle": 25.0, "radius": 5.0, "dimension": [3.0, 2.0, 3.0], "speed": 2.0, "size": 1.5, "height": 6.0)"}),
        file("Tornado", {R"("name": "Small Tornado", "texture": "Particles/ParticleRoket.png", "type": "Tornado", "num": 1500, "radius_small": 5.0, "radius_big": 50.0, "height": 160.0, "fade_time_msec": 1500.0, "speed_angular": 4.5, "speed": 25.0, "size": 20.0, "direction": [0.0, 1.0, 0.0])"}),
        file("Laser", {R"("name": "SimpleFirework", "texture": "Particles/ParticleLaser1.png", "type": "Laser", "fade_time_msec": 1500.0, "size": 1.0, "colour1": 4294967040, "colour2": 4294901760, "modulate_colour": false, "speed": 150.0)"}),
        file("rubber", {R"("name": "Rubber", "texture": "Particles/ParticleLaser1.png", "type": "Laser", "fade_time_msec": 1000.0, "size": 1.0, "colour1": 4278255360, "colour2": 4278190335, "modulate_colour": false, "speed": 0.0)"}),
        file("Lightning", {R"("name": "SimpleFirework", "texture": "Particles/ParticleLaser1.png", "type": "Lightning", "fade_time_msec": 500, "size": 1.0, "colour": 15658751, "modulate_colour": false, "num": 10, "shift_coeff": 0.5, "shake_coeff": 0.75)"}),
        file("Degenerate", {R"("name": "point", "texture": "Leaf.png", "type": "Firework", "num": 3, "radius": 0, "speed": 30.0, "size": 1.0, "colour1": 4294901760)",
                            R"("name": "still", "texture": "Leaf.png", "type": "Smoke", "num": 5, "speed": 0.0, "height": 0.0, "size": 1.0)",
                            R"("name": "bare", "texture": "Leaf.png", "type": "Flash", "size": 2.0)",
                            R"("name": "nothing", "texture": "Leaf.png", "type": "LeafFall")"}),
        file("Oreolblue", {R"("name": "Oreol", "texture": "Oreols/02.png", "type": "Oreol", "num": 1, "speed_angular": 3.5, "speed": 1.0, "radius_offset": 1.0, "radius": -2.0, "inverse_rotation": false)",
                           R"("name": "OreolShadow", "texture": "Oreols/07.png", "type": "Oreol", "num": 1, "speed_angular": -1.5, "speed": 2.0, "radius_offset": 1.0, "radius": 1.0, "shadow_plane": [0.0, 1.0, 0.0], "shadow_position": [0.0, 0.1, 0.0], "inverse_rotation": false)"}),
    };
    static const ParticleLibrary lib = ParticleLibrary::parse(sources);
    return lib;
}

const ParticleView kView{};  // right +x, up +y, forward +z

std::vector<ParticleQuad> quadsOf(const ParticleSystem& system) {
    std::vector<ParticleQuad> quads;
    system.appendQuads(kView, quads);
    return quads;
}

// Steps of `step` seconds, the last one shorter, to `seconds` in all.
void run(ParticleSystem& system, double seconds, double step = 1.0 / 60) {
    for (double left = seconds; left > 1e-9; left -= step) {
        system.update(std::min(step, left));
    }
}

// ---- The definitions ---------------------------------------------------------------------------------------------------------

void testParser() {
    const ParticleLibrary& lib = library();
    check(lib.problems().empty() && lib.unknownAttributes().empty(), "parser: the made-up files read without a word");

    const ParticleDef* flash = lib.find("Flash");
    check(flash && flash->type == ParticleType::Flash && flash->name == "Flash 2" && flash->file == "Flash", "parser: a file alone is its first definition");
    check(flash && near(flash->fadeTime, 0.2f) && near(flash->size, 30) && near(flash->speed, 500) && flash->texture == "Oreols/02.png",
          "parser: fade_time_msec is seconds, the rest as given");
    check(flash && !flash->direction && !flash->colour1 && flash->count == 0, "parser: what a file does not give is absent or 0");
    const ParticleDef* meteor = lib.find("EXPLOSEMETEOR");
    check(meteor && meteor->direction && near(*meteor->direction, {0, 1, 0}), "parser: a direction is a vector; names ignore case");

    const ParticleDef* red = lib.find("fireworks/SIMPLEFIREWORK");
    check(red && red->type == ParticleType::Firework && red->count == 30 && near(red->radius, 10) && near(red->speed, 30), "parser: a Firework's numbers");
    check(red && red->colour1 && near(red->colour1->r, 1) && near(red->colour1->g, 0) && near(red->colour1->b, 0) && near(red->colour1->a, 1),
          "parser: colour1 0xffff0000 is opaque red");
    check(red && red->colour2 && near(red->colour2->r, 1) && near(red->colour2->g, 1) && near(red->colour2->b, 0), "parser: colour2 0xffffff00 is yellow");
    const ParticleDef* trail = lib.find("Trail/TrailBlue");
    check(trail && trail->colour1 && near(trail->colour1->a, 0) && near(trail->colour1->r, 226 / 255.0f) && near(trail->colour2->b, 1) && !trail->modulateColour,
          "parser: 0x00e2e2ff keeps its zero alpha byte (drawing leaves it out); modulate_colour no");
    const ParticleDef* bolt = lib.find("Lightning");
    check(bolt && bolt->colour && !bolt->colour1 && bolt->count == 10 && near(bolt->shiftCoeff, 0.5f) && near(bolt->shakeCoeff, 0.75f), "parser: a Lightning's colour, pieces and coefficients");
    const ParticleDef* shadow = lib.find("Oreolblue/OreolShadow");
    check(shadow && shadow->shadowPlane && shadow->shadowPosition && near(shadow->speedAngular, -1.5f) && near(shadow->radius, 1) && near(shadow->radiusOffset, 1),
          "parser: an OreolShadow's plane, position and spin");
    const ParticleDef* tornado = lib.find("tornado");
    check(tornado && near(tornado->radiusSmall, 5) && near(tornado->radiusBig, 50) && near(tornado->height, 160) && near(tornado->speedAngular, 4.5f), "parser: a Tornado's radii, height and spin");
    const ParticleDef* leaves = lib.find("LeafFall");
    check(leaves && leaves->dimension && near(*leaves->dimension, {3, 2, 3}) && near(leaves->angle, 25), "parser: a LeafFall's box and angle");

    check(lib.file("fireworks").size() == 3 && lib.file("Fireworks")[2].name == "blue" && lib.file("nothing").empty(), "parser: a file's definitions in order; none for a file that is not there");
    check(lib.find("Fireworks/blue") == &lib.file("fireworks")[2] && lib.find("Fireworks/purple") == nullptr && lib.find("Gone") == nullptr, "parser: lookups by file and name, null for a miss");
    for (std::size_t i = 0; i < lib.all().size(); ++i) {
        check(lib.all()[i].id == i, "parser: a definition's id is its place");
    }
    check(particleTypeByName("lightning") == ParticleType::Lightning && !particleTypeByName("Plasma") && particleTypeName(ParticleType::LeafFall) == "LeafFall", "parser: the type names");
}

void testParserReports() {
    const std::vector<ParticleLibrary::Source> sources{
        file("Odd", {R"("name": "kept", "type": "Flash", "size": 4, "sparkle": 7, "wobble": [1, 2])",
                     R"("name": "no type", "size": 4)",
                     R"("name": "wrong type", "type": "Plasma")",
                     R"("name": "bad value", "type": "Flash", "size": "big")",
                     R"("name": "bad vector", "type": "Smoke", "direction": [1, 2])",
                     R"("name": "bad colour", "type": "Laser", "colour1": -5)",
                     R"("name": "kept too", "type": "Smoke", "num": 3)"}),
        {"Broken", "{ this is not json"},
        {"Empty", R"({"version": 1})"},
    };
    const ParticleLibrary lib = ParticleLibrary::parse(sources);
    check(lib.all().size() == 2 && lib.find("Odd/kept") && lib.find("Odd/kept too"), "reports: what could be read is kept, the rest left out");
    check(lib.unknownAttributes().size() == 2 && lib.unknownAttributes()[0] == "Odd/kept: sparkle" && lib.unknownAttributes()[1] == "Odd/kept: wobble",
          "reports: an unknown attribute is named by its definition");
    check(lib.problems().size() == 7, "reports: each definition that could not be read and each file, " + std::to_string(lib.problems().size()));
    const ParticleDef* kept = lib.find("Odd/kept");
    check(kept && near(kept->size, 4), "reports: and the known attributes of a definition with unknown ones are read");
    check(lib.find("Odd") == kept, "reports: the file is its first readable definition");
    check(ParticleLibrary::load("/no/such/folder").all().empty(), "reports: a missing folder is an empty library");
}

// ---- Flash -------------------------------------------------------------------------------------------------------------------

void testFlash() {
    ParticleSystem system(library());
    const EffectId id = system.spawn("Flash", {1, 2, 3});
    check(id != kNoEffect && system.alive(id) && system.particleCount() == 1, "flash: one sprite");
    run(system, 0.02);
    std::vector<ParticleQuad> quads = quadsOf(system);
    check(quads.size() == 1 && quads[0].blend == BlendMode::AlphaBlend && !quads[0].depthTest && !quads[0].colourOnly && quads[0].def->name == "Flash 2",
          "flash: alpha blended, no depth test, the texture as it is");
    check(near(length(quads[0].halfRight) * 2, 500 * 0.02f, 0.05f) && near(quads[0].centre, {1, 2, 3}), "flash: it swells at its speed (500 units/s) from the point");
    check(near(dot(quads[0].halfRight, kView.forward), 0) && near(dot(quads[0].halfUp, kView.forward), 0) && near(dot(quads[0].halfRight, quads[0].halfUp), 0),
          "flash: it faces the camera");
    check(near(quads[0].colourBegin.a, 1 - 0.02f / 0.2f, 0.02f), "flash: it fades in a straight line over its fade time");
    run(system, 0.06);
    quads = quadsOf(system);
    check(near(length(quads[0].halfRight) * 2, 30, 0.05f), "flash: and stops at its size (30)");
    check(near(quads[0].colourBegin.a, 1 - 0.08f / 0.2f, 0.02f), "flash: a straight fade");
    run(system, 0.13);
    check(system.particleCount() == 0 && !system.alive(id) && quadsOf(system).empty() && system.effectCount() == 0, "flash: gone when it has faded, and the effect with it");

    // The scale is of the size and the distance, not of the time.
    ParticleSystem scaled(library());
    scaled.spawn("Flash", {}, {.scale = 0.5f});
    run(scaled, 0.1);
    quads = quadsOf(scaled);
    check(quads.size() == 1 && near(length(quads[0].halfRight) * 2, 15, 0.05f), "flash: a scale of a half halves the size");
    run(scaled, 0.11);
    check(scaled.particleCount() == 0, "flash: and not the time");

    // With a direction it lies in the plane perpendicular to it, a little above the point, tested against the depth.
    ParticleSystem ring(library());
    ring.spawn("explosemeteor", {4, 0, 5});
    run(ring, 0.1);
    quads = quadsOf(ring);
    check(quads.size() == 1 && quads[0].depthTest && near(dot(quads[0].halfRight, {0, 1, 0}), 0) && near(dot(quads[0].halfUp, {0, 1, 0}), 0) &&
              near(quads[0].centre.y, 0.1f) && near(length(quads[0].halfRight) * 2, 5, 0.05f),
          "flash: explosemeteor's direction up lays it flat on the floor, swelling at 50 units/s");
    // `seconds` is how long a flash lives where its fade_time would be too short: it still swells at its speed and fades over all of it.
    ParticleSystem longer(library());
    longer.spawn("explosemeteor", {4, 0, 5}, {.seconds = 0.4f});
    run(longer, 0.3);
    quads = quadsOf(longer);
    check(quads.size() == 1 && near(length(quads[0].halfRight) * 2, 10, 0.05f) && near(quads[0].colourBegin.a, 0.25f, 0.06f),
          "flash: with `seconds` it is full size at 0.2 s and a quarter strong at 0.3 s of 0.4");
    run(longer, 0.15);
    check(longer.particleCount() == 0, "flash: and gone after it");
    ParticleSystem tilted(library());
    tilted.spawn("Flash", {}, {.direction = {1, 0, 0}});
    run(tilted, 0.05);
    quads = quadsOf(tilted);
    check(quads.size() == 1 && near(dot(quads[0].halfRight, {1, 0, 0}), 0) && near(dot(quads[0].halfUp, {1, 0, 0}), 0), "flash: a direction in the options turns any flash flat across it");
}

// ---- Firework ----------------------------------------------------------------------------------------------------------------

void testFirework() {
    ParticleSystem system(library());
    system.spawn("Fireworks/blue", {0, 20, 0});
    check(system.particleCount() == 30, "firework: its 30 sparks go off at once");
    run(system, 0.05);
    std::vector<ParticleQuad> quads = quadsOf(system);
    check(quads.size() == 30 && quads[0].colourOnly && quads[0].blend == BlendMode::Additive, "firework: additive, the colours replace the texture's");
    bool blueToCyan = true;
    for (const ParticleQuad& q : quads) {
        blueToCyan = blueToCyan && near(q.colourBegin.r, 0) && near(q.colourBegin.b, 1) && q.colourBegin.g >= -1e-4f && q.colourBegin.g < 0.3f;
    }
    check(blueToCyan, "firework: a spark begins as colour1 (blue)");
    run(system, 0.2);
    float cyan = 0, farthest = 0;
    for (const ParticleQuad& q : quadsOf(system)) {
        cyan = std::max(cyan, q.colourBegin.g);
        farthest = std::max(farthest, length(q.centre - Vec3{0, 20, 0}));
    }
    check(cyan > 0.5f && cyan <= 1 + 1e-4f, "firework: and runs towards colour2 (cyan) as it ages");
    check(farthest <= 10.5f && farthest > 5, "firework: they fly out, no farther than `radius` (10) allows, " + std::to_string(farthest));

    // They slow down: a spark goes farther in the first part of its life than in the last, and it ends at rest.
    ParticleSystem single(library());
    single.spawn("Fireworks/one", {0, 20, 0}, {.seed = 7});
    check(single.particleCount() == 1, "firework: a `num` of 1 is one spark");
    std::vector<Vec3> track;
    while (single.particleCount() > 0) {
        track.push_back(quadsOf(single)[0].centre);
        single.update(0.01);
    }
    const std::size_t n = track.size();
    check(n > 30 && length(track[n / 3] - track[0]) > 2 * length(track[n - 1] - track[2 * n / 3]), "firework: a spark is fast at first and slow at its reach, over " + std::to_string(n) + " steps");

    // The whole burst droops along `direction` (0, -1, 0).
    ParticleSystem drooping(library());
    drooping.spawn("Fireworks/blue", {0, 20, 0});
    run(drooping, 0.3);
    float sum = 0;
    for (const ParticleQuad& q : quadsOf(drooping)) {
        sum += q.centre.y - 20;
    }
    check(sum < 0, "firework: the burst has drooped along `direction` (0, -1, 0)");

    // They fade, and are all gone within 2 * radius / speed (0.67 s); a seed makes a burst repeat.
    run(system, 0.5);
    check(system.particleCount() == 0, "firework: all gone within a life");
    ParticleSystem a(library()), b(library()), c(library());
    a.spawn("Fireworks", {}, {.seed = 5});
    b.spawn("Fireworks", {}, {.seed = 5});
    c.spawn("Fireworks", {}, {.seed = 6});
    run(a, 0.2);
    run(b, 0.2);
    run(c, 0.2);
    check(near(quadsOf(a)[3].centre, quadsOf(b)[3].centre, 0) && !near(quadsOf(a)[3].centre, quadsOf(c)[3].centre, 0.01f), "firework: the same seed bursts the same way, another differently");
}

// ---- Trail -------------------------------------------------------------------------------------------------------------------

void testTrail() {
    ParticleSystem system(library());
    check(system.attach("Flash", {}) == kNoEffect && system.attach("Laser", {}) == kNoEffect, "trail: only emitters attach");
    const EffectId id = system.attach("Trail/TrailBlue", {0, 0, 0});
    check(id != kNoEffect && system.particleCount() == 0, "trail: nothing until it is stepped");
    Vec3 at{};
    for (int i = 0; i < 60; ++i) {
        at.y += 0.26f;  // a rocket at 15.6 units/s
        system.moveTo(id, at);
        system.update(1.0 / 60);
    }
    check(system.particleCount() >= 29 && system.particleCount() <= 31, "trail: about `num` (30) in the air at steady state, " + std::to_string(system.particleCount()));
    std::vector<ParticleQuad> quads = quadsOf(system);
    float oldest = 1e9f, youngest = -1e9f;
    bool sorted = true;
    for (std::size_t i = 0; i < quads.size(); ++i) {
        oldest = std::min(oldest, quads[i].centre.y);
        youngest = std::max(youngest, quads[i].centre.y);
        sorted = sorted && (i == 0 || quads[i].centre.y >= quads[i - 1].centre.y - 1e-4f);
        sorted = sorted && near(quads[i].centre.x, 0) && near(quads[i].centre.z, 0);  // radius 0: on the path
    }
    check(sorted && youngest - oldest > 6.0f && youngest - oldest < 8.0f, "trail: a line along the way it went, 0.5 s of it (7.8 units), oldest first");
    check(quads[0].colourOnly && near(quads.back().colourBegin.a, 1, 0.1f) && quads.front().colourBegin.a < 0.2f, "trail: the young are strong and the old nearly gone");
    check(quads.front().colourBegin.b > 0.95f && quads.back().colourBegin.r > quads.front().colourBegin.r, "trail: from colour1 (pale) to colour2 (blue) as it ages");
    check(length(quads.back().halfRight) > length(quads.front().halfRight), "trail: it shrinks as it ages");

    // Stopped, it puts out no more and what it left fades out.
    system.stop(id);
    const std::size_t before = system.particleCount();
    system.update(1.0 / 60);
    check(system.particleCount() <= before && system.alive(id), "trail: stopped, no more come out, and it lives while its particles do");
    run(system, 0.6);
    check(system.particleCount() == 0 && !system.alive(id) && system.effectCount() == 0, "trail: and then it is gone");

    // A long frame still leaves an even line: the particles are spread along the step, not put at its end.
    ParticleSystem jumped(library());
    const EffectId jumper = jumped.attach("Trail/TrailBlue", {0, 0, 0});
    jumped.moveTo(jumper, {0, 0, 12});
    jumped.update(0.25);
    quads = quadsOf(jumped);
    float least = 1e9f, most = -1e9f;
    for (const ParticleQuad& q : quads) {
        least = std::min(least, q.centre.z);
        most = std::max(most, q.centre.z);
    }
    check(quads.size() == 15 && least < 1 && most > 11, "trail: 15 particles spread along a jump of 12 units in a quarter second");

    // Its `radius` scatters them: TrailOrange's 1.
    ParticleSystem scattered(library());
    scattered.attach("Trail/TrailOrange", {0, 0, 0});
    run(scattered, 0.5);
    float wide = 0;
    for (const ParticleQuad& q : quadsOf(scattered)) {
        wide = std::max(wide, length(q.centre));
    }
    check(wide > 0.3f && wide <= 1.0f + 1e-3f, "trail: radius 1 scatters the particles within a unit of the point");

    // spawn() with seconds is an emitter that stops itself.
    ParticleSystem timed(library());
    const EffectId once = timed.spawn("Trail/TrailBlue", {}, {.seconds = 0.1f});
    run(timed, 0.05);
    const std::size_t half = timed.particleCount();
    run(timed, 0.2);
    const std::size_t after = timed.particleCount();
    check(half >= 2 && half <= 4 && after >= 5 && after <= 7, "trail: spawn() with `seconds` emits that long (60 a second for 0.1 s: 6), " + std::to_string(half) + ", " + std::to_string(after));
    run(timed, 1);
    check(!timed.alive(once), "trail: and the effect ends with the last of them");
    // ... and with none, puts its `num` out at once.
    ParticleSystem all(library());
    all.spawn("Trail/TrailBlue", {});
    check(all.particleCount() == 30, "trail: spawn() with no `seconds` puts all `num` out at once");
}

// ---- Smoke, bubbles, leaves, tornado -----------------------------------------------------------------------------------------

void testSmoke() {
    ParticleSystem system(library());
    const EffectId id = system.attach("Smoke", {0, 1, 0});
    run(system, 2.5);
    check(system.particleCount() >= 49 && system.particleCount() <= 51, "smoke: about `num` (50) puffs at steady state, " + std::to_string(system.particleCount()));
    const std::vector<ParticleQuad> quads = quadsOf(system);
    bool inColumn = true, rising = true, weak = true;
    float top = 0;
    for (const ParticleQuad& q : quads) {
        const Vec3 off = q.centre - Vec3{0, 1, 0};
        top = std::max(top, off.y);
        rising = rising && off.y >= -1e-3f;
        inColumn = inColumn && std::hypot(off.x, off.z) <= 0.1f + off.y * std::tan(radians(10.5f)) + 1e-3f;
        weak = weak && q.colourBegin.a <= 0.5f + 1e-4f && q.blend == BlendMode::AlphaBlend && !q.colourOnly;
    }
    check(inColumn && rising && top <= 3.0f + 1e-3f && top > 2.0f, "smoke: puffs rise in a cone of 10.5 degrees from a source `width` wide, to `height` (3) at most, " + std::to_string(top));
    check(weak, "smoke: alpha blended, weak, the texture's own colour");
    float small = 1e9f, big = 0;
    for (const ParticleQuad& q : quads) {
        small = std::min(small, length(q.halfRight));
        big = std::max(big, length(q.halfRight));
    }
    check(big > 1.5f * small && big <= 1.5f * 0.5f * 1.01f, "smoke: puffs swell as they rise, to the size (1.5) at most");
    // It leans the way it is told.
    ParticleSystem leaning(library());
    leaning.attach("Smoke", {}, {.direction = {1, 0, 0}});
    run(leaning, 1.5);
    bool sideways = true;
    for (const ParticleQuad& q : quadsOf(leaning)) {
        sideways = sideways && q.centre.x >= -1e-3f;
    }
    check(sideways && !quadsOf(leaning).empty(), "smoke: with a direction in the options it goes that way");
    system.stop(id);
    run(system, 2);
    check(system.particleCount() == 0, "smoke: and when stopped it clears");
}

void testBubbles() {
    ParticleSystem system(library());
    system.attach("SparkKoster", {0, 0.5f, 0});
    run(system, 2);
    const std::vector<ParticleQuad> quads = quadsOf(system);
    check(quads.size() >= 9 && quads.size() <= 11, "bubbles: about `num` (10) alive, " + std::to_string(quads.size()));
    bool disc = true;
    for (const ParticleQuad& q : quads) {
        // Within `radius` (2) of the column, plus the sway, and no higher than it can have risen in its life (1.5 s at 1 unit/s).
        disc = disc && std::hypot(q.centre.x, q.centre.z) <= 2.0f + 0.2f && q.centre.y >= 0.5f - 1e-3f && q.centre.y <= 0.5f + 1.5f + 1e-3f &&
               q.blend == BlendMode::AlphaBlend && near(length(q.halfRight) * 2, 1.0f, 1e-3f);
    }
    check(disc, "bubbles: they rise from a disc of `radius` at `speed`, alpha blended, `size` across");
    // The fade time (50 ms) is the edge: a bubble is full strength in the middle of its life, and not at its start.
    ParticleSystem edge(library());
    edge.spawn("SparkKoster", {});
    check(edge.particleCount() == 10 && quadsOf(edge)[0].colourBegin.a < 0.01f, "bubbles: spawn() puts `num` out, and they begin clear");
    run(edge, 0.7);
    check(near(quadsOf(edge)[0].colourBegin.a, 1), "bubbles: and are strong a while later");
}

void testLeaves() {
    ParticleSystem system(library());
    system.spawn("LeafFall", {10, 8, -4});
    check(system.particleCount() == 15, "leaves: 15 at once");
    std::vector<ParticleQuad> quads = quadsOf(system);
    bool inBox = true;
    for (const ParticleQuad& q : quads) {
        inBox = inBox && std::fabs(q.centre.x - 10) <= 1.5f + 1e-3f && std::fabs(q.centre.y - 8) <= 1.0f + 1e-3f && std::fabs(q.centre.z + 4) <= 1.5f + 1e-3f;
    }
    check(inBox, "leaves: they start in the box (3 x 2 x 3) round the point");
    run(system, 1.0);
    quads = quadsOf(system);
    float fell = 0;
    bool aside = true;
    for (const ParticleQuad& q : quads) {
        fell += (8 - q.centre.y) / 15.0f;
        aside = aside && std::hypot(q.centre.x - 10, q.centre.z + 4) <= 1.5f * 1.42f + 5.0f;
        check(q.blend == BlendMode::AlphaBlend && near(length(q.halfRight), 0.75f, 1e-3f), "leaves: alpha blended, `size` (1.5) across");
    }
    check(fell > 1.2f && fell < 3.0f, "leaves: a second later they have fallen about `speed` (2) units, " + std::to_string(fell));
    check(aside, "leaves: and drifted sideways, no more than `radius` away");
    run(system, 5);
    check(system.particleCount() == 0, "leaves: gone after height / speed (3 s, a little more for the slowest)");
    // On the floor they lie at the floor.
    ParticleSystem low(library());
    low.spawn("LeafFall", {0, 1, 0});
    run(low, 2.0);
    float lowest = 1e9f;
    for (const ParticleQuad& q : quadsOf(low)) {
        lowest = std::min(lowest, q.centre.y);
    }
    check(lowest >= 0.15f - 1e-3f, "leaves: none goes through the floor");
    // The flutter turns them by up to `angle` (25 degrees).
    ParticleSystem flutter(library());
    flutter.spawn("LeafFall", {0, 20, 0});
    float most = 0;
    for (int i = 0; i < 60; ++i) {
        flutter.update(1.0 / 30);
        for (const ParticleQuad& q : quadsOf(flutter)) {
            most = std::max(most, std::fabs(std::atan2(q.halfRight.y, q.halfRight.x)));
        }
    }
    check(most > radians(10) && most <= radians(25) + 1e-3f, "leaves: they flutter by up to 25 degrees, " + std::to_string(most * 180 / kPi));
}

void testTornado() {
    ParticleSystem system(library());
    system.attach("Tornado", {0, 0, 0}, {.scale = 0.1f});
    run(system, 1.0);
    const std::vector<ParticleQuad> quads = quadsOf(system);
    check(quads.size() > 200 && quads.size() < 300, "tornado: 1500 over its 6.4 s life makes 234 a second, " + std::to_string(quads.size()));
    bool funnel = true;
    for (const ParticleQuad& q : quads) {
        const float height = q.centre.y;
        const float expected = (5.0f + 45.0f * height / 16.0f) * 0.1f;  // the radius grows with the height; the scale is 0.1
        funnel = funnel && near(std::hypot(q.centre.x, q.centre.z), expected, 0.01f) && height <= 2.51f;
    }
    check(funnel, "tornado: a funnel, the radius growing from `radius_small` to `radius_big` over the height (scaled)");
    ParticleSystem once(library());
    once.spawn("Tornado", {}, {.scale = 0.1f, .seed = 3});
    check(once.particleCount() == 1500, "tornado: spawn() with no seconds puts its 1500 particles out at once");
}

// ---- Beams and bolts ---------------------------------------------------------------------------------------------------------

// `grow`: the front reaches the far end in that time, whatever the length and the definition's speed.
void testLaserGrow() {
    ParticleSystem system(library());
    system.beam("Laser", {0, 1, 0}, {0, 1, 100}, {.seconds = 1, .grow = 0.1f});
    system.update(0.05);
    std::vector<ParticleQuad> quads = quadsOf(system);
    check(quads.size() == 3 && near(length(quads[0].halfRight) * 2, 50, 0.01f) && near(quads[2].centre, {0, 1, 50}, 0.01f),
          "laser grow: half of a beam of 100 units is out after half of 0.1 s, where the speed alone would have 7.5");
    check(near(quads[0].colourEnd.g, 0, 0.01f) && near(quads[2].colourBegin.g, 0, 0.01f), "laser grow: and its tip is colour2 (red) already");
    system.update(0.06);
    quads = quadsOf(system);
    check(near(length(quads[0].halfRight) * 2, 100, 0.01f) && near(quads[2].centre, {0, 1, 100}, 0.01f), "laser grow: all of it is out at 0.1 s");
    // A short beam is not quicker for being short.
    ParticleSystem brief(library());
    brief.beam("Laser", {0, 1, 0}, {0, 1, 10}, {.seconds = 1, .grow = 0.1f});
    brief.update(0.05);
    check(near(length(quadsOf(brief)[0].halfRight) * 2, 5, 0.01f), "laser grow: and a beam of 10 units has 5 out at 0.05 s");
}

void testLaser() {
    ParticleSystem system(library());
    check(system.beam("Flash", {}, {1, 0, 0}) == kNoEffect && system.spawn("Laser", {}) == kNoEffect && system.attach("Laser", {}) == kNoEffect,
          "laser: only a beam() takes a beam, and a beam() only beams");
    const EffectId id = system.beam("Laser", {0, 1, 0}, {0, 1, 100}, {.seconds = 0.2f});
    system.update(0.1);
    std::vector<ParticleQuad> quads = quadsOf(system);
    check(quads.size() == 3 && quads[0].blend == BlendMode::Additive && quads[0].colourOnly, "laser: a strip and an end at each end of it");
    // The front has run 150 * 0.1 = 15 units from the first end.
    check(near(length(quads[0].halfRight) * 2, 15, 0.01f) && near(quads[0].centre, {0, 1, 7.5f}, 0.01f), "laser: the front runs out at its speed (150 units/s)");
    check(near(length(quads[0].halfUp) * 2, 1, 1e-3f) && near(dot(quads[0].halfUp, kView.forward), 0, 1e-3f) && near(dot(quads[0].halfUp, quads[0].halfRight), 0, 1e-3f),
          "laser: `size` wide, across as the camera sees it");
    check(near(quads[0].uv0.x, 1.0f / 3) && near(quads[0].uv1.x, 2.0f / 3) && near(quads[1].uv0.x, 0) && near(quads[1].uv1.x, 1.0f / 3) && near(quads[2].uv0.x, 2.0f / 3) && near(quads[2].uv1.x, 1),
          "laser: the body is the middle tile of the texture and the ends the left and the right ones");
    check(near(quads[0].colourBegin.r, 1) && near(quads[0].colourBegin.g, 1) && near(quads[0].colourBegin.b, 0) && quads[0].colourEnd.r > 0.99f && near(quads[0].colourEnd.g, 0, 0.01f),
          "laser: from colour1 (yellow) at the first end to colour2 (red) at the front, whatever of it is out");
    run(system, 0.6);
    quads = quadsOf(system);
    check(quads.size() == 3 && near(length(quads[0].halfRight) * 2, 100, 0.01f) && near(quads[0].colourEnd.g, 0, 0.01f) && near(quads[0].colourEnd.r, 1, 0.01f), "laser: it reaches the far end, which is colour2 (red)");
    check(near(quads[0].colourBegin.a, 1 - (0.7f - 0.1f - 0.2f) / 1.5f, 0.03f), "laser: full strength for `seconds`, then it fades over `fade_time_msec` (1.5 s)");
    run(system, 1.5);
    check(!system.alive(id) && quadsOf(system).empty(), "laser: gone when it has faded");

    // `fade` says how long it takes to fade, where the definition's would be too long.
    ParticleSystem quick(library());
    const EffectId brief = quick.beam("Laser", {0, 1, 0}, {0, 1, 100}, {.seconds = 0.2f, .fade = 0.25f});
    run(quick, 0.2 + 0.125);
    check(near(quadsOf(quick)[0].colourBegin.a, 0.5f, 0.15f), "laser: with `fade` it is about half strong half that long into its fade");
    run(quick, 0.25);
    check(!quick.alive(brief) && quadsOf(quick).empty(), "laser: and gone after it, not the definition's 1.5 s");

    // Aimed every frame it never fades, and follows; stopped, it fades.
    ParticleSystem held(library());
    const EffectId bolt = held.beam("Laser", {0, 1, 0}, {0, 1, 40});
    for (int i = 0; i < 120; ++i) {
        held.aim(bolt, {0, 1, 0.1f * i}, {0, 1, 40});
        held.update(1.0 / 60);
    }
    quads = quadsOf(held);
    check(held.alive(bolt) && quads.size() == 3 && near(quads[0].colourBegin.a, 1, 1e-3f) && near(quads[1].centre.z, 11.9f, 0.01f), "laser: aimed every frame it stays at full strength and moves with the aim");
    held.stop(bolt);
    run(held, 0.5);
    check(quadsOf(held)[0].colourBegin.a < 0.7f && held.alive(bolt), "laser: stopped, it fades");
    run(held, 1.1);
    check(!held.alive(bolt), "laser: and goes");

    // A speed of 0 is the whole beam at once (the yo-yo's string).
    ParticleSystem string(library());
    string.beam("rubber", {0, 1, 0}, {5, 1, 0});
    string.update(1.0 / 60);
    quads = quadsOf(string);
    check(quads.size() == 3 && near(length(quads[0].halfRight) * 2, 5, 1e-3f) && near(quads[0].colourBegin.g, 1) && near(quads[0].colourEnd.b, 1), "laser: speed 0 is all of it at once, green to blue (rubber)");
    // A beam of no length is nothing to draw.
    ParticleSystem none(library());
    none.beam("Laser", {1, 1, 1}, {1, 1, 1});
    none.update(0.1);
    check(quadsOf(none).empty(), "laser: from a point to itself draws nothing");
}

void testLightning() {
    ParticleSystem system(library());
    const Vec3 from{0, 0, 0}, to{20, 0, 0};
    const EffectId id = system.beam("Lightning", from, to, {.seconds = 0.2f, .seed = 9});
    system.update(1.0 / 60);
    std::vector<ParticleQuad> quads = quadsOf(system);
    check(quads.size() == 10 + 11, "lightning: ten pieces and an end at each of the eleven points");
    check(quads[0].blend == BlendMode::Additive && quads[0].colourOnly && near(quads[0].colourBegin.r, 238 / 255.0f) && near(quads[0].colourBegin.b, 1) && near(quads[0].colourBegin.a, 1),
          "lightning: its one colour (0x00eeeeFF, the alpha byte left out)");
    // The pieces join end to end from `from` to `to`; the points between are thrown no more than shift_coeff (0.5) of a piece (2) aside.
    bool joined = near(quads[0].centre - quads[0].halfRight, from, 1e-3f) && near(quads[9].centre + quads[9].halfRight, to, 1e-3f);
    float aside = 0;
    for (int k = 0; k < 9; ++k) {
        joined = joined && near(quads[static_cast<std::size_t>(k)].centre + quads[static_cast<std::size_t>(k)].halfRight,
                                quads[static_cast<std::size_t>(k) + 1].centre - quads[static_cast<std::size_t>(k) + 1].halfRight, 1e-3f);
        const Vec3 point = quads[static_cast<std::size_t>(k)].centre + quads[static_cast<std::size_t>(k)].halfRight;
        aside = std::max(aside, std::hypot(point.y, point.z));
        joined = joined && near(point.x, 2.0f * static_cast<float>(k + 1), 1.01f);
    }
    check(joined, "lightning: the pieces are joined, from one end to the other");
    check(aside > 0.1f && aside <= 0.5f * 2.0f + 1e-3f, "lightning: the points are thrown aside by up to shift_coeff of a piece, " + std::to_string(aside));
    // It shakes: a new shape each 1/30 s, mostly (shake_coeff 0.75) different; within a step it holds.
    const Vec3 first = quads[0].centre + quads[0].halfRight;
    system.update(1.0 / 120);
    check(near(quadsOf(system)[0].centre + quadsOf(system)[0].halfRight, first, 1e-6f), "lightning: the shape holds for a step");
    system.update(0.05);
    const Vec3 later = quadsOf(system)[0].centre + quadsOf(system)[0].halfRight;
    check(length(later - first) > 0.05f, "lightning: and shakes to another after it");
    ParticleSystem twin(library());
    twin.beam("Lightning", from, to, {.seconds = 0.2f, .seed = 9});
    twin.update(1.0 / 60);
    check(near(quadsOf(twin)[3].centre, quads[3].centre, 0), "lightning: the same seed makes the same bolt");
    run(system, 0.2);
    check(quadsOf(system)[0].colourBegin.a < 1 && system.alive(id), "lightning: it fades after its `seconds`");
    run(system, 0.6);
    check(!system.alive(id), "lightning: and is gone after its fade time (0.5 s)");
    // Faces the camera at any angle: the strips' width is across the picture.
    ParticleSystem vertical(library());
    vertical.beam("Lightning", {0, 0, 0}, {0, 10, 0}, {.seed = 2});
    vertical.update(1.0 / 60);
    check(near(dot(quadsOf(vertical)[0].halfUp, kView.forward), 0, 1e-3f), "lightning: its strips face the camera");
}

// What a file that gives little makes of it: lives that never come to nothing, and quads that stay finite.
void testDegenerate() {
    for (const char* name : {"Degenerate/point", "Degenerate/still", "Degenerate/bare", "Degenerate/nothing"}) {
        const ParticleDef* def = library().find(name);
        check(def != nullptr && particleLife(*def) >= 0.01f, std::string(name) + ": it lives some time");
        ParticleSystem system(library());
        system.spawn(name, {0, 1, 0});
        bool finite = true;
        for (int i = 0; i < 400; ++i) {
            for (const ParticleQuad& q : quadsOf(system)) {
                finite = finite && std::isfinite(q.centre.x + q.centre.y + q.centre.z + length(q.halfRight) + length(q.halfUp) + q.colourBegin.a);
            }
            system.update(0.01);
        }
        check(finite, std::string(name) + ": every quad is finite, from the first frame");
        check(system.particleCount() == 0, std::string(name) + ": and it ends");
    }
}

// ---- The system --------------------------------------------------------------------------------------------------------------

void testSystem() {
    ParticleSystem system(library());
    check(system.spawn("Nothing", {}) == kNoEffect && system.attach("Nothing", {}) == kNoEffect && system.beam("Nothing", {}, {1, 0, 0}) == kNoEffect, "system: a name the library lacks gives no effect");
    check(system.spawn("Oreolblue", {}) == kNoEffect && system.spawn("Oreolblue/OreolShadow", {}) == kNoEffect, "system: the prizes' halos are not its to draw");
    check(!system.alive(kNoEffect) && !system.alive(99), "system: neither no effect nor a stranger is alive");
    system.moveTo(99, {1, 1, 1});  // no effect
    system.aim(99, {}, {});
    system.stop(99);

    const EffectId a = system.spawn("Flash", {});
    const EffectId b = system.spawn("Flash", {});
    check(a != b && a != kNoEffect && system.effectCount() == 2, "system: each effect has its own id");
    system.update(0.3);
    check(!system.alive(a) && !system.alive(b), "system: both over");
    system.spawn("Fireworks", {});
    system.clear();
    check(system.effectCount() == 0 && system.particleCount() == 0, "system: clear() ends everything");

    // Particles are in the order they began, so what was made later is drawn over.
    ParticleSystem order(library());
    order.spawn("Flash", {1, 0, 0});
    order.update(0.01);
    order.spawn("Flash", {2, 0, 0});
    order.update(0.01);
    const std::vector<ParticleQuad> quads = quadsOf(order);
    check(quads.size() == 2 && near(quads[0].centre.x, 1) && near(quads[1].centre.x, 2), "system: oldest first");

    check(near(particleLife(*library().find("Smoke")), 1.5f) && near(particleLife(*library().find("Trail")), 0.5f) && near(particleLife(*library().find("Fireworks")), 2 * 10 / 30.0f) &&
              near(particleLife(*library().find("LeafFall")), 3) && near(particleLife(*library().find("Tornado")), 6.4f) && near(particleLife(*library().find("SparkKoster")), 1.5f),
          "system: how long a particle of each kind lives");

    // The view says which way a quad faces and how it turns: a camera looking along +y (from above) lays flashes in the xz plane.
    ParticleView above;
    above.right = {1, 0, 0};
    above.up = {0, 0, 1};
    above.forward = {0, -1, 0};
    ParticleSystem top(library());
    top.spawn("Flash", {});
    top.update(0.05);
    std::vector<ParticleQuad> seen;
    top.appendQuads(above, seen);
    check(seen.size() == 1 && near(dot(seen[0].halfRight, above.forward), 0) && near(dot(seen[0].halfUp, above.forward), 0), "system: quads face the view they are asked for");

    Mat4 facing;  // rows: right, up, forward
    facing.m[0][0] = 0; facing.m[0][2] = 1;
    facing.m[1][1] = 1;
    facing.m[2][0] = -1; facing.m[2][2] = 0;
    const ParticleView made = particleViewOf(facing);
    check(near(made.right, {0, 0, 1}) && near(made.up, {0, 1, 0}) && near(made.forward, {-1, 0, 0}), "system: a view from the rows of a facing matrix");
}

// A name the library lacks is said once, however often the effect is asked for (a rocket that retries its trail every frame would fill the log),
// and what a caller gives that is not a number does not get into the simulation.
void testHygiene() {
    {
        WarningCapture log;
        ParticleSystem system(library());
        for (int i = 0; i < 50; ++i) {
            system.spawn("Nothing", {});
            system.attach("Nothing", {});
            system.beam("Nothing", {}, {1, 0, 0});
        }
        check(log.count == 1, "hygiene: a missing effect is logged once, not on every call (" + std::to_string(log.count) + " warnings)");
        system.spawn("Another", {});
        system.spawn("Another", {});
        check(log.count == 2, "hygiene: and another name is said once too (" + std::to_string(log.count) + ")");
    }

    constexpr float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const auto finite = [](const std::vector<ParticleQuad>& quads) {
        return !quads.empty() && std::ranges::all_of(quads, [](const ParticleQuad& q) {
            return std::isfinite(q.centre.x + q.centre.y + q.centre.z + length(q.halfRight) + length(q.halfUp) + q.colourBegin.a);
        });
    };
    ParticleSystem scaled(library());
    scaled.spawn("Flash", {}, {.scale = nan});
    scaled.update(0.1);
    const std::vector<ParticleQuad> flash = quadsOf(scaled);
    check(finite(flash) && near(length(flash[0].halfRight) * 2, 30, 0.01f), "hygiene: a scale that is not a number is the default one (the flash is its 30 across)");
    ParticleSystem timed(library());
    timed.spawn("Smoke", {}, {.seconds = nan});
    timed.attach("Trail/TrailBlue", {}, {.seconds = inf, .fade = nan});
    timed.update(0.1);
    check(finite(quadsOf(timed)), "hygiene: neither is a time or a fade that is not a number");

    // A step that is not a time passes none: the emitter's debt and the particles' ages stay numbers.
    ParticleSystem steps(library());
    const EffectId trail = steps.attach("Trail/TrailBlue", {});
    steps.update(0.1);
    const std::size_t before = steps.particleCount();
    for (const double bad : {static_cast<double>(nan), static_cast<double>(inf), -1.0, 1e300}) {
        steps.update(bad);
    }
    check(finite(quadsOf(steps)) && steps.alive(trail), "hygiene: a step of NaN, infinity, less than nothing or 1e300 seconds leaves finite particles");
    steps.update(0.1);
    check(steps.particleCount() > 0 && finite(quadsOf(steps)), "hygiene: and the trail still emits afterwards (" + std::to_string(before) + " before, " +
                                                                    std::to_string(steps.particleCount()) + " after)");
    // An emitter holds its `num` particles at most, however long the step.
    ParticleSystem long_(library());
    long_.attach("Trail/TrailBlue", {});
    long_.update(1e20);
    check(long_.particleCount() <= 30, "hygiene: a step far longer than a particle's life puts out no more than the 30 a trail keeps (" + std::to_string(long_.particleCount()) + ")");
}

// A trail whose particles are asked to live a shorter time: it is a shorter line, still `num` of them while it flies.
void testTrailLife() {
    ParticleSystem system(library());
    const EffectId id = system.attach("Trail/TrailBlue", {}, {.fade = 0.06f});
    for (int i = 0; i < 60; ++i) {
        system.moveTo(id, {0, 0, 52.5f * static_cast<float>((i + 1) / 60.0)});
        system.update(1.0 / 60);
    }
    float nearest = 1e9f, farthest = -1e9f;
    for (const ParticleQuad& q : quadsOf(system)) {
        nearest = std::min(nearest, q.centre.z);
        farthest = std::max(farthest, q.centre.z);
    }
    check(system.particleCount() >= 25 && system.particleCount() <= 31, "trail life: it keeps its 30 particles alive (" + std::to_string(system.particleCount()) + ")");
    check(farthest - nearest < 3.5f && farthest - nearest > 2.0f, "trail life: over the 0.06 s they live a trail at 52.5 units a second is 3 units long (" + std::to_string(farthest - nearest) + ")");
    system.stop(id);
    system.update(0.07);
    check(system.particleCount() == 0, "trail life: and what it left is gone in 0.06 s, not the file's half second");
}

// ---- The batches -------------------------------------------------------------------------------------------------------------

void testBatches() {
    std::vector<std::string> asked;
    ParticleBatches batches(library(), [&](std::string_view name) -> TextureHandle {
        asked.emplace_back(name);
        if (name == "Particles/ParticleRoket.png") {
            return 0;  // the Tornado's, which the game does not have
        }
        return name == ParticleBatches::kFallbackTexture ? 90 : static_cast<TextureHandle>(10 + asked.size());
    });
    ParticleSystem system(library());
    system.spawn("Flash", {0, 0, 0});
    system.spawn("Flash", {1, 0, 0});
    system.spawn("Fireworks", {0, 10, 0});
    system.spawn("Smoke", {0, 0, 0});
    system.spawn("explosemeteor", {0, 0, 0});
    system.update(0.05);
    batches.build(system, kView);
    const std::span<const EffectBatch> built = batches.batches();
    check(built.size() == 4, "batches: one for each texture and way of drawing, not one for each particle: " + std::to_string(built.size()));
    std::size_t vertices = 0;
    for (const EffectBatch& batch : built) {
        vertices += batch.vertices.size();
        check(batch.vertices.size() % 6 == 0, "batches: whole quads");
    }
    check(vertices == system.particleCount() * 6 && batches.quadCount() == system.particleCount(), "batches: six vertices to a quad");
    check(built.front().blend == BlendMode::AlphaBlend && built.back().blend == BlendMode::Additive, "batches: alpha blended first, so that the glows go over the smoke");
    bool flatKept = false, replaces = false;
    for (const EffectBatch& batch : built) {
        flatKept = flatKept || batch.depthTest;
        replaces = replaces || batch.colourOnly;
    }
    check(flatKept && replaces, "batches: the flat flash is depth tested and the firework's colour replaces the texture's");
    const std::size_t lookups = asked.size();
    batches.build(system, kView);
    check(asked.size() == lookups, "batches: a texture is looked up once");

    // The corners of a quad: the picture's top left first, the colours at the two ends of halfRight, the night's brightness on the colour.
    ParticleQuad quad;
    quad.def = library().find("Flash");
    quad.centre = {10, 20, 30};
    quad.halfRight = {2, 0, 0};
    quad.halfUp = {0, 1, 0};
    quad.uv0 = {0.25f, 0};
    quad.uv1 = {0.75f, 1};
    quad.colourBegin = {1, 0.5f, 0, 0.8f};
    quad.colourEnd = {0, 1, 0, 0.4f};
    batches.build(std::span<const ParticleQuad>(&quad, 1), 0.5f);
    check(batches.batches().size() == 1 && batches.batches()[0].vertices.size() == 6, "batches: a quad is two triangles");
    const std::span<const EffectVertex> v = batches.batches()[0].vertices;
    check(near(v[0].position, {8, 21, 30}) && near(v[1].position, {12, 21, 30}) && near(v[2].position, {12, 19, 30}) && near(v[5].position, {8, 19, 30}) && near(v[3].position, v[0].position) && near(v[4].position, v[2].position),
          "batches: the corners, top left first");
    check(near(v[0].uv.x, 0.25f) && near(v[0].uv.y, 0) && near(v[2].uv.x, 0.75f) && near(v[2].uv.y, 1), "batches: the texture coordinates");
    check(near(v[0].color.r, 0.5f) && near(v[0].color.g, 0.25f) && near(v[0].color.a, 0.8f) && near(v[1].color.g, 0.5f) && near(v[1].color.a, 0.4f) && near(v[5].color.g, 0.25f),
          "batches: the begin colour on the left corners, the end on the right, dimmed but not in alpha");

    // A texture that cannot be found is the soft dot, and nothing is drawn with texture 0.
    ParticleSystem tornado(library());
    tornado.spawn("Tornado", {}, {.scale = 0.1f});
    tornado.update(0.1);
    batches.build(tornado, kView);
    check(batches.batches().size() == 1 && batches.batches()[0].texture == 90, "batches: a definition whose texture is missing takes the fallback");

    batches.build(std::span<const ParticleQuad>());
    check(batches.batches().empty() && batches.quadCount() == 0, "batches: nothing, no batches");

    // preload() looks every texture up at once, the halos' not (the prizes draw those), and drawing asks for nothing after it; the missing
    // texture is said when it is first drawn.
    std::vector<std::string> early;
    ParticleBatches loaded(library(), [&](std::string_view name) -> TextureHandle {
        early.emplace_back(name);
        return name == "Particles/ParticleRoket.png" ? 0 : (name == ParticleBatches::kFallbackTexture ? 90 : 7);
    });
    check(early.empty(), "batches: nothing is looked up before it is asked for");
    WarningCapture log;
    loaded.preload();
    const std::set<std::string> textures(early.begin(), early.end());
    check(textures.contains("Particles/ParticleBubble.png") && textures.contains("Particles/ParticleLaser1.png") &&
              textures.contains("Particles/ParticleRoket.png") && !textures.contains("Oreols/07.png"),
          "batches: preload() looks up the textures of the definitions, not the halos'");
    check(log.count == 0, "batches: and says nothing yet of the one it cannot find");
    const std::size_t looked = early.size();
    ParticleSystem every(library());
    for (const ParticleDef& def : library().all()) {
        if (def.type != ParticleType::Oreol && def.type != ParticleType::Laser && def.type != ParticleType::Lightning) {
            every.spawn(def.file + "/" + def.name, {0, 1, 0});
        }
    }
    every.update(0.05);
    loaded.build(every, kView);
    loaded.build(every, kView);
    check(early.size() == looked, "batches: so drawing every kind of effect asks for no texture");
    check(log.count == 1, "batches: the missing texture is said once, when it is first drawn (" + std::to_string(log.count) + ")");
}

// ---- The table of explosions --------------------------------------------------------------------------------------------------

void testBrickEffects() {
    const auto names = [](int type) {
        std::string out;
        for (const std::string_view name : explosionEffects(type)) {
            out += std::string(out.empty() ? "" : " ") + std::string(name);
        }
        return out;
    };
    check(names(3) == "exploseball", "bricks: prizes, bat bullets and ships' hulls (exploseType 3) go off in the small flash");
    check(names(6) == "explosesmall", "bricks: the stuff that has splinters (6) in the larger one");
    check(names(5).empty(), "bricks: people, animals and trees (5) have none: they shrink away");
    check(names(8).empty(), "bricks: dynamite (8) has the blast the level names");
    check(names(9) == "explosemeteor", "bricks: the meteor's class (9) is the ring on the floor");
    check(names(0).empty() && names(2).empty() && names(7).empty() && names(-1).empty() && names(10).empty(), "bricks: and nothing for a type that breaks nothing or that the data does not have");
    // By material: the type's flash, but not for the island, which the footage shows sinking (any case), and not for no material.
    GameMaterial shark, island, shuttlecock;
    shark.name = "shark.jpg";
    shark.exploseType = 3;
    island.name = "Ostrov.jpg";
    island.exploseType = 3;
    shuttlecock.name = "volan";
    shuttlecock.exploseType = 0;
    check(explosionEffects(&shark).size() == 1 && explosionEffects(&shark)[0] == "exploseball", "bricks: a material goes off in its type's flash");
    check(explosionEffects(&island).empty(), "bricks: the island (Ostrov.jpg, type 3) has none, as in the footage");
    island.name = "ostrov.JPG";
    check(explosionEffects(&island).empty(), "bricks: in any case");
    check(explosionEffects(&shuttlecock).empty() && explosionEffects(static_cast<const GameMaterial*>(nullptr)).empty(), "bricks: the shuttlecock (type 0) and no material have none");
}

}  // namespace

int main() {
    testParser();
    testParserReports();
    testFlash();
    testFirework();
    testTrail();
    testSmoke();
    testBubbles();
    testLeaves();
    testTornado();
    testLaser();
    testLaserGrow();
    testLightning();
    testDegenerate();
    testSystem();
    testHygiene();
    testTrailLife();
    testBatches();
    testBrickEffects();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("all particle checks passed");
    return 0;
}
