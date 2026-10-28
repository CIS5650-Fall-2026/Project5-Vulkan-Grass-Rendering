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

layout(location = 0) in vec2 pixelPosition;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 outColor;

void main() {
    gl_Position = vec4(2.0 * pixelPosition.x / frame.viewport.x - 1.0,
                       2.0 * pixelPosition.y / frame.viewport.y - 1.0,
                       0.0,
                       1.0);
    outColor = color;
}
