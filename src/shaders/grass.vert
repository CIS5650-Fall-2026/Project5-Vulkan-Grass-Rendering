
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
};

// TODO: Declare vertex shader inputs and outputs

// Blade attributes/properties given by Blade::getAttributeDescriptions()
layout(location = 0) in vec4 inV0;
layout(location = 1) in vec4 inV1;
layout(location = 2) in vec4 inV2;
layout(location = 3) in vec4 inUp;

// Inputs for tesselation control shader
layout(location = 0) out vec4 outV0;
layout(location = 1) out vec4 outV1;
layout(location = 2) out vec4 outV2;
layout(location = 3) out vec4 outUp;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
	// TODO: Write gl_Position and any other shader outputs
    vec3 worldV0 = (model * vec4(inV0.xyz, 1.0)).xyz;
    vec3 worldV1 = (model * vec4(inV1.xyz, 1.0)).xyz;
    vec3 worldV2 = (model * vec4(inV2.xyz, 1.0)).xyz;

    // Up direction vector
    vec3 worldUp = normalize((model * vec4(inUp.xyz, 0.0)).xyz);

    outV0 = vec4(worldV0, inV0.w); // Orientation
    outV1 = vec4(worldV1, inV1.w); // Height
    outV2 = vec4(worldV2, inV2.w); // Width
    outUp = vec4(worldUp, inUp.w); // Stiffness

    // Gets modifies further in the tesselation shader
    gl_Position = vec4(worldV0, 1.0);
}
