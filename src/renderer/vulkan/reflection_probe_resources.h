#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace se {

inline constexpr std::size_t kAuthoredReflectionProbeDiffuseLobeCount = 6;
using AuthoredReflectionProbeDiffuseLobes = std::array<
    std::array<f32, 3>,
    kAuthoredReflectionProbeDiffuseLobeCount
>;

class VulkanCommandPool;
class VulkanDevice;
class VulkanImage;
class VulkanPhysicalDevice;

enum class AuthoredReflectionCubemapSourceType : u32 {
    Unknown = 0,
    SixFace = 1,
    Equirectangular = 2
};

enum class AuthoredReflectionProbeFilterQuality : u32 {
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3
};

struct AuthoredReflectionProbeFilteringSettings {
    AuthoredReflectionProbeFilterQuality quality =
        AuthoredReflectionProbeFilterQuality::Medium;
    bool seamAwareFiltering = true;
};

enum class RendererReflectionProbeCaptureSource : u32 {
    None = 0,
    BuiltInProcedural = 1,
    AuthoredCubemap = 2,
    CapturedScene = 3
};

enum class RendererReflectionProbeRefreshPolicy : u32 {
    Static = 0,
    FileSignature = 1,
    Forced = 2,
    SceneDirty = 3
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
        std::string_view assetId,
        AuthoredReflectionProbeFilteringSettings filteringSettings = {}
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
    u32 AuthoredCubemapSixFaceLoadedCount() const;
    u32 AuthoredCubemapEquirectangularLoadedCount() const;
    u32 AuthoredCubemapEquirectangularConversionCount() const;
    u32 AuthoredCubemapHdrLoadedCount() const;
    u32 AuthoredCubemapPrefilteredLoadedCount() const;
    u32 AuthoredCubemapPrefilteredUploadCount() const;
    u32 AuthoredCubemapCacheHitCount() const;
    u32 AuthoredCubemapReloadCount() const;
    u32 AuthoredCubemapRefreshCheckCount() const;
    u32 AuthoredCubemapFaceSize(std::string_view assetId) const;
    u32 AuthoredCubemapMipCount(std::string_view assetId) const;
    VkFormat AuthoredCubemapFormat(std::string_view assetId) const;
    bool AuthoredCubemapHdr(std::string_view assetId) const;
    bool AuthoredCubemapPrefiltered(std::string_view assetId) const;
    u32 AuthoredCubemapGeneratedMipCount(std::string_view assetId) const;
    u32 AuthoredCubemapPrefilterSampleCount(std::string_view assetId) const;
    AuthoredReflectionProbeFilterQuality AuthoredCubemapFilterQuality(
        std::string_view assetId
    ) const;
    bool AuthoredCubemapSeamAwareFiltering(std::string_view assetId) const;
    bool AuthoredCubemapIrradianceReady(std::string_view assetId) const;
    std::array<f32, 3> AuthoredCubemapIrradianceColor(
        std::string_view assetId
    ) const;
    u32 AuthoredCubemapIrradianceReadyCount() const;
    bool AuthoredCubemapDiffuseLobesReady(std::string_view assetId) const;
    AuthoredReflectionProbeDiffuseLobes AuthoredCubemapDiffuseLobes(
        std::string_view assetId
    ) const;
    u32 AuthoredCubemapDiffuseLobesReadyCount() const;
    AuthoredReflectionCubemapSourceType AuthoredCubemapSourceType(
        std::string_view assetId
    ) const;

    void SetDescriptorSetsBound(u32 count);
    u32 DescriptorSetsBound() const;

private:
    struct AuthoredCubemapResource {
        std::unique_ptr<VulkanImage> image;
        AuthoredReflectionCubemapSourceType sourceType =
            AuthoredReflectionCubemapSourceType::Unknown;
        u64 assetSignature = 0;
        bool assetFound = false;
        bool loadFailed = false;
        bool hdr = false;
        bool prefiltered = false;
        u32 generatedMipCount = 0;
        u32 prefilterSampleCount = 0;
        AuthoredReflectionProbeFilterQuality filterQuality =
            AuthoredReflectionProbeFilterQuality::Medium;
        bool seamAwareFiltering = true;
        bool irradianceReady = false;
        std::array<f32, 3> irradianceColor{ 1.0f, 1.0f, 1.0f };
        bool diffuseLobesReady = false;
        AuthoredReflectionProbeDiffuseLobes diffuseLobes{};
    };

private:
    std::unique_ptr<VulkanImage> m_BuiltInCubemapImage;
    VkImageView m_BuiltInCubemapView = VK_NULL_HANDLE;
    std::unordered_map<std::string, AuthoredCubemapResource> m_AuthoredCubemaps;
    u32 m_AuthoredCubemapUploadCount = 0;
    u32 m_AuthoredCubemapEquirectangularConversionCount = 0;
    u32 m_AuthoredCubemapPrefilteredUploadCount = 0;
    u32 m_AuthoredCubemapCacheHitCount = 0;
    u32 m_AuthoredCubemapReloadCount = 0;
    u32 m_AuthoredCubemapRefreshCheckCount = 0;
    u32 m_DescriptorSetsBound = 0;
};

}
