
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

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec4 tcV0;
layout(location = 1) out vec4 tcV1;
layout(location = 2) out vec4 tcV2;
layout(location = 3) out vec4 tcUp;

void main() {
	// TODO: Write gl_Position and any other shader outputs

    tcV0 = vec4((model * vec4(v0.xyz, 1.0)).xyz, v0.w);
    tcV1 = vec4((model * vec4(v1.xyz, 1.0)).xyz, v1.w);
    tcV2 = vec4((model * vec4(v2.xyz, 1.0)).xyz, v2.w);
    tcUp = vec4((model * vec4(up.xyz, 1.0)).xyz, up.w);

    gl_Position = tcV0;
}
