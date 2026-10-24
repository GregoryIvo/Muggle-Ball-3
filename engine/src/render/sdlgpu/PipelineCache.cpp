#include "PipelineCache.h"

#include "mb/engine/Log.h"
#include "mb/engine/Vertex.h"

#include "effect_frag.h"
#include "effect_vert.h"
#include "line_frag.h"
#include "line_vert.h"
#include "mesh_frag.h"
#include "mesh_vert.h"
#include "sprite_frag.h"
#include "sprite_vert.h"

#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>
#include <string_view>

namespace mb {

namespace {

// One shader as built by tools/build_shaders.py: the same program for both back ends.
struct ShaderSource {
    std::span<const std::uint32_t> spirv;
    std::string_view msl;
    const char* mslEntry;
};

SDL_GPUShader* createShader(SDL_GPUDevice* device, SDL_GPUShaderStage stage, const ShaderSource& source,
                            Uint32 samplers, Uint32 uniformBuffers, const char* name) {
    SDL_GPUShaderCreateInfo info{};
    info.stage = stage;
    info.num_samplers = samplers;
    info.num_uniform_buffers = uniformBuffers;

    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
    if (supported & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = reinterpret_cast<const Uint8*>(source.spirv.data());
        info.code_size = source.spirv.size_bytes();
        info.entrypoint = "main";
    } else if (supported & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.code = reinterpret_cast<const Uint8*>(source.msl.data());
        info.code_size = source.msl.size();
        info.entrypoint = source.mslEntry;
    } else {
        throw std::runtime_error("the GPU accepts neither SPIR-V nor MSL shaders");
    }

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    if (!shader) {
        throw std::runtime_error(std::format("creating shader {} failed: {}", name, SDL_GetError()));
    }
    return shader;
}

SDL_GPUCullMode toSdl(CullMode mode) {
    switch (mode) {
        case CullMode::None: return SDL_GPU_CULLMODE_NONE;
        case CullMode::Front: return SDL_GPU_CULLMODE_FRONT;
        case CullMode::Back: break;
    }
    return SDL_GPU_CULLMODE_BACK;
}

SDL_GPUBlendFactor toSdl(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return SDL_GPU_BLENDFACTOR_ZERO;
        case BlendFactor::One: return SDL_GPU_BLENDFACTOR_ONE;
        case BlendFactor::SrcColor: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
        case BlendFactor::InvSrcColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DestAlpha: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
        case BlendFactor::InvDestAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::DestColor: return SDL_GPU_BLENDFACTOR_DST_COLOR;
        case BlendFactor::InvDestColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

SDL_GPUColorTargetBlendState blendState(const render::RenderState& state) {
    SDL_GPUColorTargetBlendState blend{};
    // The alpha channel is never written: it keeps the clear value, so screenshots
    // stay opaque whatever the textures' alpha was.
    blend.enable_color_write_mask = true;
    blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.enable_blend = state.blend;
    blend.src_color_blendfactor = state.blend ? toSdl(state.srcBlend) : SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = state.blend ? toSdl(state.dstBlend) : SDL_GPU_BLENDFACTOR_ZERO;
    return blend;
}

}  // namespace

PipelineCache::PipelineCache(SDL_GPUDevice* device, SDL_GPUTextureFormat depthFormat)
    : device_(device), depthFormat_(depthFormat) {
    try {
        meshVertex_ = createShader(device, SDL_GPU_SHADERSTAGE_VERTEX,
                                   {shaders::mesh_vert_spirv, shaders::mesh_vert_msl, shaders::mesh_vert_msl_entry},
                                   0, 1, "mesh.vert");
        meshFragment_ = createShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                     {shaders::mesh_frag_spirv, shaders::mesh_frag_msl, shaders::mesh_frag_msl_entry},
                                     1, 1, "mesh.frag");
        lineVertex_ = createShader(device, SDL_GPU_SHADERSTAGE_VERTEX,
                                   {shaders::line_vert_spirv, shaders::line_vert_msl, shaders::line_vert_msl_entry},
                                   0, 1, "line.vert");
        lineFragment_ = createShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                     {shaders::line_frag_spirv, shaders::line_frag_msl, shaders::line_frag_msl_entry},
                                     0, 0, "line.frag");
        spriteVertex_ = createShader(device, SDL_GPU_SHADERSTAGE_VERTEX,
                                     {shaders::sprite_vert_spirv, shaders::sprite_vert_msl,
                                      shaders::sprite_vert_msl_entry},
                                     0, 1, "sprite.vert");
        spriteFragment_ = createShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                       {shaders::sprite_frag_spirv, shaders::sprite_frag_msl,
                                        shaders::sprite_frag_msl_entry},
                                       1, 0, "sprite.frag");
        effectVertex_ = createShader(device, SDL_GPU_SHADERSTAGE_VERTEX,
                                     {shaders::effect_vert_spirv, shaders::effect_vert_msl,
                                      shaders::effect_vert_msl_entry},
                                     0, 1, "effect.vert");
        effectFragment_ = createShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                       {shaders::effect_frag_spirv, shaders::effect_frag_msl,
                                        shaders::effect_frag_msl_entry},
                                       1, 1, "effect.frag");
    } catch (...) {
        releaseShaders();
        throw;
    }
}

PipelineCache::~PipelineCache() {
    for (const auto& entry : pipelines_) {
        if (entry.second) {
            SDL_ReleaseGPUGraphicsPipeline(device_, entry.second);
        }
    }
    releaseShaders();
}

void PipelineCache::releaseShaders() {
    for (SDL_GPUShader* shader :
         {meshVertex_, meshFragment_, lineVertex_, lineFragment_, spriteVertex_, spriteFragment_, effectVertex_,
          effectFragment_}) {
        if (shader) {
            SDL_ReleaseGPUShader(device_, shader);
        }
    }
    meshVertex_ = meshFragment_ = lineVertex_ = lineFragment_ = spriteVertex_ = spriteFragment_ = nullptr;
    effectVertex_ = effectFragment_ = nullptr;
}

SDL_GPUGraphicsPipeline* PipelineCache::get(const PipelineKey& key) {
    const auto [it, inserted] = pipelines_.try_emplace(key.pack(), nullptr);
    if (inserted) {
        it->second = create(key);  // a failure is cached too, so it is logged once
    }
    return it->second;
}

SDL_GPUGraphicsPipeline* PipelineCache::create(const PipelineKey& key) {
    SDL_GPUVertexBufferDescription buffer{};
    buffer.slot = 0;
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    // Locations match the shaders' `layout(location = n) in` declarations.
    SDL_GPUVertexAttribute attributes[3]{};
    SDL_GPUShader* vertexShader = meshVertex_;
    SDL_GPUShader* fragmentShader = meshFragment_;
    Uint32 attributeCount = 3;
    const render::RenderState& state = key.state;
    if (state.program == render::Program::Line) {
        buffer.pitch = sizeof(render::LineVertex);
        attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(render::LineVertex, position)};
        attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(render::LineVertex, color)};
        vertexShader = lineVertex_;
        fragmentShader = lineFragment_;
        attributeCount = 2;
    } else if (state.program == render::Program::Effect) {
        buffer.pitch = sizeof(EffectVertex);
        attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(EffectVertex, position)};
        attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(EffectVertex, uv)};
        attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(EffectVertex, color)};
        vertexShader = effectVertex_;
        fragmentShader = effectFragment_;
    } else if (state.program == render::Program::Sprite) {
        buffer.pitch = sizeof(render::SpriteVertex);
        attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(render::SpriteVertex, position)};
        attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(render::SpriteVertex, uv)};
        attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(render::SpriteVertex, color)};
        vertexShader = spriteVertex_;
        fragmentShader = spriteFragment_;
    } else {
        buffer.pitch = sizeof(Vertex);
        attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, position)};
        attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, normal)};
        attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, uv)};
    }

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = key.colorFormat;
    colorTarget.blend_state = blendState(state);

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertexShader;
    info.fragment_shader = fragmentShader;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = attributeCount;
    info.primitive_type =
        state.program == render::Program::Line ? SDL_GPU_PRIMITIVETYPE_LINELIST : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = toSdl(state.cull);
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;  // Direct3D's default
    info.depth_stencil_state.enable_depth_test = state.depthTest;
    info.depth_stencil_state.enable_depth_write = state.depthTest && state.depthWrite;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = depthFormat_;
    info.target_info.has_depth_stencil_target = true;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &info);
    if (!pipeline) {
        logError("creating a graphics pipeline failed: {}", SDL_GetError());
    }
    return pipeline;
}

}  // namespace mb
