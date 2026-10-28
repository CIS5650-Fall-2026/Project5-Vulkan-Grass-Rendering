#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"

layout(location = 0) out vec3 outWorldRay;

void main() {
    vec2 position;
    if (gl_VertexIndex == 0) {
        position = vec2(-1.0, -1.0);
    } else if (gl_VertexIndex == 1) {
        position = vec2(3.0, -1.0);
    } else {
        position = vec2(-1.0, 3.0);
    }

    vec4 viewPosition = inverse(frame.proj) * vec4(position, 1.0, 1.0);
    outWorldRay = transpose(mat3(frame.view)) * viewPosition.xyz;
    gl_Position = vec4(position, 1.0, 1.0);
}
