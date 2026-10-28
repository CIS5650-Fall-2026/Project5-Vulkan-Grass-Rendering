#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"
#include "render_common.glsl"

layout(location = 0) in vec3 inWorldRay;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 worldRay = renderSafeNormalize(inWorldRay, vec3(0.0, 0.0, -1.0));
    outColor = vec4(renderToneResponse(renderSkyRadiance(worldRay)), 1.0);
}
