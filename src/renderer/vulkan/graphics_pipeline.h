#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

struct PipelineSpec;
class VulkanDescriptorSetLayout;
class VulkanMaterialDescriptorSetLayout;
class VulkanDevice;
class VulkanRenderPass;
class VulkanSwapchain;

class VulkanGraphicsPipeline {
public:
    VulkanGraphicsPipeline(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
        const VulkanRenderPass& renderPass,
        const VulkanSwapchain& swapchain,
        const PipelineSpec& spec
    );
    VulkanGraphicsPipeline(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
        VkRenderPass renderPass,
        const VulkanSwapchain& swapchain,
        const PipelineSpec& spec
    );

    ~VulkanGraphicsPipeline();

    SE_DISABLE_COPY(VulkanGraphicsPipeline);
    SE_DISABLE_MOVE(VulkanGraphicsPipeline);

    VkPipeline Handle() const;
    VkPipelineLayout Layout() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
        const VulkanRenderPass& renderPass,
        const VulkanSwapchain& swapchain,
        const PipelineSpec& spec
    );
    void Recreate(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
        VkRenderPass renderPass,
        const VulkanSwapchain& swapchain,
        const PipelineSpec& spec
    );

    void Release();

private:
    void CreateGraphicsPipeline(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& frameDescriptorSetLayout,
        const VulkanMaterialDescriptorSetLayout& materialDescriptorSetLayout,
        VkRenderPass renderPass,
        const VulkanSwapchain& swapchain,
        const PipelineSpec& spec
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};

}
