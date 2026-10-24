#include "mb/engine/Files.h"

#include "mb/engine/Log.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace mb {

namespace fs = std::filesystem;

std::optional<std::string> readTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return std::nullopt;
    }
    return text;
}

bool writeFileAtomic(const fs::path& path, std::string_view contents) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            logError("cannot create {}: {}", path.parent_path().string(), ec.message());
            return false;
        }
    }
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            logError("cannot write {}", temp.string());
            out.close();
            fs::remove(temp, ec);
            return false;
        }
    }
    fs::rename(temp, path, ec);
    if (!ec) {
        return true;
    }
    // Some file systems refuse to rename over an existing target instead of replacing it outright (whether the
    // Vita's file system ever does this for an ordinary save file is unverified, see docs/VITA.md). The fallback
    // below only applies when `path` is a regular file: a directory at the save path (or anything else that is
    // not a plain file) fails cleanly here instead, exactly as a rename over it always has, leaving the
    // directory untouched and no ".tmp" behind (tests/test_save.cpp exercises this). For a regular file, move
    // whatever is in the way to "<path>.bak" (replacing an older one of its own) and try again; if the second
    // rename also fails, put the backup back so the write leaves the original rather than nothing.
    const std::error_code firstError = ec;
    std::error_code existsError;
    if (fs::is_regular_file(path, existsError) && keepAsBackup(path)) {
        fs::rename(temp, path, ec);
        if (!ec) {
            return true;
        }
        fs::path backup = path;
        backup += ".bak";
        std::error_code restoreError;
        fs::rename(backup, path, restoreError);
        if (restoreError) {
            logError("cannot restore {} from {} after the retry also failed ({}); the backup is left at {}",
                     path.string(), backup.string(), ec.message(), backup.string());
        }
    }
    logError("cannot replace {}: {}", path.string(), firstError.message());
    fs::remove(temp, ec);
    return false;
}

bool keepAsBackup(const fs::path& path) {
    fs::path backup = path;
    backup += ".bak";
    std::error_code ec;
    fs::rename(path, backup, ec);
    if (ec) {
        logError("cannot move {} to {}: {}", path.string(), backup.string(), ec.message());
        return false;
    }
    return true;
}

}  // namespace mb
