#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

inline constexpr std::size_t kMaxDirectionalShadowCascades = 4;
inline constexpr f32 kForward3DShadowCascadeMaxDistance = 60.0f;

struct VulkanLocalShadowFilterSettings {
    f32 biasMin = 0.00065f;
    f32 biasSlope = 0.0018f;
    f32 pcfRadius = 2.4f;
    u32 pcfKernelRadius = 2;
    f32 pcssStrength = 0.32f;
    u32 pcssBlockerSampleCount = 12;
    u32 pcssFilterSampleCount = 16;
    f32 pcssSearchRadiusTexels = 8.0f;
    f32 pcssMaxPenumbraTexels = 8.0f;
};

enum class VulkanShadowQuality : int {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Ultra = 4
};

enum class VulkanDirectionalShadowFilterMode : u32 {
    HardwareBoxPcf = 0,
    OptimizedTentPcf = 1
};

struct VulkanShadowSettings {
    VulkanShadowQuality quality = VulkanShadowQuality::Ultra;
    bool enabled = true;
    bool cascadesEnabled = true;
    bool directionalShadowReceiveEnabled = true;
    bool stableCascades = true;
    f32 strength = 1.0f;
    f32 ambientStrength = 0.54f;
    f32 biasMin = 0.00065f;
    f32 biasSlope = 0.0018f;
    bool casterDepthBiasEnabled = true;
    f32 casterDepthBiasConstant = 16384.0f;
    f32 casterDepthBiasClamp = 0.006f;
    f32 casterDepthBiasSlope = 3.5f;
    f32 directionalReceiverPlaneBiasScale = 1.0f;
    f32 directionalNormalOffsetBiasTexels = 2.0f;
    f32 directionalSlopeOffsetBiasTexels = 0.5f;
    f32 pcfRadius = 1.0f;
    u32 pcfKernelRadius = 2;
    f32 pcssStrength = 1.0f;
    VulkanDirectionalShadowFilterMode directionalFilterMode =
        VulkanDirectionalShadowFilterMode::OptimizedTentPcf;
    u32 directionalFilterSampleCount = 9;
    u32 directionalFilterKernelWidth = 5;
    f32 directionalFilterReceiverBiasExtentTexels = 2.5f;
    u32 directionalPcssBlockerSampleCount = 12;
    u32 directionalPcssFilterSampleCount = 16;
    f32 directionalPcssSearchRadiusTexels = 8.0f;
    f32 directionalPcssMaxPenumbraTexels = 8.0f;
    bool directionalPcssGrazingFadeEnabled = true;
    f32 directionalPcssGrazingFadeStart = 0.25f;
    f32 directionalPcssGrazingFadeEnd = 0.80f;
    f32 localBiasMin = 0.00065f;
    f32 localBiasSlope = 0.0018f;
    f32 localPcfRadius = 2.4f;
    u32 localPcfKernelRadius = 2;
    f32 localPcssStrength = 0.32f;
    u32 localPcssBlockerSampleCount = 12;
    u32 localPcssFilterSampleCount = 16;
    f32 localPcssSearchRadiusTexels = 8.0f;
    f32 localPcssMaxPenumbraTexels = 8.0f;
    bool localProductionFilterEnabled = true;
    f32 localFaceBlendStrength = 0.80f;
    VulkanLocalShadowFilterSettings pointLocalShadowFilter{};
    VulkanLocalShadowFilterSettings spotLocalShadowFilter{};
    VulkanLocalShadowFilterSettings rectLocalShadowFilter{};
    f32 rectLightShadowBiasScale = 8.0f;
    u32 rectLightShadowSampleTiles = 4;
    bool pointLightShadowEnabled = true;
    bool spotLightShadowEnabled = true;
    bool rectLightShadowEnabled = true;
    i32 debugLocalShadowLightIndex = -1;
    f32 contactShadowStrength = 0.56f;
    f32 contactShadowLength = 0.34f;
    f32 contactShadowThickness = 0.16f;
    u32 contactShadowSteps = 8;
    f32 contactShadowJitterStrength = 0.20f;
    f32 contactShadowEdgeFadePixels = 30.0f;
    f32 ssaoStrength = 0.64f;
    f32 ssaoRadius = 2.0f;
    f32 ssaoBias = 0.02f;
    u32 ssaoSampleCount = 16;
    f32 ssrStrength = 0.78f;
    f32 ssrRayLength = 36.0f;
    f32 ssrThickness = 0.05f;
    u32 ssrStepCount = 22;
    bool ssrRefinementEnabled = true;
    bool ssrHiZEnabled = true;
    bool ssrSceneColorHistoryEnabled = true;
    bool ssrCurrentHdrSourceEnabled = false;
    bool ssrCurrentHdrRadianceFilterEnabled = true;
    bool ssrSpatialVarianceClampEnabled = true;
    bool ssrProbeFallbackBlendEnabled = true;
    bool ssrTemporalHistoryLockEnabled = true;
    bool ssrTemporalMissHistoryRejectEnabled = true;
    bool ssrHitValidationEnabled = true;
    bool ssrDeferredReceiverReprojectionEnabled = true;
    bool ssrFidelityFxBackendRequested = false;
    bool reflectionProbeFallbackEnabled = true;
    f32 reflectionProbeDiffuseIntensity = 1.0f;
    f32 reflectionProbeSpecularIntensity = 1.0f;
    f32 reflectionProbeHorizonBlend = 0.22f;
    bool globalIblCubemapEnabled = false;
    bool reflectionProbeCubemapEnabled = true;
    bool skyboxEnabled = false;
    f32 skyboxIntensity = 1.0f;
    f32 skyboxBlur = 0.0f;
    bool localReflectionProbeEnabled = false;
    f32 localReflectionProbeCenterX = 0.0f;
    f32 localReflectionProbeCenterY = 1.2f;
    f32 localReflectionProbeCenterZ = 0.0f;
    f32 localReflectionProbeRadius = 5.5f;
    f32 localReflectionProbeIntensity = 1.25f;
    f32 localReflectionProbeBlendStrength = 0.65f;
    f32 localReflectionProbeFalloff = 2.0f;
    f32 localReflectionProbeColorR = 1.0f;
    f32 localReflectionProbeColorG = 0.82f;
    f32 localReflectionProbeColorB = 0.62f;
    bool probeGridEnabled = false;
    f32 probeGridBlendStrength = 0.5f;
    bool heightFogEnabled = false;
    f32 heightFogDensity = 0.035f;
    f32 heightFogHeightFalloff = 0.08f;
    f32 heightFogStartDistance = 3.0f;
    f32 heightFogMaxOpacity = 0.72f;
    f32 heightFogColorR = 0.58f;
    f32 heightFogColorG = 0.68f;
    f32 heightFogColorB = 0.76f;
    f32 cascadeSplitLambda = 0.78f;
    f32 cascadeMaxDistance = 360.0f;
    f32 cascadeBlendRatio = 0.12f;
    f32 cascadeFadeRatio = 0.08f;
    u32 mapSize = 4096;
    u32 cascadeCount = 4;
};

inline void SyncLocalShadowKindFiltersToShared(VulkanShadowSettings& settings) {
    const VulkanLocalShadowFilterSettings shared{
        settings.localBiasMin,
        settings.localBiasSlope,
        settings.localPcfRadius,
        settings.localPcfKernelRadius,
        settings.localPcssStrength,
        settings.localPcssBlockerSampleCount,
        settings.localPcssFilterSampleCount,
        settings.localPcssSearchRadiusTexels,
        settings.localPcssMaxPenumbraTexels
    };
    settings.pointLocalShadowFilter = shared;
    settings.spotLocalShadowFilter = shared;
    settings.rectLocalShadowFilter = shared;
}

inline void ApplyShadowQualityPreset(
    VulkanShadowSettings& settings,
    VulkanShadowQuality quality
) {
    settings.quality = quality;

    switch (quality) {
    case VulkanShadowQuality::Off:
        settings.enabled = false;
        settings.cascadesEnabled = false;
        settings.directionalShadowReceiveEnabled = false;
        settings.strength = 0.0f;
        settings.ambientStrength = 0.0f;
        settings.casterDepthBiasEnabled = false;
        settings.casterDepthBiasConstant = 0.0f;
        settings.casterDepthBiasClamp = 0.0f;
        settings.casterDepthBiasSlope = 0.0f;
        settings.directionalReceiverPlaneBiasScale = 0.0f;
        settings.directionalNormalOffsetBiasTexels = 0.0f;
        settings.directionalSlopeOffsetBiasTexels = 0.0f;
        settings.pcfRadius = 0.0f;
        settings.pcfKernelRadius = 0;
        settings.pcssStrength = 0.0f;
        settings.directionalPcssBlockerSampleCount = 0u;
        settings.directionalPcssFilterSampleCount = 0u;
        settings.directionalPcssSearchRadiusTexels = 0.0f;
        settings.directionalPcssMaxPenumbraTexels = 0.0f;
        settings.directionalFilterMode = VulkanDirectionalShadowFilterMode::HardwareBoxPcf;
        settings.directionalFilterSampleCount = 0;
        settings.directionalFilterKernelWidth = 0;
        settings.directionalFilterReceiverBiasExtentTexels = 0.0f;
        settings.localBiasMin = 0.0f;
        settings.localBiasSlope = 0.0f;
        settings.localPcfRadius = 0.0f;
        settings.localPcfKernelRadius = 0;
        settings.localPcssStrength = 0.0f;
        settings.localPcssBlockerSampleCount = 0u;
        settings.localPcssFilterSampleCount = 0u;
        settings.localPcssSearchRadiusTexels = 0.0f;
        settings.localPcssMaxPenumbraTexels = 0.0f;
        settings.localProductionFilterEnabled = false;
        settings.localFaceBlendStrength = 0.0f;
        settings.rectLightShadowSampleTiles = 2u;
        settings.contactShadowStrength = 0.0f;
        settings.contactShadowLength = 0.0f;
        settings.contactShadowThickness = 0.0f;
        settings.contactShadowSteps = 0;
        settings.contactShadowJitterStrength = 0.0f;
        settings.contactShadowEdgeFadePixels = 0.0f;
        settings.ssaoStrength = 0.0f;
        settings.ssaoRadius = 0.0f;
        settings.ssaoBias = 0.0f;
        settings.ssaoSampleCount = 0;
        settings.ssrStrength = 0.0f;
        settings.ssrRayLength = 0.0f;
        settings.ssrThickness = 0.0f;
        settings.ssrStepCount = 0;
        settings.ssrSceneColorHistoryEnabled = false;
        settings.ssrCurrentHdrSourceEnabled = false;
        settings.ssrCurrentHdrRadianceFilterEnabled = false;
        settings.ssrSpatialVarianceClampEnabled = false;
        settings.ssrProbeFallbackBlendEnabled = false;
        settings.ssrTemporalHistoryLockEnabled = false;
        settings.ssrTemporalMissHistoryRejectEnabled = false;
        settings.cascadeCount = 0;
        settings.cascadeSplitLambda = 0.0f;
        settings.cascadeMaxDistance = 0.0f;
        settings.cascadeBlendRatio = 0.0f;
        settings.cascadeFadeRatio = 0.0f;
        settings.mapSize = 1024;
        break;
    case VulkanShadowQuality::Low:
        settings.enabled = true;
        settings.cascadesEnabled = false;
        settings.directionalShadowReceiveEnabled = true;
        settings.stableCascades = true;
        settings.strength = 0.82f;
        settings.ambientStrength = 0.34f;
        settings.biasMin = 0.0012f;
        settings.biasSlope = 0.0034f;
        settings.casterDepthBiasEnabled = true;
        settings.casterDepthBiasConstant = 65536.0f;
        settings.casterDepthBiasClamp = 0.012f;
        settings.casterDepthBiasSlope = 4.5f;
        settings.directionalReceiverPlaneBiasScale = 0.75f;
        settings.directionalNormalOffsetBiasTexels = 2.0f;
        settings.directionalSlopeOffsetBiasTexels = 0.5f;
        settings.pcfRadius = 1.0f;
        settings.pcfKernelRadius = 1;
        settings.pcssStrength = 0.0f;
        settings.directionalPcssBlockerSampleCount = 0u;
        settings.directionalPcssFilterSampleCount = 0u;
        settings.directionalPcssSearchRadiusTexels = 0.0f;
        settings.directionalPcssMaxPenumbraTexels = 0.0f;
        settings.directionalFilterMode = VulkanDirectionalShadowFilterMode::OptimizedTentPcf;
        settings.directionalFilterSampleCount = 9;
        settings.directionalFilterKernelWidth = 3;
        settings.directionalFilterReceiverBiasExtentTexels = 1.5f;
        settings.localBiasMin = 0.0013f;
        settings.localBiasSlope = 0.0032f;
        settings.localPcfRadius = 0.75f;
        settings.localPcfKernelRadius = 1;
        settings.localPcssStrength = 0.0f;
        settings.localPcssBlockerSampleCount = 0u;
        settings.localPcssFilterSampleCount = 4u;
        settings.localPcssSearchRadiusTexels = 0.0f;
        settings.localPcssMaxPenumbraTexels = 1.5f;
        settings.localProductionFilterEnabled = true;
        settings.localFaceBlendStrength = 0.25f;
        settings.rectLightShadowSampleTiles = 2u;
        settings.contactShadowStrength = 0.12f;
        settings.contactShadowLength = 0.12f;
        settings.contactShadowThickness = 0.08f;
        settings.contactShadowSteps = 2;
        settings.contactShadowJitterStrength = 0.08f;
        settings.contactShadowEdgeFadePixels = 16.0f;
        settings.ssaoStrength = 0.25f;
        settings.ssaoRadius = 0.85f;
        settings.ssaoBias = 0.05f;
        settings.ssaoSampleCount = 4;
        settings.ssrStrength = 0.25f;
        settings.ssrRayLength = 10.0f;
        settings.ssrThickness = 0.12f;
        settings.ssrStepCount = 8;
        settings.ssrTemporalHistoryLockEnabled = true;
        settings.ssrTemporalMissHistoryRejectEnabled = true;
        settings.cascadeCount = 1;
        settings.cascadeSplitLambda = 0.55f;
        settings.cascadeMaxDistance = 90.0f;
        settings.cascadeBlendRatio = 0.0f;
        settings.cascadeFadeRatio = 0.18f;
        settings.mapSize = 1024;
        break;
    case VulkanShadowQuality::Medium:
        settings.enabled = true;
        settings.cascadesEnabled = true;
        settings.directionalShadowReceiveEnabled = true;
        settings.stableCascades = true;
        settings.strength = 0.95f;
        settings.ambientStrength = 0.42f;
        settings.biasMin = 0.0009f;
        settings.biasSlope = 0.0024f;
        settings.casterDepthBiasEnabled = true;
        settings.casterDepthBiasConstant = 49152.0f;
        settings.casterDepthBiasClamp = 0.010f;
        settings.casterDepthBiasSlope = 4.0f;
        settings.directionalReceiverPlaneBiasScale = 1.0f;
        settings.directionalNormalOffsetBiasTexels = 2.0f;
        settings.directionalSlopeOffsetBiasTexels = 0.5f;
        settings.pcfRadius = 1.0f;
        settings.pcfKernelRadius = 1;
        settings.pcssStrength = 0.0f;
        settings.directionalPcssBlockerSampleCount = 0u;
        settings.directionalPcssFilterSampleCount = 0u;
        settings.directionalPcssSearchRadiusTexels = 0.0f;
        settings.directionalPcssMaxPenumbraTexels = 0.0f;
        settings.directionalFilterMode = VulkanDirectionalShadowFilterMode::OptimizedTentPcf;
        settings.directionalFilterSampleCount = 9;
        settings.directionalFilterKernelWidth = 3;
        settings.directionalFilterReceiverBiasExtentTexels = 1.5f;
        settings.localBiasMin = 0.0009f;
        settings.localBiasSlope = 0.0024f;
        settings.localPcfRadius = 1.25f;
        settings.localPcfKernelRadius = 1;
        settings.localPcssStrength = 0.08f;
        settings.localPcssBlockerSampleCount = 8u;
        settings.localPcssFilterSampleCount = 8u;
        settings.localPcssSearchRadiusTexels = 4.0f;
        settings.localPcssMaxPenumbraTexels = 4.0f;
        settings.localProductionFilterEnabled = true;
        settings.localFaceBlendStrength = 0.50f;
        settings.rectLightShadowSampleTiles = 2u;
        settings.contactShadowStrength = 0.35f;
        settings.contactShadowLength = 0.18f;
        settings.contactShadowThickness = 0.08f;
        settings.contactShadowSteps = 4;
        settings.contactShadowJitterStrength = 0.12f;
        settings.contactShadowEdgeFadePixels = 18.0f;
        settings.ssaoStrength = 0.45f;
        settings.ssaoRadius = 1.2f;
        settings.ssaoBias = 0.035f;
        settings.ssaoSampleCount = 8;
        settings.ssrStrength = 0.55f;
        settings.ssrRayLength = 18.0f;
        settings.ssrThickness = 0.08f;
        settings.ssrStepCount = 12;
        settings.ssrTemporalHistoryLockEnabled = true;
        settings.ssrTemporalMissHistoryRejectEnabled = true;
        settings.cascadeCount = 3;
        settings.cascadeSplitLambda = 0.68f;
        settings.cascadeMaxDistance = 180.0f;
        settings.cascadeBlendRatio = 0.08f;
        settings.cascadeFadeRatio = 0.12f;
        settings.mapSize = 2048;
        break;
    case VulkanShadowQuality::High:
        settings.enabled = true;
        settings.cascadesEnabled = true;
        settings.directionalShadowReceiveEnabled = true;
        settings.stableCascades = true;
        settings.strength = 1.0f;
        settings.ambientStrength = 0.50f;
        settings.biasMin = 0.00075f;
        settings.biasSlope = 0.0020f;
        settings.casterDepthBiasEnabled = true;
        settings.casterDepthBiasConstant = 32768.0f;
        settings.casterDepthBiasClamp = 0.008f;
        settings.casterDepthBiasSlope = 4.0f;
        settings.directionalReceiverPlaneBiasScale = 1.0f;
        settings.directionalNormalOffsetBiasTexels = 2.0f;
        settings.directionalSlopeOffsetBiasTexels = 0.5f;
        settings.pcfRadius = 1.0f;
        settings.pcfKernelRadius = 2;
        settings.pcssStrength = 0.0f;
        settings.directionalPcssBlockerSampleCount = 0u;
        settings.directionalPcssFilterSampleCount = 0u;
        settings.directionalPcssSearchRadiusTexels = 0.0f;
        settings.directionalPcssMaxPenumbraTexels = 0.0f;
        settings.directionalFilterMode = VulkanDirectionalShadowFilterMode::OptimizedTentPcf;
        settings.directionalFilterSampleCount = 9;
        settings.directionalFilterKernelWidth = 5;
        settings.directionalFilterReceiverBiasExtentTexels = 2.5f;
        settings.localBiasMin = 0.00075f;
        settings.localBiasSlope = 0.0020f;
        settings.localPcfRadius = 1.8f;
        settings.localPcfKernelRadius = 2;
        settings.localPcssStrength = 0.22f;
        settings.localPcssBlockerSampleCount = 8u;
        settings.localPcssFilterSampleCount = 12u;
        settings.localPcssSearchRadiusTexels = 6.0f;
        settings.localPcssMaxPenumbraTexels = 6.0f;
        settings.localProductionFilterEnabled = true;
        settings.localFaceBlendStrength = 0.65f;
        settings.rectLightShadowSampleTiles = 4u;
        settings.contactShadowStrength = 0.48f;
        settings.contactShadowLength = 0.28f;
        settings.contactShadowThickness = 0.12f;
        settings.contactShadowSteps = 6;
        settings.contactShadowJitterStrength = 0.18f;
        settings.contactShadowEdgeFadePixels = 24.0f;
        settings.ssaoStrength = 0.58f;
        settings.ssaoRadius = 1.7f;
        settings.ssaoBias = 0.025f;
        settings.ssaoSampleCount = 12;
        settings.ssrStrength = 0.70f;
        settings.ssrRayLength = 28.0f;
        settings.ssrThickness = 0.06f;
        settings.ssrStepCount = 18;
        settings.ssrTemporalHistoryLockEnabled = true;
        settings.ssrTemporalMissHistoryRejectEnabled = true;
        settings.cascadeCount = 4;
        settings.cascadeSplitLambda = 0.72f;
        settings.cascadeMaxDistance = 300.0f;
        settings.cascadeBlendRatio = 0.10f;
        settings.cascadeFadeRatio = 0.10f;
        settings.mapSize = 4096;
        break;
    case VulkanShadowQuality::Ultra:
        settings.enabled = true;
        settings.cascadesEnabled = true;
        settings.directionalShadowReceiveEnabled = true;
        settings.stableCascades = true;
        settings.strength = 1.0f;
        settings.ambientStrength = 0.54f;
        settings.biasMin = 0.00065f;
        settings.biasSlope = 0.0018f;
        settings.casterDepthBiasEnabled = true;
        settings.casterDepthBiasConstant = 16384.0f;
        settings.casterDepthBiasClamp = 0.006f;
        settings.casterDepthBiasSlope = 3.5f;
        settings.directionalReceiverPlaneBiasScale = 1.0f;
        settings.directionalNormalOffsetBiasTexels = 2.0f;
        settings.directionalSlopeOffsetBiasTexels = 0.5f;
        settings.pcfRadius = 1.0f;
        settings.pcfKernelRadius = 2;
        settings.pcssStrength = 1.0f;
        settings.directionalPcssBlockerSampleCount = 12u;
        settings.directionalPcssFilterSampleCount = 16u;
        settings.directionalPcssSearchRadiusTexels = 8.0f;
        settings.directionalPcssMaxPenumbraTexels = 8.0f;
        settings.directionalFilterMode = VulkanDirectionalShadowFilterMode::OptimizedTentPcf;
        settings.directionalFilterSampleCount = 9;
        settings.directionalFilterKernelWidth = 5;
        settings.directionalFilterReceiverBiasExtentTexels = 2.5f;
        settings.localBiasMin = 0.00065f;
        settings.localBiasSlope = 0.0018f;
        settings.localPcfRadius = 2.4f;
        settings.localPcfKernelRadius = 2;
        settings.localPcssStrength = 0.32f;
        settings.localPcssBlockerSampleCount = 12u;
        settings.localPcssFilterSampleCount = 16u;
        settings.localPcssSearchRadiusTexels = 8.0f;
        settings.localPcssMaxPenumbraTexels = 8.0f;
        settings.localProductionFilterEnabled = true;
        settings.localFaceBlendStrength = 0.80f;
        settings.rectLightShadowSampleTiles = 4u;
        settings.contactShadowStrength = 0.56f;
        settings.contactShadowLength = 0.34f;
        settings.contactShadowThickness = 0.16f;
        settings.contactShadowSteps = 8;
        settings.contactShadowJitterStrength = 0.20f;
        settings.contactShadowEdgeFadePixels = 30.0f;
        settings.ssaoStrength = 0.64f;
        settings.ssaoRadius = 2.0f;
        settings.ssaoBias = 0.02f;
        settings.ssaoSampleCount = 16;
        settings.ssrStrength = 0.78f;
        settings.ssrRayLength = 36.0f;
        settings.ssrThickness = 0.05f;
        settings.ssrStepCount = 22;
        settings.ssrTemporalHistoryLockEnabled = true;
        settings.ssrTemporalMissHistoryRejectEnabled = true;
        settings.cascadeCount = 4;
        settings.cascadeSplitLambda = 0.78f;
        settings.cascadeMaxDistance = 360.0f;
        settings.cascadeBlendRatio = 0.12f;
        settings.cascadeFadeRatio = 0.08f;
        settings.mapSize = 4096;
        break;
    }

    SyncLocalShadowKindFiltersToShared(settings);
}

inline void ApplyForward3DShadowProductionOverrides(VulkanShadowSettings& settings) {
    if (!settings.enabled || settings.quality == VulkanShadowQuality::Off) {
        return;
    }

    settings.stableCascades = true;
    switch (settings.quality) {
    case VulkanShadowQuality::Low:
        settings.cascadesEnabled = false;
        settings.cascadeCount = 1u;
        settings.cascadeMaxDistance = 45.0f;
        settings.biasMin = 0.0028f;
        settings.biasSlope = 0.0074f;
        settings.pcfRadius = 1.0f;
        settings.pcfKernelRadius = 1u;
        settings.pcssStrength = 0.0f;
        settings.directionalFilterMode = VulkanDirectionalShadowFilterMode::OptimizedTentPcf;
        settings.directionalFilterSampleCount = 9u;
        settings.directionalFilterKernelWidth = 3u;
        settings.directionalFilterReceiverBiasExtentTexels = 1.5f;
        settings.localPcfRadius = 0.75f;
        settings.localPcfKernelRadius = 1u;
        settings.localPcssStrength = 0.0f;
        settings.localPcssBlockerSampleCount = 0u;
        settings.localPcssFilterSampleCount = 4u;
        settings.localPcssSearchRadiusTexels = 0.0f;
        settings.localPcssMaxPenumbraTexels = 1.5f;
        settings.localFaceBlendStrength = 0.35f;
        settings.contactShadowStrength = 0.16f;
        settings.contactShadowSteps = 2u;
        settings.contactShadowJitterStrength = 0.08f;
        break;
    case VulkanShadowQuality::Medium:
        settings.cascadesEnabled = true;
        settings.cascadeCount = 3u;
        settings.cascadeMaxDistance = 55.0f;
        settings.biasMin = 0.0024f;
        settings.biasSlope = 0.0068f;
        settings.pcfRadius = 1.0f;
        settings.pcfKernelRadius = 1u;
        settings.pcssStrength = 0.0f;
        settings.directionalFilterMode = VulkanDirectionalShadowFilterMode::OptimizedTentPcf;
        settings.directionalFilterSampleCount = 9u;
        settings.directionalFilterKernelWidth = 3u;
        settings.directionalFilterReceiverBiasExtentTexels = 1.5f;
        settings.localPcfRadius = 1.4f;
        settings.localPcfKernelRadius = 1u;
        settings.localPcssStrength = 0.16f;
        settings.localPcssBlockerSampleCount = 8u;
        settings.localPcssFilterSampleCount = 8u;
        settings.localPcssSearchRadiusTexels = 4.0f;
        settings.localPcssMaxPenumbraTexels = 4.0f;
        settings.localFaceBlendStrength = 0.60f;
        settings.contactShadowJitterStrength = 0.12f;
        break;
    case VulkanShadowQuality::High:
        settings.cascadesEnabled = true;
        settings.cascadeCount = 4u;
        settings.cascadeMaxDistance = kForward3DShadowCascadeMaxDistance;
        settings.biasMin = 0.0022f;
        settings.biasSlope = 0.0064f;
        settings.pcfRadius = 1.0f;
        settings.pcfKernelRadius = 2u;
        settings.pcssStrength = 0.0f;
        settings.directionalFilterMode = VulkanDirectionalShadowFilterMode::OptimizedTentPcf;
        settings.directionalFilterSampleCount = 9u;
        settings.directionalFilterKernelWidth = 5u;
        settings.directionalFilterReceiverBiasExtentTexels = 2.5f;
        settings.localPcfRadius = 1.4f;
        settings.localPcfKernelRadius = 2u;
        settings.localPcssStrength = 0.28f;
        settings.localPcssBlockerSampleCount = 8u;
        settings.localPcssFilterSampleCount = 12u;
        settings.localPcssSearchRadiusTexels = 6.0f;
        settings.localPcssMaxPenumbraTexels = 6.0f;
        settings.localFaceBlendStrength = 0.85f;
        settings.contactShadowJitterStrength = 0.18f;
        break;
    case VulkanShadowQuality::Ultra:
        settings.cascadesEnabled = true;
        settings.cascadeCount = 4u;
        settings.cascadeMaxDistance = 75.0f;
        settings.biasMin = 0.0020f;
        settings.biasSlope = 0.0060f;
        settings.pcfRadius = 1.0f;
        settings.pcfKernelRadius = 2u;
        settings.pcssStrength = 1.0f;
        settings.directionalPcssBlockerSampleCount = 12u;
        settings.directionalPcssFilterSampleCount = 16u;
        settings.directionalPcssSearchRadiusTexels = 8.0f;
        settings.directionalPcssMaxPenumbraTexels = 8.0f;
        settings.directionalFilterMode = VulkanDirectionalShadowFilterMode::OptimizedTentPcf;
        settings.directionalFilterSampleCount = 9u;
        settings.directionalFilterKernelWidth = 5u;
        settings.directionalFilterReceiverBiasExtentTexels = 2.5f;
        settings.localPcfRadius = 2.8f;
        settings.localPcfKernelRadius = 2u;
        settings.localPcssStrength = 0.38f;
        settings.localPcssBlockerSampleCount = 12u;
        settings.localPcssFilterSampleCount = 16u;
        settings.localPcssSearchRadiusTexels = 8.0f;
        settings.localPcssMaxPenumbraTexels = 8.0f;
        settings.localFaceBlendStrength = 0.92f;
        settings.contactShadowSteps = 8u;
        settings.contactShadowJitterStrength = 0.20f;
        break;
    case VulkanShadowQuality::Off:
        break;
    }
    SyncLocalShadowKindFiltersToShared(settings);
}

inline void ApplyForward3DProductionShadowPreset(VulkanShadowSettings& settings) {
    ApplyShadowQualityPreset(settings, VulkanShadowQuality::Ultra);
    ApplyForward3DShadowProductionOverrides(settings);
}

inline void ResetShadowSettings(VulkanShadowSettings& settings) {
    settings = VulkanShadowSettings{};
}

}
