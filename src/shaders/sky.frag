#version 450

layout(location = 0) in vec3 viewRay;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;
layout(set = 1, binding = 0) uniform Time {
    float deltaTime; float totalTime; uint culling; uint simulate;
    float windStrength; float maxDistance; uint distanceBuckets; float orientationThreshold;
    float windAngle; uint collision; uint occlusion; uint colliderEnabled;
    vec4 collider;
} settings;

void main() {
    vec3 ray = normalize(viewRay);
    vec3 horizon = vec3(0.69, 0.78, 0.75);
    vec3 zenith = vec3(0.15, 0.40, 0.65);
    vec3 sky = mix(horizon, zenith, pow(max(ray.y, 0.0), 0.45));
    vec3 sunDirection = normalize(vec3(-0.6, 0.7, -0.4));
    float sun = max(dot(ray, sunDirection), 0.0);
    sky += vec3(1.0, 0.78, 0.43) * (0.22 * pow(sun, 24.0) + 0.65 * pow(sun, 1200.0));
    // A soft earth-colored lower hemisphere keeps the field against a horizon.
    vec3 earth = vec3(0.20, 0.26, 0.12);
    sky = mix(sky, earth, smoothstep(0.0, 0.22, -ray.y));
    gl_FragDepth = 1.0;
    vec3 eye = inverse(camera.view)[3].xyz;
    vec3 offset = eye - settings.collider.xyz;
    float b = dot(offset, ray);
    float discriminant = b*b - dot(offset, offset) + settings.collider.w*settings.collider.w;
    if (settings.colliderEnabled != 0u && discriminant >= 0.0) {
        float hit = -b - sqrt(discriminant);
        if (hit < 0.0) hit = -b + sqrt(discriminant);
        if (hit > 0.0) {
            vec3 point = eye + hit * ray;
            vec4 clip = camera.proj * camera.view * vec4(point, 1.0);
            float depth = clip.z / clip.w;
            if (depth >= 0.0 && depth <= 1.0) {
                vec3 n = normalize(point - settings.collider.xyz);
                float diffuse = max(dot(n, sunDirection), 0.0);
                vec3 halfDirection = normalize(sunDirection - ray);
                float highlight = pow(max(dot(n, halfDirection), 0.0), 48.0);
                sky = vec3(0.55, 0.20, 0.065) * (0.35 + 0.65 * diffuse) + vec3(0.6)*highlight;
                gl_FragDepth = depth;
            }
        }
    }
    outColor = vec4(sky, 1.0);
}
