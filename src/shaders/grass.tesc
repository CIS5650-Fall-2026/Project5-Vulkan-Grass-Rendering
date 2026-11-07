#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(vertices = 1) out;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
    vec4 camPos;
} camera;

// TODO: Declare tessellation control shader inputs and outputs
layout (location = 0) in vec4 v0[];
layout (location = 1) in vec4 v1[];
layout (location = 2) in vec4 v2[];
layout (location = 3) in vec4 up[];

layout (location = 0) out vec4 out_v0[];
layout (location = 1) out vec4 out_v1[];
layout (location = 2) out vec4 out_v2[];
layout (location = 3) out vec4 out_up[];


void main() {
	// Don't move the origin location of the patch
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

	// TODO: Write any shader outputs
    out_v0[gl_InvocationID] = v0[gl_InvocationID];
    out_v1[gl_InvocationID] = v1[gl_InvocationID];
    out_v2[gl_InvocationID] = v2[gl_InvocationID];
    out_up[gl_InvocationID] = up[gl_InvocationID];
	// TODO: Set level of tesselation
    // LOD SETTING
    float width = v2[gl_InvocationID].w;
    float max_tess = 17.0;
    float max_distance = 50.0;

    float curr_distance = length(v0[gl_InvocationID].xyz - camera.camPos.xyz);
    float tess_level = 17;
    if(curr_distance < max_distance) {
        tess_level = max_tess - (curr_distance/max_distance) * (max_tess-3);
    }
    if(width <= .01) {
        tess_level = 0;
    }

    gl_TessLevelInner[0] = tess_level;
	gl_TessLevelInner[1] = tess_level;
	gl_TessLevelOuter[0] = tess_level;
	gl_TessLevelOuter[1] = tess_level;
	gl_TessLevelOuter[2] = tess_level;
	gl_TessLevelOuter[3] = tess_level;
}
