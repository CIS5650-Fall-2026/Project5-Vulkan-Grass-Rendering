
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
};

// TODO: Declare vertex shader inputs and outputs
// One full blade per patch
layout(location = 0) in vec4 inV0;
layout(location = 1) in vec4 inV1;
layout(location = 2) in vec4 inV2;
layout(location = 3) in vec4 inUp;

layout(location = 0) out vec3 outV1;
layout(location = 1) out vec3 outV2;
layout(location = 2) out vec3 outWidthDirection;
layout(location = 3) out float outWidth;
layout(location = 4) out float outVariation;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
	// TODO: Write gl_Position and any other shader outputs
    // Project after tessellation
    gl_Position = model * vec4(inV0.xyz, 1.0);
    outV1 = (model * vec4(inV1.xyz, 1.0)).xyz;
    outV2 = (model * vec4(inV2.xyz, 1.0)).xyz;

    // Match compute's width direction
    vec3 up = normalize(inUp.xyz);
    vec3 reference = abs(up.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(reference - up * dot(reference, up));
    vec3 bitangent = cross(tangent, up);
    vec3 localWidth = cos(inV0.w) * tangent + sin(inV0.w) * bitangent;
    vec3 worldWidth = mat3(model) * localWidth;
    outWidthDirection = normalize(worldWidth);
    outWidth = inV2.w * length(worldWidth);

    // Stable color per blade
    outVariation = fract(sin(dot(inV0.xyz, vec3(12.9898, 0.0, 78.233))) * 43758.5453);
}
