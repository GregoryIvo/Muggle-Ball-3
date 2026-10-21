#include "mb/game/SceneBuilder.h"

#include "StringUtil.h"

#include "mb/game/PhysicsScene.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace mb {

namespace {

namespace fs = std::filesystem;

void collectSubtree(const Model& model, int node, std::vector<int>& out) {
    out.push_back(node);
    for (const int child : model.nodes[node].children) {
        collectSubtree(model, child, out);
    }
}

// How many different textures the .x files give the nodes' primitives. A texture that
// the converter could not find still counts: it is a different texture.
std::size_t distinctModelTextures(const Model& model, const std::vector<int>& nodes) {
    std::set<std::string> names;
    for (const int node : nodes) {
        const int mesh = model.nodes[node].mesh;
        if (mesh < 0) {
            continue;
        }
        for (const ModelPrimitive& prim : model.meshes[mesh].primitives) {
            if (prim.material >= 0 && !model.materials[prim.material].textureFile.empty()) {
                names.insert(lowerAscii(model.materials[prim.material].textureFile));
            }
        }
    }
    return names.size();
}

// The textures a game material shows.
struct TexturePlan {
    std::vector<fs::path> files;  // empty: nothing found
    int animTimeMs = 0;           // > 0: files are animation frames
};

TexturePlan materialTextures(const GameMaterial& game, const TextureIndex& index, std::set<std::string>& missing) {
    TexturePlan plan;
    const auto resolve = [&](const std::string& name) {
        if (const fs::path file = index.find(name); !file.empty()) {
            plan.files.push_back(file);
        } else {
            missing.insert(name);
        }
    };

    const std::vector<std::string>& names = game.textures;
    if (names.empty()) {
        return plan;
    }
    if (game.animated) {
        for (const std::string& name : names) {
            resolve(name);
        }
        if (plan.files.size() > 1) {
            plan.animTimeMs = game.animTimeMs;
        }
    } else {
        // Several textures on a material that does not animate are damage states
        // (Box-1D..Box-1A, Skala--3..Skala--0 go from a smashed crate to an intact one),
        // listed from most damaged to intact, and a brick's energy equals their count.
        // A fresh brick therefore shows the one at index energy - 1. This is inferred
        // from the textures and the energies, not from the original's code.
        const int last = static_cast<int>(names.size()) - 1;
        resolve(names[std::clamp(game.brickEnergy - 1, 0, last)]);
    }
    return plan;
}

// The texture the level's material gives an object, replacing the textures written in the
// .x. Only asked for objects whose subtree uses at most one .x texture: the .x then holds a
// placeholder (three of its names match no file at all) and the material decides what the
// object really looks like.
TexturePlan planTextures(const GameObject& object, const GameMaterial* game, const TextureIndex& index,
                         std::set<std::string>& missing) {
    if (game) {
        return materialTextures(*game, index, missing);
    }
    TexturePlan plan;
    if (!object.material.empty()) {
        // No game material: the level names a texture ("sky.jpg", "night.jpg") directly.
        if (const fs::path file = index.find(object.material); !file.empty()) {
            plan.files.push_back(file);
        }
    }
    return plan;
}

struct Builder {
    const ModelLookup& models;
    const MaterialLibrary& materials;
    const TextureIndex& textures;
    SceneDescription& out;

    void actor(const ActorRef& ref) {
        const Actor& a = *ref.actor;
        if (!a.object) {
            return;
        }
        // Shadow casters and lights name no mesh, so they draw nothing. A cloth's mesh is always the
        // placeholder "arbuz.x" (a watermelon, in all 22 cloths): the sail itself is a surface built
        // from the cloth's particles, which comes with the cloth simulation, so it is not drawn yet.
        if (!a.object->mesh.empty() && a.object->type != ObjectType::Cloth) {
            object(ref, *a.object);
        }
    }

    // A primitive keeps the texture its .x names, unless that name is also a game material:
    // "water.png" is the sea board's animated, blended water, and the material then decides
    // its textures, colours and blending.
    void ownTexture(const ModelMaterial& own, SceneDraw& draw) {
        if (own.textureFile.empty()) {
            return;
        }
        if (const GameMaterial* named = materials.find(own.textureFile)) {
            TexturePlan plan = materialTextures(*named, textures, out.report.missingTextures);
            if (!plan.files.empty()) {
                draw.textures = std::move(plan.files);
                draw.animTimeMs = plan.animTimeMs;
                MaterialParams& m = draw.item.material;
                m.diffuse = named->diffuse;
                m.ambient = named->ambient;
                m.emissive = named->emissive;
                named->applyBlend(m);
                return;
            }
        }
        if (!own.texturePath.empty()) {
            draw.textures.push_back(own.texturePath);
        } else {
            out.report.missingTextures.insert(own.textureFile);
        }
    }

    void object(const ActorRef& ref, const GameObject& obj) {
        const Actor& a = *ref.actor;
        const std::string file = meshFileName(obj.mesh);
        const Model* model = file.empty() ? nullptr : models(file);
        if (!model) {
            out.report.missingMeshes.insert(obj.mesh);
            return;
        }

        std::vector<int> nodes;
        if (obj.frame.empty()) {
            for (const int root : model->roots) {
                collectSubtree(*model, root, nodes);
            }
        } else if (const int node = model->findNode(obj.frame); node >= 0) {
            collectSubtree(*model, node, nodes);
        } else {
            out.report.missingFrames.insert(obj.mesh + ":" + obj.frame);
            return;
        }

        // A board's "BoardMaterial" is a placeholder, see describeScene() in the header.
        const bool board = obj.type == ObjectType::Board;
        const GameMaterial* game = obj.material.empty() || board ? nullptr : materials.find(obj.material);
        TexturePlan plan;
        if (distinctModelTextures(*model, nodes) <= 1) {
            plan = planTextures(obj, game, textures, out.report.missingTextures);
        }

        const bool background = obj.type == ObjectType::Background;
        const Mat4 pose = rigidTransform(a.pose);
        const Mat4 actorWorld = background ? pose * Mat4::translation({0, kSkyOffsetY, 0}) : pose;
        for (const int node : nodes) {
            const int mesh = model->nodes[node].mesh;
            if (mesh < 0) {
                continue;
            }
            const Mat4 local = model->globalTransform(node);
            for (const ModelPrimitive& prim : model->meshes[mesh].primitives) {
                SceneDraw draw;
                draw.meshFile = file;
                draw.item.firstIndex = prim.firstIndex;
                draw.item.indexCount = prim.indexCount;
                draw.item.world = local * actorWorld;
                draw.actor = ref.index;
                draw.local = local;
                draw.item.layer = background ? kBackgroundLayer : kWorldLayer;

                const ModelMaterial* own = prim.material >= 0 ? &model->materials[prim.material] : nullptr;
                MaterialParams& m = draw.item.material;
                if (game) {
                    m.diffuse = game->diffuse;
                    m.ambient = game->ambient;
                    m.emissive = game->emissive;
                    game->applyBlend(m);
                } else if (own) {
                    m.diffuse = own->diffuse;
                    m.ambient = own->diffuse;
                    m.emissive = own->emissive;
                }
                if (background) {
                    // The sky is a textured sphere around the whole scene: it has no lighting and
                    // is seen from inside, so it neither hides faces nor blocks anything.
                    m.lit = false;
                    m.depthWrite = false;
                    m.cull = CullMode::None;
                }

                if (!plan.files.empty()) {
                    draw.textures = plan.files;
                    draw.animTimeMs = plan.animTimeMs;
                } else if (own) {
                    ownTexture(*own, draw);
                }
                out.draws.push_back(std::move(draw));
            }
        }
    }
};

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) | static_cast<std::uint32_t>(bytes[offset + 1]) << 8 |
           static_cast<std::uint32_t>(bytes[offset + 2]) << 16 | static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
}

float readF32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<float>(readU32(bytes, offset));
}

Color readColor(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return {readF32(bytes, offset), readF32(bytes, offset + 4), readF32(bytes, offset + 8),
            readF32(bytes, offset + 12)};
}

// The corners joined by the edges of the convex hull of the points, as pairs of indices. Every plane through
// three points that has all the others on one side is a face of the hull; the points on it, reduced to their 2D
// hull, are the face's corners (a box face has four, a hull may have faces with more). This brute force is
// plenty for the 30 points at most that a physics hull of the game has.
std::vector<std::pair<std::size_t, std::size_t>> hullEdges(const std::vector<Vec3>& points) {
    const std::size_t n = std::min<std::size_t>(points.size(), 64);
    float extent = 0;
    for (std::size_t i = 0; i < n; ++i) {
        extent = std::max(extent, length(points[i] - points[0]));
    }
    const float epsilon = 1e-4f * std::max(extent, 1e-3f);
    std::set<std::uint64_t> faces;
    std::set<std::pair<std::size_t, std::size_t>> edges;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            for (std::size_t k = j + 1; k < n; ++k) {
                Vec3 normal = cross(points[j] - points[i], points[k] - points[i]);
                const float len = length(normal);
                if (len < epsilon * epsilon) {
                    continue;  // the three points are in a line
                }
                normal = normal / len;
                const float offset = dot(normal, points[i]);
                bool above = false, below = false;
                std::uint64_t onPlane = 0;
                for (std::size_t m = 0; m < n; ++m) {
                    const float distance = dot(normal, points[m]) - offset;
                    above = above || distance > epsilon;
                    below = below || distance < -epsilon;
                    onPlane |= std::uint64_t{std::fabs(distance) <= epsilon} << m;
                }
                if ((above && below) || !faces.insert(onPlane).second) {
                    continue;
                }

                // The convex polygon of the points on the plane (Andrew's monotone chain, in the plane's 2D).
                const Vec3 u = normalize(points[j] - points[i]), v = cross(normal, u);
                struct Corner {
                    float x, y;
                    std::size_t index;
                };
                std::vector<Corner> corners;
                for (std::size_t m = 0; m < n; ++m) {
                    if (onPlane >> m & 1) {
                        corners.push_back({dot(points[m] - points[i], u), dot(points[m] - points[i], v), m});
                    }
                }
                std::ranges::sort(corners, [](const Corner& a, const Corner& b) {
                    return a.x != b.x ? a.x < b.x : a.y < b.y;
                });
                const auto turn = [](const Corner& o, const Corner& a, const Corner& b) {
                    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
                };
                std::vector<Corner> polygon;
                for (int pass = 0; pass < 2; ++pass) {
                    const std::size_t start = polygon.size();
                    for (const Corner& c : corners) {
                        while (polygon.size() >= start + 2 && turn(polygon[polygon.size() - 2], polygon.back(), c) <= 0) {
                            polygon.pop_back();
                        }
                        polygon.push_back(c);
                    }
                    polygon.pop_back();
                    std::ranges::reverse(corners);
                }
                for (std::size_t c = 0; c < polygon.size(); ++c) {
                    const std::size_t a = polygon[c].index, b = polygon[(c + 1) % polygon.size()].index;
                    edges.insert({std::min(a, b), std::max(a, b)});
                }
            }
        }
    }
    return {edges.begin(), edges.end()};
}

// Collects wireframe segments; `local` places the shape in whatever space the lines are wanted in.
struct LineWriter {
    std::vector<DebugLine>& lines;
    Mat4 local;
    Color color;

    void segment(Vec3 a, Vec3 b) { lines.push_back({transformPoint(a, local), transformPoint(b, local), color}); }

    // The arc centre + radius * (cos t * u + sin t * v) for t in [from, to].
    void arc(Vec3 centre, Vec3 u, Vec3 v, float radius, float from, float to, int steps) {
        const auto point = [&](int i) {
            const float t = from + (to - from) * static_cast<float>(i) / static_cast<float>(steps);
            return centre + (u * std::cos(t) + v * std::sin(t)) * radius;
        };
        for (int i = 0; i < steps; ++i) {
            segment(point(i), point(i + 1));
        }
    }

    void box(Vec3 h) {
        const auto corner = [&](int i) {
            return Vec3{(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z};
        };
        for (int i = 0; i < 8; ++i) {
            for (const int bit : {1, 2, 4}) {
                if (!(i & bit)) {
                    segment(corner(i), corner(i | bit));
                }
            }
        }
    }

    void sphere(float r) {
        constexpr int kSteps = 24;
        arc({}, {1, 0, 0}, {0, 1, 0}, r, 0, 2 * kPi, kSteps);
        arc({}, {0, 1, 0}, {0, 0, 1}, r, 0, 2 * kPi, kSteps);
        arc({}, {0, 0, 1}, {1, 0, 0}, r, 0, 2 * kPi, kSteps);
    }

    // PhysX 2 capsule: axis +Y, `height` between the centres of the two caps.
    void capsule(float r, float height) {
        constexpr int kSteps = 24;
        const float h = height * 0.5f;
        for (const float y : {-h, h}) {
            arc({0, y, 0}, {1, 0, 0}, {0, 0, 1}, r, 0, 2 * kPi, kSteps);
        }
        for (const Vec3 side : {Vec3{r, 0, 0}, Vec3{-r, 0, 0}, Vec3{0, 0, r}, Vec3{0, 0, -r}}) {
            segment({side.x, -h, side.z}, {side.x, h, side.z});
        }
        for (const Vec3 across : {Vec3{1, 0, 0}, Vec3{0, 0, 1}}) {
            arc({0, h, 0}, across, {0, 1, 0}, r, 0, kPi, kSteps / 2);
            arc({0, -h, 0}, across, {0, -1, 0}, r, 0, kPi, kSteps / 2);
        }
    }

    void hull(const std::vector<Vec3>& points) {
        for (const auto& [a, b] : hullEdges(points)) {
            segment(points[a], points[b]);
        }
    }
};

void addShapeLines(const Shape& shape, std::vector<DebugLine>& lines) {
    constexpr Color kGrey{0.55f, 0.55f, 0.55f, 1};  // in the level but not in the collisions
    const Mat4 local = rigidTransform(shape.localPose);
    const auto colour = [&](Color solid) { return collides(shape) ? solid : kGrey; };
    switch (shape.type) {
        case ShapeType::Box:
            LineWriter{lines, local, colour({0.1f, 1.0f, 0.1f, 1})}.box(shape.halfExtents);
            break;
        case ShapeType::Sphere:
            LineWriter{lines, local, colour({1.0f, 0.9f, 0.1f, 1})}.sphere(shape.radius);
            break;
        case ShapeType::Capsule:
            LineWriter{lines, local, colour({0.1f, 0.9f, 1.0f, 1})}.capsule(shape.radius, shape.height);
            break;
        case ShapeType::Convex:
            LineWriter{lines, local, colour({1.0f, 0.55f, 0.1f, 1})}.hull(shape.hullPoints);
            break;
        case ShapeType::Plane:
        case ShapeType::Mesh:
            break;
    }
}

}  // namespace

std::string meshFileName(std::string_view levelMeshName) {
    std::string name = lowerAscii(levelMeshName);
    std::ranges::replace(name, '\\', '/');
    const fs::path path = fs::path(name).replace_extension(".glb");
    const bool escapes =
        path.has_root_path() || std::ranges::any_of(path, [](const fs::path& part) { return part == ".."; });
    return escapes ? std::string() : path.generic_string();
}

std::size_t animationFrame(std::int64_t milliseconds, int frameTimeMs, std::size_t frameCount) {
    if (frameTimeMs <= 0 || frameCount == 0) {
        return 0;
    }
    return static_cast<std::size_t>(milliseconds / frameTimeMs) % frameCount;
}

SceneDescription describeScene(const Level& level, const ModelLookup& models, const MaterialLibrary& materials,
                               const TextureIndex& textures) {
    SceneDescription scene;
    Builder builder{models, materials, textures, scene};
    for (const ActorRef& actor : flattenActors(level)) {
        builder.actor(actor);
    }
    return scene;
}

std::optional<DirectionalLight> parseLight(std::span<const std::uint8_t> d3dLight8) {
    constexpr std::size_t kSize = 0x68;
    constexpr std::uint32_t kDirectional = 3;  // D3DLIGHT_DIRECTIONAL
    if (d3dLight8.size() < kSize || readU32(d3dLight8, 0) != kDirectional) {
        return std::nullopt;
    }
    DirectionalLight light;
    light.diffuse = readColor(d3dLight8, 4);
    light.ambient = readColor(d3dLight8, 36);  // Specular sits between, at 20
    light.direction = {readF32(d3dLight8, 64), readF32(d3dLight8, 68), readF32(d3dLight8, 72)};
    if (length(light.direction) < 1e-6f) {
        return std::nullopt;
    }
    return light;
}

DirectionalLight levelLight(const Level& level, const MaterialLibrary& materials) {
    for (const Actor& actor : level.actors) {
        if (actor.object && actor.object->type == ObjectType::Light) {
            if (const std::optional<DirectionalLight> light = parseLight(actor.object->extra)) {
                return *light;
            }
        }
    }
    DirectionalLight light;
    light.direction = kDefaultLightDirection;
    light.diffuse = {kDefaultLightDiffuse, kDefaultLightDiffuse, kDefaultLightDiffuse, 1};
    light.ambient = {kDefaultLightAmbient, kDefaultLightAmbient, kDefaultLightAmbient, 1};
    if (const GameMaterial* m = materials.find("lightMaterial")) {
        light.diffuse = m->diffuse;
        light.ambient = m->ambient;
    }
    return light;
}

std::vector<ActorShapeLines> actorShapeLines(const Level& level) {
    std::vector<ActorShapeLines> out;
    for (const ActorRef& ref : flattenActors(level)) {
        ActorShapeLines entry;
        entry.actor = ref.index;
        for (const Shape& shape : ref.actor->shapes) {
            addShapeLines(shape, entry.lines);
        }
        if (!entry.lines.empty()) {
            out.push_back(std::move(entry));
        }
    }
    return out;
}

std::vector<DebugLine> shapeLines(const Level& level) {
    const std::vector<ActorRef> actors = flattenActors(level);
    std::vector<DebugLine> lines;
    for (const ActorShapeLines& entry : actorShapeLines(level)) {
        const Mat4 world = rigidTransform(actors[entry.actor].actor->pose);
        for (const DebugLine& line : entry.lines) {
            lines.push_back({transformPoint(line.a, world), transformPoint(line.b, world), line.color});
        }
    }
    return lines;
}

std::vector<DebugLine> sphereLines(float radius, Color color) {
    std::vector<DebugLine> lines;
    LineWriter{lines, Mat4::identity(), color}.sphere(radius);
    return lines;
}

}  // namespace mb
