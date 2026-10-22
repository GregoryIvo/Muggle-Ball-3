// The prizes against the real game data (needs it: skips with exit code 77 when MB3_ASSETS is not set), no GPU.
//
//   mb_prize_smoke
//        - all 28 prizes have their file in Data/Objects/Prizes, and the table's red halo is the file's `oreol`;
//        - each has its material in the group Prizes, a caption in languages.json, and (those with an icon) a PNG in
//          Textures/Icons; a catch in a real level pays the material's brickScore, the Meteorite Rain's own 4 and not the 250 of
//          the projectile of the same name included;
//        - the mean chance that a destroyed object leaves a prize, over the destroyable objects of the 83 levels, is the 14.7 % of
//          the footage (docs/GAMEPLAY.md section 15).

#include "mb/game/Gameplay.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/StringTable.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

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

// The `oreol` of the file's object, "oreolred" or "oreolblue".
std::string halo(const fs::path& file) {
    std::ifstream in(file);
    const json root = json::parse(in);
    for (const json& group : root["root"]["children"]) {
        for (const json& object : group.value("children", json::array())) {
            for (const json& part : object.value("children", json::array())) {
                if (part.value("tag", "") == "extra") {
                    return part["attrs"].value("oreol", "");
                }
            }
        }
    }
    return "";
}

fs::path iconFile(const fs::path& icons, const std::string& stem) {
    for (const auto& entry : fs::directory_iterator(icons)) {
        if (lower(entry.path().stem().string()) == lower(stem) && entry.path().extension() == ".png") {
            return entry.path();
        }
    }
    return {};
}

void checkTable(const fs::path& assets, const MaterialLibrary& materials, const Level& level) {
    const StringTable strings = StringTable::load(assets / "Data" / "xml" / "languages.json");
    for (int i = 0; i < kPrizeKindCount; ++i) {
        const PrizeInfo& info = prizeInfo(static_cast<PrizeKind>(i));
        const std::string stem(info.stem);
        const fs::path file = assets / "Data" / "Objects" / "Prizes" / (stem + ".json");
        check(fs::is_regular_file(file), stem + ": a file in Data/Objects/Prizes");
        if (fs::is_regular_file(file)) {
            check(halo(file) == (info.bad ? "oreolred" : "oreolblue"), stem + ": the halo is the table's, " + halo(file));
        }
        const GameMaterial* material = nullptr;
        for (const GameMaterial& m : materials.all()) {
            if (m.group == "Prizes" && lower(m.name) == stem) {
                material = &m;
            }
        }
        check(material != nullptr && material->brickScore > 0, stem + ": a material in the group Prizes");
        check(strings.find(info.caption) != nullptr, stem + ": the caption \"" + std::string(info.caption) + "\" is in languages.json");
        if (!info.icon.empty()) {
            check(!iconFile(assets / "Textures" / "Icons", std::string(info.icon)).empty(), stem + ": the icon " + std::string(info.icon));
        }

        // A catch in a real level pays what the prize's material says.
        SessionOptions options;
        options.rules.prizes.drops = false;
        options.physicsSettings.threads = 0;
        GameSession session(level, materials, options);
        for (int step = 0; step < 60 * 3 && session.phase() != SessionPhase::Ready; ++step) {
            session.update(1.0 / 60, {});
        }
        const std::int64_t before = session.score();
        session.dropPrize(info.kind, {session.bat().position.x, 0, session.bat().position.z + 1});
        for (int step = 0; step < 6; ++step) {
            session.update(1.0 / 60, {});
        }
        check(session.stats().prizesCaught == 1, stem + ": caught by the bat");
        const std::int64_t paid = session.score() - before;
        check(material != nullptr && paid == material->brickScore, stem + ": paid " + std::to_string(paid));
        if (info.kind == PrizeKind::Meteor) {
            check(paid == 4, "meteor: the prize is worth 4 and not the projectile's 250, paid " + std::to_string(paid));
        }
    }
}

int run(const fs::path& assets) {
    const MaterialLibrary materials = MaterialLibrary::load(assets / "Data" / "Materials" / "materials.json");
    const LevelCatalog catalog(assets / "Data" / "Levels");
    checkTable(assets, materials, loadLevelJson(catalog.levels().front().path));

    // The mean chance of a drop over every destroyable object of every level, by value.
    const PrizeRules rules;
    std::size_t objects = 0, byTier[5] = {};
    double chance = 0;
    for (const LevelEntry& entry : catalog.levels()) {
        const Level level = loadLevelJson(entry.path);
        for (const ActorRef& ref : flattenActors(level)) {
            if (ref.owner == nullptr || ref.owner->material.empty()) {
                continue;
            }
            const GameMaterial* material = materials.find(ref.owner->material);
            if (!canBeDamaged(ref.owner->type, material)) {
                continue;
            }
            ++objects;
            chance += dropChance(rules, material->brickScore);
            const auto tier = std::ranges::find_if(rules.tiers, [&](const PrizeRules::Tier& t) { return material->brickScore < t.below; });
            ++byTier[tier - rules.tiers.begin()];
        }
    }
    const double mean = objects > 0 ? chance / static_cast<double>(objects) : 0;
    std::printf("%zu destroyable objects in %zu levels: %zu below 80 points, %zu to 119, %zu to 199, %zu to 299, %zu of 300 and more\n",
                objects, catalog.levels().size(), byTier[0], byTier[1], byTier[2], byTier[3], byTier[4]);
    std::printf("mean chance that a destroyed object leaves a prize: %.2f %% (%.1f prizes a level if all are destroyed); the footage has 14.7 %%\n",
                100 * mean, chance / static_cast<double>(catalog.levels().size()));
    check(objects > 10000 && std::fabs(mean - 0.147) < 0.004, "the mean chance of a drop is 14.7 %");
    if (failures == 0) {
        std::puts("all prize checks passed");
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path assets = env;
    if (!fs::is_directory(assets / "Data" / "Levels") || !fs::is_regular_file(assets / "Data" / "Materials" / "materials.json") ||
        !fs::is_directory(assets / "Data" / "Objects" / "Prizes")) {
        std::printf("SKIP: converted levels, materials or prize definitions not found under %s\n", assets.string().c_str());
        return 77;
    }
    try {
        return run(assets);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
}
