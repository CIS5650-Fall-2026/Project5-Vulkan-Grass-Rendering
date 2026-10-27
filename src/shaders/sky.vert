#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) out vec3 fragDirection;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    vec4 viewDirection = inverse(camera.proj) * vec4(position, 1.0, 1.0);

    fragDirection = transpose(mat3(camera.view)) * viewDirection.xyz;
    gl_Position = vec4(position, 1.0, 1.0);
}
