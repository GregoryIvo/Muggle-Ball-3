// The sails on screen, on the real data in $MB3_ASSETS and the GPU without a window (skips with exit code 77 without either): every sail of
// every level gets a mesh in its material's texture; drawn with the level it covers the place of its particles and changes nothing else in
// the picture; it follows the particles as the physics moves them and is gone when the rules destroy it.

#include "Assets.h"
#include "mb/engine/Image.h"
#include "mb/engine/Renderer.h"
#include "mb/game/ClothDraw.h"
#include "mb/game/GameCamera.h"
#include "mb/game/Gameplay.h"
#include "mb/game/LevelLoader.h"
#include "mb/game/LevelScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
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

constexpr int kWidth = 1024, kHeight = 768;
constexpr double kFrame = 1.0 / 60.0;
// A sail of the footage covers thousands of pixels at this size; a few of them is something that went wrong.
constexpr std::size_t kLeastSailPixels = 1500;

struct Pixel {
    int x, y;
};

// Everything one level needs to be seen with its sails: a session (for the physics and the rules), the level's own draws and the sails'.
struct Stage {
    Level level;
    std::unique_ptr<GameSession> session;
    std::unique_ptr<ModelCache> models;
    std::unique_ptr<LevelScene> scene;
    std::unique_ptr<ClothDraw> sails;
    Camera camera;

    Stage(Renderer& renderer, mb_app::Assets& assets, const LevelEntry& entry) {
        level = loadLevelJson(entry.path);
        SessionOptions options;
        options.rules.prizes.drops = false;
        session = std::make_unique<GameSession>(level, assets.materials, options);
        models = std::make_unique<ModelCache>(renderer, assets.root / "Data" / "Meshes");
        scene = std::make_unique<LevelScene>(*models, assets.materials, assets.textures, level, session->scene().plan());
        sails = std::make_unique<ClothDraw>(*models, assets.materials, assets.textures, level, session->scene());
    }

    void play(double seconds) {
        for (double left = seconds; left > 1e-9; left -= kFrame) {
            session->update(std::min(kFrame, left), {});
            session->takeEvents();
        }
        scene->follow(session->scene());
        sails->update(session->scene());
    }

    std::vector<DrawItem> items(bool withSails) const {
        std::vector<DrawItem> out;
        scene->appendDrawn(out);
        if (withSails) {
            sails->appendDrawn(session->scene(), out);
        }
        return out;
    }

    Image picture(Renderer& renderer, bool withSails) const {
        FrameParams frame;
        frame.view = camera.view();
        frame.projection = camera.projection(static_cast<float>(kWidth) / kHeight);
        frame.eye = camera.eye();
        frame.light = scene->light();
        frame.clear = {0, 0, 0, 1};
        const std::vector<DrawItem> drawn = items(withSails);
        return renderer.renderToImage(kWidth, kHeight, frame, drawn);
    }

    // Where the sails' particles are on the picture: the box round all of them, and a margin of the particles' size.
    void box(Pixel& low, Pixel& high) const {
        const Mat4 toClip = camera.view() * camera.projection(static_cast<float>(kWidth) / kHeight);
        low = {kWidth, kHeight};
        high = {0, 0};
        const ClothSet& cloths = session->scene().cloths();
        for (std::size_t i = 0; i < cloths.size(); ++i) {
            for (const Vec3 p : cloths.positions(i)) {
                const Vec4 clip = transform({p.x, p.y, p.z, 1}, toClip);
                const int x = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * kWidth);
                const int y = static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * kHeight);
                low = {std::min(low.x, x), std::min(low.y, y)};
                high = {std::max(high.x, x), std::max(high.y, y)};
            }
        }
        constexpr int kMargin = 40;  // a particle's radius, and what the sail's surface is wider than its particles by at a bulge
        low = {std::max(0, low.x - kMargin), std::max(0, low.y - kMargin)};
        high = {std::min(kWidth - 1, high.x + kMargin), std::min(kHeight - 1, high.y + kMargin)};
    }
};

bool samePixel(const Image& a, const Image& b, int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
    return a.rgba[at] == b.rgba[at] && a.rgba[at + 1] == b.rgba[at + 1] && a.rgba[at + 2] == b.rgba[at + 2];
}

// How many pixels differ between two pictures: inside the box, and outside it.
void difference(const Image& a, const Image& b, Pixel low, Pixel high, std::size_t& inside, std::size_t& outside) {
    inside = outside = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (!samePixel(a, b, x, y)) {
                const bool in = x >= low.x && x <= high.x && y >= low.y && y <= high.y;
                (in ? inside : outside) += 1;
            }
        }
    }
}

void testEveryLevelsSails(Renderer& renderer, mb_app::Assets& assets) {
    std::size_t sails = 0;
    for (const LevelEntry& entry : assets.catalog.levels()) {
        const Level level = loadLevelJson(entry.path);
        if (level.countObjects(ObjectType::Cloth) == 0) {
            continue;
        }
        Stage stage(renderer, assets, entry);
        const ClothSet& cloths = stage.session->scene().cloths();
        const std::vector<DrawItem> drawn = stage.items(true), plain = stage.items(false);
        check(stage.sails->size() == cloths.size() && drawn.size() == plain.size() + cloths.size(),
              entry.name + ": a draw for each sail: " + std::to_string(drawn.size() - plain.size()) + " of " + std::to_string(cloths.size()));
        bool textured = true, distinct = true;
        for (std::size_t i = plain.size(); i < drawn.size(); ++i) {
            textured = textured && drawn[i].mesh != 0 && drawn[i].material.texture != 0 && drawn[i].indexCount > 0 && drawn[i].material.cull == CullMode::Back;
            for (std::size_t j = i + 1; j < drawn.size(); ++j) {
                distinct = distinct && drawn[i].mesh != drawn[j].mesh;
            }
        }
        check(textured, entry.name + ": each sail has a mesh of its own, the material's texture, and is drawn from both sides by two halves");
        check(distinct, entry.name + ": no two sails share a mesh (each follows its own particles)");
        sails += cloths.size();
    }
    check(sails == 22, "all 22 sails of the levels have a draw: " + std::to_string(sails));
}

void testThePictureOfAShip(Renderer& renderer, mb_app::Assets& assets) {
    const LevelEntry& entry = assets.catalog.levels()[*assets.catalog.find("piratos-ostrovok")];
    Stage stage(renderer, assets, entry);
    stage.play(0.001);
    const Image without = stage.picture(renderer, false), with = stage.picture(renderer, true);
    Pixel low{}, high{};
    stage.box(low, high);
    std::size_t inside = 0, outside = 0;
    difference(without, with, low, high, inside, outside);
    check(inside > kLeastSailPixels, "ship: the sails cover the place of their particles: " + std::to_string(inside) + " pixels");
    check(outside == 0, "ship: and change nothing of the picture outside it: " + std::to_string(outside) + " pixels");

    // The sails billow as the physics fills them out.
    stage.play(4.0);
    const Image later = stage.picture(renderer, true);
    stage.box(low, high);
    difference(with, later, low, high, inside, outside);
    check(inside > 500, "ship: four seconds on the sails have taken their shape: " + std::to_string(inside) + " pixels have changed");

    // The rules destroy one: it is out of the picture, and the others stay.
    const ClothSet& cloths = stage.session->scene().cloths();
    const std::size_t before = stage.items(true).size();
    check(stage.session->hit(cloths.plan(0).lead, cloths.centre(0)), "ship: a hit takes a sail's one energy");
    stage.play(0.1);
    check(stage.items(true).size() + 1 == before && !cloths.alive(0) && cloths.alive(1), "ship: the sail that is destroyed is no longer drawn, the others are");
}

}  // namespace

int main() {
    const char* env = std::getenv("MB3_ASSETS");
    if (!env || !*env) {
        std::puts("SKIP: MB3_ASSETS is not set");
        return 77;
    }
    if (!fs::is_directory(fs::path(env) / "Data" / "Levels") || !fs::is_directory(fs::path(env) / "Textures")) {
        std::printf("SKIP: converted data not found under %s\n", env);
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
        testEveryLevelsSails(*renderer, assets);
        testThePictureOfAShip(*renderer, assets);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: exception: %s\n", e.what());
        return 1;
    }
    if (failures == 0) {
        std::puts("sails drawn");
    }
    return failures == 0 ? 0 : 1;
}
