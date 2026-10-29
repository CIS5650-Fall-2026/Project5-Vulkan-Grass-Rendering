#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

#include "Vertex.h"
#include "Device.h"

struct ModelBufferObject {
    glm::mat4 modelMatrix;
    // alpha > 0 selects this color instead of the model texture in graphics.frag.
    glm::vec4 solidColor;
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
    void* mappedModelData = nullptr;

    ModelBufferObject modelBufferObject;

    VkImage texture = VK_NULL_HANDLE;
    VkImageView textureView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;

public:
    Model() = delete;
    Model(Device* device, VkCommandPool commandPool, const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices);
    virtual ~Model();

    void SetTexture(VkImage texture);
    void SetModelMatrix(const glm::mat4& modelMatrix);
    void SetSolidColor(const glm::vec4& color);

    const std::vector<Vertex>& getVertices() const;

    VkBuffer getVertexBuffer() const;

    const std::vector<uint32_t>& getIndices() const;

    VkBuffer getIndexBuffer() const;

    const ModelBufferObject& getModelBufferObject() const;

    VkBuffer GetModelBuffer() const;
    VkImageView GetTextureView() const;
    VkSampler GetTextureSampler() const;
};
