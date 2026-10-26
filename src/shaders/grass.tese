#version 450
#extension GL_ARB_separate_shader_objects : enable

// Fractional-even spacing moves the short edge segments continuously when
// levels cross even integers. It requires at least two segments on each axis.
layout(quads, fractional_even_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec4 bladeV0[];
layout(location = 1) in vec4 bladeV1[];
layout(location = 2) in vec4 bladeV2[];
layout(location = 3) in vec4 bladeUp[];
layout(location = 4) in vec3 bladeWidthVector[];
layout(location = 5) patch in vec2 bladeDetail;

in gl_PerVertex {
    vec4 gl_Position;
} gl_in[];

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec2 bladeUV;
layout(location = 2) flat out float colorVariation;
layout(location = 3) flat out vec2 detailInfo;

void main() {
    float u = gl_TessCoord.x;
    float t = gl_TessCoord.y;

    vec3 p0 = gl_in[0].gl_Position.xyz;
    vec3 p1 = bladeV1[0].xyz;
    vec3 p2 = bladeV2[0].xyz;
    vec3 center = (1.0 - t) * (1.0 - t) * p0 + 2.0 * (1.0 - t) * t * p1 + t * t * p2;
    vec3 widthVector = bladeWidthVector[0];
    vec3 position = center + (u - 0.5) * (1.0 - t) * widthVector;

    // The initial p1 == p2 makes B'(1) zero. Use the limiting tangent at the tip.
    float normalT = min(t, 0.9999);
    vec3 tangent = 2.0 * ((1.0 - normalT) * (p1 - p0) + normalT * (p2 - p1));
    vec3 normal = cross(widthVector, tangent);
    if (dot(normal, normal) < 1e-12) normal = cross(widthVector, bladeUp[0].xyz);
    if (dot(normal, normal) < 1e-12) normal = vec3(0.0, 0.0, 1.0);
    worldNormal = normalize(normal);
    bladeUV = vec2(u, t);
    colorVariation = fract(sin(dot(bladeV0[0].xyz, vec3(12.9898, 37.719, 78.233))) * 43758.5453);
    detailInfo = bladeDetail;
    gl_Position = camera.proj * camera.view * vec4(position, 1.0);
}
