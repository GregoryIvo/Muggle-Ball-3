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
    // current_path() throws where there is no working directory (the Vita's app0:/ sandbox); the error_code
    // overload reports that instead, and the candidate is simply skipped.
    std::filesystem::path local;
    if (const std::filesystem::path cwd = std::filesystem::current_path(ec); !ec) {
        local = cwd / "assets";
        if (std::filesystem::exists(local, ec)) {
            return local;
        }
    }
    const std::filesystem::path user = userDataDir();
    if (!user.empty() && std::filesystem::exists(user / "assets", ec)) {
        return user / "assets";
    }
    // Last resort, as before: a path the caller fails to find, named after whichever candidate is least unlikely.
    return !user.empty() ? user / "assets" : (!local.empty() ? local : std::filesystem::path("assets"));
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
