#include <vector>
#include <random>
#include "Blades.h"
#include "BufferUtils.h"

Blades::Blades(Device* device, VkCommandPool commandPool, float planeDim, GrassPreset preset, uint32_t fieldCount)
    : Model(device, commandPool, {}, {}) {
    bladeCount = preset == GrassPreset::Field ? fieldCount : (preset == GrassPreset::Patch ? 16u : 1u);
    std::mt19937 generator(1337);
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    auto randomFloat = [&]() { return distribution(generator); };
    std::vector<Blade> blades;
    blades.reserve(bladeCount);

    for (uint32_t i = 0; i < bladeCount; i++) {
        Blade currentBlade = Blade();

        glm::vec3 bladeUp(0.0f, 1.0f, 0.0f);

        // Generate positions and direction (v0)
        float x = (randomFloat() - 0.5f) * planeDim;
        float y = 0.0f;
        float z = (randomFloat() - 0.5f) * planeDim;
        float direction = randomFloat() * 2.f * 3.14159265f;
        if (preset == GrassPreset::Single || preset == GrassPreset::Bent) {
            x = z = direction = 0.0f;
        } else if (preset == GrassPreset::Patch) {
            x = (static_cast<float>(i % 4) - 1.5f) * 1.5f;
            z = (static_cast<float>(i / 4) - 1.5f) * 1.5f;
            direction = static_cast<float>(i % 4) * 3.14159265f / 4.0f;
        }
        glm::vec3 bladePosition(x, y, z);
        currentBlade.v0 = glm::vec4(bladePosition, direction);

        // Bezier point and height (v1)
        float height = MIN_HEIGHT + (randomFloat() * (MAX_HEIGHT - MIN_HEIGHT));
        if (bladeCount == 1) height = 2.2f;
        if (preset == GrassPreset::Patch) height = MIN_HEIGHT + static_cast<float>(i / 4) * (MAX_HEIGHT - MIN_HEIGHT) / 3.0f;
        currentBlade.v1 = glm::vec4(bladePosition + bladeUp * height, height);

        // Physical model guide and width (v2)
        float width = MIN_WIDTH + (randomFloat() * (MAX_WIDTH - MIN_WIDTH));
        if (bladeCount == 1) width = MAX_WIDTH;
        currentBlade.v2 = glm::vec4(bladePosition + bladeUp * height, width);
        // A fixed diagnostic curve, not a simulated force or a new rest state.
        if (preset == GrassPreset::Bent) {
            currentBlade.v2 = glm::vec4(bladePosition + glm::vec3(0.8f, height * 0.72f, 0.15f), width);
        }

        // Up vector and stiffness coefficient (up)
        float stiffness = MIN_BEND + (randomFloat() * (MAX_BEND - MIN_BEND));
        currentBlade.up = glm::vec4(bladeUp, stiffness);

        blades.push_back(currentBlade);
    }

    BladeDrawIndirect indirectDraw;
    indirectDraw.vertexCount = bladeCount;
    indirectDraw.instanceCount = 1;
    indirectDraw.firstVertex = 0;
    indirectDraw.firstInstance = 0;

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(bladeCount) * sizeof(Blade);
    initialBlades = blades;
    BufferUtils::CreateBufferFromData(device, commandPool, blades.data(), bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bladesBuffer, bladesBufferMemory);
    // Maximum-capacity output; only the indirect count's prefix is valid after culling.
    BufferUtils::CreateBuffer(device, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, culledBladesBuffer, culledBladesBufferMemory);
    BufferUtils::CreateBufferFromData(device, commandPool, blades.data(), bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, initialBladesBuffer, initialBladesBufferMemory);
    BufferUtils::CreateBufferFromData(device, commandPool, &indirectDraw, sizeof(BladeDrawIndirect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, numBladesBuffer, numBladesBufferMemory);
    BufferUtils::CreateBuffer(device, static_cast<VkDeviceSize>(bladeCount) * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, visibleIdsBuffer, visibleIdsBufferMemory);
}

uint32_t Blades::GetBladeCount() const {
    return bladeCount;
}

VkBuffer Blades::GetBladesBuffer() const {
    return bladesBuffer;
}

VkBuffer Blades::GetCulledBladesBuffer() const {
    return culledBladesBuffer;
}

VkBuffer Blades::GetNumBladesBuffer() const {
    return numBladesBuffer;
}

Blades::~Blades() {
    vkDestroyBuffer(device->GetVkDevice(), visibleIdsBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), visibleIdsBufferMemory, nullptr);
    vkDestroyBuffer(device->GetVkDevice(), initialBladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), initialBladesBufferMemory, nullptr);
    vkDestroyBuffer(device->GetVkDevice(), bladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), bladesBufferMemory, nullptr);
    vkDestroyBuffer(device->GetVkDevice(), culledBladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), culledBladesBufferMemory, nullptr);
    vkDestroyBuffer(device->GetVkDevice(), numBladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), numBladesBufferMemory, nullptr);
}
