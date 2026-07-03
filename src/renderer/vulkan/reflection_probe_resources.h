#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanImage;
class VulkanPhysicalDevice;

enum class RendererReflectionProbeCaptureSource : u32 {
    None = 0,
    BuiltInProcedural = 1,
    AuthoredCubemap = 2,
    CapturedScene = 3
};

enum class RendererReflectionProbeCaptureFallbackReason : u32 {
    None = 0,
    SourceDisabled = 1,
    AuthoredCubemapNotLoaded = 2,
    CapturedSceneNotImplemented = 3,
    BuiltInResourceUnavailable = 4,
    CubemapSamplingDisabled = 5,
    NoActiveSceneProbe = 6,
    FallbackDisabled = 7,
    AuthoredCubemapAssetMissing = 8,
    AuthoredCubemapLoadFailed = 9
};

class VulkanReflectionProbeResources {
public:
    void CreateBuiltInProcedural(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool
    );
    void EnsureAuthoredCubemap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        std::string_view assetId
    );
    void Release();

    bool BuiltInProceduralReady(VkSampler sampler) const;
    bool AuthoredCubemapReady(std::string_view assetId, VkSampler sampler) const;
    bool AuthoredCubemapAssetFound(std::string_view assetId) const;
    bool AuthoredCubemapLoadFailed(std::string_view assetId) const;
    VkImageView DescriptorViewFor(VkImageView fallbackView, VkSampler sampler) const;
    VkImageView AuthoredDescriptorViewFor(
        std::string_view assetId,
        VkImageView fallbackView,
        VkSampler sampler
    ) const;
    VkImageView BuiltInView() const;
    u32 FaceSize() const;
    u32 MipCount() const;
    VkFormat Format() const;
    u32 AuthoredCubemapLoadedCount() const;
    u32 AuthoredCubemapMissingCount() const;
    u32 AuthoredCubemapLoadFailedCount() const;
    u32 AuthoredCubemapUploadCount() const;
    u32 AuthoredCubemapFaceSize(std::string_view assetId) const;
    u32 AuthoredCubemapMipCount(std::string_view assetId) const;
    VkFormat AuthoredCubemapFormat(std::string_view assetId) const;

    void SetDescriptorSetsBound(u32 count);
    u32 DescriptorSetsBound() const;

private:
    struct AuthoredCubemapResource {
        std::unique_ptr<VulkanImage> image;
        bool assetFound = false;
        bool loadFailed = false;
    };

private:
    std::unique_ptr<VulkanImage> m_BuiltInCubemapImage;
    VkImageView m_BuiltInCubemapView = VK_NULL_HANDLE;
    std::unordered_map<std::string, AuthoredCubemapResource> m_AuthoredCubemaps;
    u32 m_AuthoredCubemapUploadCount = 0;
    u32 m_DescriptorSetsBound = 0;
};

}
