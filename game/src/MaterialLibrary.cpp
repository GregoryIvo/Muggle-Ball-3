#include "mb/game/MaterialLibrary.h"

#include "StringUtil.h"

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mb {

namespace {

using nlohmann::json;

constexpr int kRenderStateAlphaBlendEnable = 27;  // D3DRS_ALPHABLENDENABLE
constexpr int kRenderStateDestBlend = 20;         // D3DRS_DESTBLEND
constexpr int kBlendOne = 2;                      // D3DBLEND_ONE

// Reads obj[key] into out when present; a missing or null key leaves the default.
template <class T>
void read(const json& obj, const char* key, T& out) {
    if (const auto it = obj.find(key); it != obj.end() && !it->is_null()) {
        out = it->get<T>();
    }
}

void readColor(const json& obj, const char* key, Color& out) {
    if (const auto it = obj.find(key); it != obj.end() && !it->is_null()) {
        out = {it->at(0).get<float>(), it->at(1).get<float>(), it->at(2).get<float>(), it->at(3).get<float>()};
    }
}

// An optional nested object; absent means "all defaults".
const json& section(const json& obj, const char* key) {
    static const json none = json::object();
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return none;
    }
    if (!it->is_object()) {
        throw std::runtime_error(std::format("'{}' must be an object", key));
    }
    return *it;
}

GameMaterial parseMaterial(const json& j) {
    GameMaterial m;
    m.name = j.at("name").get<std::string>();
    read(j, "group", m.group);
    read(j, "textures", m.textures);
    read(j, "animated", m.animated);
    read(j, "animTimeMs", m.animTimeMs);
    read(j, "density", m.density);
    read(j, "splinter", m.splinter);

    const json& d3d = section(j, "d3d");
    readColor(d3d, "diffuse", m.diffuse);
    readColor(d3d, "ambient", m.ambient);
    readColor(d3d, "emissive", m.emissive);
    readColor(d3d, "specular", m.specular);
    read(d3d, "power", m.power);

    const json& physics = section(j, "physics");
    read(physics, "staticFriction", m.staticFriction);
    read(physics, "dynamicFriction", m.dynamicFriction);
    read(physics, "restitution", m.restitution);
    read(physics, "frictionCombineMode", m.frictionCombineMode);
    read(physics, "restitutionCombineMode", m.restitutionCombineMode);

    const json& brick = section(j, "brick");
    read(brick, "type", m.brickType);
    read(brick, "energy", m.brickEnergy);
    read(brick, "score", m.brickScore);
    read(brick, "exploseType", m.exploseType);
    read(brick, "exploseDelay", m.exploseDelay);
    read(brick, "explosePower", m.explosePower);

    const json& sounds = section(j, "sounds");
    read(sounds, "drop", m.dropSound);
    read(sounds, "period", m.periodSound);
    read(sounds, "hit", m.hitSound);
    read(sounds, "dead", m.deadSound);
    read(sounds, "loop", m.loopSound);

    if (const auto it = j.find("renderStates"); it != j.end()) {
        for (const json& pair : *it) {
            m.renderStates.emplace_back(pair.at(0).get<int>(), pair.at(1).get<int>());
        }
    }
    return m;
}

}  // namespace

BlendMode GameMaterial::blendMode() const {
    bool blending = false;
    int destBlend = 0;
    for (const auto& [state, value] : renderStates) {
        if (state == kRenderStateAlphaBlendEnable) {
            blending = value == 1;
        } else if (state == kRenderStateDestBlend) {
            destBlend = value;
        }
    }
    if (!blending) {
        return BlendMode::Opaque;
    }
    return destBlend == kBlendOne ? BlendMode::Additive : BlendMode::AlphaBlend;
}

MaterialLibrary MaterialLibrary::load(const std::filesystem::path& materialsJson) {
    std::ifstream in(materialsJson, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open materials: " + materialsJson.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    try {
        return parse(buf.str());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", materialsJson.string(), e.what()));
    }
}

MaterialLibrary MaterialLibrary::parse(std::string_view text) {
    MaterialLibrary library;
    try {
        const json root = json::parse(text);
        if (const int version = root.value("version", 1); version != 1) {
            throw std::runtime_error(std::format("unsupported materials version {}", version));
        }
        std::size_t index = 0;
        for (const json& entry : root.at("materials")) {
            try {
                library.materials_.push_back(parseMaterial(entry));
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("material #{}: {}", index, e.what()));
            }
            // emplace keeps the first definition of a repeated name.
            library.byName_.emplace(lowerAscii(library.materials_.back().name), index);
            ++index;
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::format("invalid materials JSON: {}", e.what()));
    }
    return library;
}

const GameMaterial* MaterialLibrary::find(std::string_view name) const {
    const auto it = byName_.find(lowerAscii(name));
    return it == byName_.end() ? nullptr : &materials_[it->second];
}

}  // namespace mb
