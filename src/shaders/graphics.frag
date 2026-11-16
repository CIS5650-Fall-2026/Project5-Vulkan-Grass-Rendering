#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosition;
layout(location = 2) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

// integer hash of a lattice point to [0, 1]
float hash(ivec2 cell) {
    uint n = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u;
    n = (n ^ (n >> 13)) * 1274126177u;
    n ^= n >> 16;
    return float(n) / 4294967295.0;
}

float valueNoise(vec2 p) {
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + ivec2(1, 0)), u.x),
               mix(hash(i + ivec2(0, 1)), hash(i + ivec2(1, 1)), u.x), u.y);
}

void main() {
    // fine detail on top of the per-vertex soil color, which only changes every 0.5 m grid cell
    vec2 p = fragPosition.xz;
    float detail = 0.5 * valueNoise(p * 2.0) + 0.3 * valueNoise(p * 7.0) + 0.2 * valueNoise(p * 23.0);
    vec3 albedo = fragColor * mix(0.75, 1.2, detail);

    vec3 lightDir = normalize(vec3(-0.5, 0.6, 0.6));
    float diffuse = max(dot(normalize(fragNormal), lightDir), 0.0);

    outColor = vec4(albedo * (0.35 + 0.65 * diffuse), 1.0);
}
