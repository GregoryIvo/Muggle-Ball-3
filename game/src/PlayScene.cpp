#include "mb/game/PlayScene.h"

#include "mb/game/GameCamera.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace mb {

namespace {

// The frames of bat.x that make the default bat (Korpus brings the wings and the little gear with it); the weapons'
// frames are for the prizes that carry them.
constexpr const char* kBatFrames[] = {"Korpus", "Shesterenka-Left", "Shesterenka-Right", "Hand", "Small-Hand", "Bita"};
// Red, blue, yellow: Settings::bat.
constexpr const char* kBatMaterials[] = {"Metall-Raketka-Red.jpg", "Metall-Raketka-Blue.jpg", "Metall-Raketka-Yellow.jpg"};

// The swoop, from the frames of V1 (52.2-52.9, 7874.2-7874.8, 7876.5-7876.7): a low flight in from beyond the lower left
// corner that lands spinning, and the same out towards the corner on the bat's side.
constexpr float kSwoopX = 26.0f;       // where it comes from, left of where it lands
constexpr float kSwoopZ = 7.0f;        // and nearer the player
constexpr float kSwoopLift = 2.5f;     // the hop at the middle of the flight
constexpr float kSwoopTurns = 1.5f;    // turns about the vertical, undone on landing
constexpr float kLeaveX = 30.0f;
constexpr float kLeaveShrink = 0.4f;   // how much smaller it is as it leaves the picture

constexpr float kShadowSize = 2.4f;   // a soft blob on the floor under the ball
constexpr float kShadowAlpha = 0.85f;
constexpr float kShadowLift = 0.04f;  // above the floor, which is at y = 0 in every level

constexpr double kExplosionSeconds = 0.4;
constexpr int kSparks = 12;
constexpr float kMostRolledPerFrame = 3.0f;  // a ball that jumped (it was freed from a pin) did not roll that far

Level batLevel(int bat) {
    Level level;
    for (const char* frame : kBatFrames) {
        GameObject object;
        object.type = ObjectType::Bat;
        object.mesh = "bat.x";
        object.frame = frame;
        object.material = kBatMaterials[std::clamp(bat, 0, static_cast<int>(std::size(kBatMaterials)) - 1)];
        Actor actor;
        actor.object = object;
        level.actors.push_back(actor);
    }
    return level;
}

Mat4 facingGameCamera() {
    const Vec3 forward = normalize(kGameCameraTarget - kGameCameraEye);
    const Vec3 right = normalize(cross({0, 1, 0}, forward));
    const Vec3 up = cross(forward, right);
    Mat4 m;
    const Vec3 axes[3] = {right, up, forward};
    for (int i = 0; i < 3; ++i) {
        m.m[i][0] = axes[i].x;
        m.m[i][1] = axes[i].y;
        m.m[i][2] = axes[i].z;
    }
    return m;
}

float progress(const BatView& bat) {
    return bat.phaseDuration > 0 ? static_cast<float>(std::clamp(bat.phaseTime / bat.phaseDuration, 0.0, 1.0)) : 1.0f;
}

// How far the bat is carried on past the session's last step: it moves only while it stands.
float batCarry(const BatView& bat, float lead) {
    return bat.phase == BatPhase::Present ? bat.velocityX * lead : 0.0f;
}

// A number in [0, 1) from two others, for the sparks: the same explosion always throws the same ones.
float hash01(std::uint32_t seed, std::uint32_t n) {
    std::uint32_t h = seed * 2654435761u ^ (n + 1) * 40503u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h & 0xFFFFu) / 65536.0f;
}

}  // namespace

BatPose batPose(const BatView& bat, float lead) {
    BatPose pose;
    pose.position = {bat.position.x, bat.position.y - kBallRadius, bat.position.z};
    const float s = progress(bat);
    switch (bat.phase) {
        case BatPhase::Present:
            pose.position.x += batCarry(bat, lead);
            break;
        case BatPhase::Entering: {
            const float left = (1 - s) * (1 - s) * (1 - s);  // what is left of the flight: fast at first, then settling
            pose.position.x -= kSwoopX * left;
            pose.position.z -= kSwoopZ * left;
            pose.position.y += kSwoopLift * std::sin(kPi * (1 - left));
            pose.yaw = 2 * kPi * kSwoopTurns * left;
            break;
        }
        case BatPhase::Leaving: {
            const float gone = s * s;
            const float side = bat.position.x < 0 ? -1.0f : 1.0f;
            pose.position.x += side * kLeaveX * gone;
            pose.position.z -= kSwoopZ * gone;
            pose.position.y += kSwoopLift * std::sin(kPi * gone);
            pose.yaw = -side * 2 * kPi * gone;
            pose.scale = 1 - kLeaveShrink * gone;
            pose.visible = s < 1;
            break;
        }
        case BatPhase::Absent:
            pose.visible = false;
            break;
    }
    return pose;
}

float heldBallScale(const BatView& bat) {
    switch (bat.phase) {
        case BatPhase::Present:
            return 1;
        case BatPhase::Entering: {
            const float t = std::clamp((progress(bat) - 0.7f) / 0.3f, 0.0f, 1.0f);
            return t * t * (3 - 2 * t);
        }
        default:
            return 0;
    }
}

Vec3 ballPosition(const BallView& ball, const BatView& bat, float lead) {
    if (ball.held) {
        return ball.position + Vec3{batCarry(bat, lead), 0, 0};
    }
    return ball.position + ball.velocity * lead;
}

PlayScene::PlayScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures,
                     const Level& level, const GameSession& session, int bat)
    : level_(cache, materials, textures, level, session.scene().plan()),
      bat_(cache, materials, textures, batLevel(bat), PhysicsPlan{}),
      quad_(cache.quad()),
      flashTexture_(cache.texture(textures.find("Oreols/02.png"))),
      sparkTexture_(cache.texture(textures.find("Particles/PARTICLE.PNG"))),
      facing_(facingGameCamera()) {
    update(session, {}, 0);
}

void PlayScene::update(const GameSession& session, std::span<const GameEvent> events, double seconds, double lead) {
    time_ += seconds;
    level_.update(time_);
    level_.follow(session.scene());

    for (const GameEvent& event : events) {
        switch (event.type) {
            case EventType::BrickHit:
                if (event.actor != kNoActor) {
                    level_.setDamage(event.actor, session.actor(event.actor).textureIndex);
                }
                break;
            case EventType::BrickDestroyed:
                level_.hide(event.actor);
                explosions_.push_back({event.position, 0, event.actor * 977u + static_cast<std::uint32_t>(event.time * 1000)});
                break;
            case EventType::ActorFell:
                level_.hide(event.actor);
                break;
            case EventType::BallLost:
                // It carries on along its path while it shrinks, turning as it did.
                vanishing_.push_back({event.position, event.velocity, rolls_[event.ball].turn, 0});
                rolls_.erase(event.ball);
                break;
            default:
                break;
        }
    }
    for (Vanishing& ball : vanishing_) {
        ball.age += seconds;
    }
    std::erase_if(vanishing_, [&](const Vanishing& ball) { return ball.age >= session.rules().ballVanishTime; });
    for (Explosion& explosion : explosions_) {
        explosion.age += seconds;
    }
    std::erase_if(explosions_, [](const Explosion& explosion) { return explosion.age >= kExplosionSeconds; });

    items_.clear();
    level_.appendDrawn(items_);

    const BatPose bat = batPose(session.bat(), static_cast<float>(lead));
    if (bat.visible) {
        const Mat4 place = Mat4::scaling({bat.scale, bat.scale, bat.scale}) * Mat4::rotationY(bat.yaw) *
                           Mat4::translation(bat.position);
        for (DrawItem item : bat_.items()) {
            item.world = item.world * place;
            items_.push_back(item);
        }
    }

    std::erase_if(rolls_, [&](const auto& entry) {
        return std::ranges::none_of(session.balls(), [&](const BallView& ball) { return ball.id == entry.first; });
    });
    for (const BallView& ball : session.balls()) {
        const Vec3 at = ballPosition(ball, session.bat(), static_cast<float>(lead));
        Roll& roll = rolls_[ball.id];
        float scale = 1;
        if (ball.held) {
            scale = heldBallScale(session.bat());
            roll.placed = false;
        } else {
            if (roll.placed) {
                // Rolling on the floor: turned about the horizontal axis across its path by the distance over its radius.
                const float dx = at.x - roll.at.x, dz = at.z - roll.at.z;
                const float distance = std::hypot(dx, dz);
                if (distance > 1e-5f && distance < kMostRolledPerFrame) {
                    roll.turn = roll.turn * Mat4::rotationAxis({dz / distance, 0, -dx / distance}, distance / kBallRadius);
                }
            }
            roll.at = at;
            roll.placed = true;
        }
        if (scale > 0) {
            addBall(at, roll.turn, scale);
        }
    }
    for (const Vanishing& ball : vanishing_) {
        const float left = 1 - static_cast<float>(ball.age / session.rules().ballVanishTime);
        addBall(ball.at + ball.velocity * static_cast<float>(ball.age), ball.turn, left);
    }
    for (const Explosion& explosion : explosions_) {
        addExplosion(explosion);
    }
}

void PlayScene::addBall(Vec3 at, const Mat4& turn, float scale) {
    const Mat4 place = Mat4::scaling({scale, scale, scale}) * turn * Mat4::translation(at);
    for (DrawItem item : level_.ballItems()) {
        item.world = item.world * place;
        items_.push_back(item);
    }
    DrawItem shadow;
    shadow.mesh = quad_;
    shadow.indexCount = 6;
    shadow.world = Mat4::scaling({kShadowSize * scale, kShadowSize * scale, 1}) * Mat4::rotationX(kPi / 2) *
                   Mat4::translation({at.x, kShadowLift, at.z});
    MaterialParams& m = shadow.material;
    m.diffuse = {0, 0, 0, kShadowAlpha};
    m.texture = sparkTexture_;
    m.blend = BlendMode::AlphaBlend;
    m.cull = CullMode::None;
    m.lit = false;
    m.depthWrite = false;
    items_.push_back(shadow);
}

// A burst of light and sparks, as the original's bricks go off: an orange flash that swells and fades, and a few sparks
// thrown up and out. Which of its particle files a material's exploseType would pick is not known (docs/OBJECTS.md
// section 6), so every brick has this one; the particle systems come with step 5.
void PlayScene::addExplosion(const Explosion& explosion) {
    const float u = static_cast<float>(explosion.age / kExplosionSeconds);
    const float fade = 1 - u;
    addBillboard(explosion.at, 7.0f + 8.0f * u, {1, 1, 1, fade * std::sqrt(fade)}, flashTexture_, BlendMode::AlphaBlend);
    constexpr float kGravity = 18.0f;
    const auto t = static_cast<float>(explosion.age);
    for (int i = 0; i < kSparks; ++i) {
        const auto n = static_cast<std::uint32_t>(i);
        const float angle = 2 * kPi * hash01(explosion.seed, n * 3);
        const float out = 4.0f + 6.0f * hash01(explosion.seed, n * 3 + 1);
        const float up = 4.0f + 6.0f * hash01(explosion.seed, n * 3 + 2);
        Vec3 at = explosion.at + Vec3{std::cos(angle) * out * t, up * t - 0.5f * kGravity * t * t, std::sin(angle) * out * t};
        at.y = std::max(at.y, 0.2f);
        addBillboard(at, 1.6f * fade + 0.3f, {1.0f, 0.5f, 0.1f, fade}, sparkTexture_, BlendMode::Additive);
    }
}

void PlayScene::addBillboard(Vec3 at, float size, Color tint, TextureHandle texture, BlendMode blend) {
    DrawItem item;
    item.mesh = quad_;
    item.indexCount = 6;
    item.world = Mat4::scaling({size, size, 1}) * facing_ * Mat4::translation(at);
    MaterialParams& m = item.material;
    m.diffuse = tint;
    m.texture = texture;
    m.blend = blend;
    m.cull = CullMode::None;
    m.lit = false;
    m.depthWrite = false;
    m.depthTest = false;
    items_.push_back(item);
}

}  // namespace mb
