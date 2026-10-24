// 2D sprites in pixels of the render target, origin top-left, y down; see ../sprite.vert.
attribute vec2 inPosition;
attribute vec2 inUv;
attribute vec4 inColor;

uniform vec4 pixelToClip;  // xy: 2 / target size in pixels

varying vec2 vUv;
varying vec4 vColor;

void main() {
    gl_Position = vec4(inPosition.x * pixelToClip.x - 1.0, 1.0 - inPosition.y * pixelToClip.y, 0.0, 1.0);
    vUv = inUv;
    vColor = inColor;
}
