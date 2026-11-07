#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
    vec4 camPos;
} camera;

// TODO: Declare fragment shader inputs
layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 bladeColor;
layout(location = 2) in float fragHeight;

layout(location = 0) out vec4 outColor;

void main() {
    // TODO: Compute fragment color

    float heightLightAdjustment = clamp(fragHeight/3.0 + 0.6,0,1.5);

    float lambertian = abs(dot(normal.xyz, vec3(0.2, -0.8, 0.0)));
    vec3 grassColor = vec3(0.2, 0.55, 0.28) + clamp(lambertian, 0.0, 1.0)*vec3(0.1, 0.3, 0.2);
    outColor = vec4(grassColor*heightLightAdjustment, 1);


    //SKYBOX
    if (bladeColor.r > -0.5) {
        outColor = vec4(bladeColor, 1);
        if (bladeColor.r > 0.7) {
            if(lambertian > 0.5) {
                heightLightAdjustment = 1.1;
            }
            outColor = vec4(clamp(heightLightAdjustment, 0.0, 1.5)*(bladeColor + clamp(lambertian, 0.0, 1.0)*bladeColor), 1);
        }
    }
}
