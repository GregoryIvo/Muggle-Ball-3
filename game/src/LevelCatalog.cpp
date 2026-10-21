#include "mb/game/LevelCatalog.h"

#include "StringUtil.h"

#include <algorithm>
#include <system_error>

namespace mb {

LevelCatalog::LevelCatalog(const std::filesystem::path& levelsDir) {
    std::error_code ec, ignored;
    for (std::filesystem::directory_iterator it(levelsDir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::filesystem::path& path = it->path();
        if (it->is_regular_file(ignored) && lowerAscii(path.extension().string()) == ".json") {
            levels_.push_back({path.stem().string(), path});
        }
    }
    std::ranges::sort(levels_, [](const LevelEntry& a, const LevelEntry& b) {
        const std::string x = lowerAscii(a.name), y = lowerAscii(b.name);
        return x != y ? x < y : a.name < b.name;
    });
}

std::optional<std::size_t> LevelCatalog::find(std::string_view nameOrPath) {
    const std::string wanted = lowerAscii(nameOrPath);
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        if (lowerAscii(levels_[i].name) == wanted) {
            return i;
        }
    }
    std::error_code ec;
    const std::filesystem::path path(nameOrPath);
    if (std::filesystem::is_regular_file(path, ec)) {
        levels_.push_back({path.stem().string(), path});
        return levels_.size() - 1;
    }
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        if (lowerAscii(levels_[i].path.filename().string()) == wanted) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace mb
