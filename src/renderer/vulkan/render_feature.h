#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

struct RenderFrameGraphPlan;
struct RendererStats;
struct VulkanRenderDebugSettings;
struct VulkanShadowSettings;

enum class VulkanRenderFeatureFrameGraphStage {
    Lighting,
    PostProcess
};

struct VulkanRenderFeatureContext {
    const VulkanShadowSettings& shadowSettings;
    const VulkanRenderDebugSettings& debugSettings;
    bool has3DMainPass = false;
    bool deferredLightingAvailable = false;
    bool hdrCompositeAvailable = false;
    u32 reflectionProbeCount = 0;
    u32 activeReflectionProbeCount = 0;
    bool sceneReflectionProbeOwned = false;
    bool sceneReflectionProbeCubemapSamplingEnabled = false;
    u32 reflectionProbeCaptureSourceType = 0;
    u32 reflectionProbeCaptureFallbackReason = 0;
    bool ssrDepthPyramidAllocated = false;
    bool ssrHiZDescriptorSetsReady = false;
    bool ssrHiZBuildPipelineAvailable = false;
    bool ssrSceneColorHistoryDescriptorReady = false;
    u32 ssrDepthPyramidWidth = 0;
    u32 ssrDepthPyramidHeight = 0;
    u32 ssrDepthPyramidMipCount = 0;
    u32 ssrDepthPyramidImageCount = 0;
    VkFormat ssrDepthPyramidFormat = VK_FORMAT_UNDEFINED;
    bool ssrReconstructionTargetsAllocated = false;
    bool ssrReconstructionDescriptorSetsReady = false;
    bool ssrReconstructionPipelinesAvailable = false;
    u32 ssrReconstructionImageCount = 0;
    bool ffxSssrConstantsResourcesReady = false;
    bool ffxSssrConstantsDescriptorSetsReady = false;
    bool ffxSssrPrepareIndirectArgsResourcesReady = false;
    bool ffxSssrPrepareIndirectArgsDescriptorSetsReady = false;
    bool ffxSssrPrepareIndirectArgsPipelineReady = false;
    u64 ffxSssrPrepareIndirectArgsBufferBytes = 0;
    bool ffxSssrClassifyTilesResourcesReady = false;
    bool ffxSssrClassifyTilesDescriptorSetsReady = false;
    bool ffxSssrClassifyTilesPipelineReady = false;
    bool ffxSssrClassifyTilesInputContractReady = false;
    u32 ffxSssrClassifyTilesWidth = 0;
    u32 ffxSssrClassifyTilesHeight = 0;
    u32 ffxSssrClassifyTilesGroupCountX = 0;
    u32 ffxSssrClassifyTilesGroupCountY = 0;
    u32 ffxSssrClassifyTilesRayListCapacity = 0;
    u32 ffxSssrClassifyTilesDenoiserTileListCapacity = 0;
    u64 ffxSssrClassifyTilesMemoryBytes = 0;
    bool ffxSssrBlueNoiseResourcesReady = false;
    bool ffxSssrBlueNoiseDescriptorSetsReady = false;
    bool ffxSssrBlueNoisePipelineReady = false;
    u32 ffxSssrBlueNoiseWidth = 0;
    u32 ffxSssrBlueNoiseHeight = 0;
    u32 ffxSssrBlueNoiseGroupCountX = 0;
    u32 ffxSssrBlueNoiseGroupCountY = 0;
    u32 ffxSssrBlueNoiseSobolEntryCount = 0;
    u32 ffxSssrBlueNoiseRankingTileEntryCount = 0;
    u32 ffxSssrBlueNoiseScramblingTileEntryCount = 0;
    u64 ffxSssrBlueNoiseMemoryBytes = 0;
    bool ffxSssrIntersectResourcesReady = false;
    bool ffxSssrIntersectDescriptorSetsReady = false;
    bool ffxSssrIntersectPipelineReady = false;
    bool ffxSssrIntersectInputContractReady = false;
    u32 ffxSssrIntersectWidth = 0;
    u32 ffxSssrIntersectHeight = 0;
    u32 ffxSssrIntersectDepthPyramidMipCount = 0;
    bool ffxSssrReprojectResourcesReady = false;
    bool ffxSssrReprojectDescriptorSetsReady = false;
    bool ffxSssrReprojectPipelineReady = false;
    bool ffxSssrReprojectInputContractReady = false;
    u32 ffxSssrReprojectWidth = 0;
    u32 ffxSssrReprojectHeight = 0;
    u32 ffxSssrReprojectAverageWidth = 0;
    u32 ffxSssrReprojectAverageHeight = 0;
    bool ffxSssrReprojectHistoryReady = false;
    u32 ffxSssrReprojectHistorySource = 0;
    u64 ffxSssrReprojectMemoryBytes = 0;
    u32 ffxSssrReprojectIndirectArgsOffsetBytes = 0;
    bool ffxSssrPrefilterResourcesReady = false;
    bool ffxSssrPrefilterDescriptorSetsReady = false;
    bool ffxSssrPrefilterPipelineReady = false;
    bool ffxSssrPrefilterInputContractReady = false;
    u32 ffxSssrPrefilterWidth = 0;
    u32 ffxSssrPrefilterHeight = 0;
    u64 ffxSssrPrefilterMemoryBytes = 0;
    u32 ffxSssrPrefilterIndirectArgsOffsetBytes = 0;
};

struct VulkanRenderFeatureFrameGraphContext {
    RenderFrameGraphPlan& plan;
    const VulkanRenderFeatureContext& renderer;
    const RendererStats& stats;
    VulkanRenderFeatureFrameGraphStage stage =
        VulkanRenderFeatureFrameGraphStage::Lighting;
};

struct VulkanRenderFeatureStatsContext {
    RendererStats& stats;
    const VulkanRenderFeatureContext& renderer;
};

class VulkanRenderFeature {
public:
    virtual ~VulkanRenderFeature() = default;

    virtual void AppendFrameGraph(
        const VulkanRenderFeatureFrameGraphContext& context
    ) const = 0;
    virtual void WriteStats(
        const VulkanRenderFeatureStatsContext& context
    ) const = 0;
};

}
