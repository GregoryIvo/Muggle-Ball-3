// The mesh shader for the GL back end: GLSL ES 1.00, the same program as ../mesh.vert. The build prepends
// the target's preamble (version and precision, see engine/src/render/gl/GlPlatform.h) and
// tools/build_shaders.py embeds the text in engine/shaders/generated/gl_shaders.h.
attribute vec3 inPosition;
attribute vec3 inNormal;
attribute vec2 inUv;

// Each matrix is a mb::Mat4 uploaded unchanged (glUniformMatrix4fv, transpose GL_FALSE): row-major with
// row vectors on the CPU, which GLSL reads column-major as the transpose, the column-vector form, so
// `matrix * vector` below computes the CPU's `vector * Matrix`. viewProj already carries the D3D-to-GL clip
// remap (z' = 2z - w), done once on the CPU.
uniform mat4 viewProj;
uniform mat4 world;
uniform mat4 normalMatrix;  // inverse-transpose of the world matrix, computed on the CPU

varying vec3 vNormal;
varying vec2 vUv;

void main() {
    gl_Position = viewProj * (world * vec4(inPosition, 1.0));
    vNormal = mat3(normalMatrix) * inNormal;
    vUv = inUv;
}
