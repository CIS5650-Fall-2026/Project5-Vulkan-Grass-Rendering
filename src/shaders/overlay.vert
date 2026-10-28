#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"

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
