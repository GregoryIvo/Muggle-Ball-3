#include "mb/game/PlayScene.h"

#include "mb/game/Billboard.h"
#include "mb/game/BrickEffects.h"
#include "mb/game/GameCamera.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace mb {

namespace {

// The frames of bat.x that make the default bat (Korpus brings the wings and the little gear with it); the weapons'
// frames are BatBarrels'.
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

constexpr float kMostRolledPerFrame = 3.0f;  // a ball that jumped (it was freed from a pin) did not roll that far

const char* batMaterial(int bat) {
    return kBatMaterials[std::clamp(bat, 0, static_cast<int>(std::size(kBatMaterials)) - 1)];
}

Level batLevel(int bat) {
    Level level;
    for (const char* frame : kBatFrames) {
        GameObject object;
        object.type = ObjectType::Bat;
        object.mesh = "bat.x";
        object.frame = frame;
        object.material = batMaterial(bat);
        Actor actor;
        actor.object = object;
        level.actors.push_back(actor);
    }
    return level;
}

float progress(const BatView& bat) {
    return bat.phaseDuration > 0 ? static_cast<float>(std::clamp(bat.phaseTime / bat.phaseDuration, 0.0, 1.0)) : 1.0f;
}

// How far the bat is carried on past the session's last step: it moves only while it stands.
float batCarry(const BatView& bat, float lead) {
    return bat.phase == BatPhase::Present ? bat.velocityX * lead : 0.0f;
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

float ballScale(const BallView& ball, const BatView& bat) {
    if (ball.held) {
        return heldBallScale(bat);
    }
    return ball.stuck ? 1.0f : ball.appear;
}

Vec3 ballPosition(const BallView& ball, const BatView& bat, float lead) {
    if (ball.held || ball.stuck) {
        return ball.position + Vec3{batCarry(bat, lead), 0, 0};
    }
    return ball.position + ball.velocity * lead;
}

PlayScene::PlayScene(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures,
                     const ParticleLibrary& particles, const Level& level, const GameSession& session, int bat)
    : level_(cache, materials, textures, level, session.scene().plan()),
      sails_(cache, materials, textures, level, session.scene()),
      bat_(cache, materials, textures, batLevel(bat), PhysicsPlan{}),
      quad_(cache.quad()),
      sparkTexture_(cache.texture(textures.find("Particles/PARTICLE.PNG"))),
      view_(particleViewOf(facingGameCamera())),
      prizes_(cache, materials, textures, batMaterial(bat)),
      particles_(particles),
      batches_(particles,
               [&cache, &textures](std::string_view name) {
                   const std::filesystem::path file = textures.find(name);
                   return file.empty() ? TextureHandle{0} : cache.texture(file);
               }),
      effects_(cache, materials, textures, level, particles_),
      barrels_(cache, materials, textures, batMaterial(bat)),
      weapons_(cache, materials, textures, particles_, session.rules().weapons) {
    batches_.preload();  // (the textures are uploaded now, not by the first flash or trail in the middle of a game)
    update(session, {}, 0);
}

void PlayScene::update(const GameSession& session, std::span<const GameEvent> events, double seconds, double lead) {
    time_ += seconds;
    level_.update(time_);
    level_.follow(session.scene());
    sails_.update(session.scene());
    effects_.update(session, events, seconds, level_, lead);
    prizes_.update(events, seconds);
    barrels_.update(session, events, seconds);
    weapons_.update(session, events, lead);

    for (const GameEvent& event : events) {
        switch (event.type) {
            case EventType::BrickHit:
                if (event.actor != kNoActor) {
                    level_.setDamage(event.actor, session.actor(event.actor).textureIndex);
                }
                break;
            case EventType::BrickDestroyed:
                if (shrinksAway(event.material)) {
                    shrinking_.push_back(event.actor);
                } else {
                    level_.hide(event.actor);
                }
                // The flash is the material's exploseType's: none for a pirate [M], and none for what a bolt of lightning destroyed [M].
                if (event.material != nullptr && event.prize != PrizeKind::Thunderbolt) {
                    for (const std::string_view effect : explosionEffects(event.material->exploseType)) {
                        particles_.spawn(effect, event.position);
                    }
                }
                break;
            case EventType::ActorFell:
                level_.hide(event.actor);
                break;
            case EventType::BallLost: {
                // It carries on along its path while it shrinks, turning as it did, from the size it was drawn at: a ball lost in a
                // teleport (the Skull) was not drawn at all, and does not pop into view at the source.
                const Roll roll = rolls_[event.ball];
                if (roll.shown > 0) {
                    vanishing_.push_back({event.position, event.velocity, roll.turn, roll.size * roll.shown, roll.look, 0});
                }
                rolls_.erase(event.ball);
                break;
            }
            default:
                break;
        }
    }
    std::erase_if(shrinking_, [&](ActorIndex actor) {
        const ActorStatus status = session.actor(actor);
        if (status.removed) {
            level_.hide(actor);
        } else {
            level_.setScale(actor, 1 - status.shrink, session.centreOf(actor));
        }
        return status.removed;
    });
    for (Vanishing& ball : vanishing_) {
        ball.age += seconds;
    }
    std::erase_if(vanishing_, [&](const Vanishing& ball) { return ball.age >= session.rules().ballVanishTime; });
    particles_.update(seconds);

    items_.clear();
    level_.appendDrawn(items_);
    sails_.appendDrawn(session.scene(), items_);
    prizes_.appendWall(session, items_);

    const BatPose bat = batPose(session.bat(), static_cast<float>(lead));
    if (bat.visible) {
        const float size = bat.scale * session.batScale();  // the prizes make it wider and narrower
        const Mat4 place = Mat4::scaling({size, size, size}) * Mat4::rotationY(bat.yaw) * Mat4::translation(bat.position);
        for (DrawItem item : bat_.items()) {
            item.world = item.world * place;
            items_.push_back(item);
        }
        prizes_.appendMagnet(session, place, items_);
        barrels_.append(place, items_);
    }

    std::erase_if(rolls_, [&](const auto& entry) {
        return std::ranges::none_of(session.balls(), [&](const BallView& ball) { return ball.id == entry.first; });
    });
    const BallLook look = PrizeScene::ballLook(session);
    const bool yoyo = session.effect(PrizeKind::YoYo) != nullptr;
    for (const BallView& ball : session.balls()) {
        const Vec3 at = ballPosition(ball, session.bat(), static_cast<float>(lead));
        Roll& roll = rolls_[ball.id];
        roll.size = ball.radius / kBallRadius;
        roll.look = look;
        const float scale = ballScale(ball, session.bat());
        roll.shown = scale;
        if (ball.held || ball.stuck) {
            roll.placed = false;
        } else {
            if (roll.placed) {
                // Rolling on the floor: turned about the horizontal axis across its path by the distance over its radius.
                const float dx = at.x - roll.at.x, dz = at.z - roll.at.z;
                const float distance = std::hypot(dx, dz);
                if (distance > 1e-5f && distance < kMostRolledPerFrame) {
                    roll.turn = roll.turn * Mat4::rotationAxis({dz / distance, 0, -dx / distance}, distance / ball.radius);
                }
            }
            roll.at = at;
            roll.placed = true;
            if (yoyo && bat.visible && !ball.away) {
                prizes_.appendString(bat.position, at, items_);
            }
        }
        if (scale > 0) {
            addBall(at, roll.turn, scale * roll.size, look);
        }
    }
    for (const Vanishing& ball : vanishing_) {
        const float left = 1 - static_cast<float>(ball.age / session.rules().ballVanishTime);
        addBall(ball.at + ball.velocity * static_cast<float>(ball.age), ball.turn, left * ball.size, ball.look);
    }
    items_.insert(items_.end(), effects_.items().begin(), effects_.items().end());
    items_.insert(items_.end(), weapons_.items().begin(), weapons_.items().end());
    prizes_.appendPrizes(session, lead, items_);

    night_ = session.nightLevel();
    if (night_ > 0) {
        dimUnlit(items_, night_);
    }
    batches_.build(particles_, view_, unlitNightShare(night_));
}

void PlayScene::addBall(Vec3 at, const Mat4& turn, float scale, BallLook look) {
    const Mat4 place = Mat4::scaling({scale, scale, scale}) * turn * Mat4::translation(at);
    for (DrawItem item : look == BallLook::Plain ? level_.ballItems() : prizes_.ballItems(look)) {
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

}  // namespace mb
