#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 0) uniform CameraBufferObject {
    mat4 view;
    mat4 proj;
} camera;

// TODO: Declare tessellation evaluation shader inputs and outputs
layout (location = 0) in vec4 in_v0[];
layout (location = 1) in vec4 in_v1[];
layout (location = 2) in vec4 in_v2[];
layout (location = 3) in vec4 in_up[];

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 bladeColor;
layout(location = 2) out float fragHeight;

void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

	// TODO: Use u and v to parameterize along the grass blade and output positions for each vertex of the grass blade
    float width = in_v2[0].w;
    float theta = in_v0[0].w - 1.5708; // orientation of grass blade - 90 degrees to get t1 vector
    vec3 v0 = in_v0[0].xyz;
    vec3 v1 = in_v1[0].xyz;
    vec3 v2 = in_v2[0].xyz;
    vec3 up = in_up[0].xyz;

    mat3 orientationMatrix = mat3(
        cos(theta), 0.0,    -1.0 * sin(theta),  // col 1 
        0.0,        1.0,    0.0,                //col 2
        sin(theta), 0.0,    cos(theta)          // col3
    );

    vec3 t1 = orientationMatrix * vec3(1.0, 0.0, 0.0);

    //SKYBOX 
    if (in_up[0].w < 0) {
        v2 = in_v1[0].xyz;
        t1 = normalize(in_v2[0].xyz);
    }

    vec3 a = v0 + v * (v1 - v0);
    vec3 b = v1 + v * (v2 - v1);
    vec3 c = a + v * (b - a);

    vec3 c0 = c - width*t1;
    vec3 c1 = c + width*t1;
    vec3 t0 = (b - a) / length(b - a);
    vec3 n = cross(t0, t1) / length(cross(t0, t1));
    normal = n;
    float t = u + 0.5 * v - u*v;

    //SKYBOX 
    if (in_up[0].w < 0) {
        t = u;
    }

    vec3 p = (1.0 - t) * c0 + t *c1;

    vec4 worldpos = (gl_in[0].gl_Position + vec4(p, 1));
    gl_Position = camera.proj * camera.view * (gl_in[0].gl_Position + vec4(p, 1));
    fragHeight = (gl_in[0].gl_Position + vec4(p, 1)).y;
    //SKYBOX 
    if (in_up[0].w < 0) {
        //bladeColor = vec3(0.4, .79, .79) * (1.5 - clamp(worldpos.y/50.0, 0, 0.7));
        bladeColor = vec3(0.44, .85, .92);
        if (in_up[0].w < -1) {
            bladeColor = vec3(0.79, .4, .82);
        }
    }
    else {
        bladeColor = vec3(-1, 1, 0);
    }
}
