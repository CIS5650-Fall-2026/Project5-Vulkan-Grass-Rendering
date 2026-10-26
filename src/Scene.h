#pragma once

#include <glm/glm.hpp>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include "Model.h"
#include "Blades.h"

enum class SimulationMode { Static, Copy, Physics };
enum class SphereMotion : uint32_t { Manual = 0, Orbit = 1, Pass = 2 };
enum ForceMask : uint32_t { Gravity = 1u, Recovery = 2u, Wind = 4u };
enum CullingMask : uint32_t { FrustumCulling = 1u, OrientationCulling = 2u, DistanceCulling = 4u, AllCulling = 7u };

// The first five std140 slots are shared with compute.comp. Grass LOD reads
// the appended slots at offsets 80 and 96; physics/culling offsets stay fixed.
struct alignas(16) SimulationUniforms {
    float stepSeconds = 1.0f / 120.0f;
    float startTime = 0.0f;
    float gravityStrength = 3.0f;
    float windStrength = 8.0f;
    glm::vec4 windDirection = glm::vec4(1.0f, 0.0f, 0.35f, 0.0f);
    uint32_t stepCount = 0;
    uint32_t forceMask = Gravity | Recovery | Wind;
    uint32_t reset = 0;
    uint32_t physics = 1;
    uint32_t cullingMask = AllCulling;
    uint32_t distanceBuckets = 4;
    float orientationThreshold = 0.9f;
    float maxDistance = 60.0f;
    glm::vec4 cullingSettings = glm::vec4(0.01f, 0, 0, 0); // x: world-space frustum tolerance
    glm::vec4 lodDistances = glm::vec4(15.0f, 45.0f, 8.0f, 2.0f); // near, far, max/min along level
    glm::uvec4 lodControls = glm::uvec4(1u, 0u, 0u, 0u); // enabled, color visualization, reserved
    glm::vec4 spherePath[9] = {}; // world centre/radius at start and up to 8 fixed substeps
    glm::vec4 contactSettings = glm::vec4(35.0f, 0.06f, 0.08f, 0.0f); // strength, skin, step/height cap
    glm::uvec4 contactControls = glm::uvec4(0u); // enabled, visible, motion, reserved
};
static_assert(sizeof(SimulationUniforms) == 288, "Scene UBO size must match std140");
static_assert(offsetof(SimulationUniforms, windDirection) == 16, "Wind slot offset");
static_assert(offsetof(SimulationUniforms, stepCount) == 32, "Control slot offset");
static_assert(offsetof(SimulationUniforms, cullingMask) == 48, "Culling slot offset");
static_assert(offsetof(SimulationUniforms, cullingSettings) == 64, "Tolerance slot offset");
static_assert(offsetof(SimulationUniforms, lodDistances) == 80, "LOD distances slot offset");
static_assert(offsetof(SimulationUniforms, lodControls) == 96, "LOD controls slot offset");
static_assert(offsetof(SimulationUniforms, spherePath) == 112, "Interaction path offset");
static_assert(offsetof(SimulationUniforms, contactSettings) == 256, "Contact settings offset");
static_assert(offsetof(SimulationUniforms, contactControls) == 272, "Contact controls offset");

class Scene {
private:
    Device* device;
    VkBuffer timeBuffer;
    VkDeviceMemory timeBufferMemory;
    SimulationUniforms simulation;
    void* mappedData;
    SimulationMode mode;
    double fixedFrameSeconds = 0.0;
    double accumulator = 0.0;
    uint64_t simulationSteps = 0;
    bool paused = false;
    bool singleStepRequested = false;
    bool resetRequested = false;
    std::chrono::steady_clock::time_point lastWallTime;
    std::vector<Model*> models;
    std::vector<Blades*> blades;
    Model* sphereModel = nullptr; // Owned by main, like the other models.
    glm::vec3 sphereInitial = glm::vec3(-4.0f, 1.2f, 0.0f);
    glm::vec3 spherePosition = sphereInitial, motionOrigin = sphereInitial, movement = glm::vec3(0.0f);
    float sphereRadius = 1.1f, sphereSpeed = 3.0f, sphereTravel = 14.0f;
    uint64_t motionSteps = 0;
    void UpdateInteraction();
public:
    Scene() = delete;
    Scene(Device* device, SimulationMode mode = SimulationMode::Physics);
    ~Scene();
    const std::vector<Model*>& GetModels() const;
    const std::vector<Blades*>& GetBlades() const;
    void AddModel(Model* model);
    void AddBlades(Blades* blades);
    VkBuffer GetTimeBuffer() const;
    SimulationMode GetSimulationMode() const { return mode; }
    const SimulationUniforms& GetSimulation() const { return simulation; }
    double GetSimulationTime() const { return simulationSteps / 120.0; }
    uint64_t GetSimulationSteps() const { return simulationSteps; }
    bool IsPaused() const { return paused; }
    void Configure(double fixedDt, uint32_t forces, float wind, float gravity, bool startPaused);
    void ConfigureCulling(uint32_t mask, uint32_t buckets, float threshold, float maxDistance, float tolerance);
    void SetCulling(uint32_t mask);
    void CycleCulling();
    void ConfigureLod(bool enabled, bool colors, float nearDistance, float farDistance, float maximum, float minimum);
    void SetLod(bool enabled) { simulation.lodControls.x = enabled ? 1u : 0u; }
    void ToggleLodColors() { simulation.lodControls.y ^= 1u; }
    void ConfigureInteraction(Model* model, bool enabled, bool visible, const glm::vec3& position,
        float radius, float strength, float speed, float travel, SphereMotion motion);
    void SetInteraction(bool enabled) { simulation.contactControls.x = enabled ? 1u : 0u; }
    void SetSphereMotion(SphereMotion motion);
    void SetMovement(const glm::vec3& direction);
    void ResetSphere();
    void MoveSphereAway();
    bool HasSphere() const { return sphereModel != nullptr; }
    const glm::vec3& GetSpherePosition() const { return spherePosition; }
    void SetPaused(bool value);
    void Step();
    void Reset();
    void ToggleForce(uint32_t force);
    void DiscardElapsedTime();
    void UpdateTime();
};
