#include "mb/game/ClothDraw.h"

#include "mb/game/SceneBuilder.h"

#include <algorithm>
#include <array>

namespace mb {

namespace {

bool same(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    return std::ranges::equal(a, b, [](Vec3 p, Vec3 q) { return p.x == q.x && p.y == q.y && p.z == q.z; });
}

std::uint32_t at(int width, int row, int column) {
    return static_cast<std::uint32_t>(row * width + column);
}

}  // namespace

std::vector<Vertex> clothVertices(int width, int height, bool topIsLastRow, std::span<const Vec3> positions) {
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    // The mean of the faces round a particle, each weighted by its size (the cross product's length).
    std::vector<Vec3> normals(count);
    for (int r = 0; r + 1 < height; ++r) {
        for (int c = 0; c + 1 < width; ++c) {
            const std::uint32_t a = at(width, r, c), b = at(width, r, c + 1), d = at(width, r + 1, c + 1), e = at(width, r + 1, c);
            const Vec3 first = cross(positions[b] - positions[a], positions[d] - positions[a]);
            const Vec3 second = cross(positions[d] - positions[a], positions[e] - positions[a]);
            for (const std::uint32_t corner : {a, b, d}) {
                normals[corner] = normals[corner] + first;
            }
            for (const std::uint32_t corner : {a, d, e}) {
                normals[corner] = normals[corner] + second;
            }
        }
    }
    std::vector<Vertex> out;
    out.reserve(2 * count);
    for (const float side : {1.0f, -1.0f}) {
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                const std::uint32_t k = at(width, r, c);
                const float up = static_cast<float>(r) / static_cast<float>(height - 1);
                const Vec3 normal = normalize(normals[k]) * side;
                out.push_back({positions[k], normal, {static_cast<float>(c) / static_cast<float>(width - 1), topIsLastRow ? 1 - up : up}});
            }
        }
    }
    return out;
}

std::vector<std::uint32_t> clothIndices(int width, int height) {
    const auto count = static_cast<std::uint32_t>(width * height);
    std::vector<std::uint32_t> out;
    for (const bool back : {false, true}) {
        const std::uint32_t base = back ? count : 0;
        for (int r = 0; r + 1 < height; ++r) {
            for (int c = 0; c + 1 < width; ++c) {
                const std::uint32_t a = base + at(width, r, c), b = base + at(width, r, c + 1), d = base + at(width, r + 1, c + 1),
                                    e = base + at(width, r + 1, c);
                for (const std::uint32_t corner : back ? std::array{a, d, b, a, e, d} : std::array{a, b, d, a, d, e}) {
                    out.push_back(corner);
                }
            }
        }
    }
    return out;
}

bool topIsLastRow(const ClothPlan& cloth) {
    float first = 0, last = 0;
    for (int c = 0; c < cloth.width; ++c) {
        first += cloth.rest[static_cast<std::size_t>(c)].y;
        last += cloth.rest[static_cast<std::size_t>((cloth.height - 1) * cloth.width + c)].y;
    }
    return last >= first;
}

ClothDraw::ClothDraw(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, const Level& level,
                     const PhysicsScene& scene)
    : cache_(cache) {
    const ClothSet& cloths = scene.cloths();
    const std::vector<ActorRef> actors = flattenActors(level);
    for (std::size_t i = 0; i < cloths.size(); ++i) {
        const ClothPlan& cloth = cloths.plan(i);
        Sail sail;
        sail.topIsLast = topIsLastRow(cloth);
        const std::vector<std::uint32_t> indices = clothIndices(cloth.width, cloth.height);
        sail.mesh = cache.dynamicMesh(clothVertices(cloth.width, cloth.height, sail.topIsLast, cloth.rest), indices);
        sail.item.mesh = sail.mesh;
        sail.item.indexCount = static_cast<std::uint32_t>(indices.size());
        sail.item.layer = kWorldLayer;
        const GameObject* object = actors[cloth.lead].owner;
        const GameMaterial* game = object != nullptr && !object->material.empty() ? materials.find(object->material) : nullptr;
        MaterialParams& m = sail.item.material;
        if (game != nullptr) {
            m.diffuse = game->diffuse;
            m.ambient = game->ambient;
            m.emissive = game->emissive;
            game->applyBlend(m);
            if (!game->textures.empty()) {
                if (const std::filesystem::path file = textures.find(game->textures.front()); !file.empty()) {
                    m.texture = cache.texture(file);
                }
            }
        }
        sail.drawn = cloth.rest;
        sails_.push_back(std::move(sail));
    }
}

ClothDraw::~ClothDraw() {
    for (const Sail& sail : sails_) {
        cache_.destroyMesh(sail.mesh);
    }
}

void ClothDraw::update(const PhysicsScene& scene) {
    const ClothSet& cloths = scene.cloths();
    for (std::size_t i = 0; i < sails_.size(); ++i) {
        if (!cloths.alive(i)) {
            continue;
        }
        const std::vector<Vec3> now = cloths.positions(i);
        Sail& sail = sails_[i];
        if (same(now, sail.drawn)) {
            continue;
        }
        const ClothPlan& cloth = cloths.plan(i);
        cache_.updateMesh(sail.mesh, clothVertices(cloth.width, cloth.height, sail.topIsLast, now));
        sail.drawn = now;
    }
}

void ClothDraw::appendDrawn(const PhysicsScene& scene, std::vector<DrawItem>& out) const {
    const ClothSet& cloths = scene.cloths();
    for (std::size_t i = 0; i < sails_.size(); ++i) {
        if (cloths.alive(i)) {
            out.push_back(sails_[i].item);
        }
    }
}

}  // namespace mb
