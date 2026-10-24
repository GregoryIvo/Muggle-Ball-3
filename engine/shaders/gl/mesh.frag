// Direct3D 8 fixed-function lighting for one directional light, per pixel; see MaterialParams in Renderer.h
// and ../mesh.frag. tools/build_shaders.py makes two programs of this file: one as it is, and one with the
// lines between the ALPHA_TEST markers removed (the discard-free variant, used when a texture has no alpha
// below 255 and the material's alpha is 1, so the PowerVR GPU keeps its hidden-surface removal).
varying vec3 vNormal;
varying vec2 vUv;

uniform sampler2D baseTexture;
uniform vec4 diffuse;
uniform vec4 ambient;
uniform vec4 emissive;
uniform vec4 lightDirection;  // xyz: the direction the light travels, unit length
uniform vec4 lightDiffuse;
uniform vec4 lightAmbient;
uniform vec4 flags;           // x: 1 = lit, y: 1 = alpha test

void main() {
    vec4 texel = texture2D(baseTexture, vUv);
    float alpha = texel.a * diffuse.a;
    // ALPHA_TEST_BEGIN
    if (flags.y > 0.5 && alpha < 0.5) {
        discard;
    }
    // ALPHA_TEST_END

    vec3 rgb;
    if (flags.x > 0.5) {
        float facing = max(dot(normalize(vNormal), -lightDirection.xyz), 0.0);
        vec3 light = emissive.rgb + ambient.rgb * lightAmbient.rgb + diffuse.rgb * lightDiffuse.rgb * facing;
        rgb = texel.rgb * clamp(light, 0.0, 1.0);
    } else {
        rgb = texel.rgb * diffuse.rgb;
    }
    gl_FragColor = vec4(rgb, alpha);
}
