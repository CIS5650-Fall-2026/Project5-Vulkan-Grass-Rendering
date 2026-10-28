#version 450

layout(quads, equal_spacing, ccw) in;

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

vec3 safeNormalize(vec3 value, vec3 fallback) {
    float magnitude = length(value);
    return magnitude > 1e-6 ? value / magnitude : fallback;
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

    vec3 root = inV0[0].xyz;
    vec3 guide = inV1[0].xyz;
    vec3 tip = inV2[0].xyz;
    vec3 surfaceUp = safeNormalize(inUp[0].xyz, vec3(0.0, 1.0, 0.0));

    vec3 firstEdge = mix(root, guide, bladeHeight);
    vec3 secondEdge = mix(guide, tip, bladeHeight);
    vec3 center = mix(firstEdge, secondEdge, bladeHeight);
    vec3 derivative = 2.0 * (secondEdge - firstEdge);
    vec3 curveTangent = safeNormalize(derivative, surfaceUp);

    vec3 baseLateral;
    vec3 baseFront;
    tangentFrame(surfaceUp, inV0[0].w, baseLateral, baseFront);

    vec3 widthDirection = baseLateral - curveTangent * dot(baseLateral, curveTangent);
    if (dot(widthDirection, widthDirection) <= 1e-10) {
        widthDirection = cross(curveTangent, baseFront);
    }
    widthDirection = safeNormalize(widthDirection, baseLateral);

    vec3 bladeNormal = safeNormalize(cross(widthDirection, curveTangent), baseFront);
    if (dot(bladeNormal, baseFront) < 0.0) {
        widthDirection = -widthDirection;
        bladeNormal = -bladeNormal;
    }

    float fullWidth = max(inV2[0].w, 0.0) * (1.0 - bladeHeight);
    vec3 worldPosition = center + widthDirection * (0.5 * fullWidth * bladeAcross);

    outWorldPosition = worldPosition;
    outNormal = bladeNormal;
    outBladeHeight = bladeHeight;
    outVariation = stableVariation(root, inV0[0].w);
    outTangent = curveTangent;
    outBladeAcross = bladeAcross;
    gl_Position = frame.proj * frame.view * vec4(worldPosition, 1.0);
}
