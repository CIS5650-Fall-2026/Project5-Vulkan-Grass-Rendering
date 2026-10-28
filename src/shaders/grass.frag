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

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inBladeHeight;
layout(location = 3) in float inVariation;
layout(location = 4) in vec3 inTangent;
layout(location = 5) in float inBladeAcross;

layout(location = 0) out vec4 outColor;

const uint FEATURE_BETTER_RENDERING = 4u;

vec3 safeNormalize(vec3 value, vec3 fallback) {
    float magnitude = length(value);
    return magnitude > 1e-6 ? value / magnitude : fallback;
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

float atmosphereFog(vec3 worldPosition, float cameraDistance) {
    float maximumDistance = max(frame.parameters.y, 1.0);
    float fogStart = clamp(maximumDistance * 0.28, 12.0, 16.0);
    float fogEnd = max(maximumDistance + 4.0, fogStart + 8.0);
    float distanceFog = smoothstep(fogStart, fogEnd, cameraDistance);

    float edgeFog = 0.0;
    if (frame.terrain.x > 1.0e-4) {
        float halfField = 0.5 * frame.terrain.x;
        float edgeDistance = max(abs(worldPosition.x), abs(worldPosition.z));
        edgeFog = smoothstep(0.80, 0.995, edgeDistance / halfField);
    }
    return 1.0 - (1.0 - distanceFog) * (1.0 - edgeFog);
}

vec3 exposureToneMap(vec3 linearColor) {
    float exposure = max(frame.lighting.w, 0.0);
    return vec3(1.0) - exp(-max(linearColor, vec3(0.0)) * exposure);
}

vec3 basicShading(vec3 normal, vec3 sunDirection) {
    float height = clamp(inBladeHeight, 0.0, 1.0);
    vec3 baseColor = mix(vec3(0.12, 0.31, 0.065), vec3(0.32, 0.47, 0.11), height);
    baseColor *= mix(0.88, 1.10, inVariation);
    float lambert = max(dot(normal, sunDirection), 0.0);
    return baseColor * (0.28 + 0.72 * lambert);
}

vec3 improvedShading(vec3 normal, vec3 sunDirection, vec3 viewDirection) {
    float height = clamp(inBladeHeight, 0.0, 1.0);
    float across = clamp(inBladeAcross, -1.0, 1.0);
    vec3 tangent = safeNormalize(inTangent, vec3(0.0, 1.0, 0.0));
    vec3 lateral = safeNormalize(cross(tangent, normal), vec3(1.0, 0.0, 0.0));

    float roundCenter = sqrt(max(1.0 - 0.72 * across * across, 0.0));
    vec3 roundedNormal = safeNormalize(normal * roundCenter + lateral * (0.58 * across), normal);

    vec3 rootColor = vec3(0.052, 0.125, 0.022);
    vec3 middleColor = vec3(0.145, 0.355, 0.055);
    vec3 upperColor = vec3(0.225, 0.405, 0.075);
    vec3 goldColor = vec3(0.405, 0.345, 0.080);
    vec3 bladeColor = height < 0.56
        ? mix(rootColor, middleColor, height / 0.56)
        : mix(middleColor, upperColor, (height - 0.56) / 0.44);

    float warmVariation = inVariation * 2.0 - 1.0;
    float warmTip = smoothstep(0.68, 0.98, height)
                  * smoothstep(0.58, 0.90, inVariation);
    bladeColor = mix(bladeColor, goldColor, 0.68 * warmTip);
    bladeColor *= 1.0 + 0.075 * warmVariation;
    bladeColor += vec3(0.010, 0.006, -0.004) * warmVariation * height;

    float frontLight = max(dot(roundedNormal, sunDirection), 0.0);
    float backLight = max(dot(-roundedNormal, sunDirection), 0.0);
    float diffuse = max(frontLight, 0.28 * backLight);
    float transmission = pow(backLight, 1.5) * (0.09 + 0.15 * height);

    float cameraDistance = length(frame.cameraTime.xyz - inWorldPosition);
    float specularNear = max(frame.viewport.z, 0.0);
    float specularFar = max(frame.viewport.w, specularNear + 1e-3);
    float specularFade = 1.0 - smoothstep(specularNear, specularFar, cameraDistance);
    vec3 halfVector = safeNormalize(sunDirection + viewDirection, sunDirection);
    float specular = pow(max(dot(roundedNormal, halfVector), 0.0), 32.0)
                   * 0.07 * specularFade;

    float contact = mix(0.72, 1.0, smoothstep(0.0, 0.22, height));
    vec3 color = bladeColor * (0.25 + 0.78 * diffuse + transmission) * contact;
    color += vec3(0.76, 0.68, 0.40) * specular * smoothstep(0.18, 0.64, height);

    float fogAmount = atmosphereFog(inWorldPosition, cameraDistance);
    vec3 eyeRay = safeNormalize(inWorldPosition - frame.cameraTime.xyz,
                                vec3(0.0, 0.0, -1.0));
    return mix(color, skyRadiance(eyeRay), fogAmount);
}

void main() {
    vec3 geometricNormal = safeNormalize(inNormal, vec3(0.0, 1.0, 0.0));
    vec3 twoSidedNormal = gl_FrontFacing ? geometricNormal : -geometricNormal;
    vec3 sunDirection = safeNormalize(frame.lighting.xyz, vec3(0.0, 1.0, 0.0));
    vec3 viewDirection = safeNormalize(frame.cameraTime.xyz - inWorldPosition, twoSidedNormal);

    bool betterRendering = (frame.flags.w & FEATURE_BETTER_RENDERING) != 0u;
    vec3 color;
    if (betterRendering) {
        color = exposureToneMap(
            improvedShading(twoSidedNormal, sunDirection, viewDirection));
    } else {
        color = basicShading(twoSidedNormal, sunDirection);
        float exposure = max(frame.lighting.w, 0.0);
        color = vec3(1.0) - exp(-max(color, vec3(0.0)) * exposure);
    }
    outColor = vec4(color, 1.0);
}
