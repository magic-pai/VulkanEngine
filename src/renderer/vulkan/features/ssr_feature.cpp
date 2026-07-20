#include "renderer/vulkan/features/ssr_feature.h"

#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/shadow_settings.h"

#include <algorithm>

namespace se {

namespace {

void AppendFfxSssrFrameGraphResources(RenderFrameGraphPlan& plan) {
    constexpr RenderGraphResourceStatus kPhysical =
        RenderGraphResourceStatus::Physical;
    constexpr RenderGraphResourceLifetime kPerFrame =
        RenderGraphResourceLifetime::PerFrame;
    constexpr RenderGraphResourceLifetime kHistory =
        RenderGraphResourceLifetime::PersistentHistory;
    constexpr RenderGraphResourceLifetime kCache =
        RenderGraphResourceLifetime::PersistentCache;

    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FrameConstants",
        "uniform buffer",
        "sampled by per-frame shader constants",
        "per swapchain frame"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kCache,
        "FFX SobolBuffer",
        "R32_UINT",
        "uniform texel buffer",
        "128x128 1spp blue-noise table"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kCache,
        "FFX RankingTileBuffer",
        "R32_UINT",
        "uniform texel buffer",
        "128x128x8 blue-noise table"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kCache,
        "FFX ScramblingTileBuffer",
        "R32_UINT",
        "uniform texel buffer",
        "128x128x8 blue-noise table"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX RayCounter",
        "R32_UINT",
        "storage texel buffer, transfer, indirect args",
        "per swapchain frame"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX IntersectArgs",
        "R32_UINT",
        "storage texel buffer, indirect args",
        "two dispatch records"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX RayList",
        "R32_UINT",
        "storage texel buffer",
        "internal resolution pixels"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX DenoiserTiles",
        "R32_UINT",
        "storage/uniform texel buffer",
        "8x8 tile grid"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX ExtractedRoughness",
        "R32_SFLOAT",
        "storage image, sampled",
        "internal resolution"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX BlueNoise",
        "R32G32_SFLOAT",
        "storage image, sampled",
        "128x128"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX IntersectOutput",
        "R32G32B32A32_SFLOAT",
        "storage image, sampled",
        "internal resolution"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kHistory,
        "FFX RadianceHistory",
        "R32G32B32A32_SFLOAT",
        "sampled history and optional Deferred reflection composite input",
        "internal resolution history"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kHistory,
        "FFX AverageRadianceHistory",
        "R32G32B32A32_SFLOAT",
        "sampled placeholder average history",
        "8x8 tile history"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kHistory,
        "FFX VarianceHistory",
        "R32_SFLOAT",
        "sampled placeholder variance history",
        "internal resolution history"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kHistory,
        "FFX SampleCountHistory",
        "R32_SFLOAT",
        "sampled placeholder sample-count history",
        "internal resolution history"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX ReprojectedRadiance",
        "R32G32B32A32_SFLOAT",
        "storage image",
        "internal resolution"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX AverageRadiance",
        "R32G32B32A32_SFLOAT",
        "storage image",
        "8x8 tile grid"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX Variance",
        "R32_SFLOAT",
        "storage image",
        "internal resolution"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX SampleCount",
        "R32_SFLOAT",
        "storage image",
        "internal resolution"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX PrefilteredRadiance",
        "R32G32B32A32_SFLOAT",
        "storage image",
        "internal resolution"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX PrefilteredVariance",
        "R32_SFLOAT",
        "storage image",
        "internal resolution"
    );
    AppendRenderFrameGraphResource(
        plan,
        kPhysical,
        kPerFrame,
        "FFX PrefilteredSampleCount",
        "R32_SFLOAT",
        "storage image",
        "internal resolution"
    );
}

} // namespace

void VulkanSsrFeature::AppendFrameGraph(
    const VulkanRenderFeatureFrameGraphContext& context
) const {
    if (context.stage != VulkanRenderFeatureFrameGraphStage::Lighting) {
        return;
    }
    if (context.stats.ssr.backendRequestedProvider == 1u) {
        AppendFfxSssrFrameGraphResources(context.plan);
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            context.stats.ssr.fidelityFxSssrRuntimeDispatchReady > 0
                ? RenderFramePassStatus::Active
                : RenderFramePassStatus::Roadmap,
            RenderFramePassQueue::Compute,
            "FidelityFXSSSRClassifyTiles",
            "FrameConstants, GBufferNormalRoughness, SceneDepth, PrefilteredEnvironmentMap, FFX VarianceHistory",
            "FFX RayCounter, FFX RayList, FFX DenoiserTiles, FFX ExtractedRoughness, FFX IntersectOutput",
            "AMD FidelityFX SSSR ClassifyTiles bridge classifies rays and denoiser tiles from scene depth, roughness, normal, and variance inputs."
        );
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            context.stats.ssr.fidelityFxSssrRuntimeDispatchReady > 0
                ? RenderFramePassStatus::Active
                : RenderFramePassStatus::Roadmap,
            RenderFramePassQueue::Compute,
            "FidelityFXSSSRPrepareIndirectArgs",
            "FrameConstants, FFX RayCounter",
            "FFX IntersectArgs",
            "AMD FidelityFX SSSR PrepareIndirectArgs bridge writes the Intersect and Reproject indirect dispatch records."
        );
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            context.stats.ssr.fidelityFxSssrRuntimeDispatchReady > 0
                ? RenderFramePassStatus::Active
                : RenderFramePassStatus::Roadmap,
            RenderFramePassQueue::Compute,
            "FidelityFXSSSRPrepareBlueNoise",
            "FrameConstants, FFX SobolBuffer, FFX RankingTileBuffer, FFX ScramblingTileBuffer",
            "FFX BlueNoise",
            "AMD FidelityFX SSSR PrepareBlueNoiseTexture bridge expands the official 128x128 1spp tables for runtime sampling."
        );
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            context.stats.ssr.fidelityFxSssrRuntimeDispatchReady > 0
                ? RenderFramePassStatus::Active
                : RenderFramePassStatus::Roadmap,
            RenderFramePassQueue::Compute,
            "FidelityFXSSSRIntersect",
            "FrameConstants, HDRSceneColor, SSRDepthPyramid, GBufferNormalRoughness, FFX ExtractedRoughness, PrefilteredEnvironmentMap, FFX BlueNoise, FFX RayList, FFX IntersectArgs",
            "FFX IntersectOutput, FFX RayCounter",
            "AMD FidelityFX SSSR Intersect bridge consumes the official ray list and writes the intersection payload; it is still an auditable intermediate."
        );
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            context.stats.ssr.fidelityFxSssrRuntimeDispatchReady > 0
                ? RenderFramePassStatus::Active
                : RenderFramePassStatus::Roadmap,
            RenderFramePassQueue::Compute,
            "FidelityFXSSSRReproject",
            "FrameConstants, SceneDepth, FFX ExtractedRoughness, GBufferNormalRoughness, FFX IntersectOutput, Velocity, FFX BlueNoise, FFX DenoiserTiles, FFX RadianceHistory, FFX AverageRadianceHistory, FFX VarianceHistory, FFX SampleCountHistory, FFX IntersectArgs",
            "FFX ReprojectedRadiance, FFX AverageRadiance, FFX Variance, FFX SampleCount",
            "AMD FidelityFX SSSR Reproject bridge consumes denoiser tiles through the second indirect dispatch record and prepares DNSR variance and history inputs."
        );
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            context.stats.ssr.fidelityFxSssrRuntimeDispatchReady > 0
                ? RenderFramePassStatus::Active
                : RenderFramePassStatus::Roadmap,
            RenderFramePassQueue::Compute,
            "FidelityFXSSSRPrefilter",
            "FrameConstants, SceneDepth, FFX ExtractedRoughness, GBufferNormalRoughness, FFX AverageRadiance, FFX IntersectOutput, FFX Variance, FFX SampleCount, FFX DenoiserTiles, FFX IntersectArgs",
            "FFX PrefilteredRadiance, FFX PrefilteredVariance, FFX PrefilteredSampleCount",
            "AMD FidelityFX SSSR Prefilter bridge applies the official DNSR spatial prefilter as an auditable intermediate. ResolveTemporal, real history swap, and final image contribution are still pending."
        );
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            context.stats.ssr.fidelityFxSssrRuntimeDispatchReady > 0
                ? RenderFramePassStatus::Active
                : RenderFramePassStatus::Roadmap,
            RenderFramePassQueue::Compute,
            "FidelityFXSSSRResolveTemporal",
            "FrameConstants, FFX ExtractedRoughness, FFX AverageRadiance, FFX PrefilteredRadiance, FFX ReprojectedRadiance, FFX PrefilteredVariance, FFX PrefilteredSampleCount, FFX DenoiserTiles, FFX IntersectArgs",
            "FFX RadianceHistory, FFX AverageRadianceHistory, FFX VarianceHistory, FFX SampleCountHistory",
            "AMD FidelityFX SSSR ResolveTemporal bridge clips and accumulates the prefiltered signal into history images consumed by the next Reproject pass and the current frame reflection apply pass."
        );
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            context.stats.ssr.fidelityFxSssrSameFrameCompositeActive > 0
                ? RenderFramePassStatus::Active
                : RenderFramePassStatus::Roadmap,
            RenderFramePassQueue::Graphics,
            "FidelityFXSSSRApplyReflections",
            "HDRSceneColor, FFX RadianceHistory, GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, SceneDepth, BRDFLUT",
            "HDRSceneColor",
            "AMD-style same-frame post composition applies current ResolveTemporal radiance before temporal upscaling; destination alpha preserves the attenuated IBL replacement baseline."
        );
        AppendRenderFrameGraphPass(
            context.plan,
            RenderFramePassKind::Reflections,
            context.stats.ssr.fidelityFxSssrDeferredCompositeActive > 0
                ? RenderFramePassStatus::Active
                : RenderFramePassStatus::Roadmap,
            RenderFramePassQueue::Graphics,
            "FidelityFXSSSRDeferredComposite",
            "FFX RadianceHistory, Velocity, SSRHistoryMetadata, GBufferNormalRoughness, SceneDepth, BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap",
            "HDRSceneColor",
            "Debug reverse-control fallback: Deferred lighting samples the previous completed FidelityFX SSSR ResolveTemporal history with receiver validation when same-frame composition is disabled."
        );
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
                "SSR temporal accumulation validates motion, previous-view depth, normal, roughness, and history lock state; current-HDR hit radiance is an explicit experimental input, not the production default."
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
    ssr.backendRequestedProvider =
        settings.ssrFidelityFxBackendRequested ? 1u : 0u;
    ssr.backendActiveProvider = 0u;
#if defined(SE_ENABLE_FIDELITYFX_SSSR) && SE_ENABLE_FIDELITYFX_SSSR
    ssr.fidelityFxSssrContractVersion = 12u;
    ssr.fidelityFxSssrSourceReady = 1u;
    ssr.fidelityFxSssrShaderBuildIntegrated = 1u;
    ssr.fidelityFxSssrShaderCount = SE_FIDELITYFX_SSSR_SHADER_COUNT;
    ssr.fidelityFxSssrDenoiserDependencyReady = 1u;
    ssr.fidelityFxSssrSpdDependencyReady = 1u;
#else
    ssr.fidelityFxSssrContractVersion = 0u;
    ssr.fidelityFxSssrSourceReady = 0u;
    ssr.fidelityFxSssrShaderBuildIntegrated = 0u;
    ssr.fidelityFxSssrShaderCount = 0u;
    ssr.fidelityFxSssrDenoiserDependencyReady = 0u;
    ssr.fidelityFxSssrSpdDependencyReady = 0u;
#endif
    ssr.fidelityFxSssrConstantsResourcesReady =
        context.renderer.ffxSssrConstantsResourcesReady ? 1u : 0u;
    ssr.fidelityFxSssrConstantsDescriptorSetsReady =
        context.renderer.ffxSssrConstantsDescriptorSetsReady ? 1u : 0u;
    ssr.fidelityFxSssrTemporalStabilityFactor =
        context.renderer.ffxSssrTemporalStabilityFactor;
    ssr.fidelityFxSssrSamplesPerQuad =
        context.renderer.ffxSssrSamplesPerQuad;
    ssr.fidelityFxSssrStableEnvironmentFallbackEnabled =
        context.renderer.ffxSssrStableEnvironmentFallbackEnabled ? 1u : 0u;
    ssr.fidelityFxSssrConstantEnvironmentFallbackEnabled =
        context.renderer.ffxSssrConstantEnvironmentFallbackEnabled ? 1u : 0u;
    ssr.fidelityFxSssrPerfectReflectionDirectionsEnabled =
        context.renderer.ffxSssrPerfectReflectionDirectionsEnabled ? 1u : 0u;
    ssr.fidelityFxSssrPrefilterBypassEnabled =
        context.renderer.ffxSssrPrefilterBypassEnabled ? 1u : 0u;
    ssr.fidelityFxSssrResolveTemporalBypassEnabled =
        context.renderer.ffxSssrResolveTemporalBypassEnabled ? 1u : 0u;
    ssr.fidelityFxSssrClassifySurfaceSeedEnabled =
        context.renderer.ffxSssrClassifySurfaceSeedEnabled ? 1u : 0u;
    ssr.fidelityFxSssrIntersectCoverageMarkerEnabled =
        context.renderer.ffxSssrIntersectCoverageMarkerEnabled ? 1u : 0u;
    ssr.fidelityFxSssrEnvironmentMipCount =
        context.renderer.ffxSssrEnvironmentMipCount;
    ssr.fidelityFxSssrPrepareIndirectArgsResourcesReady =
        context.renderer.ffxSssrPrepareIndirectArgsResourcesReady ? 1u : 0u;
    ssr.fidelityFxSssrPrepareIndirectArgsDescriptorSetsReady =
        context.renderer.ffxSssrPrepareIndirectArgsDescriptorSetsReady ? 1u : 0u;
    ssr.fidelityFxSssrPrepareIndirectArgsPipelineReady =
        context.renderer.ffxSssrPrepareIndirectArgsPipelineReady ? 1u : 0u;
    ssr.fidelityFxSssrPrepareIndirectArgsBufferBytes =
        context.renderer.ffxSssrPrepareIndirectArgsBufferBytes;
    ssr.fidelityFxSssrClassifyTilesResourcesReady =
        context.renderer.ffxSssrClassifyTilesResourcesReady ? 1u : 0u;
    ssr.fidelityFxSssrClassifyTilesDescriptorSetsReady =
        context.renderer.ffxSssrClassifyTilesDescriptorSetsReady ? 1u : 0u;
    ssr.fidelityFxSssrClassifyTilesPipelineReady =
        context.renderer.ffxSssrClassifyTilesPipelineReady ? 1u : 0u;
    ssr.fidelityFxSssrClassifyTilesInputContractReady =
        context.renderer.ffxSssrClassifyTilesInputContractReady ? 1u : 0u;
    ssr.fidelityFxSssrClassifyTilesWidth =
        context.renderer.ffxSssrClassifyTilesWidth;
    ssr.fidelityFxSssrClassifyTilesHeight =
        context.renderer.ffxSssrClassifyTilesHeight;
    ssr.fidelityFxSssrClassifyTilesGroupCountX =
        context.renderer.ffxSssrClassifyTilesGroupCountX;
    ssr.fidelityFxSssrClassifyTilesGroupCountY =
        context.renderer.ffxSssrClassifyTilesGroupCountY;
    ssr.fidelityFxSssrClassifyTilesRayListCapacity =
        context.renderer.ffxSssrClassifyTilesRayListCapacity;
    ssr.fidelityFxSssrClassifyTilesDenoiserTileListCapacity =
        context.renderer.ffxSssrClassifyTilesDenoiserTileListCapacity;
    ssr.fidelityFxSssrClassifyTilesMemoryBytes =
        context.renderer.ffxSssrClassifyTilesMemoryBytes;
    ssr.fidelityFxSssrBlueNoiseResourcesReady =
        context.renderer.ffxSssrBlueNoiseResourcesReady ? 1u : 0u;
    ssr.fidelityFxSssrBlueNoiseDescriptorSetsReady =
        context.renderer.ffxSssrBlueNoiseDescriptorSetsReady ? 1u : 0u;
    ssr.fidelityFxSssrBlueNoisePipelineReady =
        context.renderer.ffxSssrBlueNoisePipelineReady ? 1u : 0u;
    ssr.fidelityFxSssrBlueNoiseWidth =
        context.renderer.ffxSssrBlueNoiseWidth;
    ssr.fidelityFxSssrBlueNoiseHeight =
        context.renderer.ffxSssrBlueNoiseHeight;
    ssr.fidelityFxSssrBlueNoiseGroupCountX =
        context.renderer.ffxSssrBlueNoiseGroupCountX;
    ssr.fidelityFxSssrBlueNoiseGroupCountY =
        context.renderer.ffxSssrBlueNoiseGroupCountY;
    ssr.fidelityFxSssrBlueNoiseSobolEntryCount =
        context.renderer.ffxSssrBlueNoiseSobolEntryCount;
    ssr.fidelityFxSssrBlueNoiseRankingTileEntryCount =
        context.renderer.ffxSssrBlueNoiseRankingTileEntryCount;
    ssr.fidelityFxSssrBlueNoiseScramblingTileEntryCount =
        context.renderer.ffxSssrBlueNoiseScramblingTileEntryCount;
    ssr.fidelityFxSssrBlueNoiseMemoryBytes =
        context.renderer.ffxSssrBlueNoiseMemoryBytes;
    ssr.fidelityFxSssrIntersectResourcesReady =
        context.renderer.ffxSssrIntersectResourcesReady ? 1u : 0u;
    ssr.fidelityFxSssrIntersectDescriptorSetsReady =
        context.renderer.ffxSssrIntersectDescriptorSetsReady ? 1u : 0u;
    ssr.fidelityFxSssrIntersectPipelineReady =
        context.renderer.ffxSssrIntersectPipelineReady ? 1u : 0u;
    ssr.fidelityFxSssrIntersectInputContractReady =
        context.renderer.ffxSssrIntersectInputContractReady ? 1u : 0u;
    ssr.fidelityFxSssrIntersectWidth =
        context.renderer.ffxSssrIntersectWidth;
    ssr.fidelityFxSssrIntersectHeight =
        context.renderer.ffxSssrIntersectHeight;
    ssr.fidelityFxSssrIntersectDepthPyramidMipCount =
        context.renderer.ffxSssrIntersectDepthPyramidMipCount;
    ssr.fidelityFxSssrReprojectResourcesReady =
        context.renderer.ffxSssrReprojectResourcesReady ? 1u : 0u;
    ssr.fidelityFxSssrReprojectDescriptorSetsReady =
        context.renderer.ffxSssrReprojectDescriptorSetsReady ? 1u : 0u;
    ssr.fidelityFxSssrReprojectPipelineReady =
        context.renderer.ffxSssrReprojectPipelineReady ? 1u : 0u;
    ssr.fidelityFxSssrReprojectInputContractReady =
        context.renderer.ffxSssrReprojectInputContractReady ? 1u : 0u;
    ssr.fidelityFxSssrReprojectWidth =
        context.renderer.ffxSssrReprojectWidth;
    ssr.fidelityFxSssrReprojectHeight =
        context.renderer.ffxSssrReprojectHeight;
    ssr.fidelityFxSssrReprojectAverageWidth =
        context.renderer.ffxSssrReprojectAverageWidth;
    ssr.fidelityFxSssrReprojectAverageHeight =
        context.renderer.ffxSssrReprojectAverageHeight;
    ssr.fidelityFxSssrReprojectHistoryReady =
        context.renderer.ffxSssrReprojectHistoryReady ? 1u : 0u;
    ssr.fidelityFxSssrReprojectHistorySource =
        context.renderer.ffxSssrReprojectHistorySource;
    ssr.fidelityFxSssrReprojectHistoryMetadataSource =
        context.renderer.ffxSssrReprojectHistoryMetadataSource;
    ssr.fidelityFxSssrReprojectMemoryBytes =
        context.renderer.ffxSssrReprojectMemoryBytes;
    ssr.fidelityFxSssrReprojectIndirectArgsOffsetBytes =
        context.renderer.ffxSssrReprojectIndirectArgsOffsetBytes;
    ssr.fidelityFxSssrReprojectMotionVectorMode =
        context.renderer.ffxSssrReprojectMotionVectorMode;
    ssr.fidelityFxSssrReprojectMotionVectorScaleX =
        context.renderer.ffxSssrReprojectMotionVectorScaleX;
    ssr.fidelityFxSssrReprojectMotionVectorScaleY =
        context.renderer.ffxSssrReprojectMotionVectorScaleY;
    ssr.fidelityFxSssrReprojectMotionVectorContractReady =
        context.renderer.ffxSssrReprojectMotionVectorContractReady ? 1u : 0u;
    ssr.fidelityFxSssrReprojectHitReprojectionEnabled =
        context.renderer.ffxSssrReprojectHitReprojectionEnabled ? 1u : 0u;
    ssr.fidelityFxSssrReprojectReprojectionContractReady =
        context.renderer.ffxSssrReprojectReprojectionContractReady ? 1u : 0u;
    ssr.fidelityFxSssrPrefilterResourcesReady =
        context.renderer.ffxSssrPrefilterResourcesReady ? 1u : 0u;
    ssr.fidelityFxSssrPrefilterDescriptorSetsReady =
        context.renderer.ffxSssrPrefilterDescriptorSetsReady ? 1u : 0u;
    ssr.fidelityFxSssrPrefilterPipelineReady =
        context.renderer.ffxSssrPrefilterPipelineReady ? 1u : 0u;
    ssr.fidelityFxSssrPrefilterInputContractReady =
        context.renderer.ffxSssrPrefilterInputContractReady ? 1u : 0u;
    ssr.fidelityFxSssrPrefilterWidth =
        context.renderer.ffxSssrPrefilterWidth;
    ssr.fidelityFxSssrPrefilterHeight =
        context.renderer.ffxSssrPrefilterHeight;
    ssr.fidelityFxSssrPrefilterMemoryBytes =
        context.renderer.ffxSssrPrefilterMemoryBytes;
    ssr.fidelityFxSssrPrefilterIndirectArgsOffsetBytes =
        context.renderer.ffxSssrPrefilterIndirectArgsOffsetBytes;
    ssr.fidelityFxSssrResolveTemporalResourcesReady =
        context.renderer.ffxSssrResolveTemporalResourcesReady ? 1u : 0u;
    ssr.fidelityFxSssrResolveTemporalDescriptorSetsReady =
        context.renderer.ffxSssrResolveTemporalDescriptorSetsReady ? 1u : 0u;
    ssr.fidelityFxSssrResolveTemporalPipelineReady =
        context.renderer.ffxSssrResolveTemporalPipelineReady ? 1u : 0u;
    ssr.fidelityFxSssrResolveTemporalInputContractReady =
        context.renderer.ffxSssrResolveTemporalInputContractReady ? 1u : 0u;
    ssr.fidelityFxSssrResolveTemporalHistoryWritebackReady =
        context.renderer.ffxSssrResolveTemporalHistoryWritebackReady ? 1u : 0u;
    ssr.fidelityFxSssrResolveTemporalWidth =
        context.renderer.ffxSssrResolveTemporalWidth;
    ssr.fidelityFxSssrResolveTemporalHeight =
        context.renderer.ffxSssrResolveTemporalHeight;
    ssr.fidelityFxSssrResolveTemporalMemoryBytes =
        context.renderer.ffxSssrResolveTemporalMemoryBytes;
    ssr.fidelityFxSssrResolveTemporalIndirectArgsOffsetBytes =
        context.renderer.ffxSssrResolveTemporalIndirectArgsOffsetBytes;
    ssr.fidelityFxSssrSampleCountWritebackReady =
        context.renderer.ffxSssrSampleCountWritebackReady ? 1u : 0u;
    ssr.fidelityFxSssrRuntimeDispatchReady =
        ssr.backendRequestedProvider > 0u &&
        ssr.colorResolveEnabled > 0u &&
        ssr.fidelityFxSssrSourceReady > 0u &&
        ssr.fidelityFxSssrShaderBuildIntegrated > 0u &&
        ssr.fidelityFxSssrDenoiserDependencyReady > 0u &&
        ssr.fidelityFxSssrSpdDependencyReady > 0u &&
        ssr.fidelityFxSssrConstantsResourcesReady > 0u &&
        ssr.fidelityFxSssrConstantsDescriptorSetsReady > 0u &&
        ssr.fidelityFxSssrPrepareIndirectArgsResourcesReady > 0u &&
        ssr.fidelityFxSssrPrepareIndirectArgsDescriptorSetsReady > 0u &&
        ssr.fidelityFxSssrPrepareIndirectArgsPipelineReady > 0u &&
        ssr.fidelityFxSssrClassifyTilesResourcesReady > 0u &&
        ssr.fidelityFxSssrClassifyTilesDescriptorSetsReady > 0u &&
        ssr.fidelityFxSssrClassifyTilesPipelineReady > 0u &&
        ssr.fidelityFxSssrClassifyTilesInputContractReady > 0u &&
        ssr.fidelityFxSssrBlueNoiseResourcesReady > 0u &&
        ssr.fidelityFxSssrBlueNoiseDescriptorSetsReady > 0u &&
        ssr.fidelityFxSssrBlueNoisePipelineReady > 0u &&
        ssr.fidelityFxSssrBlueNoiseWidth == 128u &&
        ssr.fidelityFxSssrBlueNoiseHeight == 128u &&
        ssr.fidelityFxSssrBlueNoiseSobolEntryCount == 256u * 256u &&
        ssr.fidelityFxSssrBlueNoiseRankingTileEntryCount == 128u * 128u * 8u &&
        ssr.fidelityFxSssrBlueNoiseScramblingTileEntryCount == 128u * 128u * 8u &&
        ssr.fidelityFxSssrIntersectResourcesReady > 0u &&
        ssr.fidelityFxSssrIntersectDescriptorSetsReady > 0u &&
        ssr.fidelityFxSssrIntersectPipelineReady > 0u &&
        ssr.fidelityFxSssrIntersectInputContractReady > 0u &&
        ssr.fidelityFxSssrReprojectResourcesReady > 0u &&
        ssr.fidelityFxSssrReprojectDescriptorSetsReady > 0u &&
        ssr.fidelityFxSssrReprojectPipelineReady > 0u &&
        ssr.fidelityFxSssrReprojectInputContractReady > 0u &&
        ssr.fidelityFxSssrReprojectHistoryReady > 0u &&
        ssr.fidelityFxSssrReprojectHistoryMetadataSource == 1u &&
        ssr.fidelityFxSssrReprojectIndirectArgsOffsetBytes == sizeof(u32) * 3u &&
        ssr.fidelityFxSssrReprojectMotionVectorContractReady > 0u &&
        (ssr.fidelityFxSssrReprojectMotionVectorMode == 1u ||
            ssr.fidelityFxSssrReprojectMotionVectorMode == 2u) &&
        ssr.fidelityFxSssrReprojectReprojectionContractReady > 0u &&
        ssr.fidelityFxSssrPrefilterResourcesReady > 0u &&
        ssr.fidelityFxSssrPrefilterDescriptorSetsReady > 0u &&
        ssr.fidelityFxSssrPrefilterPipelineReady > 0u &&
        ssr.fidelityFxSssrPrefilterInputContractReady > 0u &&
        ssr.fidelityFxSssrPrefilterIndirectArgsOffsetBytes == sizeof(u32) * 3u &&
        ssr.fidelityFxSssrResolveTemporalResourcesReady > 0u &&
        ssr.fidelityFxSssrResolveTemporalDescriptorSetsReady > 0u &&
        ssr.fidelityFxSssrResolveTemporalPipelineReady > 0u &&
        ssr.fidelityFxSssrResolveTemporalInputContractReady > 0u &&
        ssr.fidelityFxSssrResolveTemporalHistoryWritebackReady > 0u &&
        ssr.fidelityFxSssrSampleCountWritebackReady > 0u &&
        ssr.fidelityFxSssrResolveTemporalIndirectArgsOffsetBytes == sizeof(u32) * 3u
            ? 1u
            : 0u;
    ssr.fidelityFxSssrRuntimeActive = 0u;
    ssr.fidelityFxSssrFallbackReason =
        ssr.backendRequestedProvider == 0u
            ? 1u
            : (ssr.fidelityFxSssrSourceReady == 0u ||
                    ssr.fidelityFxSssrShaderBuildIntegrated == 0u ||
                    ssr.fidelityFxSssrDenoiserDependencyReady == 0u ||
                    ssr.fidelityFxSssrSpdDependencyReady == 0u
                ? 3u
                : (ssr.fidelityFxSssrRuntimeDispatchReady > 0u ? 0u : 2u));
}

}
