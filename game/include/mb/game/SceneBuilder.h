#pragma once

#include "mb/engine/Model.h"
#include "mb/engine/Renderer.h"
#include "mb/game/Level.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/TextureIndex.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mb {

// Renderer layers: the sky is drawn first, everything else on top of it.
inline constexpr int kBackgroundLayer = 0;
inline constexpr int kWorldLayer = 1;

// How far the sky sphere is moved down from where its model and the level's pose put it
// (centre at y = -5.1). Fitted to the height of the white horizon band behind the far rim in
// the two reference screenshots, not recovered from the original's code: with it the band
// lands within about 5 pixels of both at 1024x768; without it, about 70 pixels too high.
inline constexpr float kSkyOffsetY = -8.5f;

// The light the original uses when a level has no light object. Direction and
// brightness were tuned against screenshots of the original game, not recovered
// from its binary; the colours come from the game material "lightMaterial"
// (0.753 diffuse, 0.502 ambient), these are the fallbacks if it is missing.
inline constexpr Vec3 kDefaultLightDirection{0.0f, -0.66f, 0.75f};
inline constexpr float kDefaultLightDiffuse = 0.753f;
inline constexpr float kDefaultLightAmbient = 0.502f;

// Everything needed to draw one primitive of one actor, without any GPU handle.
struct SceneDraw {
    std::string meshFile;  // file under Data/Meshes, e.g. "bg/bg_day.glb"; see meshFileName()
    // item.mesh and item.material.texture are left 0 for the caller to fill in. item.world is where the primitive
    // stands with its actor at the pose the level saved.
    DrawItem item;
    // The actor the primitive belongs to (see flattenActors()) and where it sits in the actor's frame: the node's
    // transform in the model. When the actor moves, item.world = local * the actor's pose. (A background, the sky,
    // does not move; its item.world also holds the kSkyOffsetY shift.)
    ActorIndex actor = kNoActor;
    Mat4 local;
    // Texture files for item.material.texture: none, one, or (when animTimeMs > 0)
    // the frames of an animation, each shown for animTimeMs.
    std::vector<std::filesystem::path> textures;
    int animTimeMs = 0;
};

// What did not resolve while describing a scene. Entries are names as the level or
// model wrote them.
struct SceneReport {
    std::set<std::string> missingMeshes;    // level mesh names with no converted model
    std::set<std::string> missingFrames;    // "mesh:frame" where the model has no such frame
    std::set<std::string> missingTextures;  // texture names that match no file
};

struct SceneDescription {
    std::vector<SceneDraw> draws;
    SceneReport report;
};

// Returns the converted model for a file name from meshFileName(), or nullptr.
using ModelLookup = std::function<const Model*(const std::string& meshFile)>;

// "bg/Bg_Day.x" -> "bg/bg_day.glb" (docs/ASSET_FORMATS.md). Empty for a name that is absolute or
// climbs out of the meshes folder with "..": such a mesh is reported missing, never opened.
std::string meshFileName(std::string_view levelMeshName);

// Turns a level into draw descriptions (see docs/ASSET_FORMATS.md for what the
// data means): every actor whose object names a mesh, cloth segments included,
// draws its `frame` subtree (all root frames when no frame is named) at
// model.globalTransform(node) * actor pose (the rigid one, see rigidTransform()). The exception is a cloth object's own
// mesh, which is only a placeholder for the sail the cloth simulation will build.
//
// Which texture a primitive gets, in order:
//   1. If the subtree uses at most one .x texture and the object's game material has
//      textures, the material's: its animation frames when it animates, otherwise the
//      one for a fresh brick (see SceneBuilder.cpp). With no game material, a material
//      name that is itself a texture ("sky.jpg") is used.
//   2. Otherwise the .x texture, unless its name is a game material too ("water.png"),
//      which then supplies the textures, the colours and the blending.
// Colours come from the object's game material when it has one, else from the .x. A board is the
// exception: its material "BoardMaterial" is a placeholder (the same in all 83 levels, every
// colour 1.0, the ball's texture), so a board takes its textures and colours from its .x (unless the
// .x texture names a game material, see 2), where the self-lit white would otherwise hide all shading
// on the walls and fences. A background object is the sky: drawn first, unlit and from inside, hung
// kSkyOffsetY lower than its pose.
SceneDescription describeScene(const Level& level, const ModelLookup& models, const MaterialLibrary& materials,
                               const TextureIndex& textures);

// Which frame of a texture animation is showing `milliseconds` after the scene appeared.
std::size_t animationFrame(std::int64_t milliseconds, int frameTimeMs, std::size_t frameCount);

// A D3DLIGHT8 as stored in a light object's `extra` (0x68 bytes, little-endian).
// Only directional lights (type 3) are understood; anything else gives nothing.
std::optional<DirectionalLight> parseLight(std::span<const std::uint8_t> d3dLight8);

// The level's own light object, or the default light.
DirectionalLight levelLight(const Level& level, const MaterialLibrary& materials);

// Wireframes of the physics shapes (boxes, spheres, capsules and convex hulls) in the actor's frame, shape pose
// applied: one entry per actor that has shapes, cloth segments included. Shapes that take no part in collisions
// (see collides()) are grey. Planes and triangle meshes are not drawn.
struct ActorShapeLines {
    ActorIndex actor = kNoActor;
    std::vector<DebugLine> lines;
};
std::vector<ActorShapeLines> actorShapeLines(const Level& level);

// The same in world space at the poses the level saved.
std::vector<DebugLine> shapeLines(const Level& level);

// A sphere of the given radius at the origin.
std::vector<DebugLine> sphereLines(float radius, Color color);

}  // namespace mb
