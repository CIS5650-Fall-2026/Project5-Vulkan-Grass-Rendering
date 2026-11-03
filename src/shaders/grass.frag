#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec4 normal;
layout(location = 1) in vec2 uv;
layout(location = 2) in float bladeRand;
layout(location = 3) in vec3 viewDir;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 darkGreen  = vec3(0.05, 0.3, 0.05);
    vec3 lightGreen = vec3(0.4, 0.8, 0.2);
    vec3 baseColor  = mix(darkGreen, lightGreen, uv.y);

    float brightness = mix(0.85, 1.15, bladeRand);
    float hueShift   = fract(bladeRand * 7.31) - 0.5;          // [-0.5, 0.5]
    vec3 tint = vec3(0.1, 0.05, -0.1) * hueShift;
    baseColor = baseColor * brightness + tint;
    baseColor = max(baseColor, 0.0);

    vec3 lightDir = normalize(vec3(1.0, 3.0, 2.0));

    vec3 n = normalize(normal.xyz);
    vec3 v = normalize(viewDir);
    if (dot(n, v) < 0) n = -n;
    float ndotl = dot(n, lightDir);
    float lambert = ndotl / 2 + 0.5;
    lambert = lambert * lambert;

    // hemisphere ambient: faces pointing up get a bluish sky color, faces pointing down a dim brownish ground color
    vec3 skyColor    = vec3(0.35, 0.40, 0.50);
    vec3 groundColor = vec3(0.18, 0.14, 0.10);
    float hemi = n.y * 0.5 + 0.5;
    vec3 ambientLight = mix(groundColor, skyColor, hemi);

    // slightly warm sun
    vec3 sunColor = vec3(1.0, 0.95, 0.85);
    vec3 color = baseColor * (ambientLight + lambert * sunColor);

    float trans = pow(max(dot(v, -lightDir), 0.0), 5) * uv.y;
    vec3 transColor = vec3(0.6, 0.8, 0.2);
    color += transColor * trans * 0.3;

    outColor = vec4(color, 1.0);
}
