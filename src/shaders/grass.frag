#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;
layout(location = 0) in vec3 normal;
layout(location = 1) in float bladeHeight;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) in float variation;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 rootColor = vec3(0.07, 0.18, 0.025);
    vec3 tipColor = mix(vec3(0.30, 0.56, 0.08), vec3(0.52, 0.65, 0.16), variation);
    vec3 albedo = mix(rootColor, tipColor, smoothstep(0.0, 1.0, bladeHeight));
    vec3 eye = inverse(camera.view)[3].xyz;
    vec3 viewDirection = normalize(eye - worldPosition);
    vec3 n = normalize(normal);
    if (dot(n, viewDirection) < 0.0) n = -n;
    vec3 lightDirection = normalize(vec3(-0.6, 0.7, -0.4));
    vec3 sunlight = vec3(1.0, 0.91, 0.70);
    float diffuse = clamp(0.4 + 0.6 * dot(n, lightDirection), 0.0, 1.0);
    float transmission = pow(max(dot(-viewDirection, lightDirection), 0.0), 3.0);
    float rootShade = mix(0.60, 1.0, sqrt(bladeHeight));
    vec3 ambient = mix(vec3(0.55, 0.65, 0.40), vec3(0.65, 0.75, 0.85), abs(n.y));
    vec3 color = albedo * (ambient + sunlight * (0.85 * diffuse + 0.65 * transmission)) * rootShade;
    vec3 halfVector = normalize(lightDirection + viewDirection);
    float highlight = pow(max(dot(n, halfVector), 0.0), 24.0) * 0.10 * bladeHeight;
    color += sunlight * highlight;
    float haze = 1.0 - exp(-length(eye - worldPosition) * 0.004);
    color = mix(color, vec3(0.69, 0.78, 0.75), haze);
    outColor = vec4(color, 1.0);
}
