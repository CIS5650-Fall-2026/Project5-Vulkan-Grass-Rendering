#version 450
#extension GL_ARB_separate_shader_objects : enable

#define LOD 1

layout(vertices = 1) out;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
}
camera;

layout(location = 0) in vec4 in_v0[];
layout(location = 1) in vec4 in_v1[];
layout(location = 2) in vec4 in_v2[];
layout(location = 3) in vec4 in_up[];

layout(location = 0) out vec4 out_v0[];
layout(location = 1) out vec4 out_v1[];
layout(location = 2) out vec4 out_v2[];
layout(location = 3) out vec4 out_up[];

void main() {
    out_v0[gl_InvocationID] = in_v0[gl_InvocationID];
    out_v1[gl_InvocationID] = in_v1[gl_InvocationID];
    out_v2[gl_InvocationID] = in_v2[gl_InvocationID];
    out_up[gl_InvocationID] = in_up[gl_InvocationID];

    vec4 p = camera.view * vec4(in_v0[gl_InvocationID].xyz, 1);

    float height = in_v1[gl_InvocationID].w;
    float width = in_v2[gl_InvocationID].w;
    float ratio = width / height;

    const float d_max = 60;
    const int n = 8;
    const uint base = 1;

#if LOD
    float factor = max(1.0 - length(p.xyz) / d_max, 0);
    factor *= factor;
#else
    float factor = 1.0;
#endif

    uint height_level = uint(floor(factor * n)) + base;
    uint width_level = uint(floor(ratio * height_level)) + base;

    if (gl_InvocationID == 0) {
        gl_TessLevelInner[0] = width_level;
        gl_TessLevelInner[1] = height_level;

        gl_TessLevelOuter[0] = height_level;
        gl_TessLevelOuter[1] = width_level;
        gl_TessLevelOuter[2] = height_level;
        gl_TessLevelOuter[3] = height_level;
    }
}
