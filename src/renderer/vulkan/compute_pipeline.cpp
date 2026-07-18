#include "renderer/vulkan/compute_pipeline.h"

#include "renderer/vulkan/descriptor_set_layout.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/shader_module.h"

namespace se {

VulkanComputePipeline::VulkanComputePipeline(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const VulkanMaterialDescriptorSetLayout& sampledImageDescriptorSetLayout,
    const std::string& computeShaderPath
) : m_Device(device.Handle()) {
    const VkDescriptorSetLayout descriptorSetLayouts[] = {
        frameDescriptorSetLayout.Handle(),
        sampledImageDescriptorSetLayout.Handle()
    };
    CreateComputePipeline(device, descriptorSetLayouts, computeShaderPath);
}

VulkanComputePipeline::VulkanComputePipeline(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const std::string& computeShaderPath
) : m_Device(device.Handle()) {
    const VkDescriptorSetLayout descriptorSetLayouts[] = {
        frameDescriptorSetLayout.Handle()
    };
    CreateComputePipeline(device, descriptorSetLayouts, computeShaderPath);
}

VulkanComputePipeline::VulkanComputePipeline(
    const VulkanDevice& device,
    const VulkanHiZDescriptorSetLayout& descriptorSetLayout,
    const std::string& computeShaderPath
) : m_Device(device.Handle()) {
    const VkDescriptorSetLayout descriptorSetLayouts[] = {
        descriptorSetLayout.Handle()
    };
    CreateComputePipeline(device, descriptorSetLayouts, computeShaderPath);
}

VulkanComputePipeline::VulkanComputePipeline(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const VulkanSsrReconstructionDescriptorSetLayout&
        reconstructionDescriptorSetLayout,
    const std::string& computeShaderPath
) : m_Device(device.Handle()) {
    const VkDescriptorSetLayout descriptorSetLayouts[] = {
        frameDescriptorSetLayout.Handle(),
        reconstructionDescriptorSetLayout.Handle()
    };
    CreateComputePipeline(device, descriptorSetLayouts, computeShaderPath);
}

VulkanComputePipeline::~VulkanComputePipeline() {
    Release();
}

VkPipeline VulkanComputePipeline::Handle() const {
    return m_Pipeline;
}

VkPipelineLayout VulkanComputePipeline::Layout() const {
    return m_PipelineLayout;
}

void VulkanComputePipeline::Recreate(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const VulkanMaterialDescriptorSetLayout& sampledImageDescriptorSetLayout,
    const std::string& computeShaderPath
) {
    Release();
    const VkDescriptorSetLayout descriptorSetLayouts[] = {
        frameDescriptorSetLayout.Handle(),
        sampledImageDescriptorSetLayout.Handle()
    };
    CreateComputePipeline(device, descriptorSetLayouts, computeShaderPath);
}

void VulkanComputePipeline::Recreate(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const VulkanSsrReconstructionDescriptorSetLayout&
        reconstructionDescriptorSetLayout,
    const std::string& computeShaderPath
) {
    Release();
    const VkDescriptorSetLayout descriptorSetLayouts[] = {
        frameDescriptorSetLayout.Handle(),
        reconstructionDescriptorSetLayout.Handle()
    };
    CreateComputePipeline(device, descriptorSetLayouts, computeShaderPath);
}

void VulkanComputePipeline::Recreate(
    const VulkanDevice& device,
    const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
    const std::string& computeShaderPath
) {
    Release();
    const VkDescriptorSetLayout descriptorSetLayouts[] = {
        frameDescriptorSetLayout.Handle()
    };
    CreateComputePipeline(device, descriptorSetLayouts, computeShaderPath);
}

void VulkanComputePipeline::CreateComputePipeline(
    const VulkanDevice& device,
    std::span<const VkDescriptorSetLayout> descriptorSetLayouts,
    const std::string& computeShaderPath
) {
    m_Device = device.Handle();

    VulkanShaderModule computeShader(device, computeShaderPath);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

    if (vkCreatePipelineLayout(
            device.Handle(),
            &pipelineLayoutInfo,
            nullptr,
            &m_PipelineLayout
        ) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan compute pipeline layout");
    }

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = computeShader.Handle();
    shaderStage.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateComputePipelines(
            device.Handle(),
            device.PipelineCacheHandle(),
            1,
            &pipelineInfo,
            nullptr,
            &m_Pipeline
        ) != VK_SUCCESS) {
        Release();
        throw std::runtime_error("Failed to create Vulkan compute pipeline");
    }
}

void VulkanComputePipeline::Release() {
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
