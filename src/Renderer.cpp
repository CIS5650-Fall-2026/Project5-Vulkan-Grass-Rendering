#include "Renderer.h"
#include "Instance.h"
#include "ShaderModule.h"
#include "Vertex.h"
#include "Blades.h"
#include "Camera.h"
#include "Image.h"

static constexpr unsigned int WORKGROUP_SIZE = 32;

Renderer::Renderer(Device* device, SwapChain* swapChain, Scene* scene, Camera* camera)
  : device(device),
    logicalDevice(device->GetVkDevice()),
    swapChain(swapChain),
    scene(scene),
    camera(camera) {

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

    // Reuse one set of blade buffers
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &graphicsFinishedSemaphore);
    vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &computeFinishedSemaphore);
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(logicalDevice, &fenceInfo, nullptr, &frameFence);

    // Make uploads visible to compute
    VkSubmitInfo uploadsFinished = {};
    uploadsFinished.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    uploadsFinished.signalSemaphoreCount = 1;
    uploadsFinished.pSignalSemaphores = &graphicsFinishedSemaphore;
    vkQueueSubmit(device->GetQueue(QueueFlags::Graphics), 1, &uploadsFinished, VK_NULL_HANDLE);
    RecordCommandBuffers();
    RecordComputeCommandBuffer();
}

void Renderer::CreateCommandPools() {
    VkCommandPoolCreateInfo graphicsPoolInfo = {};
    graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    graphicsPoolInfo.queueFamilyIndex = device->GetInstance()->GetQueueFamilyIndices()[QueueFlags::Graphics];
    graphicsPoolInfo.flags = 0;

    if (vkCreateCommandPool(logicalDevice, &graphicsPoolInfo, nullptr, &graphicsCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    VkCommandPoolCreateInfo computePoolInfo = {};
    computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    computePoolInfo.queueFamilyIndex = device->GetInstance()->GetQueueFamilyIndices()[QueueFlags::Compute];
    computePoolInfo.flags = 0;

    if (vkCreateCommandPool(logicalDevice, &computePoolInfo, nullptr, &computeCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
}

void Renderer::CreateRenderPass() {
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
    VkFormat depthFormat = device->GetInstance()->GetSupportedFormat({ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
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

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

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

    if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
}

void Renderer::CreateCameraDescriptorSetLayout() {
    // Describe the binding of the descriptor set layout
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { uboLayoutBinding };

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &cameraDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void Renderer::CreateModelDescriptorSetLayout() {
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

    std::vector<VkDescriptorSetLayoutBinding> bindings = { uboLayoutBinding, samplerLayoutBinding };

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &modelDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }

    // Grass only needs a transform
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;
    vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &grassDescriptorSetLayout);
}

void Renderer::CreateTimeDescriptorSetLayout() {
    // Describe the binding of the descriptor set layout
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { uboLayoutBinding };

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &timeDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void Renderer::CreateComputeDescriptorSetLayout() {
    // TODO: Create the descriptor set layout for the compute pipeline
    // Remember this is like a class definition stating why types of information
    // will be stored at each binding
    // Set 2: blades, survivors, draw count, transform
    std::array<VkDescriptorSetLayoutBinding, 4> bindings = {};
    for (uint32_t binding = 0; binding < bindings.size(); ++binding) {
        bindings[binding].binding = binding;
        bindings[binding].descriptorCount = 1;
        bindings[binding].descriptorType = binding < 3 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &computeDescriptorSetLayout);
}

void Renderer::CreateDescriptorPool() {
    uint32_t modelCount = static_cast<uint32_t>(scene->GetModels().size());
    uint32_t grassCount = static_cast<uint32_t>(scene->GetBlades().size());
    // Describe which descriptor types that the descriptor sets will contain
    std::vector<VkDescriptorPoolSize> poolSizes = {
        // Camera
        // Models + Blades
        // Time (compute)
        // Grass shares transforms across two sets
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 + modelCount + 2 * grassCount },

        // Models + Blades
        // Grass has no texture
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, modelCount },

        // TODO: Add any additional types and counts of descriptors you will need to allocate
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * grassCount }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2 + modelCount + 2 * grassCount;

    if (vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

void Renderer::CreateCameraDescriptorSet() {
    // Describe the desciptor set
    VkDescriptorSetLayout layouts[] = { cameraDescriptorSetLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layouts;

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &cameraDescriptorSet) != VK_SUCCESS) {
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

void Renderer::CreateModelDescriptorSets() {
    modelDescriptorSets.resize(scene->GetModels().size());

    // Describe the desciptor set
    std::vector<VkDescriptorSetLayout> layouts(modelDescriptorSets.size(), modelDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(modelDescriptorSets.size());
    allocInfo.pSetLayouts = layouts.data();

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, modelDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites(2 * modelDescriptorSets.size());
    // Keep infos alive until descriptor writes
    std::vector<VkDescriptorBufferInfo> bufferInfos(modelDescriptorSets.size());
    std::vector<VkDescriptorImageInfo> imageInfos(modelDescriptorSets.size());

    for (uint32_t i = 0; i < scene->GetModels().size(); ++i) {
        VkDescriptorBufferInfo& modelBufferInfo = bufferInfos[i];
        modelBufferInfo.buffer = scene->GetModels()[i]->GetModelBuffer();
        modelBufferInfo.offset = 0;
        modelBufferInfo.range = sizeof(ModelBufferObject);

        // Bind image and sampler resources to the descriptor
        VkDescriptorImageInfo& imageInfo = imageInfos[i];
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = scene->GetModels()[i]->GetTextureView();
        imageInfo.sampler = scene->GetModels()[i]->GetTextureSampler();

        descriptorWrites[2 * i + 0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2 * i + 0].dstSet = modelDescriptorSets[i];
        descriptorWrites[2 * i + 0].dstBinding = 0;
        descriptorWrites[2 * i + 0].dstArrayElement = 0;
        descriptorWrites[2 * i + 0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[2 * i + 0].descriptorCount = 1;
        descriptorWrites[2 * i + 0].pBufferInfo = &modelBufferInfo;
        descriptorWrites[2 * i + 0].pImageInfo = nullptr;
        descriptorWrites[2 * i + 0].pTexelBufferView = nullptr;

        descriptorWrites[2 * i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2 * i + 1].dstSet = modelDescriptorSets[i];
        descriptorWrites[2 * i + 1].dstBinding = 1;
        descriptorWrites[2 * i + 1].dstArrayElement = 0;
        descriptorWrites[2 * i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2 * i + 1].descriptorCount = 1;
        descriptorWrites[2 * i + 1].pImageInfo = &imageInfo;
    }

    // Update descriptor sets
    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Renderer::CreateGrassDescriptorSets() {
    // TODO: Create Descriptor sets for the grass.
    // This should involve creating descriptor sets which point to the model matrix of each group of grass blades
    grassDescriptorSets.resize(scene->GetBlades().size());
    std::vector<VkDescriptorSetLayout> layouts(grassDescriptorSets.size(), grassDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(logicalDevice, &allocInfo, grassDescriptorSets.data());

    for (uint32_t i = 0; i < grassDescriptorSets.size(); ++i) {
        VkDescriptorBufferInfo modelInfo = {};
        modelInfo.buffer = scene->GetBlades()[i]->GetModelBuffer();
        modelInfo.range = sizeof(ModelBufferObject);
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = grassDescriptorSets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &modelInfo;
        vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);
    }
}

void Renderer::CreateTimeDescriptorSet() {
    // Describe the desciptor set
    VkDescriptorSetLayout layouts[] = { timeDescriptorSetLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layouts;

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &timeDescriptorSet) != VK_SUCCESS) {
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

void Renderer::CreateComputeDescriptorSets() {
    // TODO: Create Descriptor sets for the compute pipeline
    // The descriptors should point to Storage buffers which will hold the grass blades, the culled grass blades, and the output number of grass blades 
    computeDescriptorSets.resize(scene->GetBlades().size());
    std::vector<VkDescriptorSetLayout> layouts(computeDescriptorSets.size(), computeDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(logicalDevice, &allocInfo, computeDescriptorSets.data());

    for (uint32_t i = 0; i < computeDescriptorSets.size(); ++i) {
        Blades* blades = scene->GetBlades()[i];
        VkDeviceSize bladeBytes = static_cast<VkDeviceSize>(blades->GetBladeCount()) * sizeof(Blade);
        std::array<VkDescriptorBufferInfo, 4> infos = {};
        infos[0].buffer = blades->GetBladesBuffer();
        infos[0].range = bladeBytes;
        infos[1].buffer = blades->GetCulledBladesBuffer();
        infos[1].range = bladeBytes;
        infos[2].buffer = blades->GetNumBladesBuffer();
        infos[2].range = sizeof(BladeDrawIndirect);
        infos[3].buffer = blades->GetModelBuffer();
        infos[3].range = sizeof(ModelBufferObject);

        std::array<VkWriteDescriptorSet, 4> writes = {};
        for (uint32_t binding = 0; binding < writes.size(); ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = computeDescriptorSets[i];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType = binding < 3 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[binding].pBufferInfo = &infos[binding];
        }
        vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void Renderer::CreateGraphicsPipeline() {
    VkShaderModule vertShaderModule = ShaderModule::Create("shaders/graphics.vert.spv", logicalDevice);
    VkShaderModule fragShaderModule = ShaderModule::Create("shaders/graphics.frag.spv", logicalDevice);

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

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

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
    scissor.offset = { 0, 0 };
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

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { cameraDescriptorSetLayout, modelDescriptorSetLayout };

    // Pipeline layout: used to specify uniform values
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &graphicsPipelineLayout) != VK_SUCCESS) {
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

    if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
}

void Renderer::CreateGrassPipeline() {
    // --- Set up programmable shaders ---
    VkShaderModule vertShaderModule = ShaderModule::Create("shaders/grass.vert.spv", logicalDevice);
    VkShaderModule tescShaderModule = ShaderModule::Create("shaders/grass.tesc.spv", logicalDevice);
    VkShaderModule teseShaderModule = ShaderModule::Create("shaders/grass.tese.spv", logicalDevice);
    VkShaderModule fragShaderModule = ShaderModule::Create("shaders/grass.frag.spv", logicalDevice);

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

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, tescShaderStageInfo, teseShaderStageInfo, fragShaderStageInfo };

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
    scissor.offset = { 0, 0 };
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

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { cameraDescriptorSetLayout, grassDescriptorSetLayout };

    // Pipeline layout: used to specify uniform values
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &grassPipelineLayout) != VK_SUCCESS) {
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

    if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &grassPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    // No need for the shader modules anymore
    vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, tescShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, teseShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
}

void Renderer::CreateComputePipeline() {
    // Set up programmable shaders
    VkShaderModule computeShaderModule = ShaderModule::Create("shaders/compute.comp.spv", logicalDevice);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo = {};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    // TODO: Add the compute dsecriptor set layout you create to this list
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { cameraDescriptorSetLayout, timeDescriptorSetLayout, computeDescriptorSetLayout };

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
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

    if (vkCreateComputePipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline");
    }

    // No need for shader modules anymore
    vkDestroyShaderModule(logicalDevice, computeShaderModule, nullptr);
}

void Renderer::CreateFrameResources() {
    imageViews.resize(swapChain->GetCount());

    for (uint32_t i = 0; i < swapChain->GetCount(); i++) {
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
        if (vkCreateImageView(logicalDevice, &createInfo, nullptr, &imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image views");
        }
    }

    VkFormat depthFormat = device->GetInstance()->GetSupportedFormat({ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    // CREATE DEPTH IMAGE
    Image::Create(device,
        swapChain->GetVkExtent().width,
        swapChain->GetVkExtent().height,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depthImage,
        depthImageMemory
    );

    depthImageView = Image::CreateView(device, depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    
    // Transition the image for use as depth-stencil
    Image::TransitionLayout(device, graphicsCommandPool, depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    
    // CREATE FRAMEBUFFERS
    framebuffers.resize(swapChain->GetCount());
    for (size_t i = 0; i < swapChain->GetCount(); i++) {
        std::vector<VkImageView> attachments = {
            imageViews[i],
            depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChain->GetVkExtent().width;
        framebufferInfo.height = swapChain->GetVkExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }

    }
}

void Renderer::DestroyFrameResources() {
    // Drop framebuffers before their views
    for (size_t i = 0; i < framebuffers.size(); i++) {
        vkDestroyFramebuffer(logicalDevice, framebuffers[i], nullptr);
    }
    for (size_t i = 0; i < imageViews.size(); i++) {
        vkDestroyImageView(logicalDevice, imageViews[i], nullptr);
    }

    vkDestroyImageView(logicalDevice, depthImageView, nullptr);
    vkDestroyImage(logicalDevice, depthImage, nullptr);
    vkFreeMemory(logicalDevice, depthImageMemory, nullptr);
}

void Renderer::RecreateFrameResources() {
    // Finish old frames before rebuilding
    vkDeviceWaitIdle(logicalDevice);
    vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, grassPipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice, graphicsPipelineLayout, nullptr);
    vkDestroyPipelineLayout(logicalDevice, grassPipelineLayout, nullptr);
    vkFreeCommandBuffers(logicalDevice, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

    DestroyFrameResources();
    // Drop views before swapchain images
    swapChain->Recreate();
    CreateFrameResources();
    VkExtent2D extent = swapChain->GetVkExtent();
    camera->UpdateAspectRatio(static_cast<float>(extent.width) / extent.height);
    CreateGraphicsPipeline();
    CreateGrassPipeline();
    RecordCommandBuffers();
}

void Renderer::RecordComputeCommandBuffer() {
    // Specify the command pool and number of buffers to allocate
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = computeCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &computeCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    // ~ Start recording ~
    if (vkBeginCommandBuffer(computeCommandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording compute command buffer");
    }

    // Bind to the compute pipeline
    vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);

    // Bind camera descriptor set
    vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &cameraDescriptorSet, 0, nullptr);

    // Bind descriptor set for time uniforms
    vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 1, 1, &timeDescriptorSet, 0, nullptr);

    // TODO: For each group of blades bind its descriptor set and dispatch
    // Previous frame's simulation writes
    VkMemoryBarrier stateBarrier = {};
    stateBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    stateBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    stateBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(computeCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &stateBarrier, 0, nullptr, 0, nullptr);

    for (uint32_t i = 0; i < scene->GetBlades().size(); ++i) {
        Blades* blades = scene->GetBlades()[i];
        // Reset count before any workgroup runs
        vkCmdFillBuffer(computeCommandBuffer, blades->GetNumBladesBuffer(), 0, sizeof(uint32_t), 0);
        VkBufferMemoryBarrier countBarrier = {};
        countBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        countBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        countBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        countBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        countBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        countBarrier.buffer = blades->GetNumBladesBuffer();
        countBarrier.size = sizeof(uint32_t);
        vkCmdPipelineBarrier(computeCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 1, &countBarrier, 0, nullptr);

        vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout,
            2, 1, &computeDescriptorSets[i], 0, nullptr);
        uint32_t groups = (blades->GetBladeCount() + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        vkCmdDispatch(computeCommandBuffer, groups, 1, 1);
    }

    // ~ End recording ~
    if (vkEndCommandBuffer(computeCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record compute command buffer");
    }
}

void Renderer::RecordCommandBuffers() {
    commandBuffers.resize(swapChain->GetCount());

    // Specify the command pool and number of buffers to allocate
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = graphicsCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    // Start command buffer recording
    for (size_t i = 0; i < commandBuffers.size(); i++) {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr;

        // ~ Start recording ~
        if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("Failed to begin recording command buffer");
        }

        // Begin the render pass
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[i];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = swapChain->GetVkExtent();

        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
        clearValues[1].depthStencil = { 1.0f, 0 };
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        // Same queue: compute writes before draw reads
        if (device->GetQueue(QueueFlags::Compute) == device->GetQueue(QueueFlags::Graphics)) {
            std::vector<VkBufferMemoryBarrier> barriers(2 * scene->GetBlades().size());
            for (uint32_t j = 0; j < scene->GetBlades().size(); ++j) {
                VkBufferMemoryBarrier& count = barriers[2 * j];
                count.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                count.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                count.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                count.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                count.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                count.buffer = scene->GetBlades()[j]->GetNumBladesBuffer();
                count.size = sizeof(BladeDrawIndirect);

                VkBufferMemoryBarrier& vertices = barriers[2 * j + 1];
                vertices = count;
                vertices.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
                vertices.buffer = scene->GetBlades()[j]->GetCulledBladesBuffer();
                vertices.size = static_cast<VkDeviceSize>(scene->GetBlades()[j]->GetBladeCount()) * sizeof(Blade);
            }
            vkCmdPipelineBarrier(commandBuffers[i], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                0, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data(), 0, nullptr);
        }

        // Bind the camera descriptor set. This is set 0 in all pipelines so it will be inherited
        vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &cameraDescriptorSet, 0, nullptr);

        vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Bind the graphics pipeline
        vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        for (uint32_t j = 0; j < scene->GetModels().size(); ++j) {
            // Bind the vertex and index buffers
            VkBuffer vertexBuffers[] = { scene->GetModels()[j]->getVertexBuffer() };
            VkDeviceSize offsets[] = { 0 };
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

        for (uint32_t j = 0; j < scene->GetBlades().size(); ++j) {
            VkBuffer vertexBuffers[] = { scene->GetBlades()[j]->GetCulledBladesBuffer() };
            VkDeviceSize offsets[] = { 0 };
            // TODO: Uncomment this when the buffers are populated
            // vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);
            vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

            // TODO: Bind the descriptor set for each grass blades model
            vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, grassPipelineLayout,
                1, 1, &grassDescriptorSets[j], 0, nullptr);

            // Draw
            // TODO: Uncomment this when the buffers are populated
            // vkCmdDrawIndirect(commandBuffers[i], scene->GetBlades()[j]->GetNumBladesBuffer(), 0, 1, sizeof(BladeDrawIndirect));
            vkCmdDrawIndirect(commandBuffers[i], scene->GetBlades()[j]->GetNumBladesBuffer(), 0, 1, sizeof(BladeDrawIndirect));
        }

        // End render pass
        vkCmdEndRenderPass(commandBuffers[i]);

        // ~ End recording ~
        if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to record command buffer");
        }
    }
}

void Renderer::BeginFrame() {
    // Wait before updating shared uniforms
    vkWaitForFences(logicalDevice, 1, &frameFence, VK_TRUE, UINT64_MAX);
}

bool Renderer::Frame() {
    // Acquire before resetting the fence
    if (!swapChain->Acquire()) {
        RecreateFrameResources();
        return false;
    }
    vkResetFences(logicalDevice, 1, &frameFence);

    VkSubmitInfo computeSubmitInfo = {};
    computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkPipelineStageFlags computeWaitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    computeSubmitInfo.waitSemaphoreCount = 1;
    computeSubmitInfo.pWaitSemaphores = &graphicsFinishedSemaphore;
    computeSubmitInfo.pWaitDstStageMask = &computeWaitStage;
    computeSubmitInfo.signalSemaphoreCount = 1;
    computeSubmitInfo.pSignalSemaphores = &computeFinishedSemaphore;

    computeSubmitInfo.commandBufferCount = 1;
    computeSubmitInfo.pCommandBuffers = &computeCommandBuffer;

    if (vkQueueSubmit(device->GetQueue(QueueFlags::Compute), 1, &computeSubmitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    // Submit the command buffer
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // Wait at each resource's first use
    VkSemaphore waitSemaphores[] = { swapChain->GetImageAvailableVkSemaphore(), computeFinishedSemaphore };
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
    };
    submitInfo.waitSemaphoreCount = 2;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[swapChain->GetIndex()];

    VkSemaphore signalSemaphores[] = { swapChain->GetRenderFinishedVkSemaphore(), graphicsFinishedSemaphore };
    submitInfo.signalSemaphoreCount = 2;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device->GetQueue(QueueFlags::Graphics), 1, &submitInfo, frameFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    if (!swapChain->Present()) {
        RecreateFrameResources();
        return false;
    }
    return true;
}

Renderer::~Renderer() {
    vkDeviceWaitIdle(logicalDevice);

    // TODO: destroy any resources you created
    vkDestroyFence(logicalDevice, frameFence, nullptr);
    vkDestroySemaphore(logicalDevice, graphicsFinishedSemaphore, nullptr);
    vkDestroySemaphore(logicalDevice, computeFinishedSemaphore, nullptr);

    vkFreeCommandBuffers(logicalDevice, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
    vkFreeCommandBuffers(logicalDevice, computeCommandPool, 1, &computeCommandBuffer);
    
    vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, grassPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, computePipeline, nullptr);

    vkDestroyPipelineLayout(logicalDevice, graphicsPipelineLayout, nullptr);
    vkDestroyPipelineLayout(logicalDevice, grassPipelineLayout, nullptr);
    vkDestroyPipelineLayout(logicalDevice, computePipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(logicalDevice, cameraDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, modelDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, timeDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, grassDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, computeDescriptorSetLayout, nullptr);

    vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);

    DestroyFrameResources();
    vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
    vkDestroyCommandPool(logicalDevice, computeCommandPool, nullptr);
    vkDestroyCommandPool(logicalDevice, graphicsCommandPool, nullptr);
}
