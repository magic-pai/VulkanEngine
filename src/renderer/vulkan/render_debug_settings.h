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
    ReflectionProbe = 35
};

struct VulkanRenderDebugSettings {
    ForwardDebugView forwardView = ForwardDebugView::Lit;
    f32 exposure = 1.0f;
};

inline void ResetRenderDebugSettings(VulkanRenderDebugSettings& settings) {
    settings = VulkanRenderDebugSettings{};
}

}
