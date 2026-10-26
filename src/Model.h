#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

#include "Vertex.h"
#include "Device.h"

struct ModelBufferObject {
    glm::mat4 modelMatrix;
    glm::vec4 material = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f); // w: vertex-colour material
};

class Model {
protected:
    Device* device;

    std::vector<Vertex> vertices;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;

    std::vector<uint32_t> indices;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;

    VkBuffer modelBuffer;
    VkDeviceMemory modelBufferMemory;

    ModelBufferObject modelBufferObject;
    void* mappedModel = nullptr;

    VkImage texture = VK_NULL_HANDLE;
    VkImageView textureView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;

public:
    Model() = delete;
    Model(Device* device, VkCommandPool commandPool, const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices);
    virtual ~Model();

    void SetTexture(VkImage texture);
    // Call only after the previous frame has completed (the renderer serializes frames).
    void SetTransform(const glm::mat4& transform);
    void SetVertexColorMaterial(bool enabled);

    const std::vector<Vertex>& getVertices() const;

    VkBuffer getVertexBuffer() const;

    const std::vector<uint32_t>& getIndices() const;

    VkBuffer getIndexBuffer() const;

    const ModelBufferObject& getModelBufferObject() const;

    VkBuffer GetModelBuffer() const;
    VkImageView GetTextureView() const;
    VkSampler GetTextureSampler() const;
};
