#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

struct RenderFrameGraphPlan;
struct RendererStats;
struct VulkanShadowSettings;

struct VulkanRenderFeatureContext {
    const VulkanShadowSettings& shadowSettings;
    bool has3DMainPass = false;
    bool deferredLightingAvailable = false;
};

struct VulkanRenderFeatureFrameGraphContext {
    RenderFrameGraphPlan& plan;
    const VulkanRenderFeatureContext& renderer;
    const RendererStats& stats;
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
