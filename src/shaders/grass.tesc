#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(vertices = 1) out;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out vec4 outV0[];
layout(location = 1) out vec4 outV1[];
layout(location = 2) out vec4 outV2[];
layout(location = 3) out vec4 outUp[];

void main() {
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    outV0[gl_InvocationID] = inV0[gl_InvocationID];
    outV1[gl_InvocationID] = inV1[gl_InvocationID];
    outV2[gl_InvocationID] = inV2[gl_InvocationID];
    outUp[gl_InvocationID] = inUp[gl_InvocationID];

    vec3 cameraPosition = (inverse(camera.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float cameraDistance = length(cameraPosition - inV0[gl_InvocationID].xyz);
    float curveTessellation = 4.0;
    if (cameraDistance < 12.0) {
        curveTessellation = 16.0;
    } else if (cameraDistance < 24.0) {
        curveTessellation = 12.0;
    } else if (cameraDistance < 36.0) {
        curveTessellation = 8.0;
    }

    gl_TessLevelInner[0] = curveTessellation;
    gl_TessLevelInner[1] = 2.0;
    gl_TessLevelOuter[0] = 2.0;
    gl_TessLevelOuter[1] = curveTessellation;
    gl_TessLevelOuter[2] = 2.0;
    gl_TessLevelOuter[3] = curveTessellation;
}
