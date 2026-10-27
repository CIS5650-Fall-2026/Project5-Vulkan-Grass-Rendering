#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 1) uniform SunBufferObject {
    vec4 direction;
    vec4 color;
} sun;

layout(location = 0) in vec3 fragDirection;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 viewDir = normalize(fragDirection);
    float daylight = sun.color.w;

    vec3 zenithColor = mix(vec3(0.015, 0.020, 0.060), vec3(0.12, 0.35, 0.70), daylight);
    vec3 horizonColor = mix(vec3(0.030, 0.035, 0.070), vec3(0.65, 0.80, 0.95), daylight);

    float sunset = (1.0 - smoothstep(0.0, 0.35, sun.direction.y)) * daylight;
    horizonColor = mix(horizonColor, vec3(0.95, 0.45, 0.22), sunset);

    float height = sqrt(clamp(viewDir.y, 0.0, 1.0));
    vec3 color = mix(horizonColor, zenithColor, height);

    float sunDisk = smoothstep(cos(0.020), cos(0.015), dot(viewDir, sun.direction.xyz));
    sunDisk *= step(0.0, viewDir.y);
    color = mix(color, sun.color.rgb, sunDisk);

    outColor = vec4(color, 1.0);
}
