#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

// Each matrix is a mb::Mat4 uploaded unchanged: row-major with row vectors on the
// CPU, which GLSL reads column-major as the transpose. That transpose is the
// column-vector form, so `matrix * vector` below computes the same thing as the
// CPU's `vector * Matrix`.
layout(set = 1, binding = 0, std140) uniform VertexUniforms {
    mat4 viewProj;
    mat4 world;
    mat4 normalMatrix;  // inverse-transpose of the world matrix, computed on the CPU
} u;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUv;

void main() {
    gl_Position = u.viewProj * (u.world * vec4(inPosition, 1.0));
    outNormal = mat3(u.normalMatrix) * inNormal;
    outUv = inUv;
}
