#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(set = 0, binding = 1) uniform SunBufferObject {
    vec4 direction;
    vec4 color;
} sun;

// TODO: Declare fragment shader inputs

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in float fragHeight;
layout(location = 2) flat in float fragVariation;
layout(location = 3) in vec3 fragViewDirection;

layout(location = 0) out vec4 outColor;

void main() {
    // TODO: Compute fragment color

    vec3 lightDir = sun.direction.xyz;
    vec3 lightColor = sun.color.rgb * sun.direction.w;

    vec3 rootColor = vec3(0.10, 0.30, 0.08);
    vec3 tipColor = vec3(0.32, 0.65, 0.18);
    vec3 albedo = mix(rootColor, tipColor, fragHeight);
    albedo *= mix(0.9, 1.1, fragVariation);

    vec3 normal = normalize(fragNormal);
    float diffuse = abs(dot(normal, lightDir));

    vec3 viewDir = normalize(fragViewDirection);
    vec3 facingNormal = dot(normal, viewDir) < 0.0 ? -normal : normal;
    float backLight = max(dot(-facingNormal, lightDir), 0.0);
    float viewAlignment = pow(max(dot(-lightDir, viewDir), 0.0), 4.0);
    float transmission = backLight * viewAlignment;

    float ambientStrength = mix(0.04, 0.25, sun.color.w);
    float diffuseStrength = 0.75;
    float transmissionStrength = 0.35;

    vec3 color = albedo * (vec3(ambientStrength) + lightColor * diffuseStrength * diffuse);
    vec3 transmissionColor = albedo * vec3(1.2, 1.0, 0.6);
    color += transmissionColor * lightColor * transmissionStrength * transmission;

    outColor = vec4(color, 1.0);
}
