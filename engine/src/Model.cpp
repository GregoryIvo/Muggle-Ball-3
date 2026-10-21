#include "mb/engine/Model.h"

#include "mb/engine/Log.h"

#include <cgltf.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace mb {

namespace {

using nlohmann::json;
using GltfPtr = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>;

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& why) {
    throw std::runtime_error(std::format("{}: {}", path.string(), why));
}

const char* resultName(cgltf_result result) {
    switch (result) {
        case cgltf_result_data_too_short: return "file is truncated";
        case cgltf_result_unknown_format: return "not a binary glTF (.glb) file";
        case cgltf_result_invalid_json: return "invalid JSON chunk";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_file_not_found: return "buffer file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "glTF 1.0 is not supported";
        default: return "cannot be parsed";
    }
}

char lowerAscii(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) { return lowerAscii(x) == lowerAscii(y); });
}

// Reads an accessor into a flat float array; throws unless it holds `type` elements.
std::vector<float> readFloats(const std::filesystem::path& path, const cgltf_accessor& accessor,
                              cgltf_type type, const char* what) {
    if (accessor.type != type) {
        fail(path, std::format("{} accessor has the wrong element type", what));
    }
    const cgltf_size count = accessor.count * cgltf_num_components(type);
    std::vector<float> out(count);
    if (cgltf_accessor_unpack_floats(&accessor, out.data(), count) != count) {
        fail(path, std::format("cannot read {} data", what));
    }
    return out;
}

Color rgb(const json& j) {
    if (!j.is_array() || j.size() < 3) {
        return {0, 0, 0, 1};
    }
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), 1};
}

// Resolves a base-colour image uri against the .glb's directory.
std::filesystem::path resolveTexture(const std::filesystem::path& glbPath, const char* uri) {
    std::string decoded(uri);
    decoded.resize(cgltf_decode_uri(decoded.data()));
    return (glbPath.parent_path() / std::filesystem::path(decoded)).lexically_normal();
}

ModelMaterial convertMaterial(const std::filesystem::path& glbPath, const cgltf_material& src) {
    ModelMaterial out;
    if (src.name) {
        out.name = src.name;
    }
    if (src.has_pbr_metallic_roughness) {
        const cgltf_float* c = src.pbr_metallic_roughness.base_color_factor;
        out.diffuse = {c[0], c[1], c[2], c[3]};
    }

    if (src.extras.data) {
        const json extras = json::parse(src.extras.data, nullptr, false);
        if (extras.is_object()) {
            if (auto it = extras.find("power"); it != extras.end() && it->is_number()) {
                out.power = it->get<float>();
            }
            if (auto it = extras.find("specular"); it != extras.end()) {
                out.specular = rgb(*it);
            }
            if (auto it = extras.find("emissive"); it != extras.end()) {
                out.emissive = rgb(*it);
            }
            if (auto it = extras.find("textureFile"); it != extras.end() && it->is_string()) {
                out.textureFile = it->get<std::string>();
            }
        } else {
            logWarn("{}: material '{}' has unreadable extras", glbPath.string(), out.name);
        }
    }

    const cgltf_texture* texture =
        src.has_pbr_metallic_roughness ? src.pbr_metallic_roughness.base_color_texture.texture : nullptr;
    if (texture && texture->image && texture->image->uri) {
        const std::filesystem::path resolved = resolveTexture(glbPath, texture->image->uri);
        std::error_code ec;
        if (std::filesystem::is_regular_file(resolved, ec)) {
            out.texturePath = resolved;
        } else {
            logWarn("{}: material '{}': texture file not found: {}", glbPath.string(), out.name,
                    resolved.string());
        }
    } else if (!out.textureFile.empty()) {
        logWarn("{}: material '{}': texture '{}' was missing when the mesh was converted", glbPath.string(),
                out.name, out.textureFile);
    }
    return out;
}

// Appends one primitive's vertices and (absolute) indices to the model's pools.
ModelPrimitive appendPrimitive(const std::filesystem::path& path, const cgltf_data& data,
                               const cgltf_primitive& prim, Model& model) {
    if (prim.type != cgltf_primitive_type_triangles) {
        fail(path, "only triangle primitives are supported");
    }
    const cgltf_accessor* position = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0);
    if (!position) {
        fail(path, "primitive has no POSITION attribute");
    }
    const cgltf_accessor* normal = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0);
    const cgltf_accessor* texcoord = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0);
    if ((normal && normal->count != position->count) || (texcoord && texcoord->count != position->count)) {
        fail(path, "primitive attributes have different vertex counts");
    }

    const std::vector<float> positions = readFloats(path, *position, cgltf_type_vec3, "POSITION");
    std::vector<float> normals, uvs;
    if (normal) {
        normals = readFloats(path, *normal, cgltf_type_vec3, "NORMAL");
    }
    if (texcoord) {
        uvs = readFloats(path, *texcoord, cgltf_type_vec2, "TEXCOORD_0");
    }

    const std::uint32_t baseVertex = static_cast<std::uint32_t>(model.vertices.size());
    for (cgltf_size i = 0; i < position->count; ++i) {
        Vertex v;
        v.position = {positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]};
        v.normal = normal ? Vec3{normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]} : Vec3{0, 1, 0};
        v.uv = texcoord ? Vec2{uvs[i * 2], uvs[i * 2 + 1]} : Vec2{0, 0};
        model.vertices.push_back(v);
    }

    ModelPrimitive out;
    out.firstIndex = static_cast<std::uint32_t>(model.indices.size());
    const cgltf_size indexCount = prim.indices ? prim.indices->count : position->count;
    if (indexCount % 3 != 0) {
        fail(path, "primitive index count is not a multiple of 3");
    }
    out.indexCount = static_cast<std::uint32_t>(indexCount);
    if (prim.indices) {
        std::vector<std::uint32_t> local(indexCount);
        if (cgltf_accessor_unpack_indices(prim.indices, local.data(), sizeof(std::uint32_t), indexCount) !=
            indexCount) {
            fail(path, "cannot read index data");
        }
        for (const std::uint32_t index : local) {
            if (index >= position->count) {
                fail(path, "primitive index is out of range");
            }
            model.indices.push_back(baseVertex + index);
        }
    } else {
        for (cgltf_size i = 0; i < indexCount; ++i) {
            model.indices.push_back(baseVertex + static_cast<std::uint32_t>(i));
        }
    }
    out.material = prim.material ? static_cast<int>(cgltf_material_index(&data, prim.material)) : -1;
    return out;
}

}  // namespace

int Model::findNode(std::string_view name) const {
    if (name.empty()) {
        return -1;  // unnamed frames cannot be looked up by name
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (equalsIgnoreCase(nodes[i].name, name)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Mat4 Model::globalTransform(int node) const {
    if (node < 0 || node >= static_cast<int>(nodes.size())) {
        return Mat4::identity();
    }
    Mat4 result = nodes[node].local;
    for (int p = nodes[node].parent; p >= 0; p = nodes[p].parent) {
        result = result * nodes[p].local;
    }
    return result;
}

Model loadModel(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail(path, "cannot open model file");
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    cgltf_options options{};
    options.type = cgltf_file_type_glb;
    cgltf_data* raw = nullptr;
    if (const cgltf_result r = cgltf_parse(&options, bytes.data(), bytes.size(), &raw);
        r != cgltf_result_success) {
        fail(path, resultName(r));
    }
    const GltfPtr data(raw, cgltf_free);
    if (const cgltf_result r = cgltf_load_buffers(&options, raw, path.string().c_str());
        r != cgltf_result_success) {
        fail(path, resultName(r));
    }
    if (const cgltf_result r = cgltf_validate(raw); r != cgltf_result_success) {
        fail(path, resultName(r));
    }

    Model model;
    model.materials.reserve(raw->materials_count);
    for (cgltf_size i = 0; i < raw->materials_count; ++i) {
        model.materials.push_back(convertMaterial(path, raw->materials[i]));
    }

    model.meshes.resize(raw->meshes_count);
    for (cgltf_size i = 0; i < raw->meshes_count; ++i) {
        const cgltf_mesh& mesh = raw->meshes[i];
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            model.meshes[i].primitives.push_back(appendPrimitive(path, *raw, mesh.primitives[p], model));
        }
    }

    model.nodes.resize(raw->nodes_count);
    for (cgltf_size i = 0; i < raw->nodes_count; ++i) {
        const cgltf_node& src = raw->nodes[i];
        ModelNode& node = model.nodes[i];
        if (src.name) {
            node.name = src.name;
        }
        float local[16];
        cgltf_node_transform_local(&src, local);
        node.local = Mat4::fromRowMajor(local);
        node.parent = src.parent ? static_cast<int>(cgltf_node_index(raw, src.parent)) : -1;
        for (cgltf_size c = 0; c < src.children_count; ++c) {
            node.children.push_back(static_cast<int>(cgltf_node_index(raw, src.children[c])));
        }
        node.mesh = src.mesh ? static_cast<int>(cgltf_mesh_index(raw, src.mesh)) : -1;
    }

    const cgltf_scene* scene = raw->scene ? raw->scene : (raw->scenes_count > 0 ? &raw->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
            model.roots.push_back(static_cast<int>(cgltf_node_index(raw, scene->nodes[i])));
        }
    } else {
        for (std::size_t i = 0; i < model.nodes.size(); ++i) {
            if (model.nodes[i].parent < 0) {
                model.roots.push_back(static_cast<int>(i));
            }
        }
    }
    return model;
}

}  // namespace mb
