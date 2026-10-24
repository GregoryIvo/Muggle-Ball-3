// Straight alpha, like the meshes: the blend state multiplies by alpha. See ../effect.frag.
// highp, not the fragment stage's default mediump: see sprite.frag's comment.
varying highp vec2 vUv;
varying vec4 vColor;

uniform sampler2D baseTexture;
uniform vec4 flags;  // x: 1 = the vertex colour replaces the texture's (EffectBatch::colourOnly)

void main() {
    vec4 texel = texture2D(baseTexture, vUv);
    gl_FragColor = flags.x > 0.5 ? vec4(vColor.rgb, texel.a * vColor.a) : texel * vColor;
}
