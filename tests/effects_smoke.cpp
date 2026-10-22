// The particle effects in the game's scene, on the real data in $MB3_ASSETS and the GPU without a window (skips with exit code 77 without
// either): events the rules report become the effects of docs/GAMEPLAY.md section 19, in the one particle system of the PlayScene, and
// reach the renderer as a few batches.
//
//   - a flicker burns (its sparks and smoke, the files the level names), and goes out with it;
//   - a charge of dynamite goes off in the explosion its level names, at that file's size; a cannonball and a rocket's burst likewise;
//   - a brick that is destroyed gives the flash of its material's exploseType, and a person none; a tree that is hit sheds its leaves;
//   - the night dims the particles with the rest of what has no light of its own;
//   - the autopilot lights a firework crate: its rocket leaves a trail and bursts into sparks.

#include "PlaySession.h"
#include "mb/engine/Image.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/PlayScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr double kFrame = 1.0 / 60.0;

// How many different colours a picture has: a picture of a level is far from flat.
std::size_t colours(const Image& image) {
    std::set<std::uint32_t> seen;
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        seen.insert(static_cast<std::uint32_t>(image.rgba[i]) << 16 | static_cast<std::uint32_t>(image.rgba[i + 1]) << 8 | image.rgba[i + 2]);
    }
    return seen.size();
}
const ParticleView kView = particleViewOf(Mat4{});  // right x, up y, forward z: only the counts and sizes are looked at

// The particles of a scene by the file they come from.
std::map<std::string, int> filesOf(PlayScene& scene) {
    std::vector<ParticleQuad> quads;
    scene.particles().appendQuads(kView, quads);
    std::map<std::string, int> files;
    for (const ParticleQuad& quad : quads) {
        ++files[quad.def->file];
    }
    return files;
}

// A level of the real data in a session and its picture.
struct Stage {
    Level level;
    std::unique_ptr<GameSession> session;
    std::unique_ptr<ModelCache> models;
    std::unique_ptr<PlayScene> scene;

    Stage(Renderer& renderer, mb_app::Assets& assets, const std::string& name) {
        const LevelEntry& entry = assets.catalog.levels()[*assets.catalog.find(name)];
        level = loadLevelJson(entry.path);
        session = std::make_unique<GameSession>(level, assets.materials, SessionOptions{});
        models = std::make_unique<ModelCache>(renderer, assets.root / "Data" / "Meshes");
        scene = std::make_unique<PlayScene>(*models, assets.materials, assets.textures, assets.particles, level, *session, 0);
    }
    // The picture goes on by `seconds`, with these events (the session does not step: the rules are not what is looked at).
    void play(double seconds, std::span<const GameEvent> events = {}) {
        scene->update(*session, events, std::min(seconds, kFrame));
        for (double left = seconds - kFrame; left > 1e-9; left -= kFrame) {
            scene->update(*session, {}, std::min(kFrame, left));
        }
    }
    // The first actor of an object type, and the object.
    std::optional<ActorIndex> first(ObjectType type, const std::string& target = {}) const {
        for (const ActorRef& ref : flattenActors(level)) {
            if (ref.owner && ref.owner->type == type && (target.empty() || ref.owner->target == target)) {
                return ref.index;
            }
        }
        return std::nullopt;
    }
};

GameEvent event(EventType type, Vec3 at) {
    GameEvent e;
    e.type = type;
    e.position = at;
    return e;
}

std::size_t vertices(std::span<const EffectBatch> batches) {
    std::size_t count = 0;
    for (const EffectBatch& batch : batches) {
        count += batch.vertices.size();
    }
    return count;
}

void testFires(Renderer& renderer, mb_app::Assets& assets) {
    Stage stage(renderer, assets, "knight-fruktovie-derevia");  // the cauldron
    stage.play(2.0);
    const std::map<std::string, int> files = filesOf(*stage.scene);
    check(files.count("SparkKoster") && files.at("SparkKoster") >= 8 && files.count("Smoke") && files.at("Smoke") >= 40,
          "fires: the cauldron burns the sparks and the smoke its level names");
    check(!stage.scene->effects().empty() && vertices(stage.scene->effects()) >= 6 * 48, "fires: and they reach the renderer as batches");
    check(stage.scene->effects().size() <= 4, "fires: a handful of them, not one for each particle: " + std::to_string(stage.scene->effects().size()));

    // The torches of village-labirint name the other sparks, and burn from the top of their posts.
    Stage torches(renderer, assets, "village-labirint");
    torches.play(2.0);
    const std::map<std::string, int> torchFiles = filesOf(*torches.scene);
    check(torchFiles.count("Spark") == 1 && torchFiles.at("Spark") >= 8 && torchFiles.count("Smoke") == 1, "fires: a torch burns Spark and Smoke");
    std::vector<ParticleQuad> quads;
    torches.scene->particles().appendQuads(kView, quads);
    float lowest = 1e9f;
    for (const ParticleQuad& q : quads) {
        if (q.def->file == "Spark") {
            lowest = std::min(lowest, q.centre.y);
        }
    }
    check(lowest > 3.5f, "fires: from the top of the torch, 4.4 up: " + std::to_string(lowest));
}

void testBlasts(Renderer& renderer, mb_app::Assets& assets) {
    for (const std::string target : {"explosebig", "explosesmall"}) {
        const std::string level = target == "explosebig" ? "knight-fruktovie-derevia" : "cowboys-priehali";
        Stage stage(renderer, assets, level);
        const std::optional<ActorIndex> charge = stage.first(ObjectType::Dynamite, target);
        check(charge.has_value(), target + ": " + level + " has a charge that names it");
        if (!charge) {
            continue;
        }
        const std::size_t before = filesOf(*stage.scene).count(target) ? 1 : 0;
        GameEvent blast = event(EventType::DynamiteBlast, {3, 1.3f, -2});
        blast.actor = *charge;
        blast.speed = target == "explosebig" ? 5.0f : 3.0f;
        stage.play(0.2, {&blast, 1});
        std::vector<ParticleQuad> quads;
        stage.scene->particles().appendQuads(kView, quads);
        const auto flash = std::ranges::find_if(quads, [&](const ParticleQuad& q) { return q.def->file == target; });
        check(before == 0 && flash != quads.end(), target + ": a charge goes off in the explosion its level names");
        check(flash != quads.end() && std::fabs(length(flash->halfRight) * 2 - (target == "explosebig" ? 25.0f : 15.0f)) < 0.05f,
              target + ": at the size the file gives (25 for the radius 5, 15 for 3)");
        stage.play(0.4);
        check(filesOf(*stage.scene).empty() || !filesOf(*stage.scene).count(target), target + ": and it fades out in half a second");
    }

    Stage stage(renderer, assets, "knight-fruktovie-derevia");
    stage.play(0.1);
    const GameEvent shot = event(EventType::ShotHit, {0, 1, 4});
    stage.play(0.1, {&shot, 1});
    check(filesOf(*stage.scene).count("exploseyadro") == 1, "cannonball: it bursts in exploseyadro's flash");
    const GameEvent burst = event(EventType::RocketBurst, {-3, 20, 1});
    stage.play(0.1, {&burst, 1});
    check(filesOf(*stage.scene).count("Fireworks") == 1 && filesOf(*stage.scene)["Fireworks"] == 30, "rocket: its burst is the 30 sparks of one of the Fireworks");
}

void testBricks(Renderer& renderer, mb_app::Assets& assets) {
    Stage stage(renderer, assets, "piratos-ostrovok");
    const std::optional<ActorIndex> brick = stage.first(ObjectType::Brick);
    check(brick.has_value(), "bricks: the level has one");
    if (!brick) {
        return;
    }
    const auto destroyed = [&](const char* material, Vec3 at) {
        GameEvent e = event(EventType::BrickDestroyed, at);
        e.actor = *brick;
        e.material = assets.materials.find(material);
        return e;
    };
    const struct {
        const char* material;
        const char* file;  // empty: none
    } cases[] = {{"shark.jpg", "exploseball"}, {"plitka.jpg", "explosesmall"}, {"Pirat1.jpg", ""}, {"Dinamit.jpg", ""}};
    for (const auto& c : cases) {
        const GameMaterial* material = assets.materials.find(c.material);
        check(material != nullptr, std::string("bricks: the material ") + c.material + " exists");
        Stage fresh(renderer, assets, "piratos-ostrovok");
        const GameEvent e = destroyed(c.material, {2, 1, 3});
        fresh.play(0.1, {&e, 1});
        const std::map<std::string, int> files = filesOf(*fresh.scene);
        const std::string what = std::string("bricks: ") + c.material + " (exploseType " + std::to_string(material ? material->exploseType : -1) + ")";
        if (std::string(c.file).empty()) {
            check(files.empty(), what + " leaves no flash");
        } else {
            check(files.size() == 1 && files.count(c.file) == 1, what + " goes off in " + c.file);
        }
    }
    // A brick with no material (the event says none) is no trouble.
    GameEvent bare = event(EventType::BrickDestroyed, {1, 1, 1});
    bare.actor = *brick;
    stage.play(0.1, {&bare, 1});
    check(filesOf(*stage.scene).empty(), "bricks: one with no material leaves no flash");
}

void testTrees(Renderer& renderer, mb_app::Assets& assets) {
    Stage stage(renderer, assets, "knight-fruktovie-derevia");
    const std::optional<ActorIndex> tree = stage.first(ObjectType::Tree);
    check(tree.has_value(), "trees: the level has one");
    if (!tree) {
        return;
    }
    GameEvent hit = event(EventType::BrickHit, {0, 0.7f, 0});
    hit.actor = *tree;
    stage.play(0.1, {&hit, 1});
    check(filesOf(*stage.scene).count("LeafFall") == 1 && filesOf(*stage.scene)["LeafFall"] == 15, "trees: a tree that is hit sheds its 15 leaves");
    std::vector<ParticleQuad> quads;
    stage.scene->particles().appendQuads(kView, quads);
    const Vec3 trunk = stage.session->centreOf(*tree);
    bool above = true;
    for (const ParticleQuad& q : quads) {
        if (q.def->file == "LeafFall") {
            above = above && q.centre.y > trunk.y + 1.0f && std::fabs(q.centre.x - trunk.x) < 2.5f;
        }
    }
    check(above, "trees: from the crown, above the middle of the trunk");
    stage.play(5.0);
    check(filesOf(*stage.scene).count("LeafFall") == 0, "trees: and they are gone when they have fallen");
}

void testNight(Renderer& renderer, mb_app::Assets& assets) {
    Stage stage(renderer, assets, "piratos-ostrovok");
    const GameEvent shot = event(EventType::ShotHit, {0, 1, 4});
    stage.play(0.05, {&shot, 1});
    check(!stage.scene->effects().empty(), "night: a flash is drawn by day");
    const float day = stage.scene->effects()[0].vertices[0].color.r;
    stage.session->givePrize(PrizeKind::Night);
    for (int i = 0; i < 90; ++i) {
        stage.session->update(kFrame, {});
    }
    stage.play(0.001, {});
    const GameEvent again = event(EventType::ShotHit, {0, 1, 4});
    stage.play(0.05, {&again, 1});
    check(stage.session->nightLevel() > 0.99f, "night: it is dark");
    float night = 1;
    for (const EffectBatch& batch : stage.scene->effects()) {
        night = std::min(night, batch.vertices[0].color.r);
    }
    check(day > 0.5f && night < day * 0.55f && night > day * 0.4f, "night: and the flash is dimmed to 0.49 of itself with the rest of what has no light: " + std::to_string(day) + " to " + std::to_string(night));
}

// The autopilot touches a firework crate: its rocket rises with a trail, and bursts.
void testRocket(Renderer& renderer, mb_app::Assets& assets) {
    const SaveData save{fs::path()};
    mb_app::PlaySession session(renderer, assets, save, nullptr, {.autoplay = true});
    GameHud hud;
    hud.lives = 99;
    hud.level = 11;
    check(session.start("piratos-po-krugy", hud, [](float) {}), "rocket: the level starts");
    const InputEvents nobody;
    double seconds = 0;
    while (session.rules()->objects().rockets().empty() && seconds < 300) {
        session.update(nobody, kFrame, hud);
        seconds += kFrame;
    }
    check(!session.rules()->objects().rockets().empty(), "rocket: the autopilot lights the crate");
    for (int i = 0; i < 20; ++i) {
        session.update(nobody, kFrame, hud);
    }
    bool trail = false;
    for (const EffectBatch& batch : session.effects()) {
        trail = trail || (batch.colourOnly && batch.blend == BlendMode::Additive && batch.vertices.size() >= 6 * 8);
    }
    check(trail, "rocket: it leaves a trail of glowing sprites in the colours of a flame");
    check(colours(renderer.renderToImage(1024, 768, session.frame(4.0f / 3.0f), session.items(), {}, {}, session.effects())) > 500, "rocket: the picture with the particles is a level");
    while (!session.rules()->objects().rockets().empty() && seconds < 400) {
        session.update(nobody, kFrame, hud);
        seconds += kFrame;
    }
    session.update(nobody, kFrame, hud);
    check(vertices(session.effects()) >= 6 * 30, "rocket: it bursts in sparks: " + std::to_string(vertices(session.effects()) / 6) + " quads");
    for (int i = 0; i < 90; ++i) {
        session.update(nobody, kFrame, hud);
    }
    check(vertices(session.effects()) == 0, "rocket: and the trail and the burst fade out");
}

}  // namespace

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path root = env;
    if (!fs::is_directory(root / "Data" / "Levels") || !fs::is_directory(root / "Data" / "Particles")) {
        std::printf("SKIP: converted data not found under %s\n", root.string().c_str());
        return 77;
    }
    std::unique_ptr<Renderer> renderer;
    try {
        renderer = std::make_unique<Renderer>();
    } catch (const std::exception& e) {
        std::printf("SKIP: no GPU (%s)\n", e.what());
        return 77;
    }
    try {
        mb_app::Assets assets;
        testFires(*renderer, assets);
        testBlasts(*renderer, assets);
        testBricks(*renderer, assets);
        testTrees(*renderer, assets);
        testNight(*renderer, assets);
        testRocket(*renderer, assets);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("effects played");
    }
    return failures == 0 ? 0 : 1;
}
