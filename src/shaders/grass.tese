#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
};

layout(location = 0) in vec4 v0[];
layout(location = 1) in vec4 v1[];
layout(location = 2) in vec4 v2[];
layout(location = 3) in vec4 up[];

layout(location = 0) out vec3 normal;
layout(location = 1) out float bladeHeight;
layout(location = 2) out vec3 worldPosition;
layout(location = 3) out float variation;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;
    vec3 p0 = v0[0].xyz;
    vec3 p1 = v1[0].xyz;
    vec3 p2 = v2[0].xyz;
    vec3 center = (1.0 - v) * (1.0 - v) * p0 + 2.0 * (1.0 - v) * v * p1 + v * v * p2;
    vec3 tangent = 2.0 * ((1.0 - v) * (p1 - p0) + v * (p2 - p1));
    // The starter places p1 at p2, so the derivative at the tip is zero.
    if (dot(tangent, tangent) < 0.000001) tangent = up[0].xyz;

    vec3 bladeUp = normalize(up[0].xyz);
    vec3 axis = abs(bladeUp.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 baseSide = normalize(axis - bladeUp * dot(axis, bladeUp));
    vec3 side = cos(v0[0].w) * baseSide + sin(v0[0].w) * cross(baseSide, bladeUp);
    vec3 position = center + side * (u - 0.5) * v2[0].w * (1.0 - v);
    vec3 worldSide = mat3(model) * side;
    vec3 worldTangent = mat3(model) * tangent;
    vec3 surfaceNormal = cross(worldSide, worldTangent);
    if (dot(surfaceNormal, surfaceNormal) < 0.000001) surfaceNormal = cross(worldSide, mat3(model) * bladeUp);
    normal = normalize(surfaceNormal);
    bladeHeight = v;
    worldPosition = (model * vec4(position, 1.0)).xyz;
    variation = fract(sin(dot(p0.xz, vec2(12.9898, 78.233))) * 43758.5453);
    gl_Position = camera.proj * camera.view * model * vec4(position, 1.0);
}
