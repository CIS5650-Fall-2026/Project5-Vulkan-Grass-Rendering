#version 450

layout(location = 0) out vec3 viewRay;
layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

void main() {
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    vec4 ray = inverse(camera.proj) * vec4(position, 1.0, 1.0);
    viewRay = transpose(mat3(camera.view)) * ray.xyz;
    gl_Position = vec4(position, 0.0, 1.0);
}
