#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare fragment shader inputs
layout(location = 0) in vec3 inNormal;
layout(location = 0) out vec4 outColor;

void main() {
    // TODO: Compute fragment color
    vec3 lightDir = normalize(vec3(1.0, 2.0, 1.0));

    float diffuse = max(dot(inNormal, lightDir), 0.0);
    vec3 grassColor = vec3(0.2, 0.6, 0.1);

    outColor = vec4(grassColor * (0.5 + 0.75 * diffuse), 1.0);
}
