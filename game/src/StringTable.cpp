#include "mb/game/StringTable.h"

#include "StringUtil.h"

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mb {

StringTable StringTable::load(const std::filesystem::path& languagesJson) {
    std::ifstream in(languagesJson, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open string table: " + languagesJson.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    try {
        return parse(buf.str());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", languagesJson.string(), e.what()));
    }
}

StringTable StringTable::parse(std::string_view text) {
    using nlohmann::json;
    StringTable table;
    try {
        const json root = json::parse(text);
        if (const int version = root.value("version", 1); version != 1) {
            throw std::runtime_error(std::format("unsupported string table version {}", version));
        }
        for (const json& entry : root.at("languages")) {
            Language language;
            language.name = entry.at("name").get<std::string>();
            for (const auto& [key, value] : entry.at("strings").items()) {
                language.strings.emplace(key, value.get<std::string>());
            }
            table.languages_.push_back(std::move(language));
        }
        if (table.languages_.empty()) {
            throw std::runtime_error("the string table has no languages");
        }
        table.setLanguage(root.value("current", table.languages_.front().name));
    } catch (const json::exception& e) {
        throw std::runtime_error(std::format("invalid string table JSON: {}", e.what()));
    }
    return table;
}

std::vector<std::string> StringTable::languages() const {
    std::vector<std::string> names;
    for (const Language& language : languages_) {
        names.push_back(language.name);
    }
    return names;
}

bool StringTable::setLanguage(std::string_view name) {
    const std::string wanted = lowerAscii(name);
    for (std::size_t i = 0; i < languages_.size(); ++i) {
        if (lowerAscii(languages_[i].name) == wanted) {
            current_ = i;
            return true;
        }
    }
    return false;
}

const std::string* StringTable::find(std::string_view key) const {
    const std::string wanted(key);
    for (const std::size_t index : {current_, std::size_t{0}}) {
        const auto& strings = languages_[index].strings;
        if (const auto it = strings.find(wanted); it != strings.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

std::string StringTable::resolve(std::string_view keyOrText) const {
    if (const std::string* found = find(keyOrText)) {
        return *found;
    }
    return std::string(keyOrText);
}

}  // namespace mb
