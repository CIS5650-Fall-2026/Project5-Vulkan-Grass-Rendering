#pragma once

#include "Device.h"
#include "SwapChain.h"
#include "Scene.h"
#include "Camera.h"

class Renderer {
public:
    Renderer() = delete;
    Renderer(Device* device, SwapChain* swapChain, Scene* scene, Camera* camera, bool benchmark = false, bool offscreen = false, bool lod = true);
    ~Renderer();

    void CreateCommandPools();

    void CreateRenderPass();

    void CreateCameraDescriptorSetLayout();
    void CreateModelDescriptorSetLayout();
    void CreateGrassDescriptorSetLayout();
    void CreateTimeDescriptorSetLayout();
    void CreateComputeDescriptorSetLayout();

    void CreateDescriptorPool();

    void CreateCameraDescriptorSet();
    void CreateModelDescriptorSets();
    void CreateGrassDescriptorSets();
    void CreateTimeDescriptorSet();
    void CreateComputeDescriptorSets();

    void CreateGraphicsPipeline();
    void CreateGrassPipeline();
    void CreateComputePipeline();

    void CreateFrameResources();
    void DestroyFrameResources();
    void RecreateFrameResources();

    void RecordCommandBuffers();
    void RecordComputeCommandBuffer();

    bool Frame();
    double GetComputeMilliseconds() const { return computeMilliseconds; }
    double GetGraphicsMilliseconds() const { return graphicsMilliseconds; }
    uint32_t ReadBackBlades(bool verify, const std::string& dumpPath = "") const;
    void SaveFrame(const std::string& path) const;

private:
    Device* device;
    VkDevice logicalDevice;
    SwapChain* swapChain;
    Scene* scene;
    Camera* camera;
    bool offscreen;
    bool lod;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory = VK_NULL_HANDLE;

    VkCommandPool graphicsCommandPool;
    VkCommandPool computeCommandPool;

    VkRenderPass renderPass;

    VkDescriptorSetLayout cameraDescriptorSetLayout;
    VkDescriptorSetLayout modelDescriptorSetLayout;
    VkDescriptorSetLayout timeDescriptorSetLayout;
    VkDescriptorSetLayout computeDescriptorSetLayout;
    VkDescriptorSetLayout grassDescriptorSetLayout;
    
    VkDescriptorPool descriptorPool;

    VkDescriptorSet cameraDescriptorSet;
    std::vector<VkDescriptorSet> modelDescriptorSets;
    VkDescriptorSet timeDescriptorSet;
    std::vector<VkDescriptorSet> grassDescriptorSets;
    std::vector<VkDescriptorSet> computeDescriptorSets;
    VkSemaphore computeFinishedSemaphore;
    VkQueryPool computeQueries = VK_NULL_HANDLE;
    VkQueryPool graphicsQueries = VK_NULL_HANDLE;
    uint32_t computeTimestampBits = 0;
    uint32_t graphicsTimestampBits = 0;
    float timestampPeriod = 0.0f;
    double computeMilliseconds = 0.0;
    double graphicsMilliseconds = 0.0;

    VkPipelineLayout graphicsPipelineLayout;
    VkPipelineLayout skyPipelineLayout;
    VkPipelineLayout grassPipelineLayout;
    VkPipelineLayout computePipelineLayout;

    VkPipeline graphicsPipeline;
    VkPipeline skyPipeline;
    VkPipeline grassPipeline;
    VkPipeline computePipeline;

    std::vector<VkImageView> imageViews;
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
    std::vector<VkFramebuffer> framebuffers;

    std::vector<VkCommandBuffer> commandBuffers;
    VkCommandBuffer computeCommandBuffer;
};
