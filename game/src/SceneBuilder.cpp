#include "mb/game/SceneBuilder.h"

#include "StringUtil.h"

#include <algorithm>
#include <bit>
#include <cmath>

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

    void actor(const Actor& a) {
        if (!a.object) {
            return;
        }
        // Shadow casters and lights name no mesh, so they draw nothing. A cloth's mesh is always the
        // placeholder "arbuz.x" (a watermelon, in all 22 cloths): the sail itself is a surface built
        // from the cloth's particles, which comes with the cloth simulation, so it is not drawn yet.
        if (!a.object->mesh.empty() && a.object->type != ObjectType::Cloth) {
            object(a, *a.object);
        }
        for (const Actor& segment : a.object->clothSegments) {
            actor(segment);
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

    void object(const Actor& a, const GameObject& obj) {
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
        const Mat4 pose = Mat4::fromPhysX(a.pose.rotation, a.pose.position);
        const Mat4 actorWorld = background ? pose * Mat4::translation({0, kSkyOffsetY, 0}) : pose;
        for (const int node : nodes) {
            const int mesh = model->nodes[node].mesh;
            if (mesh < 0) {
                continue;
            }
            const Mat4 world = model->globalTransform(node) * actorWorld;
            for (const ModelPrimitive& prim : model->meshes[mesh].primitives) {
                SceneDraw draw;
                draw.meshFile = file;
                draw.item.firstIndex = prim.firstIndex;
                draw.item.indexCount = prim.indexCount;
                draw.item.world = world;
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

// Collects wireframe segments in world space.
struct LineWriter {
    std::vector<DebugLine>& lines;
    Mat4 world;  // local shape space -> world
    Color color;

    void segment(Vec3 a, Vec3 b) { lines.push_back({transformPoint(a, world), transformPoint(b, world), color}); }

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
};

void addActorShapes(const Actor& actor, std::vector<DebugLine>& lines) {
    const Mat4 actorWorld = Mat4::fromPhysX(actor.pose.rotation, actor.pose.position);
    for (const Shape& shape : actor.shapes) {
        const Mat4 world = Mat4::fromPhysX(shape.localPose.rotation, shape.localPose.position) * actorWorld;
        switch (shape.type) {
            case ShapeType::Box:
                LineWriter{lines, world, {0.1f, 1.0f, 0.1f, 1}}.box(shape.halfExtents);
                break;
            case ShapeType::Sphere:
                LineWriter{lines, world, {1.0f, 0.9f, 0.1f, 1}}.sphere(shape.radius);
                break;
            case ShapeType::Capsule:
                LineWriter{lines, world, {0.1f, 0.9f, 1.0f, 1}}.capsule(shape.radius, shape.height);
                break;
            case ShapeType::Plane:
            case ShapeType::Convex:
            case ShapeType::Mesh:
                break;
        }
    }
    if (actor.object) {
        for (const Actor& segment : actor.object->clothSegments) {
            addActorShapes(segment, lines);
        }
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
    for (const Actor& actor : level.actors) {
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

std::vector<DebugLine> shapeLines(const Level& level) {
    std::vector<DebugLine> lines;
    for (const Actor& actor : level.actors) {
        addActorShapes(actor, lines);
    }
    return lines;
}

}  // namespace mb
