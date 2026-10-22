#pragma once

#include "mb/engine/Math.h"
#include "mb/engine/Renderer.h"
#include "mb/engine/Vertex.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mb {

// A material as the .x file defined it (Direct3D Material template).
struct ModelMaterial {
    std::string name;
    Color diffuse{1, 1, 1, 1};   // faceColor
    Color specular{0, 0, 0, 1};
    Color emissive{0, 0, 0, 1};
    float power = 0;
    std::string textureFile;            // as written in the .x, e.g. "Pushka.jpg"; empty if none
    std::filesystem::path texturePath;  // resolved file on disk; empty if none or missing
};

// A run of triangles sharing one material. Indices are absolute into Model::vertices.
struct ModelPrimitive {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    int material = -1;  // index into Model::materials, -1 if none
};

struct ModelMesh {
    std::vector<ModelPrimitive> primitives;
};

// One .x Frame.
struct ModelNode {
    std::string name;
    Mat4 local;  // FrameTransformMatrix (row vectors)
    int parent = -1;
    std::vector<int> children;
    int mesh = -1;  // index into Model::meshes, -1 if the frame has no mesh
    // `local` as the file gives it in parts, which an animation that moves only some of them keeps the rest of. A node written
    // with a `matrix` has no parts (the converter writes the frames an animation moves as parts): these stay at their defaults.
    Vec3 translation{};
    Vec4 rotation{0, 0, 0, 1};  // a unit quaternion, x y z w
    Vec3 scale{1, 1, 1};
};

// How an animation moves one node: keys for the parts of its transform it drives, in seconds, linearly interpolated (rotations
// by the short way round). A part with no keys stays as the node has it; outside its keys a part holds the first or the last.
struct ModelTrack {
    int node = -1;
    std::vector<float> translationTimes;
    std::vector<Vec3> translation;
    std::vector<float> rotationTimes;
    std::vector<Vec4> rotation;
    std::vector<float> scaleTimes;
    std::vector<Vec3> scale;
};

// One glTF animation: the converter writes one for each .x AnimationSet that moves a frame ("Anim-1" in most files; bat.x has
// "Common", "Hit", "Right" and "Left").
struct ModelAnimation {
    std::string name;
    float duration = 0;  // the time of the last key of any track
    std::vector<ModelTrack> tracks;
};

// The transform of translation, rotation and scale as the loader makes a node's `local` of them: scale first, then the
// rotation (a unit quaternion x y z w), then the move; row vectors.
Mat4 trsMatrix(Vec3 translation, Vec4 rotation, Vec3 scale);

// CPU-side copy of one converted mesh file (assets/Data/Meshes/*.glb, see
// docs/ASSET_FORMATS.md). Upload with Renderer::createMesh(vertices, indices).
struct Model {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;  // triangle list, clockwise front faces
    std::vector<ModelMesh> meshes;
    std::vector<ModelNode> nodes;
    std::vector<int> roots;  // top-level nodes, in file order
    std::vector<ModelMaterial> materials;
    std::vector<ModelAnimation> animations;

    // Case-insensitive frame lookup; -1 if absent.
    int findNode(std::string_view name) const;
    // node's local transform followed by each ancestor's: local * parent.local * ...
    Mat4 globalTransform(int node) const;

    // Case-insensitive animation lookup; null if the model has no animation of that name (a file converted before the
    // converter wrote animations has none).
    const ModelAnimation* findAnimation(std::string_view name) const;
    // Every node's local transform `time` seconds into the animation: the nodes it moves as it has them there (a time before
    // its first key or after its last holds that key), the others as they are.
    std::vector<Mat4> localPoses(const ModelAnimation& animation, float time) const;
    // The same with each node's ancestors applied, as globalTransform() has it: the poses to draw the nodes' meshes at.
    std::vector<Mat4> globalPoses(const ModelAnimation& animation, float time) const;
};

// Loads a .glb written by tools/x2glb.py, animations included. Throws std::runtime_error on failure.
Model loadModel(const std::filesystem::path& path);

}  // namespace mb
