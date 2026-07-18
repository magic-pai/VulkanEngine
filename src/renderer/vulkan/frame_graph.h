#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

enum class RenderFramePassStatus {
    Active,
    Roadmap
};

enum class RenderFramePassQueue {
    Graphics,
    Compute,
    AsyncCompute,
    Transfer,
    Present
};

enum class RenderFramePassKind {
    FrameSetup,
    Visibility,
    DepthPrepass,
    VirtualGeometry,
    Shadow,
    GBuffer,
    DeferredLighting,
    ScreenSpaceAmbientOcclusion,
    Forward,
    GlobalIllumination,
    Reflections,
    Volumetrics,
    PostProcess,
    TemporalUpscale,
    UserInterface,
    Present
};

enum class RenderGraphResourceStatus {
    Physical,
    Planned
};

enum class RenderGraphResourceLifetime {
    Swapchain,
    PerFrame,
    PersistentHistory,
    PersistentCache
};

enum class RenderFrameGraphResourceAccess {
    ReadSampled,
    ReadAttachment,
    WriteColorAttachment,
    WriteDepthAttachment,
    WriteStorage,
    Present
};

enum class RenderFrameGraphValidationIssueKind {
    UnnamedPass,
    DuplicatePassId,
    UnnamedResource,
    DuplicateResourceId,
    MissingResourceRef,
    ReadBeforeFirstWrite,
    UnusedPhysicalResource,
    WriteOnlyRoadmapResource,
    ActivePassWritesPlannedResource
};

enum class RenderFrameGraphBarrierResourceKind {
    Image,
    Buffer
};

enum class RenderFrameGraphBarrierBridge {
    LightTileCullFragmentRead,
    AutoExposureHistoryFragmentRead
};

struct RenderGraphResource {
    u32 id = 0;
    RenderGraphResourceStatus status = RenderGraphResourceStatus::Planned;
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::PerFrame;
    std::string_view name;
    std::string_view format;
    std::string_view usage;
    std::string_view scale;
    u32 firstUsePassId = 0;
    std::string_view firstUsePassName;
    u32 lastUsePassId = 0;
    std::string_view lastUsePassName;
    u32 readCount = 0;
    u32 writeCount = 0;
};

struct RenderFrameGraphResourceRef {
    u32 resourceId = 0;
    std::string_view name;
    RenderFrameGraphResourceAccess access =
        RenderFrameGraphResourceAccess::ReadSampled;
};

struct RenderFrameGraphPassDependency {
    u32 passId = 0;
    std::string_view passName;
    u32 resourceId = 0;
    std::string_view resourceName;
    bool writeDependency = false;
};

struct RenderFrameGraphValidationIssue {
    RenderFrameGraphValidationIssueKind kind =
        RenderFrameGraphValidationIssueKind::MissingResourceRef;
    u32 passId = 0;
    std::string passName;
    u32 resourceId = 0;
    std::string resourceName;
    bool writeRef = false;

    RenderFrameGraphValidationIssue() = default;

    RenderFrameGraphValidationIssue(
        RenderFrameGraphValidationIssueKind issueKind,
        u32 issuePassId,
        std::string_view issuePassName,
        u32 issueResourceId,
        std::string_view issueResourceName,
        bool issueWriteRef
    )
        : kind(issueKind),
          passId(issuePassId),
          passName(issuePassName),
          resourceId(issueResourceId),
          resourceName(issueResourceName),
          writeRef(issueWriteRef) {}
};

struct RenderFrameGraphBarrierTransition {
    u32 producerPassId = 0;
    std::string_view producerPassName;
    RenderFramePassQueue producerQueue = RenderFramePassQueue::Graphics;
    u32 consumerPassId = 0;
    std::string_view consumerPassName;
    RenderFramePassQueue consumerQueue = RenderFramePassQueue::Graphics;
    u32 resourceId = 0;
    std::string_view resourceName;
    RenderFrameGraphBarrierResourceKind resourceKind =
        RenderFrameGraphBarrierResourceKind::Image;
    RenderFrameGraphResourceAccess srcAccess =
        RenderFrameGraphResourceAccess::WriteStorage;
    RenderFrameGraphResourceAccess dstAccess =
        RenderFrameGraphResourceAccess::ReadSampled;
    std::string_view srcStage;
    std::string_view dstStage;
    std::string_view oldLayout;
    std::string_view newLayout;
    bool layoutTransition = false;
    bool queueOwnershipTransfer = false;
    bool writeDependency = false;
};

struct RenderFramePass {
    u32 id = 0;
    RenderFramePassKind kind = RenderFramePassKind::FrameSetup;
    RenderFramePassStatus status = RenderFramePassStatus::Roadmap;
    RenderFramePassQueue queue = RenderFramePassQueue::Graphics;
    std::string_view name;
    std::string_view reads;
    std::string_view writes;
    std::string_view purpose;
    std::vector<RenderFrameGraphResourceRef> readResources;
    std::vector<RenderFrameGraphResourceRef> writeResources;
    std::vector<RenderFrameGraphPassDependency> dependencies;
};

struct RenderFrameGraphValidation {
    u32 issueCount = 0;
    u32 unnamedPassCount = 0;
    u32 duplicatePassIdCount = 0;
    u32 unnamedResourceCount = 0;
    u32 duplicateResourceIdCount = 0;
    u32 missingResourceRefCount = 0;
    u32 readBeforeFirstWriteCount = 0;
    u32 unusedPhysicalResourceCount = 0;
    u32 writeOnlyRoadmapResourceCount = 0;
    u32 activePassWritesPlannedResourceCount = 0;
    std::vector<RenderFrameGraphValidationIssue> issues;
};

struct RenderFrameGraphReferenceStats {
    u32 readCount = 0;
    u32 writeCount = 0;
    u32 readSampledCount = 0;
    u32 readAttachmentCount = 0;
    u32 writeColorAttachmentCount = 0;
    u32 writeDepthAttachmentCount = 0;
    u32 writeStorageCount = 0;
    u32 presentCount = 0;
    u32 unstructuredReadTokenCount = 0;
    u32 unstructuredWriteTokenCount = 0;
};

struct RenderFrameGraphDependencyStats {
    u32 dependencyCount = 0;
    u32 readAfterWriteCount = 0;
    u32 writeAfterWriteCount = 0;
};

struct RenderFrameGraphLifetimeStats {
    u32 usedResourceCount = 0;
    u32 unusedResourceCount = 0;
    u32 readOnlyResourceCount = 0;
    u32 writeOnlyResourceCount = 0;
    u32 readWriteResourceCount = 0;
};

struct RenderFrameGraphBarrierStats {
    u32 transitionCount = 0;
    u32 imageTransitionCount = 0;
    u32 bufferTransitionCount = 0;
    u32 layoutTransitionCount = 0;
    u32 queueOwnershipTransferCount = 0;
    u32 readAfterWriteTransitionCount = 0;
    u32 writeAfterWriteTransitionCount = 0;
};

struct RenderFrameGraphBarrierExecutionStats {
    u32 plannedBridgeBarrierCount = 0;
    u32 executedBarrierCount = 0;
    u32 fallbackBarrierCount = 0;
    u32 mismatchCount = 0;
};

struct RenderFrameGraphBarrierExecutionResult {
    u32 plannedBarrierCount = 0;
    bool matched = false;
    bool fallback = false;
    bool mismatch = false;
};

struct RenderFrameGraphPlan {
    std::string_view name;
    std::string_view target;
    std::vector<RenderFramePass> passes;
    std::vector<RenderGraphResource> resources;
    std::vector<RenderFrameGraphBarrierTransition> barrierTransitions;
    RenderFrameGraphValidation validation;
    RenderFrameGraphReferenceStats references;
    RenderFrameGraphDependencyStats dependencies;
    RenderFrameGraphLifetimeStats lifetimes;
    RenderFrameGraphBarrierStats barriers;
    RenderFrameGraphBarrierExecutionStats barrierExecution;
    u32 activePassCount = 0;
    u32 roadmapPassCount = 0;
    u32 physicalResourceCount = 0;
    u32 plannedResourceCount = 0;
};

using RenderFrameGraphAppendCallback = void (*)(
    RenderFrameGraphPlan& plan,
    RenderFramePassKind stage,
    const void* userData
);

struct CurrentVulkanFrameGraphInputs {
    bool shadowPassEnabled = false;
    bool overlayPassEnabled = false;
    bool imguiPassEnabled = true;
    bool has3DMainPass = false;
    bool usesLegacyForwardMain = true;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    u32 swapchainImageCount = 0;
    u32 shadowMapSize = 0;
    u32 directionalShadowAtlasWidth = 0;
    u32 directionalShadowAtlasHeight = 0;
    u32 directionalShadowAtlasTileSize = 0;
    u32 directionalShadowAtlasCapacity = 0;
    u32 localShadowAtlasWidth = 0;
    u32 localShadowAtlasHeight = 0;
    u32 localShadowAtlasTileSize = 0;
    u32 localShadowAtlasCapacity = 0;
    u32 localShadowAtlasAssignedTiles = 0;
    u32 directionalShadowAtlasPasses = 0;
    u32 directionalShadowCascadeCount = 0;
    bool directionalShadowCascadeScaffoldEnabled = false;
    bool hdrSceneColorAllocated = false;
    VkFormat hdrSceneColorFormat = VK_FORMAT_UNDEFINED;
    bool hdrRenderPassAllocated = false;
    bool bloomPyramidAllocated = false;
    VkFormat bloomPyramidFormat = VK_FORMAT_UNDEFINED;
    u32 bloomPyramidMipCount = 0;
    bool colorGradingLutAllocated = false;
    VkFormat colorGradingLutFormat = VK_FORMAT_UNDEFINED;
    u32 colorGradingLutSize = 0;
    bool iblBrdfLutAllocated = false;
    VkFormat iblBrdfLutFormat = VK_FORMAT_UNDEFINED;
    u32 iblBrdfLutSize = 0;
    bool iblIrradianceMapAllocated = false;
    VkFormat iblIrradianceFormat = VK_FORMAT_UNDEFINED;
    u32 iblIrradianceFaceSize = 0;
    bool iblPrefilteredMapAllocated = false;
    VkFormat iblPrefilteredFormat = VK_FORMAT_UNDEFINED;
    u32 iblPrefilteredFaceSize = 0;
    u32 iblPrefilteredMipCount = 0;
    bool sceneReflectionProbesAllocated = false;
    u32 sceneReflectionProbeCount = 0;
    bool sceneReflectionProbeSelectionAllocated = false;
    u32 sceneReflectionProbeSelectedMask = 0;
    u32 sceneReflectionProbeBoxProjectionMask = 0;
    u32 sceneReflectionProbePositiveInfluenceMask = 0;
    f32 sceneReflectionProbeNormalizedBlendWeightSum = 0.0f;
    u32 sceneReflectionProbeBlendNormalizationFallbackCount = 0;
    bool reflectionCaptureSourceAllocated = false;
    u32 reflectionCaptureSourceType = 0;
    u32 reflectionCaptureFallbackReason = 0;
    bool reflectionCaptureRefreshPolicyAllocated = false;
    u32 reflectionCaptureRefreshPolicy = 0;
    bool reflectionCaptureForcedRefreshRequested = false;
    bool reflectionCaptureSceneDirtyRequested = false;
    bool capturedSceneReflectionProbePlaceholderAllocated = false;
    u32 capturedSceneReflectionProbePlaceholderReadyCount = 0;
    u32 capturedSceneReflectionProbeInvalidatedCount = 0;
    bool sceneReflectionProbeCubemapAllocated = false;
    VkFormat sceneReflectionProbeCubemapFormat = VK_FORMAT_UNDEFINED;
    u32 sceneReflectionProbeCubemapFaceSize = 0;
    u32 sceneReflectionProbeCubemapMipCount = 0;
    bool authoredReflectionCubemapCacheAllocated = false;
    VkFormat authoredReflectionCubemapFormat = VK_FORMAT_UNDEFINED;
    u32 authoredReflectionCubemapFaceSize = 0;
    u32 authoredReflectionCubemapMipCount = 0;
    u32 authoredReflectionCubemapLoadedCount = 0;
    u32 authoredReflectionCubemapEquirectangularLoadedCount = 0;
    u32 authoredReflectionCubemapEquirectangularConversionCount = 0;
    u32 authoredReflectionCubemapHdrLoadedCount = 0;
    u32 authoredReflectionCubemapPrefilteredLoadedCount = 0;
    u32 authoredReflectionCubemapPrefilteredUploadCount = 0;
    u32 authoredReflectionCubemapPrefilterMode = 0;
    u32 authoredReflectionCubemapFilterQuality = 1;
    bool authoredReflectionCubemapSeamAwareFiltering = false;
    u32 authoredReflectionCubemapIrradianceReadyCount = 0;
    bool authoredReflectionCubemapIrradianceApplied = false;
    u32 authoredReflectionCubemapDiffuseLobesReadyCount = 0;
    bool authoredReflectionCubemapDiffuseLobesApplied = false;
    u32 authoredReflectionCubemapDiffuseLobeCount = 0;
    u32 authoredReflectionCubemapCacheHitCount = 0;
    u32 authoredReflectionCubemapReloadCount = 0;
    u32 authoredReflectionCubemapRefreshCheckCount = 0;
    bool autoExposureHistogramEnabled = false;
    bool autoExposureHistoryAllocated = false;
    bool deferredLightingEnabled = false;
    RenderFrameGraphAppendCallback appendRenderFeatures = nullptr;
    const void* appendRenderFeaturesUserData = nullptr;
    bool lightTileCullComputeEnabled = false;
    bool gBufferDebugEnabled = false;
    bool weightedTranslucencyTargetsAllocated = false;
    VkFormat weightedTranslucencyAccumFormat = VK_FORMAT_UNDEFINED;
    VkFormat weightedTranslucencyRevealageFormat = VK_FORMAT_UNDEFINED;
    bool weightedTranslucencyRenderPassAllocated = false;
    u32 weightedTranslucencyFramebufferCount = 0;
    bool forwardResidualPreUpscaleEnabled = false;
    bool deferredTargetsAllocated = false;
    VkFormat sceneDepthFormat = VK_FORMAT_UNDEFINED;
    VkFormat velocityFormat = VK_FORMAT_UNDEFINED;
    VkFormat gBufferAlbedoFormat = VK_FORMAT_UNDEFINED;
    VkFormat gBufferNormalRoughnessFormat = VK_FORMAT_UNDEFINED;
    VkFormat gBufferMaterialFormat = VK_FORMAT_UNDEFINED;
    VkFormat gBufferEmissiveFormat = VK_FORMAT_UNDEFINED;
    VkFormat gBufferMaterialAuxFormat = VK_FORMAT_UNDEFINED;
    bool gBufferRenderPassAllocated = false;
    bool gBufferGeometryEnabled = false;
    bool forwardResidualVelocityPreUpscaleEnabled = false;
    bool weightedTranslucencyVelocityPreUpscaleEnabled = false;
    bool temporalStateAllocated = false;
    bool temporalHistoryValid = false;
    bool temporalHistoryReset = false;
    u32 temporalHistoryResetReason = 0;
    bool temporalJitterEnabled = false;
    bool temporalJitterApplied = false;
    bool velocityCameraMotionReady = false;
    bool velocityObjectMotionReady = false;
    bool velocityMaterialAuxMigrated = false;
    bool temporalHistoryColorAllocated = false;
    VkFormat temporalHistoryColorFormat = VK_FORMAT_UNDEFINED;
    bool temporalHistoryColorReady = false;
    bool temporalHistoryColorCopyEnabled = false;
    bool taaResolveConfigured = false;
    bool taaResolveEnabled = false;
    bool taaVelocityReprojectionEnabled = false;
    u32 taaFallbackReason = 0;
    u32 temporalConsumerReadinessMask = 0;
    u32 temporalConsumerActiveMask = 0;
    u32 temporalConsumerUnsupportedMask = 0;
    f32 temporalRenderScaleRequested = 1.0f;
    f32 temporalRenderScaleActive = 1.0f;
    bool temporalRenderScaleApplied = false;
    u32 temporalUpscaleDisplayWidth = 0;
    u32 temporalUpscaleDisplayHeight = 0;
    u32 temporalUpscaleRequestedWidth = 0;
    u32 temporalUpscaleRequestedHeight = 0;
    u32 temporalUpscaleActiveWidth = 0;
    u32 temporalUpscaleActiveHeight = 0;
    bool temporalUpscaleOutputAllocated = false;
    VkFormat temporalUpscaleOutputFormat = VK_FORMAT_UNDEFINED;
    u32 temporalUpscaleOutputWidth = 0;
    u32 temporalUpscaleOutputHeight = 0;
    bool dlssMaskInputsAllocated = false;
    VkFormat dlssBiasCurrentColorMaskFormat = VK_FORMAT_UNDEFINED;
    VkFormat dlssTransparencyMaskFormat = VK_FORMAT_UNDEFINED;
    bool dlssMaskPreUpscaleEnabled = false;
    bool dynamicResolutionRequested = false;
    bool dynamicResolutionEnabled = false;
    bool taauRequested = false;
    bool temporalUpscaleRequested = false;
    bool temporalUpscaleEnabled = false;
    u32 temporalUpscaleFallbackReason = 0;
    u32 temporalUpscaleInputReadinessMask = 0;
    u32 temporalUpscaleRequiredInputMask = 0;
    bool temporalUpscaleContractReady = false;
    bool temporalUpscalerPluginRequested = false;
    bool temporalUpscalerPluginAvailable = false;
    u32 temporalUpscalerProviderKind = 0;
    u32 temporalUpscalerPackageFallbackReason = 0;
    bool temporalUpscalerPackageReady = false;
    bool temporalUpscalerSuperResolutionSymbolsFound = false;
    bool temporalUpscalerEvaluateAdapterAvailable = false;
    u32 temporalUpscalerRuntimeFallbackReason = 0;
    bool temporalUpscalerAdapterCompiled = false;
    bool temporalUpscalerInitializationAttempted = false;
    bool temporalUpscalerInitialized = false;
    bool temporalUpscalerCapabilityParametersReady = false;
    bool temporalUpscalerFeatureRequirementsQueried = false;
    bool temporalUpscalerFeatureRequirementsSupported = false;
    u32 temporalUpscalerFeatureSupportedMask = 0;
    u32 temporalUpscalerInstanceExtensionMissingAvailableCount = 0;
    u32 temporalUpscalerInstanceExtensionMissingEnabledCount = 0;
    u32 temporalUpscalerDeviceExtensionMissingAvailableCount = 0;
    u32 temporalUpscalerDeviceExtensionMissingEnabledCount = 0;
    bool temporalUpscalerSuperResolutionSupported = false;
    bool temporalUpscalerOptimalSettingsQueried = false;
    bool temporalUpscalerDlssQualityGateRequested = false;
    bool temporalUpscalerDlssQualityGateReady = false;
    u32 temporalUpscalerDlssQualityRequiredMask = 0;
    u32 temporalUpscalerDlssQualityReadyMask = 0;
    u32 temporalUpscalerDlssQualityBlockerMask = 0;
    bool reflectionCaptureSlotTableAllocated = false;
    u32 reflectionCaptureSlotCount = 0;
    u32 reflectionCaptureSlotReadyCount = 0;
    u32 reflectionCaptureSlotFallbackCount = 0;
    bool probeGridAllocated = false;
    bool probeGridEnabled = false;
    u32 probeGridProbeCount = 0;
    u32 probeGridSizeX = 0;
    u32 probeGridSizeY = 0;
    u32 probeGridSizeZ = 0;
    u32 probeGridVec4sPerProbe = 0;
    u32 probeGridDirectionalLobeCount = 0;
    u32 probeGridCellCount = 0;
    u32 probeGridFallbackReason = 0;
    bool probeGridDebugViewEnabled = false;
    bool probeGridCellDebugViewEnabled = false;
    bool temporalUpscalePostSourceRequested = false;
    bool temporalUpscalePostSourceExpected = false;
    u32 temporalUpscalePostSourceFallbackReason = 0;
    bool ssrDepthPyramidAllocated = false;
    bool ssrReconstructionActive = false;
    VkFormat ssrDepthPyramidFormat = VK_FORMAT_UNDEFINED;
    u32 ssrDepthPyramidWidth = 0;
    u32 ssrDepthPyramidHeight = 0;
    u32 ssrDepthPyramidMipCount = 0;
};

std::string_view RenderFramePassStatusName(RenderFramePassStatus status);
std::string_view RenderFramePassQueueName(RenderFramePassQueue queue);
std::string_view RenderGraphResourceStatusName(RenderGraphResourceStatus status);
std::string_view RenderGraphResourceLifetimeName(RenderGraphResourceLifetime lifetime);
std::string_view RenderFrameGraphResourceAccessName(
    RenderFrameGraphResourceAccess access
);
std::string_view RenderFrameGraphValidationIssueKindName(
    RenderFrameGraphValidationIssueKind kind
);
std::string_view RenderFrameGraphBarrierResourceKindName(
    RenderFrameGraphBarrierResourceKind kind
);
std::string_view RenderFrameGraphBarrierBridgeName(
    RenderFrameGraphBarrierBridge bridge
);

RenderFrameGraphBarrierExecutionResult RecordRenderFrameGraphBarrierExecution(
    RenderFrameGraphPlan& plan,
    RenderFrameGraphBarrierBridge bridge
);

RenderFrameGraphPlan BuildCurrentVulkanFrameGraphPlan(
    CurrentVulkanFrameGraphInputs inputs
);
RenderFrameGraphPlan BuildAAAFrameGraphBlueprint();
void AppendRenderFrameGraphPass(
    RenderFrameGraphPlan& plan,
    RenderFramePassKind kind,
    RenderFramePassStatus status,
    RenderFramePassQueue queue,
    std::string_view name,
    std::string_view reads,
    std::string_view writes,
    std::string_view purpose
);

}
