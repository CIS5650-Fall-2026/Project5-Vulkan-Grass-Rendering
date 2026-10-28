#version 450

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec2 skyNdc;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 viewRay = inverse(camera.proj) * vec4(skyNdc, 1.0, 1.0);
    vec3 worldRay = normalize((inverse(camera.view) * vec4(viewRay.xyz, 0.0)).xyz);

    vec3 horizonColor = vec3(0.67, 0.78, 0.88);
    vec3 zenithColor = vec3(0.08, 0.22, 0.43);
    vec3 lowHazeColor = vec3(0.20, 0.28, 0.34);
    float skyHeight = worldRay.y;
    vec3 skyColor = skyHeight >= 0.0
        ? mix(horizonColor, zenithColor, smoothstep(0.0, 0.72, skyHeight))
        : mix(lowHazeColor, horizonColor, smoothstep(-0.24, 0.0, skyHeight));

    vec3 sunDirection = normalize(vec3(-0.38, 0.42, -0.82));
    float sunAlignment = max(dot(worldRay, sunDirection), 0.0);
    float sunDisc = pow(sunAlignment, 180.0);
    float sunGlow = pow(sunAlignment, 12.0) * 0.18;
    skyColor += vec3(1.0, 0.78, 0.48) * (sunDisc + sunGlow);

    outColor = vec4(skyColor, 1.0);
}
