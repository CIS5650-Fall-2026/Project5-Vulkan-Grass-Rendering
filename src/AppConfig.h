#pragma once

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum CullBits : uint32_t {
    CullOrientation = 1u, CullFrustum = 2u, CullDistance = 4u, CullOcclusion = 8u
};
enum FeatureBits : uint32_t {
    SimulateForces = 1u, InteractiveSphere = 2u, BetterShading = 4u, Wireframe = 8u,
    ShowOverlay = 16u
};

struct RunOptions {
    uint32_t width = 1280, height = 720;
    uint32_t bladeCount = 524288, seed = 5650;
    uint32_t culling = CullOrientation | CullFrustum | CullDistance;
    bool lod = true, occlusion = true, hidden = false, help = false, verify = false;
#ifdef NDEBUG
    bool validation = false;
#else
    bool validation = true;
#endif
    uint32_t warmup = 120, frames = 300;
    float fixedDelta = 1.0f / 60.0f;
    float fieldSize = 64.0f, maxDistance = 50.0f;
    std::string preset = "final", camera = "overview";
    std::string benchmarkPath, captureDir;
};

struct Blade {
    glm::vec4 v0; // root and lateral orientation
    glm::vec4 v1; // curve guide and rest height
    glm::vec4 v2; // physical tip and width
    glm::vec4 up; // surface normal and stiffness
};
static_assert(sizeof(Blade) == 64, "Blade must match the four-vec4 shader layout");

struct SceneVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

struct SceneDraw {
    uint32_t firstIndex = 0, indexCount = 0;
    uint32_t material = 0; // 0 ground, 1 stone, 2 interactive sphere
    bool movable = false;
};

struct alignas(16) FrameUniform {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec4 cameraTime{0.0f}; // camera xyz, simulation time
    glm::vec4 wind{4.0f, 0.0f, 1.0f, 1.0f}; // base wind xyz, gust frequency
    glm::vec4 gravity{0.0f, -1.5f, 0.0f, 0.25f}; // acceleration xyz, front factor
    glm::vec4 collider{0.0f, 0.75f, 0.0f, 1.0f}; // center xyz, radius
    glm::vec4 parameters{1.0f / 60.0f, 50.0f, 8.0f, 12.0f}; // dt, max distance, buckets, fixed segments
    glm::vec4 viewport{1280.0f, 720.0f, 5.0f, 40.0f}; // framebuffer dimensions, near/far LOD distance
    glm::uvec4 flags{0u}; // blade count, cull mask, adaptive LOD, feature bits
    glm::vec4 lighting{-0.4f, 0.8f, 0.25f, 1.0f}; // sun direction, exposure
    glm::vec4 terrain{64.0f, 0.0f, 0.0f, 0.0f}; // field size, reserved
};
static_assert(sizeof(FrameUniform) == 272, "Frame uniform must match GLSL std140");
static_assert(offsetof(FrameUniform, flags) == 224, "Frame uniform member offsets must match GLSL");

struct DrawIndirect {
    uint32_t vertexCount = 0, instanceCount = 1, firstVertex = 0, firstInstance = 0;
};
static_assert(sizeof(DrawIndirect) == 16, "Indirect command must match VkDrawIndirectCommand");

struct VisibilityStats {
    uint32_t inputCount = 0;
    uint32_t orientationRejected = 0, frustumRejected = 0, distanceRejected = 0;
    uint32_t occlusionRejected = 0, visibleCount = 0, reserved0 = 0, reserved1 = 0;
};
static_assert(sizeof(VisibilityStats) == 32, "Visibility statistics must match the shader layout");

RunOptions ParseOptions(int argc, char** argv);
void PrintHelp();
