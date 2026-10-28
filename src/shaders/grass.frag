#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare fragment shader inputs
layout(location = 0) in vec3 inNormal;
layout(location = 1) in float inHeightFraction;
layout(location = 2) in float inVariation;

layout(location = 0) out vec4 outColor;

void main() {
    // TODO: Compute fragment color
    // Light both sides
    vec3 normal = normalize(inNormal);
    if (!gl_FrontFacing) {
        normal = -normal;
    }
    vec3 lightDirection = normalize(vec3(-0.4, 1.0, 0.6));
    float lighting = 0.35 + 0.65 * max(dot(normal, lightDirection), 0.0);
    vec3 baseColor = vec3(0.07, 0.22, 0.03);
    vec3 tipColor = vec3(0.32, 0.60, 0.12);
    vec3 color = mix(baseColor, tipColor, inHeightFraction);
    color *= mix(0.85, 1.10, inVariation);
    outColor = vec4(color * lighting, 1.0);
}
