#include "mb/game/ParticleDef.h"

#include "StringUtil.h"
#include "mb/engine/Files.h"
#include "mb/engine/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <utility>

namespace mb {

namespace {

using nlohmann::json;

constexpr std::array<std::pair<std::string_view, ParticleType>, kParticleTypeCount> kTypeNames{{
    {"Oreol", ParticleType::Oreol},
    {"Flash", ParticleType::Flash},
    {"Firework", ParticleType::Firework},
    {"Laser", ParticleType::Laser},
    {"Lightning", ParticleType::Lightning},
    {"Trail", ParticleType::Trail},
    {"LeafFall", ParticleType::LeafFall},
    {"Smoke", ParticleType::Smoke},
    {"WaterBubble", ParticleType::WaterBubble},
    {"Tornado", ParticleType::Tornado},
}};

// The attributes, each with how it is stored. A reader returns false for a value of the wrong kind.
using Reader = std::function<bool(ParticleDef&, const json&)>;

Reader number(float ParticleDef::*field, float scale = 1.0f) {
    return [field, scale](ParticleDef& def, const json& value) {
        if (!value.is_number()) {
            return false;
        }
        def.*field = value.get<float>() * scale;
        return true;
    };
}

Reader boolean(bool ParticleDef::*field) {
    return [field](ParticleDef& def, const json& value) {
        if (!value.is_boolean()) {
            return false;
        }
        def.*field = value.get<bool>();
        return true;
    };
}

Reader text(std::string ParticleDef::*field) {
    return [field](ParticleDef& def, const json& value) {
        if (!value.is_string()) {
            return false;
        }
        def.*field = value.get<std::string>();
        return true;
    };
}

Reader vector(std::optional<Vec3> ParticleDef::*field) {
    return [field](ParticleDef& def, const json& value) {
        if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
            return false;
        }
        def.*field = Vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        return true;
    };
}

Reader colour(std::optional<Color> ParticleDef::*field) {
    return [field](ParticleDef& def, const json& value) {
        if (!value.is_number_unsigned()) {
            return false;
        }
        const auto argb = value.get<std::uint64_t>();
        const auto channel = [argb](int shift) { return static_cast<float>((argb >> shift) & 0xFF) / 255.0f; };
        def.*field = Color{channel(16), channel(8), channel(0), channel(24)};
        return true;
    };
}

const std::unordered_map<std::string, Reader>& readers() {
    static const std::unordered_map<std::string, Reader> table{
        {"name", text(&ParticleDef::name)},
        {"texture", text(&ParticleDef::texture)},
        {"num",
         [](ParticleDef& def, const json& value) {
             if (!value.is_number()) {
                 return false;
             }
             def.count = static_cast<int>(std::lround(value.get<double>()));
             return true;
         }},
        {"size", number(&ParticleDef::size)},
        {"radius", number(&ParticleDef::radius)},
        {"radius_offset", number(&ParticleDef::radiusOffset)},
        {"radius_small", number(&ParticleDef::radiusSmall)},
        {"radius_big", number(&ParticleDef::radiusBig)},
        {"speed", number(&ParticleDef::speed)},
        {"speed_angular", number(&ParticleDef::speedAngular)},
        {"angle", number(&ParticleDef::angle)},
        {"height", number(&ParticleDef::height)},
        {"width", number(&ParticleDef::width)},
        {"fade_time_msec", number(&ParticleDef::fadeTime, 0.001f)},
        {"shift_coeff", number(&ParticleDef::shiftCoeff)},
        {"shake_coeff", number(&ParticleDef::shakeCoeff)},
        {"inverse_rotation", boolean(&ParticleDef::inverseRotation)},
        {"modulate_colour", boolean(&ParticleDef::modulateColour)},
        {"direction", vector(&ParticleDef::direction)},
        {"dimension", vector(&ParticleDef::dimension)},
        {"shadow_plane", vector(&ParticleDef::shadowPlane)},
        {"shadow_position", vector(&ParticleDef::shadowPosition)},
        {"colour", colour(&ParticleDef::colour)},
        {"colour1", colour(&ParticleDef::colour1)},
        {"colour2", colour(&ParticleDef::colour2)},
    };
    return table;
}

}  // namespace

std::string_view particleTypeName(ParticleType type) {
    for (const auto& [name, value] : kTypeNames) {
        if (value == type) {
            return name;
        }
    }
    return "?";
}

std::optional<ParticleType> particleTypeByName(std::string_view name) {
    const std::string wanted = lowerAscii(name);
    for (const auto& [spelling, value] : kTypeNames) {
        if (lowerAscii(spelling) == wanted) {
            return value;
        }
    }
    return std::nullopt;
}

ParticleLibrary ParticleLibrary::load(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    std::error_code error, ignored;
    // The error_code overloads throughout: directory_iterator's default increment (as a range-for would call it)
    // throws on a read error partway through, which the manual loop below avoids.
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(ignored) && lowerAscii(it->path().extension().string()) == ".json") {
            files.push_back(it->path());
        }
    }
    if (error) {
        logWarn("particles: cannot read the folder {}: {}", directory.string(), error.message());
    }
    std::sort(files.begin(), files.end());
    std::vector<Source> sources;
    for (const std::filesystem::path& file : files) {
        if (std::optional<std::string> contents = readTextFile(file)) {
            sources.push_back({file.stem().string(), std::move(*contents)});
        } else {
            logWarn("particles: cannot read {}", file.string());
        }
    }
    ParticleLibrary library = parse(sources);
    for (const std::string& problem : library.problems_) {
        logWarn("particles: {}", problem);
    }
    for (const std::string& attribute : library.unknown_) {
        logWarn("particles: unknown attribute {}", attribute);
    }
    return library;
}

ParticleLibrary ParticleLibrary::parse(std::span<const Source> sources) {
    ParticleLibrary library;
    for (const Source& source : sources) {
        library.add(source);
    }
    return library;
}

void ParticleLibrary::add(const Source& source) {
    json root;
    try {
        root = json::parse(source.json);
    } catch (const json::exception& e) {
        problems_.push_back(source.stem + ": not JSON (" + e.what() + ")");
        return;
    }
    const std::string stem = lowerAscii(source.stem);
    const std::size_t first = defs_.size();
    const json* children = root.contains("root") && root["root"].contains("children") ? &root["root"]["children"] : nullptr;
    if (children == nullptr || !children->is_array()) {
        problems_.push_back(source.stem + ": no <particles> element");
        return;
    }
    for (const json& element : *children) {
        if (element.value("tag", "") != "particle" || !element.contains("attrs") || !element["attrs"].is_object()) {
            continue;
        }
        ParticleDef def;
        def.file = source.stem;
        std::optional<ParticleType> type;
        std::vector<std::string> unknownKeys;
        bool ok = true;
        for (const auto& [key, value] : element["attrs"].items()) {
            if (key == "type") {
                type = value.is_string() ? particleTypeByName(value.get<std::string>()) : std::nullopt;
                continue;
            }
            const auto reader = readers().find(key);
            if (reader == readers().end()) {
                unknownKeys.push_back(key);
            } else if (!reader->second(def, value)) {
                problems_.push_back(source.stem + "/" + def.name + ": the value of " + key + " is of the wrong kind");
                ok = false;
            }
        }
        if (!type) {
            problems_.push_back(source.stem + "/" + element["attrs"].value("name", "") + ": no known type");
            continue;
        }
        if (!ok) {
            continue;
        }
        def.type = *type;
        def.id = static_cast<std::uint32_t>(defs_.size());
        for (const std::string& key : unknownKeys) {
            unknown_.push_back(source.stem + "/" + def.name + ": " + key);
        }
        byName_.try_emplace(stem + "/" + lowerAscii(def.name), defs_.size());
        defs_.push_back(std::move(def));
    }
    if (defs_.size() > first) {
        byName_.try_emplace(stem, first);
        files_[stem] = {first, defs_.size() - first};
    }
}

const ParticleDef* ParticleLibrary::find(std::string_view spec) const {
    const auto found = byName_.find(lowerAscii(spec));
    return found == byName_.end() ? nullptr : &defs_[found->second];
}

std::span<const ParticleDef> ParticleLibrary::file(std::string_view stem) const {
    const auto found = files_.find(lowerAscii(stem));
    return found == files_.end() ? std::span<const ParticleDef>() : std::span<const ParticleDef>(defs_).subspan(found->second.first, found->second.second);
}

}  // namespace mb
