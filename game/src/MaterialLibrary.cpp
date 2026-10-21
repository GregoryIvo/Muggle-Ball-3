// STUB: replaced by the real implementation.
#include "mb/game/MaterialLibrary.h"

#include <stdexcept>
#include <string>

namespace mb {

BlendMode GameMaterial::blendMode() const { return BlendMode::Opaque; }

MaterialLibrary MaterialLibrary::load(const std::filesystem::path& path) {
    throw std::runtime_error("MaterialLibrary::load not implemented: " + path.string());
}

MaterialLibrary MaterialLibrary::parse(std::string_view) {
    throw std::runtime_error("MaterialLibrary::parse not implemented");
}

const GameMaterial* MaterialLibrary::find(std::string_view) const { return nullptr; }

}  // namespace mb
