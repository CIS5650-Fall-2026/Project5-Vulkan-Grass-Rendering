#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 ground = texture(texSampler, fragTexCoord).rgb;
    // Match the warm sunlight and keep the ground quieter than the blades.
    outColor = vec4(ground * vec3(0.48, 0.48, 0.32), 1.0);
}
