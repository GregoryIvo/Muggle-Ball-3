#version 450

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(set = 2, binding = 0) uniform sampler2D baseTexture;

layout(set = 3, binding = 0, std140) uniform FragmentUniforms {
    vec4 flags;  // x: 1 = the vertex colour replaces the texture's (EffectBatch::colourOnly)
} u;

layout(location = 0) out vec4 outColor;

// Straight alpha, like the meshes: the blend state multiplies by alpha.
void main() {
    vec4 texel = texture(baseTexture, inUv);
    outColor = u.flags.x > 0.5 ? vec4(inColor.rgb, texel.a * inColor.a) : texel * inColor;
}
