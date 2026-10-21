#include "mb/game/LevelLoader.h"

#include "mb/engine/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>
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
    if (j.contains("body")) a.body = Body{};
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
