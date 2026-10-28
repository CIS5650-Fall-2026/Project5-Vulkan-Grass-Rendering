#version 450

layout(location = 0) out vec2 skyNdc;

void main() {
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );
    skyNdc = positions[gl_VertexIndex];
    gl_Position = vec4(skyNdc, 1.0, 1.0);
}
