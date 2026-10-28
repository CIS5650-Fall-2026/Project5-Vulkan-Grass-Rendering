#include <fstream>
#include "Renderer.h"
#include "Instance.h"
#include "ShaderModule.h"
#include "Vertex.h"
#include "Blades.h"
#include "Camera.h"
#include "Image.h"
#include <iostream>

static constexpr unsigned int WORKGROUP_SIZE = 32;

Renderer::Renderer(Device *device, SwapChain *swapChain, Scene *scene, Camera *camera)
    : device(device),
      logicalDevice(device->GetVkDevice()),
      swapChain(swapChain),
      scene(scene),
      camera(camera),
      computeCommandBuffer(VK_NULL_HANDLE),
      performanceQueryPool(VK_NULL_HANDLE),
      timestampPeriodNanoseconds(0.0),
      computeTimestampValidBits(0),
      graphicsTimestampValidBits(0),
      timestampQueriesSupported(false),
      performanceCaptureEnabled(false),
      performanceLogInitialized(false),
      hasSubmittedPerformanceFrame(false),
      performanceFrameNumber(0),
      submittedBladeCount(0),
      submittedCullingMask(0),
      recordedComputeBladeCount(0)
{

    CreateCommandPools();
    CreateRenderPass();
    CreateCameraDescriptorSetLayout();
    CreateModelDescriptorSetLayout();
    CreateTimeDescriptorSetLayout();
    CreateComputeDescriptorSetLayout();
    CreateDescriptorPool();
    CreateCameraDescriptorSet();
    CreateModelDescriptorSets();
    CreateGrassDescriptorSets();
    CreateTimeDescriptorSet();
    CreateComputeDescriptorSets();
    CreateFrameResources();
    CreateGraphicsPipeline();
    CreateGrassPipeline();
    CreateComputePipeline();
    CreateSkyPipeline();

    VkPhysicalDevice physicalDevice = device->GetInstance()->GetPhysicalDevice();
    VkPhysicalDeviceProperties physicalDeviceProperties = {};
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
    timestampPeriodNanoseconds = physicalDeviceProperties.limits.timestampPeriod;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    const QueueFamilyIndices& queueFamilyIndices = device->GetInstance()->GetQueueFamilyIndices();
    computeTimestampValidBits = queueFamilies[queueFamilyIndices[QueueFlags::Compute]].timestampValidBits;
    graphicsTimestampValidBits = queueFamilies[queueFamilyIndices[QueueFlags::Graphics]].timestampValidBits;
    timestampQueriesSupported = computeTimestampValidBits != 0 && graphicsTimestampValidBits != 0;
    if (timestampQueriesSupported)
    {
        VkQueryPoolCreateInfo queryPoolInfo = {};
        queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = 4;
        if (vkCreateQueryPool(logicalDevice, &queryPoolInfo, nullptr, &performanceQueryPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create performance timestamp query pool");
        }
    }

    RecordCommandBuffers();
    RecordComputeCommandBuffer();

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &computeFinishedSemaphore) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create compute completion semaphore");
    }

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(logicalDevice, &fenceInfo, nullptr, &frameFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create frame fence");
    }
}

void Renderer::CreateCommandPools()
{
    VkCommandPoolCreateInfo graphicsPoolInfo = {};
    graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    graphicsPoolInfo.queueFamilyIndex = device->GetInstance()->GetQueueFamilyIndices()[QueueFlags::Graphics];
    graphicsPoolInfo.flags = 0;

    if (vkCreateCommandPool(logicalDevice, &graphicsPoolInfo, nullptr, &graphicsCommandPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool");
    }

    VkCommandPoolCreateInfo computePoolInfo = {};
    computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    computePoolInfo.queueFamilyIndex = device->GetInstance()->GetQueueFamilyIndices()[QueueFlags::Compute];
    computePoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(logicalDevice, &computePoolInfo, nullptr, &computeCommandPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool");
    }
}

void Renderer::CreateRenderPass()
{
    // Color buffer attachment represented by one of the images from the swap chain
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapChain->GetVkImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Create a color attachment reference to be used with subpass
    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Depth buffer attachment
    VkFormat depthFormat = device->GetInstance()->GetSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Create a depth attachment reference
    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Create subpass description
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    // Specify subpass dependency
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Create render pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create render pass");
    }
}

void Renderer::CreateCameraDescriptorSetLayout()
{
    // Describe the binding of the descriptor set layout
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {uboLayoutBinding};

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &cameraDescriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void Renderer::CreateModelDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {uboLayoutBinding, samplerLayoutBinding};

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &modelDescriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void Renderer::CreateTimeDescriptorSetLayout()
{
    // Describe the binding of the descriptor set layout
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {uboLayoutBinding};

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &timeDescriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void Renderer::CreateComputeDescriptorSetLayout()
{
    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &computeDescriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create compute descriptor set layout");
    }
}

void Renderer::CreateDescriptorPool()
{
    // Describe which descriptor types that the descriptor sets will contain
    std::vector<VkDescriptorPoolSize> poolSizes = {
        // Camera
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},

        // Models + Blades
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(scene->GetModels().size() + scene->GetBlades().size())},

        // Models + Blades
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(scene->GetModels().size() + scene->GetBlades().size())},

        // Time (compute)
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},

        // Compute storage buffers (input, culled, indirect count)
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(scene->GetBlades().size() * 3)},
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(2 + scene->GetModels().size() + scene->GetBlades().size() * 2);

    if (vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

void Renderer::CreateCameraDescriptorSet()
{
    // Describe the desciptor set
    VkDescriptorSetLayout layouts[] = {cameraDescriptorSetLayout};
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layouts;

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &cameraDescriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    // Configure the descriptors to refer to buffers
    VkDescriptorBufferInfo cameraBufferInfo = {};
    cameraBufferInfo.buffer = camera->GetBuffer();
    cameraBufferInfo.offset = 0;
    cameraBufferInfo.range = sizeof(CameraBufferObject);

    std::array<VkWriteDescriptorSet, 1> descriptorWrites = {};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = cameraDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &cameraBufferInfo;
    descriptorWrites[0].pImageInfo = nullptr;
    descriptorWrites[0].pTexelBufferView = nullptr;

    // Update descriptor sets
    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Renderer::CreateModelDescriptorSets()
{
    modelDescriptorSets.resize(scene->GetModels().size());

    std::vector<VkDescriptorSetLayout> layouts(modelDescriptorSets.size(), modelDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(modelDescriptorSets.size());
    allocInfo.pSetLayouts = layouts.data();

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, modelDescriptorSets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites(2 * modelDescriptorSets.size());
    std::vector<VkDescriptorBufferInfo> modelBufferInfos(modelDescriptorSets.size());
    std::vector<VkDescriptorImageInfo> imageInfos(modelDescriptorSets.size());

    for (uint32_t i = 0; i < scene->GetModels().size(); ++i)
    {
        modelBufferInfos[i].buffer = scene->GetModels()[i]->GetModelBuffer();
        modelBufferInfos[i].offset = 0;
        modelBufferInfos[i].range = sizeof(ModelBufferObject);

        // Bind image and sampler resources to the descriptor
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[i].imageView = scene->GetModels()[i]->GetTextureView();
        imageInfos[i].sampler = scene->GetModels()[i]->GetTextureSampler();

        descriptorWrites[2 * i + 0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2 * i + 0].dstSet = modelDescriptorSets[i];
        descriptorWrites[2 * i + 0].dstBinding = 0;
        descriptorWrites[2 * i + 0].dstArrayElement = 0;
        descriptorWrites[2 * i + 0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[2 * i + 0].descriptorCount = 1;
        descriptorWrites[2 * i + 0].pBufferInfo = &modelBufferInfos[i];
        descriptorWrites[2 * i + 0].pImageInfo = nullptr;
        descriptorWrites[2 * i + 0].pTexelBufferView = nullptr;

        descriptorWrites[2 * i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2 * i + 1].dstSet = modelDescriptorSets[i];
        descriptorWrites[2 * i + 1].dstBinding = 1;
        descriptorWrites[2 * i + 1].dstArrayElement = 0;
        descriptorWrites[2 * i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2 * i + 1].descriptorCount = 1;
        descriptorWrites[2 * i + 1].pImageInfo = &imageInfos[i];
    }

    // Update descriptor sets
    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Renderer::CreateGrassDescriptorSets()
{
    grassDescriptorSets.resize(scene->GetBlades().size());

    std::vector<VkDescriptorSetLayout> layouts(grassDescriptorSets.size(), modelDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(grassDescriptorSets.size());
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, grassDescriptorSets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate grass descriptor sets");
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites(scene->GetBlades().size());
    std::vector<VkDescriptorBufferInfo> modelBufferInfos(scene->GetBlades().size());
    for (uint32_t i = 0; i < scene->GetBlades().size(); ++i)
    {
        modelBufferInfos[i].buffer = scene->GetBlades()[i]->GetModelBuffer();
        modelBufferInfos[i].offset = 0;
        modelBufferInfos[i].range = sizeof(ModelBufferObject);

        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = grassDescriptorSets[i];
        descriptorWrites[i].dstBinding = 0;
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pBufferInfo = &modelBufferInfos[i];
        descriptorWrites[i].pImageInfo = nullptr;
        descriptorWrites[i].pTexelBufferView = nullptr;
    }

    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Renderer::CreateTimeDescriptorSet()
{
    // Describe the desciptor set
    VkDescriptorSetLayout layouts[] = {timeDescriptorSetLayout};
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layouts;

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &timeDescriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    // Configure the descriptors to refer to buffers
    VkDescriptorBufferInfo timeBufferInfo = {};
    timeBufferInfo.buffer = scene->GetTimeBuffer();
    timeBufferInfo.offset = 0;
    timeBufferInfo.range = sizeof(Time);

    std::array<VkWriteDescriptorSet, 1> descriptorWrites = {};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = timeDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &timeBufferInfo;
    descriptorWrites[0].pImageInfo = nullptr;
    descriptorWrites[0].pTexelBufferView = nullptr;

    // Update descriptor sets
    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Renderer::CreateComputeDescriptorSets()
{
    computeDescriptorSets.resize(scene->GetBlades().size());

    std::vector<VkDescriptorSetLayout> layouts(computeDescriptorSets.size(), computeDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(computeDescriptorSets.size());
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, computeDescriptorSets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate compute descriptor sets");
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites(scene->GetBlades().size() * 3);
    for (uint32_t i = 0; i < scene->GetBlades().size(); ++i)
    {
        VkDescriptorBufferInfo inputBufferInfo = {};
        inputBufferInfo.buffer = scene->GetBlades()[i]->GetBladesBuffer();
        inputBufferInfo.offset = 0;
        inputBufferInfo.range = NUM_BLADES * sizeof(Blade);

        VkDescriptorBufferInfo culledBufferInfo = {};
        culledBufferInfo.buffer = scene->GetBlades()[i]->GetCulledBladesBuffer();
        culledBufferInfo.offset = 0;
        culledBufferInfo.range = NUM_BLADES * sizeof(Blade);

        VkDescriptorBufferInfo numBladesBufferInfo = {};
        numBladesBufferInfo.buffer = scene->GetBlades()[i]->GetNumBladesBuffer();
        numBladesBufferInfo.offset = 0;
        numBladesBufferInfo.range = sizeof(BladeDrawIndirect);

        descriptorWrites[i * 3 + 0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i * 3 + 0].dstSet = computeDescriptorSets[i];
        descriptorWrites[i * 3 + 0].dstBinding = 0;
        descriptorWrites[i * 3 + 0].dstArrayElement = 0;
        descriptorWrites[i * 3 + 0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[i * 3 + 0].descriptorCount = 1;
        descriptorWrites[i * 3 + 0].pBufferInfo = &inputBufferInfo;

        descriptorWrites[i * 3 + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i * 3 + 1].dstSet = computeDescriptorSets[i];
        descriptorWrites[i * 3 + 1].dstBinding = 1;
        descriptorWrites[i * 3 + 1].dstArrayElement = 0;
        descriptorWrites[i * 3 + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[i * 3 + 1].descriptorCount = 1;
        descriptorWrites[i * 3 + 1].pBufferInfo = &culledBufferInfo;

        descriptorWrites[i * 3 + 2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i * 3 + 2].dstSet = computeDescriptorSets[i];
        descriptorWrites[i * 3 + 2].dstBinding = 2;
        descriptorWrites[i * 3 + 2].dstArrayElement = 0;
        descriptorWrites[i * 3 + 2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[i * 3 + 2].descriptorCount = 1;
        descriptorWrites[i * 3 + 2].pBufferInfo = &numBladesBufferInfo;
    }

    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

namespace
{
    std::string ResolveShaderPath(const std::string &relativePath)
    {
        const std::string candidates[] = {
            relativePath,
            std::string("../") + relativePath,
            std::string("../build/src/") + relativePath,
            std::string("build/src/") + relativePath,
            std::string("src/") + relativePath,
            std::string("../src/") + relativePath,
        };

        for (const std::string &candidate : candidates)
        {
            std::ifstream test(candidate.c_str());
            if (test.good())
            {
                return candidate;
            }
        }

        return relativePath;
    }
}

void Renderer::CreateGraphicsPipeline()
{
    VkShaderModule vertShaderModule = ShaderModule::Create(ResolveShaderPath("shaders/graphics.vert.spv"), logicalDevice);
    VkShaderModule fragShaderModule = ShaderModule::Create(ResolveShaderPath("shaders/graphics.frag.spv"), logicalDevice);

    // Assign each shader module to the appropriate stage in the pipeline
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // --- Set up fixed-function stages ---

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewports and Scissors (rectangles that define in which regions pixels are stored)
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChain->GetVkExtent().width);
    viewport.height = static_cast<float>(swapChain->GetVkExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = swapChain->GetVkExtent();

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    // Multisampling (turned off here)
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Depth testing
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending (turned off here, but showing options for learning)
    // --> Configuration per attached framebuffer
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    // --> Global color blending settings
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {cameraDescriptorSetLayout, modelDescriptorSetLayout};

    // Pipeline layout: used to specify uniform values
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &graphicsPipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    // --- Create graphics pipeline ---
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = graphicsPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
}

void Renderer::CreateSkyPipeline()
{
    VkShaderModule vertShaderModule = ShaderModule::Create(ResolveShaderPath("shaders/sky.vert.spv"), logicalDevice);
    VkShaderModule fragShaderModule = ShaderModule::Create(ResolveShaderPath("shaders/sky.frag.spv"), logicalDevice);

    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShaderModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShaderModule;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {};
    viewport.width = static_cast<float>(swapChain->GetVkExtent().width);
    viewport.height = static_cast<float>(swapChain->GetVkExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.extent = swapChain->GetVkExtent();

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorAttachment = {};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = graphicsPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
        vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
        throw std::runtime_error("Failed to create sky pipeline");
    }

    vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
}

void Renderer::CreateGrassPipeline()
{
    // --- Set up programmable shaders ---
    VkShaderModule vertShaderModule = ShaderModule::Create(ResolveShaderPath("shaders/grass.vert.spv"), logicalDevice);
    VkShaderModule tescShaderModule = ShaderModule::Create(ResolveShaderPath("shaders/grass.tesc.spv"), logicalDevice);
    VkShaderModule teseShaderModule = ShaderModule::Create(ResolveShaderPath("shaders/grass.tese.spv"), logicalDevice);
    VkShaderModule fragShaderModule = ShaderModule::Create(ResolveShaderPath("shaders/grass.frag.spv"), logicalDevice);

    // Assign each shader module to the appropriate stage in the pipeline
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo tescShaderStageInfo = {};
    tescShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tescShaderStageInfo.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    tescShaderStageInfo.module = tescShaderModule;
    tescShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo teseShaderStageInfo = {};
    teseShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    teseShaderStageInfo.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    teseShaderStageInfo.module = teseShaderModule;
    teseShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, tescShaderStageInfo, teseShaderStageInfo, fragShaderStageInfo};

    // --- Set up fixed-function stages ---

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescription = Blade::getBindingDescription();
    auto attributeDescriptions = Blade::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewports and Scissors (rectangles that define in which regions pixels are stored)
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChain->GetVkExtent().width);
    viewport.height = static_cast<float>(swapChain->GetVkExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = swapChain->GetVkExtent();

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    // Multisampling (turned off here)
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Depth testing
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending (turned off here, but showing options for learning)
    // --> Configuration per attached framebuffer
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    // --> Global color blending settings
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {cameraDescriptorSetLayout, modelDescriptorSetLayout};

    // Pipeline layout: used to specify uniform values
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &grassPipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    // Tessellation state
    VkPipelineTessellationStateCreateInfo tessellationInfo = {};
    tessellationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellationInfo.pNext = NULL;
    tessellationInfo.flags = 0;
    tessellationInfo.patchControlPoints = 1;

    // --- Create graphics pipeline ---
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 4;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pTessellationState = &tessellationInfo;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = grassPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &grassPipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    // No need for the shader modules anymore
    vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, tescShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, teseShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
}

void Renderer::CreateComputePipeline()
{
    // Set up programmable shaders
    VkShaderModule computeShaderModule = ShaderModule::Create(ResolveShaderPath("shaders/compute.comp.spv"), logicalDevice);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo = {};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {cameraDescriptorSetLayout, timeDescriptorSetLayout, computeDescriptorSetLayout};

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = computePipelineLayout;
    pipelineInfo.pNext = nullptr;
    pipelineInfo.flags = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateComputePipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create compute pipeline");
    }

    // No need for shader modules anymore
    vkDestroyShaderModule(logicalDevice, computeShaderModule, nullptr);
}

void Renderer::CreateFrameResources()
{
    imageViews.resize(swapChain->GetCount());

    for (uint32_t i = 0; i < swapChain->GetCount(); i++)
    {
        // --- Create an image view for each swap chain image ---
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChain->GetVkImage(i);

        // Specify how the image data should be interpreted
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChain->GetVkImageFormat();

        // Specify color channel mappings (can be used for swizzling)
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // Describe the image's purpose and which part of the image should be accessed
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        // Create the image view
        if (vkCreateImageView(logicalDevice, &createInfo, nullptr, &imageViews[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create image views");
        }
    }

    VkFormat depthFormat = device->GetInstance()->GetSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    // CREATE DEPTH IMAGE
    Image::Create(device,
                  swapChain->GetVkExtent().width,
                  swapChain->GetVkExtent().height,
                  depthFormat,
                  VK_IMAGE_TILING_OPTIMAL,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  depthImage,
                  depthImageMemory);

    depthImageView = Image::CreateView(device, depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Transition the image for use as depth-stencil
    Image::TransitionLayout(device, graphicsCommandPool, depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    // CREATE FRAMEBUFFERS
    framebuffers.resize(swapChain->GetCount());
    for (size_t i = 0; i < swapChain->GetCount(); i++)
    {
        std::vector<VkImageView> attachments = {
            imageViews[i],
            depthImageView};

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChain->GetVkExtent().width;
        framebufferInfo.height = swapChain->GetVkExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }
}

void Renderer::DestroyFrameResources()
{
    for (size_t i = 0; i < imageViews.size(); i++)
    {
        vkDestroyImageView(logicalDevice, imageViews[i], nullptr);
    }

    vkDestroyImageView(logicalDevice, depthImageView, nullptr);
    vkFreeMemory(logicalDevice, depthImageMemory, nullptr);
    vkDestroyImage(logicalDevice, depthImage, nullptr);

    for (size_t i = 0; i < framebuffers.size(); i++)
    {
        vkDestroyFramebuffer(logicalDevice, framebuffers[i], nullptr);
    }
}

void Renderer::RecreateFrameResources()
{
    if (vkDeviceWaitIdle(logicalDevice) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to wait for device before recreating frame resources");
    }

    vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, grassPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, skyPipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice, graphicsPipelineLayout, nullptr);
    vkDestroyPipelineLayout(logicalDevice, grassPipelineLayout, nullptr);
    vkFreeCommandBuffers(logicalDevice, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

    DestroyFrameResources();
    CreateFrameResources();
    CreateGraphicsPipeline();
    CreateGrassPipeline();
    CreateSkyPipeline();
    RecordCommandBuffers();
}

void Renderer::RecordComputeCommandBuffer()
{
    if (computeCommandBuffer == VK_NULL_HANDLE)
    {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = computeCommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &computeCommandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffers");
        }
    }
    else if (vkResetCommandBuffer(computeCommandBuffer, 0) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to reset compute command buffer");
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    // ~ Start recording ~
    if (vkBeginCommandBuffer(computeCommandBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording compute command buffer");
    }

    if (timestampQueriesSupported)
    {
        vkCmdResetQueryPool(computeCommandBuffer, performanceQueryPool, 0, 4);
        vkCmdWriteTimestamp(computeCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, performanceQueryPool, 0);
    }

    // Bind to the compute pipeline
    vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);

    // Bind camera descriptor set
    vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &cameraDescriptorSet, 0, nullptr);

    // Bind descriptor set for time uniforms
    vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 1, 1, &timeDescriptorSet, 0, nullptr);

    std::vector<VkBufferMemoryBarrier> resetBarriers(scene->GetBlades().size());
    for (uint32_t i = 0; i < scene->GetBlades().size(); ++i)
    {
        VkBuffer numBladesBuffer = scene->GetBlades()[i]->GetNumBladesBuffer();
        vkCmdFillBuffer(computeCommandBuffer, numBladesBuffer, 0, sizeof(uint32_t), 0);

        resetBarriers[i] = {};
        resetBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        resetBarriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        resetBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        resetBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resetBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resetBarriers[i].buffer = numBladesBuffer;
        resetBarriers[i].offset = 0;
        resetBarriers[i].size = sizeof(BladeDrawIndirect);
    }

    if (!resetBarriers.empty())
    {
        vkCmdPipelineBarrier(
            computeCommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            static_cast<uint32_t>(resetBarriers.size()),
            resetBarriers.data(),
            0,
            nullptr);
    }

    for (uint32_t i = 0; i < scene->GetBlades().size(); ++i)
    {
        vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 2, 1, &computeDescriptorSets[i], 0, nullptr);
        const uint32_t bladeCount = scene->GetActiveBladeCount();
        vkCmdDispatch(computeCommandBuffer, (bladeCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, 1, 1);
    }

    VkMemoryBarrier simulationBarrier = {};
    simulationBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    simulationBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    simulationBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(
        computeCommandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        1,
        &simulationBarrier,
        0,
        nullptr,
        0,
        nullptr);

    if (timestampQueriesSupported)
    {
        vkCmdWriteTimestamp(computeCommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, performanceQueryPool, 1);
    }

    // ~ End recording ~
    if (vkEndCommandBuffer(computeCommandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record compute command buffer");
    }
    recordedComputeBladeCount = scene->GetActiveBladeCount();
}

void Renderer::RecordCommandBuffers()
{
    commandBuffers.resize(swapChain->GetCount());

    // Specify the command pool and number of buffers to allocate
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = graphicsCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    // Start command buffer recording
    for (size_t i = 0; i < commandBuffers.size(); i++)
    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr;

        // ~ Start recording ~
        if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to begin recording command buffer");
        }

        if (timestampQueriesSupported)
        {
            vkCmdWriteTimestamp(commandBuffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, performanceQueryPool, 2);
        }

        // Begin the render pass
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[i];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChain->GetVkExtent();

        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        // Bind the camera descriptor set. This is set 0 in all pipelines so it will be inherited
        vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &cameraDescriptorSet, 0, nullptr);

        vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
        vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &cameraDescriptorSet, 0, nullptr);
        vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

        // Bind the graphics pipeline
        vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        for (uint32_t j = 0; j < scene->GetModels().size(); ++j)
        {
            // Bind the vertex and index buffers
            VkBuffer vertexBuffers[] = {scene->GetModels()[j]->getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

            vkCmdBindIndexBuffer(commandBuffers[i], scene->GetModels()[j]->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // Bind the descriptor set for each model
            vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 1, 1, &modelDescriptorSets[j], 0, nullptr);

            // Draw
            std::vector<uint32_t> indices = scene->GetModels()[j]->getIndices();
            vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
        }

        // Bind the grass pipeline
        vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, grassPipeline);

        for (uint32_t j = 0; j < scene->GetBlades().size(); ++j)
        {
            VkBuffer vertexBuffers[] = {scene->GetBlades()[j]->GetCulledBladesBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

            vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, grassPipelineLayout, 1, 1, &grassDescriptorSets[j], 0, nullptr);

            vkCmdDrawIndirect(commandBuffers[i], scene->GetBlades()[j]->GetNumBladesBuffer(), 0, 1, sizeof(BladeDrawIndirect));
        }

        // End render pass
        vkCmdEndRenderPass(commandBuffers[i]);

        if (timestampQueriesSupported)
        {
            vkCmdWriteTimestamp(commandBuffers[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, performanceQueryPool, 3);
        }

        // ~ End recording ~
        if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to record command buffer");
        }
    }
}

void Renderer::Frame()
{
    if (vkWaitForFences(logicalDevice, 1, &frameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to wait for previous frame");
    }

    if (hasSubmittedPerformanceFrame)
    {
        RecordCompletedPerformanceFrame();
        hasSubmittedPerformanceFrame = false;
    }

    if (recordedComputeBladeCount != scene->GetActiveBladeCount())
    {
        RecordComputeCommandBuffer();
    }

    if (!swapChain->Acquire())
    {
        RecreateFrameResources();
        return;
    }

    VkSubmitInfo computeSubmitInfo = {};
    computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    computeSubmitInfo.commandBufferCount = 1;
    computeSubmitInfo.pCommandBuffers = &computeCommandBuffer;
    computeSubmitInfo.signalSemaphoreCount = 1;
    computeSubmitInfo.pSignalSemaphores = &computeFinishedSemaphore;

    if (vkQueueSubmit(device->GetQueue(QueueFlags::Compute), 1, &computeSubmitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit compute command buffer");
    }

    // Submit the command buffer
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {swapChain->GetImageAvailableVkSemaphore(), computeFinishedSemaphore};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
    submitInfo.waitSemaphoreCount = 2;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[swapChain->GetIndex()];

    VkSemaphore signalSemaphores[] = {swapChain->GetRenderFinishedVkSemaphore()};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkResetFences(logicalDevice, 1, &frameFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to reset frame fence");
    }
    if (vkQueueSubmit(device->GetQueue(QueueFlags::Graphics), 1, &submitInfo, frameFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    submittedBladeCount = scene->GetActiveBladeCount();
    submittedCullingMask = scene->GetCullingMask();
    hasSubmittedPerformanceFrame = true;

    if (!swapChain->Present())
    {
        RecreateFrameResources();
    }
}

void Renderer::TogglePerformanceCapture()
{
    if (!timestampQueriesSupported)
    {
        std::cerr << "GPU timestamp queries are unavailable on the selected queues.\n";
        return;
    }

    if (performanceCaptureEnabled)
    {
        performanceCaptureEnabled = false;
        performanceLog.flush();
        performanceLog.close();
        std::cout << "GPU timing capture stopped.\n";
        return;
    }

    const std::ios_base::openmode mode = std::ios::out |
        (performanceLogInitialized ? std::ios::app : std::ios::trunc);
    performanceLog.open("performance.csv", mode);
    if (!performanceLog.is_open())
    {
        throw std::runtime_error("Failed to open performance.csv for GPU timing capture");
    }
    if (!performanceLogInitialized)
    {
        performanceLog << "frame,blade_count,orientation_culling,frustum_culling,distance_culling,compute_ms,graphics_ms\n";
        performanceLogInitialized = true;
    }
    performanceCaptureEnabled = true;
    std::cout << "GPU timing capture started: performance.csv\n";
}

void Renderer::RecordCompletedPerformanceFrame()
{
    if (!timestampQueriesSupported || !performanceCaptureEnabled)
    {
        return;
    }

    uint64_t timestamps[4] = {};
    const VkResult result = vkGetQueryPoolResults(
        logicalDevice,
        performanceQueryPool,
        0,
        4,
        sizeof(timestamps),
        timestamps,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to read completed GPU performance timestamps");
    }

    auto elapsedTicks = [](uint64_t start, uint64_t end, uint32_t validBits) {
        const uint64_t difference = end - start;
        if (validBits >= 64)
        {
            return difference;
        }
        return difference & ((uint64_t(1) << validBits) - 1);
    };
    const double computeMilliseconds =
        elapsedTicks(timestamps[0], timestamps[1], computeTimestampValidBits) *
        timestampPeriodNanoseconds / 1000000.0;
    const double graphicsMilliseconds =
        elapsedTicks(timestamps[2], timestamps[3], graphicsTimestampValidBits) *
        timestampPeriodNanoseconds / 1000000.0;

    performanceLog << performanceFrameNumber++ << ','
                   << submittedBladeCount << ','
                   << ((submittedCullingMask & OrientationCulling) != 0) << ','
                   << ((submittedCullingMask & FrustumCulling) != 0) << ','
                   << ((submittedCullingMask & DistanceCulling) != 0) << ','
                   << computeMilliseconds << ','
                   << graphicsMilliseconds << '\n';
    if (!performanceLog)
    {
        throw std::runtime_error("Failed while writing GPU performance data to performance.csv");
    }
}

Renderer::~Renderer()
{
    vkDeviceWaitIdle(logicalDevice);
    if (hasSubmittedPerformanceFrame)
    {
        RecordCompletedPerformanceFrame();
    }
    if (performanceLog.is_open())
    {
        performanceLog.close();
    }

    // TODO: destroy any resources you created
    if (performanceQueryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(logicalDevice, performanceQueryPool, nullptr);
    }
    vkDestroyFence(logicalDevice, frameFence, nullptr);
    vkDestroySemaphore(logicalDevice, computeFinishedSemaphore, nullptr);

    vkFreeCommandBuffers(logicalDevice, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
    vkFreeCommandBuffers(logicalDevice, computeCommandPool, 1, &computeCommandBuffer);

    vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, grassPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, computePipeline, nullptr);
    vkDestroyPipeline(logicalDevice, skyPipeline, nullptr);

    vkDestroyPipelineLayout(logicalDevice, graphicsPipelineLayout, nullptr);
    vkDestroyPipelineLayout(logicalDevice, grassPipelineLayout, nullptr);
    vkDestroyPipelineLayout(logicalDevice, computePipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(logicalDevice, cameraDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, modelDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, timeDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, computeDescriptorSetLayout, nullptr);

    vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);

    vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
    DestroyFrameResources();
    vkDestroyCommandPool(logicalDevice, computeCommandPool, nullptr);
    vkDestroyCommandPool(logicalDevice, graphicsCommandPool, nullptr);
}
