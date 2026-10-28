#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"

layout(push_constant) uniform DrawPushConstant {
    vec4 translation;
} drawData;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outColor;
layout(location = 3) flat out uint outMaterial;

void main() {
    vec3 worldPosition = inPosition + drawData.translation.xyz;
    float normalLengthSquared = dot(inNormal, inNormal);

    outWorldPosition = worldPosition;
    outNormal = normalLengthSquared > 1.0e-10
        ? inNormal * inversesqrt(normalLengthSquared)
        : vec3(0.0, 1.0, 0.0);
    outColor = inColor;
    outMaterial = uint(clamp(floor(drawData.translation.w + 0.5), 0.0, 2.0));
    gl_Position = frame.proj * frame.view * vec4(worldPosition, 1.0);
}
