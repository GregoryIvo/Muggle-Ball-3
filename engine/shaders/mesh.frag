#version 450

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUv;

layout(set = 2, binding = 0) uniform sampler2D baseTexture;

layout(set = 3, binding = 0, std140) uniform FragmentUniforms {
    vec4 diffuse;
    vec4 ambient;
    vec4 emissive;
    vec4 lightDirection;  // xyz: the direction the light travels, unit length
    vec4 lightDiffuse;
    vec4 lightAmbient;
    vec4 flags;           // x: 1 = lit, y: 1 = alpha test
} u;

layout(location = 0) out vec4 outColor;

// Direct3D 8 fixed-function lighting for one directional light, per pixel; see
// MaterialParams in Renderer.h.
void main() {
    vec4 texel = texture(baseTexture, inUv);
    float alpha = texel.a * u.diffuse.a;
    if (u.flags.y > 0.5 && alpha < 0.5) {
        discard;
    }

    vec3 rgb;
    if (u.flags.x > 0.5) {
        float facing = max(dot(normalize(inNormal), -u.lightDirection.xyz), 0.0);
        vec3 light = u.emissive.rgb + u.ambient.rgb * u.lightAmbient.rgb +
                     u.diffuse.rgb * u.lightDiffuse.rgb * facing;
        rgb = texel.rgb * clamp(light, 0.0, 1.0);
    } else {
        rgb = texel.rgb * u.diffuse.rgb;
    }
    outColor = vec4(rgb, alpha);
}
