#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <memory>

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
    AuthoredCubemapAssetMissing = 8
};

class VulkanReflectionProbeResources {
public:
    void CreateBuiltInProcedural(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool
    );
    void Release();

    bool BuiltInProceduralReady(VkSampler sampler) const;
    VkImageView DescriptorViewFor(VkImageView fallbackView, VkSampler sampler) const;
    VkImageView BuiltInView() const;
    u32 FaceSize() const;
    u32 MipCount() const;
    VkFormat Format() const;

    void SetDescriptorSetsBound(u32 count);
    u32 DescriptorSetsBound() const;

private:
    std::unique_ptr<VulkanImage> m_BuiltInCubemapImage;
    VkImageView m_BuiltInCubemapView = VK_NULL_HANDLE;
    u32 m_DescriptorSetsBound = 0;
};

}
