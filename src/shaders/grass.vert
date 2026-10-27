
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
};

// TODO: Declare vertex shader inputs and outputs

layout(location = 0) in vec4 inV0;
layout(location = 1) in vec4 inV1;
layout(location = 2) in vec4 inV2;

layout(location = 0) out vec4 vV0;
layout(location = 1) out vec4 vV1;
layout(location = 2) out vec4 vV2;
layout(location = 3) out vec3 vHalfWidth;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
	// TODO: Write gl_Position and any other shader outputs

    vV0 = vec4((model * vec4(inV0.xyz, 1.0)).xyz, inV0.w);
    vV1 = vec4((model * vec4(inV1.xyz, 1.0)).xyz, inV1.w);
    vV2 = vec4((model * vec4(inV2.xyz, 1.0)).xyz, inV2.w);

    vec3 bladeUp = normalize(inV1.xyz - inV0.xyz);

    vec3 reference = abs(bladeUp.z) < 0.99
        ? vec3(0.0, 0.0, 1.0)
        : vec3(0.0, 1.0, 0.0);

    vec3 axisX = normalize(cross(bladeUp, reference));
    vec3 axisZ = normalize(cross(axisX, bladeUp));

    float angle = inV0.w;

    vec3 bladeDir = cos(angle) * axisX + sin(angle) * axisZ;

    float width = inV2.w;

    vHalfWidth = mat3(model) * (0.5 * width * bladeDir);

    gl_Position = vec4(vV0.xyz, 1.0);
}
