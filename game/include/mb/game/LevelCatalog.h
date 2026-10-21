#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mb {

struct LevelEntry {
    std::string name;  // file stem, e.g. "piratos-ostrovok"
    std::filesystem::path path;
};

// The converted level files (assets/Data/Levels/*.json), ordered by name ignoring case.
class LevelCatalog {
public:
    explicit LevelCatalog(const std::filesystem::path& levelsDir);

    const std::vector<LevelEntry>& levels() const { return levels_; }

    // `nameOrPath` is a level's file stem (case-insensitive) or the path of a level .json,
    // which joins the catalog if it was not in the directory. Empty if neither matches.
    std::optional<std::size_t> find(std::string_view nameOrPath);

private:
    std::vector<LevelEntry> levels_;
};

}  // namespace mb
