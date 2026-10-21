#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mb {

// The whole file, or nothing if it cannot be opened or read.
std::optional<std::string> readTextFile(const std::filesystem::path& path);

// Replaces `path` with `contents` so that a crash never leaves half a file: the bytes are written to
// "<path>.tmp" and renamed over the target, which leaves either the old file or the new one (it does not
// survive a power cut, which would need an fsync). Missing parent folders are created. Returns false and
// logs why on failure; the old file is then untouched and no ".tmp" is left behind.
bool writeFileAtomic(const std::filesystem::path& path, std::string_view contents);

// Moves a file that cannot be used to "<path>.bak", replacing an older backup, so that a fresh file can
// be written without destroying what was there. Returns false (and logs) if the move failed.
bool keepAsBackup(const std::filesystem::path& path);

}  // namespace mb
