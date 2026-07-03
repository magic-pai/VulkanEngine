#include "renderer/vulkan/imgui_layer.h"

#include "platform/window.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/render_debug_settings.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "renderer/vulkan/render_pass.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/shadow_settings.h"
#include "renderer/vulkan/swapchain.h"
#include "scene/camera_2d.h"
#include "scene/camera_3d.h"
#include "scene/renderable_2d.h"
#include "scene/renderable_3d.h"
#include "scene/scene_2d.h"
#include "scene/scene_3d.h"
#include "scene/transform.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace se {

namespace {

void CheckVkResult(VkResult result) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Dear ImGui Vulkan backend reported a Vulkan error");
    }
}

void ResetBlackHoleParameters(MaterialProperties& properties) {
    properties.baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
    properties.textureMix = 0.7f;
    properties.custom[1] = 1.0f;
    properties.custom[2] = 0.25f;
    properties.custom[3] = 0.55f;
    properties.viewControls = { 1.0f, 1.05f, 0.12f, 0.0f };
    properties.cameraControls[0] = 15.0f;
    properties.cameraControls[1] = 1.0f;
    properties.cameraControls[2] = 0.8f;
    properties.cameraControls[3] = 0.5f;
}

void ResetForward3DParameters(MaterialProperties& properties) {
    properties.baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
    properties.textureMix = 0.0f;
    properties.custom = { -0.45f, -0.82f, -0.35f, 0.22f };
    properties.viewControls = { 0.78f, 0.24f, 48.0f, 0.0f };
}

const char* ReflectionCaptureSourceName(u32 source) {
    switch (source) {
    case 0:
        return "none";
    case 1:
        return "built-in procedural";
    case 2:
        return "authored cubemap";
    case 3:
        return "captured scene";
    default:
        return "unknown";
    }
}

const char* ReflectionCaptureFallbackReasonName(u32 reason) {
    switch (reason) {
    case 0:
        return "none";
    case 1:
        return "source disabled";
    case 2:
        return "authored cubemap not loaded";
    case 3:
        return "captured scene not implemented";
    case 4:
        return "built-in resource unavailable";
    case 5:
        return "cubemap sampling disabled";
    case 6:
        return "no active scene probe";
    case 7:
        return "fallback disabled";
    case 8:
        return "authored cubemap asset missing";
    default:
        return "unknown";
    }
}

void DrawBlackHoleControls(MaterialProperties& properties) {
    ImGui::SeparatorText("Black Hole");
    ImGui::SliderFloat("Lensing", &properties.custom[1], 0.0f, 2.0f);
    ImGui::SliderFloat("Disk light", &properties.custom[2], 0.0f, 4.0f);
    ImGui::SliderFloat("Disk height", &properties.custom[3], 0.03f, 1.0f);
    ImGui::SliderFloat("Noise scale", &properties.cameraControls[2], 0.0f, 10.0f);
    ImGui::SliderFloat("Disk speed", &properties.cameraControls[3], 0.0f, 1.0f);
    ImGui::SliderFloat("Render quality", &properties.textureMix, 0.35f, 1.0f);

    ImGui::SeparatorText("Skybox");
    ImGui::SliderFloat("Exposure", &properties.viewControls[0], 0.0f, 2.0f);
    ImGui::SliderFloat("Saturation", &properties.viewControls[1], 0.0f, 2.0f);
    ImGui::SliderFloat("Blur", &properties.viewControls[2], 0.0f, 3.0f);

    ImGui::SeparatorText("View");
    ImGui::SliderFloat("Distance", &properties.cameraControls[0], 3.0f, 35.0f);
    ImGui::SliderFloat("FOV", &properties.cameraControls[1], 0.35f, 2.3f);
}

void DrawCamera3DControls(Camera3D& camera) {
    ImGui::SeparatorText("Camera3D");

    const glm::vec3& position = camera.Position();
    const glm::vec3& forward = camera.Forward();
    ImGui::Text("Position: %.2f, %.2f, %.2f", position.x, position.y, position.z);
    ImGui::Text("Forward: %.2f, %.2f, %.2f", forward.x, forward.y, forward.z);

    f32 distance = camera.Distance();
    if (ImGui::SliderFloat("Orbit distance", &distance, 3.0f, 35.0f)) {
        camera.SetDistance(distance);
    }

    f32 fovScale = camera.FovScale();
    if (ImGui::SliderFloat("FOV scale", &fovScale, 0.35f, 2.3f)) {
        camera.SetFovScale(fovScale);
    }

    f32 moveSpeed = camera.MoveSpeed();
    if (ImGui::SliderFloat("Move speed", &moveSpeed, 0.05f, 40.0f)) {
        camera.SetMoveSpeed(moveSpeed);
    }

    f32 nearClip = camera.NearClip();
    f32 farClip = camera.FarClip();
    const bool nearChanged = ImGui::DragFloat(
        "Near clip",
        &nearClip,
        0.01f,
        0.001f,
        1000.0f
    );
    const bool farChanged = ImGui::DragFloat(
        "Far clip",
        &farClip,
        100.0f,
        1.0f,
        1000000.0f
    );
    if (nearChanged || farChanged) {
        camera.SetClipPlanes(nearClip, farClip);
    }

    if (ImGui::Button("Reset orbit")) {
        camera.ResetOrbit();
    }
}

void DrawShadowControls(VulkanShadowSettings& settings) {
    ImGui::SeparatorText("Shadows");

    auto qualityName = [](VulkanShadowQuality quality) {
        switch (quality) {
        case VulkanShadowQuality::Off:
            return "Off";
        case VulkanShadowQuality::Low:
            return "Low";
        case VulkanShadowQuality::Medium:
            return "Medium";
        case VulkanShadowQuality::High:
            return "High";
        case VulkanShadowQuality::Ultra:
            return "Ultra";
        }

        return "Medium";
    };

    static constexpr VulkanShadowQuality kQualities[] = {
        VulkanShadowQuality::Off,
        VulkanShadowQuality::Low,
        VulkanShadowQuality::Medium,
        VulkanShadowQuality::High,
        VulkanShadowQuality::Ultra
    };

    if (ImGui::BeginCombo("Quality##Shadow", qualityName(settings.quality))) {
        for (const VulkanShadowQuality quality : kQualities) {
            const bool selected = settings.quality == quality;
            if (ImGui::Selectable(qualityName(quality), selected)) {
                ApplyShadowQualityPreset(settings, quality);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    static constexpr u32 kShadowMapSizes[] = { 512, 1024, 2048, 4096 };
    const char* currentSizeLabel = "Custom";
    char sizeLabels[4][16]{};
    for (std::size_t index = 0; index < std::size(kShadowMapSizes); ++index) {
        std::snprintf(
            sizeLabels[index],
            sizeof(sizeLabels[index]),
            "%u",
            kShadowMapSizes[index]
        );
        if (settings.mapSize == kShadowMapSizes[index]) {
            currentSizeLabel = sizeLabels[index];
        }
    }

    if (ImGui::BeginCombo("Map size##Shadow", currentSizeLabel)) {
        for (std::size_t index = 0; index < std::size(kShadowMapSizes); ++index) {
            const bool selected = settings.mapSize == kShadowMapSizes[index];
            if (ImGui::Selectable(sizeLabels[index], selected)) {
                settings.mapSize = kShadowMapSizes[index];
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::Checkbox("Enabled##Shadow", &settings.enabled);
    ImGui::Checkbox("Cascades##Shadow", &settings.cascadesEnabled);
    ImGui::Checkbox("Stable cascades##Shadow", &settings.stableCascades);
    int cascadeCount = static_cast<int>(settings.cascadeCount);
    if (ImGui::SliderInt(
            "Cascade count##Shadow",
            &cascadeCount,
            1,
            static_cast<int>(kMaxDirectionalShadowCascades)
        )) {
        settings.cascadeCount = static_cast<u32>(std::clamp(
            cascadeCount,
            1,
            static_cast<int>(kMaxDirectionalShadowCascades)
        ));
    }
    ImGui::SliderFloat("Strength##Shadow", &settings.strength, 0.0f, 1.0f);
    ImGui::SliderFloat("Ambient shadow##Shadow", &settings.ambientStrength, 0.0f, 1.0f);
    ImGui::SliderFloat("Bias min##Shadow", &settings.biasMin, 0.0f, 0.006f, "%.5f");
    ImGui::SliderFloat("Bias slope##Shadow", &settings.biasSlope, 0.0f, 0.012f, "%.5f");
    ImGui::SliderFloat("PCF radius##Shadow", &settings.pcfRadius, 0.0f, 3.0f);
    int pcfKernelRadius = static_cast<int>(settings.pcfKernelRadius);
    if (ImGui::SliderInt("PCF kernel radius##Shadow", &pcfKernelRadius, 0, 2)) {
        settings.pcfKernelRadius = static_cast<u32>(std::clamp(pcfKernelRadius, 0, 2));
    }
    ImGui::SliderFloat("PCSS strength##Shadow", &settings.pcssStrength, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Cascade lambda##Shadow", &settings.cascadeSplitLambda, 0.0f, 1.0f);
    ImGui::SliderFloat("Cascade distance##Shadow", &settings.cascadeMaxDistance, 25.0f, 2000.0f);
    ImGui::SliderFloat("Cascade blend##Shadow", &settings.cascadeBlendRatio, 0.0f, 0.25f, "%.3f");
    ImGui::SliderFloat("Cascade fade##Shadow", &settings.cascadeFadeRatio, 0.0f, 0.35f, "%.3f");
    ImGui::SeparatorText("Contact shadows");
    ImGui::SliderFloat(
        "Contact strength##Shadow",
        &settings.contactShadowStrength,
        0.0f,
        1.0f,
        "%.3f"
    );
    ImGui::SliderFloat(
        "Contact length##Shadow",
        &settings.contactShadowLength,
        0.0f,
        1.0f,
        "%.3f"
    );
    ImGui::SliderFloat(
        "Contact thickness##Shadow",
        &settings.contactShadowThickness,
        0.0f,
        0.5f,
        "%.3f"
    );
    int contactShadowSteps = static_cast<int>(settings.contactShadowSteps);
    if (ImGui::SliderInt("Contact steps##Shadow", &contactShadowSteps, 0, 12)) {
        settings.contactShadowSteps =
            static_cast<u32>(std::clamp(contactShadowSteps, 0, 12));
    }
    ImGui::SliderFloat(
        "Contact jitter##Shadow",
        &settings.contactShadowJitterStrength,
        0.0f,
        1.0f,
        "%.3f"
    );
    ImGui::SliderFloat(
        "Contact edge fade px##Shadow",
        &settings.contactShadowEdgeFadePixels,
        0.0f,
        96.0f,
        "%.1f"
    );
    ImGui::SeparatorText("SSAO");
    ImGui::SliderFloat("SSAO strength##Shadow", &settings.ssaoStrength, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("SSAO radius##Shadow", &settings.ssaoRadius, 0.0f, 8.0f, "%.2f");
    ImGui::SliderFloat("SSAO bias##Shadow", &settings.ssaoBias, 0.0f, 0.5f, "%.3f");
    int ssaoSampleCount = static_cast<int>(settings.ssaoSampleCount);
    if (ImGui::SliderInt("SSAO samples##Shadow", &ssaoSampleCount, 0, 16)) {
        settings.ssaoSampleCount =
            static_cast<u32>(std::clamp(ssaoSampleCount, 0, 16));
    }
    ImGui::SeparatorText("SSR");
    ImGui::SliderFloat("SSR strength##Shadow", &settings.ssrStrength, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("SSR ray length##Shadow", &settings.ssrRayLength, 0.0f, 64.0f, "%.1f");
    ImGui::SliderFloat("SSR thickness##Shadow", &settings.ssrThickness, 0.0f, 0.5f, "%.3f");
    int ssrStepCount = static_cast<int>(settings.ssrStepCount);
    if (ImGui::SliderInt("SSR steps##Shadow", &ssrStepCount, 0, 32)) {
        settings.ssrStepCount =
            static_cast<u32>(std::clamp(ssrStepCount, 0, 32));
    }
    ImGui::SeparatorText("Reflection fallback");
    ImGui::Checkbox("Global reflection fallback##Shadow", &settings.reflectionProbeFallbackEnabled);
    ImGui::SliderFloat(
        "Probe diffuse intensity##Shadow",
        &settings.reflectionProbeDiffuseIntensity,
        0.0f,
        4.0f,
        "%.2f"
    );
    ImGui::SliderFloat(
        "Probe specular intensity##Shadow",
        &settings.reflectionProbeSpecularIntensity,
        0.0f,
        4.0f,
        "%.2f"
    );
    ImGui::SliderFloat(
        "Probe horizon blend##Shadow",
        &settings.reflectionProbeHorizonBlend,
        0.0f,
        1.0f,
        "%.2f"
    );
    ImGui::Checkbox("Local reflection probe##Shadow", &settings.localReflectionProbeEnabled);
    ImGui::SliderFloat(
        "Local probe radius##Shadow",
        &settings.localReflectionProbeRadius,
        0.1f,
        64.0f,
        "%.2f"
    );
    ImGui::SliderFloat(
        "Local probe intensity##Shadow",
        &settings.localReflectionProbeIntensity,
        0.0f,
        4.0f,
        "%.2f"
    );
    ImGui::SliderFloat(
        "Local probe blend##Shadow",
        &settings.localReflectionProbeBlendStrength,
        0.0f,
        1.0f,
        "%.2f"
    );
    ImGui::SliderFloat(
        "Local probe falloff##Shadow",
        &settings.localReflectionProbeFalloff,
        0.25f,
        8.0f,
        "%.2f"
    );
    float localProbeColor[3]{
        settings.localReflectionProbeColorR,
        settings.localReflectionProbeColorG,
        settings.localReflectionProbeColorB
    };
    if (ImGui::ColorEdit3("Local probe color##Shadow", localProbeColor)) {
        settings.localReflectionProbeColorR = localProbeColor[0];
        settings.localReflectionProbeColorG = localProbeColor[1];
        settings.localReflectionProbeColorB = localProbeColor[2];
    }
    ImGui::SeparatorText("Height fog");
    ImGui::Checkbox("Height fog##Shadow", &settings.heightFogEnabled);
    ImGui::SliderFloat(
        "Fog density##Shadow",
        &settings.heightFogDensity,
        0.0f,
        0.25f,
        "%.4f"
    );
    ImGui::SliderFloat(
        "Fog height falloff##Shadow",
        &settings.heightFogHeightFalloff,
        0.0f,
        0.6f,
        "%.3f"
    );
    ImGui::SliderFloat(
        "Fog start distance##Shadow",
        &settings.heightFogStartDistance,
        0.0f,
        80.0f,
        "%.1f"
    );
    ImGui::SliderFloat(
        "Fog max opacity##Shadow",
        &settings.heightFogMaxOpacity,
        0.0f,
        1.0f,
        "%.3f"
    );
    float heightFogColor[3]{
        settings.heightFogColorR,
        settings.heightFogColorG,
        settings.heightFogColorB
    };
    if (ImGui::ColorEdit3("Fog color##Shadow", heightFogColor)) {
        settings.heightFogColorR = heightFogColor[0];
        settings.heightFogColorG = heightFogColor[1];
        settings.heightFogColorB = heightFogColor[2];
    }
    ImGui::SeparatorText("Local shadows");
    ImGui::SliderFloat(
        "Local bias min##Shadow",
        &settings.localBiasMin,
        0.0f,
        0.02f,
        "%.5f"
    );
    ImGui::SliderFloat(
        "Local bias slope##Shadow",
        &settings.localBiasSlope,
        0.0f,
        0.05f,
        "%.5f"
    );
    ImGui::SliderFloat(
        "Local PCF radius##Shadow",
        &settings.localPcfRadius,
        0.0f,
        4.0f,
        "%.2f"
    );
    int localPcfKernelRadius = static_cast<int>(settings.localPcfKernelRadius);
    if (ImGui::SliderInt(
            "Local PCF kernel radius##Shadow",
            &localPcfKernelRadius,
            0,
            2
        )) {
        settings.localPcfKernelRadius =
            static_cast<u32>(std::clamp(localPcfKernelRadius, 0, 2));
    }
    ImGui::SliderFloat(
        "Local PCSS strength##Shadow",
        &settings.localPcssStrength,
        0.0f,
        1.0f,
        "%.3f"
    );
    ImGui::SliderFloat(
        "Local face blend##Shadow",
        &settings.localFaceBlendStrength,
        0.0f,
        1.0f,
        "%.3f"
    );

    if (ImGui::Button("Reset shadows")) {
        ResetShadowSettings(settings);
    }
}

const char* ForwardDebugViewName(ForwardDebugView view) {
    switch (view) {
    case ForwardDebugView::Lit:
        return "Lit";
    case ForwardDebugView::Albedo:
        return "Albedo";
    case ForwardDebugView::Normal:
        return "Normal";
    case ForwardDebugView::Roughness:
        return "Roughness";
    case ForwardDebugView::Metallic:
        return "Metallic";
    case ForwardDebugView::Occlusion:
        return "Occlusion";
    case ForwardDebugView::Shadow:
        return "Shadow";
    case ForwardDebugView::LightSpaceDepth:
        return "Light depth";
    case ForwardDebugView::ForwardLightComplexity:
        return "Forward Light Complexity";
    case ForwardDebugView::DeferredHdr:
        return "Deferred HDR";
    case ForwardDebugView::GBufferAlbedo:
        return "GBuffer Albedo";
    case ForwardDebugView::GBufferNormal:
        return "GBuffer Normal";
    case ForwardDebugView::GBufferRoughness:
        return "GBuffer Roughness";
    case ForwardDebugView::GBufferMetallic:
        return "GBuffer Metallic";
    case ForwardDebugView::GBufferMaterialId:
        return "GBuffer Material Id";
    case ForwardDebugView::GBufferDepth:
        return "GBuffer Depth";
    case ForwardDebugView::GBufferEmissive:
        return "GBuffer Emissive";
    case ForwardDebugView::GBufferVelocity:
        return "GBuffer Velocity";
    case ForwardDebugView::DeferredShadow:
        return "Deferred Shadow";
    case ForwardDebugView::DeferredDirect:
        return "Deferred Direct";
    case ForwardDebugView::DeferredAmbient:
        return "Deferred Ambient";
    case ForwardDebugView::DeferredSpecular:
        return "Deferred Specular";
    case ForwardDebugView::DeferredLightComplexity:
        return "Deferred Light Complexity";
    case ForwardDebugView::DeferredTileOccupancy:
        return "Deferred Tile Occupancy";
    case ForwardDebugView::DeferredMaterialTable:
        return "Deferred Material Table";
    case ForwardDebugView::ShadowCascade:
        return "Shadow Cascade";
    case ForwardDebugView::LocalShadowAtlas:
        return "Local Shadow Atlas";
    case ForwardDebugView::LocalShadowVisibility:
        return "Local Shadow Visibility";
    case ForwardDebugView::ContactShadow:
        return "Contact Shadow";
    case ForwardDebugView::LocalShadowFace:
        return "Local Shadow Face";
    case ForwardDebugView::WeightedTranslucencyAccum:
        return "WBOIT Accum";
    case ForwardDebugView::WeightedTranslucencyRevealage:
        return "WBOIT Revealage";
    case ForwardDebugView::WeightedTranslucencyWeight:
        return "WBOIT Weight";
    case ForwardDebugView::Ssao:
        return "SSAO";
    case ForwardDebugView::Ssr:
        return "SSR";
    case ForwardDebugView::ReflectionProbe:
        return "Reflection Probe";
    case ForwardDebugView::HeightFog:
        return "Height Fog";
    case ForwardDebugView::Bloom:
        return "Bloom";
    case ForwardDebugView::ColorGrading:
        return "Color Grading";
    case ForwardDebugView::ToneMapping:
        return "Tone Mapping";
    case ForwardDebugView::AutoExposure:
        return "Auto Exposure";
    case ForwardDebugView::Sharpening:
        return "Sharpening";
    }

    return "Lit";
}

void DrawRenderDebugControls(VulkanRenderDebugSettings& settings) {
    static constexpr ForwardDebugView kViews[] = {
        ForwardDebugView::Lit,
        ForwardDebugView::Albedo,
        ForwardDebugView::Normal,
        ForwardDebugView::Roughness,
        ForwardDebugView::Metallic,
        ForwardDebugView::Occlusion,
        ForwardDebugView::Shadow,
        ForwardDebugView::LightSpaceDepth,
        ForwardDebugView::ForwardLightComplexity,
        ForwardDebugView::DeferredHdr,
        ForwardDebugView::GBufferAlbedo,
        ForwardDebugView::GBufferNormal,
        ForwardDebugView::GBufferRoughness,
        ForwardDebugView::GBufferMetallic,
        ForwardDebugView::GBufferMaterialId,
        ForwardDebugView::GBufferDepth,
        ForwardDebugView::GBufferEmissive,
        ForwardDebugView::GBufferVelocity,
        ForwardDebugView::DeferredShadow,
        ForwardDebugView::DeferredDirect,
        ForwardDebugView::DeferredAmbient,
        ForwardDebugView::DeferredSpecular,
        ForwardDebugView::DeferredLightComplexity,
        ForwardDebugView::DeferredTileOccupancy,
        ForwardDebugView::DeferredMaterialTable,
        ForwardDebugView::ShadowCascade,
        ForwardDebugView::LocalShadowAtlas,
        ForwardDebugView::LocalShadowVisibility,
        ForwardDebugView::ContactShadow,
        ForwardDebugView::LocalShadowFace,
        ForwardDebugView::WeightedTranslucencyAccum,
        ForwardDebugView::WeightedTranslucencyRevealage,
        ForwardDebugView::WeightedTranslucencyWeight,
        ForwardDebugView::Ssao,
        ForwardDebugView::Ssr,
        ForwardDebugView::ReflectionProbe,
        ForwardDebugView::HeightFog,
        ForwardDebugView::Bloom,
        ForwardDebugView::ColorGrading,
        ForwardDebugView::ToneMapping,
        ForwardDebugView::AutoExposure,
        ForwardDebugView::Sharpening
    };

    ImGui::SeparatorText("Render Debug");
    if (ImGui::BeginCombo("Forward view", ForwardDebugViewName(settings.forwardView))) {
        for (const ForwardDebugView view : kViews) {
            const bool selected = settings.forwardView == view;
            if (ImGui::Selectable(ForwardDebugViewName(view), selected)) {
                settings.forwardView = view;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SliderFloat("Debug exposure", &settings.exposure, 0.1f, 5.0f);
    ImGui::SeparatorText("Post");
    static constexpr const char* kToneMapModes[] = { "ACES", "Reinhard", "Linear Clamp" };
    int toneMapMode = static_cast<int>(std::clamp<u32>(settings.toneMapMode, 0u, 2u));
    if (ImGui::Combo("Tone map##Post", &toneMapMode, kToneMapModes, 3)) {
        settings.toneMapMode = static_cast<u32>(std::clamp(toneMapMode, 0, 2));
    }
    ImGui::SliderFloat("Tone white point##Post", &settings.toneMapWhitePoint, 0.1f, 64.0f, "%.2f");
    ImGui::Checkbox("Auto exposure##Post", &settings.autoExposureEnabled);
    ImGui::SliderFloat("Auto target luminance##Post", &settings.autoExposureTargetLuminance, 0.001f, 4.0f, "%.3f");
    ImGui::SliderFloat("Auto min exposure##Post", &settings.autoExposureMin, 0.001f, 32.0f, "%.3f");
    ImGui::SliderFloat("Auto max exposure##Post", &settings.autoExposureMax, 0.001f, 32.0f, "%.3f");
    ImGui::SliderFloat("Auto adaptation##Post", &settings.autoExposureAdaptation, 0.0f, 1.0f, "%.3f");
    ImGui::Checkbox("Bloom##Post", &settings.bloomEnabled);
    ImGui::SliderFloat("Bloom intensity##Post", &settings.bloomIntensity, 0.0f, 4.0f, "%.3f");
    ImGui::SliderFloat("Bloom threshold##Post", &settings.bloomThreshold, 0.0f, 8.0f, "%.3f");
    ImGui::SliderFloat("Bloom radius px##Post", &settings.bloomRadiusPixels, 0.0f, 24.0f, "%.2f");
    ImGui::Checkbox("Color grading##Post", &settings.colorGradingEnabled);
    ImGui::SliderFloat("Saturation##Post", &settings.colorGradingSaturation, 0.0f, 2.5f, "%.3f");
    ImGui::SliderFloat("Contrast##Post", &settings.colorGradingContrast, 0.0f, 2.5f, "%.3f");
    ImGui::SliderFloat("Gamma##Post", &settings.colorGradingGamma, 0.25f, 4.0f, "%.3f");
    ImGui::SliderFloat("LUT strength##Post", &settings.colorGradingLutStrength, 0.0f, 1.0f, "%.3f");
    ImGui::Checkbox("Sharpening##Post", &settings.sharpeningEnabled);
    ImGui::SliderFloat("Sharpen strength##Post", &settings.sharpeningStrength, 0.0f, 2.0f, "%.3f");
    ImGui::SliderFloat("Sharpen radius px##Post", &settings.sharpeningRadiusPixels, 0.0f, 4.0f, "%.2f");
    if (ImGui::Button("Reset render debug")) {
        ResetRenderDebugSettings(settings);
    }
}

void DrawPerformanceStats(const RendererStats& stats) {
    const RendererCpuStats& cpu = stats.cpu;
    const RendererDrawStats& draw = stats.draw;
    const RendererShadowCascadeStats& shadowCascades = stats.shadowCascades;
    const RendererLocalShadowAtlasStats& localShadowAtlas = stats.localShadowAtlas;
    const RendererWeightedTranslucencyStats& weightedTranslucency =
        stats.weightedTranslucency;
    const RendererBindStats& binds = stats.binds;
    const RendererGpuStats& gpu = stats.gpu;

    ImGui::SeparatorText("Performance");
    ImGui::Text("CPU frame: %.3f ms", cpu.totalFrameMs);
    if (gpu.available) {
        ImGui::Text("GPU recorded: %.3f ms", gpu.totalRecordedMs);
    } else {
        ImGui::TextUnformatted("GPU recorded: pending");
    }
    ImGui::Text(
        "Draws: %u main / %u gbuffer / %u overlay / %u shadow",
        draw.mainDraws,
        draw.gBufferDraws,
        draw.overlayDraws,
        draw.shadowDraws
    );
    ImGui::Text(
        "Hybrid routes: %u deferred / %u transparent / %u special / %u weighted / %u residual",
        draw.hybridDeferredOpaqueDraws,
        draw.hybridForwardTransparentDraws,
        draw.hybridForwardSpecialDraws,
        draw.hybridWeightedTranslucencyDraws,
        draw.hybridForwardResidualDraws
    );
    ImGui::Text(
        "Residual pass: %u draws / alpha ref %s / %u frame binds / %u shared light-list draws / %u material binds / %u mesh binds",
        binds.forwardResidualDraws,
        binds.forwardResidualAlphaReferenceEnabled ? "on" : "off",
        binds.forwardResidualFrameBinds,
        binds.forwardResidualSharedLightListDraws,
        binds.forwardResidualMaterialBinds,
        binds.forwardResidualMeshBinds
    );
    ImGui::Text(
        "Weighted sort: %u ops / %u transparent",
        draw.hybridWeightedTranslucencySortOps,
        draw.hybridWeightedTranslucencySortedTransparentDraws
    );
    ImGui::Text(
        "Residual sort: %u ops / %u transparent / %u stable special",
        draw.hybridForwardResidualSortOps,
        draw.hybridForwardResidualSortedTransparentDraws,
        draw.hybridForwardResidualStableSpecialDraws
    );
    ImGui::Text(
        "Depth bridge: %u copies / %u prefill draws / %u prefill mesh binds",
        binds.depthCopyOps,
        binds.depthPrefillDraws,
        binds.depthPrefillMeshBinds
    );
    ImGui::Text(
        "Weighted translucency: %s, accum %ux%u, revealage %ux%u, framebuffers %u, clears %u, draws %u, shared lights %u, shadow-ready %u, resolves %u, debug %u",
        weightedTranslucency.allocated ? "yes" : "no",
        weightedTranslucency.accumWidth,
        weightedTranslucency.accumHeight,
        weightedTranslucency.revealageWidth,
        weightedTranslucency.revealageHeight,
        weightedTranslucency.framebufferCount,
        weightedTranslucency.clearPasses,
        weightedTranslucency.draws,
        weightedTranslucency.sharedLightListDraws,
        weightedTranslucency.shadowReadyDraws,
        weightedTranslucency.resolveDraws,
        binds.weightedTranslucencyDebugDraws
    );
    ImGui::Text(
        "Weighted reference: alpha ref %s, mismatch draws %u",
        binds.forwardResidualAlphaReferenceEnabled ? "on" : "off",
        binds.weightedTranslucencyAlphaReferenceMismatchDraws
    );
    ImGui::Text(
        "Instancing: %u draws / %u instances",
        draw.mainInstancedDraws,
        draw.mainInstancedInstances
    );
    ImGui::Text(
        "Visible: %u main / %u overlay / %u shadow",
        draw.mainVisible,
        draw.overlayVisible,
        draw.shadowVisible
    );
    ImGui::Text(
        "Culled: %u main / %u overlay / %u shadow",
        draw.mainCulled,
        draw.overlayCulled,
        draw.shadowCulled
    );
    ImGui::Text(
        "Triangles: %llu main / %llu gbuffer / %llu overlay / %llu shadow",
        static_cast<unsigned long long>(draw.mainTriangles),
        static_cast<unsigned long long>(draw.gBufferTriangles),
        static_cast<unsigned long long>(draw.overlayTriangles),
        static_cast<unsigned long long>(draw.shadowTriangles)
    );
    ImGui::Text("Matrix recalcs: %u", draw.matrixRecalculations);
    ImGui::Text(
        "Shadow cascades: %u active / %u configured, stable %s, lambda %.2f, PCF %ux%u, PCSS %.3f, blend %.3f, fade %.3f, range %.2f-%.2f",
        shadowCascades.activeCount,
        shadowCascades.configuredCount,
        shadowCascades.stableSnappingEnabled ? "yes" : "no",
        shadowCascades.splitLambda,
        shadowCascades.pcfKernelRadius * 2u + 1u,
        shadowCascades.pcfKernelRadius * 2u + 1u,
        shadowCascades.pcssStrength,
        shadowCascades.blendRatio,
        shadowCascades.fadeRatio,
        shadowCascades.nearDepth,
        shadowCascades.farDepth
    );
    ImGui::Text(
        "Cascade splits: %.2f / %.2f / %.2f / %.2f",
        shadowCascades.splitDepths[0],
        shadowCascades.splitDepths[1],
        shadowCascades.splitDepths[2],
        shadowCascades.splitDepths[3]
    );
    ImGui::Text(
        "Cascade texels: %.4f / %.4f / %.4f / %.4f",
        shadowCascades.texelWorldSizes[0],
        shadowCascades.texelWorldSizes[1],
        shadowCascades.texelWorldSizes[2],
        shadowCascades.texelWorldSizes[3]
    );
    ImGui::Text(
        "Cascade atlas: %s, tile %u, extent %ux%u, grid %ux%u, capacity %u",
        shadowCascades.atlasAllocated ? "yes" : "no",
        shadowCascades.atlasTileSize,
        shadowCascades.atlasWidth,
        shadowCascades.atlasHeight,
        shadowCascades.atlasTileColumns,
        shadowCascades.atlasTileRows,
        shadowCascades.atlasCascadeCapacity
    );
    ImGui::Text(
        "Contact shadows: strength %.3f, length %.3f, thickness %.3f, steps %u, jitter %.3f, edge %.1f px",
        shadowCascades.contactShadowStrength,
        shadowCascades.contactShadowLength,
        shadowCascades.contactShadowThickness,
        shadowCascades.contactShadowSteps,
        shadowCascades.contactShadowJitterStrength,
        shadowCascades.contactShadowEdgeFadePixels
    );
    ImGui::Text(
        "SSAO: %s, strength %.3f, radius %.2f, bias %.3f, samples %u",
        stats.ssao.enabled ? "enabled" : "off",
        stats.ssao.strength,
        stats.ssao.radius,
        stats.ssao.bias,
        stats.ssao.sampleCount
    );
    ImGui::Text(
        "SSR: %s, color %s, strength %.3f, ray %.1f, thickness %.3f, steps %u",
        stats.ssr.enabled ? "enabled" : "off",
        stats.ssr.colorResolveEnabled ? "on" : "off",
        stats.ssr.strength,
        stats.ssr.rayLength,
        stats.ssr.thickness,
        stats.ssr.stepCount
    );
    ImGui::Text(
        "Reflection fallback: %s, diffuse %.2f, specular %.2f, horizon %.2f",
        stats.reflectionProbe.fallbackEnabled ? "enabled" : "off",
        stats.reflectionProbe.diffuseIntensity,
        stats.reflectionProbe.specularIntensity,
        stats.reflectionProbe.horizonBlend
    );
    ImGui::Text(
        "IBL resources: BRDF %s %u, irradiance %s %u, prefiltered %s %u mips %u, descriptors %u",
        stats.ibl.brdfLutAllocated ? "on" : "off",
        stats.ibl.brdfLutSize,
        stats.ibl.irradianceMapAllocated ? "on" : "off",
        stats.ibl.irradianceFaceSize,
        stats.ibl.prefilteredMapAllocated ? "on" : "off",
        stats.ibl.prefilteredFaceSize,
        stats.ibl.prefilteredMipCount,
        stats.ibl.descriptorSetsBound
    );
    ImGui::Text(
        "Local reflection probe: %s, scene %s, probes %u/%u, eligible %u, selected %u, top %d, dropped %u, radius %.2f, box %.1f %.1f %.1f, intensity %.2f, blend %.2f, falloff %.2f",
        stats.reflectionProbe.localEnabled ? "enabled" : "off",
        stats.reflectionProbe.localSceneOwned ? "yes" : "no",
        stats.reflectionProbe.activeProbeCount,
        stats.reflectionProbe.sceneProbeCount,
        stats.reflectionProbe.sceneEligibleProbeCount,
        stats.reflectionProbe.selectedProbeCount,
        stats.reflectionProbe.selectedProbeIndex,
        stats.reflectionProbe.droppedProbeCount,
        stats.reflectionProbe.localRadius,
        stats.reflectionProbe.localBoxExtentX,
        stats.reflectionProbe.localBoxExtentY,
        stats.reflectionProbe.localBoxExtentZ,
        stats.reflectionProbe.localIntensity,
        stats.reflectionProbe.localBlendStrength,
        stats.reflectionProbe.localFalloff
    );
    ImGui::Text(
        "Reflection probe blend: %s, blended %u, max %.3f, total %.3f, top [%d, %d, %d, %d]",
        stats.reflectionProbe.multiBlendEnabled ? "enabled" : "off",
        stats.reflectionProbe.blendedProbeCount,
        stats.reflectionProbe.maxBlendWeight,
        stats.reflectionProbe.totalBlendWeight,
        stats.reflectionProbe.selectedProbeIndices[0],
        stats.reflectionProbe.selectedProbeIndices[1],
        stats.reflectionProbe.selectedProbeIndices[2],
        stats.reflectionProbe.selectedProbeIndices[3]
    );
    ImGui::Text(
        "Reflection capture slots: slots %u, ready %u, fallback %u, sampling %u, ready mask 0x%X, sampling mask 0x%X",
        stats.reflectionProbe.selectedCaptureSlotCount,
        stats.reflectionProbe.selectedCaptureResourceReadyCount,
        stats.reflectionProbe.selectedCaptureFallbackCount,
        stats.reflectionProbe.selectedCubemapSamplingCount,
        stats.reflectionProbe.selectedCaptureReadyMask,
        stats.reflectionProbe.selectedCubemapSamplingMask
    );
    ImGui::Text(
        "Reflection capture top slots: [%d/%u/%u, %d/%u/%u, %d/%u/%u, %d/%u/%u]",
        stats.reflectionProbe.selectedCaptureSlots[0],
        stats.reflectionProbe.selectedCaptureSourceTypes[0],
        stats.reflectionProbe.selectedCaptureFallbackReasons[0],
        stats.reflectionProbe.selectedCaptureSlots[1],
        stats.reflectionProbe.selectedCaptureSourceTypes[1],
        stats.reflectionProbe.selectedCaptureFallbackReasons[1],
        stats.reflectionProbe.selectedCaptureSlots[2],
        stats.reflectionProbe.selectedCaptureSourceTypes[2],
        stats.reflectionProbe.selectedCaptureFallbackReasons[2],
        stats.reflectionProbe.selectedCaptureSlots[3],
        stats.reflectionProbe.selectedCaptureSourceTypes[3],
        stats.reflectionProbe.selectedCaptureFallbackReasons[3]
    );
    ImGui::Text(
        "Reflection authored assets: specified %u, found %u, missing %u, found mask 0x%X, hashes [%u, %u, %u, %u]",
        stats.reflectionProbe.selectedAuthoredAssetSpecifiedCount,
        stats.reflectionProbe.selectedAuthoredAssetFoundCount,
        stats.reflectionProbe.selectedAuthoredAssetMissingCount,
        stats.reflectionProbe.selectedAuthoredAssetFoundMask,
        stats.reflectionProbe.selectedAuthoredAssetHashes[0],
        stats.reflectionProbe.selectedAuthoredAssetHashes[1],
        stats.reflectionProbe.selectedAuthoredAssetHashes[2],
        stats.reflectionProbe.selectedAuthoredAssetHashes[3]
    );
    ImGui::Text(
        "Reflection probe cubemap: %s, face %u, mips %u, descriptors %u, shader %s, source %u",
        stats.reflectionProbe.localCubemapAllocated ? "allocated" : "off",
        stats.reflectionProbe.localCubemapFaceSize,
        stats.reflectionProbe.localCubemapMipCount,
        stats.reflectionProbe.localCubemapDescriptorSetsBound,
        stats.reflectionProbe.localCubemapShaderSamplingEnabled ? "sampling" : "off",
        stats.reflectionProbe.localCubemapSourceType
    );
    ImGui::Text(
        "Reflection capture: %s, ready %s, descriptor %s, fallback %s",
        ReflectionCaptureSourceName(stats.reflectionProbe.captureSourceType),
        stats.reflectionProbe.captureResourceReady ? "yes" : "no",
        stats.reflectionProbe.captureDescriptorBound ? "bound" : "fallback",
        ReflectionCaptureFallbackReasonName(
            stats.reflectionProbe.captureFallbackReason
        )
    );
    ImGui::Text(
        "Reflection probe spatial policy: box projection %s, parallax %s, influence mode %u",
        stats.reflectionProbe.boxProjectionEnabled ? "on" : "off",
        stats.reflectionProbe.parallaxCorrectionEnabled ? "on" : "off",
        stats.reflectionProbe.influenceMode
    );
    ImGui::Text(
        "Height fog: %s, density %.4f, falloff %.3f, start %.1f, max %.3f",
        stats.heightFog.enabled ? "enabled" : "off",
        stats.heightFog.density,
        stats.heightFog.heightFalloff,
        stats.heightFog.startDistance,
        stats.heightFog.maxOpacity
    );
    ImGui::Text(
        "Bloom: %s, pyramid %s, mips %u, intensity %.3f, threshold %.3f, radius %.2f px, fallbacks %u",
        stats.postProcess.bloomEnabled ? "enabled" : "off",
        stats.postProcess.bloomPyramidEnabled ? "enabled" : "off",
        stats.postProcess.bloomPyramidMipCount,
        stats.postProcess.bloomIntensity,
        stats.postProcess.bloomThreshold,
        stats.postProcess.bloomRadiusPixels,
        stats.postProcess.bloomPyramidFallbacks
    );
    ImGui::Text(
        "Tone mapping: %s, mode %u, exposure %.3f, white %.2f",
        stats.postProcess.toneMappingEnabled ? "enabled" : "linear fallback",
        stats.postProcess.toneMapMode,
        stats.postProcess.exposure,
        stats.postProcess.toneMapWhitePoint
    );
    ImGui::Text(
        "Auto exposure: %s, target %.3f, range %.3f-%.3f, adapt %.3f",
        stats.postProcess.autoExposureEnabled ? "enabled" : "off",
        stats.postProcess.autoExposureTargetLuminance,
        stats.postProcess.autoExposureMin,
        stats.postProcess.autoExposureMax,
        stats.postProcess.autoExposureAdaptation
    );
    ImGui::Text(
        "Auto exposure GPU: %s, history %s, exposure %.3f, target %.3f, avg lum %.3f, fallbacks %u",
        stats.postProcess.autoExposureHistogramEnabled ? "histogram" : "fallback",
        stats.postProcess.autoExposureHistoryValid ? "valid" : "cold",
        stats.postProcess.autoExposureGpuExposure,
        stats.postProcess.autoExposureGpuTargetExposure,
        stats.postProcess.autoExposureGpuAverageLuminance,
        stats.postProcess.autoExposureFallbacks
    );
    ImGui::Text(
        "Color grading: %s, saturation %.3f, contrast %.3f, gamma %.3f, LUT %s size %u strength %.3f fallbacks %u",
        stats.postProcess.colorGradingEnabled ? "enabled" : "off",
        stats.postProcess.colorGradingSaturation,
        stats.postProcess.colorGradingContrast,
        stats.postProcess.colorGradingGamma,
        stats.postProcess.colorGradingLutEnabled ? "enabled" : "off",
        stats.postProcess.colorGradingLutSize,
        stats.postProcess.colorGradingLutStrength,
        stats.postProcess.colorGradingLutFallbacks
    );
    ImGui::Text(
        "Sharpening: %s, strength %.3f, radius %.2f px",
        stats.postProcess.sharpeningEnabled ? "enabled" : "off",
        stats.postProcess.sharpeningStrength,
        stats.postProcess.sharpeningRadiusPixels
    );
    ImGui::Text(
        "Local shadow atlas: %s, tile %u, extent %ux%u, grid %ux%u, capacity %u",
        localShadowAtlas.allocated ? "yes" : "no",
        localShadowAtlas.tileSize,
        localShadowAtlas.atlasWidth,
        localShadowAtlas.atlasHeight,
        localShadowAtlas.tileColumns,
        localShadowAtlas.tileRows,
        localShadowAtlas.tileCapacity
    );
    ImGui::Text(
        "Local shadow budget: %u point / %u spot, point faces %u, spot tiles %u, requested %u, assigned %u, dropped %u",
        localShadowAtlas.pointLightCount,
        localShadowAtlas.spotLightCount,
        localShadowAtlas.pointFaceTiles,
        localShadowAtlas.spotTiles,
        localShadowAtlas.requestedTiles,
        localShadowAtlas.assignedTiles,
        localShadowAtlas.droppedTiles
    );
    ImGui::Text(
        "Local shadow atlas pass: %u tile passes / %u draws / %u mesh binds",
        localShadowAtlas.recordedTilePasses,
        localShadowAtlas.recordedDraws,
        localShadowAtlas.recordedMeshBinds
    );
    ImGui::Text(
        "Local shadow cache: %u eligible / %u hits / %u misses / %u skipped",
        localShadowAtlas.cacheEligibleTiles,
        localShadowAtlas.cacheHitTiles,
        localShadowAtlas.cacheMissTiles,
        localShadowAtlas.cacheSkippedTiles
    );
    ImGui::Text(
        "Local shadow filtering: bias %.5f / slope %.5f, PCF radius %.2f, kernel %ux%u, PCSS %.3f, face blend %.3f",
        localShadowAtlas.biasMin,
        localShadowAtlas.biasSlope,
        localShadowAtlas.pcfRadius,
        localShadowAtlas.pcfKernelRadius * 2u + 1u,
        localShadowAtlas.pcfKernelRadius * 2u + 1u,
        localShadowAtlas.pcssStrength,
        localShadowAtlas.faceBlendStrength
    );
    ImGui::Text(
        "Bounds cache: %u/%u main hits/misses, %u/%u overlay",
        draw.mainBoundsCacheHits,
        draw.mainBoundsCacheMisses,
        draw.overlayBoundsCacheHits,
        draw.overlayBoundsCacheMisses
    );
    ImGui::Text(
        "Material binds: %u main / %u gbuffer / %u overlay",
        binds.mainMaterialBinds,
        binds.gBufferMaterialBinds,
        binds.overlayMaterialBinds
    );
    ImGui::Text(
        "Deferred lighting: %u draws / %u frame binds / %u gbuffer binds",
        binds.deferredLightingDraws,
        binds.deferredLightingFrameBinds,
        binds.deferredLightingGBufferBinds
    );
    ImGui::Text(
        "Deferred PBR debug: %u draws / %u frame binds / %u gbuffer binds",
        binds.deferredPbrDebugDraws,
        binds.deferredPbrDebugFrameBinds,
        binds.deferredPbrDebugGBufferBinds
    );
    ImGui::Text(
        "HDR composite: %u draws / %u frame binds / %u texture binds",
        binds.hdrCompositeDraws,
        binds.hdrCompositeFrameBinds,
        binds.hdrCompositeTextureBinds
    );
    ImGui::Text(
        "GBuffer debug: %u draws / %u frame binds / %u texture binds",
        binds.gBufferDebugDraws,
        binds.gBufferDebugFrameBinds,
        binds.gBufferDebugTextureBinds
    );
    ImGui::Text(
        "Deferred shadow debug: %u draws / %u frame binds / %u texture binds",
        binds.deferredShadowDebugDraws,
        binds.deferredShadowDebugFrameBinds,
        binds.deferredShadowDebugTextureBinds
    );
    ImGui::Text(
        "Shadow cascade debug: %u draws / %u frame binds / %u texture binds",
        binds.shadowCascadeDebugDraws,
        binds.shadowCascadeDebugFrameBinds,
        binds.shadowCascadeDebugTextureBinds
    );
    ImGui::Text(
        "Local shadow debug: %u draws / %u frame binds / %u texture binds",
        binds.localShadowAtlasDebugDraws,
        binds.localShadowAtlasDebugFrameBinds,
        binds.localShadowAtlasDebugTextureBinds
    );
    ImGui::Text(
        "Local shadow visibility debug: %u draws / %u frame binds / %u texture binds",
        binds.localShadowVisibilityDebugDraws,
        binds.localShadowVisibilityDebugFrameBinds,
        binds.localShadowVisibilityDebugTextureBinds
    );
    ImGui::Text(
        "Local shadow face debug: %u draws / %u frame binds / %u texture binds",
        binds.localShadowFaceDebugDraws,
        binds.localShadowFaceDebugFrameBinds,
        binds.localShadowFaceDebugTextureBinds
    );
    ImGui::Text(
        "Contact shadow debug: %u draws / %u frame binds / %u gbuffer binds",
        binds.contactShadowDebugDraws,
        binds.contactShadowDebugFrameBinds,
        binds.contactShadowDebugGBufferBinds
    );
    ImGui::Text(
        "SSAO debug: %u draws / %u frame binds / %u gbuffer binds",
        binds.ssaoDebugDraws,
        binds.ssaoDebugFrameBinds,
        binds.ssaoDebugGBufferBinds
    );
    ImGui::Text(
        "SSR debug: %u draws / %u frame binds / %u gbuffer binds",
        binds.ssrDebugDraws,
        binds.ssrDebugFrameBinds,
        binds.ssrDebugGBufferBinds
    );
    ImGui::Text(
        "Reflection probe debug: %u draws / %u frame binds / %u gbuffer binds",
        binds.reflectionProbeDebugDraws,
        binds.reflectionProbeDebugFrameBinds,
        binds.reflectionProbeDebugGBufferBinds
    );
    ImGui::Text(
        "Height fog debug: %u draws / %u frame binds / %u gbuffer binds",
        binds.heightFogDebugDraws,
        binds.heightFogDebugFrameBinds,
        binds.heightFogDebugGBufferBinds
    );
    ImGui::Text(
        "Bloom debug: %u draws / %u frame binds / %u texture binds",
        binds.bloomDebugDraws,
        binds.bloomDebugFrameBinds,
        binds.bloomDebugTextureBinds
    );
    ImGui::Text(
        "Bloom pyramid: down %u / up %u draws, frame binds %u/%u, texture binds %u/%u",
        binds.bloomDownsampleDraws,
        binds.bloomUpsampleDraws,
        binds.bloomDownsampleFrameBinds,
        binds.bloomUpsampleFrameBinds,
        binds.bloomDownsampleTextureBinds,
        binds.bloomUpsampleTextureBinds
    );
    ImGui::Text(
        "Tone mapping debug: %u draws / %u frame binds / %u texture binds",
        binds.toneMappingDebugDraws,
        binds.toneMappingDebugFrameBinds,
        binds.toneMappingDebugTextureBinds
    );
    ImGui::Text(
        "Auto exposure debug: %u draws / %u frame binds / %u texture binds",
        binds.autoExposureDebugDraws,
        binds.autoExposureDebugFrameBinds,
        binds.autoExposureDebugTextureBinds
    );
    ImGui::Text(
        "Color grading debug: %u draws / %u frame binds / %u texture binds",
        binds.colorGradingDebugDraws,
        binds.colorGradingDebugFrameBinds,
        binds.colorGradingDebugTextureBinds
    );
    ImGui::Text(
        "Sharpening debug: %u draws / %u frame binds / %u texture binds",
        binds.sharpeningDebugDraws,
        binds.sharpeningDebugFrameBinds,
        binds.sharpeningDebugTextureBinds
    );
    ImGui::Text(
        "Light tile compute: %u dispatches / %u frame binds / groups %ux%u",
        binds.lightTileCullComputeDispatches,
        binds.lightTileCullComputeFrameBinds,
        binds.lightTileCullComputeGroupsX,
        binds.lightTileCullComputeGroupsY
    );
    ImGui::Text(
        "Auto exposure compute: %u dispatches / %u frame binds / %u texture binds / groups %ux%u",
        binds.autoExposureHistogramDispatches,
        binds.autoExposureHistogramFrameBinds,
        binds.autoExposureHistogramTextureBinds,
        binds.autoExposureHistogramGroupsX,
        binds.autoExposureHistogramGroupsY
    );
    ImGui::Text(
        "Mesh binds: %u main / %u gbuffer / %u overlay / %u shadow",
        binds.mainMeshBinds,
        binds.gBufferMeshBinds,
        binds.overlayMeshBinds,
        binds.shadowMeshBinds
    );
    ImGui::Text(
        "Shadow cascade atlas: %u tile passes / %u draws / %u mesh binds",
        binds.shadowCascadeAtlasPasses,
        binds.shadowCascadeAtlasDraws,
        binds.shadowCascadeAtlasMeshBinds
    );
    ImGui::Text(
        "Local shadow atlas binds: %u tile passes / %u draws / %u mesh binds",
        binds.localShadowAtlasPasses,
        binds.localShadowAtlasDraws,
        binds.localShadowAtlasMeshBinds
    );
    ImGui::Text(
        "Local shadow resolve: %s",
        binds.localShadowResolveEnabled ? "enabled" : "off"
    );
    ImGui::Text(
        "Shadow buffers: %u cascade / %u local updates",
        binds.shadowCascadeBufferUpdates,
        binds.localShadowBufferUpdates
    );
    ImGui::Text(
        "Frame light data: %u constants / %u buffer updates",
        binds.frameLightConstantUpdates,
        binds.frameLightBufferUpdates
    );
    ImGui::Text(
        "Frame lights: %u total / %u directional / %u local / %u rect",
        binds.frameLightTotalCount,
        binds.frameDirectionalLightCount,
        binds.frameLocalLightCount,
        binds.frameRectLightCount
    );
    ImGui::Text(
        "Light tiles: %ux%u @ %upx (%u tiles)",
        binds.frameLightTileCountX,
        binds.frameLightTileCountY,
        binds.frameLightTileSize,
        binds.frameLightTileCount
    );
    ImGui::Text(
        "Light tile assignments: %u / %u, fallbacks %u",
        binds.frameLightTileAssignments,
        binds.frameLightTileAssignmentCapacity,
        binds.frameLightTileAssignmentFallbacks
    );
    ImGui::Text(
        "Light tile overflow: %u / %u indices, %u tiles, dropped %u",
        binds.frameLightTileOverflowAssignments,
        binds.frameLightTileOverflowCapacity,
        binds.frameLightTileOverflowTiles,
        binds.frameLightTileOverflowDropped
    );
    ImGui::Text(
        "Light tile GPU readback: %s, saturated %u, max raw %u, raw sum %llu",
        binds.frameLightTileGpuReadbackValid ? "yes" : "no",
        binds.frameLightTileGpuSaturatedTiles,
        binds.frameLightTileGpuMaxCandidates,
        static_cast<unsigned long long>(binds.frameLightTileGpuRawCandidates)
    );
    ImGui::Text(
        "Light tile GPU overflow: tiles %u, dropped tiles %u, stored %u, dropped %u",
        binds.frameLightTileGpuOverflowTiles,
        binds.frameLightTileGpuOverflowDroppedTiles,
        binds.frameLightTileGpuOverflowStored,
        binds.frameLightTileGpuOverflowDropped
    );
    ImGui::Text(
        "Frame materials: %u / %u, overflow %u, updates %u",
        binds.frameMaterialCount,
        binds.frameMaterialCapacity,
        binds.frameMaterialOverflowCount,
        binds.frameMaterialBufferUpdates
    );
    ImGui::Text(
        "Material classes: %u opaque / %u transparent / %u special / %u emissive / %u specular / %u spec tex / %u mask / %u blend / %u uv / %u two-sided / %u clearcoat / %u clearcoat tex / %u coat rough tex / %u transmission / %u trans tex / %u volume / %u opacity / %u textured",
        binds.frameMaterialOpaqueCount,
        binds.frameMaterialTransparentCount,
        binds.frameMaterialForwardSpecialCount,
        binds.frameMaterialEmissiveHintCount,
        binds.frameMaterialSpecularHintCount,
        binds.frameMaterialSpecularTextureCount,
        binds.frameMaterialAlphaMaskCount,
        binds.frameMaterialAlphaBlendCount,
        binds.frameMaterialUvTransformCount,
        binds.frameMaterialDoubleSidedCount,
        binds.frameMaterialClearcoatCount,
        binds.frameMaterialClearcoatTextureCount,
        binds.frameMaterialClearcoatRoughnessTextureCount,
        binds.frameMaterialTransmissionCount,
        binds.frameMaterialTransmissionTextureCount,
        binds.frameMaterialVolumeCount,
        binds.frameMaterialOpacityTextureCount,
        binds.frameMaterialTexturedCount
    );
    ImGui::Text(
        "Push constants: %u updates / %llu bytes",
        binds.pushConstantUpdates,
        static_cast<unsigned long long>(binds.pushConstantBytes)
    );
    ImGui::Text(
        "Frame graph: %u active / %u roadmap passes",
        stats.frameGraph.activePassCount,
        stats.frameGraph.roadmapPassCount
    );
    ImGui::Text(
        "Graph resources: %u physical / %u planned",
        stats.frameGraph.physicalResourceCount,
        stats.frameGraph.plannedResourceCount
    );
    ImGui::Text(
        "Graph validation: %u issues",
        stats.frameGraph.validation.issueCount
    );
    ImGui::Text(
        "Graph validation classes: missing refs %u / read-before-write %u / unused physical %u / roadmap write-only %u / active writes planned %u",
        stats.frameGraph.validation.missingResourceRefCount,
        stats.frameGraph.validation.readBeforeFirstWriteCount,
        stats.frameGraph.validation.unusedPhysicalResourceCount,
        stats.frameGraph.validation.writeOnlyRoadmapResourceCount,
        stats.frameGraph.validation.activePassWritesPlannedResourceCount
    );
    ImGui::Text(
        "Graph refs: %u reads / %u writes",
        stats.frameGraph.references.readCount,
        stats.frameGraph.references.writeCount
    );
    ImGui::Text(
        "Graph unstructured refs: %u reads / %u writes",
        stats.frameGraph.references.unstructuredReadTokenCount,
        stats.frameGraph.references.unstructuredWriteTokenCount
    );
    ImGui::Text(
        "Graph access: sampled %u / attachment reads %u / color writes %u / depth writes %u / storage writes %u / presents %u",
        stats.frameGraph.references.readSampledCount,
        stats.frameGraph.references.readAttachmentCount,
        stats.frameGraph.references.writeColorAttachmentCount,
        stats.frameGraph.references.writeDepthAttachmentCount,
        stats.frameGraph.references.writeStorageCount,
        stats.frameGraph.references.presentCount
    );
    ImGui::Text(
        "Graph dependencies: %u total / %u read-after-write / %u write-after-write",
        stats.frameGraph.dependencies.dependencyCount,
        stats.frameGraph.dependencies.readAfterWriteCount,
        stats.frameGraph.dependencies.writeAfterWriteCount
    );
    ImGui::Text(
        "Graph lifetimes: %u used / %u unused / %u read-only / %u write-only / %u read-write",
        stats.frameGraph.lifetimes.usedResourceCount,
        stats.frameGraph.lifetimes.unusedResourceCount,
        stats.frameGraph.lifetimes.readOnlyResourceCount,
        stats.frameGraph.lifetimes.writeOnlyResourceCount,
        stats.frameGraph.lifetimes.readWriteResourceCount
    );
    ImGui::Text(
        "Graph barriers: %u transitions / %u buffer / %u layout / %u queue transfers",
        stats.frameGraph.barriers.transitionCount,
        stats.frameGraph.barriers.bufferTransitionCount,
        stats.frameGraph.barriers.layoutTransitionCount,
        stats.frameGraph.barriers.queueOwnershipTransferCount
    );
    ImGui::Text(
        "Graph barrier bridge: %u planned / %u executed / %u fallback / %u mismatch",
        stats.frameGraph.barrierExecution.plannedBridgeBarrierCount,
        stats.frameGraph.barrierExecution.executedBarrierCount,
        stats.frameGraph.barrierExecution.fallbackBarrierCount,
        stats.frameGraph.barrierExecution.mismatchCount
    );

    if (ImGui::TreeNode("CPU breakdown")) {
        ImGui::Text("Wait + acquire: %.3f ms", cpu.waitAcquireMs);
        ImGui::Text("ImGui: %.3f ms", cpu.imguiMs);
        ImGui::Text("Picking: %.3f ms", cpu.pickingMs);
        ImGui::Text("Build queues: %.3f ms", cpu.queueBuildMs);
        ImGui::Text("Uniform update: %.3f ms", cpu.uniformUpdateMs);
        ImGui::Text("Record commands: %.3f ms", cpu.commandRecordMs);
        ImGui::Text("Submit + present: %.3f ms", cpu.submitPresentMs);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("GPU breakdown")) {
        if (gpu.available) {
            ImGui::Text("Shadow: %.3f ms", gpu.shadowMs);
            ImGui::Text("Main: %.3f ms", gpu.mainMs);
            ImGui::Text("Overlay: %.3f ms", gpu.overlayMs);
            ImGui::Text("ImGui: %.3f ms", gpu.imguiMs);
        } else {
            ImGui::TextUnformatted("Timestamp query results are not ready.");
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Frame graph")) {
        ImGui::Text(
            "%.*s",
            static_cast<int>(stats.frameGraph.target.size()),
            stats.frameGraph.target.data()
        );
        for (const RenderFramePass& pass : stats.frameGraph.passes) {
            ImGui::BulletText(
                "#%08X [%.*s/%.*s] %.*s",
                pass.id,
                static_cast<int>(RenderFramePassStatusName(pass.status).size()),
                RenderFramePassStatusName(pass.status).data(),
                static_cast<int>(RenderFramePassQueueName(pass.queue).size()),
                RenderFramePassQueueName(pass.queue).data(),
                static_cast<int>(pass.name.size()),
                pass.name.data()
            );
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextWrapped(
                    "%.*s",
                    static_cast<int>(pass.purpose.size()),
                    pass.purpose.data()
                );
                ImGui::Separator();
                ImGui::Text(
                    "Reads: %.*s",
                    static_cast<int>(pass.reads.size()),
                    pass.reads.data()
                );
                if (!pass.readResources.empty()) {
                    ImGui::Text("Read refs:");
                    for (const RenderFrameGraphResourceRef& ref : pass.readResources) {
                        const std::string_view access =
                            RenderFrameGraphResourceAccessName(ref.access);
                        ImGui::BulletText(
                            "#%08X [%.*s] %.*s",
                            ref.resourceId,
                            static_cast<int>(access.size()),
                            access.data(),
                            static_cast<int>(ref.name.size()),
                            ref.name.data()
                        );
                    }
                }
                ImGui::Text(
                    "Writes: %.*s",
                    static_cast<int>(pass.writes.size()),
                    pass.writes.data()
                );
                if (!pass.writeResources.empty()) {
                    ImGui::Text("Write refs:");
                    for (const RenderFrameGraphResourceRef& ref : pass.writeResources) {
                        const std::string_view access =
                            RenderFrameGraphResourceAccessName(ref.access);
                        ImGui::BulletText(
                            "#%08X [%.*s] %.*s",
                            ref.resourceId,
                            static_cast<int>(access.size()),
                            access.data(),
                            static_cast<int>(ref.name.size()),
                            ref.name.data()
                        );
                    }
                }
                if (!pass.dependencies.empty()) {
                    ImGui::Separator();
                    ImGui::Text("Depends on:");
                    for (const RenderFrameGraphPassDependency& dependency :
                        pass.dependencies) {
                        ImGui::BulletText(
                            "#%08X %.*s via %.*s (%s)",
                            dependency.passId,
                            static_cast<int>(dependency.passName.size()),
                            dependency.passName.data(),
                            static_cast<int>(dependency.resourceName.size()),
                            dependency.resourceName.data(),
                            dependency.writeDependency ? "write" : "read"
                        );
                    }
                }
                ImGui::EndTooltip();
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Graph validation")) {
        if (stats.frameGraph.validation.issues.empty()) {
            ImGui::TextUnformatted("No frame graph validation issues.");
        } else {
            for (const RenderFrameGraphValidationIssue& issue :
                stats.frameGraph.validation.issues) {
                const std::string_view kind =
                    RenderFrameGraphValidationIssueKindName(issue.kind);
                ImGui::BulletText(
                    "%.*s: pass #%08X %.*s / resource #%08X %.*s%s",
                    static_cast<int>(kind.size()),
                    kind.data(),
                    issue.passId,
                    static_cast<int>(issue.passName.size()),
                    issue.passName.data(),
                    issue.resourceId,
                    static_cast<int>(issue.resourceName.size()),
                    issue.resourceName.data(),
                    issue.writeRef ? " [write]" : ""
                );
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Graph barrier plan")) {
        ImGui::Text(
            "Bridge: %u planned / %u executed / %u fallback / %u mismatch",
            stats.frameGraph.barrierExecution.plannedBridgeBarrierCount,
            stats.frameGraph.barrierExecution.executedBarrierCount,
            stats.frameGraph.barrierExecution.fallbackBarrierCount,
            stats.frameGraph.barrierExecution.mismatchCount
        );
        if (stats.frameGraph.barrierTransitions.empty()) {
            ImGui::TextUnformatted("No inferred barrier transitions.");
        } else {
            for (const RenderFrameGraphBarrierTransition& transition :
                stats.frameGraph.barrierTransitions) {
                const std::string_view resourceKind =
                    RenderFrameGraphBarrierResourceKindName(
                        transition.resourceKind
                    );
                const std::string_view srcAccess =
                    RenderFrameGraphResourceAccessName(transition.srcAccess);
                const std::string_view dstAccess =
                    RenderFrameGraphResourceAccessName(transition.dstAccess);
                ImGui::BulletText(
                    "%.*s -> %.*s via %.*s",
                    static_cast<int>(transition.producerPassName.size()),
                    transition.producerPassName.data(),
                    static_cast<int>(transition.consumerPassName.size()),
                    transition.consumerPassName.data(),
                    static_cast<int>(transition.resourceName.size()),
                    transition.resourceName.data()
                );
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text(
                        "Resource: #%08X [%.*s] %.*s",
                        transition.resourceId,
                        static_cast<int>(resourceKind.size()),
                        resourceKind.data(),
                        static_cast<int>(transition.resourceName.size()),
                        transition.resourceName.data()
                    );
                    ImGui::Text(
                        "Access: %.*s -> %.*s",
                        static_cast<int>(srcAccess.size()),
                        srcAccess.data(),
                        static_cast<int>(dstAccess.size()),
                        dstAccess.data()
                    );
                    ImGui::Text(
                        "Stage: %.*s -> %.*s",
                        static_cast<int>(transition.srcStage.size()),
                        transition.srcStage.data(),
                        static_cast<int>(transition.dstStage.size()),
                        transition.dstStage.data()
                    );
                    ImGui::Text(
                        "Layout: %.*s -> %.*s",
                        static_cast<int>(transition.oldLayout.size()),
                        transition.oldLayout.data(),
                        static_cast<int>(transition.newLayout.size()),
                        transition.newLayout.data()
                    );
                    ImGui::Text(
                        "Dependency: %s / queue transfer: %s",
                        transition.writeDependency ? "write-after-write" : "read-after-write",
                        transition.queueOwnershipTransfer ? "yes" : "no"
                    );
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Graph resources")) {
        for (const RenderGraphResource& resource : stats.frameGraph.resources) {
            const std::string_view status = RenderGraphResourceStatusName(resource.status);
            const std::string_view lifetime =
                RenderGraphResourceLifetimeName(resource.lifetime);
            ImGui::BulletText(
                "#%08X [%.*s/%.*s] %.*s",
                resource.id,
                static_cast<int>(status.size()),
                status.data(),
                static_cast<int>(lifetime.size()),
                lifetime.data(),
                static_cast<int>(resource.name.size()),
                resource.name.data()
            );
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text(
                    "Format: %.*s",
                    static_cast<int>(resource.format.size()),
                    resource.format.data()
                );
                ImGui::TextWrapped(
                    "Usage: %.*s",
                    static_cast<int>(resource.usage.size()),
                    resource.usage.data()
                );
                ImGui::Text(
                    "Scale: %.*s",
                    static_cast<int>(resource.scale.size()),
                    resource.scale.data()
                );
                ImGui::Separator();
                ImGui::Text(
                    "Uses: %u reads / %u writes",
                    resource.readCount,
                    resource.writeCount
                );
                if (resource.firstUsePassId != 0u) {
                    ImGui::Text(
                        "First: #%08X %.*s",
                        resource.firstUsePassId,
                        static_cast<int>(resource.firstUsePassName.size()),
                        resource.firstUsePassName.data()
                    );
                    ImGui::Text(
                        "Last: #%08X %.*s",
                        resource.lastUsePassId,
                        static_cast<int>(resource.lastUsePassName.size()),
                        resource.lastUsePassName.data()
                    );
                }
                ImGui::EndTooltip();
            }
        }
        ImGui::TreePop();
    }
}

Renderable3D* DrawScene3DPicker(Scene3D& scene) {
    ImGui::SeparatorText("Scene3D");
    std::size_t pickableCount = 0;
    for (Renderable3D* candidate : scene.Renderables()) {
        if (candidate != nullptr && candidate->Pickable()) {
            ++pickableCount;
        }
    }

    ImGui::Text("Objects: %zu", pickableCount);

    const std::size_t selectedIndex = scene.SelectedIndex();
    Renderable3D* selectedRenderable = scene.SelectedRenderable();
    const char* selectedName =
        selectedRenderable != nullptr && selectedRenderable->Pickable()
        ? selectedRenderable->Name().c_str()
        : "None";

    if (ImGui::BeginCombo("Selected 3D object", selectedName)) {
        std::size_t index = 0;
        for (Renderable3D* candidate : scene.Renderables()) {
            if (candidate == nullptr || !candidate->Pickable()) {
                ++index;
                continue;
            }

            const bool selected = index == selectedIndex;
            const char* candidateName = candidate->Name().c_str();

            if (ImGui::Selectable(candidateName, selected)) {
                scene.SetSelectedIndex(index);
            }

            if (selected) {
                ImGui::SetItemDefaultFocus();
            }

            ++index;
        }

        ImGui::EndCombo();
    }

    Renderable3D* currentRenderable = scene.SelectedRenderable();
    return currentRenderable != nullptr && currentRenderable->Pickable()
        ? currentRenderable
        : nullptr;
}

void DrawRenderable3DControls(
    Renderable3D& renderable,
    const VulkanRenderResources2D& renderResources
) {
    Transform3D& transform = renderable.Transform();
    VulkanMaterial& material = renderResources.Material(renderable.MaterialId());
    MaterialProperties& properties = material.Properties();

    ImGui::SeparatorText("Object3D");
    ImGui::Text("%s", renderable.Name().c_str());

    ImGui::SeparatorText("Transform3D");
    glm::vec3 position = transform.Position();
    if (ImGui::DragFloat3("Position##3D", &position.x, 0.02f, -24.0f, 24.0f)) {
        transform.SetPosition(position);
    }
    glm::vec3 rotationDegrees = transform.RotationDegrees();
    if (ImGui::SliderFloat3("Rotation##3D", &rotationDegrees.x, -180.0f, 180.0f)) {
        transform.SetRotationDegrees(rotationDegrees);
    }
    glm::vec3 scale = transform.Scale();
    if (ImGui::DragFloat3("Scale##3D", &scale.x, 0.01f, 0.01f, 24.0f)) {
        transform.SetScale(scale);
    }
    glm::vec3 spinSpeed = transform.RotationSpeedDegreesPerSecond();
    if (ImGui::DragFloat3("Spin speed##3D", &spinSpeed.x, 0.1f, -180.0f, 180.0f)) {
        transform.SetRotationSpeedDegreesPerSecond(spinSpeed);
    }

    i32 drawOrder = renderable.DrawOrder();
    if (ImGui::InputInt("Draw order##3D", &drawOrder)) {
        renderable.SetDrawOrder(drawOrder);
    }

    bool animateRotation = transform.AnimateRotation();
    if (ImGui::Checkbox("Animate rotation##3D", &animateRotation)) {
        transform.SetAnimateRotation(animateRotation);
    }
    bool castShadow = renderable.CastShadow();
    if (ImGui::Checkbox("Cast shadow##3D", &castShadow)) {
        renderable.SetCastShadow(castShadow);
    }

    if (ImGui::Button("Reset transform##3D")) {
        transform.Reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Place on disk##3D")) {
        transform.SetPosition({ 8.84f, 0.03f, 1.0f });
        transform.SetRotationDegrees({ 0.0f, -97.297f, -2.4f });
        transform.SetScale({ 0.1f, 0.1f, 0.1f });
        transform.SetAnimateRotation(false);
    }

    ImGui::SeparatorText("Material3D");
    ImGui::ColorEdit4("Base color##3D", properties.baseColorFactor.data());
    ImGui::SliderFloat("Texture mix##3D", &properties.textureMix, 0.0f, 1.0f);

    ImGui::SeparatorText("Lighting3D");
    ImGui::DragFloat3("Light direction##3D", properties.custom.data(), 0.01f, -1.0f, 1.0f);
    ImGui::SliderFloat("Ambient##3D", &properties.custom[3], 0.0f, 1.0f);
    ImGui::SliderFloat("Diffuse##3D", &properties.viewControls[0], 0.0f, 4.0f);
    ImGui::SliderFloat("Specular##3D", &properties.viewControls[1], 0.0f, 2.0f);
    ImGui::SliderFloat("Shininess##3D", &properties.viewControls[2], 1.0f, 128.0f);

    if (properties.cameraDirection[3] > 0.001f || properties.cameraPosition[3] > 0.001f) {
        ImGui::SeparatorText("Black Hole Mask");
        ImGui::SliderFloat("Apparent shadow radius##3D", &properties.cameraDirection[3], 0.0f, 6.0f, "%.3f");
        ImGui::SliderFloat("Depth bias##3D", &properties.cameraPosition[3], 0.0f, 0.2f, "%.4f");
    }

    if (ImGui::Button("Reset material##3D")) {
        ResetForward3DParameters(properties);
    }
}

}

VulkanImGuiLayer::VulkanImGuiLayer(
    Window& window,
    VkInstance instance,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanDevice& device,
    const VulkanRenderPass& renderPass,
    const VulkanSwapchain& swapchain
) : m_Device(device.Handle()) {
    CreateContext(window);
    InitializeVulkanBackend(instance, physicalDevice, device, renderPass, swapchain);
    m_Initialized = true;
}

VulkanImGuiLayer::~VulkanImGuiLayer() {
    if (!m_Initialized) {
        return;
    }

    vkDeviceWaitIdle(m_Device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void VulkanImGuiLayer::BeginFrame(
    Scene2D* scene,
    Camera2D* camera,
    Scene3D* scene3D,
    Camera3D* camera3D,
    const VulkanRenderResources2D* renderResources,
    const RendererStats* rendererStats,
    VulkanRenderDebugSettings* renderDebugSettings,
    VulkanShadowSettings* shadowSettings
) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 620.0f), ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once);

    ImGui::Begin("SelfEngine");
    ImGui::Text("Vulkan renderer");
    ImGui::Separator();
    ImGui::Text("Frame time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    if (rendererStats != nullptr) {
        DrawPerformanceStats(*rendererStats);
    }
    ImGui::Spacing();
    ImGui::BulletText("Texture2D + alpha blending");
    ImGui::BulletText("Depth buffer");
    ImGui::BulletText("Swapchain recreation");

    if (camera != nullptr) {
        ImGui::SeparatorText("Camera2D");
        ImGui::DragFloat2("Camera position", &camera->Position().x, 0.01f, -10.0f, 10.0f);

        f32 zoom = camera->Zoom();
        if (ImGui::SliderFloat("Camera zoom", &zoom, 0.1f, 5.0f)) {
            camera->SetZoom(zoom);
        }

        if (ImGui::Button("Reset camera")) {
            camera->Reset();
        }
    }

    if (camera3D != nullptr) {
        DrawCamera3DControls(*camera3D);
    }

    if (renderDebugSettings != nullptr && scene3D != nullptr) {
        DrawRenderDebugControls(*renderDebugSettings);
    }

    if (shadowSettings != nullptr && scene3D != nullptr) {
        DrawShadowControls(*shadowSettings);
    }

    Renderable2D* renderable = nullptr;
    if (scene != nullptr && !scene->Empty()) {
        ImGui::SeparatorText("Scene2D");
        ImGui::Text("Objects: %zu", scene->Count());

        std::size_t selectedIndex = scene->SelectedIndex();
        Renderable2D* selectedRenderable = scene->SelectedRenderable();
        const char* selectedName = selectedRenderable != nullptr
            ? selectedRenderable->Name().c_str()
            : "None";

        if (ImGui::BeginCombo("Selected object", selectedName)) {
            std::size_t index = 0;
            for (Renderable2D* candidate : scene->Renderables()) {
                const bool selected = index == selectedIndex;
                const char* candidateName = candidate != nullptr
                    ? candidate->Name().c_str()
                    : "Null object";

                if (ImGui::Selectable(candidateName, selected)) {
                    scene->SetSelectedIndex(index);
                }

                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }

                ++index;
            }

            ImGui::EndCombo();
        }

        renderable = scene->SelectedRenderable();
    }

    Renderable3D* renderable3D = nullptr;
    if (scene3D != nullptr && !scene3D->Empty()) {
        renderable3D = DrawScene3DPicker(*scene3D);
    }

    if (renderable != nullptr) {
        Transform2D& transform = renderable->Transform();
        SE_ASSERT(
            renderResources != nullptr,
            "VulkanImGuiLayer needs render resources to edit a selected renderable"
        );
        VulkanMaterial& material = renderResources->Material(renderable->MaterialId());
        MaterialProperties& properties = material.Properties();
        const bool isBlackHole = renderable->Name() == "Black Hole";

        ImGui::SeparatorText("Object");
        ImGui::Text("%s", renderable->Name().c_str());
        if (isBlackHole) {
            DrawBlackHoleControls(properties);
        }

        if (!isBlackHole) {
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat2("Position", &transform.position.x, 0.01f, -2.0f, 2.0f);
            ImGui::SliderFloat("Rotation", &transform.rotationDegrees, -180.0f, 180.0f);
            ImGui::DragFloat2("Scale", &transform.scale.x, 0.01f, 0.05f, 3.0f);
            i32 drawOrder = renderable->DrawOrder();
            if (ImGui::InputInt("Draw order", &drawOrder)) {
                renderable->SetDrawOrder(drawOrder);
            }
            ImGui::Checkbox("Animate rotation", &transform.animateRotation);

            if (ImGui::Button("Reset transform")) {
                transform.Reset();
            }
        }

        ImGui::SeparatorText("Material");
        ImGui::ColorEdit4("Base color", properties.baseColorFactor.data());
        if (!isBlackHole) {
            ImGui::SliderFloat("Texture mix", &properties.textureMix, 0.0f, 1.0f);
        }

        if (ImGui::Button("Reset material")) {
            if (isBlackHole) {
                ResetBlackHoleParameters(properties);
            } else {
                properties = MaterialProperties{};
            }
        }
    }

    if (renderable3D != nullptr) {
        SE_ASSERT(
            renderResources != nullptr,
            "VulkanImGuiLayer needs render resources to edit a selected 3D renderable"
        );
        DrawRenderable3DControls(*renderable3D, *renderResources);
    }

    ImGui::End();
}

void VulkanImGuiLayer::Render(VkCommandBuffer commandBuffer) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void VulkanImGuiLayer::OnSwapchainRecreated(const VulkanSwapchain& swapchain) {
    ImGui_ImplVulkan_SetMinImageCount(static_cast<u32>(swapchain.Images().size()));
}

void VulkanImGuiLayer::CreateContext(Window& window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;

    if (!ImGui_ImplGlfw_InitForVulkan(window.NativeHandle(), true)) {
        throw std::runtime_error("Failed to initialize Dear ImGui GLFW backend");
    }
}

void VulkanImGuiLayer::InitializeVulkanBackend(
    VkInstance instance,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanDevice& device,
    const VulkanRenderPass& renderPass,
    const VulkanSwapchain& swapchain
) {
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice.Handle();
    initInfo.Device = device.Handle();
    initInfo.QueueFamily = physicalDevice.QueueFamilies().graphicsFamily.value();
    initInfo.Queue = device.GraphicsQueue();
    initInfo.DescriptorPoolSize = 100;
    initInfo.MinImageCount = static_cast<u32>(swapchain.Images().size());
    initInfo.ImageCount = static_cast<u32>(swapchain.Images().size());
    initInfo.PipelineInfoMain.RenderPass = renderPass.Handle();
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = CheckVkResult;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("Failed to initialize Dear ImGui Vulkan backend");
    }
}

}
