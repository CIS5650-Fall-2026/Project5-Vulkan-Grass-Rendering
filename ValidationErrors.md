Because for some reason the terminal window closes upon exiting the main program. Just writing down the validation layers here:

The Vulkan spec states: Any variable, Block-decorated OpTypeStruct, or Block-decorated OpTypeStruct members shared between the OpEntryPoint of two shader stages must have matching decorations as defined in interface matching (https://docs.vulkan.org/spec/latest/appendices/spirvenv.html#VUID-RuntimeSpirv-OpVariable-08746)
Validation layer: vkCmdDrawIndexed(): VkRenderingInfo::pColorAttachments[0].imageView is VkImageView 0x3a000000003a, but currently bound graphics pipeline VkPipeline 0x450000000045 was created with VkPipelineRenderingCreateInfo::pColorAttachmentFormats[0] equal to VK_FORMAT_UNDEFINED.

Validation layer: vkCreateGraphicsPipelines(): pCreateInfos[0].pColorBlendState->attachmentCount is 1, but VkPipelineRenderingCreateInfo::colorAttachmentCount is zero because the pNext chain was not included.
The Vulkan spec states: If renderPass is VK_NULL_HANDLE, pColorBlendState is not dynamic, and the pipeline is being created with fragment output interface state, pColorBlendState->attachmentCount must be equal to VkPipelineRenderingCreateInfo::colorAttachmentCount (https://docs.vulkan.org/spec/latest/chapters/pipelines.html#VUID-VkGraphicsPipelineCreateInfo-renderPass-06055)
Validation layer: vkCreateGraphicsPipelines(): pCreateInfos[0] (SPIR-V Interface) Mismatch in BuiltIn blocks:
 [VK_SHADER_STAGE_VERTEX_BIT] Output Block {
        0: Position
}
[VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT] Input Block {
        0: Position
        1: PointSize
        2: ClipDistance
        3: CullDistance
}