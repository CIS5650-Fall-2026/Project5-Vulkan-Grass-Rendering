#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

// Match our outputs from grass.tesc
layout(location = 0) in vec4 bladeV0[];
layout(location = 1) in vec4 bladeV1[];
layout(location = 2) in vec4 bladeV2[];

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation evaluation shader inputs and outputs

layout(location = 0) out float bladeHeight;

void main() {
    float u = gl_TessCoord.x; // across
    float v = gl_TessCoord.y; // root to tip

    bladeHeight = v;

	// TODO: Use u and v to parameterize along the grass blade and output positions for each vertex of the grass blade

    vec3 v0 = bladeV0[0].xyz;
    vec3 v1 = bladeV1[0].xyz;
    vec3 v2 = bladeV2[0].xyz;

    float orient = bladeV0[0].w;
    float width = bladeV2[0].w;

    // Bezier
    float oneMinusT = 1.0 - v;
    vec3 center = (oneMinusT * oneMinusT * v0) + (2.0 * oneMinusT * v * v1) + (v * v * v2);

    vec3 widthDirection = vec3(cos(orient), 0.0, sin(orient));

    float triwidth = width * (1.0 - v);

    // Center triangle shapinh width around the curve
    float sidewaysOffset = (u - 0.5) * triwidth;
    vec3 worldPosition = center + sidewaysOffset * widthDirection;

    gl_Position = camera.proj * camera.view * vec4(worldPosition, 1.0);
}
