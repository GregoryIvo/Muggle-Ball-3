#include "mb/game/Campaign.h"

#include "StringUtil.h"
#include "mb/engine/Log.h"

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mb {

namespace {

// The level-select form of each stage, by the stage's name (its music). The forms and the levels.xml agree on the
// order Pirates, Village, Cowboys, Knights; the forms call the village "farm".
std::string formOfStage(std::string_view stage) {
    const std::string name = lowerAscii(stage);
    if (name == "pirates") return "stagepiratos";
    if (name == "village") return "stagefarm";
    if (name == "cowboys") return "stagecowboys";
    if (name == "knights") return "stageknights";
    logWarn("no level-select form for the stage \"{}\"", stage);
    return {};
}

}  // namespace

Campaign Campaign::load(const std::filesystem::path& levelsJson) {
    std::ifstream in(levelsJson, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open the level list " + levelsJson.string() + " (run tools/convert_assets.py)");
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    try {
        return parse(buf.str());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", levelsJson.string(), e.what()));
    }
}

Campaign Campaign::parse(std::string_view text) {
    using nlohmann::json;
    Campaign campaign;
    try {
        const json root = json::parse(text);
        if (const int version = root.value("version", 1); version != 1) {
            throw std::runtime_error(std::format("unsupported level list version {}", version));
        }
        for (const json& entry : root.at("stages")) {
            Stage stage;
            stage.name = entry.at("name").get<std::string>();
            stage.form = formOfStage(stage.name);
            stage.first = campaign.count_;
            stage.levels = entry.at("levels").get<std::vector<std::string>>();
            if (stage.levels.empty()) {
                throw std::runtime_error(std::format("the stage {} has no levels", stage.name));
            }
            campaign.count_ += static_cast<int>(stage.levels.size());
            campaign.stages_.push_back(std::move(stage));
        }
        if (campaign.stages_.empty()) {
            throw std::runtime_error("the level list has no stages");
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::format("invalid level list JSON: {}", e.what()));
    }
    return campaign;
}

const Stage* Campaign::stageOf(int index) const {
    for (const Stage& stage : stages_) {
        if (index >= stage.first && index < stage.first + static_cast<int>(stage.levels.size())) {
            return &stage;
        }
    }
    return nullptr;
}

const std::string* Campaign::levelName(int index) const {
    const Stage* stage = stageOf(index);
    return stage ? &stage->levels[static_cast<std::size_t>(index - stage->first)] : nullptr;
}

const Stage* Campaign::stageForForm(std::string_view form) const {
    const std::string wanted = lowerAscii(form);
    for (const Stage& stage : stages_) {
        if (stage.form == wanted) {
            return &stage;
        }
    }
    return nullptr;
}

std::optional<int> Campaign::indexOf(std::string_view levelName) const {
    const std::string wanted = lowerAscii(levelName);
    for (const Stage& stage : stages_) {
        for (std::size_t i = 0; i < stage.levels.size(); ++i) {
            if (lowerAscii(stage.levels[i]) == wanted) {
                return stage.first + static_cast<int>(i);
            }
        }
    }
    return std::nullopt;
}

}  // namespace mb
