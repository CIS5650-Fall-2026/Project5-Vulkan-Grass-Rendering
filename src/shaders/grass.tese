#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation evaluation shader inputs and outputs
layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out float outHeight;
layout(location = 1) out vec3 outViewPosition;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

	// TODO: Use u and v to parameterize along the grass blade and output positions for each vertex of the grass blade

    vec3 root = inV0[0].xyz;
    vec3 control = inV1[0].xyz;
    vec3 tip = inV2[0].xyz;

    // Quadratic Bézier centerline.
    vec3 center = (1.0 - v) * (1.0 - v) * root
                + 2.0 * (1.0 - v) * v * control
                + v * v * tip;

    float angle = inV0[0].w;
    vec3 sideways = vec3(cos(angle), 0.0, sin(angle));
    float width = inV2[0].w * (1.0 - v) * (1.0 + 0.5 * v);
    vec3 position = center + (u - 0.5) * width * sideways;

    vec4 viewPosition = camera.view * vec4(position, 1.0);
    gl_Position = camera.proj * viewPosition;
    outViewPosition = viewPosition.xyz;
    outHeight = v;
}
