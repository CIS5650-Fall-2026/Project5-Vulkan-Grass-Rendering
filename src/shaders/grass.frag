#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare fragment shader inputs
layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec4 outColor;

void main() {
    // TODO: Compute fragment color
    vec3 rootColor = vec3(0.22, 0.83, 0.45);
    vec3 tipColor  = vec3(0.518, 0.87, 0.627);
    vec3 baseColor = mix(rootColor, tipColor, in_uv.y);

    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));
    float diffuse = clamp(abs(dot(normalize(in_normal), lightDir)), 0.0, 1.0);
    float ambient = 0.35;

    vec3 color = baseColor * (diffuse + ambient);
    outColor = vec4(color, 1.0);
}
