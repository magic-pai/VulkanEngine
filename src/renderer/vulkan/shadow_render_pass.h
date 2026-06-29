#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;
class VulkanShadowMap;

class VulkanShadowRenderPass {
public:
    VulkanShadowRenderPass(
        const VulkanDevice& device,
        const VulkanShadowMap& shadowMap
    );
    ~VulkanShadowRenderPass();

    SE_DISABLE_COPY(VulkanShadowRenderPass);
    SE_DISABLE_MOVE(VulkanShadowRenderPass);

    VkRenderPass Handle() const;
    void Release();

private:
    void CreateRenderPass(
        const VulkanDevice& device,
        const VulkanShadowMap& shadowMap
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
};

}
