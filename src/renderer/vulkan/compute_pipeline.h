#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <span>

namespace se {

class VulkanDescriptorSetLayout;
class VulkanHiZDescriptorSetLayout;
class VulkanSsrReconstructionDescriptorSetLayout;
class VulkanMaterialDescriptorSetLayout;
class VulkanDevice;

class VulkanComputePipeline {
public:
    VulkanComputePipeline(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const VulkanMaterialDescriptorSetLayout& sampledImageDescriptorSetLayout,
        const std::string& computeShaderPath
    );

    VulkanComputePipeline(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const std::string& computeShaderPath
    );

    VulkanComputePipeline(
        const VulkanDevice& device,
        const VulkanHiZDescriptorSetLayout& descriptorSetLayout,
        const std::string& computeShaderPath
    );

    VulkanComputePipeline(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const VulkanSsrReconstructionDescriptorSetLayout&
            reconstructionDescriptorSetLayout,
        const std::string& computeShaderPath
    );

    ~VulkanComputePipeline();

    SE_DISABLE_COPY(VulkanComputePipeline);
    SE_DISABLE_MOVE(VulkanComputePipeline);

    VkPipeline Handle() const;
    VkPipelineLayout Layout() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const VulkanMaterialDescriptorSetLayout& sampledImageDescriptorSetLayout,
        const std::string& computeShaderPath
    );
    void Recreate(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const std::string& computeShaderPath
    );
    void Recreate(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const VulkanSsrReconstructionDescriptorSetLayout&
            reconstructionDescriptorSetLayout,
        const std::string& computeShaderPath
    );
    void Release();

private:
    void CreateComputePipeline(
        const VulkanDevice& device,
        std::span<const VkDescriptorSetLayout> descriptorSetLayouts,
        const std::string& computeShaderPath
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};

}
