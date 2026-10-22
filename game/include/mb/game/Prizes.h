#pragma once

#include "mb/engine/Math.h"

#include <array>
#include <climits>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace mb {

// The prizes of the game (Data/Objects/Prizes, docs/OBJECTS.md section 5, docs/GAMEPLAY.md sections 7 and 14): what drops from
// a destroyed brick, falls down the board and is caught by the bat. The rules that decide what drops and what each prize does
// are here and in GameSession (src/SessionPrizes.cpp, "Prizes" in docs/GAMEPLAY.md section 15); nothing here draws or plays.

// In the order of the table of docs/GAMEPLAY.md section 7.
enum class PrizeKind : std::uint8_t {
    Bonus,          // bonus
    Life,           // life
    Skull,          // scull
    IncBat,         // incbat
    DecBat,         // decbat
    SpeedUp,        // speedup
    SlowDown,       // slowdown
    BigBall,        // bigball
    SmallBall,      // smallball (Weak Ball)
    PowerBall,      // powerfull
    NormalBall,     // commonball
    ExplosiveBall,  // explosiveball
    Drunken,        // drunken
    Multiball,      // tripple
    Magnet,         // magnit
    YoYo,           // pingpong
    Wall,           // wall
    Night,          // night
    Gun,            // gun (Machine Gun)
    Cannon,         // cannon
    Bazooka,        // bazuka
    Laser,          // lazer
    Thunderbolt,    // flash
    Meteor,         // meteor (Meteorite Rain)
    Quake,          // quake (Earthquake)
    Wind,           // wind
    Surprise,       // random
    NextLevel,      // endlevel
};
inline constexpr int kPrizeKindCount = 28;

// What the data and the footage say about one prize, without a rule in it.
struct PrizeInfo {
    PrizeKind kind;
    std::string_view stem;     // Data/Objects/Prizes/<stem>.json and the name of its material: brickScore is the catch score
                               // and the dead sound the catch's sound [D]
    std::string_view caption;  // the key of the caption in languages.json (shown for about 2.2 s at the bottom centre)
    std::string_view icon;     // the stem of the HUD icon in Textures/Icons while the effect lasts, empty for none [M]
    bool bad;                  // the red halo (`oreolred`): drops only from level PrizeRules::badFromLevel on [D, M]
    double seconds;            // how long the effect lasts, 0 if it is instant or counts shots instead [M unless stated]
    int count;                 // shots of a weapon [M], blasts of the Explosive Ball [I]
    int weight;                // relative chance among the drops, see docs/GAMEPLAY.md section 15 [I]
};

const PrizeInfo& prizeInfo(PrizeKind kind);
// The prize whose file is called `stem` ("life", "bazuka"; any case), if there is one.
std::optional<PrizeKind> prizeByStem(std::string_view stem);

enum class WeaponType : std::uint8_t { Gun, Cannon, Bazooka, Laser };
inline constexpr int kWeaponTypeCount = 4;
// The weapon a prize gives, if it is one.
std::optional<WeaponType> weaponOf(PrizeKind kind);
PrizeKind prizeOf(WeaponType type);

// Every number the prizes use. The comment says where it comes from, as GameplayRules does: [M] measured from footage, [D] read
// from the game's data, [I] inferred, [?] a guess; the sections are those of docs/GAMEPLAY.md.
struct PrizeRules {
    // False: nothing drops by itself (prizes can still be given with GameSession::dropPrize). The tests that count on the
    // bricks and the ball alone turn it off.
    bool drops = true;

    // ---- What drops (sections 7, "Rules settled by the second study" 6, 14.4, 15.1)
    // The chance that a destroyed object leaves a prize, by what it was worth. 14.7 % of all kill events had a caught prize,
    // 13-14 % of the cheap bricks and 4-6 % of the objects worth 120 or more [M]; prizes that were missed are not seen, so
    // these are lower bounds. The numbers are the second study's fit by value (13.5, 8.5, 5.5, 4.2 and 5.9 %) raised by 1.18, so
    // that the mean over the destroyable objects of the 83 levels is 14.7 % (mb_prize_smoke prints it) [I].
    struct Tier {
        int below;     // objects worth less than this (brickScore)
        float chance;  // leave a prize with this chance
    };
    std::array<Tier, 5> tiers = {{{80, 0.158f}, {120, 0.10f}, {200, 0.065f}, {300, 0.05f}, {INT_MAX, 0.07f}}};
    // The prizes with a red halo are ordinary drops from level 10 on, none in levels 1 to 9 (about 100 catches) [M]; a harder
    // unlock level is not proven.
    int badFromLevel = 10;
    // The Next Level star ended 70 of the 83 levels, each time with most of the board gone already (at least 90 % in every
    // ending looked at), and is 5 % of the catches [M]: no drop of constant chance. Whether its chance rises as the bricks run
    // out is not shown, but fits [?]; so it is rolled apart from the others, once for each destroyed object. Nothing until
    // fewer than `starWindowShare` of the level's targets (and at least starWindowMin of them) are left, then a chance rising in
    // a straight line to starChanceLast as the last one goes. A star in the air is not followed by another. The 13 levels of 83
    // that ended by themselves are a star that never fell or was missed, one in six [I]: this one falls in nine levels of ten,
    // and a player who catches nine stars in ten ends four levels in five.
    float starWindowShare = 0.10f;
    int starWindowMin = 6;
    float starChanceLast = 0.5f;

    // ---- Fall and catch (14.1, 14.4)
    float fallSpeed = 10.0f;  // units/s straight along -z from where the brick died, x unchanged: 8.8 to 11.0 measured [M]
    float height = 1.0f;      // the orb floats at this y: 1.0 to 1.2 [M]
    // Caught when the orb overlaps the bat's sphere: its distance in the plane is at most batRadius + halfSize, 4 u. Caught at
    // 1.8 u, missed at 8.8 and 26 u [M]; the 4 is [I].
    float halfSize = 1.0f;
    float missZ = -21.0f;  // a missed one fizzles at the near edge, the bat's line [M]

    // ---- Effects (14.3, 15.3)
    // Same prize again: a timed effect starts over from its full time (it does not add up); shots and blasts add up. The
    // footage never watched a second catch [?].
    float batStep = 1.24f;     // Increase Bat Size: the arc went from 75 px to 93 px, +24 % [M]; Decrease is the inverse [I]
    float batScaleMin = 0.6f;  // the bat's collision sphere is scaled by up to these [I]
    float batScaleMax = 1.9f;
    // The bat's outer edge stays inside this, short of the walls (+-20 at the ball's height): its centre is held to
    // min(batLimitX, batEdge - radius) [I].
    float batEdge = 19.0f;
    float speedUp = 1.10f;   // Increase Ball Speed 15.6 to 17.2 u/s [M]
    float slowDown = 0.91f;  // Decrease Ball Speed 0.90-0.91 [M]
    float speedMin = 0.5f;   // limits of the product of the speed prizes of a level: none known [?]
    float speedMax = 2.0f;
    float bigBallRadius = 1.0f;    // diameter x1.62 of 1.2 (r 0.97; ball-powerfull.x is 0.94) [M], 1.0 [I]
    float smallBallRadius = 0.4f;  // x0.63 (r 0.38) [M], 0.4 [I]
    float blastRadius = 2.5f;      // the explose definition: a trigger sphere of radius 2.5 [D]; one blast per charge [I]
    // Drunken Ball: each ball's heading turns at 20 degrees/s x sin(2 pi t / 1.6 s + a phase of its own): "15-22 degrees per
    // second, the sign changing about once a second", speed unchanged [M]; the sine [I].
    float drunkenRate = 20.0f;
    float drunkenPeriod = 1.6f;
    // Multiball: two balls beside the ball nearest the bat, on headings turned from its own by these angles: -20 and +98
    // degrees in the one case measured [M, one case]. No more than maxBalls at a time [I].
    std::array<float, 2> multiballTurns = {-20.0f, 98.0f};
    int maxBalls = 12;
    // Magnet: a ball that touches the bat sticks where it touched and goes with the bat, 2.7 s and more in the footage. It is
    // let go by a click, or by itself after magnetHold seconds, or when the magnet ends [M, the rest I].
    double magnetHold = 4.0;
    // Yo-Yo: the ball is kept within this of the bat, by a string that turns it back [M: 7.0 u at the end, 1-5 u before];
    // 7.5 s in one case [M, one case]. Measured at the plain bat; the string is tied to its rim, so a bat the prizes made bigger or
    // smaller has it that much longer or shorter [I].
    float yoyoLength = 7.0f;
    // Wall: the rope along the near edge. Wall.XML is a box 41.43 x 3.22 x 0.74 at z -20.01 [D]; a ball bounces off its far
    // face [I].
    float wallZ = -20.01f;
    float wallHalfDepth = 0.37f;
    float wallHalfWidth = 20.7f;
    // Night: the board's brightness falls to 0.38 within a second, holds, and is back at about 80 % at the end [M];
    // nightLevel() is 0 to 1, ramping up over nightRampIn and down over the last nightRampOut seconds [I].
    double nightRampIn = 1.0;
    double nightRampOut = 1.5;
    // Earthquake (3 s of jitter and jumps, the camera still [M]): every quakeEvery seconds each loose body gets a kick of up to
    // quakeKick sideways and up to quakeJump upwards. Stronger ones threw bricks out of the ball's reach in the autopilot's
    // levels [I].
    double quakeEvery = 0.1;
    float quakeKick = 0.5f;
    float quakeJump = 1.0f;
    // Wind (5 s [I]): no icon and no visible effect in the footage [M, weak]. The pieces that are moving are accelerated along
    // x, to the left or to the right for the whole time; 8 u/s^2 blew the pieces of whole levels off the board [I].
    float windAccel = 2.5f;
    // Skull: the bat dies at the catch: it leaves 0.4 s later, every ball vanishes 1.5 s after the catch and the heart goes at
    // about 1.9 s (V1 8536.6: bat 8537.0, balls 8538.0, hearts 8538.5-8539) [M]. The life sequence of a lost ball runs on
    // from a loss that began skullLead seconds before the catch [I].
    double skullLead = 0.2;
    double skullBallsVanish = 1.5;
};

// Randomness of the prizes, reproducible: SplitMix64, whose output depends on the seed and nothing else (not on the library).
class PrizeRandom {
public:
    explicit PrizeRandom(std::uint64_t seed = 0) : state_(seed) {}
    std::uint64_t next();
    // In [0, 1).
    double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    // In [low, high).
    float between(float low, float high) { return low + static_cast<float>(uniform()) * (high - low); }

private:
    std::uint64_t state_;
};

// The chance that the destruction of an object worth `points` leaves a prize.
float dropChance(const PrizeRules& rules, int points);
// The chance that the destruction of an object, which leaves `targetsLeft` of the level's `targetsAtStart`, leaves the star.
float starChance(const PrizeRules& rules, int targetsLeft, int targetsAtStart);

// Which prize drops, from u in [0, 1): by the weights of PrizeInfo. The Next Level star is no part of it. GoodOnly leaves
// out the prizes with a red halo; Gift (what a Surprise gives) the Surprise as well.
enum class PrizePool { All, GoodOnly, Gift };
PrizeKind pickPrize(double u, PrizePool pool);

// ---- State the presentation reads

// A prize on its way down the board.
struct FallingPrize {
    int id = 0;  // unique in the session
    PrizeKind kind = PrizeKind::Bonus;
    Vec3 position{};  // the orb's centre
    double age = 0;   // seconds since it appeared, for the picture
    std::uint32_t source = UINT32_MAX;  // ActorIndex of the object whose destruction it came from, kNoActor if none
};

// An effect that is going on; the HUD shows those with an icon, oldest first.
struct ActiveEffect {
    PrizeKind kind = PrizeKind::Bonus;
    double timeLeft = 0;  // seconds, for a timed effect (the HUD shows it rounded up)
    double duration = 0;  // what it started with
    int shots = 0;        // shots left of a weapon, blasts left of the Explosive Ball
};

struct WeaponState {
    WeaponType type = WeaponType::Gun;
    int shots = 0;
};

}  // namespace mb
