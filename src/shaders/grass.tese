#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation evaluation shader inputs and outputs
layout(location = 0) in vec4 tsV0[];
layout(location = 1) in vec4 tsV1[];
layout(location = 2) in vec4 tsV2[];
layout(location = 3) in vec4 tsUp[];

layout(location = 0) out float lightIntensity;
layout(location = 1) out float specularIntensity;

vec3 getCamPos() {
    mat4 invView = inverse(camera.view);
    return invView[3].xyz;
}

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec3 v0 = tsV0[0].xyz;
    vec3 v1 = tsV1[0].xyz;
    vec3 v2 = tsV2[0].xyz;
    vec3 up = tsUp[0].xyz;
    float dir = tsV0[0].w;
    float width = tsV2[0].w;

	// TODO: Use u and v to parameterize along the grass blade and output positions for each vertex of the grass blade
    vec3 a = v0 + v * (v1 - v0);
    vec3 b = v1 + v * (v2 - v1);
    vec3 c = a + v * (b - a);
    vec3 t1 = vec3(cos(dir), 0.0, sin(dir));
    vec3 c0 = c - width * t1;
    vec3 c1 = c + width * t1;
    vec3 t0 = normalize(b - a);
    vec3 n = normalize(cross(t0, t1));

    // using u, v, c0, c1, n used to create sophisticated shapes for grass
    // triangle tip shape
    // float tau = 0.8;
    // float t = 0.5 + (u - 0.5) * (1.0 - max(v - tau, 0.0) / (1.0 - tau));
    // triangle shape
    float t = u + (0.5 * v) - (u * v);
    vec3 p = (1.0 - t) * c0 + t * c1; // world space position

    // output clip space position
    gl_Position = camera.proj * camera.view * vec4(p, 1.0);

    // lighting calculations
    vec3 lightVec = normalize(vec3(0.0, -1.0, 0.0));
    lightIntensity = max(dot(n, lightVec), 0.0);

    float shininess = 20.0;
    vec3 viewVec = normalize(getCamPos() - p);
    vec3 halfVec = normalize(v + lightVec);

    if (dot(n, viewVec) < 0.0) n = -n;

    specularIntensity = max(pow(dot(halfVec, n), shininess), 0.0);
}
