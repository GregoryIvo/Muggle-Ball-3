#include "mb/game/SaveData.h"

#include "StringUtil.h"
#include "mb/engine/Files.h"
#include "mb/engine/Log.h"
#include "mb/engine/Window.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <system_error>

namespace mb {

namespace {

using nlohmann::json;
using nlohmann::ordered_json;
namespace fs = std::filesystem;

constexpr const char* kSettingsFile = "settings.json";
constexpr const char* kProgressFile = "progress.json";
constexpr const char* kHighScoresFile = "highscores.json";

constexpr int kMaxWindowSize = 16384;
constexpr int kMaxUnlockedLevels = 10000;
constexpr const char* kDefaultName = "Player";

// --- Reading ---------------------------------------------------------------------

// The parsed file, or nothing for a first run or a file that cannot be used (which is then kept as .bak).
std::optional<json> loadDocument(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return std::nullopt;
    }
    const std::optional<std::string> text = readTextFile(path);
    json doc = text ? json::parse(*text, nullptr, false) : json(json::value_t::discarded);

    const char* problem = nullptr;
    if (!text) {
        problem = "cannot be read";
    } else if (doc.is_discarded()) {
        problem = "is not valid JSON";
    } else if (!doc.is_object()) {
        problem = "is not a JSON object";
    } else if (!doc.contains("version") || !doc["version"].is_number_integer() || doc["version"].get<std::int64_t>() < 1) {
        problem = "has no valid version";
    } else if (doc["version"].get<std::int64_t>() > kSaveFormatVersion) {
        problem = "was written by a newer version of the game";
    }
    if (!problem) {
        return doc;
    }
    logWarn("{} {}: keeping it as {}.bak and starting from defaults", path.filename().string(), problem,
            path.filename().string());
    keepAsBackup(path);
    return std::nullopt;
}

void readBool(const json& object, const char* key, bool& out, const char* file) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return;
    }
    if (!found->is_boolean()) {
        logWarn("{}: \"{}\" is not true or false, using {}", file, key, out);
        return;
    }
    out = found->get<bool>();
}

void readFloat(const json& object, const char* key, float& out, float lo, float hi, const char* file) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return;
    }
    if (!found->is_number()) {
        logWarn("{}: \"{}\" is not a number, using {}", file, key, out);
        return;
    }
    const auto value = found->get<double>();
    const double clamped = std::clamp(value, static_cast<double>(lo), static_cast<double>(hi));
    if (clamped != value) {
        logWarn("{}: \"{}\" {} is outside {}..{}, using {}", file, key, value, lo, hi, clamped);
    }
    out = static_cast<float>(clamped);
}

// The value as a 64-bit integer (saturating at the top), or nothing if it is not a whole number.
std::optional<std::int64_t> integerOf(const json& value) {
    if (value.is_number_unsigned()) {
        return static_cast<std::int64_t>(std::min<std::uint64_t>(value.get<std::uint64_t>(),
                                                                 std::numeric_limits<std::int64_t>::max()));
    }
    if (value.is_number_integer()) {
        return value.get<std::int64_t>();
    }
    return std::nullopt;
}

void readInt(const json& object, const char* key, int& out, int lo, int hi, const char* file) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return;
    }
    const std::optional<std::int64_t> value = integerOf(*found);
    if (!value) {
        logWarn("{}: \"{}\" is not a whole number, using {}", file, key, out);
        return;
    }
    out = static_cast<int>(std::clamp<std::int64_t>(*value, lo, hi));
    if (out != *value) {
        logWarn("{}: \"{}\" {} is outside {}..{}, using {}", file, key, *value, lo, hi, out);
    }
}

Settings readSettings(const json& doc) {
    Settings s;
    readFloat(doc, "musicVolume", s.musicVolume, 0.0f, 1.0f, kSettingsFile);
    readFloat(doc, "soundVolume", s.soundVolume, 0.0f, 1.0f, kSettingsFile);
    readBool(doc, "smartCamera", s.smartCamera, kSettingsFile);
    readBool(doc, "freezeSky", s.freezeSky, kSettingsFile);
    readBool(doc, "mirrors", s.mirrors, kSettingsFile);
    readBool(doc, "shadows", s.shadows, kSettingsFile);
    readBool(doc, "fullscreen", s.fullscreen, kSettingsFile);
    readInt(doc, "windowWidth", s.windowWidth, kMinWindowWidth, kMaxWindowSize, kSettingsFile);
    readInt(doc, "windowHeight", s.windowHeight, kMinWindowHeight, kMaxWindowSize, kSettingsFile);
    return s;
}

// A score is a whole number that is not negative.
bool readScore(const json& value, std::int64_t& out) {
    const std::optional<std::int64_t> score = integerOf(value);
    if (!score || *score < 0) {
        return false;
    }
    out = *score;
    return true;
}

Progress readProgress(const json& doc) {
    Progress p;
    if (const auto stages = doc.find("stages"); stages != doc.end()) {
        if (!stages->is_object()) {
            logWarn("{}: \"stages\" is not an object, ignoring it", kProgressFile);
        } else {
            for (const auto& [id, value] : stages->items()) {
                if (!value.is_object()) {
                    logWarn("{}: stage \"{}\" is not an object, ignoring it", kProgressFile, id);
                    continue;
                }
                StageProgress stage;
                readInt(value, "unlockedLevels", stage.unlockedLevels, 1, kMaxUnlockedLevels, kProgressFile);
                if (const auto done = value.find("completed"); done != value.end() && done->is_array()) {
                    for (const json& name : *done) {
                        if (name.is_string()) {
                            stage.completed.insert(lowerAscii(name.get<std::string>()));
                        }
                    }
                }
                p.stages[lowerAscii(id)] = std::move(stage);
            }
        }
    }
    if (const auto scores = doc.find("bestScores"); scores != doc.end()) {
        if (!scores->is_object()) {
            logWarn("{}: \"bestScores\" is not an object, ignoring it", kProgressFile);
        } else {
            for (const auto& [level, value] : scores->items()) {
                std::int64_t score = 0;
                if (readScore(value, score)) {
                    p.bestScores[lowerAscii(level)] = score;
                } else {
                    logWarn("{}: best score of \"{}\" is not a score, ignoring it", kProgressFile, level);
                }
            }
        }
    }
    return p;
}

std::string cleanName(std::string_view name) {
    std::string out;
    for (const char c : name) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) {
            out += c;
        }
    }
    auto trim = [&out] {
        const std::size_t first = out.find_first_not_of(' ');
        out.erase(0, first == std::string::npos ? out.size() : first);
        out.erase(out.find_last_not_of(' ') + 1);
    };
    trim();
    if (out.size() > kMaxNameBytes) {
        std::size_t cut = kMaxNameBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;  // the first dropped byte continues a character: drop the whole character
        }
        out.resize(cut);
        trim();
    }
    return out.empty() ? kDefaultName : out;
}

HighScores readHighScores(const json& doc) {
    HighScores table;
    const auto entries = doc.find("entries");
    if (entries == doc.end()) {
        return table;
    }
    if (!entries->is_array()) {
        logWarn("{}: \"entries\" is not a list, ignoring it", kHighScoresFile);
        return table;
    }
    for (const json& entry : *entries) {
        std::int64_t score = 0;
        const bool valid = entry.is_object() && entry.contains("score") && readScore(entry["score"], score) && score > 0 &&
                           entry.contains("name") && entry["name"].is_string();
        if (!valid) {
            logWarn("{}: ignoring an entry that is not a name and a score above zero", kHighScoresFile);
            continue;
        }
        HighScore item;
        item.name = cleanName(entry["name"].get<std::string>());
        item.score = score;
        if (const auto level = entry.find("level"); level != entry.end() && level->is_string()) {
            item.level = level->get<std::string>();
        }
        table.entries.push_back(std::move(item));
    }
    // Hand-edited files may be out of order or too long.
    std::ranges::stable_sort(table.entries, std::greater<>(), &HighScore::score);
    if (table.entries.size() > kMaxHighScores) {
        table.entries.resize(kMaxHighScores);
    }
    return table;
}

// --- Writing ---------------------------------------------------------------------

// `doc` keeps the order its keys were added in, so "version" comes first in the file.
bool writeDocument(const fs::path& directory, const char* file, const ordered_json& doc) {
    if (directory.empty()) {
        return false;
    }
    // Replace, rather than throw on, text that is not valid UTF-8 (a name typed by the player).
    return writeFileAtomic(directory / file, doc.dump(2, ' ', false, ordered_json::error_handler_t::replace) + "\n");
}

// A volume as a short decimal: 0.7f would otherwise be written as 0.699999988079071.
double thousandths(float volume) {
    return std::round(static_cast<double>(volume) * 1000.0) / 1000.0;
}

}  // namespace

// --- Progress --------------------------------------------------------------------

int Progress::unlockedLevels(std::string_view stage) const {
    const auto found = stages.find(lowerAscii(stage));
    return found == stages.end() ? 1 : found->second.unlockedLevels;
}

bool Progress::isCompleted(std::string_view stage, std::string_view level) const {
    const auto found = stages.find(lowerAscii(stage));
    return found != stages.end() && found->second.completed.count(lowerAscii(level)) > 0;
}

std::int64_t Progress::bestScore(std::string_view level) const {
    const auto found = bestScores.find(lowerAscii(level));
    return found == bestScores.end() ? 0 : found->second;
}

bool Progress::recordCompletion(std::string_view stage, int indexInStage, std::string_view level, std::int64_t score) {
    StageProgress& entry = stages[lowerAscii(stage)];
    entry.completed.insert(lowerAscii(level));
    entry.unlockedLevels = std::max({entry.unlockedLevels, std::max(indexInStage, 0) + 2, 1});
    return recordScore(level, score);
}

bool Progress::recordScore(std::string_view level, std::int64_t score) {
    if (score <= bestScore(level)) {
        return false;
    }
    bestScores[lowerAscii(level)] = score;
    return true;
}

// --- HighScores ------------------------------------------------------------------

bool HighScores::qualifies(std::int64_t score) const {
    return score > 0 && (entries.size() < kMaxHighScores || score > entries.back().score);
}

std::optional<std::size_t> HighScores::add(std::string_view name, std::int64_t score, std::string_view level) {
    if (!qualifies(score)) {
        return std::nullopt;
    }
    // After every entry that scored at least as much: a tie ranks below the entry that got there first.
    const auto place = std::ranges::find_if(entries, [score](const HighScore& e) { return e.score < score; });
    const auto rank = static_cast<std::size_t>(place - entries.begin());
    entries.insert(place, HighScore{cleanName(name), score, std::string(level)});
    if (entries.size() > kMaxHighScores) {
        entries.pop_back();
    }
    return rank;
}

// --- SaveData --------------------------------------------------------------------

SaveData::SaveData(fs::path directory) : directory_(std::move(directory)) {
    if (directory_.empty()) {
        return;
    }
    if (const auto doc = loadDocument(directory_ / kSettingsFile)) {
        settings = readSettings(*doc);
    }
    if (const auto doc = loadDocument(directory_ / kProgressFile)) {
        progress = readProgress(*doc);
    }
    if (const auto doc = loadDocument(directory_ / kHighScoresFile)) {
        highScores = readHighScores(*doc);
    }
}

bool SaveData::saveSettings() const {
    ordered_json doc;
    doc["version"] = kSaveFormatVersion;
    doc["musicVolume"] = thousandths(settings.musicVolume);
    doc["soundVolume"] = thousandths(settings.soundVolume);
    doc["smartCamera"] = settings.smartCamera;
    doc["freezeSky"] = settings.freezeSky;
    doc["mirrors"] = settings.mirrors;
    doc["shadows"] = settings.shadows;
    doc["fullscreen"] = settings.fullscreen;
    doc["windowWidth"] = settings.windowWidth;
    doc["windowHeight"] = settings.windowHeight;
    return writeDocument(directory_, kSettingsFile, doc);
}

bool SaveData::saveProgress() const {
    ordered_json doc;
    doc["version"] = kSaveFormatVersion;
    doc["stages"] = json::object();
    for (const auto& [id, stage] : progress.stages) {
        doc["stages"][id] = {{"unlockedLevels", stage.unlockedLevels}, {"completed", stage.completed}};
    }
    doc["bestScores"] = json::object();
    for (const auto& [level, score] : progress.bestScores) {
        doc["bestScores"][level] = score;
    }
    return writeDocument(directory_, kProgressFile, doc);
}

bool SaveData::saveHighScores() const {
    ordered_json doc;
    doc["version"] = kSaveFormatVersion;
    doc["entries"] = json::array();
    for (const HighScore& e : highScores.entries) {
        doc["entries"].push_back({{"name", e.name}, {"score", e.score}, {"level", e.level}});
    }
    return writeDocument(directory_, kHighScoresFile, doc);
}

}  // namespace mb
