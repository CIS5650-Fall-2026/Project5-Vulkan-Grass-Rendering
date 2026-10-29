#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
    vec4 solidColor;
};

layout(set = 1, binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    if (solidColor.a > 0.0) {
        vec3 normal = normalize(fragColor * 2.0 - 1.0);
        vec3 lightDirection = normalize(vec3(0.4, 1.0, 0.3));
        float lighting = 0.25 + 0.75 * max(dot(normal, lightDirection), 0.0);
        outColor = vec4(solidColor.rgb * lighting, solidColor.a);
    } else {
        outColor = texture(texSampler, fragTexCoord);
    }
}
