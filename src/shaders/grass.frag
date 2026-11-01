#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare fragment shader inputs
layout(location = 0) in float lightIntensity;
layout(location = 1) in float specularIntensity;

layout(location = 0) out vec4 outColor;

void main() {
    // TODO: Compute fragment color
    vec3 albedo = vec3(0.29, 0.58, 0.373);
    vec3 color = albedo * (lightIntensity + 0.2) + vec3(1.0, 1.0, 1.0) * specularIntensity;
    outColor = vec4(color, 1.0);
}
