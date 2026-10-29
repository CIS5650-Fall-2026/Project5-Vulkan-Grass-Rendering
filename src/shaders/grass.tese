#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation evaluation shader inputs and outputs
// input
layout(location = 0) in vec4 in_v0[];
layout(location = 1) in vec4 in_v1[];
layout(location = 2) in vec4 in_v2[];
layout(location = 3) in vec4 in_up[];

// to frag
layout(location = 0) out float out_v;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

	// TODO: Use u and v to parameterize along the grass blade and output positions for each vertex of the grass blade
    vec3 v0 = in_v0[0].xyz;
    vec3 v1 = in_v1[0].xyz;
    vec3 v2 = in_v2[0].xyz;
    float orientation = in_v0[0].w;
    float width = in_v2[0].w;
    vec3 wt1 = width * vec3(cos(orientation), 0.0, sin(orientation));

    // from the paper
    vec3 a = v0 + v * (v1 - v0);
    vec3 b = v1 + v * (v2 - v1);
    vec3 c = a + v * (b - a);
    vec3 c0 = c - wt1;
    vec3 c1 = c + wt1;
    
    float tao = 0.5;
    float t = 0.5 + (u - 0.5) * (1 - max((v - tao), 0)/(1 - tao));

    vec3 p = (1 - t) * c0 + t * c1;

    out_v = v;
    gl_Position = camera.proj * camera.view * vec4(p, 1.0);
}
