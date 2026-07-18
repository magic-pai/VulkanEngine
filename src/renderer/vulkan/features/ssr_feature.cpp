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
        if (context.stats.ssr.reconstructionActive > 0) {
            AppendRenderFrameGraphPass(
                context.plan,
                RenderFramePassKind::Reflections,
                RenderFramePassStatus::Active,
                RenderFramePassQueue::Compute,
                "SSRTemporal",
                "HDRSceneColor, GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive, Velocity, SceneDepth, SSRRaw, SSRHistoryColor, SSRHistoryMetadata",
                "SSRHistoryColor, SSRHistoryMetadata",
                "Post-lighting SSR temporal accumulation samples the current HDR hit radiance and validates motion, previous-view depth, normal, roughness, and a motion-aware history lock."
            );
            AppendRenderFrameGraphPass(
                context.plan,
                RenderFramePassKind::Reflections,
                RenderFramePassStatus::Active,
                RenderFramePassQueue::Compute,
                "SSRSpatial",
                "GBufferNormalRoughness, SceneDepth, SSRRaw, SSRHistoryColor",
                "SSRResolved",
                "SSR spatial reconstruction applies edge-aware variance-clipped filtering, then writes a roughness-aware probe/IBL fallback confidence for the Deferred consumer."
            );
            if (context.stats.ssr.holeDiagnosticsActive > 0) {
                AppendRenderFrameGraphPass(
                    context.plan,
                    RenderFramePassKind::Reflections,
                    RenderFramePassStatus::Active,
                    RenderFramePassQueue::Compute,
                    "SSRHoleDiagnostics",
                    "SSRRaw, SSRHistoryColor, SSRResolved, LightTileDiagnostics",
                    "LightTileDiagnostics",
                    "Debug-only SSR hit topology and temporal/spatial reliability audit; no production image is modified."
                );
            }
            return;
        }
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            context.stats.ssr.hierarchicalActive > 0
                ? "SSRHiZTrace"
                : (context.stats.ssr.refinementEnabled > 0
                    ? "SSRRefinedFallbackTrace"
                    : "SSRFixedStepFallbackTrace"),
            context.stats.ssr.hierarchicalActive > 0
                ? (context.stats.ssr.sceneColorHistoryActive > 0
                    ? "GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive, Velocity, SceneDepth, SSRDepthPyramid, TemporalHistoryColor"
                    : "GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive, SceneDepth, SSRDepthPyramid")
                : (context.stats.ssr.sceneColorHistoryActive > 0
                    ? "GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive, Velocity, SceneDepth, TemporalHistoryColor"
                    : "GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive, SceneDepth"),
            "",
            context.stats.ssr.hierarchicalActive > 0
                ? (context.stats.ssr.sceneColorHistoryActive > 0
                    ? "Deferred SSR traverses the conservative depth pyramid, refines the crossing, velocity-reprojects the hit into completed HDR scene-color history, and blends low-confidence/rough receivers back to probe/IBL fallback."
                    : "Deferred SSR traverses the conservative per-frame depth pyramid from coarse cells to full-resolution crossings, then refines the hit; environment and probe reflection remain the explicit miss fallback.")
                : (context.stats.ssr.refinementEnabled > 0
                    ? "Deferred SSR uses the refined fixed-step fallback because Hi-Z is unavailable or disabled; environment and probe reflection remain the explicit miss fallback."
                    : "Deferred SSR uses the fixed-step diagnostic control path; environment and probe reflection remain the explicit miss fallback.")
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
    ssr.traceInputsReady =
        context.renderer.has3DMainPass && context.renderer.deferredLightingAvailable
            ? 1u
            : 0u;
    ssr.enabled =
        ssr.strength > 0.0001f &&
        ssr.rayLength > 0.0001f &&
        ssr.stepCount > 0
            ? 1u
            : 0u;
    ssr.colorResolveEnabled =
        ssr.enabled > 0 &&
        ssr.traceInputsReady > 0
            ? 1u
            : 0u;
    ssr.hierarchicalRequested =
        ssr.enabled > 0 && settings.ssrHiZEnabled ? 1u : 0u;
    ssr.depthPyramidAllocated = context.renderer.ssrDepthPyramidAllocated ? 1u : 0u;
    ssr.depthPyramidWidth = context.renderer.ssrDepthPyramidWidth;
    ssr.depthPyramidHeight = context.renderer.ssrDepthPyramidHeight;
    ssr.depthPyramidMipCount = context.renderer.ssrDepthPyramidMipCount;
    ssr.depthPyramidImageCount = context.renderer.ssrDepthPyramidImageCount;
    ssr.depthPyramidFormat = context.renderer.ssrDepthPyramidFormat;
    ssr.depthPyramidReady =
        ssr.depthPyramidAllocated > 0 &&
        ssr.depthPyramidWidth > 0 &&
        ssr.depthPyramidHeight > 0 &&
        ssr.depthPyramidMipCount > 1 &&
        ssr.depthPyramidImageCount > 0 &&
        ssr.depthPyramidFormat == VK_FORMAT_R32_SFLOAT &&
        context.renderer.ssrHiZDescriptorSetsReady &&
        context.renderer.ssrHiZBuildPipelineAvailable
            ? 1u
            : 0u;
    u64 mipPixelCount = 0;
    u32 mipWidth = ssr.depthPyramidWidth;
    u32 mipHeight = ssr.depthPyramidHeight;
    for (u32 mipIndex = 0; mipIndex < ssr.depthPyramidMipCount; ++mipIndex) {
        mipPixelCount += static_cast<u64>(mipWidth) * mipHeight;
        mipWidth = std::max(mipWidth >> 1u, 1u);
        mipHeight = std::max(mipHeight >> 1u, 1u);
    }
    ssr.depthPyramidMemoryBytes =
        mipPixelCount * sizeof(f32) * ssr.depthPyramidImageCount;
    ssr.hierarchicalActive =
        ssr.colorResolveEnabled > 0 &&
        ssr.hierarchicalRequested > 0 &&
        ssr.depthPyramidReady > 0
            ? 1u
            : 0u;
    if (ssr.hierarchicalActive > 0) {
        ssr.hierarchicalFallbackReason = 0u;
    } else if (ssr.hierarchicalRequested == 0u) {
        ssr.hierarchicalFallbackReason = 1u;
    } else if (ssr.colorResolveEnabled == 0u) {
        ssr.hierarchicalFallbackReason = 2u;
    } else if (ssr.depthPyramidAllocated == 0u ||
        ssr.depthPyramidWidth == 0u ||
        ssr.depthPyramidHeight == 0u ||
        ssr.depthPyramidMipCount <= 1u ||
        ssr.depthPyramidFormat != VK_FORMAT_R32_SFLOAT) {
        ssr.hierarchicalFallbackReason = 3u;
    } else if (!context.renderer.ssrHiZDescriptorSetsReady) {
        ssr.hierarchicalFallbackReason = 4u;
    } else {
        ssr.hierarchicalFallbackReason = 5u;
    }
    ssr.fixedStepFallbackActive =
        ssr.colorResolveEnabled > 0 && ssr.hierarchicalActive == 0u ? 1u : 0u;
    ssr.depthPyramidBuildDispatchCount =
        ssr.hierarchicalActive > 0 ? ssr.depthPyramidMipCount : 0u;
    ssr.depthPyramidGeneratedMipMask = ssr.hierarchicalActive == 0u
        ? 0ull
        : (ssr.depthPyramidMipCount >= 64u
            ? ~0ull
            : ((1ull << ssr.depthPyramidMipCount) - 1ull));
    ssr.traversalMaxMip =
        ssr.hierarchicalActive > 0 ? ssr.depthPyramidMipCount - 1u : 0u;
    ssr.refinementEnabled =
        ssr.colorResolveEnabled > 0 && settings.ssrRefinementEnabled
            ? 1u
            : 0u;
    ssr.refinementStepCount = ssr.refinementEnabled > 0 ? 4u : 0u;
    ssr.hitValidationRequested = settings.ssrHitValidationEnabled ? 1u : 0u;
    ssr.hitValidationActive =
        ssr.colorResolveEnabled > 0 && ssr.hitValidationRequested > 0 ? 1u : 0u;
    ssr.hitValidationContractVersion = ssr.hitValidationActive > 0 ? 1u : 0u;
    ssr.hitNormalValidationEnabled = ssr.hitValidationActive;
    // Validation checks both the hit surface and the receiving surface.
    ssr.hitFootprintTapCount = ssr.hitValidationActive > 0 ? 8u : 0u;
    ssr.signedDepthValidationEnabled = ssr.hitValidationActive;
    ssr.originBiasMinimumPixels = ssr.hitValidationActive > 0 ? 2.0f : 0.0f;
    ssr.originBiasMaximumPixels = ssr.hitValidationActive > 0 ? 6.0f : 0.0f;
    ssr.reconstructionRequested = ssr.colorResolveEnabled;
    ssr.reconstructionTargetsAllocated =
        context.renderer.ssrReconstructionTargetsAllocated ? 1u : 0u;
    ssr.reconstructionDescriptorSetsReady =
        context.renderer.ssrReconstructionDescriptorSetsReady ? 1u : 0u;
    ssr.reconstructionImageCount = context.renderer.ssrReconstructionImageCount;
    ssr.reconstructionActive =
        ssr.reconstructionRequested > 0 &&
        ssr.hierarchicalActive > 0 &&
        context.renderer.ssrReconstructionTargetsAllocated &&
        context.renderer.ssrReconstructionDescriptorSetsReady &&
        context.renderer.ssrReconstructionPipelinesAvailable &&
        context.renderer.ssrReconstructionImageCount > 1u
            ? 1u
            : 0u;
    const u64 reconstructionPixels =
        static_cast<u64>(ssr.depthPyramidWidth) * ssr.depthPyramidHeight;
    ssr.reconstructionMemoryBytes = reconstructionPixels *
        sizeof(u16) * 4u *
        static_cast<u64>(ssr.reconstructionImageCount > 0
            ? ssr.reconstructionImageCount * 4u
            : 0u);
    ssr.reflectionProbeFallbackEnabled =
        settings.reflectionProbeFallbackEnabled ? 1u : 0u;
    ssr.sceneColorHistoryRequested =
        ssr.colorResolveEnabled > 0 && settings.ssrSceneColorHistoryEnabled ? 1u : 0u;
    ssr.sceneColorHistoryDescriptorBound =
        context.renderer.ssrSceneColorHistoryDescriptorReady ? 1u : 0u;
    ssr.sceneColorHistoryReady = 0u;
    ssr.sceneColorHistoryActive = 0u;
    ssr.sceneColorHistoryFallbackReason =
        ssr.sceneColorHistoryRequested == 0u
            ? 1u
            : (ssr.sceneColorHistoryDescriptorBound == 0u ? 2u : 3u);
    ssr.radianceSource = ssr.colorResolveEnabled > 0 ? 1u : 0u;
}

}
