#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(vertices = 1) out;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// segments across the blade (u) and along it (v)
#define TESS_WIDTH 2.0
#define TESS_HEIGHT 8.0

in gl_PerVertex {
    vec4 gl_Position;
} gl_in[gl_MaxPatchVertices];

out gl_PerVertex {
    vec4 gl_Position;
} gl_out[];

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out vec4 outV0[];
layout(location = 1) out vec4 outV1[];
layout(location = 2) out vec4 outV2[];
layout(location = 3) out vec4 outUp[];

void main() {
	// Don't move the origin location of the patch
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

	outV0[gl_InvocationID] = inV0[gl_InvocationID];
	outV1[gl_InvocationID] = inV1[gl_InvocationID];
	outV2[gl_InvocationID] = inV2[gl_InvocationID];
	outUp[gl_InvocationID] = inUp[gl_InvocationID];

	// quads: outer[0] and [2] are the u = 0 and u = 1 edges (split along v), outer[1] and [3] the v = 0 and v = 1 edges
	gl_TessLevelInner[0] = TESS_WIDTH;
	gl_TessLevelInner[1] = TESS_HEIGHT;
	gl_TessLevelOuter[0] = TESS_HEIGHT;
	gl_TessLevelOuter[1] = TESS_WIDTH;
	gl_TessLevelOuter[2] = TESS_HEIGHT;
	gl_TessLevelOuter[3] = TESS_WIDTH;
}
