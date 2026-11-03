#version 450
#extension GL_ARB_separate_shader_objects : enable

// 0: fixed 8 x 8 (base code)   1: 1 segment across, 8 along   2: 1 across, 8 -> 2 along by distance
#define LOD 2

layout(vertices = 1) out;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec4 tesc_v1[];
layout(location = 1) in vec4 tesc_v2[];
layout(location = 2) in vec4 tesc_vup[];

layout(location = 0) out vec4 tese_v1[];
layout(location = 1) out vec4 tese_v2[];
layout(location = 2) out vec4 tese_vup[];

void main() {
	// Don't move the origin location of the patch
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    tese_v1[gl_InvocationID] = tesc_v1[gl_InvocationID];
    tese_v2[gl_InvocationID] = tesc_v2[gl_InvocationID];
    tese_vup[gl_InvocationID] = tesc_vup[gl_InvocationID];

	// Set level of tesselation
    vec3 camPos = inverse(camera.view)[3].xyz;
    float d = distance(camPos, gl_in[0].gl_Position.xyz);

    // 8 segments along the blade within 10 units (the default camera distance), down to 2 at 30 (the distance cull limit).
    // across the blade 1 segment is exact: the tese position is linear in u, so extra segments add nothing
    float near = 10.0, far = 30.0;
    float lod = mix(8.0, 2.0, clamp((d - near) / (far - near), 0.0, 1.0));
    #if LOD == 2
        float heightLevel = lod;
        float widthLevel = 1.0;
    #elif LOD == 1
        float heightLevel = 8.0;
        float widthLevel = 1.0;
    #else
        float heightLevel = 8.0;
        float widthLevel = 8.0;
    #endif

    // quads: u runs across the blade (width), v runs along it (height).
    // inner[0] and the v=0 / v=1 edges (outer[1], outer[3]) are split along u, the rest along v
    gl_TessLevelInner[0] = widthLevel;
    gl_TessLevelInner[1] = heightLevel;
    gl_TessLevelOuter[0] = heightLevel;
    gl_TessLevelOuter[1] = widthLevel;
    gl_TessLevelOuter[2] = heightLevel;
    gl_TessLevelOuter[3] = widthLevel;
}
