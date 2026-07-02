#include "renderer/vulkan/features/reflection_probe_fallback_feature.h"

#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/shadow_settings.h"

#include <algorithm>

namespace se {

void VulkanReflectionProbeFallbackFeature::AppendFrameGraph(
    const VulkanRenderFeatureFrameGraphContext& context
) const {
    if (context.stage != VulkanRenderFeatureFrameGraphStage::Lighting) {
        return;
    }
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
            "",
            "",
            "Procedural global reflection fallback used by deferred, forward, and WBOIT lighting until imported UE reflection captures and local probes are available."
        );
    }
    if (reflectionProbe.localEnabled > 0 &&
        reflectionProbe.fallbackEnabled > 0 &&
        context.renderer.has3DMainPass) {
        const bool sceneCubemapSampling =
            context.renderer.sceneReflectionProbeOwned &&
            context.renderer.sceneReflectionProbeCubemapSamplingEnabled &&
            reflectionProbe.localCubemapShaderSamplingEnabled > 0;
        if (context.renderer.sceneReflectionProbeOwned) {
            AppendRenderFrameGraphPass(
                context.plan,
                RenderFramePassKind::Reflections,
                RenderFramePassStatus::Active,
                RenderFramePassQueue::Graphics,
                "SceneReflectionProbeSelection",
                "SceneReflectionProbes",
                "SceneReflectionProbeSelection",
                "Selects the best scene-owned reflection probe for the current camera and records dropped probe diagnostics before multi-probe blending is added."
            );
        }
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "ReflectionCaptureSourceResolve",
            reflectionProbe.captureResourceReady > 0
                ? context.renderer.sceneReflectionProbeOwned
                    ? "SceneReflectionProbeSelection, ReflectionCaptureSource, SceneReflectionProbeCubemap"
                    : "ReflectionCaptureSource, SceneReflectionProbeCubemap"
                : context.renderer.sceneReflectionProbeOwned
                    ? "SceneReflectionProbeSelection, ReflectionCaptureSource"
                    : "ReflectionCaptureSource",
            "",
            reflectionProbe.captureResourceReady > 0
                ? "Resolves the active reflection probe capture source to a renderer-owned cubemap descriptor."
                : "Selects the active reflection probe source and records the explicit fallback reason."
        );
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            sceneCubemapSampling
                ? "SceneReflectionProbeCubemapSample"
                : context.renderer.sceneReflectionProbeOwned
                ? "SceneReflectionProbeBlend"
                : "LocalReflectionProbeBlend",
            sceneCubemapSampling
                ? "SceneReflectionProbeSelection, ReflectionCaptureSource, SceneReflectionProbeCubemap, BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap"
                : context.renderer.sceneReflectionProbeOwned
                ? "SceneReflectionProbeSelection, ReflectionCaptureSource, BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap"
                : "ReflectionCaptureSource, BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap",
            "",
            sceneCubemapSampling
                ? "Scene-owned reflection probe samples a renderer-owned local cubemap in deferred, forward, and WBOIT environment lighting."
                : context.renderer.sceneReflectionProbeOwned
                ? "Scene-owned reflection probe influence volume blended into deferred, forward, and WBOIT environment lighting before real cubemap capture/import is added."
                : "Debug local reflection-probe influence volume blended into deferred, forward, and WBOIT environment lighting before real cubemap capture/import is added."
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
    reflectionProbe.sceneProbeCount = context.renderer.reflectionProbeCount;
    reflectionProbe.activeProbeCount =
        context.renderer.activeReflectionProbeCount;
    reflectionProbe.localSceneOwned =
        context.renderer.sceneReflectionProbeOwned ? 1u : 0u;
    reflectionProbe.localRadius =
        std::clamp(settings.localReflectionProbeRadius, 0.01f, 256.0f);
    reflectionProbe.localBoxExtentX = reflectionProbe.localRadius;
    reflectionProbe.localBoxExtentY = reflectionProbe.localRadius;
    reflectionProbe.localBoxExtentZ = reflectionProbe.localRadius;
    reflectionProbe.localIntensity =
        std::clamp(settings.localReflectionProbeIntensity, 0.0f, 4.0f);
    reflectionProbe.localBlendStrength =
        std::clamp(settings.localReflectionProbeBlendStrength, 0.0f, 1.0f);
    reflectionProbe.localFalloff =
        std::clamp(settings.localReflectionProbeFalloff, 0.25f, 8.0f);
}

}
