#include "mb/engine/Log.h"

#include <SDL3/SDL_log.h>

#include <string>

namespace mb {

void logMessage(LogLevel level, std::string_view message) {
    const std::string text(message);
    switch (level) {
        case LogLevel::Info: SDL_Log("%s", text.c_str()); break;
        case LogLevel::Warn: SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str()); break;
        case LogLevel::Error: SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", text.c_str()); break;
    }
}

}  // namespace mb
