#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"

layout(quads, equal_spacing, ccw) in;

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out float outBladeHeight;
layout(location = 3) out float outVariation;
layout(location = 4) out vec3 outTangent;
layout(location = 5) out float outBladeAcross;
layout(location = 6) out vec3 outLateral;

const float FRAME_EPSILON = 1.0e-6;

bool finiteFloat(float value) {
    return !isnan(value) && !isinf(value);
}

bool finiteVec3(vec3 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

vec3 safeNormalize(vec3 value, vec3 fallback) {
    float lengthSquared = dot(value, value);
    if (!finiteVec3(value) || !finiteFloat(lengthSquared)
            || lengthSquared <= FRAME_EPSILON * FRAME_EPSILON) {
        return fallback;
    }
    return value * inversesqrt(lengthSquared);
}

void tangentFrame(vec3 surfaceUp, float angle, out vec3 lateral, out vec3 front) {
    vec3 reference = abs(surfaceUp.y) < 0.95
        ? vec3(0.0, 1.0, 0.0)
        : vec3(1.0, 0.0, 0.0);
    vec3 base = safeNormalize(cross(reference, surfaceUp), vec3(1.0, 0.0, 0.0));
    vec3 second = safeNormalize(cross(surfaceUp, base), vec3(0.0, 0.0, 1.0));
    lateral = safeNormalize(cos(angle) * base + sin(angle) * second, base);
    front = safeNormalize(cross(lateral, surfaceUp), vec3(0.0, 0.0, 1.0));
}

vec3 rotateAroundAxis(vec3 value, vec3 axis, float cosine, float sine) {
    return value * cosine
         + cross(axis, value) * sine
         + axis * dot(axis, value) * (1.0 - cosine);
}

vec3 degenerateCurveTangent(float bladeHeight, vec3 firstHandle,
                            vec3 secondHandle, vec3 surfaceUp) {
    if (bladeHeight <= FRAME_EPSILON) {
        return safeNormalize(secondHandle, surfaceUp);
    }
    if (bladeHeight >= 1.0 - FRAME_EPSILON) {
        return safeNormalize(firstHandle, surfaceUp);
    }
    return surfaceUp;
}

// Course-seeded odd multiplies and alternating xor shifts diffuse adjacent inputs.
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

float stableVariation(vec3 root, float angle) {
    uint state = projectMix(floatBitsToUint(root.x) ^ 5650u);
    state = projectMix(state ^ (floatBitsToUint(root.y) + 17u));
    state = projectMix(state ^ (floatBitsToUint(root.z) + 1013u));
    state = projectMix(state ^ (floatBitsToUint(angle) + 7919u));
    return projectUnit(state);
}

void main() {
    float bladeAcross = gl_TessCoord.x * 2.0 - 1.0;
    float bladeHeight = clamp(gl_TessCoord.y, 0.0, 1.0);

    vec3 root = finiteVec3(inV0[0].xyz) ? inV0[0].xyz : vec3(0.0);
    vec3 surfaceUp = safeNormalize(inUp[0].xyz, vec3(0.0, 1.0, 0.0));
    vec3 sourceGuide = finiteVec3(inV1[0].xyz)
        ? inV1[0].xyz
        : root + surfaceUp;
    vec3 tip = finiteVec3(inV2[0].xyz)
        ? inV2[0].xyz
        : root + surfaceUp;

    // The paper guide is constrained to the root/up line, keeping the curve planar.
    vec3 guide = root + surfaceUp * dot(sourceGuide - root, surfaceUp);

    vec3 baseLateral;
    vec3 baseFront;
    float rootAngle = finiteFloat(inV0[0].w) ? inV0[0].w : 0.0;
    tangentFrame(surfaceUp, rootAngle, baseLateral, baseFront);

    vec3 tipOffset = tip - root;
    vec3 horizontalTip = tipOffset - surfaceUp * dot(tipOffset, surfaceUp);
    vec3 bendDirection = safeNormalize(horizontalTip, baseFront);
    vec3 bendAxis = safeNormalize(cross(surfaceUp, bendDirection), baseLateral);

    vec3 firstEdge = mix(root, guide, bladeHeight);
    vec3 secondEdge = mix(guide, tip, bladeHeight);
    vec3 center = mix(firstEdge, secondEdge, bladeHeight);
    vec3 firstHandle = guide - root;
    vec3 secondHandle = tip - guide;
    vec3 curveDerivative = 2.0 * (secondEdge - firstEdge);
    vec3 curveSecondDerivative = 2.0 * (tip - 2.0 * guide + root);

    float derivativeUp = dot(curveDerivative, surfaceUp);
    float derivativeBend = dot(curveDerivative, bendDirection);
    vec3 fallbackTangent = degenerateCurveTangent(
        bladeHeight, firstHandle, secondHandle, surfaceUp);
    fallbackTangent = safeNormalize(
        surfaceUp * dot(fallbackTangent, surfaceUp)
            + bendDirection * dot(fallbackTangent, bendDirection),
        surfaceUp);
    vec3 curveTangent = safeNormalize(
        derivativeUp * surfaceUp + derivativeBend * bendDirection,
        fallbackTangent);

    float tangentCosine = dot(curveTangent, surfaceUp);
    float tangentSine = dot(curveTangent, bendDirection);
    vec3 widthDirection = safeNormalize(
        rotateAroundAxis(baseLateral, bendAxis, tangentCosine, tangentSine),
        baseLateral);
    vec3 transportedTangent = curveTangent;
    vec3 transportedFront = safeNormalize(
        cross(widthDirection, curveTangent), baseFront);

    float angleRate = 0.0;
    float angleDenominator = derivativeUp * derivativeUp
                           + derivativeBend * derivativeBend;
    if (finiteFloat(angleDenominator)
            && angleDenominator > FRAME_EPSILON * FRAME_EPSILON) {
        float secondUp = dot(curveSecondDerivative, surfaceUp);
        float secondBend = dot(curveSecondDerivative, bendDirection);
        float candidateRate = (derivativeUp * secondBend
                             - derivativeBend * secondUp) / angleDenominator;
        angleRate = finiteFloat(candidateRate) ? candidateRate : 0.0;
    }
    vec3 widthRate = angleRate * cross(bendAxis, widthDirection);

    float encodedWidth = finiteFloat(inV2[0].w) ? max(inV2[0].w, 0.0) : 0.0;
    float rootHalfWidth = 0.5 * encodedWidth;
    float halfWidth = rootHalfWidth * (1.0 - bladeHeight);
    float halfWidthRate = -rootHalfWidth;

    vec3 surfaceHeightDerivative = curveDerivative
        + bladeAcross * (halfWidthRate * widthDirection
                       + halfWidth * widthRate);
    vec3 surfaceAcrossDerivative = halfWidth * widthDirection;
    vec3 rawNormalValue = cross(surfaceAcrossDerivative,
                                surfaceHeightDerivative);
    if (dot(rawNormalValue, rawNormalValue)
            <= FRAME_EPSILON * FRAME_EPSILON) {
        rawNormalValue = cross(widthDirection, surfaceHeightDerivative);
    }
    vec3 bladeNormal = safeNormalize(rawNormalValue, transportedFront);
    vec3 surfaceTangent = safeNormalize(surfaceHeightDerivative,
                                        transportedTangent);

    vec3 worldPosition = center + widthDirection * (halfWidth * bladeAcross);

    outWorldPosition = worldPosition;
    outNormal = bladeNormal;
    outBladeHeight = bladeHeight;
    outVariation = stableVariation(root, rootAngle);
    outTangent = surfaceTangent;
    outBladeAcross = bladeAcross;
    outLateral = widthDirection;
    gl_Position = frame.proj * frame.view * vec4(worldPosition, 1.0);
}
