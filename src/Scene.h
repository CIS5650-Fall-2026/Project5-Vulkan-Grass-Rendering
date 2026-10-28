#pragma once

#include <glm/glm.hpp>
#include <chrono>

#include "Model.h"
#include "Blades.h"

using namespace std::chrono;

enum CullingTest : uint32_t {
    OrientationCulling = 1u,
    FrustumCulling = 2u,
    DistanceCulling = 4u
};

struct alignas(16) Time {
    float deltaTime = 0.0f;
    float totalTime = 0.0f;
    glm::vec2 padding = glm::vec2(0.0f);

    // Aligned vec4 fields start at byte 16
    // Forces use each blade's space
    alignas(16) glm::vec4 gravity = glm::vec4(0.0f, -1.0f, 0.0f, 9.81f);
    alignas(16) glm::vec4 wind = glm::vec4(1.0f, 0.0f, 0.35f, 4.0f);
    // Facing threshold, max distance, buckets, tolerance
    alignas(16) glm::vec4 cullingParameters = glm::vec4(0.9f, 30.0f, 8.0f, 0.025f);
    alignas(16) glm::uvec4 options = glm::uvec4(7u, 0u, 0u, 0u);
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

    void UpdateTime();
    void ToggleCulling(CullingTest test);
    uint32_t GetCullingMask() const;
};
