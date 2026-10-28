#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 cameraPosition = (inverse(camera.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vec3 viewDirection = normalize(cameraPosition - fragWorldPos);
    vec3 normal = normalize(fragNormal);
    if (dot(normal, viewDirection) < 0.0) {
        normal = -normal;
    }

    vec3 lightDir = normalize(vec3(-0.45, 0.85, 0.3));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float translucency = max(dot(-normal, lightDir), 0.0);
    float skyAmbient = 0.28 + 0.2 * max(normal.y, 0.0);
    float ambientOcclusion = mix(0.35, 1.0, smoothstep(0.0, 0.5, fragWorldPos.y));
    vec3 halfVector = normalize(lightDir + viewDirection);
    float specular = pow(max(dot(normal, halfVector), 0.0), 24.0) * 0.12;
    vec3 color = fragColor * (skyAmbient + diffuse * 0.85 + translucency * 0.25) * ambientOcclusion
        + vec3(0.8, 0.92, 0.55) * specular * ambientOcclusion;
    outColor = vec4(color, 1.0);
}
