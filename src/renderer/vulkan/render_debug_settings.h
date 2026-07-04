#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

enum class ForwardDebugView : int {
    Lit = 0,
    Albedo = 1,
    Normal = 2,
    Roughness = 3,
    Metallic = 4,
    Occlusion = 5,
    Shadow = 6,
    LightSpaceDepth = 7,
    ForwardLightComplexity = 8,
    DeferredHdr = 9,
    GBufferAlbedo = 10,
    GBufferNormal = 11,
    GBufferRoughness = 12,
    GBufferMetallic = 13,
    GBufferMaterialId = 14,
    GBufferDepth = 15,
    GBufferEmissive = 16,
    GBufferVelocity = 17,
    DeferredShadow = 18,
    DeferredDirect = 19,
    DeferredAmbient = 20,
    DeferredSpecular = 21,
    DeferredLightComplexity = 22,
    DeferredTileOccupancy = 23,
    DeferredMaterialTable = 24,
    ShadowCascade = 25,
    LocalShadowAtlas = 26,
    LocalShadowVisibility = 27,
    ContactShadow = 28,
    LocalShadowFace = 29,
    WeightedTranslucencyAccum = 30,
    WeightedTranslucencyRevealage = 31,
    WeightedTranslucencyWeight = 32,
    Ssao = 33,
    Ssr = 34,
    ReflectionProbe = 35,
    HeightFog = 36,
    Bloom = 37,
    ColorGrading = 38,
    ToneMapping = 39,
    AutoExposure = 40,
    Sharpening = 41,
    ProbeGrid = 42,
    ProbeGridCell = 43,
    Taa = 44,
    TaaRejection = 45
};

struct VulkanRenderDebugSettings {
    ForwardDebugView forwardView = ForwardDebugView::Lit;
    f32 exposure = 1.0f;
    u32 toneMapMode = 0;
    f32 toneMapWhitePoint = 4.0f;
    bool autoExposureEnabled = false;
    f32 autoExposureTargetLuminance = 0.18f;
    f32 autoExposureMin = 0.25f;
    f32 autoExposureMax = 4.0f;
    f32 autoExposureAdaptation = 1.0f;
    bool bloomEnabled = false;
    f32 bloomIntensity = 0.35f;
    f32 bloomThreshold = 1.0f;
    f32 bloomRadiusPixels = 2.5f;
    bool colorGradingEnabled = false;
    f32 colorGradingSaturation = 1.0f;
    f32 colorGradingContrast = 1.0f;
    f32 colorGradingGamma = 1.0f;
    f32 colorGradingLutStrength = 1.0f;
    bool sharpeningEnabled = false;
    f32 sharpeningStrength = 0.35f;
    f32 sharpeningRadiusPixels = 1.0f;
};

inline void ResetRenderDebugSettings(VulkanRenderDebugSettings& settings) {
    settings = VulkanRenderDebugSettings{};
}

}
