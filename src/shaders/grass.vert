
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform ModelBufferObject {
    mat4 model;
};

// Declare vertex shader inputs and outputs
layout(location = 0) in vec4 v0;
layout(location = 1) in vec4 v1;
layout(location = 2) in vec4 v2;
layout(location = 3) in vec4 up;

layout(location = 0) out vec4 tesc_v1;
layout(location = 1) out vec4 tesc_v2;
layout(location = 2) out vec4 tesc_vup;



void main() {
    // Transform xyz to world space, keep .w (orientation / height / width / stiffness) untouched
    gl_Position = vec4((model * vec4(v0.xyz, 1.0)).xyz, v0.w);
    tesc_v1 = vec4((model * vec4(v1.xyz, 1.0)).xyz, v1.w);
    tesc_v2 = vec4((model * vec4(v2.xyz, 1.0)).xyz, v2.w);
    tesc_vup = vec4((model * vec4(up.xyz, 0.0)).xyz, up.w);
}
