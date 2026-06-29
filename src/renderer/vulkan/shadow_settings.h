#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

inline constexpr std::size_t kMaxDirectionalShadowCascades = 4;

enum class VulkanShadowQuality : int {
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Ultra = 4
};

struct VulkanShadowSettings {
    VulkanShadowQuality quality = VulkanShadowQuality::Medium;
    bool enabled = true;
    bool cascadesEnabled = true;
    bool stableCascades = true;
    f32 strength = 0.95f;
    f32 ambientStrength = 0.42f;
    f32 biasMin = 0.00045f;
    f32 biasSlope = 0.0012f;
    f32 pcfRadius = 1.0f;
    u32 pcfKernelRadius = 1;
    f32 pcssStrength = 0.0f;
    f32 localBiasMin = 0.0009f;
    f32 localBiasSlope = 0.0024f;
    f32 localPcfRadius = 1.0f;
    u32 localPcfKernelRadius = 1;
    f32 localPcssStrength = 0.0f;
    f32 localFaceBlendStrength = 0.35f;
    f32 contactShadowStrength = 0.35f;
    f32 contactShadowLength = 0.18f;
    f32 contactShadowThickness = 0.08f;
    u32 contactShadowSteps = 4;
    f32 contactShadowJitterStrength = 0.35f;
    f32 contactShadowEdgeFadePixels = 18.0f;
    f32 ssaoStrength = 0.45f;
    f32 ssaoRadius = 1.2f;
    f32 ssaoBias = 0.035f;
    u32 ssaoSampleCount = 8;
    f32 ssrStrength = 0.55f;
    f32 ssrRayLength = 18.0f;
    f32 ssrThickness = 0.08f;
    u32 ssrStepCount = 12;
    f32 cascadeSplitLambda = 0.68f;
    f32 cascadeMaxDistance = 250.0f;
    f32 cascadeBlendRatio = 0.08f;
    f32 cascadeFadeRatio = 0.12f;
    u32 mapSize = 2048;
    u32 cascadeCount = 3;
};

inline void ApplyShadowQualityPreset(
    VulkanShadowSettings& settings,
    VulkanShadowQuality quality
) {
    settings.quality = quality;

    switch (quality) {
    case VulkanShadowQuality::Off:
        settings.enabled = false;
        settings.cascadesEnabled = false;
        settings.strength = 0.0f;
        settings.ambientStrength = 0.0f;
        settings.pcfRadius = 0.0f;
        settings.pcfKernelRadius = 0;
        settings.pcssStrength = 0.0f;
        settings.localBiasMin = 0.0f;
        settings.localBiasSlope = 0.0f;
        settings.localPcfRadius = 0.0f;
        settings.localPcfKernelRadius = 0;
        settings.localPcssStrength = 0.0f;
        settings.localFaceBlendStrength = 0.0f;
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
        settings.strength = 0.72f;
        settings.ambientStrength = 0.25f;
        settings.pcfRadius = 0.0f;
        settings.pcfKernelRadius = 0;
        settings.pcssStrength = 0.0f;
        settings.localBiasMin = 0.0011f;
        settings.localBiasSlope = 0.0028f;
        settings.localPcfRadius = 0.0f;
        settings.localPcfKernelRadius = 0;
        settings.localPcssStrength = 0.0f;
        settings.localFaceBlendStrength = 0.0f;
        settings.contactShadowStrength = 0.0f;
        settings.contactShadowLength = 0.0f;
        settings.contactShadowThickness = 0.0f;
        settings.contactShadowSteps = 0;
        settings.contactShadowJitterStrength = 0.0f;
        settings.contactShadowEdgeFadePixels = 0.0f;
        settings.ssaoStrength = 0.22f;
        settings.ssaoRadius = 0.75f;
        settings.ssaoBias = 0.05f;
        settings.ssaoSampleCount = 4;
        settings.ssrStrength = 0.35f;
        settings.ssrRayLength = 10.0f;
        settings.ssrThickness = 0.12f;
        settings.ssrStepCount = 8;
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
        settings.strength = 0.95f;
        settings.ambientStrength = 0.42f;
        settings.pcfRadius = 1.0f;
        settings.pcfKernelRadius = 1;
        settings.pcssStrength = 0.0f;
        settings.localBiasMin = 0.0009f;
        settings.localBiasSlope = 0.0024f;
        settings.localPcfRadius = 1.0f;
        settings.localPcfKernelRadius = 1;
        settings.localPcssStrength = 0.0f;
        settings.localFaceBlendStrength = 0.35f;
        settings.contactShadowStrength = 0.35f;
        settings.contactShadowLength = 0.18f;
        settings.contactShadowThickness = 0.08f;
        settings.contactShadowSteps = 4;
        settings.contactShadowJitterStrength = 0.35f;
        settings.contactShadowEdgeFadePixels = 18.0f;
        settings.ssaoStrength = 0.45f;
        settings.ssaoRadius = 1.2f;
        settings.ssaoBias = 0.035f;
        settings.ssaoSampleCount = 8;
        settings.ssrStrength = 0.55f;
        settings.ssrRayLength = 18.0f;
        settings.ssrThickness = 0.08f;
        settings.ssrStepCount = 12;
        settings.cascadeCount = 3;
        settings.cascadeSplitLambda = 0.68f;
        settings.cascadeMaxDistance = 250.0f;
        settings.cascadeBlendRatio = 0.08f;
        settings.cascadeFadeRatio = 0.12f;
        settings.mapSize = 2048;
        break;
    case VulkanShadowQuality::High:
        settings.enabled = true;
        settings.cascadesEnabled = true;
        settings.strength = 1.0f;
        settings.ambientStrength = 0.50f;
        settings.pcfRadius = 1.8f;
        settings.pcfKernelRadius = 2;
        settings.pcssStrength = 0.35f;
        settings.localBiasMin = 0.00075f;
        settings.localBiasSlope = 0.0020f;
        settings.localPcfRadius = 1.6f;
        settings.localPcfKernelRadius = 2;
        settings.localPcssStrength = 0.30f;
        settings.localFaceBlendStrength = 0.55f;
        settings.contactShadowStrength = 0.48f;
        settings.contactShadowLength = 0.28f;
        settings.contactShadowThickness = 0.12f;
        settings.contactShadowSteps = 6;
        settings.contactShadowJitterStrength = 0.55f;
        settings.contactShadowEdgeFadePixels = 24.0f;
        settings.ssaoStrength = 0.58f;
        settings.ssaoRadius = 1.7f;
        settings.ssaoBias = 0.025f;
        settings.ssaoSampleCount = 12;
        settings.ssrStrength = 0.70f;
        settings.ssrRayLength = 28.0f;
        settings.ssrThickness = 0.06f;
        settings.ssrStepCount = 18;
        settings.cascadeCount = 4;
        settings.cascadeSplitLambda = 0.72f;
        settings.cascadeMaxDistance = 500.0f;
        settings.cascadeBlendRatio = 0.10f;
        settings.cascadeFadeRatio = 0.10f;
        settings.mapSize = 4096;
        break;
    case VulkanShadowQuality::Ultra:
        settings.enabled = true;
        settings.cascadesEnabled = true;
        settings.strength = 1.0f;
        settings.ambientStrength = 0.58f;
        settings.pcfRadius = 2.6f;
        settings.pcfKernelRadius = 2;
        settings.pcssStrength = 0.55f;
        settings.localBiasMin = 0.00065f;
        settings.localBiasSlope = 0.0018f;
        settings.localPcfRadius = 2.2f;
        settings.localPcfKernelRadius = 2;
        settings.localPcssStrength = 0.45f;
        settings.localFaceBlendStrength = 0.75f;
        settings.contactShadowStrength = 0.60f;
        settings.contactShadowLength = 0.36f;
        settings.contactShadowThickness = 0.16f;
        settings.contactShadowSteps = 8;
        settings.contactShadowJitterStrength = 0.75f;
        settings.contactShadowEdgeFadePixels = 30.0f;
        settings.ssaoStrength = 0.68f;
        settings.ssaoRadius = 2.1f;
        settings.ssaoBias = 0.02f;
        settings.ssaoSampleCount = 16;
        settings.ssrStrength = 0.82f;
        settings.ssrRayLength = 42.0f;
        settings.ssrThickness = 0.05f;
        settings.ssrStepCount = 24;
        settings.cascadeCount = 4;
        settings.cascadeSplitLambda = 0.78f;
        settings.cascadeMaxDistance = 1000.0f;
        settings.cascadeBlendRatio = 0.12f;
        settings.cascadeFadeRatio = 0.08f;
        settings.mapSize = 4096;
        break;
    }
}

inline void ResetShadowSettings(VulkanShadowSettings& settings) {
    settings = VulkanShadowSettings{};
}

}
