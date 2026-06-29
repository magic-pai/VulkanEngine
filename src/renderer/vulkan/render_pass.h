#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;
class VulkanDepthBuffer;
class VulkanSwapchain;

class VulkanRenderPass {
public:
    VulkanRenderPass(
        const VulkanDevice& device,
        const VulkanSwapchain& swapchain,
        const VulkanDepthBuffer& depthBuffer,
        bool loadDepth = false
    );
    ~VulkanRenderPass();

    SE_DISABLE_COPY(VulkanRenderPass);
    SE_DISABLE_MOVE(VulkanRenderPass);

    VkRenderPass Handle() const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanSwapchain& swapchain,
        const VulkanDepthBuffer& depthBuffer,
        bool loadDepth = false
    );
    void Release();

private:
    void CreateRenderPass(
        const VulkanDevice& device,
        const VulkanSwapchain& swapchain,
        const VulkanDepthBuffer& depthBuffer,
        bool loadDepth
    );
private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
};

}
