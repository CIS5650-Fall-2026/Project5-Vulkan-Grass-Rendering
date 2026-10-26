#pragma once

#include <glm/glm.hpp>
#include <chrono>

#include "Model.h"
#include "Blades.h"

using namespace std::chrono;

struct Time {
    float deltaTime = 0.0f;
    float totalTime = 0.0f;
    uint32_t culling = 7;
    uint32_t simulate = 1;
    float windStrength = 6.0f;
    float maxDistance = 40.0f;
    uint32_t distanceBuckets = 8;
    float orientationThreshold = 0.9f;
    float windAngle = 0.0f;
    uint32_t collision = 1;
    uint32_t occlusion = 1;
    uint32_t colliderEnabled = 1;
    glm::vec4 collider = glm::vec4(0.0f, 1.5f, 0.0f, 1.5f);
};

class Scene {
private:
    Device* device;
    
    VkBuffer timeBuffer;
    VkDeviceMemory timeBufferMemory;
    Time time;
    
    void* mappedData;

    std::vector<Model*> models;
    std::vector<Blades*> blades;

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

    void UpdateTime(float fixedDelta = 0.0f);
    void SetSettings(uint32_t culling, bool simulate, float windStrength);
    void ToggleCulling(uint32_t bit);
    void ToggleSimulation();
    void MoveCollider(float x, float z);
    void SetInteraction(bool enabled, bool collision, bool occlusion);
    void ToggleOcclusion();
    void AdjustWind(float strengthChange, float angleChange);
    const Time& GetSettings() const { return time; }
};
