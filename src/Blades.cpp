#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <fstream>
#include <cstdio>
#include "Blades.h"
#include "BufferUtils.h"

float generateRandomFloat() {
    return rand() / (float)RAND_MAX;
}

Blades::Blades(Device* device, VkCommandPool commandPool, float planeDim, uint32_t count) : Model(device, commandPool, {}, {}), bladeCount(count) {
    std::vector<Blade> blades;
    blades.reserve(bladeCount);

    for (uint32_t i = 0; i < bladeCount; i++) {
        Blade currentBlade = Blade();

        glm::vec3 bladeUp(0.0f, 1.0f, 0.0f);

        // Generate positions and direction (v0)
        float x = (generateRandomFloat() - 0.5f) * planeDim;
        float y = 0.0f;
        float z = (generateRandomFloat() - 0.5f) * planeDim;
        float direction = generateRandomFloat() * 2.f * 3.14159265f;
        glm::vec3 bladePosition(x, y, z);
        currentBlade.v0 = glm::vec4(bladePosition, direction);

        // Bezier point and height (v1)
        float height = MIN_HEIGHT + (generateRandomFloat() * (MAX_HEIGHT - MIN_HEIGHT));
        currentBlade.v1 = glm::vec4(bladePosition + bladeUp * height, height);

        // Physical model guide and width (v2)
        float width = MIN_WIDTH + (generateRandomFloat() * (MAX_WIDTH - MIN_WIDTH));
        currentBlade.v2 = glm::vec4(bladePosition + bladeUp * height, width);

        // Up vector and stiffness coefficient (up)
        float stiffness = MIN_BEND + (generateRandomFloat() * (MAX_BEND - MIN_BEND));
        currentBlade.up = glm::vec4(bladeUp, stiffness);

        blades.push_back(currentBlade);
    }

    BladeDrawIndirect indirectDraw;
    indirectDraw.vertexCount = bladeCount;
    indirectDraw.instanceCount = 1;
    indirectDraw.firstVertex = 0;
    indirectDraw.firstInstance = 0;

    BufferUtils::CreateBufferFromData(device, commandPool, blades.data(), bladeCount * sizeof(Blade), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bladesBuffer, bladesBufferMemory);
    BufferUtils::CreateBuffer(device, bladeCount * sizeof(Blade), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, culledBladesBuffer, culledBladesBufferMemory);
    BufferUtils::CreateBufferFromData(device, commandPool, &indirectDraw, sizeof(BladeDrawIndirect), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, numBladesBuffer, numBladesBufferMemory);
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
    vkDestroyBuffer(device->GetVkDevice(), bladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), bladesBufferMemory, nullptr);
    vkDestroyBuffer(device->GetVkDevice(), culledBladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), culledBladesBufferMemory, nullptr);
    vkDestroyBuffer(device->GetVkDevice(), numBladesBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), numBladesBufferMemory, nullptr);
}

uint32_t Blades::ReadBack(VkCommandPool commandPool, bool verify, const std::string& dumpPath) const {
    VkDeviceSize bladeBytes = VkDeviceSize(bladeCount) * sizeof(Blade);
    VkDeviceSize size = bladeBytes * 2 + sizeof(BladeDrawIndirect);
    VkBuffer staging;
    VkDeviceMemory memory;
    BufferUtils::CreateBuffer(device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, memory);

    VkCommandBufferAllocateInfo alloc = {};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer command;
    if (vkAllocateCommandBuffers(device->GetVkDevice(), &alloc, &command) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate readback command buffer");
    }
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command, &begin);
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    VkBufferCopy region = { 0, 0, bladeBytes };
    vkCmdCopyBuffer(command, bladesBuffer, staging, 1, &region);
    region.dstOffset = bladeBytes;
    vkCmdCopyBuffer(command, culledBladesBuffer, staging, 1, &region);
    region.dstOffset = bladeBytes * 2;
    region.size = sizeof(BladeDrawIndirect);
    vkCmdCopyBuffer(command, numBladesBuffer, staging, 1, &region);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    vkEndCommandBuffer(command);
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    if (vkQueueSubmit(device->GetQueue(QueueFlags::Graphics), 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit blade readback");
    }
    vkQueueWaitIdle(device->GetQueue(QueueFlags::Graphics));

    void* mapped;
    if (vkMapMemory(device->GetVkDevice(), memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map blade readback");
    }
    std::vector<Blade> input(bladeCount), output(bladeCount);
    BladeDrawIndirect draw;
    memcpy(input.data(), mapped, static_cast<size_t>(bladeBytes));
    memcpy(output.data(), static_cast<char*>(mapped) + bladeBytes, static_cast<size_t>(bladeBytes));
    memcpy(&draw, static_cast<char*>(mapped) + bladeBytes * 2, sizeof(draw));
    vkUnmapMemory(device->GetVkDevice(), memory);
    vkFreeCommandBuffers(device->GetVkDevice(), commandPool, 1, &command);
    vkDestroyBuffer(device->GetVkDevice(), staging, nullptr);
    vkFreeMemory(device->GetVkDevice(), memory, nullptr);

    if (draw.vertexCount > bladeCount || draw.instanceCount != 1 || draw.firstVertex != 0 || draw.firstInstance != 0) {
        throw std::runtime_error("Invalid indirect draw arguments");
    }
    if (verify) {
        std::map<std::array<float, 4>, uint32_t> sourceIndices;
        uint32_t bent = 0;
        float maxLengthError = 0.0f;
        for (uint32_t i = 0; i < bladeCount; ++i) {
            const Blade& blade = input[i];
            const glm::vec4 fields[] = { blade.v0, blade.v1, blade.v2, blade.up };
            for (const auto& field : fields) {
                for (int component = 0; component < 4; ++component) {
                    if (!std::isfinite(field[component])) throw std::runtime_error("Nonfinite blade data");
                }
            }
            glm::vec3 root(blade.v0), guide(blade.v1), tip(blade.v2), up(blade.up);
            float length = (2.0f * glm::length(tip - root) + glm::length(guide - root) + glm::length(tip - guide)) / 3.0f;
            float error = std::abs(length - blade.v1.w) / blade.v1.w;
            maxLengthError = std::max(maxLengthError, error);
            if (error > 0.0001f || glm::dot(tip - root, up) < -0.0001f ||
                glm::length(glm::cross(guide - root, up)) > 0.0001f || blade.v2.w <= 0.0f) {
                throw std::runtime_error("Blade state failed ground, guide, or length validation");
            }
            if (glm::length(glm::cross(tip - root, up)) > 0.001f) ++bent;
            sourceIndices[{ blade.v0.x, blade.v0.y, blade.v0.z, blade.v0.w }] = i;
        }
        std::set<uint32_t> seen;
        for (uint32_t i = 0; i < draw.vertexCount; ++i) {
            const Blade& blade = output[i];
            auto found = sourceIndices.find({ blade.v0.x, blade.v0.y, blade.v0.z, blade.v0.w });
            if (found == sourceIndices.end() || !seen.insert(found->second).second ||
                memcmp(&blade, &input[found->second], sizeof(Blade)) != 0) {
                throw std::runtime_error("Compacted blade is duplicated or differs from simulation state");
            }
        }
        printf("Verified %u blades, %u visible, %u bent, max relative length error %.8f\n",
            bladeCount, draw.vertexCount, bent, maxLengthError);
    }
    if (!dumpPath.empty()) {
        std::ofstream file(dumpPath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(bladeBytes));
        if (!file) throw std::runtime_error("Failed to write blade dump");
    }
    return draw.vertexCount;
}
