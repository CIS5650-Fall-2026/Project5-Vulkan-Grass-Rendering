#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(vertices = 1) out;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation control shader inputs and outputs
layout(location = 0) in vec4 tcV0[];
layout(location = 1) in vec4 tcV1[];
layout(location = 2) in vec4 tcV2[];
layout(location = 3) in vec4 tcUp[];

layout(location = 0) out vec4 tsV0[];
layout(location = 1) out vec4 tsV1[];
layout(location = 2) out vec4 tsV2[];
layout(location = 3) out vec4 tsUp[];


void main() {
	// Don't move the origin location of the patch
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

	// TODO: Write any shader outputs
    tsV0[gl_InvocationID] = tcV0[gl_InvocationID];
    tsV1[gl_InvocationID] = tcV1[gl_InvocationID];
    tsV2[gl_InvocationID] = tcV2[gl_InvocationID];
    tsUp[gl_InvocationID] = tcUp[gl_InvocationID];

	// TODO: Set level of tesselation
    gl_TessLevelInner[0] = 5.0;
    gl_TessLevelInner[1] = 5.0;
    gl_TessLevelOuter[0] = 5.0;
    gl_TessLevelOuter[1] = 5.0;
    gl_TessLevelOuter[2] = 5.0;
    gl_TessLevelOuter[3] = 5.0;
}
