#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(set = 1, binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragMaterialColor;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) in vec3 fragWorldPosition;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 textureColor = texture(texSampler, fragTexCoord).rgb;
    if (fragMaterialColor.a < 0.5) {
        vec2 sphereUv = fragTexCoord;
        float grain = sin((sphereUv.y * 74.0 + sin(sphereUv.x * 18.0) * 1.4) * 6.2831853) * 0.035;
        vec2 cells = floor(sphereUv * vec2(72.0, 40.0));
        float speckle = fract(sin(dot(cells, vec2(127.1, 311.7))) * 43758.5453) - 0.5;
        vec3 baseColor = fragMaterialColor.rgb * (0.84 + grain + speckle * 0.12);
        vec3 normal = normalize(fragNormal);
        vec3 lightDirection = normalize(vec3(-0.4, 0.85, 0.35));
        float diffuse = max(dot(normal, lightDirection), 0.0);
        vec3 cameraPosition = (inverse(camera.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
        vec3 viewDirection = normalize(cameraPosition - fragWorldPosition);
        float rim = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.0) * 0.16;
        float lighting = 0.32 + diffuse * 0.78 + rim;
        vec3 halfVector = normalize(lightDirection + viewDirection);
        float specular = pow(max(dot(normal, halfVector), 0.0), 28.0) * 0.22;
        outColor = vec4(baseColor * lighting + vec3(0.55, 0.78, 1.0) * specular, 1.0);
    } else {
        outColor = vec4(mix(textureColor, fragMaterialColor.rgb, 1.0 - fragMaterialColor.a), 1.0);
    }
}
