#pragma once
#include "renderer/vulkan/vulkan_common.h"
#include <memory>
#include <string>
#include <vector>

namespace se {
class VulkanImage;
class VulkanBuffer;
class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;

constexpr u32 kIblBrdfLutSize = 128;
constexpr u32 kIblIrradianceFaceSize = 32;
constexpr u32 kIblPrefilteredFaceSize = 256;
constexpr u32 kIblPrefilteredMipCount = 5;
constexpr VkFormat kIblBrdfLutFormat = VK_FORMAT_R16G16_SFLOAT;
constexpr VkFormat kIblEnvironmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr u32 kReflectionProbeCubemapFaceSize = 64;
constexpr u32 kReflectionProbeCubemapMipCount = 5;
constexpr VkFormat kReflectionProbeCubemapFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

enum class VulkanIblQuality : u32 {
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3
};

enum class VulkanIblSource : u32 {
    Procedural = 0,
    VisibleSkybox = 1,
    AuthoredEquirectangular = 2,
    AuthoredCubemap = 3
};

enum class VulkanIblSourceFallbackReason : u32 {
    None = 0,
    RuntimeSourceUnsupported = 1,
    SourceAssetMissing = 2,
    SourceLoadFailed = 3
};

enum class VulkanIblCachePolicy : u32 {
    RuntimeGenerated = 0,
    PreferOffline = 1
};

enum class VulkanIblCacheFallbackReason : u32 {
    None = 0,
    OfflineCacheUnavailable = 1
};

struct VulkanIblGenerationSettings {
    VulkanIblQuality quality = VulkanIblQuality::Medium;
    VulkanIblSource source = VulkanIblSource::Procedural;
    VulkanIblCachePolicy cachePolicy = VulkanIblCachePolicy::RuntimeGenerated;
    std::string sourceAssetPath;
};

struct VulkanIblGenerationInfo {
    VulkanIblQuality quality = VulkanIblQuality::Medium;
    VulkanIblSource requestedSource = VulkanIblSource::Procedural;
    VulkanIblSource actualSource = VulkanIblSource::Procedural;
    VulkanIblSourceFallbackReason sourceFallbackReason =
        VulkanIblSourceFallbackReason::None;
    VulkanIblCachePolicy cachePolicy = VulkanIblCachePolicy::RuntimeGenerated;
    VulkanIblCacheFallbackReason cacheFallbackReason =
        VulkanIblCacheFallbackReason::None;
    u32 cacheHit = 0;
    u32 runtimeGenerated = 1;
    u32 sourceAssetSpecified = 0;
    u32 sourceAssetFound = 0;
    u64 sourceSignature = 0;
    u32 brdfLutSize = kIblBrdfLutSize;
    u32 irradianceFaceSize = kIblIrradianceFaceSize;
    u32 prefilteredFaceSize = kIblPrefilteredFaceSize;
    u32 prefilteredMipCount = kIblPrefilteredMipCount;
};

void GenerateIblTextures(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool,
    std::unique_ptr<VulkanImage>& brdfImage, std::unique_ptr<VulkanImage>& irradianceImage,
    std::unique_ptr<VulkanImage>& prefilteredImage,
    VkImageView& irradianceView, VkImageView& prefilteredView, VkSampler& sampler,
    const VulkanIblGenerationSettings& settings = {},
    VulkanIblGenerationInfo* generationInfo = nullptr);

void GenerateReflectionProbeCubemap(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool,
    std::unique_ptr<VulkanImage>& cubemapImage, VkImageView& cubemapView);
}
