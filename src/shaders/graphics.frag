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
layout(location = 2) in vec3 inColor;
layout(location = 3) flat in uint inMaterial;

layout(location = 0) out vec4 outColor;

const uint FEATURE_BETTER_RENDERING = 4u;

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

vec3 basicSkyColor(vec3 direction) {
    float upAmount = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.72, 0.79, 0.83);
    vec3 zenith = vec3(0.28, 0.47, 0.66);
    return mix(horizon, zenith, smoothstep(0.0, 0.8, upAmount));
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
    vec3 normal = dot(inNormal, inNormal) > 1.0e-10
        ? normalize(inNormal)
        : vec3(0.0, 1.0, 0.0);
    vec3 toCamera = frame.cameraTime.xyz - inWorldPosition;
    float cameraDistance = length(toCamera);
    vec3 viewDirection = cameraDistance > 1.0e-5
        ? toCamera / cameraDistance
        : normal;
    vec3 sunDirection = dot(frame.lighting.xyz, frame.lighting.xyz) > 1.0e-10
        ? normalize(frame.lighting.xyz)
        : normalize(vec3(-0.4, 0.8, 0.25));

    vec3 eyeRay = cameraDistance > 1.0e-5
        ? -viewDirection
        : vec3(0.0, 0.0, -1.0);

    if ((frame.flags.w & FEATURE_BETTER_RENDERING) == 0u) {
        float diffuse = max(dot(normal, sunDirection), 0.0);
        float skyFill = 0.16 + 0.16 * clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 albedo = materialAlbedo();
        vec3 color = albedo * (skyFill + 0.76 * diffuse);
        float fogStart = max(frame.viewport.z, 4.0);
        float fogEnd = max(frame.parameters.y, fogStart + 1.0);
        float fog = smoothstep(fogStart, fogEnd * 1.15, cameraDistance);
        color = mix(color, basicSkyColor(eyeRay), fog * 0.78);
        float exposure = max(frame.lighting.w, 0.0);
        outColor = vec4(max(color * exposure, vec3(0.0)), 1.0);
        return;
    }

    vec3 albedo = improvedMaterialAlbedo();
    float upFacing = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    float sunFacing = dot(normal, sunDirection);
    float wrappedDiffuse = clamp((sunFacing + 0.12) / 1.12, 0.0, 1.0);
    float directDiffuse = inMaterial == 1u
        ? max(sunFacing, 0.0)
        : wrappedDiffuse;
    float ambient = 0.18 + 0.12 * upFacing;
    float directStrength = inMaterial == 1u ? 0.86 : 0.74;
    vec3 color = albedo * (ambient + directStrength * directDiffuse);

    vec3 halfVector = sunDirection + viewDirection;
    float halfLengthSquared = dot(halfVector, halfVector);
    float gloss = inMaterial == 2u ? 0.16 : (inMaterial == 1u ? 0.045 : 0.010);
    float shininess = inMaterial == 2u ? 32.0 : (inMaterial == 1u ? 22.0 : 12.0);
    float specular = halfLengthSquared > 1.0e-10
        ? pow(max(dot(normal, halfVector * inversesqrt(halfLengthSquared)), 0.0),
              shininess)
        : 0.0;
    float specularFade = 1.0 - smoothstep(18.0,
        max(frame.parameters.y, 24.0), cameraDistance);
    color += vec3(0.82, 0.72, 0.54) * gloss * specular * specularFade;

    float fog = atmosphereFog(inWorldPosition, cameraDistance);
    color = mix(color, skyRadiance(eyeRay), fog);
    outColor = vec4(exposureToneMap(color), 1.0);
}
