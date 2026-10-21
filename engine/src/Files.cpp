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
    if (ec) {
        logError("cannot replace {}: {}", path.string(), ec.message());
        fs::remove(temp, ec);
        return false;
    }
    return true;
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
