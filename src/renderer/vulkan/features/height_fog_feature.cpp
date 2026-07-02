#include "renderer/vulkan/features/height_fog_feature.h"

#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/shadow_settings.h"

#include <algorithm>

namespace se {

void VulkanHeightFogFeature::AppendFrameGraph(
    const VulkanRenderFeatureFrameGraphContext& context
) const {
    if (context.stage != VulkanRenderFeatureFrameGraphStage::Lighting) {
        return;
    }
    if (context.stats.heightFog.enabled > 0 &&
        context.renderer.has3DMainPass) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Volumetrics,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "HeightFogIntegrated",
            "",
            "",
            "First analytic height/distance fog tier integrated into deferred, legacy forward, and WBOIT shading before a full volumetric fog volume is added."
        );
    }
}

void VulkanHeightFogFeature::WriteStats(
    const VulkanRenderFeatureStatsContext& context
) const {
    const VulkanShadowSettings& settings = context.renderer.shadowSettings;
    RendererHeightFogStats& heightFog = context.stats.heightFog;

    heightFog.density =
        std::clamp(settings.heightFogDensity, 0.0f, 1.0f);
    heightFog.heightFalloff =
        std::clamp(settings.heightFogHeightFalloff, 0.0f, 2.0f);
    heightFog.startDistance =
        std::clamp(settings.heightFogStartDistance, 0.0f, 1000.0f);
    heightFog.maxOpacity =
        std::clamp(settings.heightFogMaxOpacity, 0.0f, 1.0f);
    heightFog.enabled =
        settings.heightFogEnabled &&
            heightFog.density > 0.0001f &&
            heightFog.maxOpacity > 0.0001f
            ? 1u
            : 0u;
}

}
