#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare fragment shader inputs

layout(location = 0) in float bladeHeight;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 norm = normal;
    if (!gl_FrontFacing) {
        norm = -norm;
    }

    // Keep the change deliberately subtle: slightly darker at the root and
    // slightly lighter toward the tip.
    vec3 baseColor = vec3(0.035, 0.16, 0.045);
    vec3 tipColor  = vec3(0.10,  0.30, 0.075);
    vec3 grassColor = mix(baseColor, tipColor, smoothstep(0.0, 1.0, bladeHeight));

    vec3 lightDir = normalize(vec3(0.4, 1.0, 0.3));
    float diffuse = max(dot(normalize(norm), lightDir), 0.0);
    outColor = vec4(grassColor * (1 + 0.5 * diffuse), 1);
}
