#include "mb/game/LevelLoader.h"

#include "mb/engine/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace mb {

namespace {

using nlohmann::json;

Vec3 vec3(const json& j) {
    return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()};
}

Pose pose(const json& j) {
    Pose p;
    const json& rot = j.at("rotation");
    for (std::size_t i = 0; i < p.rotation.size(); ++i) {
        p.rotation[i] = rot.at(i).get<float>();
    }
    p.position = vec3(j.at("position"));
    return p;
}

ShapeType shapeType(const std::string& name) {
    if (name == "plane") return ShapeType::Plane;
    if (name == "sphere") return ShapeType::Sphere;
    if (name == "box") return ShapeType::Box;
    if (name == "capsule") return ShapeType::Capsule;
    if (name == "convex") return ShapeType::Convex;
    if (name == "mesh") return ShapeType::Mesh;
    throw std::runtime_error("unknown shape type: " + name);
}

Shape shape(const json& j) {
    Shape s;
    s.type = shapeType(j.at("type").get<std::string>());
    s.localPose = pose(j.at("localPose"));
    if (j.contains("halfExtents")) s.halfExtents = vec3(j["halfExtents"]);
    if (j.contains("radius")) s.radius = j["radius"].get<float>();
    if (j.contains("radiusHeight")) {
        s.radius = j["radiusHeight"].at(0).get<float>();
        s.height = j["radiusHeight"].at(1).get<float>();
    }
    if (j.contains("meshName")) s.meshName = j["meshName"].get<std::string>();
    for (const json& p : j.value("hullPoints", json::array())) {
        s.hullPoints.push_back(vec3(p));
    }
    s.shapeFlags = j.value("shapeFlags", 0u);
    s.group = static_cast<std::uint16_t>(j.value("group", 0u));
    s.materialIndex = static_cast<std::uint16_t>(j.value("materialIndex", 0u));
    s.skinWidth = j.value("skinWidth", s.skinWidth);
    return s;
}

Actor actor(const json& j);

std::vector<std::uint8_t> hexBytes(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        throw std::runtime_error("odd-length hex string");
    }
    std::vector<std::uint8_t> bytes(hex.size() / 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
    }
    return bytes;
}

// Before version 3 the body descriptor was a hex string; it still gives a dynamic actor, with default mass properties.
Body body(const json& j) {
    Body b;
    if (j.is_string()) return b;
    if (j.contains("massLocalPose")) b.massLocalPose = pose(j["massLocalPose"]);
    if (j.contains("massSpaceInertia")) b.massSpaceInertia = vec3(j["massSpaceInertia"]);
    b.mass = j.value("mass", b.mass);
    if (j.contains("linearVelocity")) b.linearVelocity = vec3(j["linearVelocity"]);
    if (j.contains("angularVelocity")) b.angularVelocity = vec3(j["angularVelocity"]);
    b.wakeUpCounter = j.value("wakeUpCounter", b.wakeUpCounter);
    b.linearDamping = j.value("linearDamping", b.linearDamping);
    b.angularDamping = j.value("angularDamping", b.angularDamping);
    b.maxAngularVelocity = j.value("maxAngularVelocity", b.maxAngularVelocity);
    b.flags = j.value("flags", b.flags);
    b.sleepLinearVelocity = j.value("sleepLinearVelocity", b.sleepLinearVelocity);
    b.sleepAngularVelocity = j.value("sleepAngularVelocity", b.sleepAngularVelocity);
    b.solverIterationCount = j.value("solverIterationCount", b.solverIterationCount);
    return b;
}

JointLimitDesc jointLimit(const json& j) {
    JointLimitDesc l;
    l.value = j.value("value", l.value);
    l.restitution = j.value("restitution", l.restitution);
    l.hardness = j.value("hardness", l.hardness);
    return l;
}

SpringDesc springDesc(const json& j) {
    SpringDesc s;
    s.spring = j.value("spring", s.spring);
    s.damper = j.value("damper", s.damper);
    s.targetValue = j.value("targetValue", s.targetValue);
    return s;
}

// The revolute and the spherical joint keep their limits and springs under different names: both
// fill the same Joint fields (Level.h says which).
Joint joint(const json& j) {
    Joint out;
    out.type = static_cast<JointType>(j.at("type").get<std::uint32_t>());
    out.actorA = j.at("actorA").get<std::uint32_t>();
    out.actorB = j.at("actorB").get<std::uint32_t>();
    const auto perActor = [&j](const char* key, Vec3 (&dst)[2]) {
        if (!j.contains(key)) return;
        for (std::size_t i = 0; i < 2; ++i) dst[i] = vec3(j[key].at(i));
    };
    perActor("localNormal", out.localNormal);
    perActor("localAxis", out.localAxis);
    perActor("localAnchor", out.localAnchor);
    // A joint with no stored limit does not break.
    out.maxForce = j.value("maxForce", std::numeric_limits<float>::max());
    out.maxTorque = j.value("maxTorque", std::numeric_limits<float>::max());
    out.jointFlags = j.value("jointFlags", 0u);
    out.typeFlags = j.value("flags", 0u);
    out.projectionDistance = j.value("projectionDistance", out.projectionDistance);
    out.projectionAngle = j.value("projectionAngle", out.projectionAngle);
    out.projectionMode = j.value("projectionMode", 0u);
    for (const char* key : {"limit", "twistLimit"}) {
        if (!j.contains(key)) continue;
        out.limitLow = jointLimit(j[key].at("low"));
        out.limitHigh = jointLimit(j[key].at("high"));
    }
    if (j.contains("swingLimit")) out.swingLimit = jointLimit(j["swingLimit"]);
    if (j.contains("swingAxis")) out.swingAxis = vec3(j["swingAxis"]);
    if (j.contains("motor")) {
        const json& motor = j["motor"];
        out.motorVelTarget = motor.value("velTarget", out.motorVelTarget);
        out.motorMaxForce = motor.value("maxForce", out.motorMaxForce);
        out.motorFreeSpin = motor.value("freeSpin", out.motorFreeSpin);
    }
    for (const char* key : {"spring", "jointSpring"}) {
        if (j.contains(key)) out.spring = springDesc(j[key]);
    }
    if (j.contains("twistSpring")) out.twistSpring = springDesc(j["twistSpring"]);
    if (j.contains("swingSpring")) out.swingSpring = springDesc(j["swingSpring"]);
    if (j.contains("raw")) out.raw = hexBytes(j["raw"].get<std::string>());
    return out;
}

GameObject object(const json& j) {
    GameObject o;
    o.type = objectTypeFromName(j.at("type").get<std::string>());
    o.id = j.at("id").get<std::uint32_t>();
    o.flags = j.at("flags").get<std::uint32_t>();
    o.material = j.value("material", "");
    o.mesh = j.value("mesh", "");
    o.frame = j.value("frame", "");
    o.prize = j.value("prize", "");
    if (j.contains("extra")) o.extra = hexBytes(j["extra"].get<std::string>());
    if (j.contains("cloth")) {
        for (const json& seg : j["cloth"].at("segments")) {
            o.clothSegments.push_back(actor(seg));
        }
    }
    return o;
}

Actor actor(const json& j) {
    Actor a;
    a.pose = pose(j.at("pose"));
    if (j.contains("body")) a.body = body(j["body"]);
    a.density = j.value("density", a.density);
    a.actorFlags = j.value("actorFlags", a.actorFlags);
    a.group = static_cast<std::uint16_t>(j.value("group", 0u));
    for (const json& s : j.at("shapes")) {
        a.shapes.push_back(shape(s));
    }
    if (j.contains("object")) {
        a.object = object(j["object"]);
    }
    return a;
}

}  // namespace

Level parseLevelJson(std::string_view text) {
    const json j = json::parse(text);
    Level level;
    level.version = j.value("version", 1);
    level.gravity = vec3(j.at("gravity"));
    for (const json& a : j.at("actors")) {
        level.actors.push_back(actor(a));
    }
    for (const json& jj : j.value("joints", json::array())) {
        level.joints.push_back(joint(jj));
    }
    for (const json& p : j.value("paths", json::array())) {
        Path path;
        path.name = p.at("name").get<std::string>();
        for (const json& pt : p.at("points")) {
            path.points.push_back({vec3(pt.at("pos")), vec3(pt.at("tng")), vec3(pt.at("dir")), vec3(pt.at("up"))});
        }
        level.paths.push_back(std::move(path));
    }
    return level;
}

Level loadLevelJson(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open level: " + path.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    Level level = parseLevelJson(buf.str());
    if (level.version < kLevelJsonVersion) {
        logWarn("{}: level JSON is version {}, this build reads version {}; "
                "run tools/convert_assets.py again to refresh it",
                path.string(), level.version, kLevelJsonVersion);
    }
    return level;
}

}  // namespace mb
