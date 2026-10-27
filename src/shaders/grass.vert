#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
};

// TODO: Declare vertex shader inputs and outputs
layout(location = 0) in vec4 inV0;
layout(location = 1) in vec4 inV1;
layout(location = 2) in vec4 inV2;
layout(location = 3) in vec4 inUp;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec4 outV1;
layout(location = 1) out vec4 outV2;
layout(location = 2) out vec4 outUp;

void main() {
	// TODO: Write gl_Position and any other shader outputs
    gl_Position = model * vec4(inV0.xyz, 1.0);
    gl_Position.w = inV0.w;

    outV1 = model * vec4(inV1.xyz, 1.0);
    outV1.w = inV1.w;

    outV2 = model * vec4(inV2.xyz, 1.0);
    outV2.w = inV2.w;

    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vec3 up = normalize(normalMatrix * inUp.xyz);
    outUp = vec4(up, inUp.w);
}
