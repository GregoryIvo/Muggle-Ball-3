#pragma once

#include <filesystem>

namespace mb {

// Where converted game assets live. Order of precedence:
//   1. the MB3_ASSETS environment variable,
//   2. Resources/assets inside the macOS app bundle,
//   3. ./assets next to the executable's working directory.
std::filesystem::path assetRoot();

}  // namespace mb
