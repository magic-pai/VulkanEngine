#include "renderer/vulkan/features/reflection_probe_fallback_feature.h"

#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/shadow_settings.h"

#include <algorithm>

namespace se {

void VulkanReflectionProbeFallbackFeature::AppendFrameGraph(
    const VulkanRenderFeatureFrameGraphContext& context
) const {
    const RendererReflectionProbeStats& reflectionProbe =
        context.stats.reflectionProbe;
    if (reflectionProbe.fallbackEnabled > 0 &&
        context.renderer.has3DMainPass) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "GlobalReflectionFallback",
            "frame reflection-probe fallback controls, sky directional basis",
            "diffuse and specular fallback radiance",
            "Procedural global reflection fallback used by deferred, forward, and WBOIT lighting until imported UE reflection captures and local probes are available."
        );
    }
    if (reflectionProbe.localEnabled > 0 &&
        reflectionProbe.fallbackEnabled > 0 &&
        context.renderer.has3DMainPass) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "LocalReflectionProbeBlend",
            "frame local reflection probe center/radius/color controls",
            "world-position weighted reflection fallback blend",
            "First local reflection-probe influence volume blended into deferred, forward, and WBOIT environment lighting before real cubemap capture is added."
        );
    }
}

void VulkanReflectionProbeFallbackFeature::WriteStats(
    const VulkanRenderFeatureStatsContext& context
) const {
    const VulkanShadowSettings& settings = context.renderer.shadowSettings;
    RendererReflectionProbeStats& reflectionProbe =
        context.stats.reflectionProbe;

    reflectionProbe.fallbackEnabled =
        settings.reflectionProbeFallbackEnabled ? 1u : 0u;
    reflectionProbe.diffuseIntensity =
        std::clamp(settings.reflectionProbeDiffuseIntensity, 0.0f, 4.0f);
    reflectionProbe.specularIntensity =
        std::clamp(settings.reflectionProbeSpecularIntensity, 0.0f, 4.0f);
    reflectionProbe.horizonBlend =
        std::clamp(settings.reflectionProbeHorizonBlend, 0.0f, 1.0f);
    reflectionProbe.localEnabled =
        settings.reflectionProbeFallbackEnabled &&
            settings.localReflectionProbeEnabled
            ? 1u
            : 0u;
    reflectionProbe.localRadius =
        std::clamp(settings.localReflectionProbeRadius, 0.01f, 256.0f);
    reflectionProbe.localIntensity =
        std::clamp(settings.localReflectionProbeIntensity, 0.0f, 4.0f);
    reflectionProbe.localBlendStrength =
        std::clamp(settings.localReflectionProbeBlendStrength, 0.0f, 1.0f);
    reflectionProbe.localFalloff =
        std::clamp(settings.localReflectionProbeFalloff, 0.25f, 8.0f);
}

}
