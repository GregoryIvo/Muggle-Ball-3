#include "mb/game/Autopilot.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mb {

namespace {

// Where on the bat, from its centre, the successive balls are met: it sends them out at about +-37, 18, 8, ... degrees.
constexpr std::array<float, 8> kMeetAt = {2.2f, -1.1f, 0.5f, -2.6f, 1.6f, -0.4f, 2.9f, -1.9f};

// The side walls' inner faces are at +-20.4 in the footage and +-20.0 at the ball's height on the converted boards: the
// autopilot aims as if a ball's centre bounced between +-19.8, near enough to both.
constexpr float kWall = 19.8f;

// x after travelling freely, bouncing off the walls at +-kWall.
float foldIntoBoard(float x) {
    const float period = 4 * kWall;
    float u = std::fmod(x + kWall, period);
    if (u < 0) {
        u += period;
    }
    return (u > 2 * kWall ? period - u : u) - kWall;
}

}  // namespace

GameInput Autopilot::decide(const GameSession& session) const {
    GameInput input;
    input.launch = session.phase() == SessionPhase::Ready;
    input.batTargetX = session.bat().position.x;

    const GameplayRules& rules = session.rules();
    const float line = rules.batZ + rules.batRadius + kBallRadius;  // where the ball meets the bat
    const BallView* coming = nullptr;
    float soonest = 0;
    for (const BallView& ball : session.balls()) {
        if (ball.held || ball.velocity.z >= 0) {
            continue;
        }
        const float t = (ball.position.z - line) / -ball.velocity.z;
        if (coming == nullptr || t < soonest) {
            coming = &ball;
            soonest = t;
        }
    }
    if (coming != nullptr) {
        const float meetAt = kMeetAt[static_cast<std::size_t>(session.stats().batHits) % kMeetAt.size()];
        input.batTargetX = foldIntoBoard(coming->position.x + coming->velocity.x * std::max(soonest, 0.0f)) - meetAt;
    } else if (!session.balls().empty() && !session.balls().front().held) {
        input.batTargetX = session.balls().front().position.x;  // waiting for it to come back
    }
    return input;
}

PlayResult autoplay(GameSession& session, double seconds, const std::function<void(const GameSession&)>& afterFrame) {
    constexpr double kFrame = 1.0 / 60.0;
    const Autopilot pilot;
    PlayResult result;
    const double end = session.time() + seconds;
    while (session.time() + kFrame / 2 < end && session.phase() != SessionPhase::Complete &&
           session.phase() != SessionPhase::GameOver) {
        session.update(kFrame, pilot.decide(session));
        for (const GameEvent& event : session.takeEvents()) {
            result.events.push_back(event);
        }
        if (afterFrame) {
            afterFrame(session);
        }
    }
    result.seconds = session.time() - (end - seconds);
    return result;
}

}  // namespace mb
