#include <vector>
#include "Blades.h"
#include "BufferUtils.h"
#include "Instance.h"

float generateRandomFloat() {
    return rand() / (float)RAND_MAX;
}

Blades::Blades(Device* device, VkCommandPool commandPool, const Terrain& terrain, unsigned int count, bool hostVisibleCulled)
    : Model(device, commandPool, {}, {}), count(count) {
    std::vector<Blade> blades;
    blades.reserve(count);
    float size = terrain.GetSize();

    while (blades.size() < count) {
        float x = (generateRandomFloat() - 0.5f) * size;
        float z = (generateRandomFloat() - 0.5f) * size;
        float lushness = terrain.LushnessAt(x, z);

        if (generateRandomFloat() > 0.7f + 0.3f * lushness) {
            continue;
        }

        Blade currentBlade = Blade();

        glm::vec3 bladeUp(0.0f, 1.0f, 0.0f);

        // Generate positions and direction (v0)
        float direction = generateRandomFloat() * 2.f * 3.14159265f;
        glm::vec3 bladePosition(x, terrain.HeightAt(x, z), z);
        currentBlade.v0 = glm::vec4(bladePosition, direction);

        // the sparsest areas grow about 3/4 as large as the lushest
        float fieldScale = 0.5f + 0.5f * lushness;

        // Bezier point and height (v1)
        float height = glm::mix(MIN_HEIGHT, MAX_HEIGHT, fieldScale) * (0.85f + 0.3f * generateRandomFloat());
        currentBlade.v1 = glm::vec4(bladePosition + bladeUp * height, height);

        // Physical model guide and width (v2)
        float width = glm::mix(MIN_WIDTH, MAX_WIDTH, fieldScale) * (0.85f + 0.3f * generateRandomFloat());
        currentBlade.v2 = glm::vec4(bladePosition + bladeUp * height, width);

        // Up vector and stiffness coefficient (up)
        float stiffness = MIN_BEND + (generateRandomFloat() * (MAX_BEND - MIN_BEND));
        currentBlade.up = glm::vec4(bladeUp, stiffness);

        blades.push_back(currentBlade);
    }

    BladeDrawIndirect indirectDraw;
    indirectDraw.vertexCount = count;
    indirectDraw.instanceCount = 1;
    indirectDraw.firstVertex = 0;
    indirectDraw.firstInstance = 0;

    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(count) * sizeof(Blade);
    VkMemoryPropertyFlags culledMemory = hostVisibleCulled ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    BufferUtils::CreateBufferFromData(device, commandPool, blades.data(), bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, bladesBuffer, bladesBufferMemory);
    BufferUtils::CreateBuffer(device, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, culledMemory, culledBladesBuffer, culledBladesBufferMemory);

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device->GetVkDevice(), culledBladesBuffer, &requirements);
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(device->GetInstance()->GetPhysicalDevice(), &memoryProperties);
    culledMemoryFlags = memoryProperties.memoryTypes[device->GetInstance()->GetMemoryTypeIndex(requirements.memoryTypeBits, culledMemory)].propertyFlags;
    // TRANSFER_SRC lets bench runs read back the drawn blade count
    BufferUtils::CreateBufferFromData(device, commandPool, &indirectDraw, sizeof(BladeDrawIndirect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, numBladesBuffer, numBladesBufferMemory);
}

unsigned int Blades::GetCount() const {
    return count;
}

VkMemoryPropertyFlags Blades::GetCulledMemoryFlags() const {
    return culledMemoryFlags;
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
    vkDestroyBuffer(device->GetVkDevice(), bladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), bladesBufferMemory, nullptr);
    vkDestroyBuffer(device->GetVkDevice(), culledBladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), culledBladesBufferMemory, nullptr);
    vkDestroyBuffer(device->GetVkDevice(), numBladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), numBladesBufferMemory, nullptr);
}
