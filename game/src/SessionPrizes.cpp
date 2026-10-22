// The prizes of a GameSession: what drops from a destroyed object, how it falls and is caught, and what each of the 28 does
// (docs/GAMEPLAY.md sections 7, 14 and 15). The rest of the session only calls in at the places marked in Gameplay.cpp.

#include "mb/game/Gameplay.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mb {

namespace {

// The effects Normal Ball cancels: what changes the ball ("removes ball-changing effects": the Weak Ball icon vanished, V2 376.0
// to 376.7 [M]). The speed prizes are not among them: they have no icon and are the inverse of each other [I].
constexpr PrizeKind kBallEffects[] = {PrizeKind::BigBall, PrizeKind::SmallBall, PrizeKind::PowerBall,
                                      PrizeKind::ExplosiveBall, PrizeKind::Drunken, PrizeKind::YoYo};

// What the bat's loss does not end: the night and the shaking go on, they are the board's [I].
bool survivesBatLoss(PrizeKind kind) {
    return kind == PrizeKind::Night || kind == PrizeKind::Quake || kind == PrizeKind::Wind;
}

// Multiball puts its balls this much beyond touching the ball they come from, and no farther out than this (the walls are at
// +-20 at the ball's height) [I].
constexpr float kMultiballGap = 0.1f;
constexpr float kMultiballMostX = 19.0f;

bool sameName(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

// `heading` (a unit vector in the plane) turned by `angle`, positive towards +x as headingAt() has it.
Vec3 rotated(Vec3 heading, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {heading.x * c + heading.z * s, 0, heading.z * c - heading.x * s};
}

}  // namespace

void GameSession::initPrizes(const MaterialLibrary& materials) {
    for (int i = 0; i < kPrizeKindCount; ++i) {
        const std::string_view stem = prizeInfo(static_cast<PrizeKind>(i)).stem;
        // "meteor" names two materials, the projectile's (group temp, first in the file) and the prize's: go by group.
        const GameMaterial* found = nullptr;
        for (const GameMaterial& material : materials.all()) {
            if (material.group == "Prizes" && sameName(material.name, stem)) {
                found = &material;
                break;
            }
        }
        prizeMaterials_[static_cast<std::size_t>(i)] = found != nullptr ? found : materials.find(stem);
    }
}

// ---- What drops

void GameSession::maybeDropPrize(Vec3 at, const GameMaterial& killed, bool allowed, ActorIndex source) {
    const PrizeRules& rules = rules_.prizes;
    if (!rules.drops || !allowed || phase_ == SessionPhase::Complete || phase_ == SessionPhase::GameOver) {
        return;
    }
    const bool starFalling =
        std::ranges::any_of(fallingPrizes_, [](const FallingPrize& prize) { return prize.kind == PrizeKind::NextLevel; });
    if (levelEnd_ != TargetRule::Never && !starFalling) {
        const float chance = starChance(rules, targetsLeft(levelEnd_), targetsAtStart_[static_cast<int>(levelEnd_)]);
        if (chance > 0 && dropRandom_.uniform() < chance) {
            spawnPrize(PrizeKind::NextLevel, at, source);
            return;
        }
    }
    if (dropRandom_.uniform() >= dropChance(rules, killed.brickScore)) {
        return;
    }
    spawnPrize(pickPrize(dropRandom_.uniform(), campaignLevel_ >= rules.badFromLevel ? PrizePool::All : PrizePool::GoodOnly), at,
               source);
}

void GameSession::spawnPrize(PrizeKind kind, Vec3 at, ActorIndex source) {
    FallingPrize prize;
    prize.id = nextPrize_++;
    prize.kind = kind;
    prize.position = {at.x, rules_.prizes.height, at.z};
    prize.source = source;
    fallingPrizes_.push_back(prize);
    ++stats_.prizesDropped;

    GameEvent event;
    event.type = EventType::PrizeDropped;
    event.prize = kind;
    event.actor = source;
    event.position = prize.position;
    event.material = prizeMaterials_[static_cast<std::size_t>(kind)];
    emit(event);
}

int GameSession::dropPrize(PrizeKind kind, Vec3 from) {
    spawnPrize(kind, from, kNoActor);
    return fallingPrizes_.back().id;
}

// ---- Falling and catching

void GameSession::stepPrizes() {
    const PrizeRules& rules = rules_.prizes;
    const double epsilon = fixedStep_ * 1e-3;

    // The bat can catch while it is there: not as it swoops in, flies off or after the last ball was lost.
    const bool batHere = batBody_ != 0 && bat_.phase == BatPhase::Present && phase_ != SessionPhase::Lost;
    const float reach = batRadius() + rules.halfSize;
    for (std::size_t i = 0; i < fallingPrizes_.size();) {
        FallingPrize& prize = fallingPrizes_[i];
        prize.position.z -= rules.fallSpeed * static_cast<float>(fixedStep_);
        prize.age += fixedStep_;
        if (batHere && std::hypot(prize.position.x - bat_.position.x, prize.position.z - bat_.position.z) <= reach) {
            const FallingPrize caught = prize;
            fallingPrizes_.erase(fallingPrizes_.begin() + static_cast<std::ptrdiff_t>(i));
            catchPrize(caught);
            continue;
        }
        if (prize.position.z <= rules.missZ) {
            GameEvent event;
            event.type = EventType::PrizeMissed;
            event.prize = prize.kind;
            event.position = prize.position;
            event.material = prizeMaterials_[static_cast<std::size_t>(prize.kind)];
            emit(event);
            ++stats_.prizesMissed;
            fallingPrizes_.erase(fallingPrizes_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }

    for (std::size_t i = 0; i < effects_.size();) {
        ActiveEffect& active = effects_[i];
        if (active.duration > 0) {
            active.timeLeft -= fixedStep_;
            if (active.timeLeft <= epsilon) {
                stopEffect(active.kind);  // takes it out of the list
                continue;
            }
        }
        ++i;
    }

    if (ballsVanishAt_ >= 0 && time_ + epsilon >= ballsVanishAt_) {
        ballsVanishAt_ = -1;
        while (!balls_.empty()) {
            const Ball& ball = balls_.front();
            Vec3 at{bat_.position.x + (ball.stuck ? ball.stuckOffset.x : rules_.heldOffsetX), rules_.ballPlaneY,
                    bat_.position.z + (ball.stuck ? ball.stuckOffset.z : heldDistance())};
            Vec3 velocity{};
            if (ball.body != 0) {
                at = scene_.world().pose(ball.body).translationPart();
                velocity = scene_.world().linearVelocity(ball.body);
            }
            loseBall(0, at, velocity);
        }
    }
    if (effect(PrizeKind::Magnet) != nullptr) {
        releaseStuck(true);
    }
    if (effect(PrizeKind::Quake) != nullptr && time_ + epsilon >= nextQuake_) {
        nextQuake_ = time_ + rules.quakeEvery;
        shakeBodies();
    }
    if (effect(PrizeKind::Wind) != nullptr) {
        blowBodies();
    }
}

void GameSession::catchPrize(const FallingPrize& prize) {
    const GameMaterial* material = prizeMaterials_[static_cast<std::size_t>(prize.kind)];
    const int points = material != nullptr ? material->brickScore : 0;
    score_ += points;
    ++stats_.prizesCaught;

    GameEvent event;
    event.type = EventType::PrizeCaught;
    event.prize = prize.kind;
    event.position = prize.position;
    event.points = points;
    event.material = material;
    emit(event);
    givePrize(prize.kind);
}

// ---- What they do

void GameSession::givePrize(PrizeKind kind) {
    const PrizeRules& rules = rules_.prizes;
    if (phase_ == SessionPhase::Complete || phase_ == SessionPhase::GameOver) {
        hearts_ += kind == PrizeKind::Life ? 1 : 0;  // a catch during the fade: the score and the heart, nothing else
        return;
    }
    switch (kind) {
        case PrizeKind::Bonus: break;
        case PrizeKind::Life: ++hearts_; break;
        case PrizeKind::Skull: killBat(); break;
        case PrizeKind::NextLevel: forceLevelComplete(); break;
        case PrizeKind::SpeedUp:
        case PrizeKind::SlowDown:
            speedFactor_ = std::clamp(speedFactor_ * (kind == PrizeKind::SpeedUp ? rules.speedUp : rules.slowDown),
                                      rules.speedMin, rules.speedMax);
            for (const Ball& ball : balls_) {  // at once, not at the next step
                if (ball.body != 0) {
                    scene_.world().setLinearVelocity(ball.body, ball.heading * ballSpeed());
                }
            }
            break;
        case PrizeKind::IncBat:
        case PrizeKind::DecBat:
            batScale_ = std::clamp(kind == PrizeKind::IncBat ? batScale_ * rules.batStep : batScale_ / rules.batStep,
                                   rules.batScaleMin, rules.batScaleMax);
            refreshBat();
            break;
        case PrizeKind::BigBall:
            stopEffect(PrizeKind::SmallBall);
            startEffect(kind);
            break;
        case PrizeKind::SmallBall:
            stopEffect(PrizeKind::BigBall);
            startEffect(kind);
            break;
        case PrizeKind::NormalBall:
            for (const PrizeKind changing : kBallEffects) {
                stopEffect(changing);
            }
            break;
        case PrizeKind::Multiball: addMultiballs(); break;
        case PrizeKind::Thunderbolt:
        case PrizeKind::Meteor: {
            GameEvent event;
            event.type = EventType::StrikeGranted;
            event.prize = kind;
            emit(event);
            break;
        }
        case PrizeKind::Surprise: {
            // Nothing showed in the footage: an instant, random, good prize that is no Surprise or Next Level [?].
            const PrizeKind gift = pickPrize(effectRandom_.uniform(), PrizePool::Gift);
            GameEvent event;
            event.type = EventType::PrizeCaught;
            event.prize = gift;
            event.position = bat_.position;
            event.material = prizeMaterials_[static_cast<std::size_t>(gift)];
            emit(event);
            givePrize(gift);
            break;
        }
        case PrizeKind::PowerBall:
        case PrizeKind::ExplosiveBall:
        case PrizeKind::Drunken:
        case PrizeKind::Magnet:
        case PrizeKind::YoYo:
        case PrizeKind::Wall:
        case PrizeKind::Night:
        case PrizeKind::Quake:
        case PrizeKind::Wind:
        case PrizeKind::Gun:
        case PrizeKind::Cannon:
        case PrizeKind::Bazooka:
        case PrizeKind::Laser: startEffect(kind); break;
    }
    refreshViews();  // (between steps, what the presentation reads shows it at once)
}

const ActiveEffect* GameSession::effect(PrizeKind kind) const {
    const auto it = std::ranges::find(effects_, kind, &ActiveEffect::kind);
    return it != effects_.end() ? &*it : nullptr;
}

ActiveEffect* GameSession::findEffect(PrizeKind kind) {
    const auto it = std::ranges::find(effects_, kind, &ActiveEffect::kind);
    return it != effects_.end() ? &*it : nullptr;
}

void GameSession::startEffect(PrizeKind kind) {
    const PrizeInfo& info = prizeInfo(kind);
    const auto found = std::ranges::find(effects_, kind, &ActiveEffect::kind);
    if (found != effects_.end()) {
        found->timeLeft = info.seconds;
        found->shots += info.count;
        if (weaponOf(kind)) {  // the weapon caught last is the one a click fires
            std::rotate(found, found + 1, effects_.end());
        }
    } else {
        effects_.push_back({kind, info.seconds, info.seconds, info.count});
        GameEvent event;
        event.type = EventType::EffectStarted;
        event.prize = kind;
        emit(event);
        if (kind == PrizeKind::Wind) {
            windDirection_ = effectRandom_.uniform() < 0.5 ? -1.0f : 1.0f;
        } else if (kind == PrizeKind::Quake) {
            nextQuake_ = time_;
        }
    }
    if (kind == PrizeKind::BigBall || kind == PrizeKind::SmallBall) {
        refreshBalls();
    }
}

void GameSession::stopEffect(PrizeKind kind) {
    const auto it = std::ranges::find(effects_, kind, &ActiveEffect::kind);
    if (it == effects_.end()) {
        return;
    }
    effects_.erase(it);
    GameEvent event;
    event.type = EventType::EffectEnded;
    event.prize = kind;
    emit(event);
    if (kind == PrizeKind::BigBall || kind == PrizeKind::SmallBall) {
        refreshBalls();
    } else if (kind == PrizeKind::Magnet) {
        releaseStuck(false);
    }
}

// The effects of the bat and the ball go with a bat that is lost; the next one comes plain (enterBat).
void GameSession::loseEffects() {
    std::vector<PrizeKind> ended;
    for (const ActiveEffect& active : effects_) {
        if (!survivesBatLoss(active.kind)) {
            ended.push_back(active.kind);
        }
    }
    for (const PrizeKind kind : ended) {
        stopEffect(kind);
    }
}

void GameSession::killBat() {
    if (phase_ != SessionPhase::Ready && phase_ != SessionPhase::Playing) {
        return;
    }
    phase_ = SessionPhase::Lost;
    life_ = Life::BatStays;
    lostAt_ = time_ - rules_.prizes.skullLead;
    ballsVanishAt_ = time_ + rules_.prizes.skullBallsVanish;
    loseEffects();
}

float GameSession::nightLevel() const {
    const ActiveEffect* night = effect(PrizeKind::Night);
    if (night == nullptr) {
        return 0;
    }
    const PrizeRules& rules = rules_.prizes;
    const double rising = (night->duration - night->timeLeft) / rules.nightRampIn;
    const double falling = night->timeLeft / rules.nightRampOut;
    return static_cast<float>(std::clamp(std::min(rising, falling), 0.0, 1.0));
}

// ---- Weapons

std::optional<WeaponState> GameSession::weapon() const {
    for (auto it = effects_.rbegin(); it != effects_.rend(); ++it) {
        if (const std::optional<WeaponType> type = weaponOf(it->kind); type && it->shots > 0) {
            return WeaponState{*type, it->shots};
        }
    }
    return std::nullopt;
}

int GameSession::shotsLeft(WeaponType type) const {
    const ActiveEffect* armed = effect(prizeOf(type));
    return armed != nullptr ? armed->shots : 0;
}

bool GameSession::useShot() {
    const std::optional<WeaponState> current = weapon();
    if (!current) {
        return false;
    }
    ActiveEffect* armed = findEffect(prizeOf(current->type));
    if (--armed->shots <= 0) {
        stopEffect(prizeOf(current->type));
    }
    return true;
}

// ---- The bat and the ball

float GameSession::batLimit() const {
    return std::min(rules_.batLimitX, rules_.prizes.batEdge - batRadius());
}

// The held ball sits just in front of the bat, wide or narrow, big or small.
float GameSession::heldDistance() const {
    return rules_.heldOffsetZ + (batRadius() - rules_.batRadius) + (ballRadius_ - kBallRadius);
}

void GameSession::refreshBat() {
    if (batBody_ == 0) {
        return;
    }
    const BodyId body = scene_.addBat(scene_.world().pose(batBody_).translationPart(), batRadius());
    if (body != 0) {
        scene_.world().removeBody(batBody_);
        batBody_ = body;
    }
}

void GameSession::refreshBalls() {
    const PrizeRules& rules = rules_.prizes;
    ballRadius_ = effect(PrizeKind::BigBall) != nullptr
                      ? rules.bigBallRadius
                      : (effect(PrizeKind::SmallBall) != nullptr ? rules.smallBallRadius : kBallRadius);
    for (Ball& ball : balls_) {
        if (ball.radius == ballRadius_) {
            continue;
        }
        if (ball.body != 0) {
            const BodyId body = scene_.addGameBall(scene_.world().pose(ball.body).translationPart(),
                                                   scene_.world().linearVelocity(ball.body), ballRadius_);
            if (body == 0) {
                continue;
            }
            scene_.world().removeBody(ball.body);
            ball.body = body;
        }
        ball.radius = ballRadius_;
    }
}

// What the ball does besides what the bounces make it do, once a step.
void GameSession::steerBall(Ball& ball, Vec3 at) {
    const PrizeRules& rules = rules_.prizes;
    if (effect(PrizeKind::Drunken) != nullptr) {
        const double wobble =
            std::sin(2 * static_cast<double>(kPi) * time_ / rules.drunkenPeriod + static_cast<double>(ball.phase));
        ball.heading = rotated(ball.heading, radians(rules.drunkenRate) * static_cast<float>(wobble * fixedStep_));
    }
    if (effect(PrizeKind::YoYo) != nullptr && batBody_ != 0) {
        // The string is taut at its length: a ball going further out is turned back like a ball off a wall round the bat.
        const Vec3 out{at.x - bat_.position.x, 0, at.z - bat_.position.z};
        const float distance = length(out);
        if (distance > rules.yoyoLength) {
            const Vec3 radial = out / distance;
            const float away = dot(ball.heading, radial);
            if (away > 0) {
                ball.heading = ball.heading - radial * (2 * away);
                GameEvent event;
                event.type = EventType::BallRubbered;
                event.ball = ball.id;
                event.position = at;
                emit(event);
            }
        }
    }
    if (effect(PrizeKind::Wall) != nullptr && ball.heading.z < 0 && std::fabs(at.x) < rules.wallHalfWidth + ball.radius &&
        at.z < rules.wallZ + rules.wallHalfDepth + ball.radius) {
        ball.heading.z = -ball.heading.z;
        ball.bounced = true;
        GameEvent event;
        event.type = EventType::BoardHit;
        event.ball = ball.id;
        event.position = at;
        event.speed = ballSpeed();
        emit(event);
    }
}

// ---- The Magnet

bool GameSession::stickBall(Ball& ball, Vec3 at) {
    if (effect(PrizeKind::Magnet) == nullptr || ball.stuck || batBody_ == 0) {
        return false;
    }
    ball.stuck = true;
    ball.stuckAt = time_;
    ball.stuckOffset = {at.x - bat_.position.x, 0, at.z - bat_.position.z};
    scene_.world().removeBody(ball.body);
    ball.body = 0;
    GameEvent event;
    event.type = EventType::BallStuck;
    event.ball = ball.id;
    event.position = at;
    emit(event);
    return true;
}

// Lets go of the balls the Magnet holds: all of them, or those held for magnetHold seconds. A ball leaves along the line from
// the bat's centre to where it stuck, as off the bat (hitBat()), and does not stick again until it has left the bat.
void GameSession::releaseStuck(bool onlyIfHeldLong) {
    const float most = radians(rules_.batMaxExitDegrees);
    for (Ball& ball : balls_) {
        if (!ball.stuck || (onlyIfHeldLong && time_ - ball.stuckAt < rules_.prizes.magnetHold)) {
            continue;
        }
        const Vec3 heading = rotated({0, 0, 1}, std::clamp(std::atan2(ball.stuckOffset.x, ball.stuckOffset.z), -most, most));
        const Vec3 at{bat_.position.x + ball.stuckOffset.x, rules_.ballPlaneY, bat_.position.z + ball.stuckOffset.z};
        const BodyId body = scene_.addGameBall(at, heading * ballSpeed(), ball.radius);
        if (body == 0) {
            continue;
        }
        ball.body = body;
        ball.stuck = false;
        ball.onBat = true;
        ball.heading = heading;
        ball.anchor = at;
        ball.anchoredAt = time_;
        GameEvent event;
        event.type = EventType::BallLaunched;
        event.ball = ball.id;
        event.position = at;
        emit(event);
    }
}

// ---- Balls and bricks

void GameSession::ballHitBrick(Vec3 at) {
    ActiveEffect* charge = findEffect(PrizeKind::ExplosiveBall);
    if (charge == nullptr) {
        return;
    }
    if (--charge->shots <= 0) {
        stopEffect(PrizeKind::ExplosiveBall);
    }
    blast(at);
}

// One hit on everything damageable within blastRadius of `at` (the distance to the box round it), but what the ball hit.
void GameSession::blast(Vec3 at) {
    const float radius = rules_.prizes.blastRadius;
    GameEvent event;
    event.type = EventType::Blast;
    event.position = at;
    event.speed = radius;
    emit(event);
    for (ActorIndex actor = 0; actor < bricks_.size(); ++actor) {
        const bool hitAlready = std::ranges::find(hitThisStep_, actor) != hitThisStep_.end();
        if (!bricks_[actor].damageable || bricks_[actor].removed || hitAlready) {
            continue;
        }
        const Bounds box = boundsOf(scene_.plan().actors[actor].body, scene_.actorPose(actor));
        const Vec3 nearest{std::clamp(at.x, box.low.x, box.high.x), std::clamp(at.y, box.low.y, box.high.y),
                           std::clamp(at.z, box.low.z, box.high.z)};
        if (length(nearest - at) <= radius) {
            hitBrick(actor, at, 0, -1);
        }
    }
}

void GameSession::addMultiballs() {
    const PrizeRules& rules = rules_.prizes;
    const Ball* source = nullptr;
    float nearest = 0;
    for (const Ball& ball : balls_) {
        if (ball.held || ball.stuck) {
            continue;
        }
        const Vec3 at = scene_.world().pose(ball.body).translationPart();
        const float distance = std::hypot(at.x - bat_.position.x, at.z - bat_.position.z);
        if (source == nullptr || distance < nearest) {
            source = &ball;
            nearest = distance;
        }
    }
    if (source == nullptr) {  // nothing in flight to take balls from
        return;
    }
    const Vec3 from = scene_.world().pose(source->body).translationPart(), heading = source->heading;
    const float gap = source->radius + ballRadius_ + kMultiballGap;
    for (const float turn : rules.multiballTurns) {
        if (static_cast<int>(balls_.size()) >= rules.maxBalls) {
            break;
        }
        const Vec3 direction = rotated(heading, radians(turn));
        Vec3 at = from + direction * gap;
        at.x = std::clamp(at.x, -kMultiballMostX, kMultiballMostX);
        addBall(at, direction);  // (`source` is no good after this)
    }
}

// ---- The board

void GameSession::shakeBodies() {
    const PrizeRules& rules = rules_.prizes;
    for (const ActorIndex actor : dynamicActors_) {
        const BodyId body = scene_.body(actor);
        if (body == 0) {
            continue;
        }
        const Vec3 kick{effectRandom_.between(-rules.quakeKick, rules.quakeKick), effectRandom_.between(0, rules.quakeJump),
                        effectRandom_.between(-rules.quakeKick, rules.quakeKick)};
        scene_.world().wake(body);
        scene_.world().setLinearVelocity(body, scene_.world().linearVelocity(body) + kick);
    }
}

void GameSession::blowBodies() {
    const float push = windDirection_ * rules_.prizes.windAccel * static_cast<float>(fixedStep_);
    for (const ActorIndex actor : dynamicActors_) {
        const BodyId body = scene_.body(actor);
        if (body == 0 || !scene_.world().isAwake(body)) {
            continue;
        }
        scene_.world().setLinearVelocity(body, scene_.world().linearVelocity(body) + Vec3{push, 0, 0});
    }
}

}  // namespace mb
