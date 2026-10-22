#include "mb/game/PrizeScene.h"

#include "mb/engine/Log.h"
#include "mb/game/Billboard.h"
#include "mb/game/SceneBuilder.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace mb {

namespace {

// ---- How the prizes look. Where the data and the footage say nothing, the number is a choice [I] (docs/GAMEPLAY.md section 16).

// The orbs turn about the vertical as they fall; the rate is not measured.
constexpr float kSpinRate = 2.5f;   // radians per second
constexpr float kSpinPhase = 1.7f;  // radians between one prize and the next, so that a row of them does not turn in step
// The halo is the particle file's two sprites (Oreolblue.xml for a good prize, Oreolred.xml for a bad one) [D]: the Oreol, a star
// that faces the camera and turns at 3.5 rad/s, and the OreolShadow, a mark on the floor 0.1 up that turns the other way at 1.5.
// The sizes are about twice the orb's, as the frames of V1 134.4 look [I].
constexpr float kGlowSize = 4.0f;
constexpr float kGlowTurn = 3.5f;
// The star is drawn behind the model, so that the model is not washed out: moved this far away from the camera along the line of
// sight, which does not move it on the screen, it only draws before the model.
constexpr float kGlowBehind = 2.0f;
constexpr float kGlowAlpha = 0.85f;
constexpr float kMarkSize = 4.0f;
constexpr float kMarkTurn = -1.5f;
constexpr float kMarkLift = 0.1f;

// A prize that is caught flashes: the halo swells and fades [I]. One that is missed fizzles in a small blue sparkle ring of about
// 0.2 s (docs/GAMEPLAY.md section 14.4 [M]).
constexpr double kPopSeconds = 0.3;
constexpr float kPopGrowth = 1.2f;  // the halo's size grows by this share as it fades
constexpr double kFizzleSeconds = 0.25;
constexpr int kFizzleSparks = 8;
constexpr float kFizzleReach = 3.0f;  // how far the sparks fly, and how wide the ring grows
constexpr Color kSparkBlue{0.35f, 0.55f, 1.0f, 1.0f};
// The Explosive Ball's blast: an orange star with a pair of blue arrows spread to about 5 u across in 0.75 s (V2 174.55-175.31,
// F35) [M]; the star is the blast's diameter (Blast::speed is its radius).
constexpr double kBlastSeconds = 0.75;
constexpr float kBlastTurn = 2.0f;  // radians per second

// The Yo-Yo's string: the rubber particle (Particles/rubber.xml) has `colour1` green and `colour2` blue [D]; green at the bat, blue
// at the ball, as the footage has it (V2 125-131) [M].
constexpr Color kStringNear{0.1f, 1.0f, 0.1f, 0.9f};
constexpr Color kStringFar{0.1f, 0.3f, 1.0f, 0.9f};
constexpr float kStringWidth = 0.22f;
constexpr int kStringSegments = 12;
constexpr float kStringOverlap = 0.04f;  // of the string's length, at each end of a piece
constexpr float kStringHeight = 1.0f;    // the string leaves the bat at the top of its arc

// The Wall's rope rises out of the floor as the effect starts and sinks back as it ends [I]. It is 2.95 tall, from -0.84 to 2.1.
constexpr double kWallRise = 0.3;
constexpr float kWallSink = 3.0f;

// The night: the board's brightness and the sky's at full night, as a share of the day's (docs/GAMEPLAY.md section 14.3) [M].
constexpr float kNightBoard = 0.38f;
constexpr float kNightSky = 0.49f;

// ---- The models

// What is not a prize: the balls with models of their own, the Wall's rope and the Magnet's horseshoe (a frame of bat.x). The
// prizes come first, in the order of PrizeKind. (The weapons' barrels are frames of bat.x too, but the game moves them: they grow
// out of the bat in its "Common" animation and recoil in it. They come with the weapons.)
enum Extra : ActorIndex {
    kPowerBall = kPrizeKindCount,
    kExplosiveBall,
    kRope,
    kMagnet,
};

struct ExtraDef {
    ObjectType type;
    const char* mesh;
    const char* frame;
    bool ofBat;  // takes the player's bat texture
    const char* material;  // otherwise, if any
};
constexpr ExtraDef kExtras[] = {
    {ObjectType::Ball, "ball-powerfull.x", "", false, ""},
    {ObjectType::Ball, "ball-explosive.x", "", false, ""},
    {ObjectType::Bat, "wall.x", "", false, "Bonus-Stena.jpg"},
    {ObjectType::Bat, "bat.x", "Magnit", true, ""},
};

Level propsLevel(std::string_view batMaterial) {
    Level level;
    const auto add = [&](ObjectType type, std::string mesh, std::string material, std::string frame) {
        GameObject object;
        object.type = type;
        object.mesh = std::move(mesh);
        object.material = std::move(material);
        object.frame = std::move(frame);
        Actor actor;
        actor.object = std::move(object);
        level.actors.push_back(std::move(actor));
    };
    for (int i = 0; i < kPrizeKindCount; ++i) {
        const std::string stem(prizeInfo(static_cast<PrizeKind>(i)).stem);
        add(ObjectType::Prize, "Prizes/" + stem + ".x", stem, "");
    }
    for (const ExtraDef& def : kExtras) {
        add(def.type, def.mesh, def.ofBat ? std::string(batMaterial) : std::string(def.material), def.frame);
    }
    return level;
}

// The box round a model, all its frames in place.
struct ModelBox {
    Vec3 low{1e9f, 1e9f, 1e9f}, high{-1e9f, -1e9f, -1e9f};
    Vec3 centre() const { return (low + high) * 0.5f; }
    float width() const { return high.x - low.x; }
};

ModelBox modelBox(const Model& model) {
    ModelBox box;
    for (std::size_t node = 0; node < model.nodes.size(); ++node) {
        if (model.nodes[node].mesh < 0) {
            continue;
        }
        const Mat4 world = model.globalTransform(static_cast<int>(node));
        for (const ModelPrimitive& primitive : model.meshes[static_cast<std::size_t>(model.nodes[node].mesh)].primitives) {
            for (std::uint32_t i = primitive.firstIndex; i < primitive.firstIndex + primitive.indexCount; ++i) {
                const Vec3 p = transformPoint(model.vertices[model.indices[i]].position, world);
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    box.low[axis] = std::min(box.low[axis], p[axis]);
                    box.high[axis] = std::max(box.high[axis], p[axis]);
                }
            }
        }
    }
    return box;
}

Vec2 square(float size) {
    return {size, size};
}

Color lerp(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

}  // namespace

PrizeScene::PrizeScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures,
                       std::string_view batMaterial)
    : props_(cache, materials, textures, propsLevel(batMaterial), PhysicsPlan{}),
      quad_(cache.quad()),
      facing_(facingGameCamera()),
      forward_{facing_.m[2][0], facing_.m[2][1], facing_.m[2][2]} {
    for (const std::string& name : props_.report().missingMeshes) {
        logWarn("prizes: missing mesh {}", name);
    }
    for (const std::string& name : props_.report().missingFrames) {
        logWarn("prizes: missing frame {}", name);
    }

    for (int i = 0; i < kPrizeKindCount; ++i) {
        const std::string file = meshFileName("Prizes/" + std::string(prizeInfo(static_cast<PrizeKind>(i)).stem) + ".x");
        if (const ModelCache::Entry* entry = cache.model(file)) {
            const ModelBox box = modelBox(entry->model);
            fit_[static_cast<std::size_t>(i)] = {box.centre(), box.low.y};
        }
    }

    // A ball of its own model is drawn as wide as the plain ball's.
    const ModelCache::Entry* plain = cache.model("ball.glb");
    const float plainWidth = plain ? modelBox(plain->model).width() : 0.0f;
    const auto ballModel = [&](ActorIndex actor, const char* mesh, std::vector<DrawItem>& out) {
        const ModelCache::Entry* entry = cache.model(meshFileName(mesh));
        const float width = entry ? modelBox(entry->model).width() : 0.0f;
        if (width <= 0 || plainWidth <= 0) {
            return;
        }
        const Mat4 fit = Mat4::scaling(Vec3{1, 1, 1} * (plainWidth / width));
        props_.appendActor(actor, out);
        for (DrawItem& item : out) {
            item.world = item.world * fit;
        }
    };
    ballModel(kPowerBall, kExtras[0].mesh, powerBall_);
    ballModel(kExplosiveBall, kExtras[1].mesh, explosiveBall_);

    const auto texture = [&](const char* name) { return cache.texture(textures.find(name)); };
    glow_ = {texture("Oreols/02.png"), texture("Oreols/11.png")};
    mark_ = {texture("Oreols/07.png"), texture("Oreols/09.png")};
    star_ = texture("Oreols/03.png");
    ring_ = texture("Particles/Boom.png");
    dot_ = texture("Particles/PARTICLE.PNG");
}

void PrizeScene::update(std::span<const GameEvent> events, double seconds) {
    for (const GameEvent& event : events) {
        switch (event.type) {
            case EventType::PrizeCaught:
                if (event.prize) {
                    pops_.push_back({event.position, 0, kGlowSize, prizeInfo(*event.prize).bad});
                }
                break;
            case EventType::PrizeMissed:
                fizzles_.push_back({event.position, 0, 0, false});
                break;
            case EventType::Blast:
                blasts_.push_back({event.position, 0, event.speed * 2, false});
                break;
            default:
                break;
        }
    }
    const auto age = [seconds](std::vector<Flash>& flashes, double lifetime) {
        for (Flash& flash : flashes) {
            flash.age += seconds;
        }
        std::erase_if(flashes, [lifetime](const Flash& flash) { return flash.age >= lifetime; });
    };
    age(pops_, kPopSeconds);
    age(fizzles_, kFizzleSeconds);
    age(blasts_, kBlastSeconds);
}

void PrizeScene::appendPrizes(const GameSession& session, double lead, std::vector<DrawItem>& out) const {
    const float fall = session.rules().prizes.fallSpeed * static_cast<float>(lead);
    for (const FallingPrize& prize : session.fallingPrizes()) {
        const Vec3 at = prize.position - Vec3{0, 0, fall};
        addHalo(at, prizeInfo(prize.kind).bad, prize.age + lead, out);
        addPrize(prize, at, out);
    }
    for (const Flash& flash : pops_) {
        addPop(flash, out);
    }
    for (const Flash& flash : fizzles_) {
        addFizzle(flash, out);
    }
    for (const Flash& flash : blasts_) {
        addBlast(flash, out);
    }
}

void PrizeScene::addHalo(Vec3 at, bool bad, double age, std::vector<DrawItem>& out) const {
    const std::size_t side = bad ? 1 : 0;
    const auto t = static_cast<float>(age);
    out.push_back(billboard(quad_, facing_, at + forward_ * kGlowBehind, square(kGlowSize), {1, 1, 1, kGlowAlpha}, glow_[side],
                            BlendMode::Additive, kGlowTurn * t));
    out.push_back(floorMark(quad_, {at.x, kMarkLift, at.z}, kMarkSize, {1, 1, 1, 1}, mark_[side], BlendMode::AlphaBlend,
                            kMarkTurn * t));
}

void PrizeScene::addPrize(const FallingPrize& prize, Vec3 at, std::vector<DrawItem>& out) const {
    const Fit& fit = fit_[static_cast<std::size_t>(prize.kind)];
    // The middle of the model's box at the orb's centre, but never through the floor.
    const float y = std::max(at.y - fit.centre.y, -fit.low);
    const float spin = kSpinRate * static_cast<float>(prize.age) + kSpinPhase * static_cast<float>(prize.id);
    const Mat4 place = Mat4::translation({-fit.centre.x, 0, -fit.centre.z}) * Mat4::rotationY(spin) *
                       Mat4::translation({at.x, y, at.z});
    const std::size_t first = out.size();
    props_.appendActor(static_cast<ActorIndex>(prize.kind), out);
    for (std::size_t i = first; i < out.size(); ++i) {
        out[i].world = out[i].world * place;
        out[i].material.blend = BlendMode::AlphaBlend;  // so that it draws after the star behind it, which is sorted with the blended
    }
}

// A caught prize's halo swells and fades where the orb was.
void PrizeScene::addPop(const Flash& flash, std::vector<DrawItem>& out) const {
    const auto u = static_cast<float>(flash.age / kPopSeconds);
    out.push_back(billboard(quad_, facing_, flash.at, square(flash.size * (1 + kPopGrowth * u)), {1, 1, 1, kGlowAlpha * (1 - u)},
                            glow_[flash.bad ? 1 : 0], BlendMode::Additive));
}

// A ring of light that spreads and a few blue sparks flying out over the floor, on the bat's line.
void PrizeScene::addFizzle(const Flash& flash, std::vector<DrawItem>& out) const {
    const auto u = static_cast<float>(flash.age / kFizzleSeconds);
    const Color fade = {kSparkBlue.r, kSparkBlue.g, kSparkBlue.b, 1 - u};
    out.push_back(billboard(quad_, facing_, flash.at, square(1.0f + kFizzleReach * u), fade, ring_, BlendMode::Additive));
    for (int i = 0; i < kFizzleSparks; ++i) {
        const float angle = 2 * kPi * (static_cast<float>(i) + 0.5f * flash.at.x) / static_cast<float>(kFizzleSparks);
        const Vec3 at = flash.at + Vec3{std::cos(angle), 0.0f, std::sin(angle)} * (kFizzleReach * u) + Vec3{0, 0.8f * u, 0};
        out.push_back(billboard(quad_, facing_, at, square(0.7f), fade, dot_, BlendMode::Additive));
    }
}

void PrizeScene::addBlast(const Flash& flash, std::vector<DrawItem>& out) const {
    const auto u = static_cast<float>(flash.age / kBlastSeconds);
    const float grown = 1 - (1 - u) * (1 - u);  // fast at first, then settling
    const float size = flash.size * (0.3f + 0.7f * grown);
    const Vec3 at{flash.at.x, 1.0f, flash.at.z};
    out.push_back(billboard(quad_, facing_, at, square(size), {1, 1, 1, 1 - u}, glow_[0], BlendMode::Additive));
    out.push_back(billboard(quad_, facing_, at, square(size), {1, 1, 1, 1 - u * u}, star_, BlendMode::Additive,
                            kBlastTurn * static_cast<float>(flash.age)));
}

void PrizeScene::appendMagnet(const GameSession& session, const Mat4& place, std::vector<DrawItem>& out) const {
    if (session.effect(PrizeKind::Magnet) == nullptr) {
        return;
    }
    const std::size_t first = out.size();
    props_.appendActor(kMagnet, out);
    for (std::size_t i = first; i < out.size(); ++i) {
        out[i].world = out[i].world * place;
    }
}

void PrizeScene::appendWall(const GameSession& session, std::vector<DrawItem>& out) const {
    const ActiveEffect* wall = session.effect(PrizeKind::Wall);
    if (wall == nullptr) {
        return;
    }
    const double rise = std::clamp(std::min(wall->duration - wall->timeLeft, wall->timeLeft) / kWallRise, 0.0, 1.0);
    const auto up = static_cast<float>(rise * rise * (3 - 2 * rise));
    const Mat4 place = Mat4::translation({0, -(1 - up) * kWallSink, 0});
    const std::size_t first = out.size();
    props_.appendActor(kRope, out);
    for (std::size_t i = first; i < out.size(); ++i) {
        out[i].world = out[i].world * place;
    }
}

void PrizeScene::appendString(Vec3 from, Vec3 to, std::vector<DrawItem>& out) const {
    from.y = kStringHeight;
    const Vec3 along = to - from;
    for (int i = 0; i < kStringSegments; ++i) {
        const float a = static_cast<float>(i) / kStringSegments, b = static_cast<float>(i + 1) / kStringSegments;
        // Each piece reaches a little into the next, which a strip that is only as long as its share of the picture does not.
        out.push_back(strip(quad_, facing_, from + along * (a - kStringOverlap), from + along * (b + kStringOverlap), kStringWidth,
                            lerp(kStringNear, kStringFar, (a + b) * 0.5f), 0, BlendMode::AlphaBlend));
    }
}

BallLook PrizeScene::ballLook(const GameSession& session) {
    if (session.effect(PrizeKind::PowerBall) != nullptr) {
        return BallLook::Power;
    }
    return session.effect(PrizeKind::ExplosiveBall) != nullptr ? BallLook::Explosive : BallLook::Plain;
}

DirectionalLight nightLight(const DirectionalLight& day, float night) {
    const float share = 1 + (kNightBoard - 1) * night;
    DirectionalLight light = day;
    light.diffuse = {day.diffuse.r * share, day.diffuse.g * share, day.diffuse.b * share, day.diffuse.a};
    light.ambient = {day.ambient.r * share, day.ambient.g * share, day.ambient.b * share, day.ambient.a};
    return light;
}

float unlitNightShare(float night) {
    return 1 + (kNightSky - 1) * night;
}

void dimUnlit(std::span<DrawItem> items, float night) {
    const float share = unlitNightShare(night);
    for (DrawItem& item : items) {
        if (!item.material.lit) {
            Color& colour = item.material.diffuse;
            colour = {colour.r * share, colour.g * share, colour.b * share, colour.a};
        }
    }
}

}  // namespace mb
