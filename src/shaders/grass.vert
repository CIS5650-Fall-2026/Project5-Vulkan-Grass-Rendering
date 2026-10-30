
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform ModelBufferObject { mat4 model; };

layout(location = 0) in vec4 in_v0;
layout(location = 1) in vec4 in_v1;
layout(location = 2) in vec4 in_v2;
layout(location = 3) in vec4 in_up;

layout(location = 0) out vec4 out_v0;
layout(location = 1) out vec4 out_v1;
layout(location = 2) out vec4 out_v2;
layout(location = 3) out vec4 out_up;

vec4 applyModel(in vec4 v) { return vec4((model * vec4(v.xyz, 1)).xyz, v.w); }

void main() {
    out_v0 = applyModel(in_v0);
    out_v1 = applyModel(in_v1);
    out_v2 = applyModel(in_v2);

    mat3 invT = mat3(transpose(inverse(model)));
    out_up = vec4(normalize(invT * in_up.xyz), in_up.w);
}
