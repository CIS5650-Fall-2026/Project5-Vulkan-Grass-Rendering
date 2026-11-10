
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
};

// TODO: Declare vertex shader inputs and outputs
layout(location = 0) in vec4 v0;
layout(location = 1) in vec4 v1;
layout(location = 2) in vec4 v2;
layout(location = 3) in vec4 up;

layout(location = 0) out vec4 outV0;
layout(location = 1) out vec4 outV1;
layout(location = 2) out vec4 outV2;
layout(location = 3) out vec4 outUp;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
	// TODO: Write gl_Position and any other shader outputs
    vec4 worldV0 = model * vec4(v0.xyz, 1.0);
    worldV0.w = v0.w;
    vec4 worldV1 = model * vec4(v1.xyz, 1.0);
    worldV1.w = v1.w;
    vec4 worldV2 = model * vec4(v2.xyz, 1.0);
    worldV2.w = v2.w;
    vec4 worldUp = model * vec4(up.xyz, 0.0);
    worldUp.w = up.w;

    outV0 = worldV0;
    outV1 = worldV1;
    outV2 = worldV2;
    outUp = worldUp;

    gl_Position = vec4(worldV0.xyz, 1.0);
}
