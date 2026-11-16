#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main() {
    
    vec3 rootColor = vec3(0.06, 0.22, 0.03);
    vec3 tipColor = vec3(0.45, 0.70, 0.18);
    vec3 albedo = mix(rootColor, tipColor, inUV.y);

    vec3 lightDir = normalize(vec3(-0.5, 0.6, 0.6));
    float diffuse = abs(dot(normalize(inNormal), lightDir));

    outColor = vec4(albedo * (0.35 + 0.65 * diffuse), 1.0);
}
