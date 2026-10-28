#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"

layout(location = 0) in vec4 inColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = inColor;
}
