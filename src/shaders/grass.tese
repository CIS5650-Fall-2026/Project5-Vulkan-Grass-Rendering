#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, fractional_odd_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// tessellation evaluation shader inputs and outputs
layout(location = 0) in vec4 tese_v1[];
layout(location = 1) in vec4 tese_v2[];
layout(location = 2) in vec4 tese_vup[];

layout(location = 0) out vec4 normal;
layout(location = 1) out vec2 uv;
layout(location = 2) out float bladeRand;
layout(location = 3) out vec3 viewDir;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    // Unpack control points: v0.w = orientation, v1.w = height, v2.w = width, up.w = stiffness
    vec4 v0 = gl_in[0].gl_Position;
    vec4 v1 = tese_v1[0];
    vec4 v2 = tese_v2[0];
    vec4 vup = tese_vup[0];

    // De Casteljau: evaluate quadratic Bezier at v (along blade height)
    vec3 a = mix(v0.xyz, v1.xyz, v);
    vec3 b = mix(v1.xyz, v2.xyz, v);
    vec3 c = mix(a, b, v);

    vec3 t0 = normalize(b - a);                     // t0: tangent along the curve
    vec3 t1 = vec3(cos(v0.w), 0, sin(v0.w));        // t1: blade width direction from orientation angle
    vec3 c0 = c - 0.5 * v2.w * t1;                        // Left and right edges of the blade at this height
    vec3 c1 = c + 0.5 * v2.w * t1;

    // Triangle shape: full width at base, tapers to a point at the tip
    float t = u + 0.5 * v - u * v;
    vec3 pos = mix(c0, c1, t);
    vec3 n = normalize(cross(t0, t1));

    // World -> clip space; pass normal and (u, v) to fragment shader
    gl_Position = camera.proj * camera.view * vec4(pos, 1.0);

    vec3 camPos = inverse(camera.view)[3].xyz;
    viewDir = normalize(camPos - pos);

    normal = vec4(n, 0.0);
    uv = vec2(u, v);
    bladeRand = hash(vec2(v0.x, v0.z));
}
