#version 450

layout(set = 0, binding = 0, std140) uniform FrameUniform {
    mat4 view;
    mat4 proj;
    vec4 cameraTime;
    vec4 wind;
    vec4 gravity;
    vec4 collider;
    vec4 parameters;
    vec4 viewport;
    uvec4 flags;
    vec4 lighting;
    vec4 terrain;
} frame;

layout(location = 0) in vec4 inV0;
layout(location = 1) in vec4 inV1;
layout(location = 2) in vec4 inV2;
layout(location = 3) in vec4 inUp;

layout(location = 0) out vec4 outV0;
layout(location = 1) out vec4 outV1;
layout(location = 2) out vec4 outV2;
layout(location = 3) out vec4 outUp;

void main() {
    outV0 = inV0;
    outV1 = inV1;
    outV2 = inV2;
    outUp = inUp;
    gl_Position = vec4(inV0.xyz, 1.0);
}
