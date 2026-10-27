#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation evaluation shader inputs and outputs

layout(location = 0) in vec4 tcV0[];
layout(location = 1) in vec4 tcV1[];
layout(location = 2) in vec4 tcV2[];
layout(location = 3) in vec3 tcHalfWidth[];

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out float fragHeight;
layout(location = 2) flat out float fragVariation;
layout(location = 3) out vec3 fragViewDirection;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;
    float oneMinusV = 1.0 - v;

	// TODO: Use u and v to parameterize along the grass blade and output positions for each vertex of the grass blade

    vec3 p0 = tcV0[0].xyz;
    vec3 p1 = tcV1[0].xyz;
    vec3 p2 = tcV2[0].xyz;

    vec3 a = p0 + v * (p1 - p0);
    vec3 b = p1 + v * (p2 - p1);
    vec3 c = a + v * (b - a);

    vec3 worldPos = c + (2.0 * u - 1.0) * oneMinusV * tcHalfWidth[0];

    vec3 t0 = b - a;

    if (dot(t0, t0) < 1e-3) {
        t0 = p1 - p0;
    }

    vec3 t1 = tcHalfWidth[0];

    fragNormal = normalize(cross(t0, t1));
    fragHeight = v;
    fragVariation = fract(sin(tcV0[0].w * 12.9898) * 43758.5453);

    vec3 cameraPosition = -(transpose(mat3(camera.view)) * camera.view[3].xyz);
    fragViewDirection = cameraPosition - worldPos;

    gl_Position = camera.proj * camera.view * vec4(worldPos, 1.0);
}
