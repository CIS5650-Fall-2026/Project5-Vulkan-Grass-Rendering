#ifndef VULKAN_GRASS_RENDER_COMMON_GLSL
#define VULKAN_GRASS_RENDER_COMMON_GLSL

vec3 renderSafeNormalize(vec3 value, vec3 fallbackValue) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-10
        ? value * inversesqrt(lengthSquared)
        : fallbackValue;
}

vec3 renderSunDirection() {
    return renderSafeNormalize(frame.lighting.xyz,
                               normalize(vec3(-0.4, 0.8, 0.25)));
}

vec3 renderSunRadiance() {
    return max(frame.sunColor.xyz, vec3(0.0)) * max(frame.sunColor.w, 0.0);
}

uint renderMixBits(uint value) {
    value ^= value >> 13u;
    value *= 5651u;
    value ^= value << 11u;
    value *= 7919u;
    value ^= value >> 15u;
    value += 104729u;
    value ^= value << 7u;
    return value;
}

float renderUnit(uint value) {
    return float(renderMixBits(value) >> 8u) * (1.0 / 16777216.0);
}

float renderHash(ivec2 cell) {
    uint state = renderMixBits(uint(cell.x) ^ 5651u);
    state = renderMixBits(state ^ uint(cell.y) ^ 7919u);
    return renderUnit(state ^ 104729u);
}

float renderNoise(vec2 coordinate) {
    ivec2 cell = ivec2(floor(coordinate));
    vec2 offset = fract(coordinate);
    vec2 weight = offset * offset * (3.0 - 2.0 * offset);
    float lower = mix(renderHash(cell), renderHash(cell + ivec2(1, 0)),
                      weight.x);
    float upper = mix(renderHash(cell + ivec2(0, 1)),
                      renderHash(cell + ivec2(1, 1)), weight.x);
    return mix(lower, upper, weight.y);
}

float renderCloudField(vec2 coordinate) {
    float broad = renderNoise(coordinate);
    float medium = renderNoise(coordinate * 2.07 + vec2(3.7, -5.1));
    float detail = renderNoise(coordinate * 4.19 + vec2(-8.3, 2.6));
    return broad * 0.57 + medium * 0.29 + detail * 0.14;
}

vec3 renderSkyRadiance(vec3 worldRay) {
    vec3 direction = renderSafeNormalize(worldRay, vec3(0.0, 0.0, -1.0));
    float skyHeight = clamp(direction.y, 0.0, 1.0);
    float zenithBlend = smoothstep(0.0, 0.86, skyHeight);

    vec3 quietHorizon = vec3(0.39, 0.56, 0.72);
    vec3 quietZenith = vec3(0.075, 0.19, 0.39);
    vec3 radiance = mix(quietHorizon, quietZenith, zenithBlend);
    if (!renderFeatureEnabled(RENDER_SKY_BIT)) {
        return radiance;
    }

    vec3 sunDirection = renderSunDirection();
    vec3 sunRadiance = renderSunRadiance();
    float sunAlignment = max(dot(direction, sunDirection), 0.0);
    float sunDisc = smoothstep(0.99970, 0.99994, sunAlignment);
    float sunGlow = pow(sunAlignment, 36.0);

    float rayHorizontalLength = length(direction.xz);
    float sunHorizontalLength = length(sunDirection.xz);
    float horizonAlignment = 0.0;
    if (rayHorizontalLength > 1.0e-5 && sunHorizontalLength > 1.0e-5) {
        horizonAlignment = max(dot(direction.xz / rayHorizontalLength,
                                   sunDirection.xz / sunHorizontalLength), 0.0);
    }
    float warmHorizon = exp(-abs(direction.y) * 6.5)
                      * pow(horizonAlignment, 5.0);
    radiance += sunRadiance
              * (4.0 * sunDisc + 0.060 * sunGlow + 0.018 * warmHorizon);

    float aboveHorizon = smoothstep(0.015, 0.20, direction.y);
    vec2 drift = vec2(0.010, -0.006) * frame.cameraTime.w;
    vec2 cloudCoordinate = direction.xz
                         * mix(3.2, 1.25, skyHeight) + drift;
    float cloudShape = renderCloudField(cloudCoordinate);
    float cloudDetail = renderNoise(cloudCoordinate * 7.3 + vec2(4.1, 9.7));
    float cloudOpacity = smoothstep(0.54, 0.72,
                                    cloudShape + 0.055 * cloudDetail)
                       * aboveHorizon;
    cloudOpacity *= 1.0 - 0.72 * sunDisc;

    float cloudFacing = 0.5 + 0.5 * max(dot(direction, sunDirection), 0.0);
    vec3 coolCloud = vec3(0.53, 0.61, 0.68);
    vec3 litCloud = mix(vec3(0.78, 0.79, 0.75),
                        max(sunRadiance, vec3(0.82, 0.62, 0.40)),
                        0.20 * cloudFacing);
    vec3 cloudColor = mix(coolCloud, litCloud, 0.38 + 0.42 * cloudFacing);
    return mix(radiance, cloudColor, 0.52 * cloudOpacity);
}

vec3 renderAmbientRadiance(vec3 normal) {
    float upFacing = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 groundBounce = vec3(0.095, 0.085, 0.050);
    vec3 skyFill = renderFeatureEnabled(RENDER_SKY_BIT)
        ? vec3(0.26, 0.37, 0.50)
        : vec3(0.25, 0.28, 0.30);
    return mix(groundBounce, skyFill, 0.28 + 0.72 * upFacing);
}

float renderFogAmount(vec3 worldPosition, float cameraDistance) {
    if (!renderFeatureEnabled(RENDER_FOG_BIT)) {
        return 0.0;
    }

    float fogStart = max(frame.atmosphere.x, 0.0);
    float fogEnd = max(frame.atmosphere.y, fogStart + 1.0e-3);
    float distanceFog = smoothstep(fogStart, fogEnd, cameraDistance);

    float fieldFog = 0.0;
    if (frame.terrain.x > 1.0e-4) {
        float halfField = 0.5 * frame.terrain.x;
        float edgeDistance = max(abs(worldPosition.x), abs(worldPosition.z));
        float edgeBand = smoothstep(0.86, 1.02, edgeDistance / halfField);
        float distantEdge = smoothstep(max(10.0, fogStart * 0.65),
                                       max(fogStart + 10.0, fogEnd * 0.65),
                                       cameraDistance);
        fieldFog = edgeBand * mix(0.12, 0.82, distantEdge);
    }
    return 1.0 - (1.0 - distanceFog) * (1.0 - fieldFog);
}

vec3 renderApplyFog(vec3 linearColor, vec3 worldPosition, vec3 eyeRay,
                    float cameraDistance) {
    float amount = renderFogAmount(worldPosition, cameraDistance);
    if (amount <= 0.0) {
        return linearColor;
    }
    return mix(linearColor, renderSkyRadiance(eyeRay), amount);
}

vec3 renderToneResponse(vec3 linearColor) {
    float exposure = max(frame.lighting.w, 0.0);
    return vec3(1.0) - exp(-max(linearColor, vec3(0.0)) * exposure);
}

#endif
