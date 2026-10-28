#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare fragment shader inputs
layout(location = 0) in float inHeight;
layout(location = 1) in vec3 inViewPosition;

layout(location = 0) out vec4 outColor;

void main() {
    // TODO: Compute fragment color

    
    vec3 color = mix (vec3(0.0, 0.4, 0.0), vec3(0.038, 0.78, 0.24), inHeight);

    // yellow at tips
    color = mix(color, vec3(0.34, 0.58, 0.09), smoothstep(0.7, 1.0, inHeight));

    // blue distance fog
    float distanceToCamera = length(inViewPosition);
    float fogAmount = 0.4 * smoothstep(5.0, 30.0, distanceToCamera);
    color = mix(color, vec3(0.53, 0.81, 0.92), fogAmount);

    outColor = vec4(color, 1.0);
}
