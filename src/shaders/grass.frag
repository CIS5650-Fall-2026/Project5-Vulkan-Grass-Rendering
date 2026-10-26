#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec2 bladeUV;
layout(location = 2) flat in float colorVariation;
layout(location = 3) flat in vec2 detailInfo;

layout(location = 0) out vec4 outColor;

void main() {
    if (detailInfo.y > 0.5) {
        vec3 low = vec3(0.10, 0.30, 0.95), middle = vec3(0.10, 0.80, 0.35), high = vec3(0.95, 0.18, 0.06);
        float level = detailInfo.x;
        outColor = vec4(level < 0.5 ? mix(low, middle, 2.0 * level) : mix(middle, high, 2.0 * level - 1.0), 1.0);
        return;
    }
    vec3 rootColor = vec3(0.035, 0.16, 0.018);
    vec3 tipColor = vec3(0.40, 0.68, 0.12);
    vec3 color = mix(rootColor, tipColor, smoothstep(0.0, 1.0, bladeUV.y));
    color *= mix(0.85, 1.12, colorVariation);
    vec3 lightDirection = normalize(vec3(-0.4, 0.8, 0.5));
    // A thin leaf receives light from both sides; back faces remain visible.
    float diffuse = abs(dot(normalize(worldNormal), lightDirection));
    float centerHighlight = 0.85 + 0.15 * sin(3.14159265 * bladeUV.x);
    outColor = vec4(color * (0.45 + 0.55 * diffuse) * centerHighlight, 1.0);
}
