#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(vertices = 1) out;
layout(constant_id = 0) const bool useLod = true;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
};

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out vec4 v0[];
layout(location = 1) out vec4 v1[];
layout(location = 2) out vec4 v2[];
layout(location = 3) out vec4 up[];

void main() {
    v0[gl_InvocationID] = inV0[gl_InvocationID];
    v1[gl_InvocationID] = inV1[gl_InvocationID];
    v2[gl_InvocationID] = inV2[gl_InvocationID];
    up[gl_InvocationID] = inUp[gl_InvocationID];
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    // Keep nearby curves smooth and spend fewer vertices on distant blades.
    vec3 root = (model * vec4(inV0[0].xyz, 1.0)).xyz;
    vec3 eye = inverse(camera.view)[3].xyz;
    float distanceToCamera = length(eye - root);
    float segments = useLod ? mix(8.0, 2.0, smoothstep(5.0, 40.0, distanceToCamera)) : 8.0;
    // One segment across the width; matching long edges avoid twisted strips.
    gl_TessLevelInner[0] = 1.0;
    gl_TessLevelInner[1] = segments;
    gl_TessLevelOuter[0] = segments;
    gl_TessLevelOuter[1] = 1.0;
    gl_TessLevelOuter[2] = segments;
    gl_TessLevelOuter[3] = 1.0;
}
