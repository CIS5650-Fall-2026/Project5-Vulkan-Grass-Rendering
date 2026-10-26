
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
};

layout(location = 0) in vec4 inV0;
layout(location = 1) in vec4 inV1;
layout(location = 2) in vec4 inV2;
layout(location = 3) in vec4 inUp;

layout(location = 0) out vec4 bladeV0;
layout(location = 1) out vec4 bladeV1;
layout(location = 2) out vec4 bladeV2;
layout(location = 3) out vec4 bladeUp;
layout(location = 4) out vec3 bladeWidthVector;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    // The w components are packed parameters, not homogeneous coordinates.
    bladeV0 = vec4((model * vec4(inV0.xyz, 1.0)).xyz, inV0.w);
    bladeV1 = vec4((model * vec4(inV1.xyz, 1.0)).xyz, inV1.w);
    bladeV2 = vec4((model * vec4(inV2.xyz, 1.0)).xyz, inV2.w);
    bladeUp = vec4(normalize(mat3(model) * inUp.xyz), inUp.w);

    vec3 up = normalize(inUp.xyz);
    vec3 reference = abs(up.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(reference - dot(reference, up) * up);
    vec3 bitangent = cross(tangent, up);
    vec3 side = cos(inV0.w) * tangent + sin(inV0.w) * bitangent;
    // Carry a full-width vector so model scaling also scales the blade width.
    bladeWidthVector = mat3(model) * (side * inV2.w);
    gl_Position = vec4(bladeV0.xyz, 1.0);
}
