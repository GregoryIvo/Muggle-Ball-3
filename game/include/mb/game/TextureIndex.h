#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mb {

// Finds texture files by the names the game data uses ("Pushka", "01", "Sky.jpg",
// "Icons/heart.png"). Matching is case-insensitive and the extension is optional.
// When several files share a name, a file directly in the root wins over one in a
// subdirectory, and .png wins over .jpg over anything else.
class TextureIndex {
public:
    TextureIndex() = default;
    explicit TextureIndex(const std::filesystem::path& texturesDir);  // indexes recursively

    // Empty path if nothing matches.
    std::filesystem::path find(std::string_view name) const;

    std::size_t size() const { return byStem_.size(); }

private:
    std::unordered_map<std::string, std::filesystem::path> byStem_;      // "pushka" -> .../Pushka.jpg
    std::unordered_map<std::string, std::filesystem::path> byRelative_;  // "icons/heart.png" -> ...
};

}  // namespace mb
