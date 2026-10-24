// Debug lines: a world position and a colour; see ../line.vert.
attribute vec3 inPosition;
attribute vec4 inColor;

uniform mat4 viewProj;  // a mb::Mat4 uploaded unchanged, with the clip remap; see mesh.vert

varying vec4 vColor;

void main() {
    gl_Position = viewProj * vec4(inPosition, 1.0);
    vColor = inColor;
}
