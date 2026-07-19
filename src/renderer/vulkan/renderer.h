#pragma once

#include "renderer/vulkan/vulkan_common.h"
#include "renderer/vulkan/ibl_generator.h"
#include "renderer/vulkan/pipeline_spec.h"
#include "renderer/vulkan/reflection_probe_resources.h"
#include "renderer/vulkan/render_debug_settings.h"
#include "renderer/vulkan/render_feature_registry.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/shadow_settings.h"
#include "renderer/vulkan/uniform_buffer.h"
#include "renderer/vulkan/vertex.h"
#include "renderer/render_queue.h"
#include "renderer/temporal_upscaler.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <array>
#include <functional>
#include <optional>
#include <string>

namespace se {

class VulkanCommandBuffer;
class VulkanCommandPool;
class VulkanComputePipeline;
class VulkanImage;
class VulkanDepthBuffer;
class VulkanDescriptorSetLayout;
class VulkanFfxSssrClassifyTilesDescriptorSetLayout;
class VulkanFfxSssrClassifyTilesResources;
class VulkanFfxSssrConstantsDescriptorSetLayout;
class VulkanFfxSssrConstantsResources;
class VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout;
class VulkanFfxSssrPrepareIndirectArgsResources;
class VulkanDescriptorSets;
class VulkanHiZDescriptorSetLayout;
class VulkanSsrReconstructionDescriptorSetLayout;
class VulkanHiZDescriptorSets;
class VulkanSsrReconstructionDescriptorSets;
class VulkanDevice;
class VulkanFramebuffer;
class VulkanForwardResidualVelocityFramebuffer;
class VulkanForwardResidualVelocityRenderPass;
class VulkanDlssMaskFramebuffer;
class VulkanDlssMaskRenderPass;
class VulkanGpuTimer;
class VulkanGBufferDescriptorSets;
class VulkanGraphicsPipeline;
class VulkanHdrDescriptorSets;
class VulkanWeightedTranslucencyDescriptorSets;
class VulkanImGuiLayer;
class VulkanInstanceBuffer;
class VulkanLocalShadowAtlas;
class VulkanMaterialDescriptorSetLayout;
class VulkanMaterialDescriptorSets;
class VulkanPhysicalDevice;
class VulkanRenderPass;
class VulkanRenderResources2D;
class VulkanSampler;
class VulkanGBufferFramebuffer;
class VulkanGBufferRenderPass;
class VulkanHdrFramebuffer;
class VulkanHdrRenderPass;
class VulkanSceneRenderTargets;
class VulkanDepthPyramid;
class VulkanWeightedTranslucencyFramebuffer;
class VulkanWeightedTranslucencyRenderPass;
class VulkanShadowFramebuffer;
class VulkanDirectionalShadowCascadeAtlas;
class VulkanShadowMap;
class VulkanShadowRenderPass;
class VulkanSurface;
class VulkanSwapchain;
class VulkanSyncObjects;
class VulkanAutoExposureBuffer;
class VulkanBloomDescriptorSets;
class VulkanBloomFramebuffer;
class VulkanBloomPyramid;
class VulkanBloomRenderPass;
class VulkanBonePaletteFallbackDescriptorSet;
class VulkanColorGradingLut;
class VulkanLightBuffer;
class VulkanLightTileDiagnosticsBuffer;
class VulkanMaterialBuffer;
class VulkanDirectionalShadowCascadeBuffer;
class VulkanLocalShadowBuffer;
class VulkanTexture2D;
class VulkanUniformBuffer;
class Camera2D;
class Camera3D;
class Scene2D;
class Scene3D;
class Window;
struct FrameMaterialSet;

struct FrameMatrices {
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
};

struct FrameTemporalState {
    FrameMatrices previousMatrices{};
    glm::vec2 jitterPixels{ 0.0f };
    glm::vec2 jitterUv{ 0.0f };
    glm::vec2 previousJitterPixels{ 0.0f };
    glm::vec2 previousJitterUv{ 0.0f };
    u32 jitterSequenceIndex = 0;
    RendererTemporalHistoryResetReason resetReason =
        RendererTemporalHistoryResetReason::FirstFrame;
    bool historyValid = false;
    bool historyReset = true;
    bool jitterEnabled = false;
    bool jitterApplied = false;
    bool velocityJitteredHistoryPolicy = false;
    bool velocityPreviousJitterApplied = false;
    bool velocityCameraMotionReady = false;
    bool velocityObjectMotionReady = false;
    bool velocityMaterialAuxMigrated = false;
    bool temporalUpscaleInputReady = false;
    bool taaResolveConfigured = false;
    bool taaResolveEnabled = false;
    bool taaResolveSuppressedForUpscaler = false;
    bool taaHistoryColorTargetAllocated = false;
    bool taaHistoryColorReady = false;
    bool taaVelocityReprojectionEnabled = false;
    f32 taaHistoryWeight = 0.0f;
    bool taaRejectionEnabled = false;
    bool taaNeighborhoodClampEnabled = false;
    f32 taaVelocityRejectionThreshold = 0.0f;
    f32 taaDepthRejectionThreshold = 0.0f;
    RendererTaaFallbackReason taaFallbackReason =
        RendererTaaFallbackReason::Disabled;
};

struct FrameTemporalUpscaleState {
    VkExtent2D displayExtent{};
    VkExtent2D requestedInternalExtent{};
    VkExtent2D activeInternalExtent{};
    f32 requestedRenderScale = 1.0f;
    f32 activeRenderScale = 1.0f;
    u32 inputReadinessMask = 0;
    u32 requiredInputMask = 0;
    bool renderScaleApplied = false;
    bool dynamicResolutionRequested = false;
    bool dynamicResolutionEnabled = false;
    bool taauRequested = false;
    bool temporalUpscaleRequested = false;
    bool temporalUpscaleEnabled = false;
    bool temporalUpscaleContractReady = false;
    bool temporalUpscalePostSourceRequested = false;
    bool upscalerPluginRequested = false;
    bool upscalerPluginAvailable = false;
    bool dlssQualityGateRequested = false;
    bool dlssQualityGateReady = false;
    bool dlssQualityEvaluateOutputReady = false;
    bool dlssQualityCameraMotionReady = false;
    bool dlssQualityObjectMotionReady = false;
    bool dlssQualitySceneContentMotionSupported = false;
    bool dlssQualityReactiveMaskReady = false;
    bool dlssQualityTransparencyMaskReady = false;
    bool dlssQualityExposurePolicyReady = false;
    bool dlssQualityPostOrderingReady = false;
    bool dlssQualityReferenceBaselineReady = false;
    u32 dlssQualityRequiredMask = 0;
    u32 dlssQualityReadyMask = 0;
    u32 dlssQualityBlockerMask = 0;
    RendererDlssQualityGateFallbackReason dlssQualityGateFallbackReason =
        RendererDlssQualityGateFallbackReason::NotRequested;
    TemporalUpscalerDlssQualityMode dlssQualityMode =
        TemporalUpscalerDlssQualityMode::Quality;
    TemporalUpscalerDlssPreset dlssPreset =
        TemporalUpscalerDlssPreset::Default;
    TemporalUpscalerPackageStatus upscalerPackage{};
    TemporalUpscalerRuntimeStatus upscalerRuntime{};
    RendererTemporalUpscaleFallbackReason fallbackReason =
        RendererTemporalUpscaleFallbackReason::Disabled;
};

struct FrameLightConstants {
    glm::vec4 directionalLight{ -0.45f, -0.82f, -0.35f, 0.78f };
    glm::vec4 ambientLight{ 0.22f, 0.24f, 0.0f, 0.0f };
};

enum class RendererLightKind : u32 {
    Directional = 0,
    Point = 1,
    Spot = 2,
    Rect = 3
};

struct RendererDirectionalLight {
    RendererLightKind kind = RendererLightKind::Directional;
    glm::vec3 direction{ -0.45f, -0.82f, -0.35f };
    f32 intensity = 0.78f;
    f32 ambient = 0.22f;
    f32 specular = 0.24f;
    f32 angularRadiusRadians = 0.00464258f;
};

struct RendererLocalLight {
    RendererLightKind kind = RendererLightKind::Point;
    glm::vec3 position{ 0.0f };
    f32 radius = 1.0f;
    glm::vec3 color{ 1.0f };
    f32 intensity = 1.0f;
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    f32 innerConeCos = 1.0f;
    f32 outerConeCos = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
    f32 sourceRadius = 0.05f;
};

inline constexpr std::size_t kRendererMaxFrameLocalLights = 64;

struct FrameLightSet {
    RendererDirectionalLight primaryDirectional{};
    std::array<RendererLocalLight, kRendererMaxFrameLocalLights> localLights{};
    u32 directionalCount = 1;
    u32 localCount = 0;
    u32 rectCount = 0;

    FrameLightConstants Constants() const;
};

enum class RendererTemporalAntialiasingMode : u32 {
    Environment = 0,
    NativeTaa = 1,
    DlssDlaa = 2,
    DlssSrQuality = 3,
    DlssSrBalanced = 4,
    DlssSrPerformance = 5,
    Off = 6
};

struct RendererReflectionProbe {
    glm::vec3 center{ 0.0f, 1.2f, 0.0f };
    f32 radius = 5.5f;
    glm::vec3 boxExtents{ 5.5f };
    glm::vec3 color{ 1.0f, 0.82f, 0.62f };
    f32 intensity = 1.25f;
    f32 blendStrength = 0.65f;
    f32 falloff = 2.0f;
    bool enabled = false;
    bool sceneOwned = false;
    i32 sceneIndex = -1;
    RendererReflectionProbeCaptureSource captureSource =
        RendererReflectionProbeCaptureSource::None;
    std::string captureAssetId;
    RendererReflectionProbeRefreshPolicy refreshPolicy =
        RendererReflectionProbeRefreshPolicy::Static;
};

struct FrameReflectionProbeSet {
    RendererReflectionProbe localProbe{};
    std::array<RendererReflectionProbe, kMaxFrameReflectionProbes> selectedProbes{};
    std::array<i32, kMaxFrameReflectionProbes> selectedCaptureSlots{};
    std::array<bool, kMaxFrameReflectionProbes> selectedCaptureResourceReady{};
    std::array<bool, kMaxFrameReflectionProbes> selectedCaptureDescriptorBound{};
    std::array<RendererReflectionProbeCaptureFallbackReason, kMaxFrameReflectionProbes>
        selectedCaptureFallbackReasons{};
    std::array<RendererReflectionProbeRefreshPolicy, kMaxFrameReflectionProbes>
        selectedRefreshPolicies{};
    std::array<bool, kMaxFrameReflectionProbes> selectedCapturedScenePlaceholderReady{};
    std::array<bool, kMaxFrameReflectionProbes> selectedCapturedSceneInvalidated{};
    std::array<bool, kMaxFrameReflectionProbes>
        selectedCapturedSceneDiffuseIrradianceReady{};
    std::array<u32, kMaxFrameReflectionProbes> selectedCaptureMipCounts{};
    std::array<u32, kMaxFrameReflectionProbes> selectedAuthoredAssetHashes{};
    std::array<bool, kMaxFrameReflectionProbes> selectedAuthoredAssetSpecified{};
    std::array<bool, kMaxFrameReflectionProbes> selectedAuthoredAssetFound{};
    std::array<
        std::array<glm::vec4, kReflectionProbeDiffuseLobeCount>,
        kMaxFrameReflectionProbes
    > selectedDiffuseIrradianceLobes{};
    std::array<bool, kMaxFrameReflectionProbes> selectedDiffuseIrradianceLobesReady{};
    std::array<f32, kMaxFrameReflectionProbes> selectedBlendWeights{};
    std::array<f32, kMaxFrameReflectionProbes> selectedNormalizedBlendWeights{};
    u32 sceneProbeCount = 0;
    u32 activeLocalProbeCount = 0;
    u32 eligibleSceneProbeCount = 0;
    u32 selectedProbeCount = 0;
    u32 blendedProbeCount = 0;
    u32 selectedCaptureSlotCount = 0;
    u32 selectedCaptureResourceReadyCount = 0;
    u32 selectedCaptureFallbackCount = 0;
    u32 selectedCubemapSamplingCount = 0;
    u32 selectedCaptureReadyMask = 0;
    u32 selectedCaptureFallbackMask = 0;
    u32 selectedCubemapSamplingMask = 0;
    u32 selectedAuthoredAssetSpecifiedCount = 0;
    u32 selectedAuthoredAssetFoundCount = 0;
    u32 selectedAuthoredAssetMissingCount = 0;
    u32 selectedAuthoredAssetSpecifiedMask = 0;
    u32 selectedAuthoredAssetFoundMask = 0;
    u32 selectedAuthoredAssetMissingMask = 0;
    u32 selectedDiffuseIrradianceLobesReadyCount = 0;
    u32 selectedDiffuseIrradianceLobesReadyMask = 0;
    u32 selectedCapturedSceneDiffuseIrradianceReadyCount = 0;
    u32 selectedCapturedSceneDiffuseIrradianceReadyMask = 0;
    u32 selectedDiffuseIrradianceLobeCount = 0;
    u32 selectedProbeMask = 0;
    u32 selectedBoxProjectionMask = 0;
    u32 selectedCapturedSceneBoxProjectionMask = 0;
    u32 selectedBoxProjectionRayHitMask = 0;
    u32 selectedBoxProjectionDirectionChangedMask = 0;
    u32 selectedBoxProjectionOutsideFallbackMask = 0;
    u32 selectedSceneOwnedMask = 0;
    u32 selectedPositiveInfluenceMask = 0;
    u32 selectedProbeDuplicateIndexMask = 0;
    u32 selectedCaptureMipReadyMask = 0;
    u32 spatialContractFailureMask = 0;
    u32 blendWeightNormalizationFallbackCount = 0;
    u32 capturedSceneRequestedCount = 0;
    u32 capturedScenePlaceholderAllocatedCount = 0;
    u32 capturedScenePlaceholderReadyCount = 0;
    u32 capturedSceneInvalidatedCount = 0;
    u32 capturedSceneRefreshRequestedCount = 0;
    u32 droppedSceneProbeCount = 0;
    i32 selectedSceneProbeIndex = -1;
    f32 maxBlendWeight = 0.0f;
    f32 totalBlendWeight = 0.0f;
    f32 normalizedBlendWeightSum = 0.0f;
    f32 normalizedBlendWeightError = 0.0f;
    bool fallbackEnabled = false;
    bool boxProjectionEnabled = false;
    bool parallaxCorrectionEnabled = false;
    bool multiBlendEnabled = false;
    bool spatialContractValid = false;
    u32 influenceMode = 0;
    bool captureResourceReady = false;
    bool captureDescriptorBound = false;
    RendererReflectionProbeCaptureSource captureSource =
        RendererReflectionProbeCaptureSource::None;
    RendererReflectionProbeRefreshPolicy refreshPolicy =
        RendererReflectionProbeRefreshPolicy::Static;
    bool forcedRefreshRequested = false;
    bool sceneDirtyRequested = false;
    RendererReflectionProbeCaptureFallbackReason captureFallbackReason =
        RendererReflectionProbeCaptureFallbackReason::NoActiveSceneProbe;
};

struct FrameLightTileStats {
    u32 tileSize = 0;
    u32 tileCountX = 0;
    u32 tileCountY = 0;
    u32 tileCount = 0;
    u32 assignments = 0;
    u32 assignmentCapacity = 0;
    u32 overflowAssignments = 0;
    u32 overflowCapacity = 0;
    u32 overflowTileCount = 0;
    u32 overflowDropped = 0;
    u32 fallbackCount = 0;
};

struct FrameLightTileGpuReadbackStats {
    bool valid = false;
    u32 saturatedTileCount = 0;
    u32 maxRawCandidateCount = 0;
    u64 rawCandidateCountSum = 0;
    u32 overflowUsedTileCount = 0;
    u32 overflowDroppedTileCount = 0;
    u32 overflowStoredCount = 0;
    u32 overflowDroppedCount = 0;
};

struct FrameSsrGpuDiagnosticsStats {
    bool valid = false;
    u32 pixelCount = 0;
    u32 rawHitPixels = 0;
    u32 rawHighConfidencePixels = 0;
    u32 temporalValidPixels = 0;
    u32 resolvedValidPixels = 0;
    u32 isolatedRawHitPixels = 0;
    u32 centerMissNeighborHitPixels = 0;
    u32 resolvedHolePixels = 0;
    u32 rawHitTemporalRejectedPixels = 0;
    u32 rawHitSpatialRejectedPixels = 0;
    u32 temporalMissCarriedPixels = 0;
    u32 fallbackBlendResolvedPixels = 0;
    u32 fallbackBlendPartialPixels = 0;
    u32 fallbackBlendHighTrustPixels = 0;
    u32 fallbackBlendWeightSum64 = 0;
    u32 contractVersion = 0;
};

struct FrameFfxSssrGpuReadbackStats {
    bool valid = false;
    u32 pendingRayCount = 0;
    u32 preparedRayCount = 0;
    u32 pendingDenoiserTileCount = 0;
    u32 preparedDenoiserTileCount = 0;
};

struct FrameAutoExposureReadbackStats {
    bool valid = false;
    f32 exposure = 1.0f;
    f32 targetExposure = 1.0f;
    f32 averageLuminance = 1.0f;
};

struct DirectionalShadowCascade {
    glm::mat4 viewProjection{ 1.0f };
    f32 nearDepth = 0.0f;
    f32 farDepth = 0.0f;
    f32 splitDepth = 0.0f;
    f32 texelWorldSize = 0.0f;
    f32 lightDepthWorldSpan = 0.0f;
};

struct DirectionalShadowCascadeSet {
    std::array<DirectionalShadowCascade, kMaxDirectionalShadowCascades> cascades{};
    u32 configuredCount = 0;
    u32 activeCount = 0;
    bool stableSnappingEnabled = false;
    // Capture-side shadows use a full standalone depth map instead of the main
    // camera's 2x2 CSM atlas.
    bool singleMapSampling = false;
    f32 splitLambda = 0.0f;
    f32 maxDistance = 0.0f;
    f32 nearDepth = 0.0f;
    f32 farDepth = 0.0f;
    f32 lightAngularRadiusRadians = 0.00464258f;
};

inline constexpr std::size_t kMaxLocalShadowTiles = 64;

enum class LocalShadowCacheDecision : u32 {
    Cold = 0,
    Hit = 1,
    TileLayoutChanged = 2,
    LightChanged = 3,
    CasterChanged = 4,
    DynamicSkinnedCaster = 5
};

struct LocalShadowTile {
    glm::mat4 viewProjection{ 1.0f };
    u32 tileIndex = 0;
    u32 localLightIndex = 0;
    u32 faceIndex = 0;
    u32 lightKind = 0;
    glm::vec4 filterGeometry{ 0.05f, 1.0f, 0.0f, 1.0f };
    u64 cacheKey = 0;
    u64 cacheTileIdentity = 0;
    u64 cacheLightSignature = 0;
    u64 cacheCasterSignature = 0;
    LocalShadowCacheDecision cacheDecision = LocalShadowCacheDecision::Cold;
    bool cacheReusable = false;
};

struct LocalShadowTileSet {
    std::array<LocalShadowTile, kMaxLocalShadowTiles> tiles{};
    std::array<u32, kRendererMaxFrameLocalLights> requestedTilesByLocalLight{};
    std::array<u32, kRendererMaxFrameLocalLights> assignedTilesByLocalLight{};
    std::array<u32, kRendererMaxFrameLocalLights> firstAssignedTileByLocalLight{};
    u32 tileSize = 0;
    u32 tileColumns = 0;
    u32 tileRows = 0;
    u32 tileCapacity = 0;
    u32 requestedCount = 0;
    u32 assignedCount = 0;
    u32 droppedCount = 0;
    u32 pointLightCount = 0;
    u32 spotLightCount = 0;
    u32 rectLightCount = 0;
    u32 pointFaceTiles = 0;
    u32 spotTiles = 0;
    u32 rectTiles = 0;
    u32 rectShadowBaseSampleTiles = 0;
    u32 rectShadowMaxSampleTiles = 0;
    u32 rectShadowSamplePattern = 0;
    u32 rectShadowExtraSampleTiles = 0;
    u32 rectShadowBudgetLimitedSampleTiles = 0;
    u32 cacheEligibleTiles = 0;
    u32 cacheHitTiles = 0;
    u32 cacheMissTiles = 0;
    u32 cacheSkippedTiles = 0;
    u32 cacheColdTiles = 0;
    u32 cacheTileLayoutChangedTiles = 0;
    u32 cacheLightChangedTiles = 0;
    u32 cacheCasterChangedTiles = 0;
    u32 cacheDynamicSkinnedCasterTiles = 0;
    std::string cacheReasonSummary;
};

struct LocalShadowCacheEntry {
    u64 tileIdentity = 0;
    u64 lightSignature = 0;
    u64 casterSignature = 0;
};

struct LocalShadowCacheState {
    std::array<LocalShadowCacheEntry, kMaxLocalShadowTiles> tiles{};
    u32 tileCount = 0;
    bool valid = false;
};

struct RenderQueueContext {
    const Frustum* frustum = nullptr;
    RenderQueueCullingStats* cullingStats = nullptr;
    RenderQueueCacheStats* cacheStats = nullptr;
    RenderQueue* shadowRenderQueue = nullptr;
    RenderQueueCullingStats* shadowCullingStats = nullptr;
};

class VulkanRenderer {
public:
    using FrameMatricesProvider = std::function<FrameMatrices(f32 aspectRatio)>;
    using RenderQueueBuilder = std::function<void(
        RenderQueue& renderQueue,
        const RenderQueueContext& context
    )>;

    VulkanRenderer(
        Window& window,
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanSurface& surface,
        VkInstance instance,
        const VulkanCommandPool& commandPool,
        Scene2D* scene,
        Camera2D* camera,
        const VulkanRenderResources2D& renderResources,
        PipelineSpec pipelineSpec
    );

    ~VulkanRenderer();

    SE_DISABLE_COPY(VulkanRenderer);
    SE_DISABLE_MOVE(VulkanRenderer);

    void DrawFrame();
    void WaitIdle() const;
    void SetFrameMatricesProvider(FrameMatricesProvider provider);
    void SetRenderQueueBuilder(RenderQueueBuilder builder);
    void SetDlssQualitySceneContentMotionSupported(bool supported);
    void SetTemporalAntialiasingMode(RendererTemporalAntialiasingMode mode);
    void ToggleTemporalAntialiasingMode();
    RendererTemporalAntialiasingMode TemporalAntialiasingMode() const;
    void SetImGui3DContext(Scene3D* scene, Camera3D* camera);
    void SetOverlay3DContext(Scene3D* scene, Camera3D* camera, PipelineSpec pipelineSpec);
    void RefreshMaterialDescriptors();
    VulkanRenderDebugSettings& RenderDebugSettings();
    const VulkanRenderDebugSettings& RenderDebugSettings() const;
    const RendererStats& Stats() const;
    VulkanShadowSettings& ShadowSettings();
    const VulkanShadowSettings& ShadowSettings() const;
    void ApplyEnvironmentRenderSettings();

private:
    struct ReflectionCaptureShadowSnapshot;

    void ValidateSceneResources() const;
    void CreateSwapchainResources();
    void RecreateSwapchain();
    void ApplyShadowMapSettings();
    void ResetLocalShadowCacheStates();
    void HandleObjectPicking();
    FrameTemporalState BuildFrameTemporalState(
        const FrameMatrices* matrices,
        const VkExtent2D& extent,
        bool velocityTargetAllocated,
        bool materialAuxTargetAllocated,
        bool hdrCompositeAvailable,
        bool historyColorTargetAllocated,
        bool historyColorReady,
        bool taaResolveConfigured,
        f32 taaHistoryWeight,
        bool taaRejectionEnabled,
        bool taaNeighborhoodClampEnabled,
        f32 taaVelocityRejectionThreshold,
        f32 taaDepthRejectionThreshold,
        bool temporalJitterApplyRequested,
        bool suppressNativeTaaResolveForUpscaler
    ) const;
    FrameTemporalUpscaleState BuildFrameTemporalUpscaleState(
        const VkExtent2D& displayExtent,
        const VkExtent2D& activeInternalExtent,
        bool hdrSceneColorReady,
        bool sceneDepthReady,
        const FrameTemporalState& temporalState,
        bool temporalReconstructionAllowed
    ) const;
    void StoreTemporalHistory(
        const FrameMatrices* matrices,
        const VkExtent2D& extent,
        const FrameTemporalState* temporalState
    );
    void PopulateTemporalUniforms(
        UniformBufferObject& uniformData,
        const FrameTemporalState* temporalState
    ) const;
    void WriteTemporalStats(
        const FrameTemporalState& temporalState,
        const FrameTemporalUpscaleState& temporalUpscaleState,
        bool velocityTargetAllocated,
        VkFormat velocityFormat,
        bool materialAuxTargetAllocated,
        VkFormat materialAuxFormat,
        bool historyColorTargetAllocated,
        VkFormat historyColorFormat,
        bool temporalUpscaleOutputAllocated,
        VkFormat temporalUpscaleOutputFormat,
        VkExtent2D temporalUpscaleOutputExtent,
        u32 historyColorCopyCount,
        RendererTemporalStats& stats
    ) const;
    bool LocalReflectionProbeCubemapReady() const;
    bool TemporalDlssModeActive() const;
    bool TemporalDlssDlaaModeActive() const;
    bool TemporalDlssSrModeActive() const;
    bool TemporalNativeTaaModeActive() const;
    f32 TemporalRenderScaleForCurrentMode() const;
    bool TemporalRenderScaleApplyEnabledForCurrentMode() const;
    VkExtent2D ActiveInternalExtentForDisplay(const VkExtent2D& displayExtent) const;
    TemporalUpscalerDlssQualityMode TemporalDlssQualityModeForCurrentMode() const;
    TemporalUpscalerDlssPreset TemporalDlssPresetForCurrentMode() const;
    bool TemporalJitterEnabledForCurrentMode() const;
    bool TemporalJitterApplyEnabledForCurrentMode() const;
    bool TemporalVelocityJitteredHistoryPolicyForCurrentMode() const;
    f32 ActiveMaterialTextureMipLodBias() const;
    bool SsrHiZResourcesReady() const;
    bool SsrReconstructionResourcesReady() const;
    void EnsureVisibleSkyboxResources();
    u32 UpdateEnvironmentDescriptorSets(
        VulkanDescriptorSets* descriptorSets,
        const FrameReflectionProbeSet* reflectionProbes = nullptr,
        std::optional<std::size_t> descriptorSetIndex = std::nullopt
    ) const;
    glm::vec2 CursorToWorldPosition(const VkExtent2D& extent) const;
    void UpdateUniformBuffer(
        std::size_t imageIndex,
        const FrameMatrices* matrices,
        const glm::mat4& lightViewProjection,
        const FrameLightConstants& lights,
        const FrameReflectionProbeSet& reflectionProbes,
        bool shadowSamplingEnabled,
        const FrameTemporalState* temporalState
    ) const;
    void UpdateFfxSssrConstants(
        std::size_t imageIndex,
        const FrameMatrices* matrices,
        const FrameTemporalState* temporalState
    ) const;
    void UpdateOverlayUniformBuffer(
        std::size_t imageIndex,
        const FrameMatrices* matrices,
        const glm::mat4& lightViewProjection,
        const FrameLightConstants& lights,
        const FrameReflectionProbeSet& reflectionProbes,
        bool shadowSamplingEnabled
    ) const;
    void UpdateLightBuffer(
        std::size_t imageIndex,
        const FrameLightSet& lights,
        const VkExtent2D& extent,
        const FrameMatrices* matrices,
        FrameLightTileStats* tileStats
    ) const;
    FrameLightTileGpuReadbackStats ReadPreviousLightTileGpuStats(
        std::size_t imageIndex
    ) const;
    FrameSsrGpuDiagnosticsStats ReadPreviousSsrGpuDiagnostics(
        std::size_t imageIndex
    ) const;
    FrameFfxSssrGpuReadbackStats ReadPreviousFfxSssrGpuReadback(
        std::size_t imageIndex
    ) const;
    FrameAutoExposureReadbackStats ReadPreviousAutoExposureStats(
        std::size_t imageIndex
    ) const;
    void UpdateMaterialBuffer(
        std::size_t imageIndex,
        const FrameMaterialSet& materials
    ) const;
    bool ProbeGridEnabled() const;
    void PopulateProbeGridUniforms(UniformBufferObject& uniformData) const;
    void UpdateProbeGridBuffer(
        std::size_t imageIndex,
        RendererProbeGridStats& stats
    ) const;
    void UpdateDirectionalShadowCascadeBuffer(
        std::size_t imageIndex,
        const DirectionalShadowCascadeSet& cascades,
        const glm::mat4& fallbackLightViewProjection
    ) const;
    void UpdateLocalShadowBuffer(
        std::size_t imageIndex,
        const LocalShadowTileSet& localShadowTiles
    ) const;
    FrameLightSet BuildFrameLightSet(std::span<const RenderCommand> renderCommands) const;
    void PrepareReflectionProbeCaptureResources(
        std::size_t imageIndex,
        const FrameLightSet& lights,
        const FrameMatrices* matrices
    );
    bool EnsureReflectionProbeCapturePipelines();
    bool CaptureNextReflectionProbeFace(
        std::size_t imageIndex,
        const FrameLightSet& lights,
        const RendererReflectionProbe& probe
    );
    FrameMatrices ReflectionProbeCaptureMatrices(
        const RendererReflectionProbe& probe,
        u32 face
    ) const;
    std::span<const RenderCommand> ReflectionCaptureInfluenceCommands();
    FrameReflectionProbeSet BuildFrameReflectionProbeSet(
        const FrameMatrices* matrices
    ) const;
    FrameMaterialSet BuildFrameMaterialSet(std::span<const RenderCommand> renderCommands) const;
    void BuildGBufferCommandList(
        std::span<const RenderCommand> renderCommands,
        std::vector<RenderCommand>& gBufferCommands,
        std::vector<RenderCommand>& weightedTranslucencyCommands,
        std::vector<RenderCommand>& forwardResidualCommands,
        const FrameMatrices* matrices,
        bool recordTransparentAlphaReference,
        RendererDrawStats& drawStats
    ) const;
    glm::mat4 LightViewProjection(
        std::span<const RenderCommand> renderCommands,
        const FrameLightSet& lights
    ) const;
    DirectionalShadowCascadeSet BuildDirectionalShadowCascades(
        std::span<const RenderCommand> renderCommands,
        const FrameLightSet& lights,
        const FrameMatrices* matrices,
        bool shadowSamplingEnabled
    ) const;
    DirectionalShadowCascadeSet BuildReflectionCaptureDirectionalShadow(
        std::span<const RenderCommand> shadowCommands,
        const FrameLightSet& lights,
        const RendererReflectionProbe& probe,
        u32 mapSize
    ) const;
    LocalShadowTileSet BuildLocalShadowTiles(
        const FrameLightSet& lights,
        std::span<const RenderCommand> shadowCommands,
        u32 atlasTileCapacity,
        const LocalShadowCacheState* cacheState,
        bool includeRectLights = true
    ) const;
    std::span<const RenderCommand> ShadowRenderCommands() const;
    const VulkanDescriptorSets* ShadowDescriptorSets() const;
    void ResetReflectionCaptureShadowSnapshot();
    void ReleaseReflectionCapturePersistentShadowSnapshots();
    u32 ReflectionCapturePersistentShadowSnapshotCount() const;
    ReflectionCaptureShadowSnapshot* AcquireReflectionCapturePersistentShadowSnapshot(
        i32 probeSceneIndex
    );
    i32 ReflectionCapturePersistentShadowSnapshotSlot(
        const ReflectionCaptureShadowSnapshot* snapshot
    ) const;
    u32 ReflectionCaptureShadowInputSignature(
        const RendererReflectionProbe& probe,
        const CapturedSceneCaptureAudit& audit,
        bool rectShadowEnabled
    ) const;
    bool BuildMainInstanceBatches(
        std::span<const RenderCommand> commands,
        bool allowCacheReuse
    );
    bool UploadMainInstancesIfNeeded(std::size_t imageIndex);

private:
    struct ReflectionCaptureShadowSnapshot {
        i32 probeSceneIndex = -1;
        u32 inputSignature = 0;
        DirectionalShadowCascadeSet directionalShadows{};
        LocalShadowTileSet localShadowTiles{};
        std::vector<std::vector<RenderCommand>> localShadowTileCommandLists;
        u32 directionalDrawCount = 0;
        u32 directionalCasterCount = 0;
        u32 localTilePassCount = 0;
        u32 localDrawCount = 0;
        u32 localPointFaceTileCount = 0;
        u32 localSpotTileCount = 0;
        u32 localRectTileCount = 0;
        u32 localRectRequestedTileCount = 0;
        u32 localRectDroppedTileCount = 0;
        bool directionalRequested = false;
        bool directionalAvailable = false;
        bool localRequested = false;
        bool localAvailable = false;
        bool rectShadowEnabled = false;
        bool built = false;
    };

    struct ReflectionCapturePersistentShadowSnapshot {
        ReflectionCaptureShadowSnapshot snapshot{};
        std::unique_ptr<VulkanShadowMap> directionalShadowMap;
        std::unique_ptr<VulkanLocalShadowAtlas> localShadowAtlas;
        std::unique_ptr<VulkanShadowFramebuffer> directionalShadowFramebuffer;
        std::unique_ptr<VulkanShadowFramebuffer> localShadowFramebuffer;
        std::unique_ptr<VulkanMaterialDescriptorSets> materialDescriptorSets;
        u64 lastUsedSchedulerFrame = 0;
    };

    static constexpr std::size_t kReflectionCapturePersistentShadowCacheCapacity = 2u;

    Window& m_Window;
    const VulkanDevice& m_Device;
    const VulkanPhysicalDevice& m_PhysicalDevice;
    const VulkanSurface& m_Surface;
    VkInstance m_Instance = VK_NULL_HANDLE;
    const VulkanCommandPool& m_CommandPool;
    Scene2D* m_Scene = nullptr;
    Camera2D* m_Camera = nullptr;
    Scene3D* m_MainScene3D = nullptr;
    Scene3D* m_ImGuiScene3D = nullptr;
    Camera3D* m_ImGuiCamera3D = nullptr;
    Scene3D* m_OverlayScene3D = nullptr;
    Camera3D* m_OverlayCamera3D = nullptr;
    const VulkanRenderResources2D& m_RenderResources;
    PipelineSpec m_PipelineSpec;
    std::optional<PipelineSpec> m_OverlayPipelineSpec;
    FrameMatricesProvider m_FrameMatricesProvider;
    RenderQueueBuilder m_RenderQueueBuilder;
    RenderQueue m_RenderQueue;
    RenderQueue m_OverlayRenderQueue;
    RenderQueue m_ShadowRenderQueue;
    RenderQueue m_ReflectionCaptureRenderQueue;
    RenderQueue m_ReflectionCaptureInfluenceRenderQueue;
    std::vector<LocalShadowCacheState> m_LocalShadowCacheStates;
    VulkanRenderDebugSettings m_RenderDebugSettings;
    VulkanShadowSettings m_ShadowSettings;
    VulkanRenderFeatureRegistry m_RenderFeatures;
    RendererStats m_LastStats;
    std::size_t m_CurrentFrame = 0;
    FrameMatrices m_PreviousTemporalMatrices{};
    VkExtent2D m_PreviousTemporalExtent{};
    glm::vec2 m_PreviousTemporalJitterPixels{ 0.0f };
    glm::vec2 m_PreviousTemporalJitterUv{ 0.0f };
    bool m_PreviousTemporalJitterApplied = false;
    u32 m_TemporalFrameCounter = 0;
    bool m_TemporalHistoryValid = false;
    bool m_TemporalHistoryColorValid = false;
    std::optional<u32> m_PreviousTemporalHistoryImageIndex;
    bool m_DlssQualitySceneContentMotionSupported = true;
    RendererTemporalAntialiasingMode m_TemporalAntialiasingMode =
        RendererTemporalAntialiasingMode::Environment;
    bool m_TemporalRenderTargetsRecreateRequested = false;
    std::vector<bool> m_TemporalUpscaleOutputInitialized;
    std::vector<bool> m_DlssMaskInputsInitialized;

    std::unique_ptr<VulkanSwapchain> m_Swapchain;
    std::unique_ptr<VulkanDescriptorSetLayout> m_DescriptorSetLayout;
    std::unique_ptr<VulkanMaterialDescriptorSetLayout> m_MaterialDescriptorSetLayout;
    std::unique_ptr<VulkanHiZDescriptorSetLayout> m_HiZDescriptorSetLayout;
    std::unique_ptr<VulkanSsrReconstructionDescriptorSetLayout>
        m_SsrReconstructionDescriptorSetLayout;
    std::unique_ptr<VulkanFfxSssrConstantsDescriptorSetLayout>
        m_FfxSssrConstantsDescriptorSetLayout;
    std::unique_ptr<VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout>
        m_FfxSssrPrepareIndirectArgsDescriptorSetLayout;
    std::unique_ptr<VulkanFfxSssrClassifyTilesDescriptorSetLayout>
        m_FfxSssrClassifyTilesDescriptorSetLayout;
    std::unique_ptr<VulkanUniformBuffer> m_UniformBuffer;
    std::unique_ptr<VulkanUniformBuffer> m_OverlayUniformBuffer;
    std::unique_ptr<VulkanDescriptorSets> m_DescriptorSets;
    std::unique_ptr<VulkanDescriptorSets> m_OverlayDescriptorSets;
    std::unique_ptr<VulkanMaterialDescriptorSets> m_MaterialDescriptorSets;
    std::unique_ptr<VulkanMaterialDescriptorSets>
        m_ReflectionCaptureMaterialDescriptorSets;
    std::unique_ptr<VulkanGBufferDescriptorSets> m_GBufferDescriptorSets;
    std::unique_ptr<VulkanHiZDescriptorSets> m_HiZDescriptorSets;
    std::unique_ptr<VulkanSsrReconstructionDescriptorSets>
        m_SsrReconstructionDescriptorSets;
    std::unique_ptr<VulkanFfxSssrConstantsResources>
        m_FfxSssrConstantsResources;
    std::unique_ptr<VulkanFfxSssrPrepareIndirectArgsResources>
        m_FfxSssrPrepareIndirectArgsResources;
    std::unique_ptr<VulkanFfxSssrClassifyTilesResources>
        m_FfxSssrClassifyTilesResources;
    std::unique_ptr<VulkanHdrDescriptorSets> m_HdrDescriptorSets;
    std::unique_ptr<VulkanHdrDescriptorSets> m_TemporalUpscaleHdrDescriptorSets;
    std::unique_ptr<VulkanBloomDescriptorSets> m_BloomDescriptorSets;
    std::unique_ptr<VulkanBloomDescriptorSets> m_TemporalUpscaleBloomDescriptorSets;
    std::unique_ptr<VulkanWeightedTranslucencyDescriptorSets> m_WeightedTranslucencyDescriptorSets;
    std::unique_ptr<VulkanLightBuffer> m_LightBuffer;
    std::unique_ptr<VulkanLightTileDiagnosticsBuffer> m_LightTileDiagnosticsBuffer;
    std::unique_ptr<VulkanAutoExposureBuffer> m_AutoExposureBuffer;
    std::unique_ptr<VulkanMaterialBuffer> m_MaterialBuffer;
    std::unique_ptr<VulkanProbeGridBuffer> m_ProbeGridBuffer;
    std::unique_ptr<VulkanDirectionalShadowCascadeBuffer> m_DirectionalShadowCascadeBuffer;
    std::unique_ptr<VulkanLocalShadowBuffer> m_LocalShadowBuffer;
    std::unique_ptr<VulkanBonePaletteFallbackDescriptorSet> m_BonePaletteFallbackDescriptorSet;
    std::unique_ptr<VulkanSceneRenderTargets> m_SceneRenderTargets;
    std::unique_ptr<VulkanDepthPyramid> m_SsrDepthPyramid;
    std::unique_ptr<VulkanBloomPyramid> m_BloomPyramid;
    std::unique_ptr<VulkanColorGradingLut> m_ColorGradingLut;
    std::unique_ptr<VulkanSampler> m_SceneTargetSampler;
    std::unique_ptr<VulkanSampler> m_SsrDepthPyramidSampler;
    std::unique_ptr<VulkanGBufferRenderPass> m_GBufferRenderPass;
    std::unique_ptr<VulkanGBufferFramebuffer> m_GBufferFramebuffer;
    std::unique_ptr<VulkanForwardResidualVelocityRenderPass> m_ForwardResidualVelocityRenderPass;
    std::unique_ptr<VulkanForwardResidualVelocityFramebuffer> m_ForwardResidualVelocityFramebuffer;
    std::unique_ptr<VulkanDlssMaskRenderPass> m_DlssMaskRenderPass;
    std::unique_ptr<VulkanDlssMaskFramebuffer> m_DlssMaskFramebuffer;
    std::unique_ptr<VulkanHdrRenderPass> m_HdrRenderPass;
    std::unique_ptr<VulkanHdrFramebuffer> m_HdrFramebuffer;
    std::unique_ptr<VulkanHdrFramebuffer> m_TaaResolveFramebuffer;
    std::unique_ptr<VulkanBloomRenderPass> m_BloomDownsampleRenderPass;
    std::unique_ptr<VulkanBloomRenderPass> m_BloomUpsampleRenderPass;
    std::unique_ptr<VulkanBloomFramebuffer> m_BloomDownsampleFramebuffer;
    std::unique_ptr<VulkanBloomFramebuffer> m_BloomUpsampleFramebuffer;
    std::unique_ptr<VulkanWeightedTranslucencyRenderPass> m_WeightedTranslucencyRenderPass;
    std::unique_ptr<VulkanWeightedTranslucencyFramebuffer> m_WeightedTranslucencyFramebuffer;
    std::unique_ptr<VulkanDepthBuffer> m_DepthBuffer;
    std::unique_ptr<VulkanShadowMap> m_ShadowMap;
    std::unique_ptr<VulkanDirectionalShadowCascadeAtlas> m_DirectionalShadowCascadeAtlas;
    std::unique_ptr<VulkanLocalShadowAtlas> m_LocalShadowAtlas;
    std::unique_ptr<VulkanShadowMap> m_ReflectionCaptureShadowMap;
    std::unique_ptr<VulkanLocalShadowAtlas> m_ReflectionCaptureLocalShadowAtlas;
    std::unique_ptr<VulkanShadowRenderPass> m_ShadowRenderPass;
    std::unique_ptr<VulkanShadowFramebuffer> m_ShadowFramebuffer;
    std::unique_ptr<VulkanShadowFramebuffer> m_DirectionalShadowCascadeFramebuffer;
    std::unique_ptr<VulkanShadowFramebuffer> m_LocalShadowFramebuffer;
    std::unique_ptr<VulkanShadowFramebuffer> m_ReflectionCaptureShadowFramebuffer;
    std::unique_ptr<VulkanShadowFramebuffer>
        m_ReflectionCaptureLocalShadowFramebuffer;
    std::unique_ptr<VulkanRenderPass> m_RenderPass;
    std::unique_ptr<VulkanRenderPass> m_DepthLoadRenderPass;
    std::unique_ptr<VulkanImGuiLayer> m_ImGuiLayer;
    std::unique_ptr<VulkanGraphicsPipeline> m_GraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_ReflectionCaptureGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedReflectionCaptureGraphicsPipeline;
    VkRenderPass m_ReflectionCapturePipelineRenderPass = VK_NULL_HANDLE;
    std::unique_ptr<VulkanGraphicsPipeline> m_InstancedGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedInstancedGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_GBufferGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedGBufferGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DeferredLightingPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_HdrCompositePipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_TaaResolvePipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_BloomDownsamplePipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_BloomUpsamplePipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_GBufferDebugPipeline;
    std::unique_ptr<VulkanComputePipeline> m_LightTileCullComputePipeline;
    std::unique_ptr<VulkanComputePipeline> m_LightClusterCullComputePipeline;
    std::unique_ptr<VulkanComputePipeline> m_AutoExposureComputePipeline;
    std::unique_ptr<VulkanComputePipeline> m_HiZBuildComputePipeline;
    std::unique_ptr<VulkanComputePipeline> m_SsrTraceComputePipeline;
    std::unique_ptr<VulkanComputePipeline> m_SsrTemporalComputePipeline;
    std::unique_ptr<VulkanComputePipeline> m_SsrSpatialComputePipeline;
    std::unique_ptr<VulkanComputePipeline> m_SsrDiagnosticsComputePipeline;
    std::unique_ptr<VulkanComputePipeline> m_FfxSssrClassifyTilesPipeline;
    std::unique_ptr<VulkanComputePipeline> m_FfxSssrPrepareIndirectArgsPipeline;
    std::vector<bool> m_FfxSssrGpuReadbackReady;
    bool m_SsrReconstructionImagesInitialized = false;
    // IBL textures
    std::unique_ptr<VulkanImage> m_IblBrdfImage;
    std::unique_ptr<VulkanImage> m_IblIrradianceImage;
    std::unique_ptr<VulkanImage> m_IblPrefilteredImage;
    VkImageView m_IblIrradianceView = VK_NULL_HANDLE;
    VkImageView m_IblPrefilteredView = VK_NULL_HANDLE;
    VkSampler m_IblSampler = VK_NULL_HANDLE;
    VulkanIblGenerationSettings m_IblGenerationSettings{};
    VulkanIblGenerationInfo m_IblGenerationInfo{};
    VulkanReflectionProbeResources m_ReflectionProbeResources;
    i32 m_ReflectionCaptureRoundRobinSceneIndex = -1;
    i32 m_ReflectionCaptureActiveSceneIndex = -1;
    u64 m_ReflectionCaptureSchedulerFrame = 0;
    ReflectionCaptureShadowSnapshot m_ReflectionCaptureShadowSnapshot{};
    std::array<ReflectionCapturePersistentShadowSnapshot,
        kReflectionCapturePersistentShadowCacheCapacity>
        m_ReflectionCapturePersistentShadowSnapshots{};
    u32 m_ReflectionCapturePersistentShadowSnapshotEvictionCount = 0;
    std::unique_ptr<VulkanTexture2D> m_VisibleSkyboxTexture;
    std::unique_ptr<VulkanTexture2D> m_VisibleSkyboxFallbackTexture;
    std::unique_ptr<VulkanSampler> m_VisibleSkyboxSampler;
    std::unique_ptr<VulkanGraphicsPipeline> m_DepthPrefillGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedDepthPrefillGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_WeightedTranslucencyGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedWeightedTranslucencyGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_WeightedTranslucencyResolvePipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_ForwardResidualVelocityGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedForwardResidualVelocityGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DlssMaskGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedDlssMaskGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_ForwardResidualHdrGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedForwardResidualHdrGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_ForwardResidualGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedForwardResidualGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_ShadowGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_DoubleSidedShadowGraphicsPipeline;
    std::unique_ptr<VulkanGraphicsPipeline> m_OverlayGraphicsPipeline;
    std::unique_ptr<VulkanFramebuffer> m_Framebuffer;
    std::unique_ptr<VulkanFramebuffer> m_DepthLoadFramebuffer;
    std::unique_ptr<VulkanCommandBuffer> m_CommandBuffer;
    std::unique_ptr<VulkanInstanceBuffer> m_InstanceBuffer;
    std::unique_ptr<VulkanGpuTimer> m_GpuTimer;
    std::unique_ptr<VulkanSyncObjects> m_SyncObjects;
    std::vector<Instance3D> m_MainInstances;
    std::vector<RenderInstanceBatch> m_MainInstanceBatches;
    std::vector<u64> m_MainInstanceUploadSignatures;
    std::vector<bool> m_LightTileGpuReadbackReady;
    u64 m_MainInstanceSignature = 0;
    bool m_MainInstanceBatchesCacheValid = false;
};

}
