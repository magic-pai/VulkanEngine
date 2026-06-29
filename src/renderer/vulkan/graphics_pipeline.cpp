#include "renderer/vulkan/graphics_pipeline.h"

#include "renderer/vulkan/descriptor_set_layout.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/pipeline_spec.h"
#include "renderer/vulkan/render_pass.h"
#include "renderer/vulkan/shader_module.h"
#include "renderer/vulkan/swapchain.h"
#include "renderer/vulkan/uniform_buffer.h"
#include "renderer/vulkan/vertex.h"

namespace se {

namespace {

VkPipelineShaderStageCreateInfo CreateShaderStageInfo(
    VkShaderStageFlagBits stage,
    VkShaderModule shaderModule
) {
    VkPipelineShaderStageCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    createInfo.stage = stage;
    createInfo.module = shaderModule;
    createInfo.pName = "main";

    return createInfo;
}

struct VertexInputDescription {
    std::array<VkVertexInputBindingDescription, 2> bindingDescriptions{};
    u32 bindingCount = 0;
    std::array<VkVertexInputAttributeDescription, 9> attributeDescriptions{};
    u32 attributeCount = 0;
};

VertexInputDescription CreateVertexInputDescription(VertexLayout layout) {
    VertexInputDescription description{};

    switch (layout) {
    case VertexLayout::Vertex2D: {
        const auto attributes = Vertex::AttributeDescriptions();
        description.bindingDescriptions[0] = Vertex::BindingDescription();
        description.bindingCount = 1;
        description.attributeCount = static_cast<u32>(attributes.size());
        std::copy(attributes.begin(), attributes.end(), description.attributeDescriptions.begin());
        return description;
    }
    case VertexLayout::Vertex3D: {
        const auto attributes = Vertex3D::AttributeDescriptions();
        description.bindingDescriptions[0] = Vertex3D::BindingDescription();
        description.bindingCount = 1;
        description.attributeCount = static_cast<u32>(attributes.size());
        std::copy(attributes.begin(), attributes.end(), description.attributeDescriptions.begin());
        return description;
    }
    case VertexLayout::Vertex3DInstanced: {
        const auto vertexAttributes = Vertex3D::AttributeDescriptions();
        const auto instanceAttributes = Instance3D::AttributeDescriptions();
        description.bindingDescriptions[0] = Vertex3D::BindingDescription();
        description.bindingDescriptions[1] = Instance3D::BindingDescription();
        description.bindingCount = 2;
        description.attributeCount =
            static_cast<u32>(vertexAttributes.size() + instanceAttributes.size());
        std::copy(
            vertexAttributes.begin(),
            vertexAttributes.end(),
            description.attributeDescriptions.begin()
        );
        std::copy(
            instanceAttributes.begin(),
            instanceAttributes.end(),
            description.attributeDescriptions.begin() + vertexAttributes.size()
        );
        return description;
    }
    case VertexLayout::FullscreenTriangle:
        return description;
    }

    SE_ASSERT(false, "Unsupported pipeline vertex layout");
    return description;
}

}

VulkanGraphicsPipeline::VulkanGraphicsPipeline(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
    const VulkanRenderPass& renderPass,
    const VulkanSwapchain& swapchain,
    const PipelineSpec& spec
) : m_Device(device.Handle()) {
    CreateGraphicsPipeline(
        device,
        frameDescriptorSetLayout,
        materialDescriptorSetLayout,
        renderPass.Handle(),
        swapchain,
        spec
    );
}

VulkanGraphicsPipeline::VulkanGraphicsPipeline(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
    VkRenderPass renderPass,
    const VulkanSwapchain& swapchain,
    const PipelineSpec& spec
) : m_Device(device.Handle()) {
    CreateGraphicsPipeline(
        device,
        frameDescriptorSetLayout,
        materialDescriptorSetLayout,
        renderPass,
        swapchain,
        spec
    );
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {
    Release();
}

VkPipeline VulkanGraphicsPipeline::Handle() const {
    return m_Pipeline;
}

VkPipelineLayout VulkanGraphicsPipeline::Layout() const {
    return m_PipelineLayout;
}

void VulkanGraphicsPipeline::Recreate(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
    const VulkanRenderPass& renderPass,
    const VulkanSwapchain& swapchain,
    const PipelineSpec& spec
) {
    Release();
    CreateGraphicsPipeline(
        device,
        frameDescriptorSetLayout,
        materialDescriptorSetLayout,
        renderPass.Handle(),
        swapchain,
        spec
    );
}

void VulkanGraphicsPipeline::Recreate(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
    VkRenderPass renderPass,
    const VulkanSwapchain& swapchain,
    const PipelineSpec& spec
) {
    Release();
    CreateGraphicsPipeline(
        device,
        frameDescriptorSetLayout,
        materialDescriptorSetLayout,
        renderPass,
        swapchain,
        spec
    );
}

void VulkanGraphicsPipeline::CreateGraphicsPipeline(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
    VkRenderPass renderPass,
    const VulkanSwapchain& swapchain,
    const PipelineSpec& spec
) {
    VulkanShaderModule vertexShader(device, spec.vertexShaderPath);
    std::unique_ptr<VulkanShaderModule> fragmentShader;
    if (spec.hasFragmentShader) {
        fragmentShader = std::make_unique<VulkanShaderModule>(device, spec.fragmentShaderPath);
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0] = CreateShaderStageInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexShader.Handle());
    u32 shaderStageCount = 1;
    if (fragmentShader != nullptr) {
        shaderStages[1] = CreateShaderStageInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader->Handle());
        shaderStageCount = 2;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    const VertexInputDescription vertexInputDescription =
        CreateVertexInputDescription(spec.vertexLayout);

    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = vertexInputDescription.bindingCount;
    vertexInputInfo.pVertexBindingDescriptions = vertexInputDescription.bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = vertexInputDescription.attributeCount;
    vertexInputInfo.pVertexAttributeDescriptions = vertexInputDescription.attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = spec.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    const VkExtent2D extent =
        spec.fixedExtent.width > 0 && spec.fixedExtent.height > 0
        ? spec.fixedExtent
        : swapchain.Extent();

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<f32>(extent.width);
    viewport.height = static_cast<f32>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = spec.dynamicViewportScissor ? nullptr : &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = spec.dynamicViewportScissor ? nullptr : &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = spec.cullMode;
    rasterizer.frontFace = spec.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = spec.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = spec.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = spec.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    SE_ASSERT(
        spec.hasColorAttachment || spec.colorAttachmentCount == 0,
        "Pipelines without color attachments must use a zero color attachment count"
    );
    const u32 colorAttachmentCount =
        spec.hasColorAttachment ? std::max(spec.colorAttachmentCount, 1u) : 0u;
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(
        colorAttachmentCount
    );
    for (u32 attachmentIndex = 0; attachmentIndex < colorAttachmentCount; ++attachmentIndex) {
        VkPipelineColorBlendAttachmentState& colorBlendAttachment =
            colorBlendAttachments[attachmentIndex];
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        ColorBlendMode blendMode = spec.colorBlendModes[attachmentIndex];
        if (blendMode == ColorBlendMode::Disabled && spec.alphaBlendEnabled) {
            blendMode = ColorBlendMode::Alpha;
        }

        switch (blendMode) {
        case ColorBlendMode::Disabled:
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case ColorBlendMode::Alpha:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case ColorBlendMode::Additive:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case ColorBlendMode::ZeroSource:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = colorAttachmentCount;
    colorBlending.pAttachments = colorBlendAttachments.data();
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    const std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    const VkDescriptorSetLayout descriptorSetLayouts[] = {
        frameDescriptorSetLayout.Handle(),
        materialDescriptorSetLayout.Handle()
    };

    VkPushConstantRange objectPushConstantRange{};
    objectPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    objectPushConstantRange.offset = 0;
    objectPushConstantRange.size = sizeof(ObjectPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(
        sizeof(descriptorSetLayouts) / sizeof(descriptorSetLayouts[0])
    );
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &objectPushConstantRange;

    if (vkCreatePipelineLayout(device.Handle(), &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = shaderStageCount;
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = spec.dynamicViewportScissor ? &dynamicState : nullptr;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(
            device.Handle(),
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &m_Pipeline
        ) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to create Vulkan graphics pipeline");
    }
}

void VulkanGraphicsPipeline::Release() {
    if (m_Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
        m_Pipeline = VK_NULL_HANDLE;
    }

    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }
}

}
