#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "Device.h"

// Matches one element of the sphere SSBO in compute.comp.
struct Collider {
    glm::vec4 centerRadius; // xyz = world-space center, w = radius
};

class Colliders {
private:
    Device* device;
    std::vector<Collider> colliders;

    VkBuffer collidersBuffer = VK_NULL_HANDLE;
    VkDeviceMemory collidersBufferMemory = VK_NULL_HANDLE;
    void* mappedData = nullptr;

public:
    Colliders(Device* device, uint32_t colliderCount);
    ~Colliders();

    // Updates the CPU sphere data and copies it into the persistently mapped SSBO.
    void Update(float totalTime);

    uint32_t GetNumColliders() const;
    VkBuffer GetCollidersBuffer() const;
    const Collider& GetCollider(uint32_t index) const;
};
