// GPU renderer tests: draw known scenes offscreen and check the pixels.
//
// Needs a GPU device (Metal on macOS, Vulkan elsewhere). Without one the test prints
// SKIP and exits with 77, which CTest reports as skipped. Every image is also written
// to <build>/test-output/ so a failure can be looked at.

#include "mb/engine/Image.h"
#include "mb/engine/Math.h"
#include "mb/engine/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifndef MB3_TEST_OUTPUT_DIR
#define MB3_TEST_OUTPUT_DIR "test-output"
#endif

namespace {

using namespace mb;

constexpr int kSize = 256;

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) {
        ++failures;
    }
}

struct Rgb {
    int r, g, b;
};

Rgb pixelAt(const Image& image, int x, int y) {
    const std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
    return {p[0], p[1], p[2]};
}

int to8(float v) {
    return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

Rgb rgb8(float r, float g, float b) {
    return {to8(r), to8(g), to8(b)};
}

std::string str(Rgb c) {
    return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + ")";
}

bool close(Rgb a, Rgb b, int tolerance) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
}

void expectPixel(const std::string& what, const Image& image, int x, int y, Rgb expected, int tolerance = 3) {
    const Rgb actual = pixelAt(image, x, y);
    check(close(actual, expected, tolerance),
          what + " at (" + std::to_string(x) + "," + std::to_string(y) + "): got " + str(actual) + ", want " +
              str(expected));
}

void save(const Image& image, const char* name) {
    const std::filesystem::path dir = MB3_TEST_OUTPUT_DIR;
    std::filesystem::create_directories(dir);
    if (!writePng(dir / name, image)) {
        check(false, std::string("writing ") + name);
    }
}

struct Rgba8 {
    std::uint8_t r, g, b, a;
};

template <class Fn>
Image makeImage(int width, int height, Fn pixel) {
    Image image;
    image.width = width;
    image.height = height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Rgba8 c = pixel(x, y);
            image.rgba.insert(image.rgba.end(), {c.r, c.g, c.b, c.a});
        }
    }
    return image;
}

// The scene every test draws: a camera at z = -3 looking at the origin, 60 degrees
// vertical field of view, and quads in the z = 0 plane.
struct Scene {
    Renderer& renderer;
    FrameParams frame;
    MeshHandle quad = 0;

    explicit Scene(Renderer& r) : renderer(r) {
        const Vec3 eye{0, 0, -3};
        frame.eye = eye;
        frame.view = lookAtLH(eye, {0, 0, 0}, {0, 1, 0});
        frame.projection = perspectiveFovLH(radians(60.0f), 1.0f, 0.1f, 100.0f);
        frame.clear = {0.2f, 0.4f, 0.6f, 1.0f};
        quad = makeQuad({0, 0, -1});
    }

    // A square of half-size 1 in the z = 0 plane, clockwise when seen from the camera. UV (0,0) is
    // the top-left corner, so a texture appears upright and unmirrored.
    MeshHandle makeQuad(Vec3 normal) {
        const Vertex vertices[] = {
            {{-1, 1, 0}, normal, {0, 0}},  // top left
            {{1, 1, 0}, normal, {1, 0}},   // top right
            {{1, -1, 0}, normal, {1, 1}},  // bottom right
            {{-1, -1, 0}, normal, {0, 1}}, // bottom left
        };
        const std::uint32_t indices[] = {0, 1, 2, 0, 2, 3};
        return renderer.createMesh(vertices, indices);
    }

    Image render(std::span<const DrawItem> items, std::span<const DebugLine> lines = {}) {
        return renderer.renderToImage(kSize, kSize, frame, items, lines);
    }
    Image renderWith(const FrameParams& other, std::span<const DrawItem> items) {
        return renderer.renderToImage(kSize, kSize, other, items);
    }

    // Pixel where a world-space point lands, using only Math.h: the reference for "where should it be".
    struct Pixel {
        int x, y;
    };
    Pixel project(Vec3 world) const {
        const Vec4 clip = transform({world.x, world.y, world.z, 1}, frame.view * frame.projection);
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        return {static_cast<int>(std::floor((ndcX * 0.5f + 0.5f) * kSize)),
                static_cast<int>(std::floor((0.5f - ndcY * 0.5f) * kSize))};
    }
};

MaterialParams unlitColor(Color c) {
    MaterialParams m;
    m.lit = false;
    m.diffuse = c;
    return m;
}

std::span<const DrawItem> one(const DrawItem& d) {
    return {&d, 1};
}

std::span<const DebugLine> one(const DebugLine& l) {
    return {&l, 1};
}

DrawItem item(MeshHandle mesh, const MaterialParams& material, Vec3 position, float scale = 1.0f) {
    DrawItem d;
    d.mesh = mesh;
    d.material = material;
    d.world = Mat4::scaling({scale, scale, scale}) * Mat4::translation(position);
    return d;
}

void testClear(Scene& s) {
    std::printf("clear colour and image size\n");
    const Image image = s.render({});
    check(image.width == kSize && image.height == kSize && image.rgba.size() == kSize * kSize * 4,
          "256x256 RGBA image");
    save(image, "01_clear.png");
    expectPixel("top-left", image, 0, 0, rgb8(0.2f, 0.4f, 0.6f));
    expectPixel("bottom-right", image, kSize - 1, kSize - 1, rgb8(0.2f, 0.4f, 0.6f));

    const Image small = s.renderer.renderToImage(128, 64, s.frame, {});
    check(small.width == 128 && small.height == 64 && small.rgba.size() == 128 * 64 * 4, "a second size works: 128x64");
    expectPixel("small image", small, 127, 63, rgb8(0.2f, 0.4f, 0.6f));
    const Image again = s.render({});
    expectPixel("back to 256x256", again, 100, 100, rgb8(0.2f, 0.4f, 0.6f));
}

void testTexturedQuad(Scene& s) {
    std::printf("textured quad: UV orientation and matrix convention\n");
    const Rgba8 red{255, 0, 0, 255}, green{0, 255, 0, 255}, blue{0, 0, 255, 255}, white{255, 255, 255, 255};
    const Image checker = makeImage(2, 2, [&](int x, int y) {
        return y == 0 ? (x == 0 ? red : green) : (x == 0 ? blue : white);
    });
    const TextureHandle texture = s.renderer.createTexture(checker, false);
    check(texture != 0, "createTexture returns a handle");

    MaterialParams m = unlitColor({1, 1, 1, 1});
    m.texture = texture;
    const DrawItem items[] = {item(s.quad, m, {0, 0, 0})};
    const Image image = s.render(items);
    save(image, "02_checker_quad.png");

    const auto topLeft = s.project({-0.5f, 0.5f, 0}), topRight = s.project({0.5f, 0.5f, 0});
    const auto bottomLeft = s.project({-0.5f, -0.5f, 0}), bottomRight = s.project({0.5f, -0.5f, 0});
    check(topLeft.x < kSize / 2 && topLeft.y < kSize / 2 && bottomRight.x > kSize / 2 && bottomRight.y > kSize / 2,
          "reference projection: +x is right and +y is up on screen");
    // Bilinear filtering mixes in a little of the neighbouring texels unless the sample sits exactly
    // on a texel centre, so these allow a few percent.
    expectPixel("top-left is red", image, topLeft.x, topLeft.y, {255, 0, 0}, 12);
    expectPixel("top-right is green", image, topRight.x, topRight.y, {0, 255, 0}, 12);
    expectPixel("bottom-left is blue", image, bottomLeft.x, bottomLeft.y, {0, 0, 255}, 12);
    expectPixel("bottom-right is white", image, bottomRight.x, bottomRight.y, {255, 255, 255}, 12);
    expectPixel("outside the quad", image, 5, 5, rgb8(0.2f, 0.4f, 0.6f));
    s.renderer.destroyTexture(texture);
}

void testDepth(Scene& s) {
    std::printf("depth test and layers\n");
    const MaterialParams red = unlitColor({1, 0, 0, 1}), green = unlitColor({0, 1, 0, 1});
    const DrawItem nearFirst[] = {item(s.quad, red, {0, 0, -1}), item(s.quad, green, {0, 0, 1})};
    const DrawItem farFirst[] = {item(s.quad, green, {0, 0, 1}), item(s.quad, red, {0, 0, -1})};
    const Image a = s.render(nearFirst);
    const Image b = s.render(farFirst);
    save(a, "03a_depth_near_first.png");
    save(b, "03b_depth_far_first.png");
    expectPixel("near quad wins, submitted first", a, kSize / 2, kSize / 2, {255, 0, 0});
    expectPixel("near quad wins, submitted last", b, kSize / 2, kSize / 2, {255, 0, 0});

    // A sky on layer 0 that does not write depth, placed behind the world: the world is
    // submitted first yet must still draw over it.
    MaterialParams sky = unlitColor({0, 0, 1, 1});
    sky.depthWrite = false;
    DrawItem skyItem = item(s.quad, sky, {0, 0, 5}, 10.0f);
    skyItem.layer = 0;
    const DrawItem layered[] = {item(s.quad, red, {0, 0, 0}, 0.5f), skyItem};
    const Image c = s.render(layered);
    save(c, "03c_layers.png");
    expectPixel("world over the sky", c, kSize / 2, kSize / 2, {255, 0, 0});
    expectPixel("sky behind the world", c, 5, 5, {0, 0, 255});

    // Depth test off: a far quad drawn after a near one overwrites it. Within a layer the renderer is
    // free to reorder opaque items by state, so the overlay goes on a later layer.
    MaterialParams noTest = unlitColor({0, 1, 0, 1});
    noTest.depthTest = false;
    DrawItem overlay = item(s.quad, noTest, {0, 0, 1});
    overlay.layer = 2;
    const DrawItem overdraw[] = {overlay, item(s.quad, red, {0, 0, -1})};
    const Image d = s.render(overdraw);
    save(d, "03d_no_depth_test.png");
    expectPixel("depth test disabled overwrites", d, kSize / 2, kSize / 2, {0, 255, 0});
}

void testCulling(Scene& s) {
    std::printf("face culling\n");
    const Vertex vertices[] = {
        {{-1, -1, 0}, {0, 0, -1}, {0, 1}},
        {{0, 1, 0}, {0, 0, -1}, {0.5f, 0}},
        {{1, -1, 0}, {0, 0, -1}, {1, 1}},
    };
    // Bottom left, top, bottom right is clockwise as seen from the camera.
    const std::uint32_t clockwise[] = {0, 1, 2};
    const std::uint32_t counterClockwise[] = {0, 2, 1};
    const MeshHandle cw = s.renderer.createMesh(vertices, clockwise);
    const MeshHandle ccw = s.renderer.createMesh(vertices, counterClockwise);
    const Scene::Pixel inside = s.project({0, -0.3f, 0});
    const Rgb clear = rgb8(0.2f, 0.4f, 0.6f);
    const Rgb green{0, 255, 0};

    struct Case {
        const char* name;
        MeshHandle mesh;
        CullMode cull;
        bool visible;
        const char* file;
    };
    const Case cases[] = {
        {"clockwise, cull back", cw, CullMode::Back, true, "04a_cw_back.png"},
        {"counter-clockwise, cull back", ccw, CullMode::Back, false, "04b_ccw_back.png"},
        {"counter-clockwise, no culling", ccw, CullMode::None, true, "04c_ccw_none.png"},
        {"clockwise, cull front", cw, CullMode::Front, false, "04d_cw_front.png"},
        {"counter-clockwise, cull front", ccw, CullMode::Front, true, "04e_ccw_front.png"},
    };
    for (const Case& c : cases) {
        MaterialParams m = unlitColor({0, 1, 0, 1});
        m.cull = c.cull;
        const DrawItem items[] = {item(c.mesh, m, {0, 0, 0})};
        const Image image = s.render(items);
        save(image, c.file);
        expectPixel(std::string(c.name) + (c.visible ? " is drawn" : " is culled"), image, inside.x, inside.y,
                    c.visible ? green : clear);
    }
}

void testBlending(Scene& s) {
    std::printf("alpha blend, additive and blended ordering\n");
    {
        DrawItem blended = item(s.quad, unlitColor({1, 0, 0, 0.5f}), {0, 0, 0});
        blended.material.blend = BlendMode::AlphaBlend;
        const Image image = s.render(one(blended));
        save(image, "05a_alpha_blend.png");
        expectPixel("50% red over the clear colour", image, kSize / 2, kSize / 2,
                    rgb8(0.5f * 1 + 0.5f * 0.2f, 0.5f * 0 + 0.5f * 0.4f, 0.5f * 0 + 0.5f * 0.6f));
        check(image.rgba[(kSize / 2 * kSize + kSize / 2) * 4 + 3] == 255, "alpha channel stays opaque");
    }
    {
        FrameParams frame = s.frame;
        frame.clear = {0.1f, 0.1f, 0.1f, 1};
        DrawItem additive = item(s.quad, unlitColor({0.5f, 0.25f, 0, 0.5f}), {0, 0, 0});
        additive.material.blend = BlendMode::Additive;
        const Image image = s.renderWith(frame, one(additive));
        save(image, "05b_additive.png");
        expectPixel("additive adds src * alpha", image, kSize / 2, kSize / 2, rgb8(0.1f + 0.25f, 0.1f + 0.125f, 0.1f));
    }
    {
        // Two translucent quads, the near one submitted first: sorting must still draw the far one first.
        FrameParams frame = s.frame;
        frame.clear = {0, 0, 0, 1};
        MaterialParams nearGreen = unlitColor({0, 1, 0, 0.5f});
        MaterialParams farRed = unlitColor({1, 0, 0, 0.5f});
        for (MaterialParams* m : {&nearGreen, &farRed}) {
            m->blend = BlendMode::AlphaBlend;
            m->depthWrite = false;
        }
        const DrawItem items[] = {item(s.quad, nearGreen, {0, 0, -1}), item(s.quad, farRed, {0, 0, 1})};
        const Image image = s.renderWith(frame, items);
        save(image, "05c_blend_order.png");
        expectPixel("far blended first, near over it", image, kSize / 2, kSize / 2, rgb8(0.25f, 0.5f, 0));
    }
    {
        // A translucent quad in front of an opaque one, submitted before it: opaque geometry draws first.
        MaterialParams glass = unlitColor({1, 0, 0, 0.5f});
        glass.blend = BlendMode::AlphaBlend;
        glass.depthWrite = false;
        const DrawItem items[] = {item(s.quad, glass, {0, 0, -1}), item(s.quad, unlitColor({0, 0, 1, 1}), {0, 0, 2})};
        const Image image = s.render(items);
        save(image, "05d_blend_after_opaque.png");
        expectPixel("glass over an opaque quad", image, kSize / 2, kSize / 2, rgb8(0.5f, 0, 0.5f));
    }
}

// One channel of the documented lighting equation, for a white-textured surface.
float litChannel(float diffuse, float ambient, float emissive, float lightDiffuse, float lightAmbient, float facing) {
    return std::clamp(emissive + ambient * lightAmbient + diffuse * lightDiffuse * std::max(facing, 0.0f), 0.0f, 1.0f);
}

void testLighting(Scene& s) {
    std::printf("lighting\n");
    MaterialParams lit;
    lit.diffuse = {0.6f, 0.6f, 0.6f, 1};
    lit.ambient = {0.2f, 0.2f, 0.2f, 1};
    lit.emissive = {0, 0, 0, 0};
    lit.lit = true;
    const DrawItem litItem = item(s.quad, lit, {0, 0, 0});

    FrameParams frame = s.frame;
    frame.light.diffuse = {1, 1, 1, 1};
    frame.light.ambient = {0.5f, 0.5f, 0.5f, 1};
    const int c = kSize / 2;

    // The quad faces the camera, so its normal is (0, 0, -1).
    frame.light.direction = {0, 0, 1};
    const Image head = s.renderWith(frame, one(litItem));
    const float grazingZ = 0.2f;  // cosine of the angle to the normal
    frame.light.direction = {std::sqrt(1 - grazingZ * grazingZ), 0, grazingZ};
    const Image grazing = s.renderWith(frame, one(litItem));
    frame.light.direction = {0, 0, -1};
    const Image behind = s.renderWith(frame, one(litItem));
    save(head, "06a_lit_head_on.png");
    save(grazing, "06b_lit_grazing.png");
    save(behind, "06c_lit_from_behind.png");

    const float headOn = litChannel(0.6f, 0.2f, 0, 1, 0.5f, 1.0f);
    const float graze = litChannel(0.6f, 0.2f, 0, 1, 0.5f, grazingZ);
    const float back = litChannel(0.6f, 0.2f, 0, 1, 0.5f, -1.0f);
    expectPixel("head-on light", head, c, c, rgb8(headOn, headOn, headOn));
    expectPixel("grazing light", grazing, c, c, rgb8(graze, graze, graze));
    expectPixel("light from behind leaves only ambient", behind, c, c, rgb8(back, back, back));
    check(pixelAt(head, c, c).r > pixelAt(grazing, c, c).r + 50 &&
              pixelAt(grazing, c, c).r > pixelAt(behind, c, c).r + 10,
          "head-on is brighter than grazing, which is brighter than from behind");

    // An emissive-only material shows its emissive colour whatever the light does.
    MaterialParams glow;
    glow.diffuse = {0, 0, 0, 1};
    glow.ambient = {0, 0, 0, 1};
    glow.emissive = {0.3f, 0.6f, 0.9f, 0};
    const DrawItem glowItem = item(s.quad, glow, {0, 0, 0});
    frame.light.direction = {0, 0, 1};
    const Image glowBright = s.renderWith(frame, one(glowItem));
    frame.light.diffuse = {0, 0, 0, 1};
    frame.light.ambient = {0, 0, 0, 1};
    const Image glowDark = s.renderWith(frame, one(glowItem));
    save(glowBright, "06d_emissive_bright_light.png");
    save(glowDark, "06e_emissive_no_light.png");
    expectPixel("emissive under a bright light", glowBright, c, c, rgb8(0.3f, 0.6f, 0.9f));
    expectPixel("emissive under no light", glowDark, c, c, rgb8(0.3f, 0.6f, 0.9f));

    // Unlit: texture * diffuse, no light involved.
    DrawItem unlit = item(s.quad, unlitColor({0.5f, 1, 1, 1}), {0, 0, 0});
    frame.light.diffuse = {1, 1, 1, 1};
    const Image unlitImage = s.renderWith(frame, one(unlit));
    save(unlitImage, "06f_unlit.png");
    expectPixel("unlit is texture * diffuse", unlitImage, c, c, rgb8(0.5f, 1, 1));

    // The normal follows the world matrix: turned 60 degrees about y, the quad's normal becomes
    // (-sin, 0, -cos), and a light travelling the opposite way lights it fully.
    frame.light.diffuse = {1, 1, 1, 1};
    frame.light.ambient = {0, 0, 0, 1};
    MaterialParams half;
    half.diffuse = {0.5f, 0.5f, 0.5f, 1};
    half.ambient = {0, 0, 0, 1};
    DrawItem turned = item(s.quad, half, {0, 0, 0}, 0.8f);
    turned.world = Mat4::scaling({0.8f, 0.8f, 0.8f}) * Mat4::rotationY(radians(60.0f));
    const float sine = std::sin(radians(60.0f)), cosine = std::cos(radians(60.0f));
    frame.light.direction = {sine, 0, cosine};
    const Image turnedImage = s.renderWith(frame, one(turned));
    save(turnedImage, "06g_rotated_normal.png");
    expectPixel("rotated quad lit along its new normal", turnedImage, c, c, rgb8(0.5f, 0.5f, 0.5f));

    // Non-uniform scale needs the inverse-transpose: with x twice as long as y and z the normal
    // (1,0,-1)/sqrt2 tilts towards -z. Using the world matrix itself would give 0.4 here instead of 0.5.
    const MeshHandle slanted = s.makeQuad(normalize({1, 0, -1}));
    DrawItem stretched = item(slanted, half, {0, 0, 0});
    stretched.world = Mat4::scaling({1.0f, 0.5f, 0.5f});
    const Vec3 worldNormal = normalize(Vec3{0.5f, 0, -1});  // n * transpose(inverse(A)), A = diag(1, 0.5, 0.5)
    frame.light.direction = -worldNormal;
    const Image stretchedImage = s.renderWith(frame, one(stretched));
    save(stretchedImage, "06h_stretched_normal.png");
    expectPixel("normal under non-uniform scale", stretchedImage, c, c, rgb8(0.5f, 0.5f, 0.5f));
    s.renderer.destroyMesh(slanted);
}

void testDebugLines(Scene& s) {
    std::printf("debug lines\n");
    const Scene::Pixel mid = s.project({0, 0.3f, 0});
    const DebugLine line{{-1, 0.3f, 0}, {1, 0.3f, 0}, {1, 1, 0, 1}};
    const Image image = s.render({}, one(line));
    save(image, "07a_debug_line.png");
    bool found = false;
    for (int dy = -1; dy <= 1; ++dy) {
        found = found || close(pixelAt(image, mid.x, mid.y + dy), {255, 255, 0}, 3);
    }
    check(found, "a yellow line crosses the middle of the image");
    expectPixel("above the line", image, mid.x, mid.y - 6, rgb8(0.2f, 0.4f, 0.6f));

    // The line is depth tested: a quad in front of it hides it. The same line without the quad is the control.
    const DebugLine behind{{-0.5f, 0.3f, 1}, {0.5f, 0.3f, 1}, {1, 1, 0, 1}};
    const Scene::Pixel farMid = s.project({0, 0.3f, 1});
    const DrawItem wall[] = {item(s.quad, unlitColor({1, 0, 0, 1}), {0, 0, 0})};
    const Image control = s.render({}, one(behind));
    const Image hidden = s.render(wall, one(behind));
    save(hidden, "07b_debug_line_hidden.png");
    bool controlVisible = false, hiddenVisible = false;
    for (int dy = -1; dy <= 1; ++dy) {
        controlVisible = controlVisible || close(pixelAt(control, farMid.x, farMid.y + dy), {255, 255, 0}, 3);
        hiddenVisible = hiddenVisible || close(pixelAt(hidden, farMid.x, farMid.y + dy), {255, 255, 0}, 30);
    }
    check(controlVisible, "the line behind the quad is visible on its own");
    check(!hiddenVisible, "the quad hides the line behind it");
    expectPixel("the quad is intact", hidden, farMid.x, farMid.y, {255, 0, 0});

    // Many lines: more than the initial buffer holds, to exercise growing it.
    std::vector<DebugLine> grid;
    for (int i = 0; i < 1500; ++i) {
        const float y = -1.5f + 3.0f * static_cast<float>(i) / 1500.0f;
        grid.push_back({{-1.5f, y, 0}, {1.5f, y, 0}, {0, 1, 1, 1}});
    }
    const Image many = s.render({}, grid);
    save(many, "07c_many_lines.png");
    expectPixel("1500 lines draw", many, kSize / 2, kSize / 2, {0, 255, 255});
}

void testAlphaTest(Scene& s) {
    std::printf("alpha test\n");
    // Quadrants: alpha 0, 0.4 (both below the 0.5 cut-off), 0.6 and 1.
    const Image texels = makeImage(2, 2, [](int x, int y) {
        if (y == 0) {
            return x == 0 ? Rgba8{255, 0, 0, 0} : Rgba8{0, 255, 0, 102};
        }
        return x == 0 ? Rgba8{0, 0, 255, 153} : Rgba8{255, 255, 255, 255};
    });
    const TextureHandle texture = s.renderer.createTexture(texels, false);
    MaterialParams m = unlitColor({1, 1, 1, 1});
    m.texture = texture;
    const DrawItem items[] = {item(s.quad, m, {0, 0, 0})};
    const Image image = s.render(items);
    save(image, "08_alpha_test.png");
    const Rgb clear = rgb8(0.2f, 0.4f, 0.6f);
    const auto topLeft = s.project({-0.5f, 0.5f, 0}), topRight = s.project({0.5f, 0.5f, 0});
    const auto bottomLeft = s.project({-0.5f, -0.5f, 0}), bottomRight = s.project({0.5f, -0.5f, 0});
    expectPixel("alpha 0 is discarded", image, topLeft.x, topLeft.y, clear);
    expectPixel("alpha 0.4 is discarded", image, topRight.x, topRight.y, clear);
    expectPixel("alpha 0.6 is kept", image, bottomLeft.x, bottomLeft.y, {0, 0, 255}, 12);
    expectPixel("alpha 1 is kept", image, bottomRight.x, bottomRight.y, {255, 255, 255}, 12);
    // The texture is left alive: the Renderer's destructor must release it.
}

struct Spread {
    double mean;
    double deviation;
};

Spread lumaSpread(const Image& image, int x0, int y0, int x1, int y1) {
    std::vector<double> values;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            values.push_back(pixelAt(image, x, y).r);
        }
    }
    double mean = 0;
    for (double v : values) {
        mean += v;
    }
    mean /= static_cast<double>(values.size());
    double variance = 0;
    for (double v : values) {
        variance += (v - mean) * (v - mean);
    }
    return {mean, std::sqrt(variance / static_cast<double>(values.size()))};
}

void testMipmaps(Scene& s) {
    std::printf("mipmaps\n");
    // 64x64 noise drawn about 8 pixels wide: with a mip chain the lookups average the noise
    // away (level 3), without one they alias and the pixels stay noisy.
    std::uint32_t seed = 12345;
    const Image noise = makeImage(64, 64, [&](int, int) {
        seed = seed * 1664525u + 1013904223u;
        const auto v = static_cast<std::uint8_t>(seed >> 24);
        return Rgba8{v, v, v, 255};
    });
    const TextureHandle mipped = s.renderer.createTexture(noise, true);
    const TextureHandle flat = s.renderer.createTexture(noise, false);
    check(mipped != 0 && flat != 0 && mipped != flat, "both textures were created");

    const float half = 4.0f / (kSize / (2.0f * 3.0f * std::tan(radians(30.0f))));  // 4 pixels, in world units
    MaterialParams m = unlitColor({1, 1, 1, 1});
    m.texture = mipped;
    const DrawItem withMips[] = {item(s.quad, m, {0, 0, 0}, half)};
    m.texture = flat;
    const DrawItem withoutMips[] = {item(s.quad, m, {0, 0, 0}, half)};
    const Image a = s.render(withMips);
    const Image b = s.render(withoutMips);
    save(a, "09a_mipmapped.png");
    save(b, "09b_not_mipmapped.png");
    const Spread mipSpread = lumaSpread(a, 126, 126, 129, 129);
    const Spread rawSpread = lumaSpread(b, 126, 126, 129, 129);
    std::printf("    mip chain: mean %.1f deviation %.1f; no mips: mean %.1f deviation %.1f\n", mipSpread.mean,
                mipSpread.deviation, rawSpread.mean, rawSpread.deviation);
    check(mipSpread.deviation < 25 && rawSpread.deviation > 2 * mipSpread.deviation,
          "the mip chain smooths minified noise");
    check(std::abs(mipSpread.mean - 127) < 30, "the smoothed noise is mid grey");

    // Odd sizes get a full chain too.
    const Image odd = makeImage(100, 37, [](int, int) { return Rgba8{255, 0, 0, 255}; });
    const TextureHandle oddTexture = s.renderer.createTexture(odd, true);
    MaterialParams oddMaterial = unlitColor({1, 1, 1, 1});
    oddMaterial.texture = oddTexture;
    const DrawItem oddItems[] = {item(s.quad, oddMaterial, {0, 0, 0})};
    const Image c = s.render(oddItems);
    expectPixel("100x37 mipmapped texture", c, kSize / 2, kSize / 2, {255, 0, 0});

    for (TextureHandle t : {mipped, flat, oddTexture}) {
        s.renderer.destroyTexture(t);
    }
}

void testHandles(Scene& s) {
    std::printf("handles and cleanup\n");
    check(s.renderer.createTexture(Image{}, true) == 0, "an empty image gives handle 0");
    check(s.renderer.createMesh({}, {}) == 0, "an empty mesh gives handle 0");
    const Vertex v[] = {{{0, 0, 0}, {0, 0, -1}, {0, 0}}};
    const std::uint32_t badIndex[] = {0, 0, 7};
    check(s.renderer.createMesh(v, badIndex) == 0, "an out-of-range index is rejected");

    const MeshHandle temporary = s.makeQuad({0, 0, -1});
    check(temporary != 0 && temporary != s.quad, "meshes get distinct handles");
    const DrawItem items[] = {item(temporary, unlitColor({1, 0, 0, 1}), {0, 0, 0})};
    s.renderer.destroyMesh(temporary);
    const Image image = s.render(items);  // drawing a destroyed mesh is skipped, not a crash
    expectPixel("destroyed mesh draws nothing", image, kSize / 2, kSize / 2, rgb8(0.2f, 0.4f, 0.6f));
    s.renderer.destroyMesh(temporary);  // destroying twice is harmless

    // A sub-range of the index buffer: the second triangle of the quad only (bottom left half).
    DrawItem half = item(s.quad, unlitColor({1, 0, 0, 1}), {0, 0, 0});
    half.firstIndex = 3;
    half.indexCount = 3;
    const Image halfImage = s.render(one(half));
    save(halfImage, "10_index_range.png");
    const auto bottomLeft = s.project({-0.5f, -0.5f, 0}), topRight = s.project({0.5f, 0.5f, 0});
    expectPixel("second triangle is drawn", halfImage, bottomLeft.x, bottomLeft.y, {255, 0, 0});
    expectPixel("first triangle is not", halfImage, topRight.x, topRight.y, rgb8(0.2f, 0.4f, 0.6f));
}

}  // namespace

int main() {
    std::unique_ptr<Renderer> renderer;
    try {
        renderer = std::make_unique<Renderer>();
    } catch (const std::exception& e) {
        std::printf("SKIP: no GPU device (%s)\n", e.what());
        return 77;
    }
    std::printf("backend: %s\n", renderer->backendName());

    Scene scene(*renderer);
    check(scene.quad != 0, "createMesh returns a handle");
    testClear(scene);
    testTexturedQuad(scene);
    testDepth(scene);
    testCulling(scene);
    testBlending(scene);
    testLighting(scene);
    testDebugLines(scene);
    testAlphaTest(scene);
    testMipmaps(scene);
    testHandles(scene);

    renderer.reset();
    std::printf(failures ? "%d check(s) FAILED\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
