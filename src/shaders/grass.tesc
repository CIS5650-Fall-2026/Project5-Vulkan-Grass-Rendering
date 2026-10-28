#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"

layout(vertices = 1) out;

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out vec4 outV0[];
layout(location = 1) out vec4 outV1[];
layout(location = 2) out vec4 outV2[];
layout(location = 3) out vec4 outUp[];

float cameraDistance(vec3 point) {
    return length(frame.cameraTime.xyz - point);
}

void main() {
    outV0[gl_InvocationID] = inV0[gl_InvocationID];
    outV1[gl_InvocationID] = inV1[gl_InvocationID];
    outV2[gl_InvocationID] = inV2[gl_InvocationID];
    outUp[gl_InvocationID] = inUp[gl_InvocationID];
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    float heightSegments;
    if (frame.flags.z != 0u) {
        float nearDistance = max(frame.viewport.z, 0.0);
        float farDistance = max(frame.viewport.w, nearDistance + 1e-3);
        float distanceFade = smoothstep(
            nearDistance,
            farDistance,
            cameraDistance(inV0[0].xyz));
        heightSegments = mix(12.0, 1.0, distanceFade);
    } else {
        heightSegments = clamp(frame.parameters.w, 1.0, 12.0);
    }

    float widthSegments = 1.0;
    // Quad u is width: outer 1/3 and inner 0; v is height: outer 0/2 and inner 1.
    gl_TessLevelInner[0] = widthSegments;
    gl_TessLevelInner[1] = heightSegments;
    gl_TessLevelOuter[0] = heightSegments;
    gl_TessLevelOuter[1] = widthSegments;
    gl_TessLevelOuter[2] = heightSegments;
    gl_TessLevelOuter[3] = widthSegments;
}
