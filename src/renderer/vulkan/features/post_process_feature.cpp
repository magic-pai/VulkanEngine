#include "renderer/vulkan/features/post_process_feature.h"

#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/render_debug_settings.h"
#include "renderer/vulkan/renderer_stats.h"

#include <algorithm>

namespace se {

namespace {

std::string_view HdrCompositeReads(const RendererPostProcessStats&) {
    return "HDRSceneColor";
}

} // namespace

void VulkanPostProcessFeature::AppendFrameGraph(
    const VulkanRenderFeatureFrameGraphContext& context
) const {
    if (context.stage != VulkanRenderFeatureFrameGraphStage::PostProcess ||
        !context.renderer.hdrCompositeAvailable) {
        return;
    }

    const RendererPostProcessStats& postProcess = context.stats.postProcess;
    if (postProcess.autoExposureEnabled > 0) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "AutoExposureIntegrated",
            "HDRSceneColor",
            "",
            "First sampled auto-exposure tier inside HDR composite; GPU histogram and eye-adaptation history can replace it later."
        );
    }
    if (postProcess.toneMappingEnabled > 0) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "ToneMappingIntegrated",
            "HDRSceneColor",
            "",
            "First explicit tone-map control tier inside HDR composite; a dedicated post graph pass can replace it later."
        );
    }
    if (postProcess.bloomEnabled > 0) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "BloomIntegrated",
            "HDRSceneColor",
            "HDRSceneColor",
            "First screen-space bloom tier sampled inside HDR composite; a downsample/upsample bloom pyramid can replace it later."
        );
    }
    if (postProcess.colorGradingEnabled > 0) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "ColorGradingIntegrated",
            "HDRSceneColor",
            "",
            "First display-referred color grading tier inside HDR composite; LUT grading can replace it later."
        );
    }
    if (postProcess.sharpeningEnabled > 0) {
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "SharpeningIntegrated",
            "HDRSceneColor",
            "",
            "First LDR unsharp-mask sharpening tier inside HDR composite; CAS/RCAS can replace it later."
        );
    }
    AppendRenderFrameGraphPass(
        context.plan,
        RenderFramePassKind::PostProcess,
        RenderFramePassStatus::Active,
        RenderFramePassQueue::Graphics,
        "HDRComposite",
        HdrCompositeReads(postProcess),
        "SwapchainColor",
        "Debug-visible HDR composite path for deferred output before it becomes the default present path."
    );
}

void VulkanPostProcessFeature::WriteStats(
    const VulkanRenderFeatureStatsContext& context
) const {
    const VulkanRenderDebugSettings& settings = context.renderer.debugSettings;
    RendererPostProcessStats& postProcess = context.stats.postProcess;

    postProcess.bloomIntensity =
        std::clamp(settings.bloomIntensity, 0.0f, 4.0f);
    postProcess.bloomThreshold =
        std::clamp(settings.bloomThreshold, 0.0f, 16.0f);
    postProcess.bloomRadiusPixels =
        std::clamp(settings.bloomRadiusPixels, 0.0f, 24.0f);
    postProcess.bloomEnabled =
        settings.bloomEnabled &&
            postProcess.bloomIntensity > 0.0001f &&
            postProcess.bloomRadiusPixels > 0.0001f
            ? 1u
            : 0u;
    postProcess.toneMapMode =
        std::clamp<u32>(settings.toneMapMode, 0u, 2u);
    postProcess.exposure =
        std::clamp(settings.exposure, 0.001f, 32.0f);
    postProcess.toneMapWhitePoint =
        std::clamp(settings.toneMapWhitePoint, 0.1f, 64.0f);
    postProcess.toneMappingEnabled =
        postProcess.toneMapMode == 2u ? 0u : 1u;
    postProcess.autoExposureEnabled =
        settings.autoExposureEnabled ? 1u : 0u;
    postProcess.autoExposureTargetLuminance =
        std::clamp(settings.autoExposureTargetLuminance, 0.001f, 4.0f);
    postProcess.autoExposureMin =
        std::clamp(settings.autoExposureMin, 0.001f, 32.0f);
    postProcess.autoExposureMax =
        std::max(
            postProcess.autoExposureMin,
            std::clamp(settings.autoExposureMax, 0.001f, 32.0f)
        );
    postProcess.autoExposureAdaptation =
        std::clamp(settings.autoExposureAdaptation, 0.0f, 1.0f);
    postProcess.colorGradingSaturation =
        std::clamp(settings.colorGradingSaturation, 0.0f, 2.5f);
    postProcess.colorGradingContrast =
        std::clamp(settings.colorGradingContrast, 0.0f, 2.5f);
    postProcess.colorGradingGamma =
        std::clamp(settings.colorGradingGamma, 0.25f, 4.0f);
    postProcess.colorGradingEnabled =
        settings.colorGradingEnabled ? 1u : 0u;
    postProcess.sharpeningStrength =
        std::clamp(settings.sharpeningStrength, 0.0f, 2.0f);
    postProcess.sharpeningRadiusPixels =
        std::clamp(settings.sharpeningRadiusPixels, 0.0f, 4.0f);
    postProcess.sharpeningEnabled =
        settings.sharpeningEnabled &&
            postProcess.sharpeningStrength > 0.0001f &&
            postProcess.sharpeningRadiusPixels > 0.0001f
            ? 1u
            : 0u;
}

}
