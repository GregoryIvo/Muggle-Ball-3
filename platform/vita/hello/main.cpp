// The M1a hello for the PlayStation Vita: the first thing the console runs, built to answer the questions in
// docs/VITA_PLAN.md (sections 4.5, 5.4b, 5.8, 5.9 and 6 "M1a") before any game code is ported. It
//   - sets the heap and stack sizes, the clocks, and logs to ux0:data/MuggleBall3/log.txt and sceClibPrintf;
//   - reads the enter-button setting (Cross or Circle confirms) and opens the SDL window;
//   - brings up vitaGL without MSAA, clears to the game's clear colour and draws one lit triangle with the
//     mesh shader in GLSL ES 1.00, through a D3D-style projection remapped to GL's clip z;
//   - presents at a two-vblank swap interval and records the presented intervals and the work time;
//   - tries the IME (Triangle opens it), SDL's GPU API (expected to fail), the audio device's formats;
//   - steps a small Jolt world and times it;
//   - runs the runtime probe: the file system on app0: and ux0:, random_device, threads, clocks, the heap;
//   - exits on the back button, writing the probe to probe.txt and the statistics to the log.
// Nothing here is shared with the game: the engine and the game are ported in later milestones.

#include <SDL3/SDL.h>
#include <vitaGL.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/display.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/system_param.h>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <malloc.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

// Newlib's heap is one fixed block taken at start-up; threads' stacks come from outside it. Both sizes and the
// default pthread stack are set here, before anything runs (the defaults are 128 MiB for the heap and 32 KiB for
// a pthread, which Jolt's workers and the probe's thread would overflow). The game's values are decided in M6b.
extern "C" {
unsigned int _newlib_heap_size_user = 160 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 1024 * 1024;
unsigned int _pthread_stack_default_user = 512 * 1024;
}

namespace {

namespace fs = std::filesystem;

constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;
constexpr int kSwapInterval = 2;                       // 30 fps: every second vblank
constexpr int kVitaGlRamThreshold = 16 * 1024 * 1024;  // RAM vitaGL leaves for everyone else (thread stacks, SDL, the IME)
constexpr const char* kDataDir = "ux0:data/MuggleBall3";
constexpr const char* kLogPath = "ux0:data/MuggleBall3/log.txt";
constexpr const char* kProbePath = "ux0:data/MuggleBall3/probe.txt";
constexpr const char* kShaderCompilerPaths[] = {"ur0:data/libshacccg.suprx", "ur0:data/external/libshacccg.suprx"};
constexpr float kClearColor[3] = {0.05f, 0.05f, 0.1f};

// SDL's Vita joystick is positional, with the d-pad as four buttons and no hat: these are the 16 indices of the
// SDK's SDL 3.4.16 (src/joystick/vita/SDL_sysjoystick.c, ext_button_map; its built-in "PSVita Controller" gamepad
// mapping says the same). L2, R2, L3 and R3 exist on a PSTV's controller only.
enum VitaButton : int {
    kTriangle = 0, kCircle = 1, kCross = 2, kSquare = 3, kL1 = 4, kR1 = 5,
    kDpadDown = 6, kDpadLeft = 7, kDpadUp = 8, kDpadRight = 9, kSelect = 10, kStart = 11,
    kL2 = 12, kR2 = 13, kL3 = 14, kR3 = 15,
};

// ---------------------------------------------------------------- logging

FILE* g_log = nullptr;

void logLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logLine(const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    sceClibPrintf("[mb3] %s\n", line);
    if (g_log) {
        std::fprintf(g_log, "%s\n", line);
        std::fflush(g_log);
    }
}

void openLog() {
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(kDataDir, 0777);
    g_log = std::fopen(kLogPath, "w");
    logLine("Muggle Ball 3 Vita hello (M1a). SDL %d.%d.%d compiled, %d.%d.%d linked", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
         SDL_MICRO_VERSION, SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
         SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
}

// ---------------------------------------------------------------- maths (the engine's conventions: row-major, row vectors, D3D left-handed)

struct Vec3 {
    float x, y, z;
};
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 v) {
    const float len = std::sqrt(dot(v, v));
    return {v.x / len, v.y / len, v.z / len};
}

struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
};

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    }
    return r;
}

// D3DXMatrixLookAtLH.
Mat4 lookAtLH(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 z = normalize(target - eye);
    const Vec3 x = normalize(cross(up, z));
    const Vec3 y = cross(z, x);
    Mat4 r;
    r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x; r.m[0][3] = 0;
    r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y; r.m[1][3] = 0;
    r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z; r.m[2][3] = 0;
    r.m[3][0] = -dot(x, eye); r.m[3][1] = -dot(y, eye); r.m[3][2] = -dot(z, eye); r.m[3][3] = 1;
    return r;
}

// D3DXMatrixPerspectiveFovLH: clip z in 0..1.
Mat4 perspectiveFovLH(float fovY, float aspect, float zNear, float zFar) {
    const float yScale = 1.0f / std::tan(fovY * 0.5f);
    Mat4 r;
    r.m[0][0] = yScale / aspect;
    r.m[1][1] = yScale;
    r.m[2][2] = zFar / (zFar - zNear);
    r.m[2][3] = 1;
    r.m[3][2] = -zNear * zFar / (zFar - zNear);
    r.m[3][3] = 0;
    return r;
}

// GL clips z in -w..w where D3D clips in 0..w: z' = 2z - w, applied after the projection (row vectors: on
// the right). This is the one place the renderer will do it too (docs/VITA_PLAN.md 4.3).
Mat4 d3dToGlClip() {
    Mat4 r;
    r.m[2][2] = 2;
    r.m[3][2] = -1;
    return r;
}

// ---------------------------------------------------------------- the mesh shader in GLSL ES 1.00

const char* const kMeshVertexShader = R"GLSL(
attribute vec3 inPosition;
attribute vec3 inNormal;
attribute vec2 inUv;

// Each matrix is a row-major, row-vector matrix uploaded unchanged: GLSL reads it column-major as the
// transpose, which is the column-vector form, so `matrix * vector` computes the CPU's `vector * Matrix`.
uniform mat4 viewProj;
uniform mat4 world;
uniform mat4 normalMatrix;

varying vec3 vNormal;
varying vec2 vUv;

void main() {
    gl_Position = viewProj * (world * vec4(inPosition, 1.0));
    vNormal = mat3(normalMatrix) * inNormal;
    vUv = inUv;
}
)GLSL";

const char* const kMeshFragmentShader = R"GLSL(
precision mediump float;

varying vec3 vNormal;
varying vec2 vUv;

uniform sampler2D baseTexture;
uniform vec4 diffuse;
uniform vec4 ambient;
uniform vec4 emissive;
uniform vec4 lightDirection;  // xyz: the direction the light travels, unit length
uniform vec4 lightDiffuse;
uniform vec4 lightAmbient;
uniform vec4 flags;           // x: 1 = lit, y: 1 = alpha test

// Direct3D 8 fixed-function lighting for one directional light, per pixel.
void main() {
    vec4 texel = texture2D(baseTexture, vUv);
    float alpha = texel.a * diffuse.a;
    if (flags.y > 0.5 && alpha < 0.5) {
        discard;
    }
    vec3 rgb;
    if (flags.x > 0.5) {
        float facing = max(dot(normalize(vNormal), -lightDirection.xyz), 0.0);
        vec3 light = emissive.rgb + ambient.rgb * lightAmbient.rgb + diffuse.rgb * lightDiffuse.rgb * facing;
        rgb = texel.rgb * clamp(light, 0.0, 1.0);
    } else {
        rgb = texel.rgb * diffuse.rgb;
    }
    gl_FragColor = vec4(rgb, alpha);
}
)GLSL";

GLuint compileShader(GLenum type, const char* source, const char* name) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    char info[1024] = {};
    GLsizei length = 0;
    glGetShaderInfoLog(shader, sizeof info - 1, &length, info);
    logLine("shader %s: compile status %d%s%s", name, ok, length ? ", log: " : "", length ? info : "");
    return shader;
}

struct MeshProgram {
    GLuint program = 0;
    GLint inPosition = -1, inNormal = -1, inUv = -1;
    GLint viewProj = -1, world = -1, normalMatrix = -1;
    GLint baseTexture = -1, diffuse = -1, ambient = -1, emissive = -1, lightDirection = -1, lightDiffuse = -1,
          lightAmbient = -1, flags = -1;
    bool ok = false;
};

MeshProgram buildMeshProgram() {
    MeshProgram p;
    const std::uint64_t t0 = sceKernelGetProcessTimeWide();
    const GLuint vs = compileShader(GL_VERTEX_SHADER, kMeshVertexShader, "mesh.vert");
    const GLuint fsh = compileShader(GL_FRAGMENT_SHADER, kMeshFragmentShader, "mesh.frag");
    p.program = glCreateProgram();
    glAttachShader(p.program, vs);
    glAttachShader(p.program, fsh);
    // vitaGL binds attributes by location as told; the engine will do the same.
    glBindAttribLocation(p.program, 0, "inPosition");
    glBindAttribLocation(p.program, 1, "inNormal");
    glBindAttribLocation(p.program, 2, "inUv");
    glLinkProgram(p.program);
    GLint linked = GL_FALSE;
    glGetProgramiv(p.program, GL_LINK_STATUS, &linked);
    const std::uint64_t t1 = sceKernelGetProcessTimeWide();
    logLine("mesh program: link status %d, compile+link %.1f ms", linked, (t1 - t0) / 1000.0);
    p.inPosition = glGetAttribLocation(p.program, "inPosition");
    p.inNormal = glGetAttribLocation(p.program, "inNormal");
    p.inUv = glGetAttribLocation(p.program, "inUv");
    p.viewProj = glGetUniformLocation(p.program, "viewProj");
    p.world = glGetUniformLocation(p.program, "world");
    p.normalMatrix = glGetUniformLocation(p.program, "normalMatrix");
    p.baseTexture = glGetUniformLocation(p.program, "baseTexture");
    p.diffuse = glGetUniformLocation(p.program, "diffuse");
    p.ambient = glGetUniformLocation(p.program, "ambient");
    p.emissive = glGetUniformLocation(p.program, "emissive");
    p.lightDirection = glGetUniformLocation(p.program, "lightDirection");
    p.lightDiffuse = glGetUniformLocation(p.program, "lightDiffuse");
    p.lightAmbient = glGetUniformLocation(p.program, "lightAmbient");
    p.flags = glGetUniformLocation(p.program, "flags");
    logLine("mesh program: attributes %d %d %d, uniforms viewProj %d world %d normal %d tex %d diffuse %d flags %d",
         p.inPosition, p.inNormal, p.inUv, p.viewProj, p.world, p.normalMatrix, p.baseTexture, p.diffuse, p.flags);
    p.ok = linked == GL_TRUE && p.inPosition >= 0 && p.viewProj >= 0;
    return p;
}

// ---------------------------------------------------------------- the Jolt spike

namespace jolt {

// One object layer and one broad-phase layer: enough to see Jolt run on the console.
class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 1; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override { return JPH::BroadPhaseLayer(0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "all"; }
#endif
};
class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
};
class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override { return true; }
};

// Drops 50 spheres on a box floor for 300 steps of 1/60 s (five seconds), timing the steps. Returns the height
// of the first sphere at the end: at rest on the floor it is about its radius.
void run() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    {
        JPH::TempAllocatorImpl temp(4 * 1024 * 1024);
        JPH::JobSystemSingleThreaded jobs(JPH::cMaxPhysicsJobs);
        BroadPhaseLayers broadPhaseLayers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairs objectPairs;
        JPH::PhysicsSystem physics;
        // One body mutex: nothing but this thread touches the world, which is also the Vita profile's setting.
        physics.Init(1024, 1, 4096, 2048, broadPhaseLayers, objectVsBroadPhase, objectPairs);
        physics.SetGravity(JPH::Vec3(0, -9.8f, 0));
        JPH::BodyInterface& bodies = physics.GetBodyInterface();

        JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(20, 0.5f, 20)), JPH::RVec3(0, -0.5f, 0),
                                        JPH::Quat::sIdentity(), JPH::EMotionType::Static, 0);
        bodies.CreateAndAddBody(floor, JPH::EActivation::DontActivate);
        JPH::BodyID first;
        for (int i = 0; i < 50; ++i) {
            JPH::BodyCreationSettings sphere(new JPH::SphereShape(0.5f), JPH::RVec3(float(i % 5) * 1.2f - 2.4f, 2.0f + float(i / 5) * 1.1f, float(i % 7) * 0.9f - 2.7f),
                                             JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, 0);
            const JPH::BodyID id = bodies.CreateAndAddBody(sphere, JPH::EActivation::Activate);
            if (i == 0) {
                first = id;
            }
        }
        physics.OptimizeBroadPhase();

        constexpr int kSteps = 300;
        std::vector<double> stepMs;
        stepMs.reserve(kSteps);
        for (int step = 0; step < kSteps; ++step) {
            const std::uint64_t t0 = sceKernelGetProcessTimeWide();
            physics.Update(1.0f / 60.0f, 1, &temp, &jobs);
            stepMs.push_back((sceKernelGetProcessTimeWide() - t0) / 1000.0);
        }
        std::sort(stepMs.begin(), stepMs.end());
        double sum = 0;
        for (const double ms : stepMs) {
            sum += ms;
        }
        const JPH::RVec3 pos = bodies.GetPosition(first);
        logLine("jolt: 50 spheres on a floor, 300 steps of 1/60 s: mean %.3f ms, p50 %.3f, p95 %.3f, max %.3f; first sphere "
             "at (%.2f, %.2f, %.2f), %u bodies active",
             sum / kSteps, stepMs[kSteps / 2], stepMs[kSteps * 95 / 100], stepMs.back(), double(pos.GetX()),
             double(pos.GetY()), double(pos.GetZ()), physics.GetNumActiveBodies(JPH::EBodyType::RigidBody));
    }
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

}  // namespace jolt

// ---------------------------------------------------------------- the runtime probe (docs/VITA_PLAN.md 5.4b)

struct Probe {
    std::string text;
    void line(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};

void Probe::line(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    text += buf;
    text += '\n';
    sceClibPrintf("[probe] %s\n", buf);
}

void probeFilesystem(Probe& p) {
    std::error_code ec;
    std::string err;
    // How std::filesystem parses the console's drive-letter-like paths; the code uses parent_path() and
    // create_directories() on them.
    for (const char* raw : {"ux0:data/MuggleBall3/settings.json", "ux0:/data/MuggleBall3/settings.json", "app0:/assets"}) {
        const fs::path path(raw);
        p.line("path %s: root_name '%s' root_dir '%s' parent '%s' filename '%s' absolute %d", raw,
               path.root_name().string().c_str(), path.root_directory().string().c_str(), path.parent_path().string().c_str(),
               path.filename().string().c_str(), int(path.is_absolute()));
    }
    // Explicit increment(ec): the range-for's operator++ throws on a read error, which would end the whole probe.
    int count = 0;
    for (auto it = fs::directory_iterator("app0:/", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (count++ < 12) {
            std::error_code typeError;
            const bool isDir = it->is_directory(typeError);
            p.line("app0:/ entry: %s%s", it->path().filename().string().c_str(), isDir ? "/" : "");
        }
    }
    err = ec.message();
    p.line("directory_iterator(app0:/): %d entries, error '%s'", count, err.c_str());
    ec.clear();
    count = 0;
    for (auto it = fs::recursive_directory_iterator("app0:/sce_sys", ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        ++count;
    }
    err = ec.message();
    p.line("recursive_directory_iterator(app0:/sce_sys): %d entries, error '%s'", count, err.c_str());

    for (const char* dir : {"ux0:data/MuggleBall3/probe/a/b", "ux0:/data/MuggleBall3/probe2/a"}) {
        ec.clear();
        const bool made = fs::create_directories(dir, ec);
        err = ec.message();  // read before the next call clears or changes ec
        const bool exists = fs::exists(dir, ec);
        p.line("create_directories(%s): returned %d, error '%s', exists %d", dir, int(made), err.c_str(), int(exists));
    }
    const fs::path dir = "ux0:data/MuggleBall3/probe";
    const fs::path a = dir / "a.txt", b = dir / "b.txt";
    auto write = [&](const fs::path& path, const char* contents) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << contents;
        return bool(out);
    };
    p.line("write a.txt %d, write b.txt %d", int(write(a, "new")), int(write(b, "old")));
    ec.clear();
    fs::rename(a, b, ec);  // over an existing file: what writeFileAtomic relies on
    std::string got;
    {
        std::ifstream in(b, std::ios::binary);
        std::getline(in, got);
    }
    err = ec.message();
    const bool aLeft = fs::exists(a, ec);
    p.line("fs::rename over an existing file: error '%s', b.txt now '%s' (want 'new'), a.txt exists %d", err.c_str(), got.c_str(),
           int(aLeft));
    write(a, "new2");
    const int renamed = std::rename(a.string().c_str(), b.string().c_str());
    p.line("::rename over an existing file: %d (errno %d)", renamed, renamed ? errno : 0);
    write(a, "new3");
    const int sceRenamed = sceIoRename(a.string().c_str(), b.string().c_str());
    p.line("sceIoRename over an existing file: 0x%08x", unsigned(sceRenamed));
    ec.clear();
    const auto size = static_cast<unsigned long long>(fs::file_size(b, ec));
    err = ec.message();
    p.line("file_size(b.txt) %llu error '%s'", size, err.c_str());
    ec.clear();
    const bool removed = fs::remove(b, ec);
    err = ec.message();
    const bool bLeft = fs::exists(b, ec);
    p.line("fs::remove(b.txt): %d, error '%s', exists after %d", int(removed), err.c_str(), int(bLeft));
    ec.clear();
    const fs::path cwd = fs::current_path(ec);
    err = ec.message();
    p.line("current_path: '%s', error '%s'", cwd.string().c_str(), err.c_str());
    ec.clear();
    fs::remove_all("ux0:data/MuggleBall3/probe", ec);
    fs::remove_all("ux0:/data/MuggleBall3/probe2", ec);
    err = ec.message();
    p.line("remove_all: error '%s'", err.c_str());
    for (const char* path : kShaderCompilerPaths) {
        SceIoStat stat{};
        const int rc = sceIoGetstat(path, &stat);
        p.line("%s: %s (0x%08x, %llu bytes)", path, rc >= 0 ? "present" : "MISSING", unsigned(rc), (unsigned long long)stat.st_size);
    }
}

void probeRuntime(Probe& p) {
    try {
        std::random_device rd;
        const unsigned a = rd(), b = rd();
        p.line("random_device: ok, %u %u, entropy %.1f", a, b, double(rd.entropy()));
    } catch (const std::exception& e) {
        p.line("random_device: THROWS (%s)", e.what());
    }
    p.line("hardware_concurrency: %u", std::thread::hardware_concurrency());
    p.line("main thread: id 0x%08x, affinity mask 0x%x", unsigned(sceKernelGetThreadId()),
           unsigned(sceKernelGetThreadCpuAffinityMask(sceKernelGetThreadId())));
    std::atomic<int> result{0};
    std::string threadReport;
    try {
        std::thread worker([&] {
            const SceUID id = sceKernelGetThreadId();
            const int before = sceKernelGetThreadCpuAffinityMask(id);
            const int set = sceKernelChangeThreadCpuAffinityMask(id, SCE_KERNEL_CPU_MASK_USER_1);
            const int after = sceKernelGetThreadCpuAffinityMask(id);
            // A 200 KiB array on the stack: fine with _pthread_stack_default_user at 512 KiB, a crash with the 4 KiB
            // default.
            volatile char big[200 * 1024];
            big[0] = 1;
            big[sizeof big - 1] = 2;
            char buf[256];
            std::snprintf(buf, sizeof buf, "worker thread: id 0x%08x, affinity before 0x%x, set rc 0x%08x, after 0x%x, 200 KiB stack ok (%d)",
                          unsigned(id), unsigned(before), unsigned(set), unsigned(after), big[0] + big[sizeof big - 1]);
            threadReport = buf;
            result = 1;
        });
        worker.join();
        p.line("%s", threadReport.c_str());
    } catch (const std::exception& e) {
        p.line("std::thread: THROWS (%s)", e.what());
    }
    // Clock resolution: the smallest non-zero difference between successive readings.
    using clock = std::chrono::steady_clock;
    long long best = -1;
    auto last = clock::now();
    for (int i = 0; i < 20000; ++i) {
        const auto now = clock::now();
        const long long d = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count();
        if (d > 0 && (best < 0 || d < best)) {
            best = d;
        }
        last = now;
    }
    p.line("steady_clock: smallest step %lld ns, period %lld/%lld", best, (long long)clock::period::num, (long long)clock::period::den);
    const std::uint64_t k0 = sceKernelGetProcessTimeWide();
    const auto s0 = clock::now();
    sceKernelDelayThread(50 * 1000);
    const double kernelMs = (sceKernelGetProcessTimeWide() - k0) / 1000.0;
    const double steadyMs = std::chrono::duration<double, std::milli>(clock::now() - s0).count();
    p.line("50 ms delay: process time %.2f ms, steady_clock %.2f ms", kernelMs, steadyMs);
    const struct mallinfo mi = mallinfo();
    p.line("mallinfo: arena %zu, in use %zu, free %zu (heap size set to %u)", static_cast<std::size_t>(mi.arena),
           static_cast<std::size_t>(mi.uordblks), static_cast<std::size_t>(mi.fordblks), _newlib_heap_size_user);
    p.line("SDL_GetBasePath: '%s'", SDL_GetBasePath() ? SDL_GetBasePath() : "(null)");
    char* pref = SDL_GetPrefPath("", "MuggleBall3");
    p.line("SDL_GetPrefPath(\"\", \"MuggleBall3\"): '%s'", pref ? pref : "(null)");
    SDL_free(pref);
}

void probeAudio(Probe& p) {
    for (const auto [freq, format] : {std::pair{44100, SDL_AUDIO_S16}, std::pair{48000, SDL_AUDIO_S16}, std::pair{44100, SDL_AUDIO_F32}}) {
        SDL_AudioSpec want{};
        want.freq = freq;
        want.format = format;
        want.channels = 2;
        SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, nullptr, nullptr);
        if (!stream) {
            p.line("audio %d Hz %s: open failed: %s", freq, SDL_GetAudioFormatName(format), SDL_GetError());
            continue;
        }
        SDL_AudioSpec got{};
        int frames = 0;
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &got, &frames);
        p.line("audio %d Hz %s: device runs %d Hz %s %d ch, %d frames per buffer", freq, SDL_GetAudioFormatName(format), got.freq,
               SDL_GetAudioFormatName(got.format), got.channels, frames);
        SDL_DestroyAudioStream(stream);
    }
}

void probeGpuApi(Probe& p) {
    // The SDK's SDL3 exports the GPU API with no back end behind it; the engine's SDL GPU renderer therefore
    // links but cannot run, which is what the renderer seam is for.
    p.line("SDL_GetNumGPUDrivers: %d", SDL_GetNumGPUDrivers());
    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    p.line("SDL_CreateGPUDevice: %s%s", device ? "UNEXPECTEDLY SUCCEEDED" : "failed as expected: ", device ? "" : SDL_GetError());
    if (device) {
        SDL_DestroyGPUDevice(device);
    }
}

void writeProbe(const Probe& p) {
    if (FILE* f = std::fopen(kProbePath, "w")) {
        std::fwrite(p.text.data(), 1, p.text.size(), f);
        std::fclose(f);
        logLine("probe written to %s", kProbePath);
    } else {
        logLine("cannot write %s (errno %d)", kProbePath, errno);
    }
    // The log carries a copy, so one file holds all the evidence.
    if (g_log) {
        std::fprintf(g_log, "---- probe ----\n%s---- end of probe ----\n", p.text.c_str());
        std::fflush(g_log);
    }
}

// ---------------------------------------------------------------- frame statistics (docs/VITA_PLAN.md section 1, item 2)

struct FrameStats {
    std::array<std::uint32_t, 9> intervals{};  // presented interval in vblanks, 0..7 and 8+
    std::vector<double> workMs;
    int lastVcount = -1;

    void frame(double work, int vcount) {
        workMs.push_back(work);
        if (lastVcount >= 0) {
            const int delta = std::max(0, vcount - lastVcount);
            ++intervals[std::min<int>(delta, 8)];
        }
        lastVcount = vcount;
    }

    void report(const char* when, int swapInterval) {
        if (workMs.empty()) {
            return;
        }
        std::vector<double> sorted = workMs;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (const double ms : sorted) {
            sum += ms;
        }
        std::uint32_t frames = 0;
        for (const std::uint32_t n : intervals) {
            frames += n;
        }
        logLine("%s: %zu frames; work time mean %.2f ms, p95 %.2f, max %.2f; presented intervals (vblanks): 1:%u 2:%u 3:%u 4:%u 5+:%u; at %d "
             "vblanks: %.1f%%",
             when, workMs.size(), sum / sorted.size(), sorted[sorted.size() * 95 / 100], sorted.back(), intervals[1], intervals[2],
             intervals[3], intervals[4], intervals[5] + intervals[6] + intervals[7] + intervals[8], swapInterval,
             frames ? 100.0 * intervals[std::clamp(swapInterval, 0, 8)] / frames : 0.0);
    }
};

// ---------------------------------------------------------------- main

struct ClockReport {
    int arm, bus, gpu, xbar;
};

ClockReport setClocks() {
    const int rcArm = scePowerSetArmClockFrequency(444);
    const int rcBus = scePowerSetBusClockFrequency(222);
    const int rcGpu = scePowerSetGpuClockFrequency(222);
    const int rcXbar = scePowerSetGpuXbarClockFrequency(166);
    logLine("clocks: set rc 0x%08x 0x%08x 0x%08x 0x%08x", unsigned(rcArm), unsigned(rcBus), unsigned(rcGpu), unsigned(rcXbar));
    const ClockReport r{scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(),
                        scePowerGetGpuXbarClockFrequency()};
    logLine("clocks: arm %d MHz, bus %d, gpu %d, xbar %d", r.arm, r.bus, r.gpu, r.xbar);
    return r;
}

bool readEnterButtonIsCircle() {
    SceAppUtilInitParam init{};
    SceAppUtilBootParam boot{};
    const int rcInit = sceAppUtilInit(&init, &boot);
    int enter = -1;
    const int rcGet = sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enter);
    const bool circle = enter == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;
    logLine("enter button: sceAppUtilInit 0x%08x, get 0x%08x, value %d -> %s confirms", unsigned(rcInit), unsigned(rcGet), enter,
         circle ? "Circle" : "Cross");
    return circle;
}

void logMemory(const char* when) {
    logLine("%s: vitaGL free/total: VRAM %zu/%zu KiB, RAM %zu/%zu KiB, PHYCONT %zu/%zu KiB, CDLG %zu/%zu KiB", when,
         vglMemFree(VGL_MEM_VRAM) / 1024, vglMemTotal(VGL_MEM_VRAM) / 1024, vglMemFree(VGL_MEM_RAM) / 1024,
         vglMemTotal(VGL_MEM_RAM) / 1024, vglMemFree(VGL_MEM_PHYCONT) / 1024, vglMemTotal(VGL_MEM_PHYCONT) / 1024,
         vglMemFree(VGL_MEM_BUDGET) / 1024, vglMemTotal(VGL_MEM_BUDGET) / 1024);
}

int run() {
    openLog();
    setClocks();
    logLine("heap %u MiB, main stack %u KiB, pthread default stack %u KiB", _newlib_heap_size_user >> 20, sceUserMainThreadStackSize >> 10,
         _pthread_stack_default_user >> 10);

    // Touch is read as touch (never as a mouse, which would press the button wherever a finger lands) and the
    // rear pad is off, as the game will have it.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_VITA_ENABLE_BACK_TOUCH, "0");
    // SDL shows the IME for text input only if it sees no physical keyboard ("auto"); a PSTV with a USB keyboard
    // would otherwise get nothing on Triangle.
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        logLine("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    logLine("SDL video driver '%s', audio driver '%s'", SDL_GetCurrentVideoDriver(), SDL_GetCurrentAudioDriver());
    const bool circleConfirms = readEnterButtonIsCircle();
    const int confirmButton = circleConfirms ? kCircle : kCross;
    const int backButton = circleConfirms ? kCross : kCircle;

    // vitaGL owns the display; SDL's window is for events only (its video driver touches no GXM).
    const std::uint64_t glStart = sceKernelGetProcessTimeWide();
    // The return value is not success: vitaGL returns GL_TRUE only when it had to clamp the resolution (vgl.c,
    // vglInitWithCustomSizes), GL_FALSE on a normal start. A failed start shows as null GL strings and GL errors.
    const GLboolean clamped = vglInitExtended(0, kScreenWidth, kScreenHeight, kVitaGlRamThreshold, SCE_GXM_MULTISAMPLE_NONE);
    const auto glString = [](GLenum name) {
        const char* value = reinterpret_cast<const char*>(glGetString(name));
        return value ? value : "(null)";
    };
    logLine("vglInitExtended(0, %d, %d, %d MiB, no MSAA): %.1f ms, returned %d (1 = resolution clamped); GL_VERSION '%s', GL_RENDERER '%s', "
            "GLSL '%s'",
            kScreenWidth, kScreenHeight, kVitaGlRamThreshold >> 20, (sceKernelGetProcessTimeWide() - glStart) / 1000.0, int(clamped),
            glString(GL_VERSION), glString(GL_RENDERER), glString(GL_SHADING_LANGUAGE_VERSION));
    logMemory("after vglInit");
    eglSwapInterval(EGL_NO_DISPLAY, kSwapInterval);

    SDL_Window* window = SDL_CreateWindow("Muggle Ball 3", kScreenWidth, kScreenHeight, 0);
    logLine("SDL_CreateWindow: %s%s", window ? "ok" : "FAILED: ", window ? "" : SDL_GetError());
    SDL_Joystick* joystick = nullptr;
    int joystickCount = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&joystickCount)) {
        if (joystickCount > 0) {
            joystick = SDL_OpenJoystick(ids[0]);
            logLine("joystick 0: '%s', %d buttons, %d axes, %d hats; a gamepad mapping %s", joystick ? SDL_GetJoystickName(joystick) : "?",
                 joystick ? SDL_GetNumJoystickButtons(joystick) : 0, joystick ? SDL_GetNumJoystickAxes(joystick) : 0,
                 joystick ? SDL_GetNumJoystickHats(joystick) : 0, SDL_IsGamepad(ids[0]) ? "exists" : "does NOT exist");
        }
        SDL_free(ids);
    }
    logLine("%d joystick(s), %d touch device(s)", joystickCount, [] { int n = 0; SDL_free(SDL_GetTouchDevices(&n)); return n; }());

    Probe probe;
    const auto section = [&](const char* name, void (*fn)(Probe&)) {
        try {
            fn(probe);
        } catch (const std::exception& e) {
            probe.line("%s: exception %s", name, e.what());
        }
    };
    section("gpu api", probeGpuApi);
    section("filesystem", probeFilesystem);
    section("runtime", probeRuntime);
    section("audio", probeAudio);
    writeProbe(probe);

    try {
        jolt::run();
    } catch (const std::exception& e) {
        logLine("jolt: exception %s", e.what());
    }

    // The scene: a camera at z = -3 looking at the origin, a lit triangle in the z = 0 plane facing it, clockwise
    // on screen, as the engine's render tests draw their quad.
    const MeshProgram program = buildMeshProgram();
    const float vertices[] = {
        // position           normal        uv
        -1.0f, -1.0f, 0.0f,   0, 0, -1,     0, 1,
         0.0f,  1.0f, 0.0f,   0, 0, -1,     0.5f, 0,
         1.0f, -1.0f, 0.0f,   0, 0, -1,     1, 1,
    };
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
    GLuint white = 0;
    glGenTextures(1, &white);
    glBindTexture(GL_TEXTURE_2D, white);
    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    logLine("buffers and texture: GL error 0x%x", glGetError());

    const Mat4 view = lookAtLH({0, 0, -3}, {0, 0, 0}, {0, 1, 0});
    const Mat4 projection = perspectiveFovLH(60.0f * 3.14159265f / 180.0f, float(kScreenWidth) / kScreenHeight, 1.0f, 500.0f);
    const Mat4 viewProj = view * projection * d3dToGlClip();
    const Mat4 world;  // identity, so the normal matrix is too
    const Vec3 lightDir = normalize({0.0f, -0.66f, 0.75f});

    glViewport(0, 0, kScreenWidth, kScreenHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    FrameStats stats;
    bool imeWasShown = false;
    std::string imeText;
    int touchLogged = 0;
    int frames = 0;
    bool running = true;
    float spin = 0;
    int swapInterval = kSwapInterval;
    logLine("entering the frame loop: %s confirms, %s exits, Triangle opens the IME, Start toggles the swap interval, Select writes the "
            "stats, the d-pad is logged",
            circleConfirms ? "Circle" : "Cross", circleConfirms ? "Cross" : "Circle");
    while (running) {
        const std::uint64_t frameStart = sceKernelGetProcessTimeWide();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_JOYSTICK_BUTTON_DOWN: {
                    const int button = event.jbutton.button;
                    logLine("button %d down", button);
                    if (button == backButton) {
                        running = false;
                    } else if (button == confirmButton) {
                        logLine("confirm pressed");
                    } else if (button == kTriangle) {
                        logLine("opening the IME: SDL_StartTextInput -> %d", int(SDL_StartTextInput(window)));
                    } else if (button == kStart) {
                        swapInterval = swapInterval == 2 ? 1 : 2;
                        eglSwapInterval(EGL_NO_DISPLAY, swapInterval);
                        stats.report("before changing the swap interval", swapInterval == 2 ? 1 : 2);
                        stats = FrameStats{};
                        logLine("swap interval now %d", swapInterval);
                    } else if (button == kSelect) {
                        stats.report("on Select", swapInterval);
                        logMemory("on Select");
                    } else if (button >= kDpadDown && button <= kDpadRight) {
                        logLine("d-pad %s", button == kDpadDown ? "down" : button == kDpadLeft ? "left" : button == kDpadUp ? "up" : "right");
                    }
                    break;
                }
                case SDL_EVENT_TEXT_INPUT:
                    imeText += event.text.text;
                    logLine("TEXT_INPUT '%s'", event.text.text);
                    break;
                case SDL_EVENT_KEY_DOWN:
                    logLine("KEY_DOWN scancode %d key %u", int(event.key.scancode), unsigned(event.key.key));
                    break;
                case SDL_EVENT_FINGER_DOWN:
                case SDL_EVENT_FINGER_UP:
                case SDL_EVENT_FINGER_MOTION:
                    if (touchLogged < 30) {
                        ++touchLogged;
                        logLine("finger %s: touch %llu finger %llu at (%.3f, %.3f) pressure %.2f",
                             event.type == SDL_EVENT_FINGER_DOWN ? "down" : event.type == SDL_EVENT_FINGER_UP ? "up" : "motion",
                             (unsigned long long)event.tfinger.touchID, (unsigned long long)event.tfinger.fingerID, event.tfinger.x,
                             event.tfinger.y, event.tfinger.pressure);
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    logLine("MOUSE_BUTTON_DOWN (touch-to-mouse synthesis is supposed to be off)");
                    break;
                default:
                    break;
            }
        }
        // SDL 3.4.16's Vita driver sends the dialog's text whenever it closes and a Return only when it was confirmed,
        // so a cancel after typing may still deliver text: the log shows both, the device run settles it.
        const bool imeShown = SDL_ScreenKeyboardShown(window);
        if (imeWasShown && !imeShown) {
            logLine("IME closed; text received: '%s' (%zu bytes)", imeText.c_str(), imeText.size());
            imeText.clear();
            SDL_StopTextInput(window);
        }
        imeWasShown = imeShown;

        glClearColor(kClearColor[0], kClearColor[1], kClearColor[2], 1.0f);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);  // the alpha channel keeps the clear value, as on desktop

        if (program.ok) {
            glUseProgram(program.program);
            // Turn slowly about y so that the lighting visibly changes and culling hides the back face.
            spin += 0.01f;
            Mat4 turning;
            turning.m[0][0] = std::cos(spin);
            turning.m[0][2] = -std::sin(spin);
            turning.m[2][0] = std::sin(spin);
            turning.m[2][2] = std::cos(spin);
            glUniformMatrix4fv(program.viewProj, 1, GL_FALSE, &viewProj.m[0][0]);
            glUniformMatrix4fv(program.world, 1, GL_FALSE, &turning.m[0][0]);
            glUniformMatrix4fv(program.normalMatrix, 1, GL_FALSE, &turning.m[0][0]);  // a rotation is its own inverse-transpose
            glUniform4f(program.diffuse, 0.9f, 0.6f, 0.2f, 1.0f);
            glUniform4f(program.ambient, 0.5f, 0.5f, 0.5f, 1.0f);
            glUniform4f(program.emissive, 0.0f, 0.0f, 0.0f, 0.0f);
            glUniform4f(program.lightDirection, lightDir.x, lightDir.y, lightDir.z, 0.0f);
            glUniform4f(program.lightDiffuse, 1.0f, 1.0f, 1.0f, 1.0f);
            glUniform4f(program.lightAmbient, 0.5f, 0.5f, 0.5f, 1.0f);
            glUniform4f(program.flags, 1.0f, 1.0f, 0.0f, 0.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, white);
            glUniform1i(program.baseTexture, 0);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (const void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (const void*)12);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (const void*)24);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
            glDisableVertexAttribArray(2);
        }
        if (frames == 0) {
            logLine("first frame: GL error 0x%x", glGetError());
        }

        // Work time: frame start to the swap call (vitaGL presents from a display queue, so the wait for the
        // vblank is not on this thread; docs/VITA_PLAN.md section 1 names this method).
        const double workMs = (sceKernelGetProcessTimeWide() - frameStart) / 1000.0;
        const int vcount = sceDisplayGetVcount();
        vglSwapBuffers(imeShown ? GL_TRUE : GL_FALSE);  // the common dialog must be updated while the IME is up
        stats.frame(workMs, vcount);
        if (++frames % 600 == 0) {
            stats.report("every 600 frames", swapInterval);
        }
    }
    stats.report("at exit", swapInterval);
    logMemory("at exit");
    logLine("exiting on the back button after %d frames", frames);

    glDeleteBuffers(1, &vbo);
    glDeleteTextures(1, &white);
    if (joystick) {
        SDL_CloseJoystick(joystick);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();  // vitaGL has no shutdown call; the process exit releases GXM
    return 0;
}

}  // namespace

int main(int, char**) {
    int rc = 1;
    try {
        rc = run();
    } catch (const std::exception& e) {
        logLine("uncaught exception: %s", e.what());
    }
    logLine("bye (%d)", rc);
    if (g_log) {
        std::fclose(g_log);
    }
    sceKernelExitProcess(rc);
    return rc;
}
