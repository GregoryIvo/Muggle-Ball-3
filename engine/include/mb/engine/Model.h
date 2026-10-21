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
};

// CPU-side copy of one converted mesh file (assets/Data/Meshes/*.glb, see
// docs/ASSET_FORMATS.md). Upload with Renderer::createMesh(vertices, indices).
struct Model {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;  // triangle list, clockwise front faces
    std::vector<ModelMesh> meshes;
    std::vector<ModelNode> nodes;
    std::vector<int> roots;  // top-level nodes, in file order
    std::vector<ModelMaterial> materials;

    // Case-insensitive frame lookup; -1 if absent.
    int findNode(std::string_view name) const;
    // node's local transform followed by each ancestor's: local * parent.local * ...
    Mat4 globalTransform(int node) const;
};

// Loads a .glb written by tools/x2glb.py. Throws std::runtime_error on failure.
Model loadModel(const std::filesystem::path& path);

}  // namespace mb
