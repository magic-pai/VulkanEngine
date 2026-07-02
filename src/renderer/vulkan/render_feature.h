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
