#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation evaluation shader inputs and outputs
layout(location = 0) in vec4 inV1[];
layout(location = 1) in vec4 inV2[];
layout(location = 2) in vec4 inUp[];

layout(location = 0) out vec3 fsNor;
layout(location = 1) out float fsAO;

const vec3 Z_AXIS = vec3(0.0, 0.0, 1.0);

void main() {
    vec4 inV0 = gl_in[0].gl_Position;

    float u = gl_TessCoord.x;
    float v = min(gl_TessCoord.y, 0.99);

	// TODO: Use u and v to parameterize along the grass blade and output positions for each vertex of the grass blade
    vec3 v0 = inV0.xyz;
    vec3 v1 = inV1[0].xyz;
    vec3 v2 = inV2[0].xyz;
    float orientation = inV0.w;
    float width = inV2[0].w;
    vec3 a = v0 + v * (v1 - v0);
    vec3 b = v1 + v * (v2 - v1);
    vec3 c = a + v * (b - a);

    vec3 planeX = normalize(cross(inUp[0].xyz, Z_AXIS));
    vec3 planeZ = normalize(cross(planeX, inUp[0].xyz));
    vec3 t1 = (cos(orientation) * planeX) + (sin(orientation) * planeZ);
    vec3 c0 = c - width * t1;
    vec3 c1 = c + width * t1;
    vec3 t0 = normalize(b - a);

    float t = u + (0.5 * v) - (u * v);
    vec3 pos = ((1.0 - t) * c0) + (t * c1);

    fsNor = normalize(cross(t0, t1));
    fsAO = dot(pos - v0, inUp[0].xyz) * 0.4;
    gl_Position = camera.proj * camera.view * vec4(pos, 1.0);
}