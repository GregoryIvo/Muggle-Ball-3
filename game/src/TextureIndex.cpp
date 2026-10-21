#include "mb/game/TextureIndex.h"

#include "StringUtil.h"
#include "mb/engine/Log.h"

#include <algorithm>
#include <system_error>
#include <tuple>
#include <vector>

namespace mb {

namespace {

// Lowercase, '/' separators, no "./" or "a/../b" detours.
std::string normalizeKey(std::string_view name) {
    std::string key = lowerAscii(name);
    std::ranges::replace(key, '\\', '/');
    return std::filesystem::path(key).lexically_normal().generic_string();
}

std::string stripExtension(const std::string& key) {
    return std::filesystem::path(key).replace_extension().generic_string();
}

struct Candidate {
    std::string key;  // path relative to the texture root, normalized
    std::filesystem::path path;
    bool subfolder = false;
    int extensionRank = 2;  // .png, then .jpg, then anything else
};

int rankOf(const std::string& key) {
    const std::string ext = std::filesystem::path(key).extension().generic_string();
    if (ext == ".png") return 0;
    if (ext == ".jpg" || ext == ".jpeg") return 1;
    return 2;
}

}  // namespace

TextureIndex::TextureIndex(const std::filesystem::path& texturesDir) {
    std::vector<Candidate> files;
    std::error_code ec, ignored;
    std::filesystem::recursive_directory_iterator it(
        texturesDir, std::filesystem::directory_options::skip_permission_denied, ec);
    for (const std::filesystem::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        const std::filesystem::path& path = it->path();
        if (!it->is_regular_file(ignored) || path.filename().string().starts_with('.')) {
            continue;
        }
        Candidate c;
        c.key = normalizeKey(path.lexically_relative(texturesDir).generic_string());
        c.path = path;
        c.subfolder = c.key.find('/') != std::string::npos;
        c.extensionRank = rankOf(c.key);
        files.push_back(std::move(c));
    }
    if (ec) {
        logWarn("cannot index textures in {}: {}", texturesDir.string(), ec.message());
    }

    // Best candidate first, so the first insertion for a key wins no matter what
    // order the filesystem listed the files in.
    std::ranges::sort(files, [](const Candidate& a, const Candidate& b) {
        return std::tie(a.subfolder, a.extensionRank, a.key, a.path) <
               std::tie(b.subfolder, b.extensionRank, b.key, b.path);
    });
    for (const Candidate& f : files) {
        byRelative_.emplace(f.key, f.path);
        byStem_.emplace(std::filesystem::path(f.key).stem().generic_string(), f.path);
    }
    // A second pass so that a real file name always beats the same name minus its extension.
    for (const Candidate& f : files) {
        byRelative_.emplace(stripExtension(f.key), f.path);
    }
}

std::filesystem::path TextureIndex::find(std::string_view name) const {
    const auto lookup = [this](const std::string& key) -> const std::filesystem::path* {
        if (const auto it = byRelative_.find(key); it != byRelative_.end()) {
            return &it->second;
        }
        if (key.find('/') == std::string::npos) {
            if (const auto it = byStem_.find(key); it != byStem_.end()) {
                return &it->second;
            }
        }
        return nullptr;
    };

    const std::string key = normalizeKey(name);
    if (const std::filesystem::path* found = lookup(key)) {
        return *found;
    }
    // A name can carry an extension the converted file does not have ("foo.dds" -> foo.png).
    const std::string stripped = stripExtension(key);
    if (stripped != key) {
        if (const std::filesystem::path* found = lookup(stripped)) {
            return *found;
        }
    }
    return {};
}

}  // namespace mb
