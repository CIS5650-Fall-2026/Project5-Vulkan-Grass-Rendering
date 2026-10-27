#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare fragment shader inputs

layout(location = 0) in vec3 fsNor;
layout(location = 1) in float fsAO;

layout(location = 0) out vec4 outColor;

const vec3 LIGHT_DIR = normalize(vec3(1.0, 1.0, 0.5));
const float AMBIENT = 0.4;

void main() {
    // TODO: Compute fragment color
    float lambertFactor = abs(dot(normalize(fsNor), LIGHT_DIR));
    lambertFactor = (lambertFactor * 0.5) + 0.5;
    lambertFactor = lambertFactor * lambertFactor;
    float lightFactor = (1.0 - AMBIENT) * lambertFactor + AMBIENT * fsAO;
    vec3 col = vec3(0.33, 0.87, 0.46) * lightFactor;
    outColor = vec4(col, 1.0);
}
