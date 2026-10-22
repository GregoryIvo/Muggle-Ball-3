#pragma once

#include "mb/engine/Renderer.h"
#include "mb/engine/Vertex.h"
#include "mb/game/ClothPlan.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/ModelCache.h"
#include "mb/game/PhysicsScene.h"
#include "mb/game/TextureIndex.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mb {

// The mesh of a sail as the particles have it (docs/GAMEPLAY.md section 20): the grid of particles is the surface, one quad a cell, the
// texture over all of it with its top at the highest row. The vertices are in two halves, the surface and then its back with the
// normals turned round (so that both sides are lit, and drawn with culling), each in the order of the particles; the normals are the
// mean of the faces round a particle. `topIsLastRow` says whether the sail's highest row is the grid's last (it is in the levels).
std::vector<Vertex> clothVertices(int width, int height, bool topIsLastRow, std::span<const Vec3> positions);
// The triangles of both halves of clothVertices(): the same for every sail of that grid, whatever its shape.
std::vector<std::uint32_t> clothIndices(int width, int height);
// Whether the last row of a grid is its highest (by the rest positions): the texture's top goes there.
bool topIsLastRow(const ClothPlan& cloth);

// The sails of a level on screen: one mesh each that follows the particles, in the material's texture and lighting. It reads the
// physics scene (where the particles are, which sails are still there) and draws nothing by itself: whoever draws takes its items.
class ClothDraw {
public:
    ClothDraw(ModelCache& cache, const MaterialLibrary& materials, const TextureIndex& textures, const Level& level, const PhysicsScene& scene);
    // The meshes are the sails' own (the cache does not know them): they go back to the renderer with it, or each level with sails would
    // leave its 3 to 5 behind until the renderer ends.
    ~ClothDraw();
    ClothDraw(const ClothDraw&) = delete;
    ClothDraw& operator=(const ClothDraw&) = delete;

    // Brings the meshes up to the particles; a sail that has not moved since the last call is not uploaded again.
    void update(const PhysicsScene& scene);
    // The draws of the sails that are in the level (a destroyed one is gone from the scene).
    void appendDrawn(const PhysicsScene& scene, std::vector<DrawItem>& out) const;

    std::size_t size() const { return sails_.size(); }

private:
    struct Sail {
        MeshHandle mesh = 0;
        bool topIsLast = true;
        DrawItem item;
        std::vector<Vec3> drawn;  // where the particles were when the mesh was last written
    };

    ModelCache& cache_;
    std::vector<Sail> sails_;  // by cloth of the session's scene
};

}  // namespace mb
