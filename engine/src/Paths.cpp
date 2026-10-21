#include "mb/engine/Paths.h"

#include <SDL3/SDL_filesystem.h>

#include <cstdlib>
#include <system_error>

namespace mb {

std::filesystem::path assetRoot() {
    if (const char* env = std::getenv("MB3_ASSETS"); env && *env) {
        return env;
    }
    std::error_code ec;  // an unreadable candidate is just one that does not exist
    if (const char* base = SDL_GetBasePath()) {
        // On macOS SDL_GetBasePath() points at Contents/Resources/.
        std::filesystem::path bundled = std::filesystem::path(base) / "assets";
        if (std::filesystem::exists(bundled, ec)) {
            return bundled;
        }
    }
    std::filesystem::path local = std::filesystem::current_path() / "assets";
    if (std::filesystem::exists(local, ec)) {
        return local;
    }
    if (const std::filesystem::path user = userDataDir(); !user.empty() && std::filesystem::exists(user / "assets", ec)) {
        return user / "assets";
    }
    return local;
}

std::filesystem::path userDataDir() {
    if (const char* env = std::getenv("MB3_USER_DATA"); env && *env) {
        std::error_code ec;
        std::filesystem::create_directories(env, ec);
        return env;
    }
    char* dir = SDL_GetPrefPath("", "MuggleBall3");
    if (!dir) {
        return {};
    }
    std::filesystem::path path(dir);  // SDL ends it with a separator
    SDL_free(dir);
    return path.has_filename() ? path : path.parent_path();
}

}  // namespace mb
