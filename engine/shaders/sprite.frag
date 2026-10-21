#version 450

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(set = 2, binding = 0) uniform sampler2D baseTexture;

layout(location = 0) out vec4 outColor;

// Straight (not premultiplied) alpha, like the meshes: the blend state multiplies by alpha.
void main() {
    outColor = texture(baseTexture, inUv) * inColor;
}
