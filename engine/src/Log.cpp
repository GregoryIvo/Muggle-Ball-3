#include "mb/engine/Log.h"

#include <SDL3/SDL_log.h>

#if defined(__vita__)
#include <psp2/kernel/clib.h>
#endif

#include <cstdio>
#include <mutex>
#include <string>
#include <system_error>

namespace mb {

namespace {

std::mutex gLogFileMutex;
std::FILE* gLogFile = nullptr;
bool gLogFileWarned = false;  // true once the one allowed SDL_Log warning about the file has been given

const char* levelPrefix(LogLevel level) {
    switch (level) {
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

}  // namespace

void setLogFile(const std::filesystem::path& path) {
    const std::string pathText = path.string();
    std::lock_guard<std::mutex> lock(gLogFileMutex);
    if (gLogFile != nullptr) {
        std::fclose(gLogFile);
        gLogFile = nullptr;
    }
    gLogFileWarned = false;
    // A fresh console has no ux0:data/MuggleBall3/ yet; fopen's "a" mode never creates missing directories, only
    // the final file, so without this the very first run would get no log at all. A failure here is harmless:
    // it falls through to fopen, which then fails too and gives the existing warning below.
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    gLogFile = std::fopen(pathText.c_str(), "a");
    if (gLogFile == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "cannot open the log file %s", pathText.c_str());
        gLogFileWarned = true;
    }
}

void logMessage(LogLevel level, std::string_view message) {
    const std::string text(message);
    switch (level) {
        case LogLevel::Info: SDL_Log("%s", text.c_str()); break;
        case LogLevel::Warn: SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str()); break;
        case LogLevel::Error: SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str()); break;
    }
#if defined(__vita__)
    // SDL_Log has nowhere useful to go on the Vita; print through the system's own debug output too.
    sceClibPrintf("%s: %s\n", levelPrefix(level), text.c_str());
#endif
    std::lock_guard<std::mutex> lock(gLogFileMutex);
    if (gLogFile == nullptr) {
        return;
    }
    const bool wrote = std::fprintf(gLogFile, "%s: %s\n", levelPrefix(level), text.c_str()) >= 0;
    if (!wrote || std::fflush(gLogFile) != 0) {
        if (!gLogFileWarned) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "the log file stopped accepting writes");
            gLogFileWarned = true;
        }
    }
}

}  // namespace mb
