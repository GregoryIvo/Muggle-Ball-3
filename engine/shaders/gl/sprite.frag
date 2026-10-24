// Straight (not premultiplied) alpha, like the meshes: the blend state multiplies by alpha.
varying vec2 vUv;
varying vec4 vColor;

uniform sampler2D baseTexture;

void main() {
    gl_FragColor = texture2D(baseTexture, vUv) * vColor;
}
