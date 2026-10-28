#pragma once

#include <glm/glm.hpp>
#include <chrono>

#include "Model.h"
#include "Blades.h"

using namespace std::chrono;

enum CullingTestBits {
    OrientationCulling = 1u,
    FrustumCulling = 2u,
    DistanceCulling = 4u
};

struct Time {
    float deltaTime = 0.0f;
    float totalTime = 0.0f;
    float windStrength = 1.0f;
    float windDirection = 0.0f;
    glm::vec4 collider = glm::vec4(0.0f, 1.8f, 0.0f, 0.65f);
    glm::vec4 colliderVelocity = glm::vec4(0.0f);
    glm::uvec4 performanceSettings = glm::uvec4(NUM_BLADES, OrientationCulling | FrustumCulling | DistanceCulling, 0u, 0u);
};
static_assert(sizeof(Time) == 64, "Time must match the compute shader uniform layout");

class Scene {
private:
    Device* device;
    
    VkBuffer timeBuffer;
    VkDeviceMemory timeBufferMemory;
    Time time;
    
    void* mappedData;

    std::vector<Model*> models;
    std::vector<Blades*> blades;
    Model* colliderModel = nullptr;
    bool colliderDragging = false;

high_resolution_clock::time_point startTime = high_resolution_clock::now();

public:
    Scene() = delete;
    Scene(Device* device);
    ~Scene();

    const std::vector<Model*>& GetModels() const;
    const std::vector<Blades*>& GetBlades() const;
    
    void AddModel(Model* model);
    void AddBlades(Blades* blades);

    VkBuffer GetTimeBuffer() const;

    void UpdateTime();
    void AdjustWind(float strengthDelta, float directionDelta);
    void ResetWind();
    void SetColliderModel(Model* model);
    void MoveCollider(float deltaX, float deltaZ);
    glm::vec3 GetColliderPosition() const;
    float GetColliderRadius() const;
    Model* GetColliderModel() const;
    void BeginColliderDrag();
    void DragColliderTo(const glm::vec3& position);
    void EndColliderDrag();
    void SetActiveBladeCount(uint32_t count);
    void ToggleCullingTest(uint32_t test);
    uint32_t GetActiveBladeCount() const;
    uint32_t GetCullingMask() const;
};
