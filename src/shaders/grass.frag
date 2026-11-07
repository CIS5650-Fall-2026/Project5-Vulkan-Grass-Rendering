#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare fragment shader inputs

layout(location = 0) out vec4 outColor;

layout(location = 0) in float bladeHeight;

void main() {
    // TODO: Compute fragment color

    //outColor = vec4(1.0);
    // outColor = vec4(0.25, 0.60, 0.15, 1.0);

    vec3 rootColor = vec3(0.04, 0.15, 0.02);
    vec3 tipColor  = vec3(0.45, 0.80, 0.15);

    float height = clamp(bladeHeight, 0.0, 1.0);
    vec3 color = mix(rootColor, tipColor, height);

    outColor = vec4(color, 1.0);
}
