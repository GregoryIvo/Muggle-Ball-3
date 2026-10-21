#pragma once

#include "mb/engine/Renderer.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mb {

// One <Material> from Data/Materials/materials.xml, converted to
// Data/Materials/materials.json by tools/materials.py (schema: docs/ASSET_FORMATS.md).
struct GameMaterial {
    std::string name;   // original spelling, e.g. "shark.jpg", "BoardMaterial"
    std::string group;  // nested <Materials> names, e.g. "Levels/Sea"
    std::vector<std::string> textures;  // texture names without extension; several when animated
    bool animated = false;
    int animTimeMs = 0;  // time per texture frame when animated
    float density = 0;
    std::string splinter;  // mesh spawned when the brick breaks, e.g. "splinter-blue1.x"

    // Direct3D material colours (converted from the XML's A R G B order).
    Color diffuse{1, 1, 1, 1};
    Color ambient{1, 1, 1, 1};
    Color emissive{0, 0, 0, 0};
    Color specular{0, 0, 0, 0};
    float power = 0;

    // PhysX material.
    float staticFriction = 0.5f;
    float dynamicFriction = 0.5f;
    float restitution = 0.5f;
    int frictionCombineMode = 0;
    int restitutionCombineMode = 0;

    // Brick behaviour (<Extra>).
    int brickType = 0;
    int brickEnergy = 1;
    int brickScore = 0;
    int exploseType = 0;
    int exploseDelay = 0;
    float explosePower = 0;

    std::string dropSound, periodSound, hitSound, deadSound, loopSound;

    // Direct3D 8 render states as (D3DRENDERSTATETYPE, value) pairs.
    std::vector<std::pair<int, int>> renderStates;

    // From renderStates: D3DRS_ALPHABLENDENABLE (27) = 1 turns blending on;
    // D3DRS_DESTBLEND (20) = D3DBLEND_ONE (2) makes it additive, anything else is AlphaBlend.
    BlendMode blendMode() const;
};

class MaterialLibrary {
public:
    static MaterialLibrary load(const std::filesystem::path& materialsJson);  // throws on error
    static MaterialLibrary parse(std::string_view json);                      // throws on error

    // Case-insensitive; when a name is defined twice the first definition wins.
    const GameMaterial* find(std::string_view name) const;

    std::size_t size() const { return materials_.size(); }
    const std::vector<GameMaterial>& all() const { return materials_; }

private:
    std::vector<GameMaterial> materials_;
    std::unordered_map<std::string, std::size_t> byName_;  // lowercase name -> index
};

}  // namespace mb
