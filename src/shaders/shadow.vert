#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"

layout(push_constant) uniform DrawPushConstant {
    vec4 translation;
} drawData;

layout(location = 0) in vec3 inPosition;

void main() {
    vec3 worldPosition = inPosition + drawData.translation.xyz;
    gl_Position = frame.lightViewProj * vec4(worldPosition, 1.0);
}
