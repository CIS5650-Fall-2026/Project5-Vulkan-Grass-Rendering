#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
}
camera;

layout(location = 0) in vec4 in_v0[];
layout(location = 1) in vec4 in_v1[];
layout(location = 2) in vec4 in_v2[];
layout(location = 3) in vec4 in_up[];

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragPos;
layout(location = 2) out vec3 out_v0;

const float tau = 0.5;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec3 v0 = in_v0[0].xyz;
    vec3 v1 = in_v1[0].xyz;
    vec3 v2 = in_v2[0].xyz;
    vec3 up = in_up[0].xyz;

    float ang = in_v0[0].w;
    float w = in_v2[0].w;
    vec3 dir = vec3(cos(ang), 0, sin(ang));
    vec3 t1 = cross(up, dir);

    vec3 a = v0 + v * (v1 - v0);
    vec3 b = v1 + v * (v2 - v1);
    vec3 c = a + v * (b - a);
    vec3 c0 = c - w * t1;
    vec3 c1 = c + w * t1;

    // TODO: can improving aliasing at further distances (from paper)
    float t = 0.5 + (u - 0.5) * (1 - max(v - tau, 0) / (1 - tau));
    vec3 p = mix(c0, c1, t);
    gl_Position = camera.proj * camera.view * vec4(p, 1);
    fragUV = gl_TessCoord.xy;
    fragPos = vec3(camera.view * vec4(p, 1));
    out_v0 = v0;
}
