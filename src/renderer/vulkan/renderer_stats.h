#pragma once

#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/shadow_settings.h"
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

struct RendererReflectionProbeStats {
    u32 fallbackEnabled = 0;
    f32 diffuseIntensity = 0.0f;
    f32 specularIntensity = 0.0f;
    f32 horizonBlend = 0.0f;
    u32 localEnabled = 0;
    f32 localRadius = 0.0f;
    f32 localIntensity = 0.0f;
    f32 localBlendStrength = 0.0f;
    f32 localFalloff = 0.0f;
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
    u32 lightTileCullComputeDispatches = 0;
    u32 lightTileCullComputeFrameBinds = 0;
    u32 lightTileCullComputeGroupsX = 0;
    u32 lightTileCullComputeGroupsY = 0;
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

struct RendererStats {
    RendererCpuStats cpu;
    RendererDrawStats draw;
    RendererShadowCascadeStats shadowCascades;
    RendererLocalShadowAtlasStats localShadowAtlas;
    RendererWeightedTranslucencyStats weightedTranslucency;
    RendererSsaoStats ssao;
    RendererSsrStats ssr;
    RendererReflectionProbeStats reflectionProbe;
    RendererBindStats binds;
    RendererGpuStats gpu;
    RenderFrameGraphPlan frameGraph = BuildAAAFrameGraphBlueprint();
};

}
