// The particle files of the real game data (needs it: skips with exit code 77 when MB3_ASSETS is not set), no GPU.
//
//   mb_particle_smoke
//        - all 20 files of Data/Particles read, with every attribute known and nothing left out, and what they define is what
//          docs/OBJECTS.md section 8 lists (the types and counts, the six fireworks and their colours, the explosions' sizes);
//        - every texture they name is in Textures, but the Tornado's (`ParticleRoket.png`);
//        - what the prizes' halos draw (PrizeScene) is what the Oreol files say: the spins and the textures;
//        - every particle file that the objects and the levels name (a flicker's `names`, a dynamite's `target`, the objects' `particles1`
//          and `particles2`) and every effect the table of explosions gives is in the library;
//        - each definition runs through the system as its type does, for as long as it lives: finite quads, and the number of particles
//          an emitter holds in the end is its `num`.

#include "mb/engine/Files.h"
#include "mb/game/BrickEffects.h"
#include "mb/game/ParticleDef.h"
#include "mb/game/ParticleSystem.h"
#include "mb/game/TextureIndex.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;
using json = nlohmann::json;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool sane(const ParticleQuad& q) {
    const auto colour = [](Color c) { return c.r >= -1e-4f && c.r <= 1.0001f && c.g >= -1e-4f && c.g <= 1.0001f && c.b >= -1e-4f && c.b <= 1.0001f && c.a >= -1e-4f && c.a <= 1.0001f; };
    return q.def != nullptr && finite(q.centre) && finite(q.halfRight) && finite(q.halfUp) && colour(q.colourBegin) && colour(q.colourEnd);
}

// The definitions of docs/OBJECTS.md section 8: file, how many it holds, the type of the first.
struct Expected {
    const char* file;
    std::size_t count;
    ParticleType type;
};
constexpr std::array<Expected, 20> kFiles{{
    {"Fireworks", 6, ParticleType::Firework},
    {"Flash", 1, ParticleType::Flash},
    {"Laser", 1, ParticleType::Laser},
    {"LeafFall", 1, ParticleType::LeafFall},
    {"Lightning", 1, ParticleType::Lightning},
    {"Oreolblue", 2, ParticleType::Oreol},
    {"Oreolred", 2, ParticleType::Oreol},
    {"Smoke", 1, ParticleType::Smoke},
    {"Spark", 1, ParticleType::WaterBubble},
    {"SparkKoster", 1, ParticleType::WaterBubble},
    {"Tornado", 1, ParticleType::Tornado},
    {"Trail", 2, ParticleType::Trail},
    {"TrailMeteor", 1, ParticleType::Trail},
    {"WaterBubbles", 1, ParticleType::WaterBubble},
    {"exploseball", 1, ParticleType::Flash},
    {"explosebig", 1, ParticleType::Flash},
    {"explosemeteor", 1, ParticleType::Flash},
    {"explosesmall", 1, ParticleType::Flash},
    {"exploseyadro", 1, ParticleType::Flash},
    {"rubber", 1, ParticleType::Laser},
}};

void testDefinitions(const ParticleLibrary& lib) {
    check(lib.problems().empty(), "files: every file and definition reads (" + std::to_string(lib.problems().size()) + " problems)");
    for (const std::string& attribute : lib.unknownAttributes()) {
        check(false, "files: an attribute the parser does not know: " + attribute);
    }
    std::size_t total = 0;
    for (const Expected& expected : kFiles) {
        const std::span<const ParticleDef> defs = lib.file(expected.file);
        total += defs.size();
        check(defs.size() == expected.count, std::string(expected.file) + ": " + std::to_string(expected.count) + " definitions, " + std::to_string(defs.size()));
        check(!defs.empty() && defs[0].type == expected.type && defs[0].file == expected.file, std::string(expected.file) + ": a " + std::string(particleTypeName(expected.type)));
    }
    check(lib.all().size() == total && total == 28, "files: 28 definitions in 20 files, and no file besides those");

    // The six fireworks and their colours (ARGB, colour1 to colour2): Fireworks.xml.
    struct Burst {
        const char* name;
        std::uint32_t one, two;
    };
    const Burst bursts[] = {{"SimpleFirework", 0xffff0000, 0xffffff00}, {"red", 0xffff0000, 0xffff8800}, {"yellow", 0xffffff00, 0xffff88ff},
                            {"green", 0xff00ff00, 0xffffff00},         {"blue", 0xff0000ff, 0xff00ffff}, {"magenta", 0xffff00ff, 0xffffffff}};
    const std::span<const ParticleDef> fireworks = lib.file("Fireworks");
    for (std::size_t i = 0; i < fireworks.size() && i < std::size(bursts); ++i) {
        const ParticleDef& def = fireworks[i];
        const auto channel = [](std::uint32_t argb, int shift) { return static_cast<float>((argb >> shift) & 0xFF) / 255.0f; };
        check(def.name == bursts[i].name && def.colour1 && def.colour2 && near(def.colour1->r, channel(bursts[i].one, 16)) && near(def.colour1->g, channel(bursts[i].one, 8)) &&
                  near(def.colour1->b, channel(bursts[i].one, 0)) && near(def.colour2->r, channel(bursts[i].two, 16)) && near(def.colour2->g, channel(bursts[i].two, 8)) &&
                  near(def.colour2->b, channel(bursts[i].two, 0)),
              "fireworks: " + std::string(bursts[i].name) + "'s two colours");
        check(def.count == 30 && near(def.radius, 10) && near(def.speed, 30) && near(def.fadeTime, 0.5f) && near(def.size, 3.5f), "fireworks: " + def.name + " has 30 sparks, radius 10, speed 30, 500 ms, size 3.5");
    }

    // A dynamite's flash is five times its blast's radius across (section 6).
    const ParticleDef* big = lib.find("explosebig");
    const ParticleDef* small = lib.find("explosesmall");
    check(big && near(big->size, 25) && near(big->speed, 250) && near(big->fadeTime, 0.5f) && small && near(small->size, 15), "explosions: explosebig 25 and explosesmall 15 across, 5 times the radius 5 and 3");
    const ParticleDef* meteor = lib.find("explosemeteor");
    check(meteor && meteor->direction && near(meteor->direction->y, 1) && meteor->texture == "Particles/boom.png", "explosions: the meteor's flash is the ring, flat on the floor");
    check(lib.find("exploseyadro") && near(lib.find("exploseyadro")->size, 10) && lib.find("exploseyadro")->texture == "Oreols/11.png", "explosions: the cannonball's is the 11th halo, 10 across");

    // The rocket's trail and the meteor's.
    const ParticleDef* orange = lib.find("Trail/TrailOrange");
    check(orange && orange->count == 30 && near(orange->radius, 1) && near(orange->size, 2.5f) && !orange->modulateColour && lib.find("TrailMeteor") && near(lib.find("TrailMeteor")->size, 5), "trails: TrailOrange, TrailMeteor");
    // The torch's sparks and smoke.
    check(lib.find("Spark") && near(lib.find("Spark")->radius, 0.5f) && lib.find("SparkKoster") && near(lib.find("SparkKoster")->radius, 2) && lib.find("Smoke") && near(lib.find("Smoke")->height, 3), "fires: Spark, SparkKoster and Smoke");
}

void testTextures(const ParticleLibrary& lib, const TextureIndex& textures) {
    for (const ParticleDef& def : lib.all()) {
        const bool found = !textures.find(def.texture).empty();
        check(found == (def.texture != "Particles/ParticleRoket.png"), def.file + "/" + def.name + ": its texture " + def.texture + (found ? " is there" : " is missing: only the Tornado's is"));
    }
    check(!textures.find("Particles/PARTICLE.PNG").empty(), "textures: the soft dot a missing texture is drawn with is there");
}

// What PrizeScene draws for a halo (docs/GAMEPLAY.md section 16.1): the turn of the star and of the mark, and their textures.
void testHalos(const ParticleLibrary& lib) {
    const ParticleDef* blue = lib.find("Oreolblue");
    const ParticleDef* blueMark = lib.find("Oreolblue/OreolShadow");
    const ParticleDef* red = lib.find("Oreolred");
    const ParticleDef* redMark = lib.find("Oreolred/OreolShadow");
    check(blue && redMark && blueMark && red, "halos: both files have the star and the mark");
    if (!(blue && redMark && blueMark && red)) {
        return;
    }
    check(near(blue->speedAngular, 3.5f) && near(red->speedAngular, 3.5f) && near(blueMark->speedAngular, -1.5f) && near(redMark->speedAngular, -1.5f), "halos: the star turns at 3.5 and the mark at -1.5 radians a second");
    check(blue->texture == "Oreols/02.png" && blueMark->texture == "Oreols/07.png" && red->texture == "Oreols/11.png" && redMark->texture == "Oreols/09.png", "halos: orange and blue for a good prize, red for a bad one");
    check(blueMark->shadowPlane && near(blueMark->shadowPlane->y, 1) && blueMark->shadowPosition && near(blueMark->shadowPosition->y, 0.1f), "halos: the mark lies on the floor, 0.1 up");
}

void collectParticleNames(const json& node, std::set<std::string>& out) {
    if (node.is_object()) {
        for (const auto& [key, value] : node.items()) {
            if ((key == "particles1" || key == "particles2") && value.is_string()) {
                out.insert(value.get<std::string>());
            }
            collectParticleNames(value, out);
        }
    } else if (node.is_array()) {
        for (const json& child : node) {
            collectParticleNames(child, out);
        }
    }
}

// Every file the data names: in the object definitions, and baked into the levels.
void testNamed(const ParticleLibrary& lib, const fs::path& data) {
    std::set<std::string> named;
    for (const auto& entry : fs::recursive_directory_iterator(data / "Objects")) {
        if (entry.is_regular_file() && lower(entry.path().extension().string()) == ".json") {
            if (const std::optional<std::string> text = readTextFile(entry.path())) {
                collectParticleNames(json::parse(*text), named);
            }
        }
    }
    check(named == std::set<std::string>{"smoke", "spark", "sparkKoster", "trail", "trailmeteor"}, "named: the objects name smoke, spark, sparkKoster, trail and trailmeteor");
    for (const std::string& name : named) {
        check(lib.find(name) != nullptr, "named: the object files' " + name + " is in the library");
    }
    std::set<std::string> baked;
    int levels = 0;
    for (const auto& entry : fs::directory_iterator(data / "Levels")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        const std::optional<std::string> text = readTextFile(entry.path());
        const json level = json::parse(text.value_or("{}"), nullptr, false);
        ++levels;
        for (const json& actor : level.value("actors", json::array())) {
            const json object = actor.value("object", json::object());
            if (object.contains("target")) {
                baked.insert(object["target"].get<std::string>());
            }
            for (const json& name : object.value("names", json::array())) {
                baked.insert(name.get<std::string>());
            }
        }
    }
    check(levels == 83, "named: 83 levels");
    check(baked == std::set<std::string>{"explosebig", "explosesmall", "smoke", "spark", "sparkKoster"}, "named: the levels name the two blasts, smoke, spark and sparkKoster");
    for (const std::string& name : baked) {
        check(lib.find(name) != nullptr, "named: the levels' " + name + " is in the library");
    }
    for (int type = 0; type <= 9; ++type) {
        for (const std::string_view effect : explosionEffects(type)) {
            check(lib.find(effect) != nullptr, "named: the table's " + std::string(effect) + " (exploseType " + std::to_string(type) + ") is in the library");
        }
    }
    for (const char* used : {"Trail/TrailOrange", "Fireworks", "Smoke", "LeafFall", "exploseyadro"}) {
        check(lib.find(used) != nullptr, std::string("named: ") + used + ", which the game spawns by name, is in the library");
    }
}

// Each definition run for as long as it lives, as its type is used: the quads stay finite, an emitter holds `num` particles and everything
// ends.
void testRunning(const ParticleLibrary& lib) {
    for (const ParticleDef& def : lib.all()) {
        const std::string name = def.file + "/" + def.name;
        ParticleSystem system(lib);
        const float scale = def.type == ParticleType::Tornado ? 0.1f : 1.0f;
        EffectId id = kNoEffect;
        switch (def.type) {
            case ParticleType::Oreol:
                check(system.spawn(name, {0, 1, 0}) == kNoEffect, name + ": the halos are the prizes' to draw, not the system's");
                continue;
            case ParticleType::Laser:
            case ParticleType::Lightning:
                id = system.beam(name, {0, 1, -10}, {5, 1, 10}, {.seconds = 0.2f});
                break;
            case ParticleType::Trail:
            case ParticleType::Smoke:
            case ParticleType::WaterBubble:
            case ParticleType::Tornado:
                id = system.attach(name, {0, 1, 0}, {.scale = scale});
                break;
            default:
                id = system.spawn(name, {0, 1, 0}, {.scale = scale});
                break;
        }
        check(id != kNoEffect, name + ": it can be started");
        std::vector<ParticleQuad> quads;
        bool allSane = true;
        std::size_t most = 0, steady = 0;
        const bool emitter = def.type == ParticleType::Trail || def.type == ParticleType::Smoke || def.type == ParticleType::WaterBubble || def.type == ParticleType::Tornado;
        const float life = particleLife(def);
        const int stopAt = emitter ? static_cast<int>(std::ceil((life + 1.0f) * 60)) : 0;
        int frame = 0;
        for (; frame < 60 * 40 && system.alive(id); ++frame) {
            if (emitter) {
                system.moveTo(id, {0.1f * static_cast<float>(frame % 120), 1, 0});
                if (frame == stopAt) {
                    steady = system.particleCount();
                    system.stop(id);
                }
            }
            system.update(1.0 / 60);
            quads.clear();
            system.appendQuads({{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, quads);
            for (const ParticleQuad& q : quads) {
                allSane = allSane && sane(q);
            }
            most = std::max(most, quads.size());
        }
        check(allSane, name + ": every quad is finite, with its colours in range");
        check(!system.alive(id) && system.particleCount() == 0 && frame < 60 * 40, name + ": it ends");
        check(most > 0, name + ": and draws something");
        std::printf("  %-28s %-12s lives %5.2f s, at most %4zu quads, %4zu held by an emitter\n", name.c_str(), std::string(particleTypeName(def.type)).c_str(), life, most, steady);
        if (emitter) {
            const auto count = static_cast<std::size_t>(def.count);
            check(steady + 3 >= count && steady <= count + 3, name + ": an emitter holds its `num` (" + std::to_string(def.count) + ") particles, " + std::to_string(steady));
        }
    }
}

}  // namespace

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path root = env;
    if (!fs::is_directory(root / "Data" / "Particles") || !fs::is_directory(root / "Data" / "Levels")) {
        std::printf("SKIP: converted data not found under %s\n", root.string().c_str());
        return 77;
    }
    const ParticleLibrary lib = ParticleLibrary::load(root / "Data" / "Particles");
    testDefinitions(lib);
    testTextures(lib, TextureIndex(root / "Textures"));
    testHalos(lib);
    testNamed(lib, root / "Data");
    testRunning(lib);
    if (failures == 0) {
        std::puts("all particle files checked");
    }
    return failures == 0 ? 0 : 1;
}
