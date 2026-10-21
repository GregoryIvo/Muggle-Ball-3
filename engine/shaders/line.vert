#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(set = 1, binding = 0, std140) uniform VertexUniforms {
    mat4 viewProj;  // a mb::Mat4 uploaded unchanged, see mesh.vert
} u;

layout(location = 0) out vec4 outColor;

void main() {
    gl_Position = u.viewProj * vec4(inPosition, 1.0);
    outColor = inColor;
}
