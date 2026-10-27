#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include "Model.h"

constexpr static unsigned int NUM_BLADES = 1 << 13;
constexpr static float MIN_HEIGHT = 1.3f;
constexpr static float MAX_HEIGHT = 2.5f;
constexpr static float MIN_WIDTH = 0.1f;
constexpr static float MAX_WIDTH = 0.14f;
constexpr static float MIN_BEND = 7.0f;
constexpr static float MAX_BEND = 13.0f;

enum class GrassPreset { Single, Bent, Patch, Field };

struct Blade {
    // Position and direction
    glm::vec4 v0;
    // Bezier point and height
    glm::vec4 v1;
    // Physical model guide and width
    glm::vec4 v2;
    // Up vector and stiffness coefficient
    glm::vec4 up;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Blade);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions = {};

        // v0
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Blade, v0);

        // v1
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Blade, v1);

        // v2
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Blade, v2);

        // up
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Blade, up);

        return attributeDescriptions;
    }
};

struct BladeDrawIndirect {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
};

static_assert(sizeof(Blade) == 64, "Blade must match std430");
static_assert(sizeof(BladeDrawIndirect) == sizeof(VkDrawIndirectCommand), "Indirect layout");

class Blades : public Model {
private:
    VkBuffer bladesBuffer;
    VkBuffer culledBladesBuffer;
    VkBuffer numBladesBuffer;
    VkBuffer initialBladesBuffer;
    VkBuffer visibleIdsBuffer;

    VkDeviceMemory bladesBufferMemory;
    VkDeviceMemory culledBladesBufferMemory;
    VkDeviceMemory numBladesBufferMemory;
    VkDeviceMemory initialBladesBufferMemory;
    VkDeviceMemory visibleIdsBufferMemory;
    std::vector<Blade> initialBlades;
    uint32_t bladeCount;

public:
    Blades(Device* device, VkCommandPool commandPool, float planeDim, GrassPreset preset = GrassPreset::Field, uint32_t fieldCount = NUM_BLADES);
    uint32_t GetBladeCount() const;
    VkBuffer GetBladesBuffer() const;
    VkBuffer GetCulledBladesBuffer() const;
    VkBuffer GetNumBladesBuffer() const;
    VkBuffer GetInitialBladesBuffer() const { return initialBladesBuffer; }
    VkBuffer GetVisibleIdsBuffer() const { return visibleIdsBuffer; }
    const std::vector<Blade>& GetInitialBlades() const { return initialBlades; }
    ~Blades();
};
