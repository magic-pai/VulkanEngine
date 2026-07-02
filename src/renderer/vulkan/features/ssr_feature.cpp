#include "renderer/vulkan/features/ssr_feature.h"

#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/shadow_settings.h"

#include <algorithm>

namespace se {

void VulkanSsrFeature::AppendFrameGraph(
    const VulkanRenderFeatureFrameGraphContext& context
) const {
    if (context.stage != VulkanRenderFeatureFrameGraphStage::Lighting) {
        return;
    }
    if (context.stats.ssr.colorResolveEnabled > 0) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "SSRIntegrated",
            "GBufferNormalRoughness, SceneDepth",
            "",
            "First screen-space reflection color tier integrated into deferred environment specular with a debug view; hierarchy, temporal accumulation, denoising, and probe fallback follow later."
        );
    }
}

void VulkanSsrFeature::WriteStats(
    const VulkanRenderFeatureStatsContext& context
) const {
    const VulkanShadowSettings& settings = context.renderer.shadowSettings;
    RendererSsrStats& ssr = context.stats.ssr;

    ssr.strength =
        std::clamp(settings.ssrStrength, 0.0f, 1.0f);
    ssr.rayLength =
        std::clamp(settings.ssrRayLength, 0.0f, 64.0f);
    ssr.thickness =
        std::clamp(settings.ssrThickness, 0.0f, 0.5f);
    ssr.stepCount =
        std::clamp<u32>(settings.ssrStepCount, 0u, 32u);
    ssr.enabled =
        ssr.strength > 0.0001f &&
        ssr.rayLength > 0.0001f &&
        ssr.stepCount > 0
            ? 1u
            : 0u;
    ssr.colorResolveEnabled =
        ssr.enabled > 0 &&
        context.renderer.has3DMainPass &&
        context.renderer.deferredLightingAvailable
            ? 1u
            : 0u;
}

}
