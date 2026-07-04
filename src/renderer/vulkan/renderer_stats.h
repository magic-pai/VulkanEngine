#pragma once

#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/shadow_settings.h"
#include "renderer/vulkan/uniform_buffer.h"
#include "renderer/vulkan/vulkan_common.h"

#include <array>

namespace se {

struct RendererCpuStats {
    f32 waitAcquireMs = 0.0f;
    f32 imguiMs = 0.0f;
    f32 pickingMs = 0.0f;
    f32 queueBuildMs = 0.0f;
    f32 uniformUpdateMs = 0.0f;
    f32 commandRecordMs = 0.0f;
    f32 submitPresentMs = 0.0f;
    f32 totalFrameMs = 0.0f;
};

struct RendererDrawStats {
    u32 mainDraws = 0;
    u32 gBufferDraws = 0;
    u32 overlayDraws = 0;
    u32 shadowDraws = 0;
    u32 hybridDeferredOpaqueDraws = 0;
    u32 hybridForwardTransparentDraws = 0;
    u32 hybridForwardSpecialDraws = 0;
    u32 hybridWeightedTranslucencyDraws = 0;
    u32 hybridWeightedTranslucencySortOps = 0;
    u32 hybridWeightedTranslucencySortedTransparentDraws = 0;
    u32 hybridForwardResidualDraws = 0;
    u32 hybridForwardResidualSortOps = 0;
    u32 hybridForwardResidualSortedTransparentDraws = 0;
    u32 hybridForwardResidualStableSpecialDraws = 0;
    u64 mainTriangles = 0;
    u64 gBufferTriangles = 0;
    u64 overlayTriangles = 0;
    u64 shadowTriangles = 0;
    u64 hybridDeferredOpaqueTriangles = 0;
    u64 hybridWeightedTranslucencyTriangles = 0;
    u64 hybridForwardResidualTriangles = 0;
    u32 matrixRecalculations = 0;
    u32 mainVisible = 0;
    u32 mainCulled = 0;
    u32 overlayVisible = 0;
    u32 overlayCulled = 0;
    u32 shadowVisible = 0;
    u32 shadowCulled = 0;
    u32 mainBoundsCacheHits = 0;
    u32 mainBoundsCacheMisses = 0;
    u32 mainCommandCacheHits = 0;
    u32 mainCommandCacheMisses = 0;
    u32 mainVisibilityCacheHits = 0;
    u32 mainVisibilityCacheMisses = 0;
    u32 mainQueueCacheHits = 0;
    u32 mainQueueCacheMisses = 0;
    u32 overlayBoundsCacheHits = 0;
    u32 overlayBoundsCacheMisses = 0;
    u32 overlayCommandCacheHits = 0;
    u32 overlayCommandCacheMisses = 0;
    u32 overlayVisibilityCacheHits = 0;
    u32 overlayVisibilityCacheMisses = 0;
    u32 overlayQueueCacheHits = 0;
    u32 overlayQueueCacheMisses = 0;
    u32 mainInstancedDraws = 0;
    u32 mainInstancedInstances = 0;
    u32 mainInstanceBatchCacheHits = 0;
    u32 mainInstanceBatchCacheMisses = 0;
};

struct RendererShadowCascadeStats {
    u32 configuredCount = 0;
    u32 activeCount = 0;
    u32 stableSnappingEnabled = 0;
    u32 atlasAllocated = 0;
    u32 atlasTileSize = 0;
    u32 atlasWidth = 0;
    u32 atlasHeight = 0;
    u32 atlasTileColumns = 0;
    u32 atlasTileRows = 0;
    u32 atlasCascadeCapacity = 0;
    u32 pcfKernelRadius = 0;
    f32 pcssStrength = 0.0f;
    f32 splitLambda = 0.0f;
    f32 maxDistance = 0.0f;
    f32 blendRatio = 0.0f;
    f32 fadeRatio = 0.0f;
    f32 contactShadowStrength = 0.0f;
    f32 contactShadowLength = 0.0f;
    f32 contactShadowThickness = 0.0f;
    u32 contactShadowSteps = 0;
    f32 contactShadowJitterStrength = 0.0f;
    f32 contactShadowEdgeFadePixels = 0.0f;
    f32 nearDepth = 0.0f;
    f32 farDepth = 0.0f;
    std::array<f32, kMaxDirectionalShadowCascades> splitDepths{};
    std::array<f32, kMaxDirectionalShadowCascades> texelWorldSizes{};
};

struct RendererLocalShadowAtlasStats {
    u32 allocated = 0;
    u32 tileSize = 0;
    u32 atlasWidth = 0;
    u32 atlasHeight = 0;
    u32 tileColumns = 0;
    u32 tileRows = 0;
    u32 tileCapacity = 0;
    u32 shadowableLocalLights = 0;
    u32 pointLightCount = 0;
    u32 spotLightCount = 0;
    u32 pointFaceTiles = 0;
    u32 spotTiles = 0;
    u32 requestedTiles = 0;
    u32 assignedTiles = 0;
    u32 droppedTiles = 0;
    u32 recordedTilePasses = 0;
    u32 recordedDraws = 0;
    u32 recordedMeshBinds = 0;
    u32 cacheEligibleTiles = 0;
    u32 cacheHitTiles = 0;
    u32 cacheMissTiles = 0;
    u32 cacheSkippedTiles = 0;
    u32 pcfKernelRadius = 0;
    f32 biasMin = 0.0f;
    f32 biasSlope = 0.0f;
    f32 pcfRadius = 0.0f;
    f32 pcssStrength = 0.0f;
    f32 faceBlendStrength = 0.0f;
};

struct RendererWeightedTranslucencyStats {
    u32 allocated = 0;
    u32 accumWidth = 0;
    u32 accumHeight = 0;
    u32 revealageWidth = 0;
    u32 revealageHeight = 0;
    VkFormat accumFormat = VK_FORMAT_UNDEFINED;
    VkFormat revealageFormat = VK_FORMAT_UNDEFINED;
    u32 renderPassAllocated = 0;
    u32 framebufferCount = 0;
    u32 clearPasses = 0;
    u32 draws = 0;
    u32 sharedLightListDraws = 0;
    u32 shadowReadyDraws = 0;
    u32 resolveDraws = 0;
};

struct RendererSsaoStats {
    u32 enabled = 0;
    f32 strength = 0.0f;
    f32 radius = 0.0f;
    f32 bias = 0.0f;
    u32 sampleCount = 0;
};

struct RendererSsrStats {
    u32 enabled = 0;
    u32 colorResolveEnabled = 0;
    f32 strength = 0.0f;
    f32 rayLength = 0.0f;
    f32 thickness = 0.0f;
    u32 stepCount = 0;
};

struct RendererIblStats {
    u32 brdfLutAllocated = 0;
    u32 brdfLutSize = 0;
    VkFormat brdfLutFormat = VK_FORMAT_UNDEFINED;
    u32 irradianceMapAllocated = 0;
    u32 irradianceFaceSize = 0;
    VkFormat irradianceFormat = VK_FORMAT_UNDEFINED;
    u32 prefilteredMapAllocated = 0;
    u32 prefilteredFaceSize = 0;
    u32 prefilteredMipCount = 0;
    VkFormat prefilteredFormat = VK_FORMAT_UNDEFINED;
    u32 descriptorSetsBound = 0;
    u32 shaderIntegrationEnabled = 0;
};

enum class RendererProbeGridFallbackReason : u32 {
    None = 0,
    Disabled = 1,
    BlendZero = 2,
    BufferUnavailable = 3,
    InvalidLayout = 4,
    FrameIndexOutOfRange = 5
};

struct RendererProbeGridStats {
    u32 allocated = 0;
    u32 enabled = 0;
    u32 shaderIntegrationEnabled = 0;
    u32 bufferUpdates = 0;
    u32 fallbackCount = 0;
    u32 fallbackReason = 0;
    u32 probeCount = 0;
    u32 sizeX = 0;
    u32 sizeY = 0;
    u32 sizeZ = 0;
    u32 vec4sPerProbe = 0;
    u32 directionalLobeCount = 0;
    u32 cellCount = 0;
    f32 originX = 0.0f;
    f32 originY = 0.0f;
    f32 originZ = 0.0f;
    f32 boundsMinX = 0.0f;
    f32 boundsMinY = 0.0f;
    f32 boundsMinZ = 0.0f;
    f32 boundsMaxX = 0.0f;
    f32 boundsMaxY = 0.0f;
    f32 boundsMaxZ = 0.0f;
    f32 spacing = 0.0f;
    f32 blendStrength = 0.0f;
    u32 debugViewEnabled = 0;
    u32 cellDebugViewEnabled = 0;
};

struct RendererReflectionProbeStats {
    u32 fallbackEnabled = 0;
    f32 diffuseIntensity = 0.0f;
    f32 specularIntensity = 0.0f;
    f32 horizonBlend = 0.0f;
    u32 sceneProbeCount = 0;
    u32 activeProbeCount = 0;
    u32 sceneEligibleProbeCount = 0;
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
    u32 capturedSceneRequestedCount = 0;
    u32 capturedScenePlaceholderAllocatedCount = 0;
    u32 capturedScenePlaceholderReadyCount = 0;
    u32 capturedSceneInvalidatedCount = 0;
    u32 capturedSceneRefreshRequestedCount = 0;
    u32 forcedRefreshRequested = 0;
    u32 sceneDirtyRequested = 0;
    u32 authoredCubemapLoadedCount = 0;
    u32 authoredCubemapMissingCount = 0;
    u32 authoredCubemapLoadFailedCount = 0;
    u32 authoredCubemapUploadCount = 0;
    u32 authoredCubemapSixFaceLoadedCount = 0;
    u32 authoredCubemapEquirectangularLoadedCount = 0;
    u32 authoredCubemapEquirectangularConversionCount = 0;
    u32 authoredCubemapHdrLoadedCount = 0;
    u32 authoredCubemapPrefilteredLoadedCount = 0;
    u32 authoredCubemapPrefilteredUploadCount = 0;
    u32 authoredCubemapCacheHitCount = 0;
    u32 authoredCubemapReloadCount = 0;
    u32 authoredCubemapRefreshCheckCount = 0;
    u32 authoredCubemapFaceSize = 0;
    u32 authoredCubemapMipCount = 0;
    VkFormat authoredCubemapFormat = VK_FORMAT_UNDEFINED;
    u32 authoredCubemapSourceType = 0;
    u32 authoredCubemapHdr = 0;
    u32 authoredCubemapPrefiltered = 0;
    u32 authoredCubemapGeneratedMipCount = 0;
    u32 authoredCubemapPrefilterSampleCount = 0;
    u32 authoredCubemapPrefilterMode = 0;
    u32 authoredCubemapFilterQuality = 1;
    u32 authoredCubemapSeamAwareFiltering = 0;
    u32 authoredCubemapIrradianceReadyCount = 0;
    u32 authoredCubemapIrradianceApplied = 0;
    f32 authoredCubemapIrradianceR = 0.0f;
    f32 authoredCubemapIrradianceG = 0.0f;
    f32 authoredCubemapIrradianceB = 0.0f;
    u32 authoredCubemapDiffuseLobesReadyCount = 0;
    u32 authoredCubemapDiffuseLobesApplied = 0;
    u32 authoredCubemapDiffuseLobeCount = 0;
    u32 selectedDiffuseLobeReadyMask = 0;
    f32 authoredCubemapDiffuseLobeEnergy = 0.0f;
    u32 droppedProbeCount = 0;
    i32 selectedProbeIndex = -1;
    u32 selectedProbeMask = 0;
    u32 selectedBoxProjectionMask = 0;
    u32 selectedSceneOwnedMask = 0;
    u32 selectedPositiveInfluenceMask = 0;
    u32 blendWeightNormalizationFallbackCount = 0;
    std::array<i32, kMaxFrameReflectionProbes> selectedProbeIndices{};
    std::array<i32, kMaxFrameReflectionProbes> selectedCaptureSlots{};
    std::array<u32, kMaxFrameReflectionProbes> selectedCaptureSourceTypes{};
    std::array<u32, kMaxFrameReflectionProbes> selectedCaptureFallbackReasons{};
    std::array<u32, kMaxFrameReflectionProbes> selectedRefreshPolicies{};
    std::array<u32, kMaxFrameReflectionProbes> selectedCapturedScenePlaceholderReady{};
    std::array<u32, kMaxFrameReflectionProbes> selectedCapturedSceneInvalidated{};
    std::array<u32, kMaxFrameReflectionProbes> selectedAuthoredAssetHashes{};
    f32 maxBlendWeight = 0.0f;
    f32 totalBlendWeight = 0.0f;
    f32 normalizedBlendWeightSum = 0.0f;
    std::array<f32, kMaxFrameReflectionProbes> selectedBlendWeights{};
    std::array<f32, kMaxFrameReflectionProbes> selectedNormalizedBlendWeights{};
    u32 multiBlendEnabled = 0;
    u32 localEnabled = 0;
    u32 localSceneOwned = 0;
    f32 localRadius = 0.0f;
    f32 localBoxExtentX = 0.0f;
    f32 localBoxExtentY = 0.0f;
    f32 localBoxExtentZ = 0.0f;
    f32 localIntensity = 0.0f;
    f32 localBlendStrength = 0.0f;
    f32 localFalloff = 0.0f;
    u32 localCubemapAllocated = 0;
    u32 localCubemapFaceSize = 0;
    u32 localCubemapMipCount = 0;
    VkFormat localCubemapFormat = VK_FORMAT_UNDEFINED;
    u32 localCubemapDescriptorSetsBound = 0;
    u32 localCubemapShaderSamplingEnabled = 0;
    u32 localCubemapSourceType = 0;
    u32 captureSourceType = 0;
    u32 refreshPolicy = 0;
    u32 captureResourceReady = 0;
    u32 captureFallbackReason = 0;
    u32 captureDescriptorBound = 0;
    u32 boxProjectionEnabled = 0;
    u32 influenceMode = 0;
    u32 parallaxCorrectionEnabled = 0;
};

struct RendererHeightFogStats {
    u32 enabled = 0;
    f32 density = 0.0f;
    f32 heightFalloff = 0.0f;
    f32 startDistance = 0.0f;
    f32 maxOpacity = 0.0f;
};

struct RendererPostProcessStats {
    u32 bloomEnabled = 0;
    f32 bloomIntensity = 0.0f;
    f32 bloomThreshold = 0.0f;
    f32 bloomRadiusPixels = 0.0f;
    u32 bloomPyramidEnabled = 0;
    u32 bloomPyramidMipCount = 0;
    u32 bloomPyramidFallbacks = 0;
    u32 toneMappingEnabled = 1;
    u32 toneMapMode = 0;
    f32 exposure = 1.0f;
    f32 toneMapWhitePoint = 4.0f;
    u32 autoExposureEnabled = 0;
    f32 autoExposureTargetLuminance = 0.18f;
    f32 autoExposureMin = 0.25f;
    f32 autoExposureMax = 4.0f;
    f32 autoExposureAdaptation = 1.0f;
    u32 autoExposureHistogramEnabled = 0;
    u32 autoExposureHistoryValid = 0;
    u32 autoExposureFallbacks = 0;
    f32 autoExposureGpuExposure = 1.0f;
    f32 autoExposureGpuTargetExposure = 1.0f;
    f32 autoExposureGpuAverageLuminance = 1.0f;
    u32 colorGradingEnabled = 0;
    f32 colorGradingSaturation = 1.0f;
    f32 colorGradingContrast = 1.0f;
    f32 colorGradingGamma = 1.0f;
    u32 colorGradingLutEnabled = 0;
    u32 colorGradingLutSize = 0;
    f32 colorGradingLutStrength = 0.0f;
    u32 colorGradingLutFallbacks = 0;
    u32 sharpeningEnabled = 0;
    f32 sharpeningStrength = 0.0f;
    f32 sharpeningRadiusPixels = 0.0f;
};

struct RendererBindStats {
    u32 mainMaterialBinds = 0;
    u32 mainMeshBinds = 0;
    u32 gBufferMaterialBinds = 0;
    u32 gBufferMeshBinds = 0;
    u32 deferredLightingDraws = 0;
    u32 deferredLightingFrameBinds = 0;
    u32 deferredLightingGBufferBinds = 0;
    u32 deferredPbrDebugDraws = 0;
    u32 deferredPbrDebugFrameBinds = 0;
    u32 deferredPbrDebugGBufferBinds = 0;
    u32 hdrCompositeDraws = 0;
    u32 hdrCompositeFrameBinds = 0;
    u32 hdrCompositeTextureBinds = 0;
    u32 gBufferDebugDraws = 0;
    u32 gBufferDebugFrameBinds = 0;
    u32 gBufferDebugTextureBinds = 0;
    u32 deferredShadowDebugDraws = 0;
    u32 deferredShadowDebugFrameBinds = 0;
    u32 deferredShadowDebugTextureBinds = 0;
    u32 shadowCascadeDebugDraws = 0;
    u32 shadowCascadeDebugFrameBinds = 0;
    u32 shadowCascadeDebugTextureBinds = 0;
    u32 localShadowAtlasDebugDraws = 0;
    u32 localShadowAtlasDebugFrameBinds = 0;
    u32 localShadowAtlasDebugTextureBinds = 0;
    u32 localShadowVisibilityDebugDraws = 0;
    u32 localShadowVisibilityDebugFrameBinds = 0;
    u32 localShadowVisibilityDebugTextureBinds = 0;
    u32 localShadowFaceDebugDraws = 0;
    u32 localShadowFaceDebugFrameBinds = 0;
    u32 localShadowFaceDebugTextureBinds = 0;
    u32 contactShadowDebugDraws = 0;
    u32 contactShadowDebugFrameBinds = 0;
    u32 contactShadowDebugGBufferBinds = 0;
    u32 ssaoDebugDraws = 0;
    u32 ssaoDebugFrameBinds = 0;
    u32 ssaoDebugGBufferBinds = 0;
    u32 ssrDebugDraws = 0;
    u32 ssrDebugFrameBinds = 0;
    u32 ssrDebugGBufferBinds = 0;
    u32 reflectionProbeDebugDraws = 0;
    u32 reflectionProbeDebugFrameBinds = 0;
    u32 reflectionProbeDebugGBufferBinds = 0;
    u32 heightFogDebugDraws = 0;
    u32 heightFogDebugFrameBinds = 0;
    u32 heightFogDebugGBufferBinds = 0;
    u32 probeGridDebugDraws = 0;
    u32 probeGridDebugFrameBinds = 0;
    u32 probeGridDebugGBufferBinds = 0;
    u32 probeGridCellDebugDraws = 0;
    u32 probeGridCellDebugFrameBinds = 0;
    u32 probeGridCellDebugGBufferBinds = 0;
    u32 bloomDebugDraws = 0;
    u32 bloomDebugFrameBinds = 0;
    u32 bloomDebugTextureBinds = 0;
    u32 bloomDownsampleDraws = 0;
    u32 bloomDownsampleFrameBinds = 0;
    u32 bloomDownsampleTextureBinds = 0;
    u32 bloomUpsampleDraws = 0;
    u32 bloomUpsampleFrameBinds = 0;
    u32 bloomUpsampleTextureBinds = 0;
    u32 toneMappingDebugDraws = 0;
    u32 toneMappingDebugFrameBinds = 0;
    u32 toneMappingDebugTextureBinds = 0;
    u32 autoExposureDebugDraws = 0;
    u32 autoExposureDebugFrameBinds = 0;
    u32 autoExposureDebugTextureBinds = 0;
    u32 colorGradingDebugDraws = 0;
    u32 colorGradingDebugFrameBinds = 0;
    u32 colorGradingDebugTextureBinds = 0;
    u32 sharpeningDebugDraws = 0;
    u32 sharpeningDebugFrameBinds = 0;
    u32 sharpeningDebugTextureBinds = 0;
    u32 lightTileCullComputeDispatches = 0;
    u32 lightTileCullComputeFrameBinds = 0;
    u32 lightTileCullComputeGroupsX = 0;
    u32 lightTileCullComputeGroupsY = 0;
    u32 autoExposureHistogramDispatches = 0;
    u32 autoExposureHistogramFrameBinds = 0;
    u32 autoExposureHistogramTextureBinds = 0;
    u32 autoExposureHistogramGroupsX = 0;
    u32 autoExposureHistogramGroupsY = 0;
    u32 depthCopyOps = 0;
    u32 depthPrefillDraws = 0;
    u32 depthPrefillMeshBinds = 0;
    u32 weightedTranslucencyClearPasses = 0;
    u32 weightedTranslucencyDraws = 0;
    u32 weightedTranslucencySharedLightListDraws = 0;
    u32 weightedTranslucencyShadowReadyDraws = 0;
    u32 weightedTranslucencyMaterialBinds = 0;
    u32 weightedTranslucencyMeshBinds = 0;
    u32 weightedTranslucencyResolveDraws = 0;
    u32 weightedTranslucencyResolveFrameBinds = 0;
    u32 weightedTranslucencyResolveTextureBinds = 0;
    u32 weightedTranslucencyDebugDraws = 0;
    u32 weightedTranslucencyDebugFrameBinds = 0;
    u32 weightedTranslucencyDebugTextureBinds = 0;
    u32 weightedTranslucencyAlphaReferenceMismatchDraws = 0;
    u32 forwardResidualAlphaReferenceEnabled = 0;
    u32 forwardResidualDraws = 0;
    u32 forwardResidualFrameBinds = 0;
    u32 forwardResidualSharedLightListDraws = 0;
    u32 forwardResidualMaterialBinds = 0;
    u32 forwardResidualMeshBinds = 0;
    u32 overlayMaterialBinds = 0;
    u32 overlayMeshBinds = 0;
    u32 shadowMeshBinds = 0;
    u32 shadowCascadeAtlasPasses = 0;
    u32 shadowCascadeAtlasDraws = 0;
    u32 shadowCascadeAtlasMeshBinds = 0;
    u32 localShadowAtlasPasses = 0;
    u32 localShadowAtlasDraws = 0;
    u32 localShadowAtlasMeshBinds = 0;
    u32 localShadowResolveEnabled = 0;
    u32 shadowCascadeBufferUpdates = 0;
    u32 localShadowBufferUpdates = 0;
    u32 frameLightConstantUpdates = 0;
    u32 frameLightBufferUpdates = 0;
    u32 frameLightTotalCount = 0;
    u32 frameDirectionalLightCount = 0;
    u32 frameLocalLightCount = 0;
    u32 frameRectLightCount = 0;
    u32 frameLightTileSize = 0;
    u32 frameLightTileCountX = 0;
    u32 frameLightTileCountY = 0;
    u32 frameLightTileCount = 0;
    u32 frameLightTileAssignments = 0;
    u32 frameLightTileAssignmentCapacity = 0;
    u32 frameLightTileOverflowAssignments = 0;
    u32 frameLightTileOverflowCapacity = 0;
    u32 frameLightTileOverflowTiles = 0;
    u32 frameLightTileOverflowDropped = 0;
    u32 frameLightTileAssignmentFallbacks = 0;
    u32 frameLightTileGpuReadbackValid = 0;
    u32 frameLightTileGpuSaturatedTiles = 0;
    u32 frameLightTileGpuMaxCandidates = 0;
    u64 frameLightTileGpuRawCandidates = 0;
    u32 frameLightTileGpuOverflowTiles = 0;
    u32 frameLightTileGpuOverflowDroppedTiles = 0;
    u32 frameLightTileGpuOverflowStored = 0;
    u32 frameLightTileGpuOverflowDropped = 0;
    u32 frameMaterialBufferUpdates = 0;
    u32 frameMaterialCount = 0;
    u32 frameMaterialCapacity = 0;
    u32 frameMaterialOverflowCount = 0;
    u32 frameMaterialOpaqueCount = 0;
    u32 frameMaterialTransparentCount = 0;
    u32 frameMaterialForwardSpecialCount = 0;
    u32 frameMaterialEmissiveHintCount = 0;
    u32 frameMaterialSpecularHintCount = 0;
    u32 frameMaterialSpecularTextureCount = 0;
    u32 frameMaterialAlphaMaskCount = 0;
    u32 frameMaterialAlphaBlendCount = 0;
    u32 frameMaterialUvTransformCount = 0;
    u32 frameMaterialDoubleSidedCount = 0;
    u32 frameMaterialClearcoatCount = 0;
    u32 frameMaterialClearcoatTextureCount = 0;
    u32 frameMaterialClearcoatRoughnessTextureCount = 0;
    u32 frameMaterialTransmissionCount = 0;
    u32 frameMaterialTransmissionTextureCount = 0;
    u32 frameMaterialVolumeCount = 0;
    u32 frameMaterialOpacityTextureCount = 0;
    u32 frameMaterialTexturedCount = 0;
    u32 mainInstancedDraws = 0;
    u32 mainInstancedInstances = 0;
    u32 mainInstanceBufferUploads = 0;
    u32 mainInstanceBufferUploadSkips = 0;
    u32 pushConstantUpdates = 0;
    u64 pushConstantBytes = 0;
};

struct RendererGpuStats {
    bool available = false;
    f32 shadowMs = 0.0f;
    f32 mainMs = 0.0f;
    f32 overlayMs = 0.0f;
    f32 imguiMs = 0.0f;
    f32 totalRecordedMs = 0.0f;
};

enum class RendererTemporalHistoryResetReason : u32 {
    None = 0,
    FirstFrame = 1,
    ExtentChanged = 2,
    MatricesUnavailable = 3,
    Forced = 4
};

enum class RendererTaaFallbackReason : u32 {
    None = 0,
    Disabled = 1,
    CompositeUnavailable = 2,
    HistoryInvalid = 3,
    HistoryColorCold = 4,
    VelocityUnavailable = 5
};

struct RendererTemporalStats {
    u32 velocityTargetAllocated = 0;
    VkFormat velocityFormat = VK_FORMAT_UNDEFINED;
    u32 velocityCameraMotionEnabled = 0;
    u32 velocityCameraMotionReady = 0;
    u32 velocityObjectMotionReady = 0;
    u32 velocityMaterialAuxTargetAllocated = 0;
    VkFormat velocityMaterialAuxFormat = VK_FORMAT_UNDEFINED;
    u32 velocityMaterialAuxMigrated = 0;
    u32 historyValid = 0;
    u32 historyReset = 0;
    u32 historyResetReason = 0;
    u32 jitterEnabled = 0;
    u32 jitterApplied = 0;
    u32 jitterSequenceIndex = 0;
    f32 jitterPixelsX = 0.0f;
    f32 jitterPixelsY = 0.0f;
    f32 jitterUvX = 0.0f;
    f32 jitterUvY = 0.0f;
    u32 taaResolveConfigured = 0;
    u32 taaResolveEnabled = 0;
    u32 taaHistoryColorTargetAllocated = 0;
    VkFormat taaHistoryColorFormat = VK_FORMAT_UNDEFINED;
    u32 taaHistoryColorReady = 0;
    u32 taaHistoryColorCopies = 0;
    f32 taaHistoryWeight = 0.0f;
    u32 taaVelocityReprojectionEnabled = 0;
    u32 taaFallbackReason = 0;
    u32 taaDebugViewEnabled = 0;
};

struct RendererStats {
    RendererCpuStats cpu;
    RendererDrawStats draw;
    RendererShadowCascadeStats shadowCascades;
    RendererLocalShadowAtlasStats localShadowAtlas;
    RendererWeightedTranslucencyStats weightedTranslucency;
    RendererSsaoStats ssao;
    RendererSsrStats ssr;
    RendererIblStats ibl;
    RendererProbeGridStats probeGrid;
    RendererReflectionProbeStats reflectionProbe;
    RendererHeightFogStats heightFog;
    RendererPostProcessStats postProcess;
    RendererBindStats binds;
    RendererGpuStats gpu;
    RendererTemporalStats temporal;
    RenderFrameGraphPlan frameGraph = BuildAAAFrameGraphBlueprint();
};

}
