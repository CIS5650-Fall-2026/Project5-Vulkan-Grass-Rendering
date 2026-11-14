#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out vec3 fragColor;

out gl_PerVertex {
    vec4 gl_Position;
};

vec3 bezier(float t, vec3 p0, vec3 p1, vec3 p2) {
    float s = 1.0 - t;
    return s * s * p0 + 2.0 * s * t * p1 + t * t * p2;
}

vec3 bezierTangent(float t, vec3 p0, vec3 p1, vec3 p2) {
    return 2.0 * (1.0 - t) * (p1 - p0) + 2.0 * t * (p2 - p1);
}

vec3 safeNormalize(vec3 v, vec3 fallback) {
    float len2 = dot(v, v);
    return len2 > 1e-8 ? v * inversesqrt(len2) : fallback;
}

void main() {
    // u is across the width. v is root (0) to tip (1).
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec3 p0 = inV0[0].xyz;
    vec3 p1 = inV1[0].xyz;
    vec3 p2 = inV2[0].xyz;

    float orientation = inV0[0].w;
    float width = inV2[0].w;
    vec3 up = safeNormalize(inUp[0].xyz, vec3(0.0, 1.0, 0.0));

    vec3 center = bezier(v, p0, p1, p2);

    // Facing direction is the blade's thin axis, spun around up by the stored angle.
    vec3 facing = vec3(cos(orientation), 0.0, sin(orientation));
    facing = safeNormalize(facing - up * dot(facing, up), safeNormalize(cross(up, vec3(1.0, 0.0, 0.0)), vec3(1.0, 0.0, 0.0)));

    vec3 tangent = safeNormalize(bezierTangent(v, p0, p1, p2), safeNormalize(p2 - p0, up));
    vec3 side = safeNormalize(cross(tangent, facing), safeNormalize(cross(up, facing), vec3(1.0, 0.0, 0.0)));

    // Full width at the root, a point at the tip.
    float taper = 1.0 - v;
    vec3 worldPos = center + (u - 0.5) * width * taper * side;

    gl_Position = camera.proj * camera.view * vec4(worldPos, 1.0);
    fragColor = mix(vec3(0.05, 0.28, 0.06), vec3(0.55, 0.82, 0.22), v);
}
