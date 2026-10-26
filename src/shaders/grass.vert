#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 inV0;
layout(location = 1) in vec4 inV1;
layout(location = 2) in vec4 inV2;
layout(location = 3) in vec4 inUp;

layout(location = 0) out vec4 v0;
layout(location = 1) out vec4 v1;
layout(location = 2) out vec4 v2;
layout(location = 3) out vec4 up;

void main() {
    // Keep the curve in model space until tessellation creates its surface.
    v0 = inV0;
    v1 = inV1;
    v2 = inV2;
    up = inUp;
    gl_Position = vec4(inV0.xyz, 1.0);
}
