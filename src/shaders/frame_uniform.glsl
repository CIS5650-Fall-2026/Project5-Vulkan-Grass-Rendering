#ifndef VULKAN_GRASS_FRAME_UNIFORM_GLSL
#define VULKAN_GRASS_FRAME_UNIFORM_GLSL

layout(std140, set = 0, binding = 0) uniform FrameUniform {
    mat4 view;
    mat4 proj;
    vec4 cameraTime;
    vec4 wind;
    vec4 gravity;
    vec4 collider;
    vec4 parameters;
    vec4 viewport;
    uvec4 flags;
    vec4 lighting;
    vec4 terrain;
    mat4 lightViewProj;
    vec4 sunColor;
    vec4 atmosphere;
    uvec4 renderControl;
} frame;

const uint ENHANCED_RENDERING_BIT = 4u;

const uint RENDER_SKY_BIT = 1u;
const uint RENDER_ROOT_COLOR_BIT = 2u;
const uint RENDER_ROUNDED_NORMALS_BIT = 4u;
const uint RENDER_TRANSMISSION_BIT = 8u;
const uint RENDER_SPECULAR_BIT = 16u;
const uint RENDER_FOG_BIT = 32u;
const uint RENDER_SHADOWS_BIT = 64u;
const uint RENDER_ALL_BITS = 127u;

bool enhancedRenderingEnabled() {
    return (frame.flags.w & ENHANCED_RENDERING_BIT) != 0u;
}

bool renderFeatureEnabled(uint featureBit) {
    return enhancedRenderingEnabled()
        && (frame.renderControl.x & featureBit) != 0u;
}

#endif
