// Prize tests: what drops from a destroyed object, how it falls and is caught, and what each of the 28 prizes does, on made-up
// levels (a walled arena with a few blocks) run in a GameSession with the physics on the calling thread. Needs neither a GPU nor
// game data. The same rules against the real data: mb_prize_smoke and mb_gameplay_smoke.

#include "mb/game/Autopilot.h"
#include "mb/game/Gameplay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) <= tolerance;
}

std::string num(double value) {
    return std::to_string(value);
}

// ---- Materials and levels ----------------------------------------------------------------------------------------

// The catch scores of the data (docs/GAMEPLAY.md section 7). "meteor" is there twice, as in materials.json: the projectile's
// material first (group temp, 250 points), then the prize's.
struct PrizeScore {
    const char* stem;
    int score;
};
constexpr PrizeScore kScores[] = {
    {"bonus", 777},     {"life", 777},     {"scull", 666},    {"incbat", 110},        {"decbat", 180}, {"speedup", 280},
    {"slowdown", 120},  {"bigball", 180},  {"smallball", 240}, {"powerfull", 170},    {"commonball", 120},
    {"explosiveball", 150}, {"drunken", 220}, {"tripple", 190}, {"magnit", 230},      {"pingpong", 140}, {"wall", 200},
    {"night", 280},     {"gun", 300},      {"cannon", 320},   {"bazuka", 340},        {"lazer", 360},  {"flash", 210},
    {"meteor", 4},      {"quake", 190},    {"wind", 100},     {"random", 299},        {"endlevel", 120}};

// Cheap.jpg to Dearest.jpg: one hit each, worth what falls in each of the five tiers of PrizeRules::tiers. Tough.jpg: three hits.
// Ice.jpg: a brick that slides without friction.
const MaterialLibrary& materials() {
    static const MaterialLibrary library = [] {
        std::string json = R"({"version": 1, "materials": [
          {"name": "BallMaterial", "density": 1, "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 0.999},
           "brick": {"type": 2, "energy": 3, "score": 4}},
          {"name": "BoardMaterial", "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 1.0},
           "brick": {"type": 2, "energy": 3, "score": 4}},
          {"name": "Cheap.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 1, "score": 20, "exploseDelay": 0}},
          {"name": "Mid.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 1, "score": 100, "exploseDelay": 0}},
          {"name": "Dear.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 1, "score": 150, "exploseDelay": 0}},
          {"name": "Dearer.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 1, "score": 250, "exploseDelay": 0}},
          {"name": "Dearest.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 1, "score": 400, "exploseDelay": 0}},
          {"name": "Tough.jpg", "physics": {"restitution": 0.5}, "brick": {"type": 0, "energy": 3, "score": 100, "exploseDelay": 100}},
          {"name": "Ice.jpg", "physics": {"staticFriction": 0, "dynamicFriction": 0, "restitution": 0.5, "frictionCombineMode": 1},
           "brick": {"type": 0, "energy": 1, "score": 10, "exploseDelay": 0}},
          {"name": "meteor", "group": "temp", "brick": {"type": 0, "energy": 1, "score": 250, "exploseType": 9}})";
        for (const PrizeScore& prize : kScores) {
            json += std::string(R"(, {"name": ")") + prize.stem + R"(", "group": "Prizes", "brick": {"type": 0, "energy": 3, "score": )" +
                    std::to_string(prize.score) + R"(, "exploseType": 3, "exploseDelay": 500}, "sounds": {"dead": ")" + prize.stem +
                    R"("}})";
        }
        return MaterialLibrary::parse(json + "]}");
    }();
    return library;
}

constexpr std::uint32_t kDynamic = 0x100, kStatic = 0x17E;  // NxBodyFlag, with the visualization bit

Shape box(Vec3 half, Vec3 at = {}, std::uint16_t group = 2) {
    Shape s;
    s.type = ShapeType::Box;
    s.halfExtents = half;
    s.localPose.position = at;
    s.group = group;
    s.shapeFlags = 8;
    return s;
}

Actor actor(ObjectType type, Vec3 at, std::uint32_t flags, float mass, std::vector<Shape> shapes, const std::string& material) {
    Actor a;
    a.pose.position = at;
    Body body;
    body.mass = mass;
    body.massSpaceInertia = {mass / 6, mass / 6, mass / 6};
    body.flags = flags;
    a.body = body;
    a.shapes = std::move(shapes);
    GameObject object;
    object.type = type;
    object.id = 1;
    object.material = material;
    a.object = object;
    return a;
}

Actor brick(Vec3 at, const std::string& material, std::uint32_t flags = kStatic, Vec3 half = {1, 1, 1}, float mass = 50) {
    return actor(ObjectType::Brick, at, flags, mass, {box(half)}, material);
}

// The board as the levels have it, but with the near end open unless `nearWall`, so that a ball can be lost.
Actor board(bool nearWall = false, bool farWall = true) {
    std::vector<Shape> shapes = {box({30, 0.5f, 30}, {0, -0.5f, 0}, 4), box({1, 2, 30}, {-21.4f, 2, 0}, 4),
                                 box({1, 2, 30}, {21.4f, 2, 0}, 4)};
    if (farWall) {
        shapes.push_back(box({30, 2, 1}, {0, 2, 23.6f}, 4));
    }
    if (nearWall) {
        shapes.push_back(box({30, 2, 1}, {0, 2, -21.3f}, 4));
    }
    return actor(ObjectType::Board, {0, 0, 0}, 0xFF, 1000, std::move(shapes), "BoardMaterial");
}

Level levelOf(std::vector<Actor> actors) {
    Level level;
    level.gravity = {0, -9.8f, 0};
    level.actors = std::move(actors);
    return level;
}

// `count` small one-hit bricks of a material, static, in rows over the board.
std::vector<Actor> crowd(int count, const std::string& material, int firstRow = 0) {
    std::vector<Actor> bricks;
    for (int i = 0; i < count; ++i) {
        const float x = -15.0f + 0.5f * static_cast<float>(i % 60);
        const float z = 0.0f + 0.5f * static_cast<float>(firstRow + i / 60);
        bricks.push_back(brick({x, 0.4f, z}, material, kStatic, {0.2f, 0.2f, 0.2f}, 1));
    }
    return bricks;
}

Level crowdLevel(int count, const std::string& material) {
    std::vector<Actor> actors = {board()};
    for (Actor& b : crowd(count, material)) {
        actors.push_back(std::move(b));
    }
    return levelOf(std::move(actors));
}

SessionOptions options(TargetRule rule = TargetRule::Never) {
    SessionOptions o;
    o.levelEnd = rule;
    o.ignoreUnreachable = false;
    o.physicsSettings.threads = 0;  // on the caller, so that a run repeats exactly
    o.rules.prizes.drops = false;   // the prizes are given by hand
    return o;
}

SessionOptions dropping(int level = 1, std::uint64_t seed = 1, TargetRule rule = TargetRule::Never) {
    SessionOptions o = options(rule);
    o.rules.prizes.drops = true;
    o.campaignLevel = level;
    o.prizeSeed = seed;
    return o;
}

// A session being played, with every event it reported.
struct Game {
    GameSession session;
    std::vector<GameEvent> log;

    explicit Game(const Level& level, const SessionOptions& o = options()) : session(level, materials(), o) {}

    void step(const GameInput& input = {}) {
        session.update(1.0 / 60, input);
        flush();
    }
    void run(double seconds, const GameInput& input = {}) {
        for (int i = 0, steps = static_cast<int>(std::lround(seconds * 60)); i < steps; ++i) {
            step(input);
        }
    }
    bool runUntil(const std::function<bool()>& done, double limit, const GameInput& input = {}) {
        for (int i = 0, steps = static_cast<int>(std::lround(limit * 60)); i < steps; ++i) {
            if (done()) {
                return true;
            }
            step(input);
        }
        return done();
    }
    bool ready() { return runUntil([&] { return session.phase() == SessionPhase::Ready; }, 3); }
    // Gives a prize and takes the events it made.
    void give(PrizeKind kind) {
        session.givePrize(kind);
        flush();
    }
    void flush() {
        for (const GameEvent& event : session.takeEvents()) {
            log.push_back(event);
        }
    }
    void launch() { step({.launch = true}); }

    int count(EventType type) const { return static_cast<int>(std::ranges::count(log, type, &GameEvent::type)); }
    int count(EventType type, PrizeKind prize) const {
        return static_cast<int>(std::ranges::count_if(
            log, [&](const GameEvent& e) { return e.type == type && e.prize && *e.prize == prize; }));
    }
    const GameEvent* find(EventType type, std::size_t from = 0) const {
        for (std::size_t i = from; i < log.size(); ++i) {
            if (log[i].type == type) {
                return &log[i];
            }
        }
        return nullptr;
    }
    const GameEvent* find(EventType type, PrizeKind prize, std::size_t from = 0) const {
        for (std::size_t i = from; i < log.size(); ++i) {
            if (log[i].type == type && log[i].prize && *log[i].prize == prize) {
                return &log[i];
            }
        }
        return nullptr;
    }
    const BallView* ball(int id) const {
        for (const BallView& view : session.balls()) {
            if (view.id == id) {
                return &view;
            }
        }
        return nullptr;
    }
    Vec3 bat() const { return session.bat().position; }
    // Drops a prize on the bat and steps until it is caught.
    bool catches(PrizeKind kind) {
        const std::size_t from = log.size();
        session.dropPrize(kind, {bat().x, 0, bat().z + 1});
        run(0.1);
        return find(EventType::PrizeCaught, kind, from) != nullptr;
    }
    // The ball nearest the bat that is neither held nor stuck.
    const BallView* flying() const {
        for (const BallView& view : session.balls()) {
            if (!view.held && !view.stuck) {
                return &view;
            }
        }
        return nullptr;
    }
};

float heading(const BallView& ball) {
    return std::atan2(ball.velocity.x, ball.velocity.z) * 180 / kPi;
}

// ---- Tests --------------------------------------------------------------------------------------------------------

void testTable() {
    check(kPrizeKindCount == 28, "table: 28 prizes");
    std::set<std::string_view> stems, captions;
    int bad = 0;
    for (int i = 0; i < kPrizeKindCount; ++i) {
        const PrizeInfo& info = prizeInfo(static_cast<PrizeKind>(i));
        check(info.kind == static_cast<PrizeKind>(i), "table: row " + std::to_string(i) + " is in the order of the enum");
        stems.insert(info.stem);
        captions.insert(info.caption);
        bad += info.bad;
        check(prizeByStem(info.stem) == info.kind, "table: " + std::string(info.stem) + " is found by its file name");
        check(info.kind == PrizeKind::NextLevel ? info.weight == 0 : info.weight > 0, "table: only the star has no weight");
    }
    check(stems.size() == 28 && captions.size() == 28, "table: every prize has a file and a caption of its own");
    check(prizeByStem("BAZUKA") == PrizeKind::Bazooka && !prizeByStem("nothing"), "table: stems are found in any case");
    // The red halos of Data/Objects/Prizes: commonball, decbat, drunken, night, scull, smallball, speedup.
    const std::set<PrizeKind> reds = {PrizeKind::NormalBall, PrizeKind::DecBat, PrizeKind::Drunken, PrizeKind::Night,
                                      PrizeKind::Skull, PrizeKind::SmallBall, PrizeKind::SpeedUp};
    check(bad == 7, "table: seven prizes are bad");
    for (const PrizeKind kind : reds) {
        check(prizeInfo(kind).bad, std::string("table: ") + std::string(prizeInfo(kind).stem) + " is bad");
    }
    // The HUD numbers.
    const std::pair<PrizeKind, double> seconds[] = {{PrizeKind::BigBall, 20}, {PrizeKind::SmallBall, 20}, {PrizeKind::PowerBall, 20},
                                                    {PrizeKind::Drunken, 30}, {PrizeKind::Magnet, 30}, {PrizeKind::Night, 10},
                                                    {PrizeKind::Wall, 30}, {PrizeKind::YoYo, 7.5}};
    for (const auto& [kind, time] : seconds) {
        check(prizeInfo(kind).seconds == time, "table: " + std::string(prizeInfo(kind).stem) + " lasts " + num(time) + " s");
    }
    check(prizeInfo(PrizeKind::Gun).count == 12 && prizeInfo(PrizeKind::Cannon).count == 6 &&
              prizeInfo(PrizeKind::Bazooka).count == 7 && prizeInfo(PrizeKind::Laser).count == 5,
          "table: the weapons carry 12, 6, 7 and 5 shots");
    for (int i = 0; i < kWeaponTypeCount; ++i) {
        const auto type = static_cast<WeaponType>(i);
        check(weaponOf(prizeOf(type)) == type, "table: a weapon and its prize match");
    }
    check(!weaponOf(PrizeKind::Magnet), "table: the Magnet is no weapon");
    // The icons the HUD has (GameHud::powerUps).
    check(prizeInfo(PrizeKind::BigBall).icon == "big" && prizeInfo(PrizeKind::Magnet).icon == "Magnit" &&
              prizeInfo(PrizeKind::Bazooka).icon == "Bazuka" && prizeInfo(PrizeKind::Laser).icon == "Lazer" &&
              prizeInfo(PrizeKind::SpeedUp).icon.empty(),
          "table: the icons are the stems of Textures/Icons, and the instant prizes have none");
    // Where the bad weights stand in the mix of the footage: 9 % of the catches of levels 10 to 83 (red-only values), a little more
    // with the three that share a value with a good one.
    int total = 0, badWeight = 0;
    for (int i = 0; i < kPrizeKindCount; ++i) {
        const PrizeInfo& info = prizeInfo(static_cast<PrizeKind>(i));
        total += info.weight;
        badWeight += info.bad ? info.weight : 0;
    }
    check(badWeight * 100 > total * 8 && badWeight * 100 < total * 15, "table: the bad prizes are about a tenth of the drops");
}

void testRandom() {
    PrizeRandom a(0);
    check(a.next() == 0xE220A8397B1DCDAFull && a.next() == 0x6E789E6AA1B965F4ull && a.next() == 0x06C45D188009454Full,
          "random: SplitMix64 from seed 0, the same on every machine");
    PrizeRandom b(7), c(7), d(8);
    bool same = true, differ = false, inRange = true;
    double sum = 0;
    for (int i = 0; i < 100000; ++i) {
        const double u = b.uniform();
        same = same && u == c.uniform();
        differ = differ || u != d.uniform();
        inRange = inRange && u >= 0 && u < 1;
        sum += u;
    }
    check(same && differ && inRange, "random: the same seed repeats, another differs, and the numbers are in [0, 1)");
    check(std::fabs(sum / 100000 - 0.5) < 0.01, "random: the mean is a half");
    float low = 9, high = -9;
    for (int i = 0; i < 1000; ++i) {
        const float v = b.between(-2, 3);
        low = std::min(low, v);
        high = std::max(high, v);
    }
    check(low >= -2 && high < 3 && low < -1.8f && high > 2.8f, "random: between() covers its range");
}

void testChances() {
    const PrizeRules rules;
    const auto& tiers = rules.tiers;
    check(dropChance(rules, 0) == tiers[0].chance && dropChance(rules, 79) == tiers[0].chance && dropChance(rules, 80) == tiers[1].chance &&
              dropChance(rules, 119) == tiers[1].chance && dropChance(rules, 120) == tiers[2].chance &&
              dropChance(rules, 199) == tiers[2].chance && dropChance(rules, 200) == tiers[3].chance &&
              dropChance(rules, 299) == tiers[3].chance && dropChance(rules, 300) == tiers[4].chance &&
              dropChance(rules, 777) == tiers[4].chance,
          "chance: the tiers break at 80, 120, 200 and 300 points");
    check(tiers[0].chance > tiers[1].chance && tiers[1].chance > tiers[2].chance && tiers[2].chance > tiers[3].chance &&
              tiers[0].chance > 0.13f && tiers[2].chance < 0.08f && tiers[3].chance > 0.03f,
          "chance: cheap bricks drop most, 13 % and more, and the ones worth 120 or more 4 to 7 %");
    // The destroyable objects of the 83 levels by value (tests/prize_smoke.cpp prints this mean from the data itself).
    const int objects[] = {11260, 889, 789, 210, 61};
    double sum = 0;
    int all = 0;
    for (int i = 0; i < 5; ++i) {
        sum += objects[i] * static_cast<double>(tiers[static_cast<std::size_t>(i)].chance);
        all += objects[i];
    }
    check(std::fabs(sum / all - 0.147) < 0.004, "chance: the mean over the objects of the levels is 14.7 %, " + num(100 * sum / all));

    check(starChance(rules, 100, 100) == 0 && starChance(rules, 10, 100) == 0 && starChance(rules, 11, 100) == 0,
          "star: none until fewer than a tenth of the targets are left");
    check(starChance(rules, 9, 100) > 0 && starChance(rules, 9, 100) < starChance(rules, 5, 100) &&
              starChance(rules, 5, 100) < starChance(rules, 1, 100) && near(starChance(rules, 0, 100), rules.starChanceLast),
          "star: and rises as they go, to starChanceLast at the last one");
    check(starChance(rules, 6, 30) == 0 && starChance(rules, 5, 30) > 0 && starChance(rules, 0, 0) == 0,
          "star: a small level has a window of at least six objects, and a level with no targets has no star");
    check(starChance(rules, 30, 300) == 0 && starChance(rules, 29, 300) > 0 && starChance(rules, 29, 300) < starChance(rules, 1, 300),
          "star: a big level one of a tenth");

    // The mix: every prize but the star comes up, the bad ones only when allowed, a gift never a Surprise.
    std::set<PrizeKind> seen, seenGood, seenGift;
    for (int i = 0; i < 20000; ++i) {
        const double u = (i + 0.5) / 20000;
        seen.insert(pickPrize(u, PrizePool::All));
        seenGood.insert(pickPrize(u, PrizePool::GoodOnly));
        seenGift.insert(pickPrize(u, PrizePool::Gift));
    }
    bool anyBadInGood = false, anyBadInGift = false;
    for (const PrizeKind kind : seenGood) {
        anyBadInGood = anyBadInGood || prizeInfo(kind).bad;
    }
    for (const PrizeKind kind : seenGift) {
        anyBadInGift = anyBadInGift || prizeInfo(kind).bad;
    }
    check(seen.size() == 27 && !seen.contains(PrizeKind::NextLevel), "mix: 27 prizes drop and the star is no part of the mix");
    check(seenGood.size() == 20 && !anyBadInGood && seenGood.contains(PrizeKind::Surprise), "mix: without the bad ones, 20 remain");
    check(seenGift.size() == 19 && !anyBadInGift && !seenGift.contains(PrizeKind::Surprise) && !seenGift.contains(PrizeKind::NextLevel),
          "mix: a gift is one of 19 good prizes, no Surprise and no star");
    check(pickPrize(0, PrizePool::All) == PrizeKind::Bonus && pickPrize(0.999999, PrizePool::All) == PrizeKind::Surprise,
          "mix: the ends of [0, 1) are the first and last prizes of the table");
}

void testDropRate() {
    // Bricks of each tier, destroyed one at a time: the share that leaves a prize is the tier's chance, within three sigma.
    const std::pair<const char*, int> tierOf[] = {{"Cheap.jpg", 0}, {"Mid.jpg", 1}, {"Dear.jpg", 2}, {"Dearer.jpg", 3}, {"Dearest.jpg", 4}};
    const PrizeRules rules;
    for (const auto& [material, tier] : tierOf) {
        constexpr int kBricks = 3000;
        Game g(crowdLevel(kBricks, material), dropping());
        int killed = 0;
        for (ActorIndex i = 0; i < kBricks + 1u; ++i) {
            if (g.session.actor(i).damageable && g.session.hit(i, {})) {
                ++killed;
            }
            if (killed % 20 == 0) {
                g.step();
            }
        }
        g.run(0.1);
        const double p = rules.tiers[static_cast<std::size_t>(tier)].chance;
        const int drops = g.count(EventType::PrizeDropped);
        check(g.count(EventType::BrickDestroyed) == kBricks && killed == kBricks, std::string("rate: all ") + material + " bricks died");
        check(std::fabs(drops - kBricks * p) <= 3.5 * std::sqrt(kBricks * p * (1 - p)),
              std::string("rate: ") + material + " left " + std::to_string(drops) + " prizes in " + std::to_string(kBricks) +
                  ", expected " + num(kBricks * p));
        check(g.session.stats().prizesDropped == drops, "rate: the statistics count the drops");
    }
}

void testDropsAreSeeded() {
    const auto kinds = [](std::uint64_t seed) {
        Game g(crowdLevel(400, "Cheap.jpg"), dropping(10, seed));
        for (ActorIndex i = 1; i <= 400; ++i) {
            g.session.hit(i, {});
            g.step();
        }
        std::vector<std::pair<int, Vec3>> drops;
        for (const GameEvent& event : g.log) {
            if (event.type == EventType::PrizeDropped) {
                drops.emplace_back(static_cast<int>(*event.prize), event.position);
            }
        }
        return drops;
    };
    const auto first = kinds(5), again = kinds(5), other = kinds(6);
    check(!first.empty() && first.size() == again.size() && std::ranges::equal(first, again, [](const auto& a, const auto& b) {
              return a.first == b.first && a.second.x == b.second.x && a.second.z == b.second.z;
          }),
          "seed: the same seed gives the same prizes in the same places");
    bool differ = first.size() != other.size();
    for (std::size_t i = 0; !differ && i < first.size(); ++i) {
        differ = first[i].first != other[i].first || first[i].second.x != other[i].second.x;
    }
    check(differ, "seed: another seed does not");
}

void testRedPrizesFromLevelTen() {
    const auto badShare = [](int level) {
        Game g(crowdLevel(3000, "Cheap.jpg"), dropping(level, 3));
        for (ActorIndex i = 1; i <= 3000; ++i) {
            g.session.hit(i, {});
            if (i % 20 == 0) {
                g.step();
            }
        }
        g.run(0.1);
        int drops = 0, bad = 0;
        for (const GameEvent& event : g.log) {
            if (event.type == EventType::PrizeDropped) {
                ++drops;
                bad += prizeInfo(*event.prize).bad;
            }
        }
        return std::pair{drops, bad};
    };
    const auto [dropsNine, badNine] = badShare(9);
    const auto [dropsTen, badTen] = badShare(10);
    check(dropsNine > 350 && badNine == 0, "red: none in level 9, " + std::to_string(badNine) + " of " + std::to_string(dropsNine));
    check(dropsTen > 350 && badTen > dropsTen * 7 / 100 && badTen < dropsTen * 17 / 100,
          "red: from level 10 about a tenth, " + std::to_string(badTen) + " of " + std::to_string(dropsTen));
    check(PrizeRules{}.badFromLevel == 10, "red: the level is 10");
}

void testPrizeStartsWhereTheBrickDied() {
    SessionOptions o = dropping(10);
    for (PrizeRules::Tier& tier : o.rules.prizes.tiers) {
        tier.chance = 1;  // every kill leaves one
    }
    Game g(levelOf({board(), brick({7.3f, 1, 12.5f}, "Mid.jpg")}), o);
    g.session.hit(1, {});
    g.run(0.1);
    const GameEvent* dropped = g.find(EventType::PrizeDropped);
    check(dropped != nullptr && dropped->actor == 1 && dropped->prize && dropped->material != nullptr &&
              dropped->material->name == prizeInfo(*dropped->prize).stem && dropped->material->group == "Prizes",
          "drop: the event names the brick it came from and the prize's material");
    check(dropped != nullptr && near(dropped->position.x, 7.3f, 0.05f) && near(dropped->position.z, 12.5f, 0.05f) &&
              near(dropped->position.y, PrizeRules{}.height),
          "drop: it starts where the brick was, at the orb's height");
    check(g.session.fallingPrizes().size() == 1 && g.session.fallingPrizes()[0].source == 1, "drop: and falls");
    const std::string text = dropped != nullptr ? describe(*dropped) : "";
    check(text.find("PrizeDropped") != std::string::npos && text.find("prize=") != std::string::npos, "drop: the log says so: " + text);

    // A hit that carries disablePrize (the Thunderbolt's and the meteors') leaves none, whatever the chance.
    Game quiet(levelOf({board(), brick({7.3f, 1, 12.5f}, "Mid.jpg")}), o);
    quiet.session.hit(1, {}, false);
    quiet.run(0.1);
    check(quiet.count(EventType::BrickDestroyed) == 1 && quiet.count(EventType::PrizeDropped) == 0,
          "drop: a kill with disablePrize leaves no prize");

    // Nor does a piece that fell off the board, which is no kill: a crate on a pillar beyond a small floor, and the pillar destroyed.
    Actor island = actor(ObjectType::Board, {0, 0, 0}, 0xFF, 1000, {box({10, 0.5f, 30}, {0, -0.5f, 0}, 4)}, "BoardMaterial");
    Game fall(levelOf({island, brick({14, 1, 0}, "Cheap.jpg", 0x180), brick({14, 2.5f, 0}, "Mid.jpg", kDynamic, {1, 0.5f, 1})}), o);
    fall.session.hit(1, {}, false);
    fall.run(4);
    check(fall.count(EventType::ActorFell) == 1 && fall.count(EventType::PrizeDropped) == 0, "drop: a piece that fell leaves no prize");
}

void testFall() {
    Game g(levelOf({board()}));
    check(g.ready(), "fall: the bat comes in");
    const int id = g.session.dropPrize(PrizeKind::Bonus, {5, 0, 15});
    g.session.dropPrize(PrizeKind::Wind, {-12, 0, 18});
    g.session.dropPrize(PrizeKind::Wall, {15, 0, 4});
    g.flush();
    check(g.session.fallingPrizes().size() == 3 && g.session.fallingPrizes()[0].id == id &&
              g.session.fallingPrizes()[1].id == id + 1,
          "fall: several at once, each with an id of its own");
    check(g.count(EventType::PrizeDropped) == 3, "fall: and three events");
    // Straight along -z at 10 units/s, x unchanged, even with the bat running away from it.
    for (int i = 0; i < 30; ++i) {
        g.step({.batTargetX = -12});
    }
    const FallingPrize& a = g.session.fallingPrizes()[0];
    check(g.session.fallingPrizes().size() == 3 && near(a.position.z, 10.0f, 0.02f) && near(a.position.x, 5.0f) &&
              near(a.position.y, PrizeRules{}.height) && near(static_cast<float>(a.age), 0.5f, 0.02f),
          "fall: 5 units in half a second, x constant and not homing on the bat: z " + num(a.position.z));
    check(near(g.session.fallingPrizes()[1].position.z, 13.0f, 0.02f) && near(g.session.fallingPrizes()[1].position.x, -12.0f),
          "fall: the others at the same pace");
    // The pace stays 10 u/s all the way (2.5 s from z 15 to -10, a second more to -21 and gone).
    g.run(1.5);
    check(near(g.session.fallingPrizes()[0].position.z, -5.0f, 0.03f), "fall: 10 units/s all the way, z " + num(g.session.fallingPrizes()[0].position.z));
}

void testCatchGeometry() {
    // The bat is at x = 0; a prize is caught if it comes within the bat's radius and its own half-size (3 + 1) of its centre.
    Game g(levelOf({board()}));
    check(g.ready(), "catch: the bat comes in");
    const auto fall = [&](float x) {
        const std::size_t from = g.log.size();
        g.session.dropPrize(PrizeKind::Wind, {x, 0, 3});
        g.run(2.8);
        const GameEvent* caught = g.find(EventType::PrizeCaught, from);
        const GameEvent* missed = g.find(EventType::PrizeMissed, from);
        check((caught != nullptr) != (missed != nullptr), "catch: one prize ends in one way");
        return std::pair{caught, missed};
    };
    const auto [near3, missed3] = fall(3.9f);
    check(near3 != nullptr && near(near3->position.x, 3.9f) && near3->position.z > -21.0f && near3->position.z < -19.5f,
          "catch: caught at 3.9 units from the bat's centre, as it passes the edge of its reach");
    const auto [far, missedFar] = fall(4.2f);
    check(missedFar != nullptr && near(missedFar->position.z, -21.0f, 0.2f), "catch: missed at 4.2, and it fizzles at z -21");
    const auto [mid, missedMid] = fall(8.8f);
    const auto [wide, missedWide] = fall(26);
    check(missedMid != nullptr && missedWide != nullptr, "catch: missed at 8.8 and 26 units, as in the footage");
    const auto [centre, missedCentre] = fall(1.8f);
    check(centre != nullptr, "catch: caught at 1.8 units, as in the footage");
    check(g.session.fallingPrizes().empty() && g.session.stats().prizesCaught == 2 && g.session.stats().prizesMissed == 3,
          "catch: counted in the statistics");

    // A wider bat reaches farther; one that has gone does not catch at all.
    g.give(PrizeKind::IncBat);
    check(fall(4.5f).first != nullptr, "catch: a wider bat (3.72) catches at 4.5");
    Game flying(levelOf({board()}));
    check(flying.ready(), "catch: a bat for the second case");
    flying.launch();
    flying.give(PrizeKind::Skull);
    flying.session.dropPrize(PrizeKind::Bonus, {0, 0, -10});
    flying.run(1.3);
    check(flying.count(EventType::PrizeMissed) == 1 && flying.count(EventType::PrizeCaught, PrizeKind::Bonus) == 0,
          "catch: a bat that is flying off catches nothing");
}

void testCatchScore() {
    Game g(levelOf({board()}));
    check(g.ready(), "score: the bat comes in");
    std::int64_t score = g.session.score();
    check(g.catches(PrizeKind::Bonus), "score: Bonus caught");
    const GameEvent* bonus = g.find(EventType::PrizeCaught, PrizeKind::Bonus);
    check(g.session.score() - score == 777 && bonus != nullptr && bonus->points == 777 && bonus->material != nullptr &&
              bonus->material->deadSound == "bonus",
          "score: Bonus is worth 777, and the event carries the material whose dead sound the catch plays");
    score = g.session.score();
    check(g.catches(PrizeKind::Laser) && g.session.score() - score == 360, "score: the Laser is worth 360 (V2 61.65)");
    score = g.session.score();
    check(g.catches(PrizeKind::Meteor) && g.session.score() - score == 4,
          "score: Meteorite Rain is worth the 4 of the prize's own material, not the 250 of the projectile that has its name");
    check(describe(*g.find(EventType::PrizeCaught, PrizeKind::Laser)).find("points=360") != std::string::npos, "score: the log shows it");
    for (const PrizeScore& prize : kScores) {
        const std::optional<PrizeKind> kind = prizeByStem(prize.stem);
        check(kind && materials().find(prize.stem) != nullptr, std::string("score: ") + prize.stem + " has a prize");
    }
}

void testInstantPrizes() {
    Game g(levelOf({board()}));
    check(g.ready(), "instant: the bat comes in");
    check(g.session.hearts() == 2, "instant: two hearts to start");
    check(g.catches(PrizeKind::Life) && g.session.hearts() == 3, "instant: Life is a heart");
    check(g.catches(PrizeKind::Life) && g.session.hearts() == 4 && g.session.stats().prizesCaught == 2, "instant: and another, no cap");
    check(g.catches(PrizeKind::Thunderbolt) && g.catches(PrizeKind::Meteor), "instant: the strike prizes are caught");
    check(g.count(EventType::StrikeGranted, PrizeKind::Thunderbolt) == 1 && g.count(EventType::StrikeGranted, PrizeKind::Meteor) == 1,
          "instant: and grant a strike each, for the next wave");
    check(g.session.activeEffects().empty(), "instant: none of these is an effect");
    const std::int64_t score = g.session.score();
    check(g.session.phase() != SessionPhase::Complete && g.catches(PrizeKind::NextLevel), "instant: the star is caught");
    check(g.session.phase() == SessionPhase::Complete && g.count(EventType::LevelComplete) == 1 && g.session.score() - score == 120,
          "instant: and ends the level at once, +120");
    // Caught during the fade: the score and a heart count, nothing else happens.
    g.session.dropPrize(PrizeKind::Life, {g.bat().x, 0, g.bat().z + 1});
    g.session.dropPrize(PrizeKind::Skull, {g.bat().x, 0, g.bat().z + 1});
    g.session.dropPrize(PrizeKind::Wall, {g.bat().x, 0, g.bat().z + 1});
    g.run(0.1);
    check(g.session.hearts() == 5 && g.session.phase() == SessionPhase::Complete && g.session.activeEffects().empty() &&
              g.count(EventType::BatLeft) == 0,
          "instant: after the end only the Life counts: no skull, no wall");
}

void testSkull() {
    for (const int hearts : {2, 0}) {
        SessionOptions o = options();
        o.hearts = hearts;
        Game g(levelOf({board()}), o);
        check(g.ready(), "skull: the bat comes in");
        g.launch();
        g.give(PrizeKind::BigBall);
        g.give(PrizeKind::Cannon);
        g.give(PrizeKind::Night);
        g.run(0.3);
        g.session.dropPrize(PrizeKind::Skull, {g.bat().x, 0, g.bat().z + 1});
        g.step();
        const GameEvent* caught = g.find(EventType::PrizeCaught, PrizeKind::Skull);
        check(caught != nullptr && caught->points == 666, "skull: caught, +666");
        if (caught == nullptr) {
            return;
        }
        const double t = caught->time;
        check(g.session.phase() == SessionPhase::Lost, "skull: the bat is dead at once");
        check(g.count(EventType::EffectEnded, PrizeKind::BigBall) == 1 && g.count(EventType::EffectEnded, PrizeKind::Cannon) == 1 &&
                  g.session.effect(PrizeKind::BigBall) == nullptr && g.session.effect(PrizeKind::Night) != nullptr,
              "skull: what the bat had goes with it, but not the night");
        g.run(3);
        const GameEvent* left = g.find(EventType::BatLeft);
        check(left != nullptr && near(static_cast<float>(left->time - t), 0.4f, 0.05f), "skull: the bat flies off 0.4 s later");
        const GameEvent* lost = g.find(EventType::BallLost);
        check(lost != nullptr && near(static_cast<float>(lost->time - t), 1.5f, 0.05f), "skull: every ball vanishes 1.5 s after the catch");
        check(g.count(EventType::BallLost) == 1 && g.session.stats().ballsLost == 1, "skull: the one ball in play");
        if (hearts > 0) {
            const GameEvent* life = g.find(EventType::LifeLost);
            check(life != nullptr && near(static_cast<float>(life->time - t), 1.9f, 0.05f) && life->hearts == 1,
                  "skull: a heart goes at about 1.9 s (V1 8538.5-8539)");
            check(g.session.phase() == SessionPhase::Ready && g.session.hearts() == 1, "skull: and the new bat is ready by 2.43 s");
            check(g.session.activeEffects().size() == 1 && g.session.batScale() == 1, "skull: plain, but for the night");
        } else {
            check(g.count(EventType::GameOver) == 1 && g.session.phase() == SessionPhase::GameOver, "skull: with no heart left, the game is over");
        }
    }
    // With a ball held on the bat, that vanishes too.
    Game held(levelOf({board()}));
    check(held.ready() && held.catches(PrizeKind::Skull), "skull: caught with the ball still on the bat");
    held.run(2);
    check(held.count(EventType::BallLost) == 1 && held.count(EventType::LifeLost) == 1, "skull: it goes with the bat, and costs the heart");
}

// Two prizes fall onto the bat in the same step, the Skull first: nothing is caught after it. The bat is dead, and what a later prize
// started would outlive it (the Skull ended every effect), or, for the star, end the level from a lost bat.
void testSkullFirstInAStep() {
    Game g(levelOf({board()}));
    check(g.ready(), "skull first: the bat comes in");
    g.launch();
    const Vec3 from{g.bat().x, 0, g.bat().z + 1};
    g.session.dropPrize(PrizeKind::Skull, from);
    g.session.dropPrize(PrizeKind::BigBall, from);
    g.session.dropPrize(PrizeKind::NextLevel, from);
    g.run(0.2);
    check(g.count(EventType::PrizeCaught, PrizeKind::Skull) == 1 && g.session.phase() == SessionPhase::Lost,
          "skull first: the Skull is caught and the bat is dead");
    check(g.count(EventType::PrizeCaught, PrizeKind::BigBall) == 0 && g.session.effect(PrizeKind::BigBall) == nullptr &&
              g.count(EventType::EffectStarted) == 0 && near(g.session.ballRadius(), 0.6f),
          "skull first: the Big Ball behind it is not caught, and starts nothing that would outlive the bat");
    check(g.count(EventType::PrizeCaught, PrizeKind::NextLevel) == 0 && g.count(EventType::LevelComplete) == 0 &&
              g.session.phase() == SessionPhase::Lost,
          "skull first: nor does the star end the level");
    g.run(0.5);
    check(g.count(EventType::PrizeMissed) == 2, "skull first: they fall on and are missed");
    g.run(4);
    check(g.session.phase() == SessionPhase::Ready && g.session.activeEffects().empty() && near(g.session.ballRadius(), 0.6f),
          "skull first: the next bat is plain");

    // The same two the other way round: the Big Ball is caught, and the Skull ends it with the rest.
    Game h(levelOf({board()}));
    h.ready();
    h.launch();
    h.session.dropPrize(PrizeKind::BigBall, from);
    h.session.dropPrize(PrizeKind::Skull, from);
    h.run(0.2);
    check(h.count(EventType::PrizeCaught, PrizeKind::BigBall) == 1 && h.count(EventType::EffectEnded, PrizeKind::BigBall) == 1 &&
              h.session.effect(PrizeKind::BigBall) == nullptr,
          "skull first: and when the Big Ball is first it is caught, and the Skull takes it away");
}

void testSpeedPrizes() {
    Game g(levelOf({board()}));
    check(g.ready(), "speed: the bat comes in");
    g.launch();
    check(near(g.session.ballSpeed(), 15.0f), "speed: 15 units/s to start");
    g.give(PrizeKind::SpeedUp);
    const float up = std::hypot(g.session.balls()[0].velocity.x, g.session.balls()[0].velocity.z);
    check(near(g.session.ballSpeed(), 16.5f, 1e-3f) && near(up, 16.5f, 1e-3f),
          "speed: Increase Ball Speed is x1.10, at once, " + num(up));
    g.run(0.5);
    check(near(std::hypot(g.session.balls()[0].velocity.x, g.session.balls()[0].velocity.z), 16.5f, 0.02f), "speed: and holds");
    g.give(PrizeKind::SlowDown);
    check(near(g.session.ballSpeed(), 15.015f, 1e-3f), "speed: Decrease Ball Speed is x0.91 of that, so the two nearly cancel");
    check(g.session.activeEffects().empty(), "speed: no timer and no icon");
    const int id = g.session.addBall({10, 0.6f, 5}, {0, 0, 1});
    g.step();
    check(g.ball(id) != nullptr && near(std::hypot(g.ball(id)->velocity.x, g.ball(id)->velocity.z), 15.015f, 0.02f),
          "speed: a ball made afterwards goes at the new speed");
    for (int i = 0; i < 12; ++i) {
        g.give(PrizeKind::SpeedUp);
    }
    check(near(g.session.ballSpeed(), 30.0f), "speed: and there is a ceiling of twice, " + num(g.session.ballSpeed()));
    for (int i = 0; i < 40; ++i) {
        g.give(PrizeKind::SlowDown);
    }
    check(near(g.session.ballSpeed(), 7.5f), "speed: and a floor of half");
}

void testBatSize() {
    Game g(levelOf({board()}));
    check(g.ready(), "bat: the bat comes in");
    check(near(g.session.batRadius(), 3.0f) && g.session.batScale() == 1, "bat: radius 3 to start");
    g.give(PrizeKind::IncBat);
    check(near(g.session.batScale(), 1.24f) && near(g.session.batRadius(), 3.72f), "bat: Increase Bat Size is +24 %");
    g.give(PrizeKind::IncBat);
    g.give(PrizeKind::IncBat);
    g.give(PrizeKind::IncBat);
    check(near(g.session.batScale(), 1.9f), "bat: up to 1.9 times");
    g.run(0.2, {.batTargetX = 100});
    check(g.bat().x <= 19.0f - g.session.batRadius() + 0.01f && g.bat().x > 12.0f,
          "bat: and it keeps its edge inside the walls, centre x " + num(g.bat().x));
    g.give(PrizeKind::DecBat);
    check(near(g.session.batScale(), 1.9f / 1.24f), "bat: Decrease Bat Size is the inverse");
    for (int i = 0; i < 9; ++i) {
        g.give(PrizeKind::DecBat);
    }
    check(near(g.session.batScale(), 0.6f), "bat: down to 0.6 times");
    check(g.session.activeEffects().empty(), "bat: no timer");

    // The collision sphere really is the size: a ball 4 units to the side of the bat's centre passes a plain bat and meets a wide one.
    const auto batHits = [](PrizeKind prize, float offset) {
        Game game(levelOf({board()}));
        game.ready();
        if (prize != PrizeKind::Bonus) {
            game.give(prize);
        }
        game.session.addBall({offset, 0.6f, -10}, {0, 0, -1});
        game.run(1.5);
        return std::pair{game.session.stats().batHits, game.count(EventType::BallLost)};
    };
    check(batHits(PrizeKind::Bonus, 4.0f) == std::pair(0, 1), "bat: a plain bat does not reach a ball 4.0 aside");
    check(batHits(PrizeKind::IncBat, 4.0f) == std::pair(1, 0), "bat: a wide one (3.72 + 0.6) does");
    check(batHits(PrizeKind::DecBat, 3.3f) == std::pair(0, 1) && batHits(PrizeKind::Bonus, 3.3f) == std::pair(1, 0),
          "bat: and a narrow one (2.42 + 0.6) does not reach 3.3");

    // A bat lost is replaced by a plain one.
    Game lost(levelOf({board()}));
    check(lost.ready(), "bat: a bat for the loss");
    lost.give(PrizeKind::IncBat);
    lost.catches(PrizeKind::Skull);
    lost.run(3);
    check(lost.session.batScale() == 1 && near(lost.session.batRadius(), 3.0f) && lost.session.phase() == SessionPhase::Ready,
          "bat: the next bat is the plain one");
}

void testBallSize() {
    Game g(levelOf({board()}));
    check(g.ready(), "size: the bat comes in");
    check(near(g.session.ballRadius(), 0.6f) && near(g.session.balls()[0].radius, 0.6f), "size: radius 0.6 to start");
    g.give(PrizeKind::BigBall);
    check(near(g.session.ballRadius(), 1.0f) && near(g.session.balls()[0].radius, 1.0f), "size: Big Ball is 1.0");
    check(g.count(EventType::EffectStarted, PrizeKind::BigBall) == 1 && g.session.activeEffects().size() == 1, "size: it is an effect");
    g.give(PrizeKind::SmallBall);
    check(near(g.session.ballRadius(), 0.4f) && g.session.effect(PrizeKind::BigBall) == nullptr &&
              g.count(EventType::EffectEnded, PrizeKind::BigBall) == 1,
          "size: Weak Ball is 0.4, and takes the Big Ball's place");
    g.give(PrizeKind::BigBall);
    check(near(g.session.ballRadius(), 1.0f) && g.session.effect(PrizeKind::SmallBall) == nullptr, "size: and the other way");
    g.launch();
    check(near(g.session.balls()[0].radius, 1.0f) && near(g.session.ballRadius(), 1.0f), "size: the launched ball is big");
    const int id = g.session.addBall({10, 0.6f, 5}, {0, 0, 1});
    check(g.ball(id) != nullptr && near(g.ball(id)->radius, 1.0f), "size: so is a new one");
    g.run(20.1);
    check(near(g.session.ballRadius(), 0.6f) && g.session.effect(PrizeKind::BigBall) == nullptr &&
              g.count(EventType::EffectEnded, PrizeKind::BigBall) == 2,
          "size: after 20 s it is the plain ball again");
    check(g.ball(id) == nullptr || near(g.ball(id)->radius, 0.6f), "size: all of them");

    // The body is the size: a ball 3.9 to the side of the bat's centre is missed by a plain ball (3.6) and met by a big one (4.0), and
    // one 3.5 aside is met by a plain ball and missed by a weak one (3.4).
    const auto batHits = [](PrizeKind prize, float offset) {
        Game game(levelOf({board()}));
        game.ready();
        if (prize != PrizeKind::Bonus) {
            game.give(prize);
        }
        game.session.addBall({offset, 0.6f, -10}, {0, 0, -1});
        game.run(1.5);
        return game.session.stats().batHits;
    };
    check(batHits(PrizeKind::Bonus, 3.9f) == 0 && batHits(PrizeKind::BigBall, 3.9f) == 1, "size: a big ball reaches the bat from farther");
    check(batHits(PrizeKind::Bonus, 3.5f) == 1 && batHits(PrizeKind::SmallBall, 3.5f) == 0, "size: and a weak one from less far");
}

void testPowerBall() {
    const auto energyAfterOneHit = [](bool power) {
        Game g(levelOf({board(), brick({0, 1, 10}, "Tough.jpg")}));
        g.ready();
        if (power) {
            g.give(PrizeKind::PowerBall);
        }
        g.session.addBall({0, 0.6f, 3}, {0, 0, 1});
        g.run(1.0);
        return std::pair{g.session.actor(1).energy, g.count(EventType::BrickHit)};
    };
    check(energyAfterOneHit(false) == std::pair(2, 1), "power: a plain ball takes one of three hits");
    check(energyAfterOneHit(true) == std::pair(0, 1), "power: a Power Ball takes them all in one hit");
    // The brick destroyed by a Power Ball scores as usual.
    Game g(levelOf({board(), brick({0, 1, 10}, "Tough.jpg")}));
    g.ready();
    g.give(PrizeKind::PowerBall);
    g.session.addBall({0, 0.6f, 3}, {0, 0, 1});
    g.run(1.0);
    check(g.count(EventType::BrickDestroyed) == 1 && g.find(EventType::BrickDestroyed)->points == 100, "power: and pays its 100");
    // Not other hits: a bullet takes the energy one hit at a time.
    Game other(levelOf({board(), brick({0, 1, 10}, "Tough.jpg")}));
    other.ready();
    other.give(PrizeKind::PowerBall);
    other.session.hit(1, {});
    check(other.session.actor(1).energy == 2, "power: only the ball's hits");
}

void testExplosiveBall() {
    // A at the ball's end, B beside it within the blast (1.5 from the contact), C beyond (6).
    const auto energies = [](bool explosive) {
        Game g(levelOf({board(), brick({0, 1, 10}, "Tough.jpg"), brick({2.5f, 1, 10}, "Tough.jpg"), brick({8, 1, 10}, "Tough.jpg")}));
        g.ready();
        if (explosive) {
            g.give(PrizeKind::ExplosiveBall);
        }
        g.session.addBall({0, 0.6f, 3}, {0, 0, 1});
        g.run(1.0);
        return std::array<int, 3>{g.session.actor(1).energy, g.session.actor(2).energy, g.session.actor(3).energy};
    };
    check(energies(false) == std::array<int, 3>{2, 3, 3}, "explosive: a plain ball hurts only what it touches");
    check(energies(true) == std::array<int, 3>{2, 2, 3}, "explosive: the blast hurts the neighbour within 2.5 and not the one at 6");

    Game g(levelOf({board(), brick({0, 1, 10}, "Tough.jpg"), brick({8, 1, 10}, "Tough.jpg")}));
    g.ready();
    g.give(PrizeKind::ExplosiveBall);
    check(g.session.effect(PrizeKind::ExplosiveBall) != nullptr && g.session.effect(PrizeKind::ExplosiveBall)->shots == 1 &&
              g.session.effect(PrizeKind::ExplosiveBall)->duration == 0,
          "explosive: one blast, no timer");
    g.session.addBall({0, 0.6f, 3}, {0, 0, 1});
    g.run(1.0);
    const GameEvent* blast = g.find(EventType::Blast);
    check(blast != nullptr && near(blast->speed, 2.5f) && near(blast->position.z, 9.0f, 0.5f), "explosive: a Blast event with its radius at the contact");
    check(g.count(EventType::Blast) == 1 && g.session.effect(PrizeKind::ExplosiveBall) == nullptr &&
              g.count(EventType::EffectEnded, PrizeKind::ExplosiveBall) == 1,
          "explosive: and it is spent");
    g.give(PrizeKind::ExplosiveBall);
    g.give(PrizeKind::ExplosiveBall);
    check(g.session.effect(PrizeKind::ExplosiveBall)->shots == 2 && g.session.activeEffects().size() == 1, "explosive: a second catch adds a blast");
}

// A ball that touches two bricks in one step hurts each of them once. The blast looks for what the ball did not hit, and the order of
// the physics' contacts must not decide which brick takes the blast as well as the blow.
void testExplosiveBallTwoBricksAtOnce() {
    Game g(levelOf({board(), brick({-1, 1, 10}, "Tough.jpg"), brick({1, 1, 10}, "Tough.jpg")}));  // side by side: a seam at x = 0
    g.ready();
    g.give(PrizeKind::ExplosiveBall);
    g.session.addBall({0, 0.6f, 3}, {0, 0, 1});  // onto the seam: both faces in the same step
    g.run(0.8);
    int hits = 0;
    for (const GameEvent& event : g.log) {
        hits += event.type == EventType::BrickHit && event.ball >= 0;
    }
    check(hits == 2 && g.count(EventType::Blast) == 1, "explosive: a ball on the seam of two bricks hits both (" + std::to_string(hits) + " hits)");
    check(g.session.actor(1).energy == 2 && g.session.actor(2).energy == 2 && g.session.stats().bricksHit == 2,
          "explosive: each takes one hit, the blow, not the blow and the blast, " + std::to_string(g.session.actor(1).energy) + " and " +
              std::to_string(g.session.actor(2).energy) + " left of three");
}

// A blow is a contact that closes at 30 % of the ball's speed, whatever the speed prizes made it (GameplayRules::hitFraction): a graze at
// 3 units/s of the 15 is none, and at the doubled speed one at 6 is none either, while one at 3 is a blow when the speed is halved.
void testBlowFollowsTheBallSpeed() {
    const auto hits = [](int speedUps, int slowDowns, float sine) {
        Game g(levelOf({board(), brick({3, 1, 10}, "Cheap.jpg", kStatic, {1, 1, 4})}));  // a wall whose face is at x = 2
        g.ready();
        for (int i = 0; i < speedUps; ++i) {
            g.session.givePrize(PrizeKind::SpeedUp);
        }
        for (int i = 0; i < slowDowns; ++i) {
            g.session.givePrize(PrizeKind::SlowDown);
        }
        g.session.addBall({1.0f, 0.6f, 6}, {sine, 0, std::sqrt(1 - sine * sine)});  // closes on the face at sine x the speed
        g.run(0.6);
        return g.count(EventType::BrickHit);
    };
    check(hits(0, 0, 0.2f) == 0 && hits(0, 0, 0.4f) == 1, "blow: at speed 15 a closing speed of 3 is a graze and 6 a blow");
    check(hits(8, 0, 0.2f) == 0 && hits(8, 0, 0.4f) == 1, "blow: at speed 30 a closing speed of 6 is a graze, 12 a blow");
    check(hits(0, 8, 0.2f) == 0 && hits(0, 8, 0.4f) == 1, "blow: at speed 7.5 a closing speed of 1.5 is a graze, and 3 a blow");
}

void testNormalBall() {
    Game g(levelOf({board()}));
    check(g.ready(), "normal: the bat comes in");
    for (const PrizeKind kind : {PrizeKind::BigBall, PrizeKind::PowerBall, PrizeKind::ExplosiveBall, PrizeKind::Drunken, PrizeKind::YoYo,
                                 PrizeKind::Magnet, PrizeKind::Wall, PrizeKind::Cannon, PrizeKind::SpeedUp}) {
        g.give(kind);
    }
    check(g.session.activeEffects().size() == 8 && near(g.session.ballRadius(), 1.0f), "normal: eight effects, and a big ball");
    check(g.catches(PrizeKind::NormalBall), "normal: caught");
    check(g.session.activeEffects().size() == 3 && g.session.effect(PrizeKind::Magnet) && g.session.effect(PrizeKind::Wall) &&
              g.session.effect(PrizeKind::Cannon),
          "normal: the ball's effects are gone and the bat's stay: the Magnet, the Wall and the weapon");
    check(near(g.session.ballRadius(), 0.6f) && near(g.session.ballSpeed(), 16.5f), "normal: the ball is the plain size, at the speed it had");
    check(g.count(EventType::EffectEnded) == 5, "normal: five effects ended");
    g.give(PrizeKind::NormalBall);
    check(g.count(EventType::EffectEnded) == 5, "normal: and with nothing to cancel it does nothing");
}

void testDrunkenBall() {
    const auto angles = [](bool drunk) {
        Game g(levelOf({board()}));
        g.ready();
        g.session.addBall({-15, 0.6f, -8}, {0.2f, 0, 1});
        if (drunk) {
            g.give(PrizeKind::Drunken);
        }
        g.step();
        const int id = g.session.balls().back().id;
        float low = 999, high = -999, worstSpeed = 0;
        for (int i = 0; i < 96; ++i) {  // one period of 1.6 s
            g.step();
            const BallView* ball = g.ball(id);
            if (ball == nullptr) {
                return std::array<float, 3>{0, 0, 999};
            }
            low = std::min(low, heading(*ball));
            high = std::max(high, heading(*ball));
            worstSpeed = std::max(worstSpeed, std::fabs(std::hypot(ball->velocity.x, ball->velocity.z) - 15.0f));
        }
        return std::array<float, 3>{low, high, worstSpeed};
    };
    const auto straight = angles(false), drunk = angles(true);
    check(straight[1] - straight[0] < 0.5f, "drunk: a plain ball keeps its heading");
    check(drunk[1] - drunk[0] > 8.0f && drunk[1] - drunk[0] < 12.0f,
          "drunk: and a drunken one wobbles about 10 degrees from side to side, " + num(drunk[1] - drunk[0]));
    check(drunk[2] < 0.05f, "drunk: at the same speed");
    Game g(levelOf({board()}));
    g.ready();
    g.give(PrizeKind::Drunken);
    check(g.session.effect(PrizeKind::Drunken)->timeLeft == 30, "drunk: 30 s");
}

void testMultiball() {
    Game g(levelOf({board()}));
    check(g.ready(), "multi: the bat comes in");
    g.give(PrizeKind::Multiball);
    check(g.session.balls().size() == 1 && g.count(EventType::BallLaunched) == 0,
          "multi: with only a ball held on the bat there is nothing to multiply");
    g.launch();
    g.run(0.5);
    const BallView original = g.session.balls()[0];
    const float angle = heading(original);
    g.give(PrizeKind::Multiball);
    check(g.session.balls().size() == 3 && g.count(EventType::BallLaunched) == 3, "multi: three balls, two of them new");
    g.step();
    check(g.session.balls().size() == 3, "multi: and they stay");
    const BallView& a = g.session.balls()[1];
    const BallView& b = g.session.balls()[2];
    float turnA = heading(a) - heading(g.session.balls()[0]), turnB = heading(b) - heading(g.session.balls()[0]);
    turnA += turnA < -180 ? 360 : (turnA > 180 ? -360 : 0);
    turnB += turnB < -180 ? 360 : (turnB > 180 ? -360 : 0);
    check(near(turnA, -20.0f, 1.0f) && near(turnB, 98.0f, 1.0f),
          "multi: headed -20 and +98 degrees from the original, " + num(turnA) + " and " + num(turnB));
    check(near(heading(g.session.balls()[0]), angle, 1.0f), "multi: the original goes on");
    for (const BallView* ball : {&a, &b}) {
        check(length(ball->position - g.session.balls()[0].position) < 2.0f && near(std::hypot(ball->velocity.x, ball->velocity.z), 15.0f, 0.05f),
              "multi: beside the original, at the ball speed");
    }
    // No more than twelve.
    for (int i = 0; i < 10; ++i) {
        g.give(PrizeKind::Multiball);
    }
    check(g.session.balls().size() == 12, "multi: twelve at most, " + std::to_string(g.session.balls().size()));
    // A life is lost only with the last.
    g.run(10);
    check(g.session.hearts() == 2 || g.session.balls().empty(), "multi: balls lost one by one do not cost a heart");
}

void testMagnet() {
    Game g(levelOf({board()}));
    check(g.ready(), "magnet: the bat comes in");
    g.launch();
    g.give(PrizeKind::Magnet);
    check(g.session.effect(PrizeKind::Magnet)->timeLeft == 30, "magnet: 30 s");
    const int id = g.session.addBall({0.5f, 0.6f, -12}, {0, 0, -1});
    check(g.runUntil([&] { return g.count(EventType::BallStuck) > 0; }, 1.0), "magnet: the ball that reaches the bat sticks");
    const BallView* ball = g.ball(id);
    check(ball != nullptr && ball->stuck && !ball->held && ball->velocity.x == 0 && ball->velocity.z == 0, "magnet: and is held, still");
    if (ball == nullptr) {
        return;
    }
    const Vec3 offset = ball->position - g.bat();
    check(length(offset) > 3.3f && length(offset) < 3.8f && std::fabs(offset.x) < 1.0f && offset.z > 3.0f,
          "magnet: on the bat's arch, where it touched, offset " + num(offset.x) + ", " + num(offset.z));
    const GameEvent* stuck = g.find(EventType::BallStuck);
    check(stuck != nullptr && stuck->ball == id, "magnet: BallStuck names the ball");
    // It goes with the bat.
    g.run(1.0, {.batTargetX = 12});
    ball = g.ball(id);
    check(ball != nullptr && ball->stuck && near(g.bat().x, 12.0f, 0.2f) && length((ball->position - g.bat()) - offset) < 1e-3f,
          "magnet: it rides with the bat for a second while the bat moves 12 units");
    // A click lets it go, away from the bat.
    const std::size_t from = g.log.size();
    g.step({.batTargetX = 12, .launch = true});
    ball = g.ball(id);
    check(ball != nullptr && !ball->stuck && ball->velocity.z > 14.0f && near(std::hypot(ball->velocity.x, ball->velocity.z), 15.0f, 0.05f),
          "magnet: a click releases it, up the board at 15 units/s");
    check(g.find(EventType::BallLaunched, from) != nullptr, "magnet: with a BallLaunched");
    g.run(0.5, {.batTargetX = 12});
    check(g.count(EventType::BallStuck) == 1, "magnet: it does not stick again as it leaves");

    // Left alone it is let go after magnetHold seconds.
    Game timed(levelOf({board()}));
    timed.ready();
    timed.launch();
    timed.give(PrizeKind::Magnet);
    const int id2 = timed.session.addBall({0.5f, 0.6f, -12}, {0, 0, -1});
    timed.runUntil([&] { return timed.count(EventType::BallStuck) > 0; }, 1.0);
    const double stuckAt = timed.session.time();
    timed.runUntil([&] { return timed.ball(id2) != nullptr && !timed.ball(id2)->stuck; }, 6.0);
    check(near(static_cast<float>(timed.session.time() - stuckAt), 4.0f, 0.1f), "magnet: after 4 s it lets go by itself");

    // And with the magnet when it ends.
    SessionOptions long_ = options();
    long_.rules.prizes.magnetHold = 1000;
    Game ends(levelOf({board()}), long_);
    ends.ready();
    ends.launch();
    ends.give(PrizeKind::Magnet);
    const int id3 = ends.session.addBall({0.5f, 0.6f, -12}, {0, 0, -1});
    ends.runUntil([&] { return ends.ball(id3) && ends.ball(id3)->stuck; }, 1.0);
    ends.run(25);
    check(ends.ball(id3) != nullptr && ends.ball(id3)->stuck, "magnet: held for 25 s");
    ends.run(5.1);
    check(ends.session.effect(PrizeKind::Magnet) == nullptr && ends.ball(id3) != nullptr && !ends.ball(id3)->stuck,
          "magnet: and let go when its 30 s are up");
    // The autopilot lets go of what the magnet holds.
    Game pilot(levelOf({board()}));
    pilot.ready();
    pilot.launch();
    pilot.give(PrizeKind::Magnet);
    const int id4 = pilot.session.addBall({0.5f, 0.6f, -12}, {0, 0, -1});
    pilot.runUntil([&] { return pilot.ball(id4) && pilot.ball(id4)->stuck; }, 1.0);
    pilot.step(Autopilot{}.decide(pilot.session));
    pilot.step();
    check(pilot.ball(id4) != nullptr && !pilot.ball(id4)->stuck, "magnet: the autopilot clicks");
}

void testYoYo() {
    Game g(levelOf({board()}));
    check(g.ready(), "yoyo: the bat comes in");
    g.launch();
    g.give(PrizeKind::YoYo);
    check(g.session.effect(PrizeKind::YoYo)->timeLeft == 7.5, "yoyo: 7.5 s");
    float farthest = 0;
    int frames = 0;
    for (int i = 0; i < 60 * 7 && g.session.effect(PrizeKind::YoYo) != nullptr; ++i) {
        GameInput input = Autopilot{}.decide(g.session);
        input.launch = false;
        g.step(input);
        for (const BallView& ball : g.session.balls()) {
            if (!ball.held) {
                farthest = std::max(farthest, std::hypot(ball.position.x - g.bat().x, ball.position.z - g.bat().z));
            }
        }
        ++frames;
    }
    check(frames > 400 && g.count(EventType::BallLost) == 0, "yoyo: the ball stayed in play for seven seconds");
    check(farthest <= 7.0f + 0.5f && farthest > 5.0f, "yoyo: and never farther from the bat than the string, " + num(farthest));
    check(g.count(EventType::BallRubbered) > 0, "yoyo: each time the string turns the ball back it reports it (the sound ballRubbered)");
    g.run(0.6);
    check(g.session.effect(PrizeKind::YoYo) == nullptr && g.count(EventType::EffectEnded, PrizeKind::YoYo) == 1, "yoyo: it ends after 7.5 s");

    Game free(levelOf({board()}));
    free.ready();
    free.launch();
    float farthestFree = 0;
    for (int i = 0; i < 60 * 3; ++i) {
        free.step();
        farthestFree = std::max(farthestFree, std::hypot(free.session.balls()[0].position.x - free.bat().x,
                                                          free.session.balls()[0].position.z - free.bat().z));
    }
    check(farthestFree > 15.0f, "yoyo: without it the ball goes where it likes, " + num(farthestFree));
    check(free.count(EventType::BallRubbered) == 0, "yoyo: and nothing turns it back");
}

void testWall() {
    // A ball that goes by the bat at x = 10 meets the rope at z -20 and comes back; without it, the ball is lost.
    struct Outcome {
        bool alive = false;
        int lost = 0;
        float turnedAt = 0;  // z of the bounce
        float velocityZ = 0;
    };
    const auto outcome = [](bool wall) {
        Game g(levelOf({board()}));
        g.ready();
        g.launch();
        if (wall) {
            g.give(PrizeKind::Wall);
        }
        const int id = g.session.addBall({10, 0.6f, -5}, {0, 0, -1});
        g.run(1.5);
        const GameEvent* hit = g.find(EventType::BoardHit);
        return Outcome{g.ball(id) != nullptr, g.count(EventType::BallLost), hit != nullptr ? hit->position.z : 0.0f,
                       g.ball(id) != nullptr ? g.ball(id)->velocity.z : 0.0f};
    };
    const Outcome without = outcome(false), with = outcome(true);
    check(!without.alive && without.lost == 1, "wall: without it the ball goes by the bat and is lost");
    check(with.alive && with.lost == 0 && with.velocityZ > 0, "wall: with it the ball comes back");
    check(near(with.turnedAt, -19.04f, 0.3f), "wall: from its far face, the ball's centre at z = -20.01 + 0.37 + 0.6, " + num(with.turnedAt));
    Game g(levelOf({board()}));
    g.ready();
    g.give(PrizeKind::Wall);
    check(g.session.effect(PrizeKind::Wall)->timeLeft == 30, "wall: 30 s");
    g.run(30.1);
    check(g.session.effect(PrizeKind::Wall) == nullptr, "wall: and gone after that");
}

void testNight() {
    Game g(levelOf({board()}));
    check(g.ready() && g.session.nightLevel() == 0, "night: day to start");
    g.give(PrizeKind::Night);
    check(g.session.nightLevel() == 0 && g.session.effect(PrizeKind::Night)->timeLeft == 10, "night: 10 s");
    g.run(0.5);
    check(near(g.session.nightLevel(), 0.5f, 0.03f), "night: dark within a second, half at 0.5 s");
    g.run(1.0);
    check(near(g.session.nightLevel(), 1.0f), "night: full dark");
    g.run(6.5);  // at 8 s
    check(near(g.session.nightLevel(), 1.0f), "night: held");
    g.run(1.5);  // at 9.5 s: 0.5 s left of the 1.5 s the light takes to come back
    check(near(g.session.nightLevel(), 0.33f, 0.05f), "night: and the light returns over the last 1.5 s, " + num(g.session.nightLevel()));
    g.run(0.6);
    check(g.session.nightLevel() == 0 && g.session.effect(PrizeKind::Night) == nullptr && g.count(EventType::EffectEnded, PrizeKind::Night) == 1,
          "night: and it is over");
    // The HUD counts down in whole seconds.
    g.give(PrizeKind::Night);
    g.run(2.0);
    check(std::ceil(g.session.effect(PrizeKind::Night)->timeLeft) == 8, "night: the HUD says 8 after 2 s");
}

void testQuake() {
    // Bricks standing on the floor, apart, asleep: a quake wakes them and throws them about for 3 s.
    std::vector<Actor> actors = {board()};
    for (int i = 0; i < 6; ++i) {
        actors.push_back(brick({-12.0f + 5.0f * static_cast<float>(i), 1, 8}, "Tough.jpg", kDynamic));
    }
    const auto jump = [&](bool quake) {
        Game g(levelOf(actors));
        g.ready();
        if (quake) {
            g.give(PrizeKind::Quake);
        }
        float highest = 0, moved = 0;
        for (int i = 0; i < 200; ++i) {
            g.step();
            for (ActorIndex a = 1; a <= 6; ++a) {
                const Vec3 at = g.session.scene().actorPose(a).translationPart();
                highest = std::max(highest, at.y - 1);
                moved = std::max(moved, std::hypot(at.x - (-12.0f + 5.0f * static_cast<float>(a - 1)), at.z - 8));
            }
        }
        return std::array<float, 3>{highest, moved, g.session.effect(PrizeKind::Quake) == nullptr ? 1.0f : 0.0f};
    };
    const auto still = jump(false), shaken = jump(true);
    check(still[0] < 0.01f && still[1] < 0.01f, "quake: bricks left alone stand still");
    check(shaken[0] > 0.1f && shaken[1] > 0.1f, "quake: and are thrown up and about by the quake, " + num(shaken[0]) + " up, " + num(shaken[1]) + " aside");
    check(shaken[2] == 1.0f, "quake: it ends after 3 s");
    Game g(levelOf({board()}));
    g.ready();
    g.give(PrizeKind::Quake);
    check(g.session.effect(PrizeKind::Quake)->duration == 3 && g.session.effect(PrizeKind::Quake)->timeLeft == 3, "quake: 3 s");
}

void testWind() {
    // A brick that slides without friction, along z: the wind pushes it sideways.
    const auto drift = [](bool wind) {
        Actor ice = brick({0, 1, 0}, "Ice.jpg", kDynamic);
        ice.body->linearVelocity = {0, 0, 1};
        SessionOptions o = options();
        o.physics.start = StartState::SavedVelocities;
        Game g(levelOf({board(), ice}), o);
        g.ready();
        if (wind) {
            g.give(PrizeKind::Wind);
        }
        g.run(1.5);
        return g.session.scene().actorPose(1).translationPart().x;
    };
    check(std::fabs(drift(false)) < 0.05f, "wind: no wind, no drift");
    check(std::fabs(drift(true)) > 2.0f, "wind: a piece that is moving is blown sideways, " + num(drift(true)));
    Game g(levelOf({board()}));
    g.ready();
    g.give(PrizeKind::Wind);
    check(g.session.effect(PrizeKind::Wind)->duration == 5, "wind: 5 s");
    g.run(5.1);
    check(g.session.effect(PrizeKind::Wind) == nullptr, "wind: and gone");
    // Leaves the balls alone.
    Game balls(levelOf({board()}));
    balls.ready();
    balls.launch();
    balls.give(PrizeKind::Wind);
    balls.run(1);
    check(near(std::hypot(balls.session.balls()[0].velocity.x, balls.session.balls()[0].velocity.z), 15.0f, 0.02f), "wind: the ball's path stays");
}

void testSurprise() {
    SessionOptions o = options();
    o.prizeSeed = 11;
    Game g(levelOf({board()}), o);
    check(g.ready(), "surprise: the bat comes in");
    g.launch();
    std::set<PrizeKind> gifts;
    for (int i = 0; i < 300; ++i) {
        const std::size_t from = g.log.size();
        g.give(PrizeKind::Surprise);
        const GameEvent* gift = g.find(EventType::PrizeCaught, from);
        check(gift != nullptr && gift->prize && gift->points == 0 && gift->material != nullptr, "surprise: the gift is announced as a catch worth nothing");
        if (gift != nullptr && gift->prize) {
            gifts.insert(*gift->prize);
            check(!prizeInfo(*gift->prize).bad && *gift->prize != PrizeKind::Surprise && *gift->prize != PrizeKind::NextLevel,
                  "surprise: a good prize, no Surprise, no star");
        }
        g.step();
        if (g.session.phase() != SessionPhase::Playing) {
            break;
        }
    }
    check(gifts.size() >= 12, "surprise: and from the whole mix, " + std::to_string(gifts.size()) + " kinds in 300");
    // A real catch is the Surprise's 299 and then the gift.
    Game c(levelOf({board()}), o);
    c.ready();
    c.launch();
    const std::int64_t before = c.session.score();
    c.catches(PrizeKind::Surprise);
    int caught = 0;
    for (const GameEvent& e : c.log) {
        caught += e.type == EventType::PrizeCaught;
    }
    check(caught == 2 && c.session.score() - before == 299, "surprise: caught for 299, announced twice");
}

void testWeapons() {
    Game g(levelOf({board()}));
    check(g.ready() && !g.session.weapon() && !g.session.useShot(), "weapon: none to start");
    g.give(PrizeKind::Cannon);
    check(g.session.weapon() && g.session.weapon()->type == WeaponType::Cannon && g.session.weapon()->shots == 6, "weapon: the Cannon comes with 6 shots");
    check(g.session.shotsLeft(WeaponType::Cannon) == 6 && g.session.shotsLeft(WeaponType::Gun) == 0, "weapon: counted by type");
    check(g.count(EventType::EffectStarted, PrizeKind::Cannon) == 1 && g.session.activeEffects()[0].shots == 6 &&
              g.session.activeEffects()[0].duration == 0,
          "weapon: an effect that counts shots, with no timer");
    check(g.session.useShot() && g.session.weapon()->shots == 5, "weapon: a shot is taken");
    g.give(PrizeKind::Bazooka);
    check(g.session.weapon()->type == WeaponType::Bazooka && g.session.weapon()->shots == 7 && g.session.activeEffects().size() == 2,
          "weapon: the one caught last is the one a click fires, and the other waits (V2 108-117)");
    g.give(PrizeKind::Cannon);
    check(g.session.weapon()->type == WeaponType::Cannon && g.session.weapon()->shots == 11 && g.session.activeEffects().size() == 2 &&
              g.count(EventType::EffectStarted, PrizeKind::Cannon) == 1,
          "weapon: the same again adds its 6 shots (5 + 6) and makes it the current one");
    g.give(PrizeKind::Laser);
    g.give(PrizeKind::Gun);
    check(g.session.shotsLeft(WeaponType::Laser) == 5 && g.session.shotsLeft(WeaponType::Gun) == 12 && g.session.weapon()->type == WeaponType::Gun,
          "weapon: the Laser has 5 and the machine gun 12");
    for (int i = 0; i < 12; ++i) {
        check(g.session.useShot(), "weapon: shot " + std::to_string(i));
    }
    g.flush();
    check(g.session.shotsLeft(WeaponType::Gun) == 0 && g.session.effect(PrizeKind::Gun) == nullptr &&
              g.count(EventType::EffectEnded, PrizeKind::Gun) == 1,
          "weapon: the last shot ends the effect");
    check(g.session.weapon()->type == WeaponType::Laser, "weapon: and the one before comes back");
    for (int i = 0; i < 5 + 11 + 7; ++i) {
        g.session.useShot();
    }
    check(!g.session.weapon() && !g.session.useShot() && g.session.activeEffects().empty(), "weapon: all spent");
}

void testTimersAndRefresh() {
    struct Timed {
        PrizeKind kind;
        double seconds;
    };
    const Timed timed[] = {{PrizeKind::BigBall, 20}, {PrizeKind::SmallBall, 20}, {PrizeKind::PowerBall, 20}, {PrizeKind::Drunken, 30},
                           {PrizeKind::Magnet, 30}, {PrizeKind::Night, 10}, {PrizeKind::Wall, 30}, {PrizeKind::YoYo, 7.5},
                           {PrizeKind::Quake, 3}, {PrizeKind::Wind, 5}};
    Game g(levelOf({board()}));
    g.ready();
    for (const Timed& t : timed) {
        const std::string name(prizeInfo(t.kind).stem);
        g.give(t.kind);
        const ActiveEffect* effect = g.session.effect(t.kind);
        check(effect != nullptr && effect->timeLeft == t.seconds && effect->duration == t.seconds, "timer: " + name + " starts at " + num(t.seconds));
        g.run(t.seconds - 0.2);
        effect = g.session.effect(t.kind);
        check(effect != nullptr && effect->timeLeft > 0.1 && effect->timeLeft < 0.3, "timer: " + name + " has 0.2 s left");
        g.run(0.4);
        check(g.session.effect(t.kind) == nullptr && g.count(EventType::EffectEnded, t.kind) == 1, "timer: " + name + " ends");
    }
    // The same prize again starts the time over; it does not add up, and is one effect, with one start.
    g.give(PrizeKind::Wall);
    g.run(20);
    check(near(static_cast<float>(g.session.effect(PrizeKind::Wall)->timeLeft), 10.0f, 0.05f), "timer: 10 s left of the Wall");
    g.give(PrizeKind::Wall);
    check(g.session.effect(PrizeKind::Wall)->timeLeft == 30 && g.session.activeEffects().size() == 1 && g.count(EventType::EffectStarted, PrizeKind::Wall) == 2,
          "timer: caught again it has its 30 s again, and is not on the list twice");
    // The icons are listed oldest first and stay in their places.
    g.give(PrizeKind::Night);
    g.give(PrizeKind::Wall);
    check(g.session.activeEffects().size() == 2 && g.session.activeEffects()[0].kind == PrizeKind::Wall &&
              g.session.activeEffects()[1].kind == PrizeKind::Night,
          "timer: oldest first, and a refresh does not move an icon");
}

void testEffectsEndWithTheBat() {
    // No far wall: the ball leaves the board and is lost.
    Game g(levelOf({board(false, false)}));
    check(g.ready(), "loss: the bat comes in");
    g.launch();
    g.give(PrizeKind::BigBall);
    g.give(PrizeKind::Cannon);
    g.give(PrizeKind::Night);
    g.give(PrizeKind::IncBat);
    g.give(PrizeKind::SpeedUp);
    check(g.runUntil([&] { return g.session.phase() == SessionPhase::Lost; }, 5), "loss: the ball is lost");
    check(g.session.effect(PrizeKind::BigBall) == nullptr && g.session.effect(PrizeKind::Cannon) == nullptr &&
              g.session.effect(PrizeKind::Night) != nullptr && g.count(EventType::EffectEnded, PrizeKind::Cannon) == 1 &&
              g.count(EventType::EffectEnded, PrizeKind::BigBall) == 1,
          "loss: the bat's and the ball's effects end with the last ball, the night does not");
    check(near(g.session.ballRadius(), 0.6f), "loss: the ball is the plain size again");
    g.run(3);
    check(g.session.phase() == SessionPhase::Ready && g.session.batScale() == 1 && near(g.session.ballSpeed(), 16.5f),
          "loss: the new bat is plain, and the ball speed of the level stays");
}

void testStar() {
    // 100 bricks destroyed one at a time under the default rule: a star may fall in the last ten, never before, and never two at once.
    int withStar = 0;
    constexpr int kSeeds = 60;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        SessionOptions o = dropping(1, static_cast<std::uint64_t>(seed), TargetRule::Bricks);
        for (PrizeRules::Tier& tier : o.rules.prizes.tiers) {
            tier.chance = 0;  // only the star
        }
        Game g(crowdLevel(100, "Cheap.jpg"), o);
        int kills = 0, firstStarAt = 0, stars = 0;
        for (ActorIndex i = 1; i <= 100; ++i) {
            g.session.hit(i, {});
            g.step();
            ++kills;
            for (const GameEvent& event : g.log) {
                if (event.type == EventType::PrizeDropped && *event.prize == PrizeKind::NextLevel) {
                    ++stars;
                    firstStarAt = firstStarAt == 0 ? kills : firstStarAt;
                }
            }
            g.log.clear();
        }
        withStar += stars > 0;
        check(stars <= 1, "star: never two at once");
        check(stars == 0 || firstStarAt >= 90, "star: not before the last ten, first at kill " + std::to_string(firstStarAt));
    }
    check(withStar >= kSeeds * 8 / 10, "star: it falls in nine levels of ten, " + std::to_string(withStar) + " of " + std::to_string(kSeeds));

    // With the other rule and with no end, no star.
    SessionOptions never = dropping(1, 3, TargetRule::Never);
    for (PrizeRules::Tier& tier : never.rules.prizes.tiers) {
        tier.chance = 0;
    }
    Game g(crowdLevel(100, "Cheap.jpg"), never);
    for (ActorIndex i = 1; i <= 100; ++i) {
        g.session.hit(i, {});
        g.step();
    }
    check(g.count(EventType::PrizeDropped) == 0, "star: a level that never ends has none");
}

void testAutoplayWithPrizes() {
    // The autopilot plays a crowd of bricks with the prizes on for a while: nothing breaks, prizes fall and some are caught.
    SessionOptions o = dropping(30, 4, TargetRule::Bricks);
    o.hearts = 99;
    Game g(crowdLevel(300, "Cheap.jpg"), o);
    float worstSpeed = 0;
    const PlayResult result = autoplay(g.session, 120, [&](const GameSession& s) {
        for (const BallView& ball : s.balls()) {
            if (!ball.held && !ball.stuck) {
                worstSpeed = std::max(worstSpeed, std::fabs(std::hypot(ball.velocity.x, ball.velocity.z) - s.ballSpeed()));
            }
        }
    });
    int dropped = 0, caught = 0, missed = 0;
    for (const GameEvent& event : result.events) {
        dropped += event.type == EventType::PrizeDropped;
        caught += event.type == EventType::PrizeCaught;
        missed += event.type == EventType::PrizeMissed;
    }
    check(dropped > 5 && caught > 0 && missed > 0 && dropped >= caught + missed, "autoplay: " + std::to_string(dropped) + " dropped, " +
                                                                                    std::to_string(caught) + " caught, " + std::to_string(missed) + " missed");
    check(worstSpeed < 0.2f, "autoplay: the ball speed is the session's whatever the prizes did, worst " + num(worstSpeed));
    check(g.session.stats().prizesCaught == caught && g.session.stats().prizesDropped == dropped, "autoplay: the statistics agree");
}

}  // namespace

int main() {
    testTable();
    testRandom();
    testChances();
    testDropRate();
    testDropsAreSeeded();
    testRedPrizesFromLevelTen();
    testPrizeStartsWhereTheBrickDied();
    testFall();
    testCatchGeometry();
    testCatchScore();
    testInstantPrizes();
    testSkull();
    testSkullFirstInAStep();
    testSpeedPrizes();
    testBatSize();
    testBallSize();
    testPowerBall();
    testExplosiveBall();
    testExplosiveBallTwoBricksAtOnce();
    testBlowFollowsTheBallSpeed();
    testNormalBall();
    testDrunkenBall();
    testMultiball();
    testMagnet();
    testYoYo();
    testWall();
    testNight();
    testQuake();
    testWind();
    testSurprise();
    testWeapons();
    testTimersAndRefresh();
    testEffectsEndWithTheBat();
    testStar();
    testAutoplayWithPrizes();
    if (failures == 0) {
        std::puts("all prize tests passed");
    }
    return failures == 0 ? 0 : 1;
}
