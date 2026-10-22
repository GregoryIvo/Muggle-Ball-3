#include "mb/game/Gameplay.h"

#include "mb/engine/Log.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace mb {

namespace {

constexpr std::uint32_t kAllFrozen = Body::kFrozenPosX | Body::kFrozenPosY | Body::kFrozenPosZ | Body::kFrozenRotX |
                                     Body::kFrozenRotY | Body::kFrozenRotZ;

// Bodies are looked at for having left the board this often (in steps): a body falls for a while before it is a
// concern, and the look costs one pose per dynamic body.
constexpr std::uint64_t kFallCheckEvery = 6;
// A ball farther than this beyond the bat's reach is no longer on it: the next touch is a new hit. A bat on the run
// pushes the ball ahead of it, so what the bat covers in a step is added.
constexpr float kBatLeaveSlack = 0.3f;
// The x part of the path a freed ball gets, on a path of z part 1: away from the wall it was pinned on, slightly.
constexpr float kFreedSlant = 0.3f;

constexpr int kRuleCount = 2;  // the TargetRules that name targets
// How often (in steps) the targets that are loose bodies are looked at again for being out of the ball's reach.
constexpr std::uint64_t kReachCheckEvery = 30;

// Events nobody collected; a game that never calls takeEvents() must not eat the memory.
constexpr std::size_t kMostQueuedEvents = 1u << 14;

std::string_view eventName(EventType type) {
    switch (type) {
        case EventType::BallLaunched: return "BallLaunched";
        case EventType::BatHit: return "BatHit";
        case EventType::BoardHit: return "BoardHit";
        case EventType::BallsHit: return "BallsHit";
        case EventType::BrickHit: return "BrickHit";
        case EventType::BrickDestroyed: return "BrickDestroyed";
        case EventType::ActorFell: return "ActorFell";
        case EventType::BallLost: return "BallLost";
        case EventType::BallFreed: return "BallFreed";
        case EventType::LifeLost: return "LifeLost";
        case EventType::BatLeft: return "BatLeft";
        case EventType::BatEntered: return "BatEntered";
        case EventType::LevelComplete: return "LevelComplete";
        case EventType::GameOver: return "GameOver";
    }
    return "?";
}

// A path nearer the x axis than asin(sinFlat), or the z axis than asin(sinSteep), turned out to that angle: to the side
// it was going in x and in z, and towards the middle of the board (from `atX`) if it was going to none.
Vec3 turnedOut(Vec3 heading, float atX, float sinFlat, float sinSteep) {
    // A component this small is rounding left by the bounce, and its sign differs between CPUs: it is no side.
    constexpr float kNoSide = 1e-3f;
    const float side = std::fabs(heading.x) > kNoSide ? heading.x : (atX > 0 ? -1.0f : 1.0f);
    const float forward = heading.z < -kNoSide ? -1.0f : 1.0f;
    if (std::fabs(heading.z) < sinFlat) {
        return {std::copysign(std::sqrt(1 - sinFlat * sinFlat), side), 0, forward * sinFlat};
    }
    if (std::fabs(heading.x) < sinSteep) {
        return {std::copysign(sinSteep, side), 0, forward * std::sqrt(1 - sinSteep * sinSteep)};
    }
    return heading;
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// The unit direction in the plane at `angle` from +z, positive towards +x.
Vec3 headingAt(float angle) {
    return {std::sin(angle), 0, std::cos(angle)};
}

}  // namespace

std::string describe(const GameEvent& event) {
    std::string text = std::format("{:8.3f}  {}", event.time, eventName(event.type));
    const auto add = [&](const char* label, auto value) { text += std::format(" {}={}", label, value); };
    if (event.ball >= 0) {
        add("ball", event.ball);
    }
    if (event.otherBall >= 0) {
        add("other", event.otherBall);
    }
    if (event.actor != kNoActor) {
        add("actor", event.actor);
    }
    switch (event.type) {
        case EventType::BrickHit: add("energy", event.energy); break;
        case EventType::BrickDestroyed: add("points", event.points); break;
        case EventType::LifeLost:
        case EventType::GameOver: add("hearts", event.hearts); break;
        default: break;
    }
    if (event.speed != 0) {
        text += std::format(" speed={:.1f}", static_cast<double>(event.speed));
    }
    if (event.material != nullptr) {
        add("material", event.material->name);
    }
    text += std::format(" at ({:.2f}, {:.2f}, {:.2f})", static_cast<double>(event.position.x),
                        static_cast<double>(event.position.y), static_cast<double>(event.position.z));
    return text;
}

bool canBeDamaged(ObjectType type, const GameMaterial* material) {
    switch (type) {
        case ObjectType::Brick:
        case ObjectType::Tree:
        case ObjectType::Cannon:
        case ObjectType::Firework:
        case ObjectType::Spring:
        case ObjectType::Volcano:
        case ObjectType::TeleportSrc:
        case ObjectType::TeleportDest:
        case ObjectType::GeneratorGood:
        case ObjectType::GeneratorBad:
        case ObjectType::Vibro:
        case ObjectType::Flicker:
        case ObjectType::Metall:
        case ObjectType::Dynamite:
            return material != nullptr && material->brickType != 2 && material->brickEnergy > 0;
        default:
            return false;
    }
}

bool isTarget(TargetRule rule, ObjectType type, bool dynamic, const GameMaterial* material) {
    if (!canBeDamaged(type, material)) {
        return false;
    }
    switch (rule) {
        case TargetRule::DynamicBricks:
            return dynamic && (type == ObjectType::Tree ||
                               (type == ObjectType::Brick && (material->brickType == 0 || material->brickType == 4)));
        case TargetRule::BrickType: return type == ObjectType::Brick && material->brickType == 0;
        case TargetRule::Never: return false;
    }
    return false;
}

int damageTextureIndex(const GameMaterial& material, int energy) {
    if (material.animated || material.textures.size() < 2) {
        return 0;
    }
    return std::clamp(energy - 1, 0, static_cast<int>(material.textures.size()) - 1);
}

std::optional<Vec3> cursorOnPlane(float cursorX, float cursorY, float width, float height, const Mat4& view,
                                  const Mat4& projection, float planeY) {
    if (!(width > 0) || !(height > 0)) {
        return std::nullopt;
    }
    const float ndcX = 2 * cursorX / width - 1, ndcY = 1 - 2 * cursorY / height;
    const Mat4 toWorld = inverse(view * projection);
    const auto unproject = [&](float depth) {
        const Vec4 p = transform(Vec4{ndcX, ndcY, depth, 1}, toWorld);
        return Vec3{p.x / p.w, p.y / p.w, p.z / p.w};
    };
    const Vec3 near = unproject(0), far = unproject(1);
    const Vec3 direction = far - near;
    if (!(std::fabs(direction.y) > 1e-6f)) {
        return std::nullopt;
    }
    const float t = (planeY - near.y) / direction.y;
    if (!(t >= 0) || !std::isfinite(t)) {
        return std::nullopt;
    }
    return near + direction * t;
}

std::optional<float> batTargetX(float cursorX, float cursorY, float width, float height, const Camera& camera,
                                float planeY) {
    if (!(width > 0) || !(height > 0)) {
        return std::nullopt;
    }
    const std::optional<Vec3> point =
        cursorOnPlane(cursorX, cursorY, width, height, camera.view(), camera.projection(width / height), planeY);
    if (!point) {
        return std::nullopt;
    }
    return point->x;
}

GameSession::GameSession(const Level& level, const MaterialLibrary& materials, const SessionOptions& options)
    : rules_(options.rules),
      levelEnd_(options.levelEnd),
      scene_(level, materials, options.physics, options.physicsSettings),
      fixedStep_(scene_.world().settings().fixedStep),
      score_(options.score),
      hearts_(options.hearts.value_or(options.rules.startHearts)),
      readyAt_(options.rules.startReadyDelay) {
    const std::vector<ActorRef> refs = flattenActors(level);
    bricks_.resize(refs.size());
    for (const ActorRef& ref : refs) {
        if (scene_.body(ref.index) == 0) {
            continue;
        }
        if (scene_.plan().actors[ref.index].motion == MotionType::Dynamic) {
            dynamicActors_.push_back(ref.index);
        }
        if (ref.owner == nullptr) {
            continue;
        }
        Brick& brick = bricks_[ref.index];
        brick.type = ref.owner->type;
        brick.material = ref.owner->material.empty() ? nullptr : materials.find(ref.owner->material);
        const std::optional<Body>& body = ref.actor->body;
        brick.dynamic = body && !body->kinematic() && (body->flags & kAllFrozen) != kAllFrozen;
        brick.damageable = canBeDamaged(brick.type, brick.material);
        if (brick.damageable) {
            brick.energy = brick.maxEnergy = brick.material->brickEnergy;
        }
        for (int rule = 0; rule < kRuleCount; ++rule) {
            if (mb::isTarget(static_cast<TargetRule>(rule), brick.type, brick.dynamic, brick.material)) {
                brick.targetFor |= 1u << rule;
                ++targetsLeft_[rule];
            }
        }
    }
    std::copy(std::begin(targetsLeft_), std::end(targetsLeft_), std::begin(targetsAtStart_));
    if (options.ignoreUnreachable) {
        // Below the board's near edge the ball is gone, so the ground of its plane starts there: what is beyond the side
        // walls is joined to the board only by that strip, which no ball goes along.
        BallReach::Options reachOptions;
        reachOptions.planeY = rules_.ballPlaneY;
        reachOptions.radius = kBallRadius;
        reachOptions.cell = rules_.reachCell;
        reachOptions.low = {-rules_.escapeX, 0, rules_.ballLostZ + 1.0f};
        reachOptions.high = {rules_.escapeX, 0, rules_.escapeZ};
        reach_.emplace(
            scene_.plan(), [this](ActorIndex actor) { return !bricks_[actor].damageable; },
            Vec3{0, rules_.ballPlaneY, rules_.batZ + rules_.heldOffsetZ}, reachOptions);
        for (const ActorRef& ref : refs) {
            if (bricks_[ref.index].targetFor != 0 && scene_.plan().actors[ref.index].motion == MotionType::Dynamic) {
                loose_.push_back(ref.index);
            }
        }
        releaseTargets(false);
    }
    enterBat();
    refreshViews();
}

void GameSession::update(double seconds, const GameInput& input) {
    if (input.batTargetX && std::isfinite(*input.batTargetX)) {
        targetX_ = std::clamp(*input.batTargetX, -rules_.batLimitX, rules_.batLimitX);
    }
    launchPending_ = launchPending_ || input.launch;
    if (!(seconds > 0) || !std::isfinite(seconds)) {
        return;
    }
    // Like PhysicsWorld::step: a step that falls short of the fixed one by rounding only still happens, and a long
    // pause is not caught up.
    accumulator_ += seconds;
    const double epsilon = fixedStep_ * 1e-5;
    const int most = scene_.world().settings().maxStepsPerCall;
    for (int ticks = 0; accumulator_ + epsilon >= fixedStep_ && ticks < most; ++ticks) {
        tick();
        accumulator_ = std::max(0.0, accumulator_ - fixedStep_);
    }
    if (accumulator_ + epsilon >= fixedStep_) {
        accumulator_ = std::fmod(accumulator_, fixedStep_);
    }
}

std::vector<GameEvent> GameSession::takeEvents() {
    std::vector<GameEvent> taken;
    taken.swap(events_);
    eventsDropped_ = false;
    return taken;
}

ActorStatus GameSession::actor(ActorIndex actor) const {
    ActorStatus status;
    if (actor >= bricks_.size()) {
        return status;
    }
    const Brick& brick = bricks_[actor];
    status.damageable = brick.damageable && !brick.removed;
    status.exploding = brick.exploding;
    status.removed = brick.removed;
    status.energy = brick.energy;
    status.maxEnergy = brick.maxEnergy;
    status.textureIndex = brick.damageable ? damageTextureIndex(*brick.material, brick.energy) : 0;
    status.target = isTarget(actor, levelEnd_);
    status.outOfPlay = brick.outOfPlay;
    return status;
}

bool GameSession::isTarget(ActorIndex actor, TargetRule rule) const {
    return rule != TargetRule::Never && actor < bricks_.size() && !bricks_[actor].removed && !bricks_[actor].outOfPlay &&
           (bricks_[actor].targetFor & (1u << static_cast<int>(rule))) != 0;
}

int GameSession::targetsLeft(TargetRule rule) const {
    return rule == TargetRule::Never ? 0 : targetsLeft_[static_cast<int>(rule)];
}

int GameSession::addBall(Vec3 position, Vec3 direction) {
    position.y = rules_.ballPlaneY;
    const Vec3 heading = normalize({direction.x, 0, direction.z});
    const int id = newBall(position, heading.x == 0 && heading.z == 0 ? Vec3{0, 0, 1} : heading, false);
    if (id < 0) {
        return id;
    }
    GameEvent event;
    event.type = EventType::BallLaunched;
    event.ball = id;
    event.position = position;
    emit(event);
    ++stats_.ballsLaunched;
    refreshViews();
    return id;
}

void GameSession::forceLevelComplete() {
    if (phase_ != SessionPhase::Complete && phase_ != SessionPhase::GameOver) {
        complete();
    }
}

// ---- One fixed step

void GameSession::tick() {
    time_ += fixedStep_;
    ++ticks_;
    const bool launch = std::exchange(launchPending_, false);

    moveBat();
    if (launch && phase_ == SessionPhase::Ready) {
        launchHeldBall();
    }
    scene_.step(static_cast<float>(fixedStep_));
    processContacts();
    keepBalls();
    explodeDue();
    if (ticks_ % kFallCheckEvery == 0) {
        removeFallen();
    }
    if (reach_ && ticks_ % kReachCheckEvery == 0) {
        releaseTargets(true);
    }
    if (phase_ == SessionPhase::Playing) {
        stalled_ += fixedStep_;
    }
    letGoOfStalled();
    runLifeSequence();
    checkComplete();
    refreshViews();
}

void GameSession::moveBat() {
    if (batBody_ == 0) {  // flying off, or gone
        return;
    }
    const float limit = rules_.batLimitX;
    bool teleport = snapBat_;
    if (snapBat_) {
        batX_ = targetX_;
        batVelocity_ = 0;
        snapBat_ = false;
    } else {
        // The exact step of a critically damped spring (rate batResponse) towards the target.
        const float rate = rules_.batResponse, dt = static_cast<float>(fixedStep_);
        const float decay = std::exp(-rate * dt);
        const float away = batX_ - targetX_;
        const float carried = (batVelocity_ + rate * away) * dt;
        batVelocity_ = (batVelocity_ - rate * carried) * decay;
        batX_ = targetX_ + (away + carried) * decay;
    }
    if (std::fabs(batX_) > limit) {
        batX_ = std::clamp(batX_, -limit, limit);
        batVelocity_ = 0;
    }
    const Mat4 pose = Mat4::translation({batX_, rules_.ballPlaneY, rules_.batZ});
    if (teleport) {
        scene_.world().setPose(batBody_, pose);
    } else {
        scene_.world().setKinematicTarget(batBody_, pose);
    }
    bat_.position = pose.translationPart();
    bat_.velocityX = batVelocity_;
}

void GameSession::enterBat() {
    if (batBody_ == 0) {
        batBody_ = scene_.addBat({0, rules_.ballPlaneY, rules_.batZ}, rules_.batRadius);
    }
    snapBat_ = true;  // the new bat appears under the cursor
    bat_.position = {targetX_, rules_.ballPlaneY, rules_.batZ};
    bat_.velocityX = 0;
    setBatPhase(BatPhase::Entering, readyAt_ - time_);
    phase_ = SessionPhase::Entering;
    newBall({}, {0, 0, 1}, true);

    GameEvent event;
    event.type = EventType::BatEntered;
    event.position = bat_.position;
    emit(event);
}

void GameSession::setBatPhase(BatPhase phase, double duration) {
    bat_.phase = phase;
    bat_.phaseDuration = duration;
    batPhaseStart_ = time_;
}

int GameSession::newBall(Vec3 position, Vec3 heading, bool held) {
    Ball ball;
    ball.held = held;
    ball.heading = heading;
    ball.anchor = position;
    ball.anchoredAt = time_;
    if (!held) {
        ball.body = scene_.addGameBall(position, heading * rules_.ballSpeed);
        if (ball.body == 0) {
            return -1;
        }
    }
    ball.id = nextBall_++;
    balls_.push_back(ball);
    return ball.id;
}

void GameSession::launchHeldBall() {
    const auto held = std::ranges::find_if(balls_, [](const Ball& ball) { return ball.held; });
    if (held == balls_.end()) {
        return;
    }
    const Vec3 position{bat_.position.x + rules_.heldOffsetX, rules_.ballPlaneY, bat_.position.z + rules_.heldOffsetZ};
    const Vec3 heading = headingAt(-radians(rules_.launchAngleDegrees));
    const BodyId body = scene_.addGameBall(position, heading * rules_.ballSpeed);
    if (body == 0) {  // the click is lost; the ball waits for the next one
        return;
    }
    held->heading = heading;
    held->body = body;
    held->held = false;
    held->anchor = position;
    held->anchoredAt = time_;
    phase_ = SessionPhase::Playing;
    ++stats_.ballsLaunched;

    GameEvent event;
    event.type = EventType::BallLaunched;
    event.ball = held->id;
    event.position = position;
    emit(event);
}

GameSession::Ball* GameSession::ballOf(BodyId body) {
    const auto it = std::ranges::find(balls_, body, &Ball::body);
    return it != balls_.end() ? &*it : nullptr;
}

void GameSession::processContacts() {
    hitThisStep_.clear();
    for (const ContactEvent& contact : scene_.contacts()) {
        Ball* a = contact.userDataA == kGameBallData ? ballOf(contact.a) : nullptr;
        Ball* b = contact.userDataB == kGameBallData ? ballOf(contact.b) : nullptr;
        const bool blow = contact.normalSpeed >= rules_.hitFraction * rules_.ballSpeed;
        if (a != nullptr && b != nullptr) {
            if (blow) {
                a->bounced = b->bounced = true;
                GameEvent event;
                event.type = EventType::BallsHit;
                event.ball = a->id;
                event.otherBall = b->id;
                event.position = contact.point;
                event.speed = contact.normalSpeed;
                emit(event);
            }
            continue;
        }
        Ball* ball = a != nullptr ? a : b;
        if (ball == nullptr) {
            continue;
        }
        const std::uint64_t other = a != nullptr ? contact.userDataB : contact.userDataA;
        if (other == kBatData) {
            hitBat(*ball);
            continue;
        }
        if (blow) {
            ball->bounced = true;
        }
        if (other < bricks_.size() && blow) {
            const auto actor = static_cast<ActorIndex>(other);
            if (scene_.plan().actors[actor].type == ObjectType::Board) {
                GameEvent event;
                event.type = EventType::BoardHit;
                event.ball = ball->id;
                event.actor = actor;
                event.position = contact.point;
                event.speed = contact.normalSpeed;
                emit(event);
            } else if (bricks_[actor].damageable && std::ranges::find(hitThisStep_, actor) == hitThisStep_.end()) {
                hitThisStep_.push_back(actor);
                hitBrick(actor, contact.point, contact.normalSpeed, ball->id);
            }
        }
    }
}

void GameSession::hitBat(Ball& ball) {
    const Vec3 at = scene_.world().pose(ball.body).translationPart();
    const Vec3 bat = scene_.world().pose(batBody_).translationPart();
    // Away from the bat's centre, whatever the ball arrived with, but never back: held to batMaxExitDegrees from +z.
    const float dx = at.x - bat.x, dz = at.z - bat.z;
    const float angle = (dx == 0 && dz == 0) ? 0.0f : std::atan2(dx, dz);
    const float most = radians(rules_.batMaxExitDegrees);
    ball.heading = headingAt(std::clamp(angle, -most, most));
    scene_.world().setLinearVelocity(ball.body, ball.heading * rules_.ballSpeed);
    if (!ball.onBat) {
        ball.onBat = true;
        ++stats_.batHits;
        GameEvent event;
        event.type = EventType::BatHit;
        event.ball = ball.id;
        event.position = at;
        event.speed = batVelocity_;
        emit(event);
    }
}

bool GameSession::hit(ActorIndex actor, Vec3 at) {
    return actor < bricks_.size() && hitBrick(actor, at, 0, -1);
}

bool GameSession::hitBrick(ActorIndex actor, Vec3 at, float speed, int ball) {
    Brick& brick = bricks_[actor];
    if (!brick.damageable || brick.removed || brick.exploding) {
        return false;
    }
    --brick.energy;
    ++stats_.bricksHit;
    stalled_ = 0;
    GameEvent event;
    event.type = EventType::BrickHit;
    event.ball = ball;
    event.actor = actor;
    event.position = at;
    event.speed = speed;
    event.energy = brick.energy;
    event.material = brick.material;
    emit(event);
    if (brick.energy <= 0) {
        brick.exploding = true;
        brick.explodeAt = time_ + brick.material->exploseDelay / 1000.0;
        exploding_.push_back(actor);
    }
    return true;
}

void GameSession::keepBalls() {
    const float sinFlat = std::sin(radians(rules_.minPlanarAngleDegrees));
    const float sinSteep = std::sin(radians(rules_.minVerticalAngleDegrees));
    const float reach = rules_.batRadius + kBallRadius + kBatLeaveSlack +
                        std::fabs(batVelocity_) * static_cast<float>(fixedStep_);
    for (std::size_t i = 0; i < balls_.size();) {
        Ball& ball = balls_[i];
        if (ball.held) {
            ++i;
            continue;
        }
        const Vec3 at = scene_.world().pose(ball.body).translationPart();
        if (!finite(at) || std::fabs(at.x) > rules_.escapeX || at.z > rules_.escapeZ || at.z < rules_.ballLostZ) {
            loseBall(i, at, scene_.world().linearVelocity(ball.body));
            continue;
        }
        if (std::hypot(at.x - ball.anchor.x, at.z - ball.anchor.z) > rules_.jamDistance) {
            ball.anchor = at;
            ball.anchoredAt = time_;
        } else if (time_ - ball.anchoredAt >= rules_.jamTime && !freeBall(ball, at)) {
            loseBall(i, at, {});
            continue;
        }
        const Vec3 velocity = scene_.world().linearVelocity(ball.body);
        const float speed = std::hypot(velocity.x, velocity.z);
        if (speed > 1e-3f) {  // (a ball that stands still keeps the way it was going)
            ball.heading = {velocity.x / speed, 0, velocity.z / speed};
        }
        if (std::exchange(ball.bounced, false)) {
            ball.heading = turnedOut(ball.heading, at.x, sinFlat, sinSteep);
        }
        scene_.world().setLinearVelocity(ball.body, ball.heading * rules_.ballSpeed);
        if (ball.onBat && batBody_ != 0) {
            const Vec3 bat = scene_.world().pose(batBody_).translationPart();
            ball.onBat = std::hypot(at.x - bat.x, at.z - bat.z) <= reach;
        } else {
            ball.onBat = false;
        }
        ++i;
    }
}

bool GameSession::freeBall(Ball& ball, Vec3 at) {
    // Towards the middle of the board: away from the wall it was against, and from the end it was pinned at.
    ball.heading = normalize({at.x > 0 ? -kFreedSlant : kFreedSlant, 0, at.z > 0 ? -1.0f : 1.0f});
    scene_.world().removeBody(ball.body);
    ball.body = scene_.addGameBall(at, ball.heading * rules_.ballSpeed);
    ball.bounced = false;
    ball.anchor = at;
    ball.anchoredAt = time_;
    if (ball.body == 0) {
        return false;
    }
    ++stats_.ballsFreed;
    GameEvent event;
    event.type = EventType::BallFreed;
    event.ball = ball.id;
    event.position = at;
    emit(event);
    return true;
}

// The ball's body is gone already when `ball.body` is 0 (freeBall() could not make a new one).
void GameSession::loseBall(std::size_t index, Vec3 position, Vec3 velocity) {
    const Ball ball = balls_[index];
    GameEvent event;
    event.type = EventType::BallLost;
    event.ball = ball.id;
    event.position = position;
    event.velocity = velocity;
    emit(event);
    if (ball.body != 0) {
        scene_.world().removeBody(ball.body);
    }
    balls_.erase(balls_.begin() + static_cast<std::ptrdiff_t>(index));
    ++stats_.ballsLost;

    if (balls_.empty() && phase_ == SessionPhase::Playing) {
        phase_ = SessionPhase::Lost;
        life_ = Life::BatStays;
        lostAt_ = time_;
    }
}

void GameSession::runLifeSequence() {
    if (phase_ == SessionPhase::Entering && time_ >= readyAt_) {
        phase_ = SessionPhase::Ready;
        setBatPhase(BatPhase::Present);
    }
    if (life_ == Life::None) {
        return;
    }
    const double since = time_ - lostAt_;
    GameEvent event;
    if (life_ == Life::BatStays && since >= rules_.batLeaveDelay) {
        scene_.world().removeBody(batBody_);
        batBody_ = 0;
        bat_.velocityX = 0;
        setBatPhase(BatPhase::Leaving, rules_.batLeaveTime);
        event.type = EventType::BatLeft;
        event.position = bat_.position;
        emit(event);
        life_ = Life::BatLeaving;
    }
    if (life_ == Life::BatLeaving && since >= rules_.batLeaveDelay + rules_.batLeaveTime) {
        setBatPhase(BatPhase::Absent);
        life_ = Life::HeartDue;
    }
    if (life_ == Life::HeartDue && since >= rules_.heartDelay) {
        event.hearts = std::max(hearts_ - 1, 0);
        if (hearts_ > 0) {
            --hearts_;
            event.type = EventType::LifeLost;
            emit(event);
            life_ = Life::BatDue;
        } else {
            phase_ = SessionPhase::GameOver;
            life_ = Life::None;
            event.type = EventType::GameOver;
            emit(event);
            return;
        }
    }
    if (life_ == Life::BatDue && since >= rules_.batEnterDelay) {
        life_ = Life::None;
        readyAt_ = lostAt_ + rules_.readyDelay;
        enterBat();
    }
}

Vec3 GameSession::centreOf(ActorIndex actor) const {
    // The pose is the model's origin, which can be well away from the pieces (a ship's is on the water line).
    return transformPoint(scene_.plan().actors[actor].body.massLocalPose.translationPart(), scene_.actorPose(actor));
}

void GameSession::explodeDue() {
    for (std::size_t i = 0; i < exploding_.size();) {
        const ActorIndex actor = exploding_[i];
        if (bricks_[actor].explodeAt > time_ + fixedStep_ * 1e-3) {
            ++i;
            continue;
        }
        exploding_.erase(exploding_.begin() + static_cast<std::ptrdiff_t>(i));
        const Brick& brick = bricks_[actor];
        stalled_ = 0;
        score_ += brick.material->brickScore;
        ++stats_.bricksDestroyed;
        GameEvent event;
        event.type = EventType::BrickDestroyed;
        event.actor = actor;
        event.position = centreOf(actor);
        event.points = brick.material->brickScore;
        event.material = brick.material;
        emit(event);
        removeBrick(actor);
    }
}

void GameSession::removeFallen() {
    for (const ActorIndex actor : dynamicActors_) {
        Brick& brick = bricks_[actor];
        const BodyId body = scene_.body(actor);
        if (body == 0) {
            continue;
        }
        const Vec3 at = scene_.world().pose(body).translationPart();
        if (at.y >= rules_.fallLimitY && finite(at)) {
            continue;
        }
        if (brick.exploding) {
            std::erase(exploding_, actor);
            brick.exploding = false;
        }
        ++stats_.actorsFallen;
        stalled_ = 0;
        GameEvent event;
        event.type = EventType::ActorFell;
        event.actor = actor;
        event.position = centreOf(actor);
        event.material = brick.material;
        if (rules_.fallScores && brick.damageable) {
            event.points = brick.material->brickScore;
            score_ += event.points;
        }
        emit(event);
        removeBrick(actor);
    }
}

void GameSession::removeBrick(ActorIndex actor) {
    Brick& brick = bricks_[actor];
    brick.removed = true;
    brick.exploding = false;
    for (int rule = 0; rule < kRuleCount && !brick.outOfPlay; ++rule) {
        if ((brick.targetFor >> rule) & 1u) {
            --targetsLeft_[rule];
        }
    }
    scene_.removeActor(actor);
}

// Whether the ball can still get at a target that is in the level (SessionOptions::ignoreUnreachable).
bool GameSession::inPlay(ActorIndex actor) const {
    const ActorPlan& plan = scene_.plan().actors[actor];
    const Bounds box = boundsOf(plan.body, scene_.actorPose(actor));
    if (box.low.y >= rules_.ballPlaneY + kBallRadius) {  // above the ball
        return plan.motion == MotionType::Dynamic && plan.body.gravity && reach_->touches(box.low.x, box.low.z, box.high.x, box.high.z);
    }
    return reach_->touches(box.low.x - kBallRadius, box.low.z - kBallRadius, box.high.x + kBallRadius,
                           box.high.z + kBallRadius);
}

void GameSession::dropTarget(ActorIndex actor) {
    Brick& brick = bricks_[actor];
    brick.outOfPlay = true;
    for (int rule = 0; rule < kRuleCount; ++rule) {
        if ((brick.targetFor >> rule) & 1u) {
            --targetsLeft_[rule];
        }
    }
}

void GameSession::releaseTargets(bool onlyLoose) {
    const auto release = [&](ActorIndex actor) {
        dropTarget(actor);
        ++stats_.targetsOutOfPlay;
    };
    if (onlyLoose) {
        std::erase_if(loose_, [&](ActorIndex actor) {
            if (bricks_[actor].removed || bricks_[actor].outOfPlay) {  // gone, or let go of by letGoOfStalled()
                return true;
            }
            if (inPlay(actor)) {
                return false;
            }
            release(actor);
            return true;
        });
        return;
    }
    for (ActorIndex actor = 0; actor < bricks_.size(); ++actor) {
        if (bricks_[actor].targetFor != 0 && !inPlay(actor)) {
            release(actor);
        }
    }
    std::erase_if(loose_, [&](ActorIndex actor) { return bricks_[actor].outOfPlay; });
}

void GameSession::letGoOfStalled() {
    if (stalled_ < rules_.stallSeconds || phase_ != SessionPhase::Playing || levelEnd_ == TargetRule::Never) {
        return;
    }
    const int left = targetsLeft(levelEnd_);
    const int most = std::max(rules_.stallMost, static_cast<int>(rules_.stallShare * static_cast<float>(targetsAtStart_[static_cast<int>(levelEnd_)])));
    if (left == 0 || left > most) {
        return;
    }
    for (ActorIndex actor = 0; actor < bricks_.size(); ++actor) {
        if (isTarget(actor, levelEnd_)) {
            dropTarget(actor);
            ++stats_.targetsLetGo;
        }
    }
}

void GameSession::checkComplete() {
    if (phase_ != SessionPhase::Complete && phase_ != SessionPhase::GameOver && levelEnd_ != TargetRule::Never &&
        targetsLeft(levelEnd_) == 0) {
        complete();
    }
}

void GameSession::complete() {
    phase_ = SessionPhase::Complete;
    life_ = Life::None;  // a heart is not spent after the level is won
    // The bat's swoop is over: no new bat comes if it was leaving, and the one coming is there.
    if (bat_.phase == BatPhase::Entering) {
        setBatPhase(BatPhase::Present);
    } else if (bat_.phase == BatPhase::Leaving) {
        setBatPhase(BatPhase::Absent);
    }
    GameEvent event;
    event.type = EventType::LevelComplete;
    emit(event);
}

void GameSession::emit(GameEvent event) {
    if (events_.size() >= kMostQueuedEvents) {
        if (!std::exchange(eventsDropped_, true)) {
            logWarn("GameSession: {} events are waiting to be taken; dropping new ones", kMostQueuedEvents);
        }
        return;
    }
    event.time = time_;
    events_.push_back(event);
}

void GameSession::refreshViews() {
    bat_.phaseTime = time_ - batPhaseStart_;
    views_.clear();
    for (const Ball& ball : balls_) {
        BallView view;
        view.id = ball.id;
        view.held = ball.held;
        if (ball.held) {
            view.position = {bat_.position.x + rules_.heldOffsetX, rules_.ballPlaneY,
                             bat_.position.z + rules_.heldOffsetZ};
        } else {
            view.position = scene_.world().pose(ball.body).translationPart();
            view.velocity = scene_.world().linearVelocity(ball.body);
        }
        views_.push_back(view);
    }
}

}  // namespace mb
