#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec4 inV0[];
layout(location = 1) in vec4 inV1[];
layout(location = 2) in vec4 inV2[];
layout(location = 3) in vec4 inUp[];

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec3 base = inV0[0].xyz;
    vec3 guide = inV1[0].xyz;
    vec3 tip = inV2[0].xyz;
    vec3 up = normalize(inUp[0].xyz);
    vec3 reference = vec3(1.0, 0.0, 0.0);
    if (abs(dot(reference, up)) > 0.999) {
        reference = vec3(0.0, 0.0, 1.0);
    }
    reference = normalize(reference - up * dot(reference, up));
    float orientation = inV0[0].w;
    vec3 facing = normalize(reference * cos(orientation) + cross(up, reference) * sin(orientation));
    vec3 right = normalize(cross(up, facing));

    float oneMinusU = 1.0 - u;
    vec3 position = oneMinusU * oneMinusU * base + 2.0 * oneMinusU * u * guide + u * u * tip;
    vec3 tangent = normalize(2.0 * oneMinusU * (guide - base) + 2.0 * u * (tip - guide));
    float bladeWidth = inV2[0].w * (1.0 - u);
    vec3 worldPos = position + right * ((v * 2.0 - 1.0) * bladeWidth);
    vec3 normal = normalize(cross(right, tangent));

    gl_Position = camera.proj * camera.view * vec4(worldPos, 1.0);
    fragWorldPos = worldPos;
    fragNormal = normal;
    float bladeVariation = fract(sin(dot(base.xz, vec2(12.9898, 78.233))) * 43758.5453);
    vec3 rootColor = vec3(0.025, 0.16, 0.025);
    vec3 tipColor = vec3(0.22, 0.62, 0.08);
    fragColor = mix(rootColor, tipColor, smoothstep(0.0, 1.0, u)) * mix(0.78, 1.18, bladeVariation);
}
