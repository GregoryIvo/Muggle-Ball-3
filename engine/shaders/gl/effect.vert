// Effect triangles, built on the CPU in world space; see ../effect.vert.
attribute vec3 inPosition;
attribute vec2 inUv;
attribute vec4 inColor;

uniform mat4 viewProj;  // a mb::Mat4 uploaded unchanged, with the clip remap; see mesh.vert

varying vec2 vUv;
varying vec4 vColor;

void main() {
    gl_Position = viewProj * vec4(inPosition, 1.0);
    vUv = inUv;
    vColor = inColor;
}
