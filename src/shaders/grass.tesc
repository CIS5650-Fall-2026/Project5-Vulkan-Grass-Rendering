#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(vertices = 1) out;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
    vec4 position;
} camera;
layout(set = 2, binding = 0, std140) uniform GrassDetailSettings {
    layout(offset = 80) vec4 distances; // near, far, max along, min along
    layout(offset = 96) uvec4 controls; // enabled, visualization
} lod;

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];
layout(location = 4) in vec3 inWidthVector[];

layout(location = 0) out vec4 bladeV0[];
layout(location = 1) out vec4 bladeV1[];
layout(location = 2) out vec4 bladeV2[];
layout(location = 3) out vec4 bladeUp[];
layout(location = 4) out vec3 bladeWidthVector[];
layout(location = 5) patch out vec2 bladeDetail; // normalized requested level, visualization

// Match the built-in output block declared by grass.vert.
in gl_PerVertex {
    vec4 gl_Position;
} gl_in[];

out gl_PerVertex {
    vec4 gl_Position;
} gl_out[];

void main() {
	// Don't move the origin location of the patch
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    bladeV0[gl_InvocationID] = inV0[gl_InvocationID];
    bladeV1[gl_InvocationID] = inV1[gl_InvocationID];
    bladeV2[gl_InvocationID] = inV2[gl_InvocationID];
    bladeUp[gl_InvocationID] = inUp[gl_InvocationID];
    bladeWidthVector[gl_InvocationID] = inWidthVector[gl_InvocationID];

    // u crosses the blade; v follows its length. One invocation per patch.
    const float across = 2.0;
    // The vertex shader has already transformed the root into world space.
    // Root distance is independent of wind/bending and of atomic output order.
    float distanceToCamera = length(camera.position.xyz - inV0[0].xyz);
    float weight = smoothstep(lod.distances.x, lod.distances.y, distanceToCamera);
    float along = lod.controls.x != 0u ? mix(lod.distances.z, lod.distances.w, weight) : lod.distances.z;
    float range = lod.distances.z - lod.distances.w;
    bladeDetail = vec2(range > 0.0 ? clamp((along - lod.distances.w) / range, 0.0, 1.0) : 1.0, float(lod.controls.y));
    gl_TessLevelOuter[0] = along;
    gl_TessLevelOuter[1] = across;
    gl_TessLevelOuter[2] = along;
    gl_TessLevelOuter[3] = across;
    gl_TessLevelInner[0] = across;
    gl_TessLevelInner[1] = along;
}
