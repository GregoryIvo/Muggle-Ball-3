#include "mb/game/PathPlayer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace mb {

namespace {

constexpr std::size_t kSteps = 32;   // pieces per segment in the table of arc lengths
constexpr float kMinLength = 1e-3f;  // a path shorter than this is a point
constexpr int kNewtonSteps = 6;      // refining a distance into a parameter
constexpr int kSearchSteps = 30;     // narrowing down the nearest point

Vec3 hermitePosition(Vec3 p0, Vec3 m0, Vec3 p1, Vec3 m1, float u) {
    const float u2 = u * u, u3 = u2 * u;
    return p0 * (2 * u3 - 3 * u2 + 1) + m0 * (u3 - 2 * u2 + u) + p1 * (3 * u2 - 2 * u3) + m1 * (u3 - u2);
}

Vec3 hermiteVelocity(Vec3 p0, Vec3 m0, Vec3 p1, Vec3 m1, float u) {
    const float u2 = u * u;
    return p0 * (6 * u2 - 6 * u) + m0 * (3 * u2 - 4 * u + 1) + p1 * (6 * u - 6 * u2) + m1 * (3 * u2 - 2 * u);
}

// The length of a segment's curve between two of its parameters, by three-point Gauss-Legendre quadrature of the speed.
// Counting chords instead would be out by a tenth on the hairpins of the sharks' paths, where the speed along the curve
// changes tenfold within a segment.
float arcLength(Vec3 p0, Vec3 m0, Vec3 p1, Vec3 m1, float u0, float u1) {
    constexpr float kNode = 0.7745966692f;  // sqrt(3 / 5)
    const float half = (u1 - u0) / 2, mid = (u0 + u1) / 2;
    const auto speed = [&](float u) { return length(hermiteVelocity(p0, m0, p1, m1, u)); };
    return half * (5.0f / 9 * (speed(mid - kNode * half) + speed(mid + kNode * half)) + 8.0f / 9 * speed(mid));
}

// `up` made perpendicular to the unit `dir`; the world's up (its z for a vertical path) if the points' own is no use.
Vec3 uprightTo(Vec3 up, Vec3 dir) {
    for (const Vec3 candidate : {up, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
        const Vec3 v = candidate - dir * dot(candidate, dir);
        if (length(v) > 1e-3f) {
            return normalize(v);
        }
    }
    return {0, 1, 0};
}

}  // namespace

Mat4 pathFrameMatrix(const PathFrame& frame) {
    const Vec3 x = normalize(cross(frame.up, frame.dir));
    Mat4 m;
    for (std::size_t j = 0; j < 3; ++j) {
        m.m[0][j] = x[j];
        m.m[1][j] = frame.up[j];
        m.m[2][j] = frame.dir[j];
        m.m[3][j] = frame.position[j];
    }
    return m;
}

PathCurve::PathCurve(const Path& path) : closed_(path.closed) {
    const std::vector<PathPoint>& points = path.points;
    const std::size_t n = points.size();
    if (n < 2) {
        return;
    }
    nodes_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        Node& node = nodes_[i];
        node.position = points[i].pos;
        node.up = points[i].up;
        if (path.tangents) {
            node.tangent = points[i].tangent;
        } else if (closed_) {
            node.tangent = (points[(i + 1) % n].pos - points[(i + n - 1) % n].pos) * 0.5f;
        } else if (i == 0) {
            node.tangent = points[1].pos - points[0].pos;
        } else if (i == n - 1) {
            node.tangent = points[n - 1].pos - points[n - 2].pos;
        } else {
            node.tangent = (points[i + 1].pos - points[i - 1].pos) * 0.5f;
        }
    }

    const std::size_t segments = closed_ ? n : n - 1;
    table_.assign(segments * kSteps + 1, 0.0f);
    float total = 0;
    for (std::size_t s = 0; s < segments; ++s) {
        const Node& a = nodes_[s];
        const Node& b = nodes_[(s + 1) % n];
        for (std::size_t j = 0; j < kSteps; ++j) {
            table_[s * kSteps + j] = total;
            total += arcLength(a.position, a.tangent, b.position, b.tangent, static_cast<float>(j) / kSteps,
                               static_cast<float>(j + 1) / kSteps);
        }
    }
    table_.back() = total;
    length_ = total;
    if (!(total > kMinLength) || !std::isfinite(total)) {
        nodes_.clear();
        table_.clear();
        length_ = 0;
        return;
    }
    segments_ = segments;
}

PathFrame PathCurve::frame(std::size_t segment, float u) const {
    const Node& a = nodes_[segment];
    const Node& b = nodes_[(segment + 1) % nodes_.size()];
    PathFrame f;
    f.position = hermitePosition(a.position, a.tangent, b.position, b.tangent, u);
    Vec3 dir = hermiteVelocity(a.position, a.tangent, b.position, b.tangent, u);
    if (length(dir) < 1e-6f) {  // a stop on the curve (two tangents that cancel): face along the chord
        dir = b.position - a.position;
    }
    f.dir = length(dir) > 0 ? normalize(dir) : Vec3{0, 0, 1};
    f.up = uprightTo(a.up * (1 - u) + b.up * u, f.dir);
    return f;
}

void PathCurve::locate(double distance, std::size_t& segment, float& u) const {
    double d = distance;
    if (closed_) {
        d = std::fmod(d, static_cast<double>(length_));
        if (d < 0) {
            d += length_;
        }
    } else {
        d = std::clamp(d, 0.0, static_cast<double>(length_));
    }
    const auto it = std::upper_bound(table_.begin(), table_.end(), static_cast<float>(d));
    const auto passed = static_cast<std::size_t>(std::max<std::ptrdiff_t>(it - table_.begin(), 1));
    const std::size_t entry = std::min(passed - 1, segments_ * kSteps - 1);
    segment = entry / kSteps;
    const float u0 = static_cast<float>(entry % kSteps) / static_cast<float>(kSteps);
    const float u1 = u0 + 1.0f / static_cast<float>(kSteps);
    const double span = table_[entry + 1] - table_[entry];
    if (!(span > 0)) {
        u = u0;
        return;
    }
    // Newton's method for the parameter that is `wanted` of arc length into the piece, from the straight-line guess.
    const float wanted = static_cast<float>(d - table_[entry]);
    const Node& a = nodes_[segment];
    const Node& b = nodes_[(segment + 1) % nodes_.size()];
    u = u0 + (u1 - u0) * wanted / static_cast<float>(span);
    for (int i = 0; i < kNewtonSteps; ++i) {
        const float speed = length(hermiteVelocity(a.position, a.tangent, b.position, b.tangent, u));
        if (!(speed > 1e-4f)) {
            break;
        }
        const float error = arcLength(a.position, a.tangent, b.position, b.tangent, u0, u) - wanted;
        if (std::fabs(error) < 1e-6f) {
            break;
        }
        u = std::clamp(u - error / speed, u0, u1);
    }
}

PathFrame PathCurve::at(double distance) const {
    if (!usable()) {
        return {};
    }
    std::size_t segment = 0;
    float u = 0;
    locate(distance, segment, u);
    return frame(segment, u);
}

double PathCurve::nearest(Vec3 position, Vec3 heading) const {
    if (!usable()) {
        return 0;
    }
    heading = normalize(heading);
    const bool aligned = length(heading) > 0;
    const auto cost = [&](const PathFrame& f) {
        return length(f.position - position) + (aligned ? 0.5f * (1 - dot(f.dir, heading)) : 0.0f);
    };

    // The best of the table's points, then the best distance between its two neighbours.
    const std::size_t samples = segments_ * kSteps + (closed_ ? 0 : 1);
    std::size_t best = 0;
    float bestCost = std::numeric_limits<float>::max();
    for (std::size_t k = 0; k < samples; ++k) {
        const std::size_t segment = std::min(k / kSteps, segments_ - 1);
        const float c = cost(frame(segment, static_cast<float>(k - segment * kSteps) / static_cast<float>(kSteps)));
        if (c < bestCost) {
            bestCost = c;
            best = k;
        }
    }
    const double here = table_[best];
    double lo = best > 0 ? table_[best - 1] : (closed_ ? table_[samples - 1] - length_ : here);
    double hi = best + 1 < table_.size() ? table_[best + 1] : here;
    const auto gap = [&](double d) { return length(at(d).position - position); };
    for (int i = 0; i < kSearchSteps; ++i) {
        const double a = lo + (hi - lo) / 3, b = hi - (hi - lo) / 3;
        if (gap(a) < gap(b)) {
            hi = b;
        } else {
            lo = a;
        }
    }
    double d = (lo + hi) / 2;
    if (closed_) {
        d = std::fmod(d, static_cast<double>(length_));
        d += d < 0 ? length_ : 0.0;
    }
    return std::clamp(d, 0.0, static_cast<double>(length_));
}

PathPlayer::PathPlayer(const Level& level) {
    curves_.reserve(level.paths.size());
    for (const Path& path : level.paths) {
        curves_.emplace_back(path);
    }
    const std::vector<ActorRef> actors = flattenActors(level);
    const std::unordered_map<std::uint32_t, ActorIndex> byId = actorsById(actors);
    std::set<ActorIndex> bound;
    for (const PathBinding& binding : level.pathBindings) {
        const auto path = std::ranges::find(level.paths, binding.name, &Path::name);
        const std::size_t index = static_cast<std::size_t>(path - level.paths.begin());
        for (const PathTrack& track : binding.tracks) {
            for (const PathKey& key : track.keys) {
                const auto actor = byId.find(key.object);
                if (path == level.paths.end() || !curves_[index].usable() || actor == byId.end() ||
                    !std::isfinite(track.speed) || !bound.insert(actor->second).second) {
                    ++unusedKeys_;
                    continue;
                }
                movers_.push_back({actor->second, index, curves_[index].nearest(key.position, key.dir),
                                   static_cast<double>(track.speed) * kPathUnitsPerSecond});
            }
        }
    }
}

PathFrame PathPlayer::frame(const PathMover& mover, double seconds) const {
    return curves_[mover.path].at(mover.start + mover.speed * seconds);
}

Mat4 PathPlayer::pose(const PathMover& mover, double seconds) const {
    return pathFrameMatrix(frame(mover, seconds));
}

}  // namespace mb
