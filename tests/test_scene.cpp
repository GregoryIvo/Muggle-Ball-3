// Scene building tests: which nodes a level draws, where, with which texture, material and
// light. Needs neither a GPU nor game data: models, materials and textures are made up here.

#include "mb/game/GameCamera.h"
#include "mb/game/LevelCatalog.h"
#include "mb/game/SceneBuilder.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mb;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

bool near(Vec3 a, Vec3 b, float tolerance = 1e-4f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

// A scratch directory holding empty files named like game textures: TextureIndex only needs names.
struct TextureDir {
    fs::path path;

    TextureDir() {
        static int counter = 0;
        path = fs::temp_directory_path() / ("mb3_scene_test_" + std::to_string(counter++));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TextureDir() { fs::remove_all(path); }

    fs::path add(const std::string& file) const {
        fs::create_directories((path / file).parent_path());
        std::ofstream(path / file).put('x');
        return path / file;
    }
};

int addMesh(Model& model, std::vector<ModelPrimitive> primitives) {
    model.meshes.push_back({std::move(primitives)});
    return static_cast<int>(model.meshes.size()) - 1;
}

int addMaterial(Model& model, const std::string& textureFile, const fs::path& texturePath, Color diffuse = {1, 1, 1, 1}) {
    ModelMaterial m;
    m.name = "m" + std::to_string(model.materials.size());
    m.diffuse = diffuse;
    m.emissive = {0.1f, 0.2f, 0.3f, 1};
    m.textureFile = textureFile;
    m.texturePath = texturePath;
    model.materials.push_back(m);
    return static_cast<int>(model.materials.size()) - 1;
}

int addNode(Model& model, const std::string& name, int parent, Mat4 local, int mesh = -1) {
    ModelNode node;
    node.name = name;
    node.local = local;
    node.parent = parent;
    node.mesh = mesh;
    model.nodes.push_back(node);
    const int index = static_cast<int>(model.nodes.size()) - 1;
    if (parent >= 0) {
        model.nodes[parent].children.push_back(index);
    } else {
        model.roots.push_back(index);
    }
    return index;
}

Actor brick(const std::string& mesh, const std::string& frame, const std::string& material, std::uint32_t id = 1,
            ObjectType type = ObjectType::Brick) {
    Actor actor;
    GameObject object;
    object.type = type;
    object.id = id;
    object.mesh = mesh;
    object.frame = frame;
    object.material = material;
    actor.object = object;
    return actor;
}

ModelLookup lookupOf(const Model& model, std::string file = "test.glb") {
    return [&model, file](const std::string& requested) { return requested == file ? &model : nullptr; };
}

MaterialLibrary makeMaterials() {
    return MaterialLibrary::parse(R"({"version": 1, "materials": [
      {"name": "Wood.jpg", "textures": ["Wood"],
       "d3d": {"diffuse": [1, 0.5, 0.25, 1], "ambient": [0.75, 0.75, 0.75, 1], "emissive": [0, 0, 0, 1]}},
      {"name": "Crate.jpg", "textures": ["Crate-D", "Crate-C", "Crate-B", "Crate-A"],
       "d3d": {"diffuse": [1, 1, 1, 1]}, "brick": {"energy": 4}},
      {"name": "Spin", "textures": ["F1", "F2", "F3"], "animated": true, "animTimeMs": 150},
      {"name": "Glass", "textures": ["Wood"], "renderStates": [[19, 5], [20, 6], [27, 1]]},
      {"name": "Water.png", "textures": ["W1", "W2"], "animated": true, "animTimeMs": 150,
       "renderStates": [[19, 5], [20, 3], [27, 1]]},
      {"name": "Lost", "textures": ["Nowhere"]},
      {"name": "BoardMaterial", "textures": ["Ball"], "animated": true,
       "d3d": {"diffuse": [1, 1, 1, 1], "ambient": [1, 1, 1, 1], "emissive": [1, 1, 1, 1]}},
      {"name": "lightMaterial", "d3d": {"diffuse": [0.75, 0.75, 0.75, 1], "ambient": [0.5, 0.5, 0.5, 1]}}
    ]})");
}

std::vector<std::uint8_t> d3dLight(std::uint32_t type, Color diffuse, Color ambient, Vec3 direction) {
    std::vector<std::uint8_t> bytes(0x68, 0);
    const auto putU32 = [&](std::size_t offset, std::uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
    };
    const auto putF32 = [&](std::size_t offset, float value) { putU32(offset, std::bit_cast<std::uint32_t>(value)); };
    putU32(0, type);
    const float d[4] = {diffuse.r, diffuse.g, diffuse.b, diffuse.a};
    const float a[4] = {ambient.r, ambient.g, ambient.b, ambient.a};
    for (int i = 0; i < 4; ++i) {
        putF32(4 + 4 * i, d[i]);
        putF32(36 + 4 * i, a[i]);
    }
    putF32(52, 7);  // position: must not be mistaken for the direction
    putF32(64, direction.x);
    putF32(68, direction.y);
    putF32(72, direction.z);
    return bytes;
}

void testMeshFileName() {
    check(meshFileName("cannon.x") == "cannon.glb", "mesh name: extension replaced");
    check(meshFileName("bg/BG_Day.X") == "bg/bg_day.glb", "mesh name: lowercased, folder kept");
    check(meshFileName("Prizes\\Cannon.X") == "prizes/cannon.glb", "mesh name: backslash becomes slash");
}

// Model: root "A" (mesh), root "B" (mesh) with child "C" (mesh), root "D" with no mesh.
Model frameModel() {
    Model model;
    const int mat = addMaterial(model, "", {});
    const auto oneTriangle = [&] { return addMesh(model, {{0, 3, mat}}); };
    addNode(model, "A", -1, Mat4::identity(), oneTriangle());
    const int b = addNode(model, "B", -1, Mat4::identity(), oneTriangle());
    addNode(model, "C", b, Mat4::identity(), oneTriangle());
    addNode(model, "D", -1, Mat4::identity());
    return model;
}

void testFrameSelection() {
    const Model model = frameModel();
    const MaterialLibrary materials = makeMaterials();
    TextureDir dir;
    const TextureIndex textures(dir.path);

    Level level;
    level.actors = {brick("Test.x", "", ""), brick("test.x", "b", ""), brick("test.x", "C", ""),
                    brick("test.x", "Nope", ""), brick("other.x", "", "")};
    const SceneDescription scene = describeScene(level, lookupOf(model), materials, textures);
    // no frame: A, B, C (D has no mesh); frame "b" (any case): B and its child C; frame C: C only.
    check(scene.draws.size() == 3 + 2 + 1, "frame selection: draw count");
    check(scene.report.missingFrames == std::set<std::string>{"test.x:Nope"}, "frame selection: unknown frame is reported");
    check(scene.report.missingMeshes == std::set<std::string>{"other.x"}, "frame selection: unknown mesh is reported");
    check(std::ranges::all_of(scene.draws, [](const SceneDraw& d) { return d.meshFile == "test.glb"; }), "mesh file name");
}

void testActorsWithoutMeshAreSkipped() {
    const Model model = frameModel();
    const MaterialLibrary materials = makeMaterials();
    TextureDir dir;
    const TextureIndex textures(dir.path);

    Level level;
    level.actors.push_back(Actor{});                            // no object at all
    level.actors.push_back(brick("", "", "", 1, ObjectType::ShadowCaster));
    level.actors.push_back(brick("", "", "", 2, ObjectType::Light));
    check(describeScene(level, lookupOf(model), materials, textures).draws.empty(), "actors without a mesh draw nothing");
}

void testClothSegmentsAndPlaceholder() {
    const Model model = frameModel();
    const MaterialLibrary materials = makeMaterials();
    TextureDir dir;
    const TextureIndex textures(dir.path);

    Actor cloth = brick("test.x", "", "", 1, ObjectType::Cloth);
    cloth.object->clothSegments.push_back(brick("test.x", "A", ""));
    Level level;
    level.actors.push_back(cloth);
    const SceneDescription scene = describeScene(level, lookupOf(model), materials, textures);
    check(scene.draws.size() == 1, "cloth: the placeholder mesh is skipped, a segment's own mesh is drawn");
}

void testWorldMatrix() {
    Model model;
    const int mat = addMaterial(model, "", {});
    const int mesh = addMesh(model, {{0, 3, mat}});
    const int parent = addNode(model, "Root", -1, Mat4::translation({0, 5, 0}));
    // local scale then translation: a point p lands at p * 2 + (1, 0, 0), then the parent adds (0, 5, 0).
    addNode(model, "Leaf", parent, Mat4::scaling({2, 2, 2}) * Mat4::translation({1, 0, 0}), mesh);

    Actor actor = brick("test.x", "Leaf", "");
    // A quarter turn about +y in PhysX (column-vector) form is the transpose of the row-vector rotation.
    const float c = 0, s = 1;
    actor.pose.rotation = {c, 0, s, 0, 1, 0, -s, 0, c};  // R * p: (x, y, z) -> (z, y, -x)
    actor.pose.position = {10, 0, 0};
    Level level;
    level.actors.push_back(actor);

    const MaterialLibrary materials = makeMaterials();
    TextureDir dir;
    const TextureIndex textures(dir.path);
    const SceneDescription scene = describeScene(level, lookupOf(model), materials, textures);
    check(scene.draws.size() == 1, "world matrix: one draw");
    // (1, 0, 0) -> scale (2, 0, 0) -> translate (3, 0, 0) -> parent (3, 5, 0) -> actor R: (0, 5, -3) + (10, 0, 0).
    const Vec3 p = transformPoint({1, 0, 0}, scene.draws.at(0).item.world);
    check(near(p, {10, 5, -3}), "world matrix: node transforms, then parents, then the actor pose");
}

void testTextureRules() {
    TextureDir dir;
    const fs::path wood = dir.add("Wood.jpg");
    dir.add("Crate-D.jpg");
    dir.add("Crate-C.jpg");
    dir.add("Crate-B.jpg");
    const fs::path crateA = dir.add("Crate-A.jpg");
    const fs::path f1 = dir.add("F1.png");
    const fs::path f2 = dir.add("F2.png");
    const fs::path f3 = dir.add("F3.png");
    const fs::path w1 = dir.add("W1.png");
    const fs::path w2 = dir.add("W2.png");
    const fs::path sky = dir.add("Sky.jpg");
    const fs::path water = dir.add("Water.jpg");
    const fs::path own = dir.add("Own.jpg");
    const fs::path other = dir.add("Other.jpg");
    const TextureIndex textures(dir.path);
    const MaterialLibrary materials = makeMaterials();

    // One primitive textured by the .x ("Own.jpg"), one frame holding two differently textured primitives.
    Model model;
    const int ownMat = addMaterial(model, "Own.jpg", own);
    const int otherMat = addMaterial(model, "Other.jpg", other);
    const int lostMat = addMaterial(model, "Gone.jpg", {});
    const int waterMat = addMaterial(model, "water.png", water);
    addNode(model, "One", -1, Mat4::identity(), addMesh(model, {{0, 3, ownMat}}));
    addNode(model, "Two", -1, Mat4::identity(), addMesh(model, {{3, 3, ownMat}, {6, 3, otherMat}}));
    addNode(model, "Lost", -1, Mat4::identity(), addMesh(model, {{9, 3, lostMat}}));
    addNode(model, "LostToo", -1, Mat4::identity(), addMesh(model, {{12, 3, ownMat}, {15, 3, lostMat}}));
    addNode(model, "Board", -1, Mat4::identity(), addMesh(model, {{18, 3, waterMat}, {21, 3, ownMat}}));

    const auto describe = [&](const Actor& actor) {
        Level level;
        level.actors.push_back(actor);
        return describeScene(level, lookupOf(model), materials, textures);
    };

    // 1. A game material with textures replaces the .x texture when the subtree has only one.
    SceneDescription scene = describe(brick("test.x", "One", "wood.jpg"));
    check(scene.draws.at(0).textures == std::vector<fs::path>{wood}, "texture: the game material's texture replaces the .x's");
    check(near(scene.draws.at(0).item.material.diffuse.g, 0.5f), "colour: the game material's diffuse");
    check(near(scene.draws.at(0).item.material.emissive.r, 0.0f), "colour: the game material's emissive, not the .x's");

    // 2. Without a game material the .x texture and colours stay.
    scene = describe(brick("test.x", "One", ""));
    check(scene.draws.at(0).textures == std::vector<fs::path>{own}, "texture: no material keeps the .x texture");
    check(near(scene.draws.at(0).item.material.emissive.b, 0.3f), "colour: no game material uses the .x colours");
    check(scene.draws.at(0).item.material.blend == BlendMode::Opaque, "blend: opaque by default");

    // 3. Two different .x textures in one subtree: the object keeps its own, whatever the material says.
    scene = describe(brick("test.x", "Two", "wood.jpg"));
    check(scene.draws.size() == 2 && scene.draws[0].textures == std::vector<fs::path>{own} &&
              scene.draws[1].textures == std::vector<fs::path>{other},
          "texture: several .x textures are kept");
    check(near(scene.draws[1].item.material.diffuse.g, 0.5f), "colour: game material colours still apply");

    // 4. An .x texture the converter could not find counts as a different texture, and is reported.
    scene = describe(brick("test.x", "LostToo", "wood.jpg"));
    check(scene.draws.size() == 2 && scene.draws[0].textures == std::vector<fs::path>{own} && scene.draws[1].textures.empty(),
          "texture: an unresolved .x texture counts as a second texture");
    check(scene.report.missingTextures == std::set<std::string>{"Gone.jpg"}, "texture: unresolved name is reported");
    scene = describe(brick("test.x", "Lost", "wood.jpg"));
    check(scene.draws.at(0).textures == std::vector<fs::path>{wood} && scene.report.missingTextures.empty(),
          "texture: a game material covers a single unresolved .x texture");

    // 5. Animated material: all frames, in order.
    scene = describe(brick("test.x", "One", "Spin"));
    check(scene.draws.at(0).textures == std::vector<fs::path>{f1, f2, f3} && scene.draws.at(0).animTimeMs == 150,
          "texture: animated material gives its frames");

    // 6. Several textures without animation: the fresh brick shows the one at index energy - 1.
    scene = describe(brick("test.x", "One", "Crate.jpg", 7));
    check(scene.draws.at(0).textures == std::vector<fs::path>{crateA} && scene.draws.at(0).animTimeMs == 0,
          "texture: damage states show the intact one");

    // 7. No game material, but the material names a texture.
    scene = describe(brick("test.x", "One", "sky.jpg"));
    check(scene.draws.at(0).textures == std::vector<fs::path>{sky}, "texture: material named like a texture file");
    check(near(scene.draws.at(0).item.material.emissive.b, 0.3f), "colour: ... with the .x colours");

    // 8. Blending comes from the game material.
    scene = describe(brick("test.x", "One", "Glass"));
    check(scene.draws.at(0).item.material.blend == BlendMode::AlphaBlend, "blend: from the game material");

    // 9. A game material whose textures are all missing is reported and the .x texture is kept.
    scene = describe(brick("test.x", "One", "Lost"));
    check(scene.draws.at(0).textures == std::vector<fs::path>{own}, "texture: unresolvable game texture falls back to the .x");
    check(scene.report.missingTextures == std::set<std::string>{"Nowhere"}, "texture: unresolvable game texture is reported");

    // 10. An .x texture that is itself a game material name ("water.png"): that material animates and blends
    // it, while the object's own material (a board's) keeps supplying the colours.
    scene = describe(brick("test.x", "Board", "BoardMaterial"));
    check(scene.draws.size() == 2, "texture: board draws both primitives");
    check(scene.draws.at(0).textures == std::vector<fs::path>{w1, w2} && scene.draws.at(0).animTimeMs == 150,
          "texture: the .x texture's own game material supplies animation frames");
    check(scene.draws.at(0).item.material.blend == BlendMode::AlphaBlend, "blend: ... and the blend mode");
    check(near(scene.draws.at(0).item.material.emissive.r, 1.0f), "colour: ... while colours stay the object's");
    check(scene.draws.at(1).textures == std::vector<fs::path>{own} && scene.draws.at(1).item.material.blend == BlendMode::Opaque,
          "texture: the other primitive keeps its own texture");
}

void testBackground() {
    TextureDir dir;
    const TextureIndex textures(dir.path);
    const MaterialLibrary materials = makeMaterials();
    Model model;
    const int mat = addMaterial(model, "", {});
    addNode(model, "Sky", -1, Mat4::identity(), addMesh(model, {{0, 3, mat}}));

    Level level;
    level.actors = {brick("test.x", "", "", 1, ObjectType::Background), brick("test.x", "", "", 2)};
    const SceneDescription scene = describeScene(level, lookupOf(model), materials, textures);
    const DrawItem& sky = scene.draws.at(0).item;
    check(sky.layer == kBackgroundLayer && !sky.material.lit && !sky.material.depthWrite &&
              sky.material.cull == CullMode::None,
          "background: first layer, unlit, no depth write, both sides");
    const DrawItem& world = scene.draws.at(1).item;
    check(world.layer == kWorldLayer && world.material.lit && world.material.depthWrite &&
              world.material.cull == CullMode::Back,
          "other objects: world layer, lit, depth-written, back faces culled");
}

void testLights() {
    check(!parseLight({}).has_value(), "light: empty data");
    check(!parseLight(d3dLight(1, {1, 1, 1, 1}, {1, 1, 1, 1}, {0, -1, 0})).has_value(), "light: point light is not understood");
    check(!parseLight(d3dLight(3, {1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0})).has_value(), "light: no direction");
    std::vector<std::uint8_t> shortData = d3dLight(3, {1, 1, 1, 1}, {1, 1, 1, 1}, {0, -1, 0});
    shortData.resize(0x60);
    check(!parseLight(shortData).has_value(), "light: truncated data");

    const auto light = parseLight(d3dLight(3, {0.5f, 0.6f, 0.7f, 1}, {0.2f, 0.3f, 0.4f, 1}, {0.001f, -0.663f, 0.749f}));
    check(light.has_value() && near(light->diffuse.g, 0.6f) && near(light->ambient.b, 0.4f) &&
              near(light->direction, {0.001f, -0.663f, 0.749f}),
          "light: diffuse, ambient and direction are read from their D3DLIGHT8 offsets");

    const MaterialLibrary materials = makeMaterials();
    Level plain;
    const DirectionalLight defaults = levelLight(plain, materials);
    check(near(defaults.diffuse.r, 0.75f) && near(defaults.ambient.r, 0.5f) &&
              near(defaults.direction, kDefaultLightDirection),
          "light: default takes its colours from lightMaterial");
    const DirectionalLight bare = levelLight(plain, MaterialLibrary::parse(R"({"materials": []})"));
    check(near(bare.diffuse.r, kDefaultLightDiffuse) && near(bare.ambient.g, kDefaultLightAmbient),
          "light: default constants without lightMaterial");

    Actor lightActor = brick("", "", "", 5, ObjectType::Light);
    lightActor.object->extra = d3dLight(3, {0.5f, 0.5f, 0.5f, 1}, {0.25f, 0.25f, 0.25f, 1}, {0, -1, 0});
    Level withLight;
    withLight.actors = {brick("x.x", "", ""), lightActor};
    const DirectionalLight own = levelLight(withLight, materials);
    check(near(own.diffuse.r, 0.5f) && near(own.ambient.r, 0.25f) && near(own.direction, {0, -1, 0}),
          "light: the level's light object wins");
}

void testShapeLines() {
    Actor actor;
    actor.pose.position = {10, 0, 0};
    Shape box;
    box.type = ShapeType::Box;
    box.halfExtents = {1, 2, 3};
    box.localPose.position = {0, 2, 0};
    box.localPose.rotation = {0, 0, 1, 0, 1, 0, -1, 0, 0};  // PhysX R * p: (x, y, z) -> (z, y, -x)
    actor.shapes.push_back(box);

    Level level;
    level.actors.push_back(actor);
    std::vector<DebugLine> lines = shapeLines(level);
    check(lines.size() == 12, "shape lines: a box has 12 edges");

    // The local corner (1, 2, 3) becomes (3, 2, -1) by the shape rotation, then moves by the shape's (0, 2, 0)
    // and the actor's (10, 0, 0).
    const Vec3 corner{13, 4, -1};
    const bool hasCorner = std::ranges::any_of(lines, [&](const DebugLine& l) { return near(l.a, corner) || near(l.b, corner); });
    check(hasCorner, "shape lines: shape pose, then actor pose");
    const bool onlyEdges = std::ranges::all_of(lines, [](const DebugLine& l) {
        const Vec3 d = l.b - l.a;
        const float length2 = dot(d, d);
        return near(length2, 4) || near(length2, 16) || near(length2, 36);
    });
    check(onlyEdges, "shape lines: edges have the box's side lengths");

    Shape sphere;
    sphere.type = ShapeType::Sphere;
    sphere.radius = 2;
    Shape capsule;
    capsule.type = ShapeType::Capsule;
    capsule.radius = 1;
    capsule.height = 4;
    Shape convex;
    convex.type = ShapeType::Convex;
    Actor second;
    second.shapes = {sphere, capsule, convex};
    level.actors = {second};
    lines = shapeLines(level);
    check(!lines.empty(), "shape lines: spheres and capsules are drawn");
    float lowest = 1e9f, highest = -1e9f, widest = 0;
    for (const DebugLine& l : lines) {
        for (const Vec3 p : {l.a, l.b}) {
            lowest = std::min(lowest, p.y);
            highest = std::max(highest, p.y);
            widest = std::max(widest, std::fabs(p.x));
        }
    }
    // The capsule spans y in [-h/2 - r, h/2 + r] = [-3, 3] along its local +Y axis; the sphere reaches x = 2.
    check(near(lowest, -3, 1e-3f) && near(highest, 3, 1e-3f) && near(widest, 2, 1e-3f),
          "shape lines: sphere radius and capsule extent");
}

void testCatalog() {
    TextureDir dir;
    dir.add("levels/b-level.json");
    dir.add("levels/A-Level.JSON");
    dir.add("levels/c-level.json");
    dir.add("levels/notes.txt");
    const fs::path outside = dir.add("outside.json");

    LevelCatalog catalog(dir.path / "levels");
    check(catalog.levels().size() == 3, "catalog: only .json files");
    check(catalog.levels().at(0).name == "A-Level" && catalog.levels().at(1).name == "b-level" &&
              catalog.levels().at(2).name == "c-level",
          "catalog: sorted by name, ignoring case");
    check(catalog.find("B-LEVEL") == std::optional<std::size_t>(1), "catalog: names match in any case");
    check(!catalog.find("missing").has_value(), "catalog: unknown name");
    const std::optional<std::size_t> added = catalog.find(outside.string());
    check(added && catalog.levels().at(*added).name == "outside", "catalog: a path joins the list");
    check(LevelCatalog(dir.path / "nope").levels().empty(), "catalog: missing directory is empty");
}

void testCamera() {
    Camera camera;
    check(near(camera.eye(), kGameCameraEye, 1e-3f), "camera: starts at the game camera");
    const Vec3 target = kGameCameraTarget;
    const float distance = length(camera.eye() - target);

    // The target is straight ahead: it lands in the middle of the screen, in front of the eye.
    const Vec4 clip = transform({target.x, target.y, target.z, 1}, camera.view() * camera.projection(4.0f / 3.0f));
    check(near(clip.x / clip.w, 0, 1e-3f) && near(clip.y / clip.w, 0, 1e-3f) && clip.w > 0, "camera: looks at the target");

    camera.orbit(100, 0);
    check(near(length(camera.eye() - target), distance, 1e-3f) && !near(camera.eye(), kGameCameraEye, 0.1f),
          "camera: orbiting keeps the distance to the target");
    camera.zoom(3);
    check(length(camera.eye() - target) < distance, "camera: zooming in gets closer");
    camera.pan(50, 20);
    camera.reset();
    check(near(camera.eye(), kGameCameraEye, 1e-3f), "camera: reset returns to the game camera");

    camera.orbit(0, 100000);
    check(std::isfinite(camera.eye().x) && camera.eye().y > 0, "camera: pitch is limited");
}

}  // namespace

int main() {
    testMeshFileName();
    testFrameSelection();
    testActorsWithoutMeshAreSkipped();
    testClothSegmentsAndPlaceholder();
    testWorldMatrix();
    testTextureRules();
    testBackground();
    testLights();
    testShapeLines();
    testCatalog();
    testCamera();
    if (failures == 0) {
        std::puts("all scene tests passed");
    }
    return failures == 0 ? 0 : 1;
}
