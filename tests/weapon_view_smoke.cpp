// The weapons' picture on the real data in $MB3_ASSETS and the GPU without a window (skips with exit code 77 without either): the barrels
// of bat.x that grow, recoil and shrink in the model's own "Common" animation, and what flies and strikes, in three levels of three
// worlds (docs/GAMEPLAY.md section 18.11 and 18.12).
//
//   - the model has the animations the converter writes (bat.x's four sets and the "Anim-1" turn of each projectile's model); a bat.glb
//     converted before that has no "Common" and the barrel checks are skipped with a note, the rest is run;
//   - each weapon's barrel starts at a tenth of its size (a seventh for the bazooka) and is full size when it is ready, its gun recoils on a shot and settles where it
//     was, and it shrinks away when the weapon ends;
//   - a weapon the session gives grows its barrel, a shot makes it recoil, the last shot ends it in a shrinking, and another weapon caught
//     takes the barrel over;
//   - every projectile in the air is a model in the scene and gone from it when it has hit; a rocket and a meteor leave their trails, the
//     cannonball and the rocket burst in their flash, a laser shot is a beam, a bolt is lightning that is gone in a fraction of a second, a
//     meteor ends in the ring on the water, and every effect it asked for is in the particle files.

#include "Assets.h"
#include "mb/game/Autopilot.h"
#include "mb/game/BatBarrels.h"
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
const ParticleView kView = particleViewOf(Mat4{});

// A level of the real data in a session, its picture, and a second set of the bat's barrels fed the same events.
struct Stage {
    Level level;
    std::unique_ptr<GameSession> session;
    std::unique_ptr<ModelCache> models;
    std::unique_ptr<PlayScene> scene;
    std::unique_ptr<BatBarrels> barrels;
    Autopilot pilot;
    std::map<std::string, int> seen;  // the most quads of a particle file at once, over the run
    std::size_t baseline = 0;         // the draws of the scene with nothing in the air
    std::size_t inTheAir = 0;         // the most projectiles at once
    std::size_t mostItems = 0;
    std::set<EventType> events;

    Stage(Renderer& renderer, mb_app::Assets& assets, const std::string& name) {
        const LevelEntry& entry = assets.catalog.levels()[*assets.catalog.find(name)];
        level = loadLevelJson(entry.path);
        SessionOptions options;
        options.hearts = 99;
        session = std::make_unique<GameSession>(level, assets.materials, options);
        models = std::make_unique<ModelCache>(renderer, assets.root / "Data" / "Meshes");
        scene = std::make_unique<PlayScene>(*models, assets.materials, assets.textures, assets.particles, level, *session, 0);
        barrels = std::make_unique<BatBarrels>(*models, assets.materials, assets.textures, "Metall-Raketka-Red.jpg");
        for (int i = 0; i < 600 && session->phase() != SessionPhase::Playing; ++i) {
            frame(true);
        }
    }

    // One frame: the session steps and the pictures follow. `fire`: the click is down.
    void frame(bool fire = false) {
        GameInput input = pilot.decide(*session);
        input.launch = fire;  // held down: the ball goes at the first click and a shot leaves every shot interval after it
        session->update(kFrame, input);
        const std::vector<GameEvent> happened = session->takeEvents();
        for (const GameEvent& e : happened) {
            events.insert(e.type);
        }
        scene->update(*session, happened, kFrame);
        barrels->update(*session, happened, kFrame);
        std::vector<ParticleQuad> quads;
        scene->particles().appendQuads(kView, quads);
        std::map<std::string, int> now;
        for (const ParticleQuad& q : quads) {
            ++now[q.def->file];
        }
        for (const auto& [file, count] : now) {
            seen[file] = std::max(seen[file], count);
        }
        inTheAir = std::max(inTheAir, session->projectiles().size());
        mostItems = std::max(mostItems, scene->items().size());
    }
    void run(double seconds, bool fire = false) {
        for (double left = seconds; left > 1e-9; left -= kFrame) {
            frame(fire);
        }
    }
};

// The size of a node of the bat at `time` seconds into the take: the length of the first row of its global pose.
float sizeOf(const Model& bat, const ModelAnimation& take, const char* node, double time) {
    const Mat4 pose = bat.globalPoses(take, static_cast<float>(time))[static_cast<std::size_t>(bat.findNode(node))];
    return length({pose.m[0][0], pose.m[0][1], pose.m[0][2]});
}

Vec3 placeOf(const Model& bat, const ModelAnimation& take, const char* node, double time) {
    return bat.globalPoses(take, static_cast<float>(time))[static_cast<std::size_t>(bat.findNode(node))].translationPart();
}

// ---- The models carry the animations the pictures need ------------------------------------------------------------------------

void testAnimations(ModelCache& cache) {
    const ModelCache::Entry* bat = cache.model("bat.glb");
    check(bat != nullptr, "animations: bat.glb loads");
    if (bat == nullptr) {
        return;
    }
    const bool converted = bat->model.findAnimation("Common") != nullptr;
    if (!converted) {
        std::puts("NOTE: bat.glb has no animations (converted before the converter wrote them): the barrels are not drawn, and not tested");
    } else {
        check(bat->model.animations.size() == 4 && bat->model.findAnimation("Hit") && bat->model.findAnimation("Right") && bat->model.findAnimation("Left"),
              "animations: bat.x's four takes");
        const ModelAnimation& take = *bat->model.findAnimation("Common");
        check(take.duration > 3.4f && take.duration < 4.1f, "animations: 'Common' is a take of 3.5 to 4 seconds: " + std::to_string(take.duration));
        const struct {
            WeaponType type;
            const char* body;   // the frame that grows
            const char* barrel;  // and the one that pushes out and recoils
        } weapons[] = {{WeaponType::Gun, "Pulemet-Right", "Pulemet-Stvol-Right"}, {WeaponType::Gun, "Pulemet-Left", "Pulemet-Stvol-Left"},
                       {WeaponType::Cannon, "Pushka", "Pushka"}, {WeaponType::Bazooka, "Bazuka-Korpus", "Bazuka-Stvol"},
                       {WeaponType::Laser, "lazer-Korpus", "Lazer-Stvol"}};
        for (const auto& w : weapons) {
            const BarrelClip clip = barrelClip(w.type);
            const float small = sizeOf(bat->model, take, w.body, 0), full = sizeOf(bat->model, take, w.body, clip.grown);
            check(full / small > 6.5f && full / small < 12, std::string("animations: ") + w.body + " grows from a tenth (a seventh, the bazooka's) of its size to full: " + std::to_string(small) + " to " + std::to_string(full));
            check(std::fabs(full - sizeOf(bat->model, take, w.body, clip.recoilFrom)) < 0.02f * full,
                  std::string("animations: ") + w.body + " stands as it is until its recoil");
            const Vec3 ready = placeOf(bat->model, take, w.barrel, clip.grown);
            float back = 0;
            for (double t = clip.recoilFrom; t <= clip.recoilTo; t += 0.02) {
                back = std::max(back, length(placeOf(bat->model, take, w.barrel, t) - ready));
            }
            check(back > 0.4f, std::string("animations: ") + w.barrel + " recoils by " + std::to_string(back) + " units");
            check(length(placeOf(bat->model, take, w.barrel, clip.recoilTo) - ready) < 0.05f, std::string("animations: ") + w.barrel + " settles where it was");
            check(sizeOf(bat->model, take, w.body, clip.shrinkTo) / full < 0.2f, std::string("animations: ") + w.body + " is small again at the end of the take");
            check(length(placeOf(bat->model, take, w.barrel, clip.shrinkTo) - ready) > 0.3f || w.type == WeaponType::Cannon,
                  std::string("animations: ") + w.barrel + " has drawn back at the end");
        }
    }
    for (const char* file : {"bat-bullets.glb", "kernel.glb", "bat-rocket.glb", "meteor.glb"}) {
        const ModelCache::Entry* entry = cache.model(file);
        check(entry != nullptr, std::string("animations: ") + file + " loads");
        if (entry != nullptr && converted) {
            const ModelAnimation* turn = entry->model.findAnimation("Anim-1");
            check(turn != nullptr && turn->duration > 1 && turn->duration < 3, std::string("animations: ") + file + " turns in 'Anim-1', once in a second or two");
        }
    }
}

// ---- The barrels follow the weapons -----------------------------------------------------------------------------------------

void testBarrels(Renderer& renderer, mb_app::Assets& assets) {
    Stage stage(renderer, assets, "piratos-ostrovok");
    testAnimations(*stage.models);
    const BatBarrels& barrels = *stage.barrels;
    check(barrels.phase(WeaponType::Gun) == BarrelPhase::Hidden && barrels.phase(WeaponType::Laser) == BarrelPhase::Hidden, "barrels: none shows with no weapon");
    std::vector<DrawItem> items;
    barrels.append({}, items);
    check(items.empty(), "barrels: and none is drawn");
    if (!barrels.animated()) {
        return;
    }

    stage.session->givePrize(PrizeKind::Cannon);
    stage.frame();
    stage.frame();
    check(barrels.phase(WeaponType::Cannon) == BarrelPhase::Growing && barrels.phase(WeaponType::Gun) == BarrelPhase::Hidden, "barrels: the cannon that is given grows its barrel");
    items.clear();
    barrels.append({}, items);
    const std::size_t growing = items.size();
    check(growing >= 1, "barrels: and it is drawn");
    stage.run(0.3);
    check(barrels.phase(WeaponType::Cannon) == BarrelPhase::Growing && barrels.time(WeaponType::Cannon) > 0.25, "barrels: it takes the take's time to grow");
    stage.run(1.0);
    check(barrels.phase(WeaponType::Cannon) == BarrelPhase::Ready, "barrels: and is ready after 0.84 s");
    // A weapon caught while another is held takes the barrel over; the other draws back.
    stage.session->givePrize(PrizeKind::Gun);
    stage.run(0.1);
    check(barrels.phase(WeaponType::Gun) == BarrelPhase::Growing && barrels.phase(WeaponType::Cannon) == BarrelPhase::Shrinking, "barrels: the weapon caught last grows and the other draws back");
    items.clear();
    barrels.append({}, items);
    check(items.size() > growing, "barrels: both are drawn while it happens");
    stage.run(1.5);
    check(barrels.phase(WeaponType::Cannon) == BarrelPhase::Hidden && barrels.phase(WeaponType::Gun) == BarrelPhase::Ready, "barrels: then the cannon is gone and the gun stands");

    // A shot makes it recoil, and the shots that are all gone end the weapon in a shrinking.
    std::vector<GameEvent> shot(1);
    shot[0].type = EventType::ShotFired;
    shot[0].prize = PrizeKind::Gun;
    stage.barrels->update(*stage.session, shot, 0);
    check(barrels.phase(WeaponType::Gun) == BarrelPhase::Recoiling, "barrels: a shot makes the gun recoil");
    // The Machine Gun's shots, at the interval the rules give, until none is left.
    stage.session->useShot();
    int shots = 0;
    while (stage.session->weapon() && shots < 40) {
        stage.session->useShot();
        ++shots;
    }
    stage.run(0.1);
    check(barrels.phase(WeaponType::Gun) == BarrelPhase::Recoiling || barrels.phase(WeaponType::Gun) == BarrelPhase::Shrinking, "barrels: the weapon has ended; the barrel is on its way out");
    stage.run(2.5);
    check(barrels.phase(WeaponType::Gun) == BarrelPhase::Hidden, "barrels: and gone about two seconds after");
    items.clear();
    barrels.append({}, items);
    check(items.empty(), "barrels: nothing is drawn");

    // The scene draws them with the bat, and a wider bat has wider barrels.
    Stage scene(renderer, assets, "piratos-ostrovok");
    const std::size_t bare = scene.scene->items().size();
    scene.session->givePrize(PrizeKind::Laser);
    scene.run(1.5);
    check(scene.scene->items().size() > bare, "barrels: the scene has more to draw with the laser's barrel on the bat: " + std::to_string(bare) + " to " + std::to_string(scene.scene->items().size()));
}

// ---- What flies -----------------------------------------------------------------------------------------------------------

// Plays a level with one weapon given, the click let through, until `done` or `seconds`.
struct Shots {
    Stage stage;
    Shots(Renderer& renderer, mb_app::Assets& assets, const std::string& level, PrizeKind prize) : stage(renderer, assets, level) {
        stage.baseline = stage.scene->items().size();
        stage.session->givePrize(prize);
        stage.session->givePrize(prize);
    }
};

void testWeapon(Renderer& renderer, mb_app::Assets& assets, const std::string& level, PrizeKind prize, const std::vector<std::string>& files, double seconds) {
    Shots shots(renderer, assets, level, prize);
    Stage& stage = shots.stage;
    stage.run(seconds, true);
    const std::string what = level + ", " + std::string(prizeInfo(prize).stem) + ": ";
    check(stage.events.contains(EventType::ShotFired), what + "it fired");
    for (const std::string& file : files) {
        check(stage.seen[file] > 0, what + "particles of " + file + " were drawn");
    }
    switch (prize) {
        case PrizeKind::Gun:
            check(stage.inTheAir >= 2 && stage.mostItems >= stage.baseline + 2, what + "bullets in the air are models in the scene (" + std::to_string(stage.inTheAir) + ", " + std::to_string(stage.mostItems - stage.baseline) + " more draws)");
            break;
        case PrizeKind::Cannon:
        case PrizeKind::Bazooka:
            check(stage.inTheAir >= 1 && stage.mostItems > stage.baseline, what + "the kernel and the rocket are models in the scene");
            break;
        default: break;
    }
}

void testStrikes(Renderer& renderer, mb_app::Assets& assets, const std::string& level) {
    {
        Stage stage(renderer, assets, level);
        stage.session->givePrize(PrizeKind::Thunderbolt);
        stage.run(14.0);
        check(stage.events.contains(EventType::BoltStruck) && stage.seen["Lightning"] >= 10, level + ": a Thunderbolt is lightning: " + std::to_string(stage.seen["Lightning"]) + " pieces at most");
        stage.run(0.5);
        std::vector<ParticleQuad> quads;
        stage.scene->particles().appendQuads(kView, quads);
        check(std::ranges::none_of(quads, [](const ParticleQuad& q) { return q.def->file == "Lightning"; }), level + ": and it is gone when it has struck");
    }
    {
        Stage stage(renderer, assets, level);
        const std::size_t baseline = stage.scene->items().size();
        stage.session->givePrize(PrizeKind::Meteor);
        stage.run(7.0);
        check(stage.events.contains(EventType::MeteorFalling) && stage.events.contains(EventType::MeteorImpact), level + ": the Meteorite Rain falls and lands");
        check(stage.seen["TrailMeteor"] >= 5, level + ": the meteors leave a trail: " + std::to_string(stage.seen["TrailMeteor"]));
        check(stage.seen["explosemeteor"] >= 1, level + ": and a ring on the water where each came down");
        check(stage.mostItems > baseline, level + ": a meteor in the air is a model in the scene");
        check(stage.session->projectiles().empty(), level + ": and none is left in the air");
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
    if (!fs::is_directory(root / "Data" / "Levels") || !fs::is_directory(root / "Data" / "Meshes")) {
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
        testBarrels(*renderer, assets);
        for (const std::string level : {"piratos-ostrovok", "cowboys-napadenie", "knight-fruktovie-derevia"}) {
            testWeapon(*renderer, assets, level, PrizeKind::Gun, {}, 3.0);
            testWeapon(*renderer, assets, level, PrizeKind::Cannon, {"exploseyadro"}, 6.0);
            testWeapon(*renderer, assets, level, PrizeKind::Bazooka, {"Trail", "exploseyadro"}, 6.0);
            testWeapon(*renderer, assets, level, PrizeKind::Laser, {"Laser"}, 4.0);
            testStrikes(*renderer, assets, level);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("weapons shown");
    }
    return failures == 0 ? 0 : 1;
}
