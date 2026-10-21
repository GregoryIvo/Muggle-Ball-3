#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace mb {

// ASCII-only lowercase: the game's asset names are ASCII, and this keeps lookups
// identical on every platform and locale.
inline std::string lowerAscii(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace mb
