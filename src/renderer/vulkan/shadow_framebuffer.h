#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;
class VulkanDirectionalShadowCascadeAtlas;
class VulkanLocalShadowAtlas;
class VulkanShadowMap;
class VulkanShadowRenderPass;

class VulkanShadowFramebuffer {
public:
    VulkanShadowFramebuffer(
        const VulkanDevice& device,
        const VulkanShadowRenderPass& renderPass,
        const VulkanShadowMap& shadowMap
    );
    VulkanShadowFramebuffer(
        const VulkanDevice& device,
        const VulkanShadowRenderPass& renderPass,
        const VulkanDirectionalShadowCascadeAtlas& atlas
    );
    VulkanShadowFramebuffer(
        const VulkanDevice& device,
        const VulkanShadowRenderPass& renderPass,
        const VulkanLocalShadowAtlas& atlas
    );
    ~VulkanShadowFramebuffer();

    SE_DISABLE_COPY(VulkanShadowFramebuffer);
    SE_DISABLE_MOVE(VulkanShadowFramebuffer);

    VkFramebuffer Handle() const;
    VkFramebuffer Handle(std::size_t index) const;
    VkExtent2D Extent() const;
    std::size_t Count() const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanShadowRenderPass& renderPass,
        const VulkanShadowMap& shadowMap
    );
    void Recreate(
        const VulkanDevice& device,
        const VulkanShadowRenderPass& renderPass,
        const VulkanDirectionalShadowCascadeAtlas& atlas
    );
    void Recreate(
        const VulkanDevice& device,
        const VulkanShadowRenderPass& renderPass,
        const VulkanLocalShadowAtlas& atlas
    );
    void Release();

private:
    void CreateFramebuffer(
        const VulkanDevice& device,
        const VulkanShadowRenderPass& renderPass,
        const VulkanShadowMap& shadowMap
    );
    void CreateFramebuffer(
        const VulkanDevice& device,
        const VulkanShadowRenderPass& renderPass,
        const VulkanDirectionalShadowCascadeAtlas& atlas
    );
    void CreateFramebuffer(
        const VulkanDevice& device,
        const VulkanShadowRenderPass& renderPass,
        const VulkanLocalShadowAtlas& atlas
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_Framebuffers;
    VkExtent2D m_Extent{};
};

}
