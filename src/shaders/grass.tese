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

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec3 v0 = inV0[0].xyz;
    vec3 v1 = inV1[0].xyz;
    vec3 v2 = inV2[0].xyz;
    vec3 up = inUp[0].xyz;
    float orientation = inV0[0].w;
    float width = inV2[0].w;

    /* de Casteljau's algorithm */
    vec3 a = v0 + v * (v1 - v0); // v / n of the way between v1 and v0
    vec3 b = v1 + v * (v2 - v1); // ' v1 and v2
    vec3 c = a + v * (b - a); // curve point, v of the way between a and b

    vec3 bitangent = vec3(cos(orientation), 0.0, sin(orientation));
    vec3 tangent = b - a;
    if (dot(tangent, tangent) < 1e-8) {
        tangent = up;
    }
    tangent = normalize(tangent);

    vec3 c0 = c - width * bitangent;
    vec3 c1 = c + width * bitangent;

    float t = u + 0.5 * v - u * v;
    vec3 position = mix(c0, c1, t);

    outNormal = normalize(cross(tangent, bitangent));
    outUV = vec2(u, v);

    gl_Position = camera.proj * camera.view * vec4(position, 1.0);
}
