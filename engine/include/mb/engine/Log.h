#pragma once

#include <format>
#include <string_view>

namespace mb {

enum class LogLevel { Info, Warn, Error };

void logMessage(LogLevel level, std::string_view message);

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
