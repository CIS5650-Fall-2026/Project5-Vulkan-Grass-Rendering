#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
}
camera;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec3 v0;

layout(location = 0) out vec4 outColor;

// noise functions I wrote in CIS 5600

const mat3 m3 = mat3(0.00, 0.80, 0.60, -0.80, 0.36, -0.48, -0.60, -0.48, 0.64);

vec3 hash3(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123);
}

vec3 random3D(vec3 p) { return hash3(p) * 2.0 - 1.0; }

float surfletFloat(vec3 p, vec3 gridPoint) {
    vec3 t = abs(p - gridPoint);
    vec3 t2 = vec3(1.0) - 6.0 * t * t * t * t * t + 15.0 * t * t * t * t - 10.0 * t * t * t;
    vec3 gradient = random3D(gridPoint);
    vec3 diff = p - gridPoint;
    float height = dot(diff, gradient);
    return height * t2.x * t2.y * t2.z;
}

float perlinNoise3DFloat(vec3 p) {
    vec3 pFloor = floor(p);
    float total = 0.0;

    // Sum contributions from 8 corners of the cube
    for (int dx = 0; dx <= 1; dx++) {
        for (int dy = 0; dy <= 1; dy++) {
            for (int dz = 0; dz <= 1; dz++) {
                vec3 gridPoint = pFloor + vec3(dx, dy, dz);
                total += surfletFloat(p, gridPoint);
            }
        }
    }
    return clamp(total * 1.41421356, -1.0, 1.0);
}

vec3 perlinNoise3D(vec3 p) {
    return vec3(perlinNoise3DFloat(p), perlinNoise3DFloat(p + vec3(123.4, 567.8, 901.2)),
                perlinNoise3DFloat(p + vec3(234.5, 678.9, 012.3)));
}

vec3 perlinNoise3DFBM(vec3 x, int octaves, float amplitude, float frequency) {
    vec3 h = vec3(0.0);
    float f = frequency;
    float a = amplitude;

    for (int i = 1; i <= octaves; i++) {
        h += perlinNoise3D(x) * a;
        a *= 0.5;
        x *= m3 * f;
    }

    return h;
}

#define RAMP_COUNT 3

vec3 color_ramp(vec4 ramp[RAMP_COUNT], float t) {
    int i = 0;
    while (i < RAMP_COUNT && ramp[i].w < t) {
        i++;
    }
    int l = max(i - 1, 0);
    int r = min(i, RAMP_COUNT - 1);

    return (l == r ? ramp[l].xyz
                   : mix(ramp[l].xyz, ramp[r].xyz, (t - ramp[l].w) / (ramp[r].w - ramp[l].w)));
}

const vec3 fog_color = vec3(0.1, 0.2, 0.3);
const float fog_density = 0.05;

float fog(float dist) {
    float factor = exp(-pow(fog_density * dist, 2.0));
    return clamp(factor, 0.0, 1.0);
}

void main() {
    vec4 stops[RAMP_COUNT] =
        vec4[RAMP_COUNT](vec4(15, 47, 44, 0.2), vec4(99, 159, 147, 0.6), vec4(104, 164, 154, 0.8));

    for (int i = 0; i < RAMP_COUNT; i++) {
        vec3 noise = perlinNoise3DFBM(v0, 2, 0.5, 0.5) * 0.5 + 0.5;
        float s = (RAMP_COUNT - i) / RAMP_COUNT;
        s = s * s * 0.1 + 0.05;
        stops[i].xyz = mix(stops[i].xyz / 255, noise, s);
    }

    vec3 c = color_ramp(stops, fragUV.y);
    c = mix(fog_color, c, fog(length(fragPos)));
    outColor = vec4(c, 1.0);
}
