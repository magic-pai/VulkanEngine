#include "renderer/vulkan/features/ssao_feature.h"

#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/shadow_settings.h"

#include <algorithm>

namespace se {

void VulkanSsaoFeature::AppendFrameGraph(
    const VulkanRenderFeatureFrameGraphContext& context
) const {
    if (context.stats.ssao.enabled > 0 &&
        context.renderer.has3DMainPass &&
        context.renderer.deferredLightingAvailable) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::ScreenSpaceAmbientOcclusion,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "SSAOIntegrated",
            "GBufferNormalRoughness, SceneDepth, frame matrices",
            "deferred ambient occlusion factor",
            "First screen-space ambient occlusion tier integrated into deferred ambient with a debug view; a standalone AO target and temporal filter follow later."
        );
    }
}

void VulkanSsaoFeature::WriteStats(
    const VulkanRenderFeatureStatsContext& context
) const {
    const VulkanShadowSettings& settings = context.renderer.shadowSettings;
    RendererSsaoStats& ssao = context.stats.ssao;

    ssao.strength =
        std::clamp(settings.ssaoStrength, 0.0f, 1.0f);
    ssao.radius =
        std::clamp(settings.ssaoRadius, 0.0f, 8.0f);
    ssao.bias =
        std::clamp(settings.ssaoBias, 0.0f, 0.5f);
    ssao.sampleCount =
        std::clamp<u32>(settings.ssaoSampleCount, 0u, 16u);
    ssao.enabled =
        ssao.strength > 0.0001f &&
        ssao.radius > 0.0001f &&
        ssao.sampleCount > 0
            ? 1u
            : 0u;
}

}
