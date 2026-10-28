#pragma once

#include "Device.h"
#include "SwapChain.h"
#include "Scene.h"
#include "Camera.h"
#include <fstream>

class Renderer {
public:
    Renderer() = delete;
    Renderer(Device* device, SwapChain* swapChain, Scene* scene, Camera* camera);
    ~Renderer();

    void CreateCommandPools();

    void CreateRenderPass();

    void CreateCameraDescriptorSetLayout();
    void CreateModelDescriptorSetLayout();
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
    void CreateSkyPipeline();
    void TogglePerformanceCapture();

    void CreateFrameResources();
    void DestroyFrameResources();
    void RecreateFrameResources();

    void RecordCommandBuffers();
    void RecordComputeCommandBuffer();

    void Frame();

private:
    Device* device;
    VkDevice logicalDevice;
    SwapChain* swapChain;
    Scene* scene;
    Camera* camera;

    VkCommandPool graphicsCommandPool;
    VkCommandPool computeCommandPool;

    VkRenderPass renderPass;

    VkDescriptorSetLayout cameraDescriptorSetLayout;
    VkDescriptorSetLayout modelDescriptorSetLayout;
    VkDescriptorSetLayout timeDescriptorSetLayout;
    VkDescriptorSetLayout computeDescriptorSetLayout;
    
    VkDescriptorPool descriptorPool;

    VkDescriptorSet cameraDescriptorSet;
    std::vector<VkDescriptorSet> modelDescriptorSets;
    std::vector<VkDescriptorSet> grassDescriptorSets;
    VkDescriptorSet timeDescriptorSet;
    std::vector<VkDescriptorSet> computeDescriptorSets;

    VkPipelineLayout graphicsPipelineLayout;
    VkPipelineLayout grassPipelineLayout;
    VkPipelineLayout computePipelineLayout;

    VkPipeline graphicsPipeline;
    VkPipeline grassPipeline;
    VkPipeline computePipeline;
    VkPipeline skyPipeline;

    std::vector<VkImageView> imageViews;
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
    std::vector<VkFramebuffer> framebuffers;

    std::vector<VkCommandBuffer> commandBuffers;
    VkCommandBuffer computeCommandBuffer;
    VkSemaphore computeFinishedSemaphore;
    VkFence frameFence;
    VkQueryPool performanceQueryPool;
    double timestampPeriodNanoseconds;
    uint32_t computeTimestampValidBits;
    uint32_t graphicsTimestampValidBits;
    bool timestampQueriesSupported;
    bool performanceCaptureEnabled;
    bool performanceLogInitialized;
    bool hasSubmittedPerformanceFrame;
    uint64_t performanceFrameNumber;
    uint32_t submittedBladeCount;
    uint32_t submittedCullingMask;
    uint32_t recordedComputeBladeCount;
    std::ofstream performanceLog;

    void RecordCompletedPerformanceFrame();
};
