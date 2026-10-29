#include "Colliders.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

#include "BufferUtils.h"

Colliders::Colliders(Device* device, uint32_t colliderCount)
    : device(device), colliders(colliderCount) {
    if (colliderCount == 0) {
        throw std::runtime_error("Colliders requires at least one collider");
    }

    BufferUtils::CreateBuffer(
        device,
        sizeof(Collider) * colliders.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        collidersBuffer,
        collidersBufferMemory
    );

    vkMapMemory(device->GetVkDevice(), collidersBufferMemory, 0,
        sizeof(Collider) * colliders.size(), 0, &mappedData);

    Update(0.0f);
}

void Colliders::Update(float totalTime) {
    if (!colliders.empty()) {
        // Its instantaneous angular speed changes smoothly over time.
        const float angle = 0.17f * totalTime
            + (0.10f / 0.45f) * (1.0f - std::cos(0.45f * totalTime));
        constexpr float radius = 1.25f;
        colliders[0].centerRadius = glm::vec4(
            4.0f * std::sin(angle),
            radius,
            4.0f * std::cos(angle),
            radius
        );
    }

    if (colliders.size() > 1) {
        // Smaller and quicker, following a figure-eight instead of a circle.
        const float angle = 0.42f * totalTime + 0.18f * std::sin(0.55f * totalTime);
        constexpr float radius = 1.75f;
        colliders[1].centerRadius = glm::vec4(
            3.0f * std::sin(angle),
            radius,
            2.0f * std::sin(2.0f * angle),
            radius
        );
    }

    std::memcpy(mappedData, colliders.data(), sizeof(Collider) * colliders.size());
}

uint32_t Colliders::GetNumColliders() const {
    return static_cast<uint32_t>(colliders.size());
}

VkBuffer Colliders::GetCollidersBuffer() const {
    return collidersBuffer;
}

const Collider& Colliders::GetCollider(uint32_t index) const {
    return colliders.at(index);
}

Colliders::~Colliders() {
    if (mappedData != nullptr) {
        vkUnmapMemory(device->GetVkDevice(), collidersBufferMemory);
    }
    vkDestroyBuffer(device->GetVkDevice(), collidersBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), collidersBufferMemory, nullptr);
}
