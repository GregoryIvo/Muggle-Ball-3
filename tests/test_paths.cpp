// Paths: the curve through a path's points, walked by distance, and the actors bound to it. Made-up paths and levels;
// needs neither a GPU nor game data. The same code against the real levels: mb_path_smoke.

#include "mb/game/LevelLoader.h"
#include "mb/game/PathPlayer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
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

bool near(double a, double b, double tolerance = 1e-3) {
    return std::fabs(a - b) <= tolerance;
}

bool near(Vec3 a, Vec3 b, float tolerance = 1e-3f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

// True when the rows of the 3x3 are orthonormal and right-handed.
bool isRigid(const Mat4& m, float tolerance = 1e-4f) {
    const Vec3 x{m.m[0][0], m.m[0][1], m.m[0][2]}, y{m.m[1][0], m.m[1][1], m.m[1][2]}, z{m.m[2][0], m.m[2][1], m.m[2][2]};
    return near(dot(x, x), 1, tolerance) && near(dot(y, y), 1, tolerance) && near(dot(z, z), 1, tolerance) &&
           near(dot(x, y), 0, tolerance) && near(dot(x, z), 0, tolerance) && near(dot(y, z), 0, tolerance) &&
           dot(cross(x, y), z) > 0;
}

float distance(Vec3 a, Vec3 b) {
    return length(a - b);
}

PathPoint point(Vec3 pos, Vec3 up = {0, 1, 0}, Vec3 tangent = {}) {
    PathPoint p;
    p.pos = pos;
    p.up = up;
    p.tangent = tangent;
    return p;
}

// n points round a circle of radius r about `centre` in the plane y = 0, counter-clockwise seen from above: +x towards +z.
Path circle(int n, float r, Vec3 centre = {5, 0, -3}) {
    Path path;
    path.name = "circle";
    path.closed = true;
    for (int i = 0; i < n; ++i) {
        const float a = 2 * kPi * static_cast<float>(i) / static_cast<float>(n);
        path.points.push_back(point(centre + Vec3{r * std::cos(a), 0, r * std::sin(a)}));
    }
    return path;
}

Path line(Vec3 from, Vec3 to) {
    Path path;
    path.name = "line";
    path.points = {point(from), point(to)};
    return path;
}

// ---- The curve ----------------------------------------------------------------------------------------------------

void testStraightPath() {
    const PathCurve curve(line({0, 1, 0}, {0, 1, 8}));
    check(curve.usable() && !curve.closed() && near(curve.totalLength(), 8), "line: a straight path is as long as it looks");
    const PathFrame start = curve.at(0), middle = curve.at(3), end = curve.at(8);
    check(near(start.position, {0, 1, 0}) && near(middle.position, {0, 1, 3}) && near(end.position, {0, 1, 8}),
          "line: the distance is steady, a point at that many units");
    check(near(start.dir, {0, 0, 1}) && near(middle.dir, {0, 0, 1}) && near(end.dir, {0, 0, 1}) &&
              near(middle.up, {0, 1, 0}),
          "line: it faces along the way it goes, upright");
    check(near(curve.at(-5).position, {0, 1, 0}) && near(curve.at(100).position, {0, 1, 8}),
          "line: an open path stops at its ends");
    check(near(curve.nearest({3, 1, 5}), 5) && near(curve.nearest({0, 1, -4}), 0) && near(curve.nearest({0, 1, 20}), 8),
          "line: the nearest point of a line is the foot of the perpendicular, or an end");

    // The frame as a matrix: the object's axes are its rows, so that a point ahead of it is its position plus the direction.
    const Mat4 m = pathFrameMatrix(middle);
    check(isRigid(m) && near(m.translationPart(), {0, 1, 3}), "frame matrix: rigid, at the position");
    check(near(transformPoint({0, 0, 1}, m), {0, 1, 4}) && near(transformVector({0, 1, 0}, m), {0, 1, 0}),
          "frame matrix: +z is the direction and +y is up");
    const PathFrame diagonal = PathCurve(line({0, 0, 0}, {10, 0, 10})).at(5);
    const Mat4 d = pathFrameMatrix(diagonal);
    check(isRigid(d) && near(transformVector({0, 0, 1}, d), normalize(Vec3{1, 0, 1})) &&
              near(transformVector({1, 0, 0}, d), normalize(Vec3{1, 0, -1})),
          "frame matrix: x is up x direction");
}

void testUnusablePaths() {
    Path one;
    one.points = {point({1, 2, 3})};
    Path none;
    Path stuck;
    stuck.points = {point({1, 2, 3}), point({1, 2, 3})};
    for (const Path* path : {&one, &none, &stuck}) {
        const PathCurve curve(*path);
        check(!curve.usable() && curve.totalLength() == 0 && curve.nearest({0, 0, 0}) == 0 &&
                  near(curve.at(5).position, {0, 0, 0}) && near(curve.at(5).dir, {0, 0, 1}),
              "unusable: fewer than two points, or none apart, give a curve that is nowhere");
    }
}

void testClosedCircle() {
    constexpr float kRadius = 10;
    const Vec3 centre{5, 0, -3};
    const PathCurve curve(circle(8, kRadius, centre));
    check(curve.usable() && curve.closed(), "circle: a closed path");
    const float length = curve.totalLength();
    check(std::fabs(length - 2 * kPi * kRadius) < 0.01f * 2 * kPi * kRadius,
          "circle: eight points on a circle make a curve about as long as it");

    bool onCircle = true, tangent = true, forwards = true, wraps = true, upright = true;
    for (float d = 0; d < length; d += 0.37f) {
        const PathFrame f = curve.at(d);
        const Vec3 radial = f.position - centre;
        onCircle = onCircle && std::fabs(std::sqrt(dot(radial, radial)) - kRadius) < 0.01f * kRadius;
        tangent = tangent && std::fabs(dot(f.dir, normalize(radial))) < 0.05f;
        forwards = forwards && cross(normalize(radial), f.dir).y < -0.99f;  // from +x towards +z is -y by the cross product
        wraps = wraps && near(curve.at(d + length).position, f.position, 1e-3f) &&
                near(curve.at(d - 2 * length).position, f.position, 1e-3f);
        upright = upright && near(f.up, {0, 1, 0}, 1e-5f);
    }
    check(onCircle, "circle: the curve stays within 1% of the circle");
    check(tangent && forwards, "circle: it faces along the circle, the way the points go");
    check(wraps, "circle: a closed path wraps, forwards and backwards");
    check(upright, "circle: a flat path is upright");

    // Steady speed: a tenth of a unit apart in distance is a tenth apart in space, all the way round.
    float worst = 0;
    for (float d = 0; d < length; d += 0.113f) {
        worst = std::max(worst, std::fabs(distance(curve.at(d).position, curve.at(d + 0.1f).position) / 0.1f - 1));
    }
    check(worst < 0.005f, "circle: equal distances are equal steps in space");
}

void testHairpin() {
    // Four points that make a path out and back: the curve turns sharply at the two far points. Its speed in the parameter of
    // the curve varies tenfold (Catmull-Rom's tangent there is small), and the distance must not.
    Path path;
    path.closed = true;
    path.points = {point({-1.92f, 0, -11.7f}), point({-15.32f, 0, -13.64f}), point({0, 0, -14.18f}),
                   point({17.37f, 0, -13.54f})};
    const PathCurve curve(path);
    float worst = 0;
    for (double d = 0; d < curve.totalLength(); d += 0.01) {
        const float moved = distance(curve.at(d).position, curve.at(d + 0.001).position);
        worst = std::max(worst, std::fabs(moved / 0.001f - 1));
    }
    check(worst < 0.01f, "hairpin: a thousandth of a unit of distance is a thousandth in space, even in the turns");
    // It passes through its points.
    for (const PathPoint& p : path.points) {
        const double d = curve.nearest(p.pos);
        check(near(curve.at(d).position, p.pos, 1e-3f), "hairpin: the curve goes through each of its points");
    }
}

void testExplicitTangents() {
    // Hermite: a quarter turn from heading +z to heading +x.
    Path path;
    path.tangents = true;
    path.points = {point({0, 0, 0}, {0, 1, 0}, {0, 0, 10}), point({10, 0, 10}, {0, 1, 0}, {10, 0, 0})};
    const PathCurve curve(path);
    check(near(curve.at(0).dir, {0, 0, 1}, 1e-4f) && near(curve.at(curve.totalLength()).dir, {1, 0, 0}, 1e-4f),
          "tangents: the curve leaves the way the first tangent says and arrives the way the second does");
    const Vec3 middle{3.75f, 0, 6.25f};  // Hermite at one half: the mean of the points plus an eighth of m0 - m1
    check(near(curve.at(curve.nearest(middle)).position, middle, 1e-3f),
          "tangents: and passes through the middle of the Hermite curve");
    // The same points without the flag are Catmull-Rom's: a straight line between two points.
    path.tangents = false;
    const PathCurve straight(path);
    check(near(straight.totalLength(), length(Vec3{10, 0, 10})) &&
              near(straight.at(straight.totalLength() / 2).position, {5, 0, 5}),
          "tangents: without the flag the tangents are ignored");

    // An open path of three points: Catmull-Rom's tangent in the middle, a one-sided one at the ends.
    Path open;
    open.points = {point({0, 0, 0}), point({10, 0, 0}), point({10, 0, 10})};
    const PathCurve bend(open);
    check(!bend.closed() && near(bend.at(0).position, {0, 0, 0}) &&
              near(bend.at(bend.totalLength()).position, {10, 0, 10}) &&
              near(bend.at(bend.nearest({10, 0, 0})).position, {10, 0, 0}, 1e-3f) &&
              near(bend.at(bend.nearest({10, 0, 0})).dir, normalize(Vec3{1, 0, 1}), 0.01f),
          "open path: runs through its points, with the middle one's tangent from its neighbours");
}

void testUp() {
    Path path = line({0, 0, 0}, {0, 0, 8});
    path.points[1].up = {1, 0, 0};  // rolled a quarter turn by the far end
    const PathCurve curve(path);
    check(near(curve.at(0).up, {0, 1, 0}) && near(curve.at(8).up, {1, 0, 0}) &&
              near(curve.at(4).up, normalize(Vec3{1, 1, 0}), 1e-3f),
          "up: the points' ups are blended along the segment");
    // An up that leans along the way is made perpendicular to it; a missing one is the world's.
    Path leaning = line({0, 0, 0}, {0, 0, 8});
    leaning.points[0].up = leaning.points[1].up = normalize(Vec3{0, 1, 1});
    check(near(PathCurve(leaning).at(2).up, {0, 1, 0}, 1e-4f), "up: an up with a part along the path loses it");
    Path missing = line({0, 0, 0}, {0, 0, 8});
    missing.points[0].up = missing.points[1].up = {};
    check(near(PathCurve(missing).at(2).up, {0, 1, 0}), "up: no up is the world's");
    Path vertical = line({0, 0, 0}, {0, 8, 0});
    const PathFrame climb = PathCurve(vertical).at(2);
    check(near(climb.dir, {0, 1, 0}) && near(dot(climb.up, climb.dir), 0) && near(length(climb.up), 1) &&
              isRigid(pathFrameMatrix(climb)),
          "up: a path that goes straight up still has a frame");
}

void testNearest() {
    const PathCurve curve(circle(8, 10));
    const float length = curve.totalLength();
    bool back = true;
    for (float d = 0.2f; d < length; d += 1.7f) {
        const double found = curve.nearest(curve.at(d).position, curve.at(d).dir);
        back = back && std::fabs(found - d) < 2e-3f;
    }
    check(back, "nearest: a point of the curve gives its own distance, and so does the one just off it");
    // From outside, the nearest point is the foot of the radius.
    const double out = curve.nearest({5 + 30.0f, 0, -3});
    check(near(curve.at(out).position, {15, 0, -3}, 0.1f), "nearest: from outside, along the radius");
    // Far from every point it is still the nearest: no sample of the curve is closer.
    const Vec3 far{-8, 2, 7};
    const float found = distance(curve.at(curve.nearest(far)).position, far);
    float best = 1e9f;
    for (float d = 0; d < length; d += 0.01f) {
        best = std::min(best, distance(curve.at(d).position, far));
    }
    check(found <= best + 1e-3f, "nearest: no point of the curve is nearer than the one found");

    // A path that crosses itself (a bow tie): the middle is on both branches, and the heading says which.
    Path bow;
    bow.closed = true;
    bow.points = {point({-4, 0, -4}), point({4, 0, 4}), point({4, 0, -4}), point({-4, 0, 4})};
    const PathCurve tie(bow);
    const Vec3 middle{};
    const double first = tie.nearest(middle, {1, 0, 1}), second = tie.nearest(middle, {-1, 0, 1});
    check(near(tie.at(first).position, middle, 0.05f) && near(tie.at(second).position, middle, 0.05f) &&
              std::fabs(first - second) > 3,
          "nearest: where a path crosses itself the heading chooses the branch");
    check(dot(tie.at(first).dir, normalize(Vec3{1, 0, 1})) > 0.9f &&
              dot(tie.at(second).dir, normalize(Vec3{-1, 0, 1})) > 0.9f,
          "nearest: and that branch runs the way it was asked");
}

// ---- The player ---------------------------------------------------------------------------------------------------

Actor actorWithId(std::uint32_t id) {
    Actor a;
    GameObject object;
    object.id = id;
    a.object = object;
    return a;
}

PathKey key(std::uint32_t object, Vec3 at, Vec3 dir = {0, 0, 1}) {
    PathKey k;
    k.position = at;
    k.dir = dir;
    k.up = {0, 1, 0};
    k.object = object;
    return k;
}

void testPlayer() {
    Level level;
    level.actors = {actorWithId(10), actorWithId(11), actorWithId(12), actorWithId(13), actorWithId(14), actorWithId(15)};
    Path ring = circle(8, 10, {0, 0, 0});
    ring.name = "track/ring";
    Path ramp = line({-10, 0, 0}, {10, 0, 0});
    ramp.name = "track/ramp";
    Path pin;
    pin.name = "track/pin";
    pin.points = {point({1, 1, 1})};
    level.paths = {ring, ramp, pin};

    // Speeds are in units a millisecond: 0.005 is five units a second.
    PathTrack ringTrack;
    ringTrack.speed = 0.005f;
    ringTrack.keys = {key(10, {0, 0, 10}, {-1, 0, 0}),  // a quarter of the way round, going counter-clockwise
                      key(11, {-10, 0, 0}, {0, 0, -1}),  // half way
                      key(99, {10, 0, 0}),               // no such actor
                      key(10, {10, 0, 0})};              // the actor has one already
    PathTrack rampTrack;
    rampTrack.speed = 0.002f;
    rampTrack.keys = {key(12, {-4, 0, 0}, {1, 0, 0})};
    PathTrack backTrack;
    backTrack.speed = -0.001f;
    backTrack.keys = {key(13, {0, 0, 10}, {-1, 0, 0})};
    PathTrack nanTrack;
    nanTrack.speed = std::numeric_limits<float>::quiet_NaN();
    nanTrack.keys = {key(14, {10, 0, 0})};
    PathTrack lostTrack;
    lostTrack.speed = 0.005f;
    lostTrack.keys = {key(15, {1, 1, 1})};
    level.pathBindings = {{"track/ring", {ringTrack, backTrack, nanTrack}}, {"track/ramp", {rampTrack}},
                          {"track/pin", {lostTrack}}, {"track/gone", {lostTrack}}};

    const PathPlayer player(level);
    check(player.movers().size() == 4 && player.unusedKeys() == 5,
          "player: the usable keys move their actors; those that name no actor, an actor that has one, a speed that is no "
          "number, a path of one point or no path at all do not");
    if (player.movers().size() != 4) {
        return;
    }
    const PathMover &a = player.movers()[0], &b = player.movers()[1];
    const PathMover &back = player.movers()[2], &ramp1 = player.movers()[3];
    check(a.actor == 0 && b.actor == 1 && back.actor == 3 && ramp1.actor == 2,
          "player: movers are the actors the keys name");
    check(std::fabs(a.speed - 5) < 1e-4 && std::fabs(ramp1.speed - 2) < 1e-4 && std::fabs(back.speed + 1) < 1e-4,
          "player: a millisecond's speed is a thousand times a second's");

    const PathCurve& ringCurve = player.curve(a);
    const double lap = static_cast<double>(ringCurve.totalLength());
    check(std::fabs(a.start - lap / 4) < 0.05 && std::fabs(b.start - lap / 2) < 0.05,
          "player: an actor starts at the point of its path nearest its key");
    check(near(player.frame(a, 0).position, {0, 0, 10}, 0.05f) && near(player.frame(a, 0).dir, {-1, 0, 0}, 0.02f),
          "player: at time 0 it is at its key, facing the way the key says");

    // It goes on at its speed, along the path.
    const double along = ringCurve.nearest(player.frame(a, 2).position, player.frame(a, 2).dir);
    check(std::fabs(std::fmod(along - a.start + lap, lap) - 10) < 0.02,
          "player: two seconds at five units a second is ten units");
    check(near(player.frame(a, lap / a.speed).position, player.frame(a, 0).position, 1e-3f) &&
              near(player.frame(a, 3 * lap / a.speed + 1).position, player.frame(a, 1).position, 1e-2f),
          "player: round a closed path again and again");
    check(near(player.frame(ramp1, 0).position, {-4, 0, 0}, 1e-3f) &&
              near(player.frame(ramp1, 1).position, {-2, 0, 0}, 1e-3f) &&
              near(player.frame(ramp1, 1000).position, {10, 0, 0}, 1e-3f) && near(player.frame(ramp1, 1000).dir, {1, 0, 0}),
          "player: along an open path to its end, where it stops");
    check(std::fabs(distance(player.frame(back, 2).position, player.frame(back, 0).position) - 2) < 0.02f &&
              player.frame(back, 2).position.x > 1.9f,
          "player: a negative speed goes the other way round, clockwise from the top of the circle");
    check(isRigid(player.pose(a, 1.234)) && near(player.pose(a, 1.234).translationPart(), player.frame(a, 1.234).position),
          "player: the pose is the frame as a matrix");

    // Nothing in a level without bindings.
    check(PathPlayer(Level{}).movers().empty() && PathPlayer().movers().empty(), "player: no bindings, no movers");
}

void testLoader() {
    const Level level = parseLevelJson(R"({
      "gravity": [0, -9.81, 0], "actors": [],
      "paths": [{"name": "track/shark", "param": 1, "mask": {"count": 4, "hex": "06"},
                 "points": [{"pos": [1,2,3], "tng": [0,0,0], "dir": [1,0,0], "up": [0,1,0]}]},
                {"name": "track/track", "param": 0, "mask": {"count": 4, "hex": "05"}, "points": []},
                {"name": "track/empty", "param": 1, "mask": {"count": 0, "hex": ""}, "points": []},
                {"name": "track/old", "points": []}],
      "pathBindings": [{"name": "track/shark", "tracks": [
        {"index": 0, "param": 1000593162, "mask": {"count": 5, "hex": "19"},
         "keys": [{"a": [1, 2, 3], "b": [0, 0, 1], "c": [0, 1, 0], "object": 212}]}]},
        {"name": "track/track", "tracks": []}]
    })");
    check(level.paths.size() == 4 && level.paths[0].closed && !level.paths[0].tangents && !level.paths[1].closed &&
              level.paths[1].tangents && level.paths[2].closed && !level.paths[2].tangents && !level.paths[3].closed &&
              !level.paths[3].tangents,
          "loader: a path is closed by its param and has tangents by bit 0 of its mask; an older file has neither");
    check(level.pathBindings.size() == 2 && level.pathBindings[0].name == "track/shark" &&
              level.pathBindings[0].tracks.size() == 1 && level.pathBindings[1].tracks.empty(),
          "loader: bindings by path name, with their tracks");
    const PathTrack& track = level.pathBindings[0].tracks[0];
    check(track.speed == 0.005f && track.keys.size() == 1 && track.keys[0].object == 212 &&
              near(track.keys[0].position, {1, 2, 3}) && near(track.keys[0].dir, {0, 0, 1}) &&
              near(track.keys[0].up, {0, 1, 0}),
          "loader: a track's param is the bits of its speed, 0.005; a key is the position, direction and up of an object");
    check(parseLevelJson(R"({"gravity": [0,0,0], "actors": []})").pathBindings.empty(),
          "loader: no bindings in an older file");
}

}  // namespace

int main() {
    testStraightPath();
    testUnusablePaths();
    testClosedCircle();
    testHairpin();
    testExplicitTangents();
    testUp();
    testNearest();
    testPlayer();
    testLoader();
    if (failures > 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::puts("path tests passed");
    return 0;
}
