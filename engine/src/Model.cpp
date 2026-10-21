// STUB: replaced by the cgltf-based loader.
#include "mb/engine/Model.h"

#include <stdexcept>

namespace mb {

int Model::findNode(std::string_view) const { return -1; }
Mat4 Model::globalTransform(int) const { return Mat4::identity(); }

Model loadModel(const std::filesystem::path& path) {
    throw std::runtime_error("loadModel not implemented: " + path.string());
}

}  // namespace mb
