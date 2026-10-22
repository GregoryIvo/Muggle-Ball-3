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

#ifdef __APPLE__
#include <mach/mach.h>
#endif

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

std::span<const EffectBatch> one(const EffectBatch& b) {
    return {&b, 1};
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

// What a blend factor multiplies, per channel, for the given source colour and alpha and destination colour and alpha.
float factorValue(BlendFactor factor, float src, float srcAlpha, float dst, float dstAlpha) {
    switch (factor) {
        case BlendFactor::Zero: return 0;
        case BlendFactor::One: return 1;
        case BlendFactor::SrcColor: return src;
        case BlendFactor::InvSrcColor: return 1 - src;
        case BlendFactor::SrcAlpha: return srcAlpha;
        case BlendFactor::InvSrcAlpha: return 1 - srcAlpha;
        case BlendFactor::DestAlpha: return dstAlpha;
        case BlendFactor::InvDestAlpha: return 1 - dstAlpha;
        case BlendFactor::DestColor: return dst;
        case BlendFactor::InvDestColor: return 1 - dst;
    }
    return 0;
}

void testBlendFactors(Scene& s) {
    std::printf("blend factors\n");
    // The clear alpha is the destination alpha, since the alpha channel is never written.
    FrameParams frame = s.frame;
    frame.clear = {0.2f, 0.4f, 0.6f, 0.25f};
    const float dst[3] = {0.2f, 0.4f, 0.6f}, dstAlpha = 0.25f;
    const float src[3] = {0.8f, 0.5f, 0.3f}, srcAlpha = 0.6f;
    const int center = kSize / 2;

    const auto drawn = [&](BlendFactor srcFactor, BlendFactor dstFactor) {
        DrawItem d = item(s.quad, unlitColor({src[0], src[1], src[2], srcAlpha}), {0, 0, 0});
        d.material.blend = BlendMode::Custom;
        d.material.srcBlend = srcFactor;
        d.material.dstBlend = dstFactor;
        return s.renderWith(frame, one(d));
    };
    const auto expected = [&](BlendFactor srcFactor, BlendFactor dstFactor) {
        float rgb[3];
        for (int c = 0; c < 3; ++c) {
            rgb[c] = src[c] * factorValue(srcFactor, src[c], srcAlpha, dst[c], dstAlpha) +
                     dst[c] * factorValue(dstFactor, src[c], srcAlpha, dst[c], dstAlpha);
        }
        return rgb8(rgb[0], rgb[1], rgb[2]);
    };

    // Each of the ten factors on its own, as the source factor against zero and as the destination factor
    // against zero, so a swapped or misnumbered factor shows.
    const char* const names[] = {"zero", "one", "src colour", "inv src colour", "src alpha", "inv src alpha",
                                 "dest alpha", "inv dest alpha", "dest colour", "inv dest colour"};
    for (int i = 0; i < 10; ++i) {
        const auto factor = static_cast<BlendFactor>(i + 1);
        expectPixel(std::string("source factor ") + names[i], drawn(factor, BlendFactor::Zero), center, center,
                    expected(factor, BlendFactor::Zero));
        expectPixel(std::string("destination factor ") + names[i], drawn(BlendFactor::Zero, factor), center, center,
                    expected(BlendFactor::Zero, factor));
    }

    // The sea of the original: Direct3D SRCBLEND = SRCALPHA, DESTBLEND = SRCCOLOR.
    const Image water = drawn(BlendFactor::SrcAlpha, BlendFactor::SrcColor);
    save(water, "05e_water_blend.png");
    expectPixel("src * a + dst * src (the original's water)", water, center, center,
                expected(BlendFactor::SrcAlpha, BlendFactor::SrcColor));

    // Custom blended items are sorted back to front like the other blended ones. With these factors the order
    // shows: the far red quad first gives green only, the far red quad last gives red only.
    frame.clear = {0, 0, 0, 1};
    MaterialParams nearGreen = unlitColor({0, 1, 0, 0.5f});
    MaterialParams farRed = unlitColor({1, 0, 0, 0.5f});
    for (MaterialParams* m : {&nearGreen, &farRed}) {
        m->blend = BlendMode::Custom;
        m->srcBlend = BlendFactor::SrcAlpha;
        m->dstBlend = BlendFactor::SrcColor;
        m->depthWrite = false;
    }
    const DrawItem items[] = {item(s.quad, nearGreen, {0, 0, -1}), item(s.quad, farRed, {0, 0, 1})};
    const Image ordered = s.renderWith(frame, items);
    save(ordered, "05f_custom_blend_order.png");
    expectPixel("far custom blend first, near over it", ordered, center, center, rgb8(0, 0.5f, 0));
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

// Memory this process holds, which on Apple silicon includes the GPU's allocations; 0 where it cannot be read.
std::size_t footprintBytes() {
#ifdef __APPLE__
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<std::size_t>(info.phys_footprint);
    }
#endif
    return 0;
}

// Rendering the same scene again and again must not grow the process. SDL's Metal back end used to keep
// about 4 MB of depth buffers and uniform buffers per call at 1024x768 after a fence-based submit.
// A solid-colour sprite: texture 0 draws the tint.
Sprite solid(float x, float y, float w, float h, Color tint) {
    Sprite sprite;
    sprite.x = x;
    sprite.y = y;
    sprite.width = w;
    sprite.height = h;
    sprite.tint = tint;
    return sprite;
}

// 16x16: four 8x8 quadrants, red and green on top, blue and white below.
Image quadrantsImage() {
    const Rgba8 red{255, 0, 0, 255}, green{0, 255, 0, 255}, blue{0, 0, 255, 255}, white{255, 255, 255, 255};
    return makeImage(16, 16, [&](int x, int y) { return y < 8 ? (x < 8 ? red : green) : (x < 8 ? blue : white); });
}

void testSpritePlacement(Scene& s) {
    std::printf("sprites: pixel placement, tint and alpha\n");
    const Rgb clear = rgb8(0.2f, 0.4f, 0.6f);
    const Sprite box = solid(40, 30, 50, 20, {1, 0, 0, 1});
    const Image image = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&box, 1));
    save(image, "11a_sprite_placement.png");
    // Pixel (x, y) is covered when x0 <= x < x0 + w: the origin is the top-left corner and y runs down.
    expectPixel("top-left pixel", image, 40, 30, {255, 0, 0}, 0);
    expectPixel("bottom-right pixel", image, 89, 49, {255, 0, 0}, 0);
    expectPixel("left of the box", image, 39, 40, clear, 0);
    expectPixel("right of the box", image, 90, 40, clear, 0);
    expectPixel("above the box", image, 60, 29, clear, 0);
    expectPixel("below the box", image, 60, 50, clear, 0);

    // The coordinates are the target's own pixels, whatever its size.
    const Sprite corner = solid(100, 40, 28, 24, {0, 1, 0, 1});
    const Image small = s.renderer.renderToImage(128, 64, s.frame, {}, {}, std::span(&corner, 1));
    expectPixel("128x64: last pixel is covered", small, 127, 63, {0, 255, 0}, 0);
    expectPixel("128x64: first pixel is covered", small, 100, 40, {0, 255, 0}, 0);
    expectPixel("128x64: outside", small, 99, 40, clear, 0);

    // Tint colours a solid sprite; alpha blends it with what is behind.
    const Sprite sprites[] = {solid(0, 0, 40, 40, {0.5f, 1.0f, 0.25f, 1}), solid(50, 0, 40, 40, {1, 1, 1, 0.5f}),
                              solid(100, 0, 40, 40, {1, 1, 1, 0})};
    const Image tinted = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, sprites);
    save(tinted, "11b_sprite_tint_alpha.png");
    expectPixel("tint", tinted, 20, 20, rgb8(0.5f, 1.0f, 0.25f), 1);
    expectPixel("half alpha white over the clear colour", tinted, 70, 20, rgb8(0.6f, 0.7f, 0.8f), 1);
    expectPixel("zero alpha leaves the clear colour", tinted, 120, 20, clear, 0);
    check(pixelAt(tinted, 70, 20).r != 0 && tinted.rgba[(20 * kSize + 70) * 4 + 3] == 255,
          "the alpha channel stays opaque");
}

void testSpriteTexture(Scene& s) {
    std::printf("sprites: texture, UV rectangle, texture alpha\n");
    const Rgb clear = rgb8(0.2f, 0.4f, 0.6f);
    const TextureHandle texture = s.renderer.createTexture(quadrantsImage(), false);
    const Rgb red{255, 0, 0}, green{0, 255, 0}, blue{0, 0, 255}, white{255, 255, 255};

    // The whole texture on 64x64 pixels at (20, 20): each quadrant is 32x32 pixels. Points sit well inside a
    // quadrant, away from the filtered seams.
    Sprite sprite = solid(20, 20, 64, 64, {1, 1, 1, 1});
    sprite.texture = texture;
    const Image whole = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&sprite, 1));
    save(whole, "11c_sprite_texture.png");
    expectPixel("top-left quadrant", whole, 36, 36, red);
    expectPixel("top-right quadrant", whole, 68, 36, green);
    expectPixel("bottom-left quadrant", whole, 36, 68, blue);
    expectPixel("bottom-right quadrant", whole, 68, 68, white);

    // A sub-rectangle: only the top-right quadrant, stretched over the whole sprite.
    sprite.uv0 = {0.5f, 0.0f};
    sprite.uv1 = {1.0f, 0.5f};
    const Image sub = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&sprite, 1));
    save(sub, "11d_sprite_uv_rect.png");
    expectPixel("UV rect, centre", sub, 52, 52, green);
    expectPixel("UV rect, near top-left", sub, 26, 26, green);
    expectPixel("UV rect, near bottom-right", sub, 78, 78, green);

    // Swapped corners mirror the image: left and right (or top and bottom) change places.
    sprite.uv0 = {1.0f, 0.0f};
    sprite.uv1 = {0.0f, 1.0f};
    const Image mirrored = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&sprite, 1));
    expectPixel("mirrored: top-left shows the green quadrant", mirrored, 36, 36, green);
    expectPixel("mirrored: top-right shows the red one", mirrored, 68, 36, red);
    sprite.uv0 = {0.0f, 1.0f};
    sprite.uv1 = {1.0f, 0.0f};
    const Image flipped = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&sprite, 1));
    expectPixel("flipped: top-left shows the blue quadrant", flipped, 36, 36, blue);
    expectPixel("flipped: bottom-left shows the red one", flipped, 36, 68, red);

    // Coordinates past 1 repeat the texture: two whole copies side by side.
    sprite = solid(20, 20, 128, 64, {1, 1, 1, 1});
    sprite.texture = texture;
    sprite.uv1 = {2.0f, 1.0f};
    const Image tiled = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&sprite, 1));
    save(tiled, "11e_sprite_repeat.png");
    expectPixel("repeat: first copy", tiled, 36, 36, red);
    expectPixel("repeat: second copy starts again with red", tiled, 100, 36, red);
    expectPixel("repeat: second copy, green quadrant", tiled, 132, 36, green);
    expectPixel("repeat: second copy, blue quadrant", tiled, 100, 68, blue);

    // Texture and tint multiply; texture alpha cuts the sprite out.
    sprite = solid(20, 20, 64, 64, {0.5f, 1, 1, 1});
    sprite.texture = texture;
    const Image multiplied = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&sprite, 1));
    expectPixel("tint times texture", multiplied, 36, 36, rgb8(0.5f, 0, 0));
    expectPixel("tint times a white texel", multiplied, 68, 68, rgb8(0.5f, 1, 1));

    const Image cutout = makeImage(16, 16, [](int x, int) {
        return x < 8 ? Rgba8{255, 255, 0, 0} : Rgba8{255, 255, 0, 255};
    });
    const TextureHandle cutoutTexture = s.renderer.createTexture(cutout, false);
    sprite = solid(20, 20, 64, 64, {1, 1, 1, 1});
    sprite.texture = cutoutTexture;
    const Image cut = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&sprite, 1));
    save(cut, "11f_sprite_texture_alpha.png");
    expectPixel("transparent texels show what is behind", cut, 30, 52, clear);
    expectPixel("opaque texels draw", cut, 74, 52, {255, 255, 0});
    s.renderer.destroyTexture(texture);
    s.renderer.destroyTexture(cutoutTexture);
}

void testSpriteOrder(Scene& s) {
    std::printf("sprites: submission order, over the 3D scene\n");
    const Rgb red{255, 0, 0}, green{0, 255, 0}, blue{0, 0, 255};
    const Sprite first = solid(20, 20, 60, 60, {1, 0, 0, 1}), second = solid(50, 20, 60, 60, {0, 1, 0, 1});
    const Sprite forward[] = {first, second}, backward[] = {second, first};
    const Image a = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, forward);
    const Image b = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, backward);
    save(a, "11g_sprite_order.png");
    expectPixel("the later sprite is on top", a, 65, 50, green);
    expectPixel("reversed submission reverses the overlap", b, 65, 50, red);
    expectPixel("the earlier sprite shows where it is not covered", a, 30, 50, red);

    // Textures are batched, but only runs of neighbours: A B A keeps its order.
    const TextureHandle t1 = s.renderer.createTexture(makeImage(2, 2, [](int, int) { return Rgba8{255, 0, 0, 255}; }), false);
    const TextureHandle t2 = s.renderer.createTexture(makeImage(2, 2, [](int, int) { return Rgba8{0, 0, 255, 255}; }), false);
    Sprite a1 = solid(20, 100, 60, 60, {1, 1, 1, 1}), b2 = solid(40, 100, 60, 60, {1, 1, 1, 1}),
           a3 = solid(60, 100, 60, 60, {1, 1, 1, 1});
    a1.texture = t1;
    b2.texture = t2;
    a3.texture = t1;
    const Sprite interleaved[] = {a1, b2, a3};
    const Image runs = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, interleaved);
    save(runs, "11h_sprite_texture_runs.png");
    expectPixel("only the first sprite", runs, 30, 130, red);
    expectPixel("second over first", runs, 50, 130, blue);
    expectPixel("third over second", runs, 70, 130, red);
    expectPixel("third alone", runs, 110, 130, red);
    s.renderer.destroyTexture(t1);
    s.renderer.destroyTexture(t2);

    // Over the scene: sprites ignore depth and draw after meshes and debug lines, whatever their order.
    const DrawItem wall[] = {item(s.quad, unlitColor({1, 0, 0, 1}), {0, 0, 0})};
    const DebugLine line{{-1, 0, -2}, {1, 0, -2}, {1, 1, 0, 1}};  // in front of the quad
    const Sprite over = solid(100, 100, 56, 56, {0, 0, 1, 1});
    const Image scene = s.renderer.renderToImage(kSize, kSize, s.frame, wall, one(line), std::span(&over, 1));
    save(scene, "11i_sprite_over_scene.png");
    expectPixel("the sprite covers the mesh and the line", scene, 128, 128, blue);
    expectPixel("the mesh is there around it", scene, 128, 90, red);
    const Image meshOnly = s.render(wall);
    expectPixel("the mesh alone is unchanged at the sprite's spot", meshOnly, 128, 128, red);

    // The same frame in the same call without any scene: the clear colour and the sprite.
    const Image only = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&over, 1));
    expectPixel("no scene: sprite", only, 128, 128, blue);
    expectPixel("no scene: clear colour", only, 20, 20, rgb8(0.2f, 0.4f, 0.6f));
}

void testSpriteRotation(Scene& s) {
    std::printf("sprites: rotation\n");
    const Rgb clear = rgb8(0.2f, 0.4f, 0.6f), red{255, 0, 0}, blue{0, 0, 255};
    // Left half red, right half blue.
    const Image halves = makeImage(16, 16, [](int x, int) { return x < 8 ? Rgba8{255, 0, 0, 255} : Rgba8{0, 0, 255, 255}; });
    const TextureHandle texture = s.renderer.createTexture(halves, false);

    // 80x20 centred on (128, 128), turned a quarter circle clockwise: now 20 wide and 80 tall, the red left
    // end at the top and the blue right end at the bottom.
    Sprite sprite = solid(88, 118, 80, 20, {1, 1, 1, 1});
    sprite.texture = texture;
    sprite.rotation = kPi / 2;
    const Image quarter = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&sprite, 1));
    save(quarter, "11j_sprite_rotated_90.png");
    expectPixel("clockwise: red end is on top", quarter, 128, 95, red);
    expectPixel("clockwise: blue end is below", quarter, 128, 161, blue);
    expectPixel("now narrow: left of the old extent", quarter, 100, 128, clear);
    expectPixel("now narrow: right of the old extent", quarter, 156, 128, clear);
    expectPixel("now tall: above the old extent", quarter, 128, 80, clear);

    // A 40x40 square at 45 degrees is a diamond: its corners reach 28.3 pixels from the centre along the axes
    // and its old corners, 28.3 pixels diagonally, are now empty.
    Sprite square = solid(108, 108, 40, 40, {1, 1, 0, 1});
    square.rotation = kPi / 4;
    const Image diamond = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&square, 1));
    save(diamond, "11k_sprite_rotated_45.png");
    expectPixel("diamond: top corner", diamond, 128, 128 - 26, {255, 255, 0});
    expectPixel("diamond: right corner", diamond, 128 + 26, 128, {255, 255, 0});
    expectPixel("diamond: the square's old corner is empty", diamond, 128 + 17, 128 - 17, clear);
    expectPixel("diamond: beyond the tip", diamond, 128, 128 - 31, clear);

    // Rotating twice a half turn (negative: anticlockwise) puts the halves back where they were.
    sprite.rotation = -kPi;
    sprite.x = 88;
    sprite.y = 118;
    const Image half = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&sprite, 1));
    expectPixel("half turn: red is now on the right", half, 150, 128, red);
    expectPixel("half turn: blue is now on the left", half, 106, 128, blue);
    s.renderer.destroyTexture(texture);
}

void testSpriteEdgeCases(Scene& s) {
    std::printf("sprites: bad input and many sprites\n");
    const Rgb clear = rgb8(0.2f, 0.4f, 0.6f);
    Sprite lost = solid(10, 10, 50, 50, {1, 0, 0, 1});
    lost.texture = 9999;  // never created
    const Sprite nothing = solid(70, 10, 0, 50, {0, 1, 0, 1});
    const Sprite fine = solid(130, 10, 50, 50, {0, 0, 1, 1});
    const Sprite batch[] = {lost, nothing, fine};
    const Image image = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, batch);
    save(image, "11l_sprite_bad_input.png");
    expectPixel("an unknown texture draws nothing", image, 30, 30, clear, 0);
    expectPixel("a zero-width sprite draws nothing", image, 70, 30, clear, 0);
    expectPixel("the valid sprite after them still draws", image, 150, 30, {0, 0, 255}, 0);
    const Image none = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span<const Sprite>(&lost, 1));
    expectPixel("a frame whose only sprite is invalid", none, 30, 30, clear, 0);

    // More sprites than the initial buffer holds: a 70 x 70 grid of 3 pixel squares.
    std::vector<Sprite> grid;
    for (int y = 0; y < 70; ++y) {
        for (int x = 0; x < 70; ++x) {
            grid.push_back(solid(static_cast<float>(x * 3), static_cast<float>(y * 3), 2, 2,
                                 {(x % 2) ? 1.0f : 0.0f, (y % 2) ? 1.0f : 0.0f, 0, 1}));
        }
    }
    const Image many = s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, grid);
    save(many, "11m_many_sprites.png");
    expectPixel("4900 sprites: first", many, 0, 0, {0, 0, 0}, 0);
    expectPixel("4900 sprites: red one", many, 3, 0, {255, 0, 0}, 0);
    expectPixel("4900 sprites: green one", many, 0, 3, {0, 255, 0}, 0);
    expectPixel("4900 sprites: last", many, 69 * 3, 69 * 3, {255, 255, 0}, 0);
    expectPixel("4900 sprites: the gap between squares", many, 2, 2, clear, 0);
}

// Two triangles making a square of half-size `half` in the z = `z` plane, one colour, UV over the whole of it.
std::vector<EffectVertex> effectSquare(float half, float z, Color colour) {
    const EffectVertex tl{{-half, half, z}, {0, 0}, colour}, tr{{half, half, z}, {1, 0}, colour},
        br{{half, -half, z}, {1, 1}, colour}, bl{{-half, -half, z}, {0, 1}, colour};
    return {tl, tr, br, tl, br, bl};
}

void testEffects(Scene& s) {
    std::printf("effect batches: blending, colour only, depth, order, bad input\n");
    const Rgb clear = rgb8(0.2f, 0.4f, 0.6f);
    const int mid = kSize / 2;
    const auto draw = [&](std::span<const EffectBatch> batches, std::span<const DrawItem> items = {}) {
        return s.renderer.renderToImage(kSize, kSize, s.frame, items, {}, {}, batches);
    };

    // Additive adds the colour times its alpha to what is there; alpha blending mixes it in.
    const std::vector<EffectVertex> glow = effectSquare(0.5f, 0, {0.3f, 0.3f, 0.1f, 1});
    EffectBatch batch;
    batch.vertices = glow;
    batch.blend = BlendMode::Additive;
    const Image additive = draw(one(batch));
    save(additive, "12a_effect_additive.png");
    expectPixel("additive adds to the clear colour", additive, mid, mid, rgb8(0.5f, 0.7f, 0.7f));
    expectPixel("and leaves what it does not cover", additive, 4, 4, clear);

    const std::vector<EffectVertex> half = effectSquare(0.5f, 0, {1, 0, 0, 0.5f});
    batch.vertices = half;
    batch.blend = BlendMode::Additive;
    expectPixel("additive scales by alpha", draw(one(batch)), mid, mid, rgb8(0.7f, 0.4f, 0.6f));
    batch.blend = BlendMode::AlphaBlend;
    expectPixel("alpha blend mixes", draw(one(batch)), mid, mid, rgb8(0.6f, 0.2f, 0.3f));

    // The texture multiplies the colour, or only shapes the sprite when the colour replaces it.
    const Rgba8 green{0, 255, 0, 255}, nothing{0, 255, 0, 0};
    const TextureHandle greenLeft = s.renderer.createTexture(makeImage(16, 1, [&](int x, int) { return x < 8 ? green : nothing; }), false);
    const std::vector<EffectVertex> red = effectSquare(0.5f, 0, {1, 0, 0, 1});
    batch.vertices = red;
    batch.texture = greenLeft;
    batch.blend = BlendMode::AlphaBlend;
    const Image modulated = draw(one(batch));
    expectPixel("a green texture times a red colour is black", modulated, mid - 20, mid, {0, 0, 0});
    expectPixel("where the texture is clear nothing is drawn", modulated, mid + 20, mid, clear);
    batch.colourOnly = true;
    const Image replaced = draw(one(batch));
    save(replaced, "12b_effect_colour_only.png");
    expectPixel("colour only: the colour, not the texture's", replaced, mid - 20, mid, {255, 0, 0});
    expectPixel("colour only: the texture's alpha still cuts the shape", replaced, mid + 20, mid, clear);

    // Depth: with the test on, a mesh in front hides the effect; with it off the effect is drawn over it. Neither writes depth.
    const DrawItem wall[] = {item(s.quad, unlitColor({0, 0, 1, 1}), {0, 0, -1}, 0.3f)};
    batch.texture = 0;
    batch.colourOnly = false;
    batch.blend = BlendMode::Additive;
    batch.vertices = glow;
    batch.depthTest = true;
    expectPixel("depth test on: hidden by what is in front", draw(one(batch), wall), mid, mid, {0, 0, 255});
    batch.depthTest = false;
    expectPixel("depth test off: drawn over it", draw(one(batch), wall), mid, mid, {77, 77, 255});
    batch.depthTest = true;
    const std::vector<EffectVertex> behind = effectSquare(0.5f, 1, {0.3f, 0.3f, 0.1f, 1});
    batch.vertices = behind;
    expectPixel("depth test on: an effect in front of nothing is drawn", draw(one(batch)), mid, mid, rgb8(0.5f, 0.7f, 0.7f));

    // Batches draw in the order given; a sprite and the debug lines go over them.
    const std::vector<EffectVertex> blue = effectSquare(0.5f, 0, {0, 0, 1, 1});
    EffectBatch both[2];
    both[0].vertices = red;
    both[0].blend = BlendMode::AlphaBlend;
    both[1].vertices = blue;
    both[1].blend = BlendMode::AlphaBlend;
    expectPixel("the later batch is on top", draw(both), mid, mid, {0, 0, 255});
    std::swap(both[0], both[1]);
    expectPixel("and the earlier one under it", draw(both), mid, mid, {255, 0, 0});
    const Sprite cover = solid(mid - 5, mid - 5, 10, 10, {0, 1, 0, 1});
    expectPixel("a sprite is over an effect",
                s.renderer.renderToImage(kSize, kSize, s.frame, {}, {}, std::span(&cover, 1), both), mid, mid, {0, 255, 0}, 0);

    // Whole triangles only; a missing texture skips its batch and not the others; empty input is fine.
    std::vector<EffectVertex> ragged = effectSquare(0.5f, 0, {1, 0, 0, 1});
    ragged.push_back(ragged.front());  // a seventh vertex, no triangle
    EffectBatch odd;
    odd.vertices = ragged;
    odd.blend = BlendMode::AlphaBlend;
    EffectBatch lost = odd;
    lost.texture = 9999;
    const EffectBatch mixed[] = {lost, odd, EffectBatch{}};
    expectPixel("a batch with a missing texture is skipped, the next drawn", draw(mixed), mid, mid, {255, 0, 0});
    check(!draw({}).empty() && !draw(std::span<const EffectBatch>(mixed + 2, 1)).empty(), "no batch, or an empty one, is no trouble");

    // Thousands of quads in a handful of batches: the stream grows and stays right frame after frame.
    std::vector<EffectVertex> many;
    for (int i = 0; i < 4000; ++i) {
        const std::vector<EffectVertex> quad = effectSquare(0.01f, 0, {0, 1, 0, 1});
        for (EffectVertex v : quad) {
            v.position.x += 0.9f * std::sin(static_cast<float>(i));
            v.position.y += 0.9f * std::cos(static_cast<float>(i) * 1.3f);
            many.push_back(v);
        }
    }
    EffectBatch crowd;
    crowd.vertices = many;
    crowd.blend = BlendMode::Additive;
    for (int frame = 0; frame < 3; ++frame) {
        const Image image = draw(one(crowd));
        check(!image.empty() && pixelAt(image, 0, 0).g == clear.g, "4000 quads in one batch, frame " + std::to_string(frame));
    }
    save(draw(one(crowd)), "12c_effect_crowd.png");
}

void testNoGrowth(Scene& s) {
    std::printf("repeated renders do not grow the process\n");
    if (footprintBytes() == 0) {
        std::printf("  SKIP: no way to read the process footprint on this platform\n");
        return;
    }
    MaterialParams glass = unlitColor({1, 0, 0, 0.5f});
    glass.blend = BlendMode::AlphaBlend;
    MaterialParams water = unlitColor({0, 0, 1, 0.5f});
    water.blend = BlendMode::Custom;
    water.srcBlend = BlendFactor::SrcAlpha;
    water.dstBlend = BlendFactor::SrcColor;
    // Hundreds of draws, since each batch of about a hundred needs another uniform buffer.
    const DrawItem layers[] = {item(s.quad, unlitColor({0, 1, 0, 1}), {0, 0, 1}), item(s.quad, glass, {0, 0, 0}),
                               item(s.quad, water, {0, 0, -1})};
    std::vector<DrawItem> items;
    for (int i = 0; i < 600; ++i) {
        items.push_back(layers[i % 3]);
    }
    const DebugLine lines[] = {{{-1, 0, 0}, {1, 0, 0}, {1, 1, 0, 1}}, {{0, -1, 0}, {0, 1, 0}, {1, 1, 0, 1}}};

    // The old submit grew the process by about 4 MB with every call. The first frames are not counted: the
    // first render of a size allocates its targets and the driver warms up. A one-off allocation after that
    // lands in one window of frames, a leak grows every window, so two growing windows fail the test.
    constexpr int kWarmUp = 20, kWindows = 3, kFramesPerWindow = 100;
    constexpr std::size_t kAllowedGrowth = 12u << 20;
    bool rendered = true;
    const auto renderMany = [&](int count) {
        for (int i = 0; i < count; ++i) {
            rendered = rendered && !s.renderer.renderToImage(1024, 768, s.frame, items, lines).empty();
        }
    };
    renderMany(kWarmUp);
    std::size_t previous = footprintBytes();
    int growing = 0;
    for (int window = 0; window < kWindows; ++window) {
        renderMany(kFramesPerWindow);
        const std::size_t now = footprintBytes();
        const std::size_t growth = now > previous ? now - previous : 0;
        std::printf("  %d renders at 1024x768: process grew by %zu KB\n", kFramesPerWindow, growth / 1024);
        growing += growth > kAllowedGrowth;
        previous = now;
    }
    check(rendered, "every 1024x768 render succeeded");
    check(growing < 2, std::to_string(growing) + " of " + std::to_string(kWindows) +
                           " windows grew the process by more than " + std::to_string(kAllowedGrowth / 1024) +
                           " KB (a leak grows every window)");
}

void testUpdateMesh(Scene& s) {
    std::printf("a mesh that changes shape\n");
    const Vec3 toward{0, 0, -1};
    const MeshHandle mesh = s.makeQuad(toward);
    const DrawItem items[] = {item(mesh, unlitColor({1, 0, 0, 1}), {0, 0, 0})};
    const auto top = s.project({0, 0.5f, 0}), bottom = s.project({0, -0.5f, 0});
    const Image whole = s.render(items);
    expectPixel("the quad as it was made: top", whole, top.x, top.y, {255, 0, 0});
    expectPixel("the quad as it was made: bottom", whole, bottom.x, bottom.y, {255, 0, 0});

    // The same four corners, the top two brought down to the middle: only the lower half is left.
    const Vertex lower[] = {{{-1, 0, 0}, toward, {0, 0}}, {{1, 0, 0}, toward, {1, 0}}, {{1, -1, 0}, toward, {1, 1}}, {{-1, -1, 0}, toward, {0, 1}}};
    s.renderer.updateMesh(mesh, lower);
    const Image half = s.render(items);
    save(half, "11_update_mesh.png");
    expectPixel("rewritten: the top is gone", half, top.x, top.y, rgb8(0.2f, 0.4f, 0.6f));
    expectPixel("rewritten: the bottom is there", half, bottom.x, bottom.y, {255, 0, 0});

    // What is not a rewrite changes nothing.
    s.renderer.updateMesh(mesh, std::span<const Vertex>(lower, 3));  // three vertices of four
    s.renderer.updateMesh(mesh + 1000, lower);                                              // a handle that is none
    const Image same = s.render(items);
    expectPixel("a rewrite with the wrong number of vertices is refused", same, top.x, top.y, rgb8(0.2f, 0.4f, 0.6f));

    // A frame's worth of rewrites after another, as a sail has them, with a frame between.
    for (int frame = 0; frame < 60; ++frame) {
        Vertex slid[4];
        std::copy(std::begin(lower), std::end(lower), slid);
        for (Vertex& v : slid) {
            v.position.x += 0.01f * static_cast<float>(frame);
        }
        s.renderer.updateMesh(mesh, slid);
        (void)s.render(items);
    }
    const Image last = s.render(items);
    const auto left = s.project({-0.9f, -0.5f, 0}), right = s.project({1.2f, -0.5f, 0});
    expectPixel("sixty rewrites on: the quad is where the last one put it (its left edge)", last, left.x, left.y, rgb8(0.2f, 0.4f, 0.6f));
    expectPixel("sixty rewrites on: (and its right)", last, right.x, right.y, {255, 0, 0});
    s.renderer.destroyMesh(mesh);
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
    testBlendFactors(scene);
    testLighting(scene);
    testDebugLines(scene);
    testAlphaTest(scene);
    testSpritePlacement(scene);
    testSpriteTexture(scene);
    testSpriteOrder(scene);
    testSpriteRotation(scene);
    testSpriteEdgeCases(scene);
    testEffects(scene);
    testMipmaps(scene);
    testNoGrowth(scene);
    testUpdateMesh(scene);
    testHandles(scene);

    renderer.reset();
    std::printf(failures ? "%d check(s) FAILED\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
