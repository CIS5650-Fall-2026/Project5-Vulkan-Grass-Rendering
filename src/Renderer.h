#pragma once

#include "Device.h"
#include "SwapChain.h"
#include "Scene.h"
#include "Camera.h"

struct GrassSnapshot {
    std::vector<Blade> state;
    std::vector<Blade> output;
    std::vector<uint32_t> visibleIds;
    BladeDrawIndirect draw;
    CameraBufferObject camera;
    SimulationUniforms simulation;
    glm::mat4 model;
};

class Renderer {
public:
    Renderer() = delete;
    Renderer(Device* device, SwapChain* swapChain, Scene* scene, Camera* camera, bool wireframe = false, bool timings = false, bool geometryStats = false);
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

    void CreateFrameResources();
    void DestroyFrameResources();
    void RecreateFrameResources();

    void RecordCommandBuffers();
    void RecordComputeCommandBuffer();

    bool Frame();
    void WaitForFrame();
    GrassSnapshot Readback(const Blades& blades);
    std::array<double, 2> ReadGpuTimings(); // compute and graphics intervals, milliseconds
    std::array<uint64_t, 2> ReadGpuGeometry(); // grass TCS patches and TES invocations (optional)

private:
    Device* device;
    VkDevice logicalDevice;
    SwapChain* swapChain;
    Scene* scene;
    Camera* camera;
    bool wireframe;

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

    std::vector<VkImageView> imageViews;
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
    std::vector<VkFramebuffer> framebuffers;

    std::vector<VkCommandBuffer> commandBuffers;
    VkCommandBuffer computeCommandBuffer;
    CameraBufferObject lastCamera;
    SimulationUniforms lastSimulation;
    VkQueryPool timestampPool = VK_NULL_HANDLE;
    VkQueryPool geometryPool = VK_NULL_HANDLE;
    float timestampPeriod = 0.0f;
    uint32_t timestampBits = 0;
};
