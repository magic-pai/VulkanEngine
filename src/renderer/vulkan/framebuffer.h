#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDepthBuffer;
class VulkanDevice;
class VulkanRenderPass;
class VulkanSwapchain;

class VulkanFramebuffer {
public:
    VulkanFramebuffer(
        const VulkanDevice& device,
        const VulkanRenderPass& renderPass,
        const VulkanDepthBuffer& depthBuffer,
        const VulkanSwapchain& swapchain
    );

    ~VulkanFramebuffer();

    SE_DISABLE_COPY(VulkanFramebuffer);
    SE_DISABLE_MOVE(VulkanFramebuffer);

    const std::vector<VkFramebuffer>& Handles() const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanRenderPass& renderPass,
        const VulkanDepthBuffer& depthBuffer,
        const VulkanSwapchain& swapchain
    );
    void Release();

private:
    void CreateFramebuffers(
        const VulkanDevice& device,
        const VulkanRenderPass& renderPass,
        const VulkanDepthBuffer& depthBuffer,
        const VulkanSwapchain& swapchain
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_Framebuffers;
};

}
