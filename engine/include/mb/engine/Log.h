#pragma once

#include <filesystem>
#include <format>
#include <string_view>

namespace mb {

enum class LogLevel { Info, Warn, Error };

void logMessage(LogLevel level, std::string_view message);

// Once set, every message logged from here on (in this call and all later ones) is also appended to `path`,
// opened once and flushed after each message so a log reaches disk even if the process is killed. On the Vita,
// where SDL_Log goes nowhere useful, this is how the hello and the game get a log at all (ux0:data/MuggleBall3/
// log.txt); on the desktop it is additional to SDL's own log, for a bug report. If the file cannot be opened,
// or a write to it fails, logMessage falls back to SDL_Log only, after one warning; it does not try again by
// itself (call setLogFile once more to retry a path). Not calling this at all leaves logging exactly as before.
void setLogFile(const std::filesystem::path& path);

template <class... Args>
void logInfo(std::format_string<Args...> fmt, Args&&... args) {
    logMessage(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void logWarn(std::format_string<Args...> fmt, Args&&... args) {
    logMessage(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void logError(std::format_string<Args...> fmt, Args&&... args) {
    logMessage(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace mb
