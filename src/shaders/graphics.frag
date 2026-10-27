#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 1) uniform SunBufferObject {
    vec4 direction;
    vec4 color;
} sun;

layout(set = 1, binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);
    float ambientStrength = mix(0.04, 0.25, sun.color.w);
    float diffuse = max(sun.direction.y, 0.0);
    vec3 lightColor = sun.color.rgb * sun.direction.w;
    vec3 color = texColor.rgb * (vec3(ambientStrength) + 0.75 * diffuse * lightColor);

    outColor = vec4(color, texColor.a);
}
