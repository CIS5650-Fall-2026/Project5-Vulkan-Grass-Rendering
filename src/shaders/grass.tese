#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;


in gl_PerVertex {
    vec4 gl_Position;
} gl_in[gl_MaxPatchVertices];

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec3 v0 = inV0[0].xyz;
    vec3 v1 = inV1[0].xyz;
    vec3 v2 = inV2[0].xyz;
    float orientation = inV0[0].w;
    float width = inV2[0].w;

    // de Casteljau: c is the point on the curve at height v
    vec3 a = mix(v0, v1, v);
    vec3 b = mix(v1, v2, v);
    vec3 c = mix(a, b, v);

    // assumes up is +y, as Blades.cpp generates it
    vec3 t1 = vec3(cos(orientation), 0.0, sin(orientation));

    // triangle shape: the two edges meet at the tip
    float t = u + 0.5 * v - u * v;
    vec3 position = mix(c - width * t1, c + width * t1, t);

    // an upright blade has v1 == v2, so b - a vanishes at the tip
    vec3 t0 = b - a;
    if (dot(t0, t0) < 1e-12) {
        t0 = v1 - v0;
    }
    outNormal = normalize(cross(t0, t1));
    outUV = vec2(u, v);

    gl_Position = camera.proj * camera.view * vec4(position, 1.0);
}
