#include "mb/engine/Paths.h"

#include <SDL3/SDL_filesystem.h>

#include <cstdlib>

namespace mb {

std::filesystem::path assetRoot() {
    if (const char* env = std::getenv("MB3_ASSETS"); env && *env) {
        return env;
    }
    if (const char* base = SDL_GetBasePath()) {
        // On macOS SDL_GetBasePath() points at Contents/Resources/.
        std::filesystem::path bundled = std::filesystem::path(base) / "assets";
        if (std::filesystem::exists(bundled)) {
            return bundled;
        }
    }
    return std::filesystem::current_path() / "assets";
}

}  // namespace mb
