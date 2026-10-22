// Plays levels of the real game data in $MB3_ASSETS on the GPU without a window: the game's PlaySession (the rules, the
// scene, the autopilot) and the pieces under it. Checks that a level builds and is drawn (the bat, the ball, the bricks), that
// a brick that is destroyed leaves the picture and one that is damaged shows its damage texture, that the picture fades in,
// and that a level the autopilot wins is reported as won once its fade out is over. Skips (exit code 77) without the data
// or a GPU.

#include "PlaySession.h"
#include "mb/engine/Image.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/PhysicsScene.h"
#include "mb/game/SceneBuilder.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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
std::size_t colours(const mb::Image& image) {
    std::set<std::uint32_t> seen;
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        seen.insert(static_cast<std::uint32_t>(image.rgba[i]) << 16 | static_cast<std::uint32_t>(image.rgba[i + 1]) << 8 |
                    image.rgba[i + 2]);
    }
    return seen.size();
}

void testScene(mb::Renderer& renderer, mb_app::Assets& assets) {
    // village-apple has crates (Box.jpg, four damage textures): a brick that shows damage.
    const mb::LevelEntry& entry = assets.catalog.levels()[*assets.catalog.find("village-apple")];
    const mb::Level level = mb::loadLevelJson(entry.path);
    mb::PhysicsScene physics(level, assets.materials);
    mb::ModelCache models(renderer, assets.root / "Data" / "Meshes");
    mb::LevelScene scene(models, assets.materials, assets.textures, level, physics.plan());

    // The draws of the level are the scene description's, one to one.
    const mb::ModelLookup lookup = [&](const std::string& file) -> const mb::Model* {
        const mb::ModelCache::Entry* found = models.model(file);
        return found ? &found->model : nullptr;
    };
    const mb::SceneDescription description = mb::describeScene(level, lookup, assets.materials, assets.textures);
    check(description.draws.size() == scene.items().size(), "scene: one draw item for each draw of the description");

    std::size_t crate = description.draws.size();
    for (std::size_t i = 0; i < description.draws.size(); ++i) {
        if (description.draws[i].damageTextures.size() == 4) {
            crate = i;
            break;
        }
    }
    check(crate < description.draws.size(), "scene: a crate has the four damage textures of Box.jpg");
    if (crate == description.draws.size()) {
        return;
    }
    const mb::ActorIndex actor = description.draws[crate].actor;
    const std::size_t draws = static_cast<std::size_t>(std::ranges::count(description.draws, actor, &mb::SceneDraw::actor));
    const mb::TextureHandle fresh = scene.items()[crate].material.texture;
    check(fresh == models.texture(description.draws[crate].damageTextures[3]), "scene: a fresh crate shows the last texture, the intact one");

    scene.setDamage(actor, 0);
    const mb::TextureHandle smashed = scene.items()[crate].material.texture;
    check(smashed == models.texture(description.draws[crate].damageTextures[0]) && smashed != fresh,
          "scene: damage state 0 retextures the crate with the first, the most damaged texture");
    scene.setDamage(actor, 2);
    check(scene.items()[crate].material.texture == models.texture(description.draws[crate].damageTextures[2]), "scene: and state 2 with the third");
    scene.setDamage(actor, 99);
    check(scene.items()[crate].material.texture == fresh, "scene: a state past the last is the last");
    scene.setDamage(mb::kNoActor, 0);  // nothing

    std::vector<mb::DrawItem> drawn;
    scene.appendDrawn(drawn);
    check(drawn.size() == scene.items().size(), "scene: nothing is hidden at first");
    scene.hide(actor);
    drawn.clear();
    scene.appendDrawn(drawn);
    check(drawn.size() + draws == scene.items().size(), "scene: a hidden actor's draws are left out, and only its");
    check(scene.items().size() == description.draws.size(), "scene: items() still has every draw");
    check(!scene.ballItems().empty(), "scene: the ball is ready to be placed");
}

void testPlaying(mb::Renderer& renderer, mb_app::Assets& assets) {
    const mb::SaveData save{fs::path()};
    mb_app::PlaySession session(renderer, assets, save, nullptr, {.autoplay = true});
    mb::GameHud hud;
    hud.lives = 2;
    std::vector<float> progress;

    check(!session.start("no-such-level", hud, [](float) {}), "play: a level that does not exist cannot be started");
    check(session.start("piratos-ostrovok", hud, [&](float fraction) { progress.push_back(fraction); }), "play: a level starts");
    check(!progress.empty() && std::is_sorted(progress.begin(), progress.end()) && progress.front() >= 0 && progress.back() <= 1,
          "play: and reports its progress, in order, between 0 and 1");
    check(session.darkness() == 1, "play: the picture fades in from the dark");
    check(session.rules() != nullptr && session.rules()->phase() == mb::SessionPhase::Entering, "play: the bat is swooping in");

    const auto picture = [&] {
        return renderer.renderToImage(1024, 768, session.frame(4.0f / 3.0f), session.items());
    };
    const mb::Image first = picture();
    check(!session.items().empty() && colours(first) > 500, "play: the level is drawn");

    mb::InputEvents nobody;
    mb::LevelOutcome outcome = mb::LevelOutcome::Playing;
    for (int i = 0; i < 30 && outcome == mb::LevelOutcome::Playing; ++i) {
        outcome = session.update(nobody, kFrame, hud);
    }
    check(session.darkness() > 0 && session.darkness() < 1, "play: half a second in it is still coming up");
    for (int i = 0; i < 60; ++i) {
        session.update(nobody, kFrame, hud);
    }
    check(session.darkness() == 0, "play: and then it is clear");
    check(session.rules()->balls().size() == 1 && !session.rules()->balls()[0].held, "play: the autopilot launched the ball");

    // The autopilot destroys the first bricks within 3 seconds: they leave the picture and their points are in the HUD.
    for (int i = 0; i < 4 * 60; ++i) {
        session.update(nobody, kFrame, hud);
    }
    check(hud.score > 0 && hud.score == session.rules()->score() && hud.lives == session.rules()->hearts(),
          "play: the HUD has the points and the hearts of the rules");
    check(session.rules()->stats().bricksDestroyed > 0, "play: bricks were destroyed");
    check(colours(picture()) > 500, "play: and the picture is still a level");
}

void testWinning(mb::Renderer& renderer, mb_app::Assets& assets) {
    const mb::SaveData save{fs::path()};
    mb_app::PlaySession session(renderer, assets, save, nullptr, {.autoplay = true});
    mb::GameHud hud;
    hud.lives = 2;
    hud.score = 1000;  // brought from the level before
    check(session.start("piratos-postroika", hud, [](float) {}), "win: a level starts");

    mb::InputEvents nobody;
    mb::LevelOutcome outcome = mb::LevelOutcome::Playing;
    double seconds = 0, completeAt = -1;
    while (outcome == mb::LevelOutcome::Playing && seconds < 400) {
        outcome = session.update(nobody, kFrame, hud);
        seconds += kFrame;
        if (completeAt < 0 && session.rules()->phase() == mb::SessionPhase::Complete) {
            completeAt = seconds;
        }
    }
    check(outcome == mb::LevelOutcome::Complete, "win: the autopilot wins the level");
    check(completeAt > 0 && seconds - completeAt > 1.0 && seconds - completeAt < 1.6,
          "win: and it is reported a second or so after the last brick (a moment's hold, then the fade out)");
    check(session.darkness() == 1, "win: the picture is dark by then");
    check(hud.score > 1000 && hud.lives >= 0 && hud.lives <= 2, "win: the points were added to those brought in, and the hearts are the rules'");
    check(session.rules()->targetsLeft(session.rules()->levelEnd()) == 0, "win: no target is left");
}

}  // namespace

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    const fs::path root = env;
    if (!fs::is_directory(root / "Data" / "Levels") || !fs::is_regular_file(root / "Data" / "Materials" / "materials.json")) {
        std::printf("SKIP: converted data not found under %s\n", root.string().c_str());
        return 77;
    }
    std::unique_ptr<mb::Renderer> renderer;
    try {
        renderer = std::make_unique<mb::Renderer>();
    } catch (const std::exception& e) {
        std::printf("SKIP: no GPU (%s)\n", e.what());
        return 77;
    }
    try {
        mb_app::Assets assets;
        testScene(*renderer, assets);
        testPlaying(*renderer, assets);
        testWinning(*renderer, assets);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("levels played");
    }
    return failures == 0 ? 0 : 1;
}
