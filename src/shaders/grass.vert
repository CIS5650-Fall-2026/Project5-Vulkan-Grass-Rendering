#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"

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
