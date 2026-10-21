// Tests for the asset loaders: Model (.glb), TextureIndex and MaterialLibrary.
// Everything is generated into a temp directory, so no game data is needed.

#include "mb/engine/Image.h"
#include "mb/engine/Model.h"
#include "mb/game/MaterialLibrary.h"
#include "mb/game/TextureIndex.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

bool near(mb::Vec3 a, mb::Vec3 b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }

// True if fn throws std::runtime_error whose message contains `needle`.
bool throwsWith(const std::function<void()>& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::runtime_error& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    } catch (...) {
    }
    return false;
}

void writeFile(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << bytes;
}

// ---------------------------------------------------------------------------
// Model

void put32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

void putFloats(std::string& out, std::initializer_list<float> values) {
    for (const float f : values) {
        std::uint32_t bits;
        static_assert(sizeof(bits) == sizeof(f));
        std::memcpy(&bits, &f, sizeof(bits));
        put32(out, bits);
    }
}

void put16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>(v >> 8));
}

// BIN chunk layout (byte offsets are repeated in the JSON below):
//    0 quad positions   48    48 quad normals   48    96 quad uvs        32
//  128 quad indices u32 24   152 tri positions  36   188 tri indices u16  6 (+2 padding)
std::string synthBin() {
    std::string bin;
    putFloats(bin, {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0});
    putFloats(bin, {0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1});
    putFloats(bin, {0, 1, 1, 1, 1, 0, 0, 0});
    for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
        put32(bin, i);
    }
    putFloats(bin, {0, 0, 1, 2, 0, 1, 0, 2, 1});
    for (const std::uint16_t i : {0, 1, 2}) {
        put16(bin, i);
    }
    put16(bin, 0);
    return bin;
}

// Two root nodes (Cannon, and Base with child Arm) plus an unnamed root; the scene lists
// them as 1, 0, 3 so that the test can tell scene order from node order.
//   Base : matrix, D3D rotationZ(90 degrees) + translation (5, 6, 7)
//   Arm  : translation (1, 0, 0) and scale 2, as TRS
//   node 3: quaternion for a 90 degree rotation about z
// Meshes: 0 = quad (material 0) + indexed triangle (material 1); 1 = non-indexed triangle, no material.
// Image 0 exists on disk, image 1 does not; material 2 names a texture the converter could not find.
json synthDocument() {
    return json::parse(R"({
  "asset": {"version": "2.0"},
  "extras": {"source": "SkinMeshes/synth.X", "coordinateSystem": "d3d-left-handed"},
  "scene": 0,
  "scenes": [{"nodes": [1, 0, 3]}],
  "nodes": [
    {"name": "Base", "children": [2], "matrix": [0,1,0,0, -1,0,0,0, 0,0,1,0, 5,6,7,1]},
    {"name": "Cannon", "mesh": 0},
    {"name": "Arm", "translation": [1,0,0], "scale": [2,2,2]},
    {"mesh": 1, "rotation": [0, 0, 0.70710678, 0.70710678]}
  ],
  "meshes": [
    {"primitives": [
      {"mode": 4, "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0},
      {"mode": 4, "attributes": {"POSITION": 4}, "indices": 5, "material": 1}]},
    {"primitives": [{"mode": 4, "attributes": {"POSITION": 4}}]}
  ],
  "materials": [
    {"name": "Wood",
     "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.25, 0.75, 1], "baseColorTexture": {"index": 0},
                              "metallicFactor": 0, "roughnessFactor": 1},
     "extras": {"power": 12.5, "specular": [0.1, 0.2, 0.3], "emissive": [0.4, 0.5, 0.6], "textureFile": "My Tex.png"}},
    {"name": "Ghost",
     "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 0.5], "baseColorTexture": {"index": 1}},
     "extras": {"textureFile": "Missing.png"}},
    {"name": "Plain",
     "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]},
     "extras": {"textureFile": "Gone.jpg", "textureMissing": true}}
  ],
  "textures": [{"source": 0}, {"source": 1}],
  "images": [{"uri": "../tex/My%20Tex.png"}, {"uri": "../tex/Missing.png"}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [0,0,0], "max": [1,1,0]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5125, "count": 6, "type": "SCALAR"},
    {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,1], "max": [2,2,1]},
    {"bufferView": 5, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48},
    {"buffer": 0, "byteOffset": 96, "byteLength": 32},
    {"buffer": 0, "byteOffset": 128, "byteLength": 24},
    {"buffer": 0, "byteOffset": 152, "byteLength": 36},
    {"buffer": 0, "byteOffset": 188, "byteLength": 6}
  ],
  "buffers": [{"byteLength": 196}]
})");
}

void writeGlb(const fs::path& path, const json& document) {
    std::string jsonChunk = document.dump();
    while (jsonChunk.size() % 4 != 0) {
        jsonChunk.push_back(' ');
    }
    const std::string bin = synthBin();
    std::string glb;
    put32(glb, 0x46546C67);  // "glTF"
    put32(glb, 2);
    put32(glb, static_cast<std::uint32_t>(12 + 8 + jsonChunk.size() + 8 + bin.size()));
    put32(glb, static_cast<std::uint32_t>(jsonChunk.size()));
    put32(glb, 0x4E4F534A);  // "JSON"
    glb += jsonChunk;
    put32(glb, static_cast<std::uint32_t>(bin.size()));
    put32(glb, 0x004E4942);  // "BIN\0"
    glb += bin;
    writeFile(path, glb);
}

void testModel(const fs::path& tmp) {
    const fs::path glb = tmp / "meshes" / "synth.glb";
    writeGlb(glb, synthDocument());
    mb::Image pixel;
    pixel.width = pixel.height = 1;
    pixel.rgba = {255, 0, 0, 255};
    fs::create_directories(tmp / "tex");
    mb::writePng(tmp / "tex" / "My Tex.png", pixel);

    const mb::Model model = mb::loadModel(glb);

    // Nodes keep glTF order, hierarchy and the scene's root order.
    check(model.nodes.size() == 4, "model: node count");
    check(model.nodes[0].name == "Base" && model.nodes[1].name == "Cannon" && model.nodes[2].name == "Arm" &&
              model.nodes[3].name.empty(),
          "model: node names");
    check(model.nodes[2].parent == 0 && model.nodes[0].parent == -1, "model: parent links");
    check(model.nodes[0].children == std::vector<int>{2}, "model: children");
    check(model.nodes[1].mesh == 0 && model.nodes[3].mesh == 1 && model.nodes[0].mesh == -1, "model: node meshes");
    check(model.roots == std::vector<int>{1, 0, 3}, "model: roots follow the scene order");

    // Local transforms are the row-major FrameTransformMatrix, not its transpose.
    const mb::Mat4& base = model.nodes[0].local;
    check(near(base.m[3][0], 5) && near(base.m[3][1], 6) && near(base.m[3][2], 7), "model: matrix translation row");
    check(near(base.m[0][1], 1) && near(base.m[1][0], -1) && near(base.m[0][0], 0), "model: matrix not transposed");
    const mb::Mat4& arm = model.nodes[2].local;
    check(near(arm.m[0][0], 2) && near(arm.m[1][1], 2) && near(arm.m[2][2], 2), "model: TRS scale");
    check(near(arm.m[3][0], 1) && near(arm.m[3][1], 0) && near(arm.m[3][2], 0), "model: TRS translation row");
    const mb::Mat4 rotZ = mb::Mat4::rotationZ(mb::kPi / 2);
    bool sameRotation = true;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            sameRotation = sameRotation && near(model.nodes[3].local.m[i][j], rotZ.m[i][j]);
        }
    }
    check(sameRotation, "model: quaternion matches Mat4::rotationZ");
    check(near(model.nodes[1].local.m[0][0], 1) && near(model.nodes[1].local.m[3][0], 0), "model: default identity");

    // findNode is case-insensitive and exact; unnamed frames cannot be looked up.
    check(model.findNode("Base") == 0 && model.findNode("base") == 0 && model.findNode("ARM") == 2, "model: findNode");
    check(model.findNode("Bas") == -1 && model.findNode("Base2") == -1 && model.findNode("") == -1,
          "model: findNode misses");

    // globalTransform = local * parent.local * ...
    const mb::Vec3 p{1, 1, 1};
    check(near(mb::transformPoint(p, model.globalTransform(2)), {3, 9, 9}), "model: globalTransform order");
    check(near(model.globalTransform(2).translationPart(), {5, 7, 7}), "model: globalTransform translation");
    check(near(mb::transformPoint(p, model.globalTransform(0)), {4, 7, 8}), "model: root globalTransform");
    check(near(mb::transformPoint(p, model.globalTransform(-1)), p), "model: invalid node is identity");

    // One pool for all primitives; indices are absolute.
    check(model.vertices.size() == 10 && model.indices.size() == 12, "model: pool sizes");
    check(model.meshes.size() == 2 && model.meshes[0].primitives.size() == 2 && model.meshes[1].primitives.size() == 1,
          "model: mesh structure");
    const auto prim = [&](int mesh, int i) { return model.meshes[mesh].primitives[i]; };
    check(prim(0, 0).firstIndex == 0 && prim(0, 0).indexCount == 6 && prim(0, 0).material == 0, "model: primitive 0");
    check(prim(0, 1).firstIndex == 6 && prim(0, 1).indexCount == 3 && prim(0, 1).material == 1, "model: primitive 1");
    check(prim(1, 0).firstIndex == 9 && prim(1, 0).indexCount == 3 && prim(1, 0).material == -1,
          "model: non-indexed primitive without material");
    const std::vector<std::uint32_t> expectedIndices = {0, 1, 2, 0, 2, 3, 4, 5, 6, 7, 8, 9};
    check(model.indices == expectedIndices, "model: absolute indices");

    check(near(model.vertices[2].position, {1, 1, 0}) && near(model.vertices[2].normal, {0, 0, -1}) &&
              near(model.vertices[2].uv.x, 1) && near(model.vertices[2].uv.y, 0),
          "model: full vertex");
    check(near(model.vertices[5].position, {2, 0, 1}) && near(model.vertices[5].normal, {0, 1, 0}) &&
              near(model.vertices[5].uv.x, 0) && near(model.vertices[5].uv.y, 0),
          "model: missing normal and uv get defaults");

    // Materials.
    check(model.materials.size() == 3, "model: material count");
    const mb::ModelMaterial& wood = model.materials[0];
    check(wood.name == "Wood" && near(wood.diffuse.r, 0.5f) && near(wood.diffuse.g, 0.25f) &&
              near(wood.diffuse.b, 0.75f) && near(wood.diffuse.a, 1),
          "model: diffuse");
    check(near(wood.specular.r, 0.1f) && near(wood.specular.g, 0.2f) && near(wood.specular.b, 0.3f) &&
              near(wood.specular.a, 1),
          "model: specular");
    check(near(wood.emissive.r, 0.4f) && near(wood.emissive.g, 0.5f) && near(wood.emissive.b, 0.6f), "model: emissive");
    check(near(wood.power, 12.5f) && wood.textureFile == "My Tex.png", "model: power and textureFile");
    check(wood.texturePath == tmp / "tex" / "My Tex.png", "model: texture uri decoded and normalized");
    const mb::ModelMaterial& ghost = model.materials[1];
    check(near(ghost.diffuse.a, 0.5f) && near(ghost.power, 0) && near(ghost.specular.r, 0) && near(ghost.specular.a, 1),
          "model: material defaults");
    check(ghost.textureFile == "Missing.png" && ghost.texturePath.empty(), "model: missing texture file");
    check(model.materials[2].textureFile == "Gone.jpg" && model.materials[2].texturePath.empty(),
          "model: textureMissing material");

    // Errors carry the path.
    const fs::path missing = tmp / "meshes" / "nope.glb";
    check(throwsWith([&] { mb::loadModel(missing); }, "nope.glb"), "model: missing file throws with path");
    writeFile(tmp / "meshes" / "junk.glb", "this is not a glb file at all");
    check(throwsWith([&] { mb::loadModel(tmp / "meshes" / "junk.glb"); }, "junk.glb"), "model: invalid glb throws");
    json strips = synthDocument();
    strips["meshes"][0]["primitives"][0]["mode"] = 5;
    writeGlb(tmp / "meshes" / "strip.glb", strips);
    check(throwsWith([&] { mb::loadModel(tmp / "meshes" / "strip.glb"); }, "strip.glb"),
          "model: non-triangle primitive throws");
}

// ---------------------------------------------------------------------------
// TextureIndex

void makeTextureTree(const fs::path& root, bool reverse) {
    std::vector<std::string> files = {
        "Pushka.jpg",  "01.jpg",           "01.png",           "Sky.JPG",       "alpha.tga",
        "alpha.jpg",   "shared.jpg",       "sub/shared.png",   "Icons/heart.png", "Icons/heart.jpg",
        "Icons/Star.dds", "a/dup.jpg",     "b/dup.png",        "a/tie.png",     "b/tie.png",
        "deep/er/leaf.png", "Weird Name.png", "conv.png",      ".DS_Store"};
    if (reverse) {
        std::vector<std::string> flipped(files.rbegin(), files.rend());
        files = flipped;
    }
    for (const std::string& f : files) {
        writeFile(root / f, f);
    }
}

void testTextureIndex(const fs::path& tmp) {
    const fs::path root = tmp / "textures";
    makeTextureTree(root, false);
    const mb::TextureIndex index(root);

    const auto rel = [&](std::string_view name) {
        const fs::path found = index.find(name);
        return found.empty() ? std::string("<none>") : found.lexically_relative(root).generic_string();
    };

    check(rel("Pushka") == "Pushka.jpg" && rel("pushka") == "Pushka.jpg" && rel("PUSHKA.JPG") == "Pushka.jpg",
          "textures: case-insensitive, extension optional");
    check(rel("sky") == "Sky.JPG" && rel("Sky.jpg") == "Sky.JPG", "textures: original spelling is returned");
    check(rel("01") == "01.png", "textures: png beats jpg");
    check(rel("01.jpg") == "01.jpg" && rel("01.png") == "01.png", "textures: an explicit extension is honoured");
    check(rel("alpha") == "alpha.jpg", "textures: jpg beats other formats");
    check(rel("shared") == "shared.jpg", "textures: root file beats a subfolder png");
    check(rel("dup") == "b/dup.png", "textures: png beats jpg across subfolders");
    check(rel("tie") == "a/tie.png", "textures: ties break alphabetically");
    check(rel("leaf") == "deep/er/leaf.png", "textures: nested files found by name");
    check(rel("heart") == "Icons/heart.png", "textures: subfolder file found by name");
    check(rel("icons/heart.png") == "Icons/heart.png" && rel("Icons\\Heart.PNG") == "Icons/heart.png",
          "textures: relative name, backslashes and case");
    check(rel("icons/heart") == "Icons/heart.png", "textures: relative name without extension");
    check(rel("Icons/heart.jpg") == "Icons/heart.jpg", "textures: explicit relative extension");
    check(rel("icons/star") == "Icons/Star.dds" && rel("star.dds") == "Icons/Star.dds", "textures: other formats indexed");
    check(rel("conv.dds") == "conv.png", "textures: unknown extension falls back to the stem");
    check(rel("weird name") == "Weird Name.png", "textures: names with spaces");
    check(rel("./icons//heart") == "Icons/heart.png", "textures: path is normalized");
    check(rel("missing") == "<none>" && rel("icons/missing") == "<none>" && rel("") == "<none>" &&
              rel(".DS_Store") == "<none>",
          "textures: misses");
    check(index.size() == 12, "textures: size counts distinct stems");

    // The same answers whatever order the filesystem hands the files out in.
    const fs::path reversedRoot = tmp / "textures_reversed";
    makeTextureTree(reversedRoot, true);
    const mb::TextureIndex reversed(reversedRoot);
    bool same = true;
    for (const char* name : {"01", "alpha", "shared", "dup", "tie", "leaf", "heart", "icons/heart", "star", "conv.dds"}) {
        same = same && index.find(name).lexically_relative(root) == reversed.find(name).lexically_relative(reversedRoot);
    }
    check(same, "textures: deterministic regardless of creation order");

    const mb::TextureIndex none(tmp / "does_not_exist");
    check(none.size() == 0 && none.find("anything").empty(), "textures: missing directory gives an empty index");
}

// ---------------------------------------------------------------------------
// MaterialLibrary

constexpr const char* kMaterials = R"({
  "version": 1,
  "texturePreload": ["Icons/bazuka.png"],
  "materials": [
    {"name": "shark.jpg", "group": "Levels/Sea", "textures": ["shark", "shark2"], "animated": true,
     "animTimeMs": 500, "density": 0.01, "splinter": "splinter-blue1.x",
     "d3d": {"diffuse": [0.1, 0.2, 0.3, 0.4], "ambient": [0.75, 0.5, 0.25, 1],
             "emissive": [0, 0.5, 0, 1], "specular": [1, 0, 0, 0.5], "power": 8},
     "physics": {"staticFriction": 0.6, "dynamicFriction": 0.4, "restitution": 0.001,
                 "frictionCombineMode": 1, "restitutionCombineMode": 2},
     "brick": {"type": 3, "energy": 2, "score": 91, "exploseType": 4, "exploseDelay": 250, "explosePower": 1.5},
     "sounds": {"drop": "shark_drop", "hit": "shark_hit", "dead": "shark_dead"},
     "renderStates": [[19, 5], [20, 6], [27, 1]]},
    {"name": "BoardMaterial"},
    {"name": "Glow", "renderStates": [[27, 1], [20, 2]]},
    {"name": "Off", "renderStates": [[27, 0], [20, 2]]},
    {"name": "NoEnable", "renderStates": [[20, 2], [19, 5]]},
    {"name": "BlendOnly", "renderStates": [[27, 1]]},
    {"name": "Looped", "sounds": {"period": "p", "loop": "l"}},
    {"name": "SHARK.JPG", "density": 9, "group": "Other"}
  ]
})";

void testMaterialLibrary(const fs::path& tmp) {
    const mb::MaterialLibrary lib = mb::MaterialLibrary::parse(kMaterials);
    check(lib.size() == 8 && lib.all().size() == 8, "materials: every definition is kept in file order");
    check(lib.all()[0].name == "shark.jpg" && lib.all()[7].name == "SHARK.JPG", "materials: file order");

    const mb::GameMaterial* shark = lib.find("Shark.JPG");
    check(shark == &lib.all()[0] && near(shark->density, 0.01f) && shark->group == "Levels/Sea",
          "materials: first duplicate wins, lookup is case-insensitive");
    check(shark && shark->name == "shark.jpg" && shark->textures == std::vector<std::string>{"shark", "shark2"} &&
              shark->animated && shark->animTimeMs == 500 && shark->splinter == "splinter-blue1.x",
          "materials: basic fields");
    check(shark && near(shark->diffuse.r, 0.1f) && near(shark->diffuse.g, 0.2f) && near(shark->diffuse.b, 0.3f) &&
              near(shark->diffuse.a, 0.4f),
          "materials: RGBA diffuse");
    check(shark && near(shark->ambient.g, 0.5f) && near(shark->emissive.g, 0.5f) && near(shark->specular.a, 0.5f) &&
              near(shark->power, 8),
          "materials: other colours and power");
    check(shark && near(shark->staticFriction, 0.6f) && near(shark->dynamicFriction, 0.4f) &&
              near(shark->restitution, 0.001f) && shark->frictionCombineMode == 1 && shark->restitutionCombineMode == 2,
          "materials: physics");
    check(shark && shark->brickType == 3 && shark->brickEnergy == 2 && shark->brickScore == 91 &&
              shark->exploseType == 4 && shark->exploseDelay == 250 && near(shark->explosePower, 1.5f),
          "materials: brick");
    check(shark && shark->dropSound == "shark_drop" && shark->hitSound == "shark_hit" &&
              shark->deadSound == "shark_dead" && shark->periodSound.empty() && shark->loopSound.empty(),
          "materials: sounds");
    check(shark && shark->renderStates == std::vector<std::pair<int, int>>{{19, 5}, {20, 6}, {27, 1}},
          "materials: render states");

    // Everything optional can be missing and falls back to GameMaterial's defaults.
    const mb::GameMaterial* board = lib.find("boardmaterial");
    const mb::GameMaterial defaults;
    check(board && board->group.empty() && board->textures.empty() && !board->animated && board->splinter.empty() &&
              board->renderStates.empty() && near(board->diffuse.r, defaults.diffuse.r) &&
              near(board->emissive.a, defaults.emissive.a) && near(board->restitution, defaults.restitution) &&
              board->brickEnergy == defaults.brickEnergy && board->dropSound.empty(),
          "materials: missing optional keys keep defaults");
    const mb::GameMaterial* looped = lib.find("looped");
    check(looped && looped->periodSound == "p" && looped->loopSound == "l" && looped->dropSound.empty(),
          "materials: partial sounds");
    check(lib.find("nothing") == nullptr && lib.find("") == nullptr, "materials: unknown name");

    check(lib.find("shark.jpg")->blendMode() == mb::BlendMode::AlphaBlend, "materials: blend 27=1 dest 6 is AlphaBlend");
    check(lib.find("glow")->blendMode() == mb::BlendMode::Additive, "materials: dest blend one is Additive");
    check(lib.find("off")->blendMode() == mb::BlendMode::Opaque, "materials: blending disabled");
    check(lib.find("noenable")->blendMode() == mb::BlendMode::Opaque, "materials: no state 27 is Opaque");
    check(lib.find("blendonly")->blendMode() == mb::BlendMode::AlphaBlend, "materials: blending without dest is AlphaBlend");
    check(lib.find("boardmaterial")->blendMode() == mb::BlendMode::Opaque, "materials: no render states is Opaque");

    check(throwsWith([] { mb::MaterialLibrary::parse("not json"); }, "invalid materials JSON"), "materials: bad JSON throws");
    check(throwsWith([] { mb::MaterialLibrary::parse("{}"); }, "materials"), "materials: missing array throws");
    check(throwsWith([] { mb::MaterialLibrary::parse(R"({"materials":[{"group":"x"}]})"); }, "material #0"),
          "materials: missing name throws");
    check(throwsWith([] { mb::MaterialLibrary::parse(R"({"materials":[{"name":"a","d3d":{"diffuse":[1,1,1]}}]})"); },
                     "material #0"),
          "materials: three-component colour throws");
    check(throwsWith([] { mb::MaterialLibrary::parse(R"({"version":2,"materials":[]})"); }, "version"),
          "materials: unknown version throws");
    check(mb::MaterialLibrary::parse(R"({"materials":[]})").size() == 0, "materials: empty library");

    const fs::path file = tmp / "materials.json";
    writeFile(file, kMaterials);
    check(mb::MaterialLibrary::load(file).size() == 8, "materials: load from file");
    check(throwsWith([&] { mb::MaterialLibrary::load(tmp / "absent.json"); }, "absent.json"),
          "materials: missing file throws with path");
}

}  // namespace

int main() {
    const fs::path tmp = fs::temp_directory_path() /
                         ("mb3-asset-tests-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    testModel(tmp);
    testTextureIndex(tmp);
    testMaterialLibrary(tmp);

    std::error_code ec;
    fs::remove_all(tmp, ec);
    if (failures == 0) {
        std::puts("all asset tests passed");
    }
    return failures == 0 ? 0 : 1;
}
