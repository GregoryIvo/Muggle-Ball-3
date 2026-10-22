// Plays levels of the real game data in $MB3_ASSETS on the GPU without a window: the game's PlaySession (the rules, the
// scene, the autopilot) and the pieces under it. Checks that a level builds and is drawn (the bat, the ball, the bricks), that
// a brick that is destroyed leaves the picture and one that is damaged shows its damage texture, that the picture fades in,
// that a person who is destroyed shrinks away before the level ends, that a level the autopilot wins holds for 0.1 s after
// its last required object is gone and is reported as won once its fade out is over, and that the click that ends a pause
// does not launch the ball. Skips (exit code 77) without the data or a GPU.

#include "PlaySession.h"
#include "mb/engine/Image.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/PhysicsScene.h"
#include "mb/game/SceneBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
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
    // A shrinking actor is drawn smaller about its point, all of its draws, and whole again at scale 1.
    const mb::Vec3 about{3, 1, 2};
    scene.setScale(actor, 0.5f, about);
    drawn.clear();
    scene.appendDrawn(drawn);
    bool halved = drawn.size() == scene.items().size();
    for (std::size_t i = 0; i < drawn.size() && halved; ++i) {
        const mb::DrawItem& was = scene.items()[i];
        const bool theirs = description.draws[i].actor == actor;
        // A point of the model stands halfway to `about` from where it was.
        const mb::Vec3 before = mb::transformPoint({0, 0, 0}, was.world), after = mb::transformPoint({0, 0, 0}, drawn[i].world);
        const mb::Vec3 expected = theirs ? about + (before - about) * 0.5f : before;
        halved = std::fabs(after.x - expected.x) < 1e-3f && std::fabs(after.y - expected.y) < 1e-3f &&
                 std::fabs(after.z - expected.z) < 1e-3f;
    }
    check(halved, "scene: setScale draws an actor's draws at half the size about a point, and nothing else");
    scene.setScale(actor, 1, about);
    drawn.clear();
    scene.appendDrawn(drawn);
    check(drawn.size() == scene.items().size() && drawn[crate].world.m[3][0] == scene.items()[crate].world.m[3][0] &&
              drawn[crate].world.m[0][0] == scene.items()[crate].world.m[0][0],
          "scene: scale 1 is whole again");
    scene.setScale(mb::kNoActor, 0.5f, about);  // nothing

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

// The autopilot wins a level. It loses balls like any player and how long it takes depends on every bounce, which differ with
// the compiler's flags (a build with -O1 and the address sanitizer plays another game than the Release one), so the level
// is the quickest of the real ones, and the game cannot end for want of hearts or of time: what is checked is what the
// session does around the win.
void testWinning(mb::Renderer& renderer, mb_app::Assets& assets) {
    const mb::SaveData save{fs::path()};
    mb_app::PlaySession session(renderer, assets, save, nullptr, {.autoplay = true});
    mb::GameHud hud;
    hud.lives = 99;
    hud.score = 1000;  // brought from the level before
    check(session.start("piratos-postroika", hud, [](float) {}), "win: a level starts");
    check(!session.ending(), "win: and is not ending");

    mb::InputEvents nobody;
    mb::LevelOutcome outcome = mb::LevelOutcome::Playing;
    double seconds = 0, completeAt = -1, darkAt = -1;
    bool byStar = false;
    while (outcome == mb::LevelOutcome::Playing && seconds < 1500) {
        outcome = session.update(nobody, kFrame, hud);
        seconds += kFrame;
        if (completeAt < 0 && session.rules()->phase() == mb::SessionPhase::Complete) {
            completeAt = seconds;
            byStar = hud.caption == "NextLevel";  // the Next Level star ends a level with targets left
            check(session.ending(), "win: the session is ending as soon as the level is won (the last target is gone, or the star was caught)");
        } else if (completeAt >= 0 && darkAt < 0 && session.darkness() > 0) {
            darkAt = seconds;
        }
    }
    check(outcome == mb::LevelOutcome::Complete, "win: the autopilot wins the level");
    check(completeAt > 0 && darkAt - completeAt > 0.09 && darkAt - completeAt < 0.15,
          "win: the picture holds for 0.1 s after the last target is gone, and then goes dark");
    check(completeAt > 0 && seconds - completeAt > 0.85 && seconds - completeAt < 0.95,
          "win: and it is reported 0.9 s after the last target (the hold, then the fade out)");
    check(session.darkness() == 1, "win: the picture is dark by then");
    check(hud.score > 1000 && hud.lives >= 0 && hud.lives <= 99, "win: the points were added to those brought in, and the hearts are the rules'");
    check(byStar || session.rules()->targetsLeft(session.rules()->levelEnd()) == 0, "win: no target is left, or the star was caught");
}

// The click that ends a pause is not the level's: the front end does not give the session its frame, and tells it the pause is
// over (resume()), so a button that is down then launches nothing, while a press after it does.
void testResume(mb::Renderer& renderer, mb_app::Assets& assets) {
    const mb::SaveData save{fs::path()};
    mb::GameHud hud;
    hud.lives = 2;
    const auto launched = [&](bool afterPause) {
        mb_app::PlaySession session(renderer, assets, save, nullptr, {});
        session.start("piratos-ostrovok", hud, [](float) {});
        mb::InputEvents nobody;
        for (int i = 0; i < 60; ++i) {  // the bat is in and the ball waits for the click
            session.update(nobody, kFrame, hud);
        }
        check(session.rules()->phase() == mb::SessionPhase::Ready, "resume: the ball waits on the bat");
        if (afterPause) {
            session.resume();
        }
        mb::InputEvents down;
        down.leftButton = true;
        down.pointer = {{500, 400, true}};
        session.update(down, kFrame, hud);
        session.update(down, kFrame, hud);
        const bool launchedByHeldButton = !session.rules()->balls()[0].held;
        mb::InputEvents up;
        up.pointer = {{500, 400, false}};
        session.update(up, kFrame, hud);
        session.update(down, kFrame, hud);
        return std::pair{launchedByHeldButton, !session.rules()->balls()[0].held};
    };
    const auto [heldBefore, pressBefore] = launched(false);
    check(heldBefore && pressBefore, "resume: without a pause a button going down launches the ball");
    const auto [heldAfter, pressAfter] = launched(true);
    check(!heldAfter && pressAfter, "resume: a button that is down as the pause ends launches nothing, and the next press does");
}

// The prizes on screen: a level played by the autopilot, the same every time, with a prize given by the harness (PrizeCue). A prize that
// falls is drawn with its halo; the effects change the picture (the Wall's rope, the Magnet's horseshoe, the night's light); the
// caption and the icons are in the HUD, and the caption goes away.
void testPrizes(mb::Renderer& renderer, mb_app::Assets& assets) {
    const mb::SaveData save{fs::path()};
    struct Run {
        std::unique_ptr<mb_app::PlaySession> session;
        mb::GameHud hud;
        void play(double seconds) {
            const mb::InputEvents nobody;
            for (int i = 0; i < static_cast<int>(std::lround(seconds / kFrame)); ++i) {
                session->update(nobody, kFrame, hud);
            }
        }
    };
    const auto start = [&](std::vector<mb_app::PrizeCue> cues) {
        Run run;
        run.session = std::make_unique<mb_app::PlaySession>(renderer, assets, save, nullptr,
                                                            mb_app::PlayOptions{.autoplay = true, .prizes = std::move(cues)});
        run.hud.lives = 2;
        run.session->start("piratos-ostrovok", run.hud, [](float) {});
        return run;
    };

    Run plain = start({});
    plain.play(2.0);
    const std::size_t plainItems = plain.session->items().size();
    check(plain.hud.powerUps.empty() && plain.hud.caption.empty() && plain.hud.captionAlpha == 0, "prizes: nothing on the HUD without one");
    const mb::DirectionalLight day = plain.session->frame(4.0f / 3.0f).light;

    Run falling = start({{mb::PrizeKind::Bonus, 1.0, false, std::nullopt}});
    falling.play(2.0);
    check(falling.session->rules()->fallingPrizes().size() == 1, "prizes: the prize the harness drops is falling");
    check(falling.session->items().size() >= plainItems + 3, "prizes: it is drawn, its model and its halo of two sprites");
    check(falling.hud.caption.empty(), "prizes: it has no caption until it is caught");

    Run wall = start({{mb::PrizeKind::Wall, 0.9, true, std::nullopt}});
    wall.play(2.0);
    check(wall.hud.caption == "Wall" && wall.hud.captionAlpha > 0.5f, "prizes: a catch puts the prize's caption on the HUD, strong at first");
    check(wall.hud.powerUps.size() == 1 && wall.hud.powerUps[0].icon == "wall" && wall.hud.powerUps[0].number >= 28 && wall.hud.powerUps[0].number <= 30,
          "prizes: and its icon, with the seconds it has left");
    check(wall.session->items().size() > plainItems, "prizes: the Wall's rope is drawn");
    wall.play(0.8);
    check(wall.hud.caption == "Wall" && wall.hud.captionAlpha < 0.5f && wall.hud.captionAlpha > 0, "prizes: the caption fades");
    wall.play(0.6);
    check(wall.hud.caption.empty() && wall.hud.captionAlpha == 0, "prizes: and is gone after about 2.2 s");
    check(wall.hud.powerUps.size() == 1 && wall.hud.powerUps[0].number >= 26 && wall.hud.powerUps[0].number <= 28, "prizes: while the icon counts down");

    Run magnet = start({{mb::PrizeKind::Magnet, 0.9, true, std::nullopt}});
    magnet.play(2.0);
    check(magnet.session->items().size() > plainItems, "prizes: the Magnet's horseshoe is on the bat");

    Run night = start({{mb::PrizeKind::Night, 0.9, true, std::nullopt}});
    night.play(3.0);
    const mb::DirectionalLight dark = night.session->frame(4.0f / 3.0f).light;
    check(dark.ambient.r < day.ambient.r * 0.45f && dark.diffuse.r < day.diffuse.r * 0.45f, "prizes: the Night dims the scene's light to 0.38");
    check(colours(renderer.renderToImage(1024, 768, night.session->frame(4.0f / 3.0f), night.session->items())) > 300, "prizes: and the board is still in sight");

    Run big = start({{mb::PrizeKind::BigBall, 0.9, true, std::nullopt}, {mb::PrizeKind::PowerBall, 0.9, true, std::nullopt}});
    big.play(2.0);
    check(big.hud.powerUps.size() == 2 && big.hud.powerUps[0].icon == "big" && big.hud.powerUps[1].icon == "Powerful",
          "prizes: two effects, two icons, the one caught first on top");
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
        testResume(*renderer, assets);
        testPrizes(*renderer, assets);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("levels played");
    }
    return failures == 0 ? 0 : 1;
}
