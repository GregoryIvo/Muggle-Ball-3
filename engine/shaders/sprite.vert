#version 450

layout(location = 0) in vec2 inPosition;  // pixels of the render target, origin top-left, y down
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(set = 1, binding = 0, std140) uniform VertexUniforms {
    vec4 pixelToClip;  // xy: 2 / target size in pixels
} u;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

void main() {
    gl_Position = vec4(inPosition.x * u.pixelToClip.x - 1.0, 1.0 - inPosition.y * u.pixelToClip.y, 0.0, 1.0);
    outUv = inUv;
    outColor = inColor;
}
