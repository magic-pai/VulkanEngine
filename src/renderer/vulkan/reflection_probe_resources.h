#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <array>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

enum class CapturedReflectionProbeFilterQuality : u32 {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Ultra = 4
};

constexpr u32 CapturedReflectionProbeGgxSampleCount(
    CapturedReflectionProbeFilterQuality quality
) {
    switch (quality) {
    case CapturedReflectionProbeFilterQuality::Off:
        return 1u;
    case CapturedReflectionProbeFilterQuality::Low:
        return 16u;
    case CapturedReflectionProbeFilterQuality::High:
        return 128u;
    case CapturedReflectionProbeFilterQuality::Ultra:
        return 256u;
    case CapturedReflectionProbeFilterQuality::Medium:
        return 64u;
    }
    return 64u;
}

constexpr bool CapturedReflectionProbeGgxPrefilterEnabled(
    CapturedReflectionProbeFilterQuality quality
) {
    return quality != CapturedReflectionProbeFilterQuality::Off;
}

struct CapturedReflectionProbeFilteringSettings {
    CapturedReflectionProbeFilterQuality quality =
        CapturedReflectionProbeFilterQuality::High;
    // The capture face size is part of the producer contract. Thin fixture
    // geometry is otherwise removed by the roughness-selected mip chain.
    u32 faceSize = 512u;
};

struct AuthoredReflectionProbeFilteringSettings {
    AuthoredReflectionProbeFilterQuality quality =
        AuthoredReflectionProbeFilterQuality::Medium;
    bool seamAwareFiltering = true;
    u32 faceSize = 512u;
};

struct CapturedReflectionProbeLightSample {
    std::array<f32, 3> position{ 0.0f, 0.0f, 0.0f };
    std::array<f32, 3> direction{ 0.0f, -1.0f, 0.0f };
    std::array<f32, 3> color{ 1.0f, 1.0f, 1.0f };
    f32 intensity = 0.0f;
    f32 radius = 1.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
    u32 kind = 0;
    u32 identityMask = 0;
    u32 regionMask = 0;
};

struct CapturedReflectionProbeSceneSample {
    std::array<f32, 3> center{ 0.0f, 1.2f, 0.0f };
    std::array<f32, 3> boxExtents{ 5.5f, 5.5f, 5.5f };
    std::array<f32, 3> tint{ 1.0f, 1.0f, 1.0f };
    std::array<f32, 3> directionalDirection{ -0.45f, -0.82f, -0.35f };
    std::array<f32, 3> ambientColor{ 0.12f, 0.12f, 0.12f };
    f32 intensity = 1.0f;
    f32 ambientStrength = 0.12f;
    f32 directionalIntensity = 0.0f;
    u32 localLightSignature = 0;
    u32 affectedLocalLightCount = 0;
    u32 localLightIdentityMask = 0;
    u32 localLightRegionMask = 0;
    u32 signature = 0;
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
    AuthoredCubemapLoadFailed = 9,
    CapturedSceneResourceUnavailable = 10
};

// The current captured-scene implementation builds an analytic CPU cubemap.
// Keeping this explicit prevents callers from treating it as a rasterized scene capture.
enum class CapturedSceneCaptureBackend : u32 {
    None = 0,
    AnalyticCpu = 1,
    RasterizedGpu = 2
};

enum class CapturedSceneRefreshReason : u32 {
    None = 0,
    Initial = 1,
    Forced = 2,
    ForcedPolicy = 3,
    SceneDirtyOverride = 4,
    MembershipChanged = 5,
    LightChanged = 6,
    RenderChanged = 7,
    ContentChanged = 8
};

enum CapturedSceneDirtyFlag : u32 {
    CapturedSceneDirtyNone = 0u,
    CapturedSceneDirtyMembership = 1u << 0u,
    CapturedSceneDirtyLight = 1u << 1u,
    CapturedSceneDirtyRender = 1u << 2u,
    CapturedSceneDirtyContent = 1u << 3u,
    CapturedSceneDirtyExternal = 1u << 4u
};

struct CapturedSceneRefreshRequest {
    RendererReflectionProbeRefreshPolicy refreshPolicy =
        RendererReflectionProbeRefreshPolicy::SceneDirty;
    u64 membershipRevision = 0;
    u64 lightRevision = 0;
    u64 renderRevision = 0;
    u32 captureSignature = 0;
    u32 localLightSignature = 0;
    u32 geometrySignature = 0;
    u32 affectedLocalLightCount = 0;
    u32 affectedRenderableCount = 0;
    u32 localLightIdentityMask = 0;
    u32 geometryIdentityMask = 0;
    u32 localLightRegionMask = 0;
    u32 geometryRegionMask = 0;
    u32 refreshPriority = 0;
    u32 minimumRefreshIntervalFrames = 0;
    u64 schedulerFrame = 0;
    bool forceRefresh = false;
    bool sceneDirtyOverride = false;
    bool selectiveInvalidationEnabled = true;
};

struct CapturedSceneCaptureAudit {
    CapturedSceneCaptureBackend backend = CapturedSceneCaptureBackend::None;
    CapturedSceneRefreshReason refreshReason =
        CapturedSceneRefreshReason::None;
    CapturedSceneRefreshReason lastRefreshReason =
        CapturedSceneRefreshReason::None;
    u32 dirtyMask = CapturedSceneDirtyNone;
    u32 faceCount = 0;
    u32 captureSignature = 0;
    u32 radianceSignature = 0;
    u64 membershipRevision = 0;
    u64 lightRevision = 0;
    u64 renderRevision = 0;
    u64 schedulerFrame = 0;
    u64 lastRefreshCompletedFrame = 0;
    u32 localLightSignature = 0;
    u32 geometrySignature = 0;
    u32 affectedLocalLightCount = 0;
    u32 affectedRenderableCount = 0;
    u32 localLightIdentityMask = 0;
    u32 geometryIdentityMask = 0;
    u32 localLightRegionMask = 0;
    u32 geometryRegionMask = 0;
    u32 dirtyLocalLightCount = 0;
    u32 dirtyRenderableCount = 0;
    u32 refreshPriority = 0;
    u32 minimumRefreshIntervalFrames = 0;
    u32 refreshDeferredCount = 0;
    i32 probeSceneIndex = -1;
    u32 facesRendered = 0;
    u32 facesPending = 0;
    u32 capturePassCount = 0;
    u32 captureDrawCount = 0;
    u32 captureVisibleCount = 0;
    u32 captureCulledCount = 0;
    u32 selfCaptureExcludedCount = 0;
    u32 captureFaceOrientationMask = 0;
    u32 mipGenerationCount = 0;
    u32 sourceMipGenerationCount = 0;
    u32 sourceMipCount = 0;
    u64 sourceMipMemoryBytes = 0;
    u32 ggxPrefilterDispatchCount = 0;
    u32 ggxPrefilterSampleCount = 0;
    u32 ggxPrefilterQuality = 0;
    u32 diffuseIrradianceDispatchCount = 0;
    u32 diffuseIrradianceSampleCount = 0;
    u32 diffuseIrradianceFaceSize = 0;
    u32 directionalShadowPassCount = 0;
    u32 directionalShadowDrawCount = 0;
    u32 directionalShadowCasterCount = 0;
    u32 directionalShadowMapSize = 0;
    u32 directionalShadowFaceMask = 0;
    i32 directionalShadowProbeSceneIndex = -1;
    u32 localShadowPassCount = 0;
    u32 localShadowDrawCount = 0;
    u32 localShadowCasterCount = 0;
    u32 localShadowTileCount = 0;
    u32 localShadowPointFaceTileCount = 0;
    u32 localShadowSpotTileCount = 0;
    u32 localShadowRectTileCount = 0;
    u32 localShadowRequestedTileCount = 0;
    u32 localShadowDroppedTileCount = 0;
    u32 localShadowRectRequestedTileCount = 0;
    u32 localShadowRectMaximumTileCount = 0;
    u32 localShadowRectExtraSampleTileCount = 0;
    u32 localShadowRectBudgetLimitedSampleTileCount = 0;
    u32 localShadowRectDroppedTileCount = 0;
    u32 localShadowMapTileSize = 0;
    u32 localShadowFaceMask = 0;
    u32 localShadowSupportedKindMask = 0;
    u32 localShadowSuppressedKindMask = 0;
    i32 localShadowProbeSceneIndex = -1;
    u32 shadowSnapshotBuildCount = 0;
    u32 shadowSnapshotReuseFaceCount = 0;
    u32 shadowSnapshotSavedDirectionalPassCount = 0;
    u32 shadowSnapshotSavedLocalTilePassCount = 0;
    u32 shadowSnapshotSavedLocalDrawCount = 0;
    u32 shadowSnapshotBuildFaceMask = 0;
    u32 shadowSnapshotReuseFaceMask = 0;
    i32 shadowSnapshotProbeSceneIndex = -1;
    i32 shadowSnapshotPersistentCacheSlot = -1;
    u32 shadowSnapshotPersistentHitCount = 0;
    u32 shadowSnapshotPersistentCacheResourceCount = 0;
    u32 shadowSnapshotPersistentCacheEvictionCount = 0;
    u32 shadowSnapshotInputSignature = 0;
    u32 lastCapturedFace = 0;
    bool resourceReady = false;
    bool refreshRequested = false;
    bool refreshPerformed = false;
    bool rasterizedGeometry = false;
    bool gpuResourcesAllocated = false;
    bool gpuCaptureInProgress = false;
    bool captureFaceOrientationValid = false;
    bool mipChainReady = false;
    bool sourceMipChainReady = false;
    bool ggxPrefilterSourceImageSeparated = false;
    bool ggxPrefilterPdfLodEnabled = false;
    bool ggxPrefilterReady = false;
    bool ggxPrefilterFallbackActive = false;
    bool diffuseIrradianceReady = false;
    bool directionalShadowRequested = false;
    bool directionalShadowReady = false;
    bool directionalShadowCameraIndependent = false;
    bool directionalShadowLocalTilesSuppressed = false;
    bool localShadowRequested = false;
    bool localShadowReady = false;
    bool localShadowCameraIndependent = false;
    bool shadowSnapshotReady = false;
    bool shadowSnapshotCameraIndependent = false;
    bool shadowSnapshotEnabled = false;
    bool shadowSnapshotFallbackActive = false;
    bool shadowSnapshotPersistentEnabled = false;
    bool shadowSnapshotPersistentHit = false;
    bool selectiveInvalidationEnabled = false;
    bool localLightDirty = false;
    bool geometryDirty = false;
    bool localityIgnoredLightRevision = false;
    bool localityIgnoredGeometryRevision = false;
    bool refreshDeferredByBudget = false;
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
    void EnsureCapturedSceneCubemap(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        i32 probeSceneIndex,
        const CapturedReflectionProbeSceneSample& sceneSample,
        std::span<const CapturedReflectionProbeLightSample> lights,
        const CapturedSceneRefreshRequest& refreshRequest,
        AuthoredReflectionProbeFilteringSettings filteringSettings = {}
    );
    bool EnsureGpuCapturedSceneResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        i32 probeSceneIndex,
        CapturedReflectionProbeFilteringSettings filteringSettings = {}
    );
    bool RequestGpuCapturedSceneRefresh(
        const CapturedSceneRefreshRequest& refreshRequest,
        i32 probeSceneIndex
    );
    bool GpuCapturedSceneRefreshPending(i32 probeSceneIndex) const;
    u32 GpuCapturedSceneNextFace(i32 probeSceneIndex) const;
    VkRenderPass GpuCapturedSceneRenderPass() const;
    VkExtent2D GpuCapturedSceneExtent() const;
    VkFramebuffer GpuCapturedSceneFramebuffer(i32 probeSceneIndex, u32 face) const;
    VkExtent2D GpuCapturedSceneExtent(i32 probeSceneIndex) const;
    void RecordGpuCapturedSceneMipGeneration(
        i32 probeSceneIndex,
        VkCommandBuffer commandBuffer,
        CapturedReflectionProbeFilteringSettings filteringSettings
    );
    void RecordGpuCapturedSceneFaceOrientation(
        i32 probeSceneIndex,
        u32 face,
        bool valid
    );
    void RecordGpuCapturedSceneDiffuseIrradiance(
        i32 probeSceneIndex,
        VkCommandBuffer commandBuffer
    ) const;
    void RecordGpuCapturedSceneDirectionalShadow(
        i32 probeSceneIndex,
        u32 face,
        u32 mapSize,
        u32 passCount,
        u32 drawCount,
        u32 casterCount,
        bool requested,
        bool ready,
        bool cameraIndependent,
        bool localTilesSuppressed
    );
    void RecordGpuCapturedSceneLocalShadow(
        i32 probeSceneIndex,
        u32 face,
        u32 mapTileSize,
        u32 passCount,
        u32 drawCount,
        u32 casterCount,
        u32 tileCount,
        u32 pointFaceTileCount,
        u32 spotTileCount,
        u32 rectTileCount,
        u32 requestedTileCount,
        u32 droppedTileCount,
        u32 rectRequestedTileCount,
        u32 rectMaximumTileCount,
        u32 rectExtraSampleTileCount,
        u32 rectBudgetLimitedSampleTileCount,
        u32 rectDroppedTileCount,
        bool requested,
        bool ready,
        bool cameraIndependent,
        u32 supportedKindMask,
        u32 suppressedKindMask
    );
    void RecordGpuCapturedSceneShadowSnapshot(
        i32 probeSceneIndex,
        u32 face,
        bool built,
        u32 savedDirectionalPassCount,
        u32 savedLocalTilePassCount,
        u32 savedLocalDrawCount,
        bool ready,
        bool cameraIndependent,
        bool enabled,
        bool persistentEnabled,
        bool persistentHit,
        i32 persistentCacheSlot,
        u32 persistentCacheResourceCount,
        u32 persistentCacheEvictionCount,
        u32 inputSignature
    );
    void CompleteGpuCapturedSceneFace(
        i32 probeSceneIndex,
        u32 face,
        u32 drawCount,
        u32 visibleCount,
        u32 culledCount,
        u32 selfCaptureExcludedCount,
        bool captureComplete,
        u64 schedulerFrame
    );
    void FailGpuCapturedSceneRefresh(i32 probeSceneIndex);
    void Release();

    bool BuiltInProceduralReady(VkSampler sampler) const;
    bool CapturedSceneReady(i32 probeSceneIndex, VkSampler sampler) const;
    bool AuthoredCubemapReady(std::string_view assetId, VkSampler sampler) const;
    bool AuthoredCubemapAssetFound(std::string_view assetId) const;
    bool AuthoredCubemapLoadFailed(std::string_view assetId) const;
    VkImageView DescriptorViewFor(VkImageView fallbackView, VkSampler sampler) const;
    VkImageView AuthoredDescriptorViewFor(
        std::string_view assetId,
        VkImageView fallbackView,
        VkSampler sampler
    ) const;
    VkImageView CapturedSceneDescriptorViewFor(
        i32 probeSceneIndex,
        VkImageView fallbackView,
        VkSampler sampler
    ) const;
    bool CapturedSceneDescriptorMatchesProbe(
        i32 probeSceneIndex,
        VkImageView descriptorView,
        VkSampler sampler
    ) const;
    bool CapturedSceneDiffuseIrradianceReady(
        i32 probeSceneIndex,
        VkSampler sampler
    ) const;
    VkImageView CapturedSceneDiffuseIrradianceDescriptorViewFor(
        i32 probeSceneIndex,
        VkImageView fallbackView,
        VkSampler sampler
    ) const;
    bool CapturedSceneDiffuseIrradianceDescriptorMatchesProbe(
        i32 probeSceneIndex,
        VkImageView descriptorView,
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
    u32 CapturedSceneFaceSize(i32 probeSceneIndex) const;
    u32 CapturedSceneMipCount(i32 probeSceneIndex) const;
    VkFormat CapturedSceneFormat(i32 probeSceneIndex) const;
    u32 CapturedSceneUploadCount() const;
    u32 CapturedSceneRefreshCheckCount() const;
    u32 CapturedSceneLocalityIgnoredLightRevisionCount() const;
    u32 CapturedSceneLocalityIgnoredGeometryRevisionCount() const;
    u32 CapturedSceneDirtyLocalLightProbeCount() const;
    u32 CapturedSceneDirtyGeometryProbeCount() const;
    u32 CapturedSceneSignature() const;
    const CapturedSceneCaptureAudit& CapturedSceneAudit() const;
    const CapturedSceneCaptureAudit& CapturedSceneAudit(i32 probeSceneIndex) const;
    u32 CapturedSceneProbeResourceCount() const;
    u32 CapturedSceneReadyProbeCount(VkSampler sampler) const;
    u32 CapturedSceneInFlightProbeCount() const;
    u32 CapturedSceneDistinctActiveViewCount(VkSampler sampler) const;
    u32 CapturedSceneDiffuseIrradianceReadyProbeCount(VkSampler sampler) const;
    u32 CapturedSceneDistinctActiveDiffuseIrradianceViewCount(
        VkSampler sampler
    ) const;

    void SetDescriptorSetsBound(u32 count);
    u32 DescriptorSetsBound() const;

private:
    struct CapturedSceneProbeResource {
        std::unique_ptr<VulkanImage> activeImage;
        std::unique_ptr<VulkanImage> targetImage;
        std::unique_ptr<VulkanImage> sourceRadianceImage;
        std::unique_ptr<VulkanImage> activeDiffuseIrradianceImage;
        std::unique_ptr<VulkanImage> targetDiffuseIrradianceImage;
        std::unique_ptr<VulkanImage> depthImage;
        std::vector<VkImageView> faceViews;
        std::vector<VkFramebuffer> framebuffers;
        VkImageView prefilterSourceView = VK_NULL_HANDLE;
        std::vector<VkImageView> prefilterMipViews;
        std::vector<VkDescriptorSet> prefilterDescriptorSets;
        VkImageView diffuseIrradianceArrayView = VK_NULL_HANDLE;
        VkDescriptorSet diffuseIrradianceDescriptorSet = VK_NULL_HANDLE;
        CapturedSceneRefreshRequest refreshRequest{};
        CapturedReflectionProbeFilteringSettings filteringSettings{};
        CapturedSceneCaptureBackend activeBackend = CapturedSceneCaptureBackend::None;
        u32 signature = 0;
        u32 radianceSignature = 0;
        u64 membershipRevision = 0;
        u64 lightRevision = 0;
        u64 renderRevision = 0;
        u64 lastRefreshCompletedFrame = 0;
        u32 localLightSignature = 0;
        u32 geometrySignature = 0;
        u32 localLightIdentityMask = 0;
        u32 geometryIdentityMask = 0;
        u32 localLightRegionMask = 0;
        u32 geometryRegionMask = 0;
        u32 refreshDeferredCount = 0;
        u32 uploadCount = 0;
        u32 refreshCheckCount = 0;
        CapturedSceneRefreshReason lastRefreshReason =
            CapturedSceneRefreshReason::None;
        CapturedSceneCaptureAudit audit{};
        bool captureInProgress = false;
        u32 nextFace = 0;
        u32 facesRendered = 0;
    };

    CapturedSceneProbeResource* FindCapturedSceneProbeResource(i32 probeSceneIndex);
    const CapturedSceneProbeResource* FindCapturedSceneProbeResource(
        i32 probeSceneIndex
    ) const;
    CapturedSceneProbeResource* FindOrCreateCapturedSceneProbeResource(
        i32 probeSceneIndex
    );
    void ReleaseGpuCapturedSceneAttachments(CapturedSceneProbeResource& resource);
    void ReleaseGpuCapturedSceneResources(CapturedSceneProbeResource& resource);
    bool EnsureGpuCapturedScenePrefilterResources(const VulkanDevice& device);
    void ReleaseGpuCapturedScenePrefilterResources();
    bool EnsureGpuCapturedSceneDiffuseIrradianceResources(
        const VulkanDevice& device
    );
    void ReleaseGpuCapturedSceneDiffuseIrradianceResources();
    void BeginGpuCapturedSceneRefresh(
        CapturedSceneProbeResource& resource,
        const CapturedSceneRefreshRequest& refreshRequest
    );
    bool CapturedSceneRefreshRequested(
        const CapturedSceneProbeResource& resource,
        const CapturedSceneRefreshRequest& refreshRequest,
        CapturedSceneCaptureAudit& audit
    ) const;

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
    VkDevice m_GpuCapturedSceneDevice = VK_NULL_HANDLE;
    VkRenderPass m_GpuCapturedSceneRenderPass = VK_NULL_HANDLE;
    VkSampler m_GpuCapturedScenePrefilterSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_GpuCapturedScenePrefilterDescriptorSetLayout =
        VK_NULL_HANDLE;
    VkDescriptorPool m_GpuCapturedScenePrefilterDescriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout m_GpuCapturedScenePrefilterPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_GpuCapturedScenePrefilterPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_GpuCapturedSceneDiffuseIrradianceDescriptorSetLayout =
        VK_NULL_HANDLE;
    VkDescriptorPool m_GpuCapturedSceneDiffuseIrradianceDescriptorPool =
        VK_NULL_HANDLE;
    VkPipelineLayout m_GpuCapturedSceneDiffuseIrradiancePipelineLayout =
        VK_NULL_HANDLE;
    VkPipeline m_GpuCapturedSceneDiffuseIrradiancePipeline = VK_NULL_HANDLE;
    std::unordered_map<i32, CapturedSceneProbeResource> m_CapturedSceneProbeResources;
    i32 m_LastCapturedSceneProbeSceneIndex = -1;
    CapturedSceneCaptureAudit m_EmptyCapturedSceneAudit{};
    std::unordered_map<std::string, AuthoredCubemapResource> m_AuthoredCubemaps;
    u32 m_AuthoredCubemapUploadCount = 0;
    u32 m_AuthoredCubemapEquirectangularConversionCount = 0;
    u32 m_AuthoredCubemapPrefilteredUploadCount = 0;
    u32 m_AuthoredCubemapCacheHitCount = 0;
    u32 m_AuthoredCubemapReloadCount = 0;
    u32 m_AuthoredCubemapRefreshCheckCount = 0;
    u32 m_DescriptorSetsBound = 0;
    u32 m_CapturedSceneCubemapFaceSize = 512u;
    bool m_CapturedSceneCubemapFaceSizeInitialized = false;
};

}
