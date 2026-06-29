#pragma once

#include "renderer/vulkan/image.h"

namespace se {

class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanSceneRenderTargets;
class VulkanSwapchain;

class VulkanHdrRenderPass {
public:
    VulkanHdrRenderPass(const VulkanDevice& device, VkFormat hdrColorFormat);
    ~VulkanHdrRenderPass();

    SE_DISABLE_COPY(VulkanHdrRenderPass);
    SE_DISABLE_MOVE(VulkanHdrRenderPass);

    VkRenderPass Handle() const;

    void Recreate(const VulkanDevice& device, VkFormat hdrColorFormat);
    void Release();

private:
    void CreateRenderPass(const VulkanDevice& device, VkFormat hdrColorFormat);

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
};

class VulkanGBufferRenderPass {
public:
    explicit VulkanGBufferRenderPass(
        const VulkanDevice& device,
        const VulkanSceneRenderTargets& renderTargets
    );
    ~VulkanGBufferRenderPass();

    SE_DISABLE_COPY(VulkanGBufferRenderPass);
    SE_DISABLE_MOVE(VulkanGBufferRenderPass);

    VkRenderPass Handle() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanSceneRenderTargets& renderTargets
    );
    void Release();

private:
    void CreateRenderPass(
        const VulkanDevice& device,
        const VulkanSceneRenderTargets& renderTargets
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
};

class VulkanSceneRenderTargets {
public:
    VulkanSceneRenderTargets(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanSwapchain& swapchain
    );

    ~VulkanSceneRenderTargets();

    SE_DISABLE_COPY(VulkanSceneRenderTargets);
    SE_DISABLE_MOVE(VulkanSceneRenderTargets);

    VkImageView HdrSceneColorView(std::size_t index) const;
    VkImage SceneDepthImage(std::size_t index) const;
    VkImageView SceneDepthView(std::size_t index) const;
    VkImageView VelocityView(std::size_t index) const;
    VkImageView GBufferAlbedoView(std::size_t index) const;
    VkImageView GBufferNormalRoughnessView(std::size_t index) const;
    VkImageView GBufferMaterialView(std::size_t index) const;
    VkImageView GBufferEmissiveView(std::size_t index) const;
    VkFormat HdrSceneColorFormat() const;
    VkFormat SceneDepthFormat() const;
    VkFormat VelocityFormat() const;
    VkFormat GBufferAlbedoFormat() const;
    VkFormat GBufferNormalRoughnessFormat() const;
    VkFormat GBufferMaterialFormat() const;
    VkFormat GBufferEmissiveFormat() const;
    VkExtent2D Extent() const;
    std::size_t Count() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanSwapchain& swapchain
    );
    void Release();

    static constexpr VkFormat kHdrSceneColorFormat =
        VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kSceneDepthFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat kVelocityFormat = VK_FORMAT_R16G16_SFLOAT;
    static constexpr VkFormat kGBufferAlbedoFormat = VK_FORMAT_R8G8B8A8_SRGB;
    static constexpr VkFormat kGBufferNormalRoughnessFormat =
        VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kGBufferMaterialFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat kGBufferEmissiveFormat =
        VK_FORMAT_R16G16B16A16_SFLOAT;

private:
    void CreateImageArray(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspectFlags,
        std::vector<std::unique_ptr<VulkanImage>>& images
    ) const;

private:
    std::vector<std::unique_ptr<VulkanImage>> m_HdrSceneColorImages;
    std::vector<std::unique_ptr<VulkanImage>> m_SceneDepthImages;
    std::vector<std::unique_ptr<VulkanImage>> m_VelocityImages;
    std::vector<std::unique_ptr<VulkanImage>> m_GBufferAlbedoImages;
    std::vector<std::unique_ptr<VulkanImage>> m_GBufferNormalRoughnessImages;
    std::vector<std::unique_ptr<VulkanImage>> m_GBufferMaterialImages;
    std::vector<std::unique_ptr<VulkanImage>> m_GBufferEmissiveImages;
    VkExtent2D m_Extent{};
};

class VulkanHdrFramebuffer {
public:
    VulkanHdrFramebuffer(
        const VulkanDevice& device,
        const VulkanHdrRenderPass& renderPass,
        const VulkanSceneRenderTargets& renderTargets
    );

    ~VulkanHdrFramebuffer();

    SE_DISABLE_COPY(VulkanHdrFramebuffer);
    SE_DISABLE_MOVE(VulkanHdrFramebuffer);

    VkFramebuffer Handle(std::size_t index) const;
    VkExtent2D Extent() const;
    std::size_t Count() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanHdrRenderPass& renderPass,
        const VulkanSceneRenderTargets& renderTargets
    );
    void Release();

private:
    void CreateFramebuffers(
        const VulkanDevice& device,
        const VulkanHdrRenderPass& renderPass,
        const VulkanSceneRenderTargets& renderTargets
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_Framebuffers;
    VkExtent2D m_Extent{};
};

class VulkanGBufferFramebuffer {
public:
    VulkanGBufferFramebuffer(
        const VulkanDevice& device,
        const VulkanGBufferRenderPass& renderPass,
        const VulkanSceneRenderTargets& renderTargets
    );

    ~VulkanGBufferFramebuffer();

    SE_DISABLE_COPY(VulkanGBufferFramebuffer);
    SE_DISABLE_MOVE(VulkanGBufferFramebuffer);

    VkFramebuffer Handle(std::size_t index) const;
    VkExtent2D Extent() const;
    std::size_t Count() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanGBufferRenderPass& renderPass,
        const VulkanSceneRenderTargets& renderTargets
    );
    void Release();

private:
    void CreateFramebuffers(
        const VulkanDevice& device,
        const VulkanGBufferRenderPass& renderPass,
        const VulkanSceneRenderTargets& renderTargets
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_Framebuffers;
    VkExtent2D m_Extent{};
};

}
