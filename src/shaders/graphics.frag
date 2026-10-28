#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"
#include "render_common.glsl"
#include "shadow_common.glsl"

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) flat in uint inMaterial;

layout(location = 0) out vec4 outColor;

// Odd project-seeded arithmetic and alternating shifts spread adjacent cells;
// the upper 24 bits then map exactly into a single-precision unit fraction.
uint projectMix(uint value) {
    value ^= value >> 13u;
    value *= 5651u;
    value ^= value << 11u;
    value *= 7919u;
    value ^= value >> 15u;
    value += 104729u;
    value ^= value << 7u;
    return value;
}

float projectUnit(uint value) {
    return float(projectMix(value) >> 8u) * (1.0 / 16777216.0);
}

float cellVariation(ivec2 cell) {
    uint state = projectMix(uint(cell.x) ^ 5651u);
    state = projectMix(state ^ uint(cell.y) ^ 7919u);
    return projectUnit(state);
}

float smoothCellVariation(vec2 coordinate) {
    ivec2 cell = ivec2(floor(coordinate));
    vec2 offset = fract(coordinate);
    vec2 weight = offset * offset * (3.0 - 2.0 * offset);
    float low = mix(cellVariation(cell),
                    cellVariation(cell + ivec2(1, 0)), weight.x);
    float high = mix(cellVariation(cell + ivec2(0, 1)),
                     cellVariation(cell + ivec2(1, 1)), weight.x);
    return mix(low, high, weight.y);
}

vec3 materialAlbedo() {
    vec3 vertexColor = clamp(inColor, vec3(0.0), vec3(1.0));

    if (inMaterial == 0u) {
        ivec2 cell = ivec2(floor(inWorldPosition.xz * 2.0));
        float broad = cellVariation(ivec2(floor(inWorldPosition.xz * 0.24)));
        float fine = cellVariation(cell);
        vec3 loam = vec3(0.155, 0.205, 0.105);
        vec3 moss = vec3(0.245, 0.335, 0.145);
        vec3 procedural = mix(loam, moss, 0.30 + 0.50 * broad);
        procedural *= mix(0.91, 1.07, fine);
        return mix(procedural, vertexColor, 0.22);
    }

    if (inMaterial == 1u) {
        ivec2 grainCell = ivec2(floor(inWorldPosition.xz * 5.0
                                    + inWorldPosition.y * 3.0));
        float grain = cellVariation(grainCell);
        vec3 stone = mix(vec3(0.29, 0.30, 0.28), vec3(0.43, 0.40, 0.35), grain);
        return mix(stone, vertexColor, 0.25);
    }

    vec3 sphere = vec3(0.55, 0.19, 0.11);
    return mix(sphere, vertexColor, 0.18);
}

vec3 improvedMaterialAlbedo() {
    vec3 vertexColor = clamp(inColor, vec3(0.0), vec3(1.0));
    vec3 mildTint = mix(vec3(1.0), 0.75 + 0.50 * vertexColor, 0.18);

    if (inMaterial == 0u) {
        float broad = smoothCellVariation(inWorldPosition.xz * 0.10
                                          + vec2(3.25, -1.75));
        float detail = smoothCellVariation(inWorldPosition.xz * 0.48
                                           + vec2(-5.5, 8.25));
        vec3 earth = vec3(0.105, 0.135, 0.052);
        vec3 moss = vec3(0.175, 0.245, 0.075);
        vec3 ground = mix(earth, moss, 0.28 + 0.52 * broad);
        ground *= mix(0.94, 1.06, detail);
        return ground * mildTint;
    }

    if (inMaterial == 1u) {
        vec2 stoneCoordinate = inWorldPosition.xz * 0.23
                             + vec2(inWorldPosition.y * 0.17,
                                    -inWorldPosition.y * 0.13);
        float broad = smoothCellVariation(stoneCoordinate + vec2(7.0, 2.5));
        float detail = smoothCellVariation(stoneCoordinate * 3.1
                                           + vec2(-2.0, 11.0));
        vec3 brownGray = vec3(0.245, 0.235, 0.185);
        vec3 oliveGray = vec3(0.355, 0.345, 0.275);
        vec3 stone = mix(brownGray, oliveGray, 0.24 + 0.56 * broad);
        stone *= mix(0.95, 1.05, detail);
        float grounded = mix(0.72, 1.0,
                             smoothstep(0.04, 1.35, inWorldPosition.y));
        return stone * grounded * mildTint;
    }

    vec3 sphere = vec3(0.52, 0.155, 0.075);
    return sphere * mildTint;
}

void main() {
    vec3 rasterNormal = cross(dFdx(inWorldPosition), dFdy(inWorldPosition));
    vec3 normal = renderSafeNormalize(inNormal, vec3(0.0, 1.0, 0.0));
    vec3 shadowNormal = shadowGeometricNormal(rasterNormal, normal);
    vec3 toCamera = frame.cameraTime.xyz - inWorldPosition;
    float cameraDistance = length(toCamera);
    vec3 viewDirection = renderSafeNormalize(toCamera, normal);
    vec3 eyeRay = -viewDirection;
    vec3 sunDirection = renderSunDirection();
    vec3 sunRadiance = renderSunRadiance();
    bool enhanced = enhancedRenderingEnabled();

    vec3 albedo = enhanced ? improvedMaterialAlbedo() : materialAlbedo();
    float sunFacing = dot(normal, sunDirection);
    float wrappedDiffuse = clamp((sunFacing + 0.12) / 1.12, 0.0, 1.0);
    float diffuse = enhanced && inMaterial != 1u
        ? wrappedDiffuse
        : max(sunFacing, 0.0);
    float directStrength = enhanced
        ? (inMaterial == 1u ? 0.90 : 0.78)
        : 0.72;
    float ambientStrength = enhanced ? 0.78 : 0.68;
    float visibility = shadowVisibility(inWorldPosition, shadowNormal,
                                        sunDirection);

    vec3 linearColor = albedo * renderAmbientRadiance(normal) * ambientStrength;
    linearColor += visibility * sunRadiance * albedo
                 * (directStrength * diffuse);

    if (enhanced && renderFeatureEnabled(RENDER_SPECULAR_BIT)) {
        vec3 halfVector = renderSafeNormalize(sunDirection + viewDirection,
                                              sunDirection);
        float gloss = inMaterial == 2u ? 0.18
                    : (inMaterial == 1u ? 0.052 : 0.012);
        float shininess = inMaterial == 2u ? 34.0
                        : (inMaterial == 1u ? 22.0 : 13.0);
        float specular = pow(max(dot(normal, halfVector), 0.0), shininess);
        float distanceFade = 1.0 - smoothstep(18.0,
            max(frame.atmosphere.y * 0.65, 30.0), cameraDistance);
        linearColor += visibility * sunRadiance * vec3(0.78, 0.70, 0.52)
                     * (gloss * specular * distanceFade);
    }

    linearColor = renderApplyFog(linearColor, inWorldPosition, eyeRay,
                                 cameraDistance);
    outColor = vec4(renderToneResponse(linearColor), 1.0);
}
