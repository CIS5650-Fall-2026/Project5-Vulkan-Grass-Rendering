#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation evaluation shader inputs and outputs
in gl_PerVertex {
    vec4 gl_Position;
} gl_in[];
out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) in vec3 inV1[];
layout(location = 1) in vec3 inV2[];
layout(location = 2) in vec3 inWidthDirection[];
layout(location = 3) in float inWidth[];
layout(location = 4) in float inVariation[];
layout(location = 0) out vec3 outNormal;
layout(location = 1) out float outHeightFraction;
layout(location = 2) out float outVariation;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

	// TODO: Use u and v to parameterize along the grass blade and output positions for each vertex of the grass blade
    vec3 v0 = gl_in[0].gl_Position.xyz;
    vec3 v1 = inV1[0];
    vec3 v2 = inV2[0];

    // De Casteljau: paper equation 21
    vec3 a = v0 + v * (v1 - v0);
    vec3 b = v1 + v * (v2 - v1);
    vec3 c = a + v * (b - a);
    vec3 tangent = b - a;
    // Tip tangent when v1 equals v2
    if (dot(tangent, tangent) == 0.0) {
        tangent = v1 - v0;
    }
    vec3 t0 = normalize(tangent);
    vec3 t1 = normalize(inWidthDirection[0]);

    // Stored width spans both sides
    float w = 0.5 * inWidth[0];
    vec3 c0 = c - w * t1;
    vec3 c1 = c + w * t1;
    outNormal = normalize(cross(t0, t1));

    // Taper both edges toward the tip
    float t = u + 0.5 * v - u * v;
    vec3 surfacePosition = (1.0 - t) * c0 + t * c1;
    gl_Position = camera.proj * camera.view * vec4(surfacePosition, 1.0);
    outHeightFraction = v;
    outVariation = inVariation[0];
}
