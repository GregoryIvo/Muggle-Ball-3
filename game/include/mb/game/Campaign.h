#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mb {

// One stage of the game: a run of levels with its own music and its own level-select form.
struct Stage {
    std::string name;   // "Pirates": also the name of its music
    std::string form;   // the level-select form, "stagepiratos"
    int first = 0;      // the campaign index of its first level
    std::vector<std::string> levels;  // level names as written in the data, e.g. "piratos-ostrovok"
};

// The levels in the order the game lists them, grouped into stages: Data/xml/levels.json, written by
// tools/campaign.py from the original's Data/Levels/levels.xml (schema in docs/ASSET_FORMATS.md). The numbers on the
// level-select forms (the buttons named 0 to 82) are indices into this list.
//
// The names are those of the level files; look them up in a LevelCatalog, which ignores case (the XML spells
// "piratos-SOS" where the file is "Piratos-SOS").
class Campaign {
public:
    static Campaign load(const std::filesystem::path& levelsJson);  // throws std::runtime_error
    static Campaign parse(std::string_view json);                   // throws std::runtime_error

    const std::vector<Stage>& stages() const { return stages_; }
    int levelCount() const { return count_; }

    // The level at a campaign index, or null out of range.
    const std::string* levelName(int index) const;
    // The stage a campaign index is in, or null out of range.
    const Stage* stageOf(int index) const;
    // The stage whose level-select form is this (case-insensitive), or null.
    const Stage* stageForForm(std::string_view form) const;
    // The campaign index of a level (file stem, case-insensitive), if the campaign has it.
    std::optional<int> indexOf(std::string_view levelName) const;

private:
    std::vector<Stage> stages_;
    int count_ = 0;
};

}  // namespace mb
