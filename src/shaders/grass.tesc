#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(vertices = 1) out;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation control shader inputs and outputs
in gl_PerVertex {
    vec4 gl_Position;
} gl_in[];
out gl_PerVertex {
    vec4 gl_Position;
} gl_out[];

layout(location = 0) in vec3 inV1[];
layout(location = 1) in vec3 inV2[];
layout(location = 2) in vec3 inWidthDirection[];
layout(location = 3) in float inWidth[];
layout(location = 4) in float inVariation[];
layout(location = 0) out vec3 outV1[];
layout(location = 1) out vec3 outV2[];
layout(location = 2) out vec3 outWidthDirection[];
layout(location = 3) out float outWidth[];
layout(location = 4) out float outVariation[];

void main() {
	// Don't move the origin location of the patch
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

	// TODO: Write any shader outputs
    outV1[gl_InvocationID] = inV1[0];
    outV2[gl_InvocationID] = inV2[0];
    outWidthDirection[gl_InvocationID] = inWidthDirection[0];
    outWidth[gl_InvocationID] = inWidth[0];
    outVariation[gl_InvocationID] = inVariation[0];

	// TODO: Set level of tesselation
    // gl_TessLevelInner[0] = ???
    // gl_TessLevelInner[1] = ???
    // gl_TessLevelOuter[0] = ???
    // gl_TessLevelOuter[1] = ???
    // gl_TessLevelOuter[2] = ???
    // gl_TessLevelOuter[3] = ???
    // One width segment, eight height segments
    gl_TessLevelInner[0] = 1.0;
    gl_TessLevelInner[1] = 8.0;
    gl_TessLevelOuter[0] = 8.0;
    gl_TessLevelOuter[1] = 1.0;
    gl_TessLevelOuter[2] = 8.0;
    gl_TessLevelOuter[3] = 1.0;
}
