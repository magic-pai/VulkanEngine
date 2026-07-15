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
    GBufferOcclusion = 14,
    GBufferMaterialId = 15,
    GBufferDepth = 16,
    GBufferEmissive = 17,
    GBufferVelocity = 18,
    DeferredShadow = 19,
    DeferredDirect = 20,
    DeferredAmbient = 21,
    DeferredSpecular = 22,
    DeferredLightComplexity = 23,
    DeferredTileOccupancy = 24,
    DeferredMaterialTable = 25,
    ShadowCascade = 26,
    LocalShadowAtlas = 27,
    LocalShadowVisibility = 28,
    ContactShadow = 29,
    LocalShadowFace = 30,
    WeightedTranslucencyAccum = 31,
    WeightedTranslucencyRevealage = 32,
    WeightedTranslucencyWeight = 33,
    Ssao = 34,
    Ssr = 35,
    ReflectionProbe = 36,
    HeightFog = 37,
    Bloom = 38,
    ColorGrading = 39,
    ToneMapping = 40,
    AutoExposure = 41,
    Sharpening = 42,
    ProbeGrid = 43,
    ProbeGridCell = 44,
    Taa = 45,
    TaaRejection = 46,
    TaaHistory = 47,
    TaaReprojection = 48,
    ShadowCascadeAtlas = 49,
    ReflectionProbeContrast = 50,
    DeferredAmbientDiffuse = 51,
    DeferredAmbientSpecular = 52,
    DeferredAmbientProbe = 53,
    LocalShadowSelected = 54,
    DeferredEnergyBalance = 55,
    ShadowCascadeReceiver = 56
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
    i32 localShadowDebugLightIndex = -1;
};

inline void ResetRenderDebugSettings(VulkanRenderDebugSettings& settings) {
    settings = VulkanRenderDebugSettings{};
}

}
