// Straight (not premultiplied) alpha, like the meshes: the blend state multiplies by alpha.
// vUv is highp (not the fragment stage's default mediump, see the GL_ES_PREAMBLE's "precision mediump
// float"): UI sprites tile NPOT textures with REPEAT and scroll a 1024-wide one (handoff 4.3), and mediump
// only guarantees a range of 2^14 and a relative precision of 2^-10 (GLSL ES 1.00, 4.5.2: about half a texel on a
// 1024-wide texture near u = 1), enough to visibly misplace texels at the tile edges and
// during scrolling. GLSL ES 1.00 lets the fragment stage declare a varying's precision independently of the
// vertex stage (as long as it is at least as precise, which vec2 vUv there already is by its own default).
varying highp vec2 vUv;
varying vec4 vColor;

uniform sampler2D baseTexture;

void main() {
    gl_FragColor = texture2D(baseTexture, vUv) * vColor;
}
