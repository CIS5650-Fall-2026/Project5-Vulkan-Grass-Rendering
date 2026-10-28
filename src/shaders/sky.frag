#version 450

layout(set = 0, binding = 0, std140) uniform FrameUniform {
    mat4 view;
    mat4 proj;
    vec4 cameraTime;
    vec4 wind;
    vec4 gravity;
    vec4 collider;
    vec4 parameters;
    vec4 viewport;
    uvec4 flags;
    vec4 lighting;
    vec4 terrain;
} frame;

layout(location = 0) in vec3 inWorldRay;
layout(location = 0) out vec4 outColor;

const uint FEATURE_BETTER_RENDERING = 4u;

vec3 safeNormalize(vec3 value, vec3 fallback) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-10 ? value * inversesqrt(lengthSquared) : fallback;
}

vec3 skyRadiance(vec3 worldRay) {
    float rayLengthSquared = dot(worldRay, worldRay);
    vec3 direction = rayLengthSquared > 1.0e-10
        ? worldRay * inversesqrt(rayLengthSquared)
        : vec3(0.0, 0.0, -1.0);

    vec3 horizon = vec3(0.36, 0.62, 0.88);
    vec3 zenith = vec3(0.055, 0.22, 0.52);
    float zenithBlend = smoothstep(0.0, 0.88, clamp(direction.y, 0.0, 1.0));
    vec3 radiance = mix(horizon, zenith, zenithBlend);

    float sunLengthSquared = dot(frame.lighting.xyz, frame.lighting.xyz);
    vec3 sunDirection = sunLengthSquared > 1.0e-10
        ? frame.lighting.xyz * inversesqrt(sunLengthSquared)
        : normalize(vec3(-0.4, 0.8, 0.25));
    float sunAlignment = max(dot(direction, sunDirection), 0.0);
    float disc = smoothstep(0.99972, 0.99993, sunAlignment);
    float glow = pow(sunAlignment, 48.0);
    float rayHorizontalSquared = dot(direction.xz, direction.xz);
    float sunHorizontalSquared = dot(sunDirection.xz, sunDirection.xz);
    float horizonAlignment = rayHorizontalSquared > 1.0e-10
                          && sunHorizontalSquared > 1.0e-10
        ? max(dot(direction.xz, sunDirection.xz)
              * inversesqrt(rayHorizontalSquared * sunHorizontalSquared), 0.0)
        : 0.0;
    float horizonHaze = exp(-abs(direction.y) * 7.0)
                      * pow(horizonAlignment, 6.0);
    radiance += vec3(1.16, 0.86, 0.53)
              * (0.55 * disc + 0.08 * glow + 0.04 * horizonHaze);
    return radiance;
}

vec3 exposureToneMap(vec3 linearColor) {
    float exposure = max(frame.lighting.w, 0.0);
    return vec3(1.0) - exp(-max(linearColor, vec3(0.0)) * exposure);
}

void main() {
    vec3 worldRay = safeNormalize(inWorldRay, vec3(0.0, 0.0, -1.0));

    if ((frame.flags.w & FEATURE_BETTER_RENDERING) == 0u) {
        float elevation = clamp(worldRay.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 horizon = vec3(0.72, 0.79, 0.83);
        vec3 zenith = vec3(0.28, 0.47, 0.66);
        vec3 color = mix(horizon, zenith, smoothstep(0.0, 0.82, elevation));
        float exposure = max(frame.lighting.w, 0.0);
        outColor = vec4(max(color * exposure, vec3(0.0)), 1.0);
        return;
    }

    outColor = vec4(exposureToneMap(skyRadiance(worldRay)), 1.0);
}
