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
#include <array>
#include <cstdio>
#include <iterator>
#include <utility>

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
        return "legacy captured-scene unavailable";
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
    case 9:
        return "authored cubemap load failed";
    case 10:
        return "captured scene resource unavailable";
    default:
        return "unknown";
    }
}

const char* ReflectionProbeRefreshPolicyName(u32 policy) {
    switch (policy) {
    case 0:
        return "static";
    case 1:
        return "file signature";
    case 2:
        return "forced";
    case 3:
        return "scene dirty";
    default:
        return "unknown";
    }
}

const char* CapturedSceneCaptureBackendName(u32 backend) {
    switch (backend) {
    case 0:
        return "none";
    case 1:
        return "analytic CPU";
    case 2:
        return "rasterized GPU";
    default:
        return "unknown";
    }
}

const char* CapturedSceneRefreshReasonName(u32 reason) {
    switch (reason) {
    case 0:
        return "none";
    case 1:
        return "initial";
    case 2:
        return "forced";
    case 3:
        return "forced policy";
    case 4:
        return "scene dirty override";
    case 5:
        return "membership changed";
    case 6:
        return "light changed";
    case 7:
        return "render changed";
    case 8:
        return "content changed";
    default:
        return "unknown";
    }
}

const char* AuthoredProbeFilterQualityName(u32 quality) {
    switch (quality) {
    case 0:
        return "low";
    case 1:
        return "medium";
    case 2:
        return "high";
    case 3:
        return "ultra";
    default:
        return "unknown";
    }
}

const char* IblQualityName(u32 quality) {
    switch (quality) {
    case 0:
        return "low";
    case 1:
        return "medium";
    case 2:
        return "high";
    case 3:
        return "ultra";
    default:
        return "unknown";
    }
}

const char* IblSourceName(u32 source) {
    switch (source) {
    case 0:
        return "procedural";
    case 1:
        return "visible skybox";
    case 2:
        return "authored equirect";
    case 3:
        return "authored cubemap";
    default:
        return "unknown";
    }
}

const char* IblSourceFallbackReasonName(u32 reason) {
    switch (reason) {
    case 0:
        return "none";
    case 1:
        return "runtime source unsupported";
    case 2:
        return "source asset missing";
    case 3:
        return "source load failed";
    default:
        return "unknown";
    }
}

const char* IblCachePolicyName(u32 policy) {
    switch (policy) {
    case 0:
        return "runtime generated";
    case 1:
        return "prefer offline";
    default:
        return "unknown";
    }
}

const char* IblCacheFallbackReasonName(u32 reason) {
    switch (reason) {
    case 0:
        return "none";
    case 1:
        return "offline cache unavailable";
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

void DrawShadowViewButton(
    const char* label,
    ForwardDebugView view,
    VulkanRenderDebugSettings& debugSettings
) {
    const bool selected = debugSettings.forwardView == view;
    if (selected) {
        ImGui::BeginDisabled();
    }
    if (ImGui::SmallButton(label)) {
        debugSettings.forwardView = view;
    }
    if (selected) {
        ImGui::EndDisabled();
    }
}

void DrawShadowQuickViews(VulkanRenderDebugSettings& debugSettings) {
    ImGui::SeparatorText("Shadow debug views");
    DrawShadowViewButton("Lit", ForwardDebugView::Lit, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Shadow", ForwardDebugView::Shadow, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Deferred", ForwardDebugView::DeferredShadow, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("CSM", ForwardDebugView::ShadowCascade, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("CSM Rx", ForwardDebugView::ShadowCascadeReceiver, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("CSM Atlas", ForwardDebugView::ShadowCascadeAtlas, debugSettings);
    DrawShadowViewButton("Local Atlas", ForwardDebugView::LocalShadowAtlas, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Local Mask", ForwardDebugView::LocalShadowVisibility, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Local One", ForwardDebugView::LocalShadowSelected, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Local Face", ForwardDebugView::LocalShadowFace, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Contact", ForwardDebugView::ContactShadow, debugSettings);
}

void DrawShadowDiagnostics(const RendererStats& stats) {
    const RendererShadowCascadeStats& cascades = stats.shadowCascades;
    const RendererLocalShadowAtlasStats& local = stats.localShadowAtlas;

    ImGui::SeparatorText("Shadow diagnostics");
    ImGui::Text(
        "CSM: %u active / %u configured, stable %s, range %.2f-%.2f, max %.2f",
        cascades.activeCount,
        cascades.configuredCount,
        cascades.stableSnappingEnabled ? "yes" : "no",
        cascades.nearDepth,
        cascades.farDepth,
        cascades.maxDistance
    );
    ImGui::Text(
        "CSM atlas: %s, tile %u, extent %ux%u, grid %ux%u",
        cascades.atlasAllocated ? "yes" : "no",
        cascades.atlasTileSize,
        cascades.atlasWidth,
        cascades.atlasHeight,
        cascades.atlasTileColumns,
        cascades.atlasTileRows
    );
    ImGui::Text(
        "CSM filter: PCF %ux%u, PCSS %.3f, blend %.3f, fade %.3f",
        cascades.pcfKernelRadius * 2u + 1u,
        cascades.pcfKernelRadius * 2u + 1u,
        cascades.pcssStrength,
        cascades.blendRatio,
        cascades.fadeRatio
    );
    ImGui::Text(
        "Splits: %.2f / %.2f / %.2f / %.2f",
        cascades.splitDepths[0],
        cascades.splitDepths[1],
        cascades.splitDepths[2],
        cascades.splitDepths[3]
    );
    ImGui::Text(
        "Texel world: %.4f / %.4f / %.4f / %.4f",
        cascades.texelWorldSizes[0],
        cascades.texelWorldSizes[1],
        cascades.texelWorldSizes[2],
        cascades.texelWorldSizes[3]
    );
    if (cascades.maxDistance > 120.0f) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.74f, 0.24f, 1.0f),
            "Wide CSM range: near-field shadows may become coarse"
        );
    }

    ImGui::Text(
        "Local atlas: %s, tile %u, extent %ux%u, assigned %u/%u, dropped %u",
        local.allocated ? "yes" : "no",
        local.tileSize,
        local.atlasWidth,
        local.atlasHeight,
        local.assignedTiles,
        local.requestedTiles,
        local.droppedTiles
    );
    ImGui::Text(
        "Local cache: %u eligible / %u hits / %u misses / %u skipped",
        local.cacheEligibleTiles,
        local.cacheHitTiles,
        local.cacheMissTiles,
        local.cacheSkippedTiles
    );
    ImGui::Text(
        "Local filter: bias %.5f / %.5f, PCF %.2f %ux%u, PCSS %.3f, face %.3f",
        local.biasMin,
        local.biasSlope,
        local.pcfRadius,
        local.pcfKernelRadius * 2u + 1u,
        local.pcfKernelRadius * 2u + 1u,
        local.pcssStrength,
        local.faceBlendStrength
    );
    ImGui::Text(
        "Contact: strength %.3f, length %.3f, thickness %.3f, steps %u, jitter %.3f, edge %.1f",
        cascades.contactShadowStrength,
        cascades.contactShadowLength,
        cascades.contactShadowThickness,
        cascades.contactShadowSteps,
        cascades.contactShadowJitterStrength,
        cascades.contactShadowEdgeFadePixels
    );
}

void DrawShadowWarning(const char* message) {
    ImGui::TextColored(ImVec4(1.0f, 0.74f, 0.24f, 1.0f), "%s", message);
}

const char* ShadowQualityName(VulkanShadowQuality quality) {
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
}

void DrawShadowControls(VulkanShadowSettings& settings);
const char* ProbeGridFallbackReasonName(u32 reason);

const char* LocalLightKindName(u32 kind) {
    switch (kind) {
    case 1:
        return "Point";
    case 2:
        return "Spot";
    case 3:
        return "Rect";
    default:
        return "Unknown";
    }
}

bool ShadowDebugPointLightEligible(const PointLight3D& light) {
    return light.enabled &&
        light.radius > 0.001f &&
        light.intensity > 0.0f;
}

bool ShadowDebugSpotLightEligible(const SpotLight3D& light) {
    return light.enabled &&
        light.radius > 0.001f &&
        light.intensity > 0.0f;
}

bool ShadowDebugRectLightEligible(const RectLight3D& light) {
    return light.enabled &&
        light.radius > 0.001f &&
        light.intensity > 0.0f &&
        light.width > 0.001f &&
        light.height > 0.001f;
}

struct SceneLocalLightSlotInfo {
    bool found = false;
    const char* kind = "Unknown";
    const std::string* name = nullptr;
    u32 sceneIndex = 0;
    glm::vec3 position{ 0.0f };
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    glm::vec3 color{ 1.0f };
    f32 radius = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
    f32 intensity = 0.0f;
};

SceneLocalLightSlotInfo FindSceneLocalLightSlot(
    const Scene3D& scene,
    u32 frameSlot
) {
    u32 slot = 0;
    const std::span<const PointLight3D> pointLights = scene.PointLights();
    for (std::size_t index = 0; index < pointLights.size(); ++index) {
        const PointLight3D& light = pointLights[index];
        if (!ShadowDebugPointLightEligible(light)) {
            continue;
        }
        if (slot == frameSlot) {
            return {
                true,
                "Point",
                &light.name,
                static_cast<u32>(index),
                light.position,
                { 0.0f, -1.0f, 0.0f },
                light.color,
                light.radius,
                0.0f,
                0.0f,
                light.intensity
            };
        }
        ++slot;
    }

    const std::span<const SpotLight3D> spotLights = scene.SpotLights();
    for (std::size_t index = 0; index < spotLights.size(); ++index) {
        const SpotLight3D& light = spotLights[index];
        if (!ShadowDebugSpotLightEligible(light)) {
            continue;
        }
        if (slot == frameSlot) {
            return {
                true,
                "Spot",
                &light.name,
                static_cast<u32>(index),
                light.position,
                light.direction,
                light.color,
                light.radius,
                0.0f,
                0.0f,
                light.intensity
            };
        }
        ++slot;
    }

    const std::span<const RectLight3D> rectLights = scene.RectLights();
    for (std::size_t index = 0; index < rectLights.size(); ++index) {
        const RectLight3D& light = rectLights[index];
        if (!ShadowDebugRectLightEligible(light)) {
            continue;
        }
        if (slot == frameSlot) {
            return {
                true,
                "Rect",
                &light.name,
                static_cast<u32>(index),
                light.position,
                light.direction,
                light.color,
                light.radius,
                light.width,
                light.height,
                light.intensity
            };
        }
        ++slot;
    }

    return {};
}

void DrawShadowDebugOverlay(
    const RendererStats& stats,
    VulkanRenderDebugSettings& debugSettings,
    VulkanShadowSettings* shadowSettings,
    const Scene3D& scene
) {
    const RendererDrawStats& draw = stats.draw;
    const RendererShadowCascadeStats& cascades = stats.shadowCascades;
    const RendererLocalShadowAtlasStats& local = stats.localShadowAtlas;
    const RendererBindStats& binds = stats.binds;

    ImGui::SetNextWindowPos(ImVec2(464.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(540.0f, 680.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Shadow Debug")) {
        ImGui::End();
        return;
    }

    DrawShadowQuickViews(debugSettings);

    ImGui::SeparatorText("Warnings");
    bool warned = false;
    if (cascades.maxDistance > 120.0f && cascades.activeCount > 0) {
        DrawShadowWarning("CSM max distance is wide; near-field shadows may look coarse.");
        warned = true;
    }
    if (local.droppedTiles > 0) {
        DrawShadowWarning("Local shadow tiles were dropped; some local shadows are missing.");
        warned = true;
    }
    if (local.cacheSkippedTiles > 0) {
        DrawShadowWarning("Local shadow cache skipped tiles; animated casters may be stale.");
        warned = true;
    }
    if (
        draw.shadowDraws == 0 &&
        draw.mainDraws > 0 &&
        (cascades.activeCount > 0 || local.assignedTiles > 0)
    ) {
        DrawShadowWarning("Shadow draws are zero while shadow passes look active.");
        warned = true;
    }
    if (
        binds.frameLightTileOverflowTiles > 0 ||
        binds.frameLightTileGpuOverflowTiles > 0 ||
        binds.frameLightTileAssignmentFallbacks > 0
    ) {
        DrawShadowWarning("Light tile culling overflow or fallback is active.");
        warned = true;
    }
    if (!warned) {
        ImGui::TextDisabled("No obvious shadow/tile warnings this frame.");
    }

    if (shadowSettings != nullptr) {
        ImGui::SeparatorText("Quality");
        ImGui::Text(
            "%s: map %u, CSM %u @ %.1fm, stable %s",
            ShadowQualityName(shadowSettings->quality),
            shadowSettings->mapSize,
            shadowSettings->cascadesEnabled ? shadowSettings->cascadeCount : 1u,
            shadowSettings->cascadeMaxDistance,
            shadowSettings->stableCascades ? "yes" : "no"
        );
        ImGui::Text(
            "Directional: PCF %.2f %ux%u, PCSS %.2f, bias %.4f / %.4f",
            shadowSettings->pcfRadius,
            shadowSettings->pcfKernelRadius * 2u + 1u,
            shadowSettings->pcfKernelRadius * 2u + 1u,
            shadowSettings->pcssStrength,
            shadowSettings->biasMin,
            shadowSettings->biasSlope
        );
        ImGui::Text(
            "Local: PCF %.2f %ux%u, PCSS %.2f, face %.2f",
            shadowSettings->localPcfRadius,
            shadowSettings->localPcfKernelRadius * 2u + 1u,
            shadowSettings->localPcfKernelRadius * 2u + 1u,
            shadowSettings->localPcssStrength,
            shadowSettings->localFaceBlendStrength
        );
        ImGui::Text(
            "Cost: CSM %u passes/%u draws, local %u passes/%u draws",
            binds.shadowCascadeAtlasPasses,
            binds.shadowCascadeAtlasDraws,
            local.recordedTilePasses,
            local.recordedDraws
        );
        ImGui::Text(
            "Rect samples: base %u, max %u, pattern %s, extra %u, budget-limited %u",
            local.rectShadowBaseSampleTiles,
            local.rectShadowMaxSampleTiles,
            local.rectShadowSamplePattern == 1u ? "2x2 surface" : "axis",
            local.rectShadowExtraSampleTiles,
            local.rectShadowBudgetLimitedSampleTiles
        );
    }

    ImGui::SeparatorText("Pipeline");
    ImGui::Text(
        "Draws: main %u / gbuffer %u / overlay %u / shadow %u",
        draw.mainDraws,
        draw.gBufferDraws,
        draw.overlayDraws,
        draw.shadowDraws
    );
    ImGui::Text(
        "Visible: main %u / overlay %u / shadow %u",
        draw.mainVisible,
        draw.overlayVisible,
        draw.shadowVisible
    );
    ImGui::Text(
        "Culled: main %u / overlay %u / shadow %u",
        draw.mainCulled,
        draw.overlayCulled,
        draw.shadowCulled
    );
    ImGui::Text(
        "Triangles: main %llu / gbuffer %llu / overlay %llu / shadow %llu",
        static_cast<unsigned long long>(draw.mainTriangles),
        static_cast<unsigned long long>(draw.gBufferTriangles),
        static_cast<unsigned long long>(draw.overlayTriangles),
        static_cast<unsigned long long>(draw.shadowTriangles)
    );
    ImGui::Text(
        "Skinned conservative bounds: main %u / shadow %u",
        draw.mainSkinnedConservativeBounds,
        draw.shadowSkinnedConservativeBounds
    );
    if (stats.gpu.available) {
        ImGui::Text(
            "GPU ms: shadow %.3f / main %.3f / overlay %.3f / imgui %.3f",
            stats.gpu.shadowMs,
            stats.gpu.mainMs,
            stats.gpu.overlayMs,
            stats.gpu.imguiMs
        );
    }

    ImGui::SeparatorText("Cascaded shadows");
    ImGui::Text(
        "Active/configured: %u / %u, stable %s",
        cascades.activeCount,
        cascades.configuredCount,
        cascades.stableSnappingEnabled ? "yes" : "no"
    );
    ImGui::Text(
        "Atlas: %s, tile %u, extent %ux%u, grid %ux%u, capacity %u",
        cascades.atlasAllocated ? "yes" : "no",
        cascades.atlasTileSize,
        cascades.atlasWidth,
        cascades.atlasHeight,
        cascades.atlasTileColumns,
        cascades.atlasTileRows,
        cascades.atlasCascadeCapacity
    );
    ImGui::Text(
        "Range: %.2f-%.2f, max %.2f, lambda %.2f",
        cascades.nearDepth,
        cascades.farDepth,
        cascades.maxDistance,
        cascades.splitLambda
    );
    ImGui::Text(
        "Blend %.3f, fade %.3f, PCF %ux%u, PCSS %.3f",
        cascades.blendRatio,
        cascades.fadeRatio,
        cascades.pcfKernelRadius * 2u + 1u,
        cascades.pcfKernelRadius * 2u + 1u,
        cascades.pcssStrength
    );
    ImGui::Text(
        "Splits: %.2f / %.2f / %.2f / %.2f",
        cascades.splitDepths[0],
        cascades.splitDepths[1],
        cascades.splitDepths[2],
        cascades.splitDepths[3]
    );
    ImGui::Text(
        "Texel world: %.4f / %.4f / %.4f / %.4f",
        cascades.texelWorldSizes[0],
        cascades.texelWorldSizes[1],
        cascades.texelWorldSizes[2],
        cascades.texelWorldSizes[3]
    );
    ImGui::Text(
        "Atlas pass: %u passes / %u draws / %u mesh binds",
        binds.shadowCascadeAtlasPasses,
        binds.shadowCascadeAtlasDraws,
        binds.shadowCascadeAtlasMeshBinds
    );
    const u32 fullCascadeDrawEstimate = binds.shadowCascadeAtlasPasses * draw.shadowVisible;
    if (fullCascadeDrawEstimate > 0) {
        ImGui::Text(
            "CSM draw culling: %u / %u candidate draws",
            binds.shadowCascadeAtlasDraws,
            fullCascadeDrawEstimate
        );
    }

    ImGui::SeparatorText("Local shadows");
    ImGui::Text(
        "Lights: %u shadowable, point %u / spot %u / rect %u",
        local.shadowableLocalLights,
        local.pointLightCount,
        local.spotLightCount,
        local.rectLightCount
    );
    ImGui::Text("Frame local lights: %u", binds.frameLocalLightCount);
    int selectedLightIndexForControls = -1;
    bool hasSelectableLocalLight = false;
    if (binds.frameLocalLightCount > 0) {
        const int maxDebugLightIndex =
            static_cast<int>(std::min<u32>(binds.frameLocalLightCount, 64u)) - 1;
        int selectedLightIndex = std::clamp(
            debugSettings.localShadowDebugLightIndex,
            -1,
            maxDebugLightIndex
        );
        if (selectedLightIndex != debugSettings.localShadowDebugLightIndex) {
            debugSettings.localShadowDebugLightIndex = selectedLightIndex;
        }
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::SliderInt(
                "Selected local light (-1 auto)",
                &selectedLightIndex,
                -1,
                maxDebugLightIndex
            )) {
            debugSettings.localShadowDebugLightIndex = selectedLightIndex;
        }
        selectedLightIndexForControls = selectedLightIndex;
        hasSelectableLocalLight = selectedLightIndex >= 0;
        ImGui::SameLine();
        if (ImGui::SmallButton("View selected")) {
            debugSettings.forwardView = ForwardDebugView::LocalShadowSelected;
        }
        if (shadowSettings != nullptr) {
            ImGui::SameLine();
            if (!hasSelectableLocalLight) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Isolate tiles")) {
                shadowSettings->debugLocalShadowLightIndex = selectedLightIndex;
            }
            if (!hasSelectableLocalLight) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear isolate")) {
                shadowSettings->debugLocalShadowLightIndex = -1;
            }
        }
    } else {
        ImGui::BeginDisabled();
        int selectedLightIndex = -1;
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderInt("Selected local light (-1 auto)", &selectedLightIndex, -1, -1);
        ImGui::SameLine();
        ImGui::SmallButton("View selected");
        ImGui::EndDisabled();
        debugSettings.localShadowDebugLightIndex = -1;
    }
    ImGui::Text(
        "View selected: %d, tile generation filter: %d",
        debugSettings.localShadowDebugLightIndex,
        local.debugLightIndex
    );
    if (
        hasSelectableLocalLight &&
        local.debugLightIndex >= 0 &&
        local.debugLightIndex != selectedLightIndexForControls
    ) {
        DrawShadowWarning("Selected debug view light and isolated shadow-tile light are different.");
    }
    ImGui::Text(
        "Tiles: requested %u, assigned %u, dropped %u, capacity %u",
        local.requestedTiles,
        local.assignedTiles,
        local.droppedTiles,
        local.tileCapacity
    );
    ImGui::Text(
        "Tile kinds: point faces %u / spot %u / rect %u",
        local.pointFaceTiles,
        local.spotTiles,
        local.rectTiles
    );
    ImGui::Text(
        "Atlas: %s, tile %u, extent %ux%u, grid %ux%u",
        local.allocated ? "yes" : "no",
        local.tileSize,
        local.atlasWidth,
        local.atlasHeight,
        local.tileColumns,
        local.tileRows
    );
    ImGui::Text(
        "Pass: %u tile passes / %u draws / %u mesh binds",
        local.recordedTilePasses,
        local.recordedDraws,
        local.recordedMeshBinds
    );
    const u32 fullLocalDrawEstimate = local.recordedTilePasses * draw.shadowVisible;
    if (fullLocalDrawEstimate > 0) {
        ImGui::Text(
            "Local draw culling: %u / %u candidate draws",
            local.recordedDraws,
            fullLocalDrawEstimate
        );
    }
    ImGui::Text(
        "Cache: %u eligible / %u hit / %u miss / %u skipped",
        local.cacheEligibleTiles,
        local.cacheHitTiles,
        local.cacheMissTiles,
        local.cacheSkippedTiles
    );
    ImGui::Text(
        "Enabled: point %s / spot %s / rect %s, only light %d",
        local.pointShadowEnabled ? "on" : "off",
        local.spotShadowEnabled ? "on" : "off",
        local.rectShadowEnabled ? "on" : "off",
        local.debugLightIndex
    );
    ImGui::SeparatorText("Selected local-shadow attribution");
    if (local.attributionLightValid == 0u) {
        ImGui::TextDisabled("Select a local light index or isolate one light to show attribution.");
    } else {
        const SceneLocalLightSlotInfo selectedLight =
            FindSceneLocalLightSlot(scene, static_cast<u32>(local.attributionLightIndex));
        ImGui::Text(
            "GPU local slot %d: %s shadow source",
            local.attributionLightIndex,
            LocalLightKindName(local.attributionLightKind)
        );
        if (selectedLight.found && selectedLight.name != nullptr) {
            ImGui::Text(
                "Scene %s[%u]: %s",
                selectedLight.kind,
                selectedLight.sceneIndex,
                selectedLight.name->c_str()
            );
            ImGui::Text(
                "Pos (%.2f, %.2f, %.2f), radius %.2f, intensity %.2f",
                selectedLight.position.x,
                selectedLight.position.y,
                selectedLight.position.z,
                selectedLight.radius,
                selectedLight.intensity
            );
            if (local.attributionLightKind == 2u || local.attributionLightKind == 3u) {
                ImGui::Text(
                    "Dir (%.2f, %.2f, %.2f)",
                    selectedLight.direction.x,
                    selectedLight.direction.y,
                    selectedLight.direction.z
                );
            }
            if (local.attributionLightKind == 3u) {
                ImGui::Text(
                    "Rect size %.2f x %.2f",
                    selectedLight.width,
                    selectedLight.height
                );
            }
        } else {
            DrawShadowWarning("Selected GPU local slot does not map back to an eligible Scene3D light.");
        }
        ImGui::Text(
            "Tiles: expected %u, requested %u, assigned %u, dropped %u",
            local.attributionExpectedTiles,
            local.attributionRequestedTiles,
            local.attributionAssignedTiles,
            local.attributionDroppedTiles
        );
        ImGui::Text(
            "Cache/recording: hit %u, miss %u, recorded %u passes / %u draws",
            local.attributionCacheHitTiles,
            local.attributionCacheMissTiles,
            local.attributionRecordedTilePasses,
            local.attributionRecordedDraws
        );
        ImGui::Text(
            "Caster attribution: %u candidate draws / %u unique, signature %llu",
            local.attributionCandidateDraws,
            local.attributionUniqueCasters,
            static_cast<unsigned long long>(local.attributionCasterSignature)
        );
        if (!local.attributionTileCandidateDraws.empty()) {
            ImGui::TextWrapped(
                "Tile candidates: %s",
                local.attributionTileCandidateDraws.c_str()
            );
        }
        if (!local.attributionCasterSummary.empty()) {
            ImGui::TextWrapped(
                "Caster sample: %s",
                local.attributionCasterSummary.c_str()
            );
        }
        if (local.attributionShadowEnabled == 0u) {
            DrawShadowWarning("Selected light kind has local shadows disabled.");
        }
        if (local.attributionMatchesGenerationFilter == 0u) {
            DrawShadowWarning("Selected light is filtered out by the local-shadow generation filter.");
        }
        if (
            local.attributionRequestedTiles > 0u &&
            local.attributionAssignedTiles < local.attributionRequestedTiles
        ) {
            DrawShadowWarning("Selected light did not receive all requested local-shadow tiles.");
        }
        if (
            local.attributionAssignedTiles > 0u &&
            local.attributionRecordedTilePasses == 0u &&
            local.attributionCacheHitTiles == local.attributionAssignedTiles
        ) {
            ImGui::TextDisabled("All selected light tiles were cache hits this frame.");
        }
    }
    ImGui::Text(
        "Shared filter: bias %.5f / %.5f, PCF %.2f %ux%u, PCSS %.3f, face %.3f",
        local.biasMin,
        local.biasSlope,
        local.pcfRadius,
        local.pcfKernelRadius * 2u + 1u,
        local.pcfKernelRadius * 2u + 1u,
        local.pcssStrength,
        local.faceBlendStrength
    );
    ImGui::Text(
        "Point filter: bias %.5f / %.5f, PCF %.2f %ux%u, PCSS %.3f",
        local.pointBiasMin,
        local.pointBiasSlope,
        local.pointPcfRadius,
        local.pointPcfKernelRadius * 2u + 1u,
        local.pointPcfKernelRadius * 2u + 1u,
        local.pointPcssStrength
    );
    ImGui::Text(
        "Spot filter: bias %.5f / %.5f, PCF %.2f %ux%u, PCSS %.3f",
        local.spotBiasMin,
        local.spotBiasSlope,
        local.spotPcfRadius,
        local.spotPcfKernelRadius * 2u + 1u,
        local.spotPcfKernelRadius * 2u + 1u,
        local.spotPcssStrength
    );
    ImGui::Text(
        "Rect filter: bias %.5f / %.5f, PCF %.2f %ux%u, PCSS %.3f, scale %.2f",
        local.rectBiasMin,
        local.rectBiasSlope,
        local.rectPcfRadius,
        local.rectPcfKernelRadius * 2u + 1u,
        local.rectPcfKernelRadius * 2u + 1u,
        local.rectPcssStrength,
        local.rectBiasScale
    );

    ImGui::SeparatorText("Contact shadows");
    ImGui::Text(
        "Strength %.3f, length %.3f, thickness %.3f, steps %u",
        cascades.contactShadowStrength,
        cascades.contactShadowLength,
        cascades.contactShadowThickness,
        cascades.contactShadowSteps
    );
    ImGui::Text(
        "Jitter %.3f, edge fade %.1f px",
        cascades.contactShadowJitterStrength,
        cascades.contactShadowEdgeFadePixels
    );

    ImGui::SeparatorText("Light tiles");
    ImGui::Text(
        "Tiles: %ux%u @ %upx (%u total)",
        binds.frameLightTileCountX,
        binds.frameLightTileCountY,
        binds.frameLightTileSize,
        binds.frameLightTileCount
    );
    ImGui::Text(
        "Assignments: %u / %u, CPU fallbacks %u",
        binds.frameLightTileAssignments,
        binds.frameLightTileAssignmentCapacity,
        binds.frameLightTileAssignmentFallbacks
    );
    ImGui::Text(
        "CPU overflow: %u/%u indices, %u tiles, dropped %u",
        binds.frameLightTileOverflowAssignments,
        binds.frameLightTileOverflowCapacity,
        binds.frameLightTileOverflowTiles,
        binds.frameLightTileOverflowDropped
    );
    ImGui::Text(
        "GPU readback: %s, saturated %u, max raw %u, raw sum %llu",
        binds.frameLightTileGpuReadbackValid ? "yes" : "no",
        binds.frameLightTileGpuSaturatedTiles,
        binds.frameLightTileGpuMaxCandidates,
        static_cast<unsigned long long>(binds.frameLightTileGpuRawCandidates)
    );
    ImGui::Text(
        "GPU overflow: tiles %u, dropped tiles %u, stored %u, dropped %u",
        binds.frameLightTileGpuOverflowTiles,
        binds.frameLightTileGpuOverflowDroppedTiles,
        binds.frameLightTileGpuOverflowStored,
        binds.frameLightTileGpuOverflowDropped
    );

    ImGui::SeparatorText("Debug passes");
    ImGui::Text(
        "Deferred shadow: %u draws / %u frame binds / %u texture binds",
        binds.deferredShadowDebugDraws,
        binds.deferredShadowDebugFrameBinds,
        binds.deferredShadowDebugTextureBinds
    );
    ImGui::Text(
        "CSM view: %u draws / %u frame binds / %u texture binds",
        binds.shadowCascadeDebugDraws,
        binds.shadowCascadeDebugFrameBinds,
        binds.shadowCascadeDebugTextureBinds
    );
    ImGui::Text(
        "Local atlas: %u draws / %u frame binds / %u texture binds",
        binds.localShadowAtlasDebugDraws,
        binds.localShadowAtlasDebugFrameBinds,
        binds.localShadowAtlasDebugTextureBinds
    );
    ImGui::Text(
        "Local mask: %u draws / %u frame binds / %u texture binds",
        binds.localShadowVisibilityDebugDraws,
        binds.localShadowVisibilityDebugFrameBinds,
        binds.localShadowVisibilityDebugTextureBinds
    );
    ImGui::Text(
        "Local face: %u draws / %u frame binds / %u texture binds",
        binds.localShadowFaceDebugDraws,
        binds.localShadowFaceDebugFrameBinds,
        binds.localShadowFaceDebugTextureBinds
    );
    ImGui::Text(
        "Contact: %u draws / %u frame binds / %u gbuffer binds",
        binds.contactShadowDebugDraws,
        binds.contactShadowDebugFrameBinds,
        binds.contactShadowDebugGBufferBinds
    );

    if (shadowSettings != nullptr && ImGui::CollapsingHeader("Live controls")) {
        DrawShadowControls(*shadowSettings);
    }

    ImGui::End();
}

const char* ToneMapModeName(u32 mode) {
    switch (mode) {
    case 0:
        return "ACES";
    case 1:
        return "Reinhard";
    case 2:
        return "Linear Clamp";
    default:
        return "Unknown";
    }
}

bool DirectionalLightEligible(const DirectionalLight3D* light) {
    return light != nullptr &&
        light->enabled &&
        glm::dot(light->direction, light->direction) > 0.0001f &&
        light->intensity > 0.0f;
}

bool PointLightEligible(const PointLight3D& light) {
    return light.enabled &&
        light.radius > 0.001f &&
        light.intensity > 0.0f;
}

bool SpotLightEligible(const SpotLight3D& light) {
    return light.enabled &&
        light.radius > 0.001f &&
        light.intensity > 0.0f;
}

bool RectLightEligible(const RectLight3D& light) {
    return light.enabled &&
        light.radius > 0.001f &&
        light.intensity > 0.0f &&
        light.width > 0.001f &&
        light.height > 0.001f;
}

bool ReflectionProbeEligible(const ReflectionProbe3D& probe) {
    return probe.enabled &&
        probe.radius > 0.001f &&
        probe.intensity > 0.0001f &&
        probe.blendStrength > 0.0001f;
}

struct SceneLightingInventory {
    u32 directionalEligible = 0;
    u32 pointTotal = 0;
    u32 pointEligible = 0;
    u32 spotTotal = 0;
    u32 spotEligible = 0;
    u32 rectTotal = 0;
    u32 rectEligible = 0;
    u32 probeTotal = 0;
    u32 probeEligible = 0;
};

SceneLightingInventory BuildSceneLightingInventory(const Scene3D& scene) {
    SceneLightingInventory inventory{};
    inventory.directionalEligible =
        DirectionalLightEligible(scene.PrimaryDirectionalLight()) ? 1u : 0u;

    const std::span<const PointLight3D> pointLights = scene.PointLights();
    inventory.pointTotal = static_cast<u32>(pointLights.size());
    for (const PointLight3D& light : pointLights) {
        if (PointLightEligible(light)) {
            ++inventory.pointEligible;
        }
    }

    const std::span<const SpotLight3D> spotLights = scene.SpotLights();
    inventory.spotTotal = static_cast<u32>(spotLights.size());
    for (const SpotLight3D& light : spotLights) {
        if (SpotLightEligible(light)) {
            ++inventory.spotEligible;
        }
    }

    const std::span<const RectLight3D> rectLights = scene.RectLights();
    inventory.rectTotal = static_cast<u32>(rectLights.size());
    for (const RectLight3D& light : rectLights) {
        if (RectLightEligible(light)) {
            ++inventory.rectEligible;
        }
    }

    const std::span<const ReflectionProbe3D> probes = scene.ReflectionProbes();
    inventory.probeTotal = static_cast<u32>(probes.size());
    for (const ReflectionProbe3D& probe : probes) {
        if (ReflectionProbeEligible(probe)) {
            ++inventory.probeEligible;
        }
    }

    return inventory;
}

void DrawSmallColorSwatch(const char* id, glm::vec3 color) {
    const glm::vec3 clamped = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    ImGui::ColorButton(
        id,
        ImVec4(clamped.r, clamped.g, clamped.b, 1.0f),
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
        ImVec2(14.0f, 14.0f)
    );
}

void DrawLightFrameSlot(bool eligible, u32 frameSlot, u32 frameLocalLightCount) {
    if (!eligible) {
        ImGui::TextDisabled("not bound");
        return;
    }

    if (frameSlot < frameLocalLightCount) {
        ImGui::Text("GPU local slot %u", frameSlot);
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.52f, 0.18f, 1.0f), "not in frame buffer");
}

void DrawSceneDirectionalLightInventory(const Scene3D& scene) {
    const DirectionalLight3D* light = scene.PrimaryDirectionalLight();
    if (light == nullptr) {
        ImGui::TextDisabled("Directional: no Scene3D primary light.");
        return;
    }

    ImGui::BulletText(
        "%s: %s dir (%.2f, %.2f, %.2f), intensity %.2f, ambient %.2f, specular %.2f",
        light->name.c_str(),
        DirectionalLightEligible(light) ? "bound" : "not bound",
        light->direction.x,
        light->direction.y,
        light->direction.z,
        light->intensity,
        light->ambient,
        light->specular
    );
}

void DrawScenePointLightInventory(
    const Scene3D& scene,
    u32 frameLocalLightCount,
    u32& frameSlot
) {
    const std::span<const PointLight3D> lights = scene.PointLights();
    if (lights.empty()) {
        return;
    }

    if (!ImGui::TreeNode("Scene point lights")) {
        for (const PointLight3D& light : lights) {
            if (PointLightEligible(light)) {
                ++frameSlot;
            }
        }
        return;
    }

    for (std::size_t index = 0; index < lights.size(); ++index) {
        const PointLight3D& light = lights[index];
        const bool eligible = PointLightEligible(light);
        ImGui::PushID(static_cast<int>(index));
        DrawSmallColorSwatch("##point-color", light.color);
        ImGui::SameLine();
        ImGui::Text(
            "%zu %s: pos (%.2f, %.2f, %.2f), radius %.2f, intensity %.2f",
            index,
            light.name.c_str(),
            light.position.x,
            light.position.y,
            light.position.z,
            light.radius,
            light.intensity
        );
        ImGui::SameLine();
        DrawLightFrameSlot(eligible, frameSlot, frameLocalLightCount);
        if (eligible) {
            ++frameSlot;
        }
        ImGui::PopID();
    }

    ImGui::TreePop();
}

void DrawSceneSpotLightInventory(
    const Scene3D& scene,
    u32 frameLocalLightCount,
    u32& frameSlot
) {
    const std::span<const SpotLight3D> lights = scene.SpotLights();
    if (lights.empty()) {
        return;
    }

    if (!ImGui::TreeNode("Scene spot lights")) {
        for (const SpotLight3D& light : lights) {
            if (SpotLightEligible(light)) {
                ++frameSlot;
            }
        }
        return;
    }

    for (std::size_t index = 0; index < lights.size(); ++index) {
        const SpotLight3D& light = lights[index];
        const bool eligible = SpotLightEligible(light);
        ImGui::PushID(static_cast<int>(index));
        DrawSmallColorSwatch("##spot-color", light.color);
        ImGui::SameLine();
        ImGui::Text(
            "%zu %s: pos (%.2f, %.2f, %.2f), dir (%.2f, %.2f, %.2f), radius %.2f, cone %.1f/%.1f, intensity %.2f",
            index,
            light.name.c_str(),
            light.position.x,
            light.position.y,
            light.position.z,
            light.direction.x,
            light.direction.y,
            light.direction.z,
            light.radius,
            light.innerConeDegrees,
            light.outerConeDegrees,
            light.intensity
        );
        ImGui::SameLine();
        DrawLightFrameSlot(eligible, frameSlot, frameLocalLightCount);
        if (eligible) {
            ++frameSlot;
        }
        ImGui::PopID();
    }

    ImGui::TreePop();
}

void DrawSceneRectLightInventory(
    const Scene3D& scene,
    u32 frameLocalLightCount,
    u32& frameSlot
) {
    const std::span<const RectLight3D> lights = scene.RectLights();
    if (lights.empty()) {
        return;
    }

    if (!ImGui::TreeNode("Scene rect lights")) {
        for (const RectLight3D& light : lights) {
            if (RectLightEligible(light)) {
                ++frameSlot;
            }
        }
        return;
    }

    for (std::size_t index = 0; index < lights.size(); ++index) {
        const RectLight3D& light = lights[index];
        const bool eligible = RectLightEligible(light);
        ImGui::PushID(static_cast<int>(index));
        DrawSmallColorSwatch("##rect-color", light.color);
        ImGui::SameLine();
        ImGui::Text(
            "%zu %s: pos (%.2f, %.2f, %.2f), dir (%.2f, %.2f, %.2f), size %.2fx%.2f, radius %.2f, intensity %.2f",
            index,
            light.name.c_str(),
            light.position.x,
            light.position.y,
            light.position.z,
            light.direction.x,
            light.direction.y,
            light.direction.z,
            light.width,
            light.height,
            light.radius,
            light.intensity
        );
        ImGui::SameLine();
        DrawLightFrameSlot(eligible, frameSlot, frameLocalLightCount);
        if (eligible) {
            ++frameSlot;
        }
        ImGui::PopID();
    }

    ImGui::TreePop();
}

void DrawSceneReflectionProbeInventory(const Scene3D& scene) {
    const std::span<const ReflectionProbe3D> probes = scene.ReflectionProbes();
    if (probes.empty() || !ImGui::TreeNode("Scene reflection probes")) {
        return;
    }

    for (std::size_t index = 0; index < probes.size(); ++index) {
        const ReflectionProbe3D& probe = probes[index];
        ImGui::BulletText(
            "%zu %s: %s center (%.2f, %.2f, %.2f), radius %.2f, intensity %.2f, blend %.2f, source %s",
            index,
            probe.name.c_str(),
            ReflectionProbeEligible(probe) ? "eligible" : "inactive",
            probe.center.x,
            probe.center.y,
            probe.center.z,
            probe.radius,
            probe.intensity,
            probe.blendStrength,
            ReflectionCaptureSourceName(static_cast<u32>(probe.captureSource))
        );
    }

    ImGui::TreePop();
}

void DrawSceneLightInventory(
    const Scene3D& scene,
    const RendererBindStats& binds
) {
    const SceneLightingInventory inventory = BuildSceneLightingInventory(scene);
    const u32 sceneEligibleLocal =
        inventory.pointEligible + inventory.spotEligible + inventory.rectEligible;

    ImGui::SeparatorText("Scene light inventory");
    ImGui::Text(
        "Scene authored: directional %u, point %u/%u, spot %u/%u, rect %u/%u, probes %u/%u",
        inventory.directionalEligible,
        inventory.pointEligible,
        inventory.pointTotal,
        inventory.spotEligible,
        inventory.spotTotal,
        inventory.rectEligible,
        inventory.rectTotal,
        inventory.probeEligible,
        inventory.probeTotal
    );
    ImGui::Text(
        "Shader-bound: directional %u, local %u, rect %u",
        binds.frameDirectionalLightCount,
        binds.frameLocalLightCount,
        binds.frameRectLightCount
    );
    if (
        binds.frameDirectionalLightCount > 0u &&
        inventory.directionalEligible == 0u
    ) {
        DrawShadowWarning("Frame directional light is coming from a renderer/material fallback, not a Scene3D primary light.");
    }
    if (binds.frameLocalLightCount > sceneEligibleLocal) {
        DrawShadowWarning("Frame contains local lights that are not authored in Scene3D. Check injected debug lights or renderer fallbacks.");
    }
    if (binds.frameLocalLightCount < sceneEligibleLocal) {
        DrawShadowWarning("Some eligible Scene3D local lights did not reach the frame light buffer.");
    }
    if (binds.frameRectLightCount != inventory.rectEligible) {
        DrawShadowWarning("Scene rect-light count does not match the shader-bound rect-light count.");
    }

    DrawSceneDirectionalLightInventory(scene);
    u32 frameSlot = 0;
    DrawScenePointLightInventory(scene, binds.frameLocalLightCount, frameSlot);
    DrawSceneSpotLightInventory(scene, binds.frameLocalLightCount, frameSlot);
    DrawSceneRectLightInventory(scene, binds.frameLocalLightCount, frameSlot);
    DrawSceneReflectionProbeInventory(scene);
}

void DrawLightingQuickViews(VulkanRenderDebugSettings& debugSettings) {
    ImGui::SeparatorText("Lighting debug views");
    DrawShadowViewButton("Lit", ForwardDebugView::Lit, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("HDR", ForwardDebugView::DeferredHdr, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Direct", ForwardDebugView::DeferredDirect, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Ambient", ForwardDebugView::DeferredAmbient, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Specular", ForwardDebugView::DeferredSpecular, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Energy", ForwardDebugView::DeferredEnergyBalance, debugSettings);

    DrawShadowViewButton("Roughness", ForwardDebugView::GBufferRoughness, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Metallic", ForwardDebugView::GBufferMetallic, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Emissive", ForwardDebugView::GBufferEmissive, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Probe", ForwardDebugView::ReflectionProbe, debugSettings);

    DrawShadowViewButton("Probe Grid", ForwardDebugView::ProbeGrid, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Bloom", ForwardDebugView::Bloom, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Tone", ForwardDebugView::ToneMapping, debugSettings);
    ImGui::SameLine();
    DrawShadowViewButton("Auto Exp", ForwardDebugView::AutoExposure, debugSettings);
}

void DrawLightingDebugOverlay(
    const RendererStats& stats,
    VulkanRenderDebugSettings& debugSettings,
    const Scene3D& scene
) {
    const RendererBindStats& binds = stats.binds;
    const RendererReflectionProbeStats& reflection = stats.reflectionProbe;
    const RendererPostProcessStats& post = stats.postProcess;

    ImGui::SetNextWindowPos(ImVec2(1018.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 600.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Lighting Debug")) {
        ImGui::End();
        return;
    }

    DrawLightingQuickViews(debugSettings);

    ImGui::SeparatorText("Warnings");
    bool warned = false;
    if (
        reflection.fallbackEnabled == 0u &&
        reflection.selectedCubemapSamplingCount == 0u &&
        binds.frameMaterialSpecularHintCount > 0u
    ) {
        DrawShadowWarning("Specular materials exist, but no reflection fallback or probe cubemap is sampled.");
        warned = true;
    }
    if (
        reflection.selectedProbeCount > 0u &&
        reflection.selectedCubemapSamplingCount == 0u &&
        binds.frameMaterialSpecularHintCount > 0u
    ) {
        DrawShadowWarning("A local reflection probe is selected, but cubemap sampling is falling back.");
        warned = true;
    }
    if (binds.frameMaterialOverflowCount > 0u) {
        DrawShadowWarning("Frame material buffer overflow; some material parameters may be missing.");
        warned = true;
    }
    if (post.toneMapMode == 2u) {
        DrawShadowWarning("Linear clamp tone mapping can make highlights look flat or clipped.");
        warned = true;
    }
    if (
        post.bloomEnabled == 0u &&
        binds.frameMaterialEmissiveHintCount > 0u
    ) {
        DrawShadowWarning("Emissive materials are present while bloom is disabled.");
        warned = true;
    }
    if (!warned) {
        ImGui::TextDisabled("No obvious lighting/material warnings this frame.");
    }

    ImGui::SeparatorText("Materials");
    ImGui::Text(
        "Frame materials: %u/%u, overflow %u, textured %u, mip bias %.2f",
        binds.frameMaterialCount,
        binds.frameMaterialCapacity,
        binds.frameMaterialOverflowCount,
        binds.frameMaterialTexturedCount,
        binds.frameMaterialTextureMipLodBias
    );
    ImGui::Text(
        "Routes: opaque %u, transparent %u, special %u, alpha mask/blend %u/%u",
        binds.frameMaterialOpaqueCount,
        binds.frameMaterialTransparentCount,
        binds.frameMaterialForwardSpecialCount,
        binds.frameMaterialAlphaMaskCount,
        binds.frameMaterialAlphaBlendCount
    );
    ImGui::Text(
        "PBR hints: emissive %u, specular %u, specular textures %u",
        binds.frameMaterialEmissiveHintCount,
        binds.frameMaterialSpecularHintCount,
        binds.frameMaterialSpecularTextureCount
    );
    ImGui::Text(
        "Extensions: clearcoat %u (%u/%u tex), transmission %u (%u tex), volume %u",
        binds.frameMaterialClearcoatCount,
        binds.frameMaterialClearcoatTextureCount,
        binds.frameMaterialClearcoatRoughnessTextureCount,
        binds.frameMaterialTransmissionCount,
        binds.frameMaterialTransmissionTextureCount,
        binds.frameMaterialVolumeCount
    );

    ImGui::SeparatorText("Lights");
    ImGui::Text(
        "Frame lights: total %u, directional %u, local %u, rect %u",
        binds.frameLightTotalCount,
        binds.frameDirectionalLightCount,
        binds.frameLocalLightCount,
        binds.frameRectLightCount
    );
    ImGui::Text(
        "Tiles: %ux%u size %u, assignments %u/%u, overflow tiles CPU/GPU %u/%u",
        binds.frameLightTileCountX,
        binds.frameLightTileCountY,
        binds.frameLightTileSize,
        binds.frameLightTileAssignments,
        binds.frameLightTileAssignmentCapacity,
        binds.frameLightTileOverflowTiles,
        binds.frameLightTileGpuOverflowTiles
    );
    DrawSceneLightInventory(scene, binds);

    ImGui::SeparatorText("Reflections");
    ImGui::Text(
        "Fallback: %s, global IBL cube %s, diffuse %.2f, specular %.2f, horizon %.2f",
        reflection.fallbackEnabled ? "enabled" : "off",
        reflection.globalIblCubemapSamplingEnabled ? "sampling" : "procedural",
        reflection.diffuseIntensity,
        reflection.specularIntensity,
        reflection.horizonBlend
    );
    ImGui::Text(
        "Local probes: scene %u, active %u, eligible %u, selected %u, blended %u, dropped %u",
        reflection.sceneProbeCount,
        reflection.activeProbeCount,
        reflection.sceneEligibleProbeCount,
        reflection.selectedProbeCount,
        reflection.blendedProbeCount,
        reflection.droppedProbeCount
    );
    ImGui::Text(
        "Capture slots: slots %u, ready %u, fallback %u, cubemap sampling %u",
        reflection.selectedCaptureSlotCount,
        reflection.selectedCaptureResourceReadyCount,
        reflection.selectedCaptureFallbackCount,
        reflection.selectedCubemapSamplingCount
    );
    ImGui::Text(
        "Top probe: index %d, weight %.3f, normalized %.3f, masks selected/box 0x%X/0x%X",
        reflection.selectedProbeIndex,
        reflection.maxBlendWeight,
        reflection.normalizedBlendWeightSum,
        reflection.selectedProbeMask,
        reflection.selectedBoxProjectionMask
    );
    ImGui::Text(
        "Capture source: %s, fallback %s, resource %s, descriptor %s",
        ReflectionCaptureSourceName(reflection.captureSourceType),
        ReflectionCaptureFallbackReasonName(reflection.captureFallbackReason),
        reflection.captureResourceReady ? "ready" : "missing",
        reflection.captureDescriptorBound ? "bound" : "fallback"
    );
    ImGui::Text(
        "Authored cubemaps: loaded %u, missing %u, failed %u, face %u, mips %u, prefilter %s/%u",
        reflection.authoredCubemapLoadedCount,
        reflection.authoredCubemapMissingCount,
        reflection.authoredCubemapLoadFailedCount,
        reflection.authoredCubemapFaceSize,
        reflection.authoredCubemapMipCount,
        AuthoredProbeFilterQualityName(reflection.authoredCubemapFilterQuality),
        reflection.authoredCubemapPrefilterSampleCount
    );
    ImGui::Text(
        "Diffuse lobes: ready/applied %u/%u, count %u, energy %.3f, mask 0x%X",
        reflection.authoredCubemapDiffuseLobesReadyCount,
        reflection.authoredCubemapDiffuseLobesApplied,
        reflection.authoredCubemapDiffuseLobeCount,
        reflection.authoredCubemapDiffuseLobeEnergy,
        reflection.selectedDiffuseLobeReadyMask
    );

    ImGui::SeparatorText("Probe grid / IBL");
    ImGui::Text(
        "Probe grid: %s, shader %s, fallback %s, blend %.2f, probes %u, cells %u",
        stats.probeGrid.enabled ? "enabled" : "off",
        stats.probeGrid.shaderIntegrationEnabled ? "on" : "off",
        ProbeGridFallbackReasonName(stats.probeGrid.fallbackReason),
        stats.probeGrid.blendStrength,
        stats.probeGrid.probeCount,
        stats.probeGrid.cellCount
    );
    ImGui::Text(
        "IBL settings: quality %s, source %s -> %s, fallback %s",
        IblQualityName(stats.ibl.quality),
        IblSourceName(stats.ibl.requestedSource),
        IblSourceName(stats.ibl.actualSource),
        IblSourceFallbackReasonName(stats.ibl.sourceFallbackReason)
    );
    ImGui::Text(
        "IBL cache: %s, fallback %s, hit %u, runtime generated %u",
        IblCachePolicyName(stats.ibl.cachePolicy),
        IblCacheFallbackReasonName(stats.ibl.cacheFallbackReason),
        stats.ibl.cacheHit,
        stats.ibl.runtimeGenerated
    );
    ImGui::Text(
        "IBL source asset: specified %u, found %u, signature %llu",
        stats.ibl.sourceAssetSpecified,
        stats.ibl.sourceAssetFound,
        static_cast<unsigned long long>(stats.ibl.sourceSignature)
    );
    ImGui::Text(
        "IBL: BRDF %s %u, irradiance %s %u, prefiltered %s %u mips %u",
        stats.ibl.brdfLutAllocated ? "on" : "off",
        stats.ibl.brdfLutSize,
        stats.ibl.irradianceMapAllocated ? "on" : "off",
        stats.ibl.irradianceFaceSize,
        stats.ibl.prefilteredMapAllocated ? "on" : "off",
        stats.ibl.prefilteredFaceSize,
        stats.ibl.prefilteredMipCount
    );

    ImGui::SeparatorText("Post");
    ImGui::Text(
        "Tone mapping: %s, mode %s, exposure %.3f, white %.2f",
        post.toneMappingEnabled ? "enabled" : "linear fallback",
        ToneMapModeName(post.toneMapMode),
        post.exposure,
        post.toneMapWhitePoint
    );
    ImGui::Text(
        "Auto exposure: %s, history %s, GPU %.3f target %.3f avg lum %.3f, fallbacks %u",
        post.autoExposureEnabled ? "enabled" : "off",
        post.autoExposureHistoryValid ? "valid" : "cold",
        post.autoExposureGpuExposure,
        post.autoExposureGpuTargetExposure,
        post.autoExposureGpuAverageLuminance,
        post.autoExposureFallbacks
    );
    ImGui::Text(
        "Bloom: %s, pyramid %s, mips %u, intensity %.3f, threshold %.3f, radius %.2f",
        post.bloomEnabled ? "enabled" : "off",
        post.bloomPyramidEnabled ? "enabled" : "off",
        post.bloomPyramidMipCount,
        post.bloomIntensity,
        post.bloomThreshold,
        post.bloomRadiusPixels
    );
    ImGui::Text(
        "Color grade: %s, sat %.3f, contrast %.3f, gamma %.3f, LUT %s %.3f",
        post.colorGradingEnabled ? "enabled" : "off",
        post.colorGradingSaturation,
        post.colorGradingContrast,
        post.colorGradingGamma,
        post.colorGradingLutEnabled ? "on" : "off",
        post.colorGradingLutStrength
    );
    ImGui::Text(
        "Sharpening: %s, strength %.3f, radius %.2f",
        post.sharpeningEnabled ? "enabled" : "off",
        post.sharpeningStrength,
        post.sharpeningRadiusPixels
    );

    ImGui::End();
}

void DrawShadowControls(VulkanShadowSettings& settings) {
    ImGui::SeparatorText("Shadows");

    static constexpr VulkanShadowQuality kQualities[] = {
        VulkanShadowQuality::Off,
        VulkanShadowQuality::Low,
        VulkanShadowQuality::Medium,
        VulkanShadowQuality::High,
        VulkanShadowQuality::Ultra
    };

    if (ImGui::BeginCombo("Quality##Shadow", ShadowQualityName(settings.quality))) {
        for (const VulkanShadowQuality quality : kQualities) {
            const bool selected = settings.quality == quality;
            if (ImGui::Selectable(ShadowQualityName(quality), selected)) {
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

    if (ImGui::Button("Forward3D 60m CSM baseline##Shadow")) {
        ApplyForward3DProductionShadowPreset(settings);
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
    ImGui::Checkbox("Global IBL cubemap##Shadow", &settings.globalIblCubemapEnabled);
    ImGui::Checkbox("Local probe cubemap##Shadow", &settings.reflectionProbeCubemapEnabled);
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
    ImGui::Checkbox("Static light-probe grid##Shadow", &settings.probeGridEnabled);
    ImGui::SliderFloat(
        "Probe grid blend##Shadow",
        &settings.probeGridBlendStrength,
        0.0f,
        2.0f,
        "%.2f"
    );
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
    if (ImGui::SliderFloat(
            "Local bias min##Shadow",
            &settings.localBiasMin,
            0.0f,
            0.02f,
            "%.5f"
        )) {
        SyncLocalShadowKindFiltersToShared(settings);
    }
    if (ImGui::SliderFloat(
            "Local bias slope##Shadow",
            &settings.localBiasSlope,
            0.0f,
            0.05f,
            "%.5f"
        )) {
        SyncLocalShadowKindFiltersToShared(settings);
    }
    if (ImGui::SliderFloat(
            "Local PCF radius##Shadow",
            &settings.localPcfRadius,
            0.0f,
            4.0f,
            "%.2f"
        )) {
        SyncLocalShadowKindFiltersToShared(settings);
    }
    int localPcfKernelRadius = static_cast<int>(settings.localPcfKernelRadius);
    if (ImGui::SliderInt(
            "Local PCF kernel radius##Shadow",
            &localPcfKernelRadius,
            0,
            2
        )) {
        settings.localPcfKernelRadius =
            static_cast<u32>(std::clamp(localPcfKernelRadius, 0, 2));
        SyncLocalShadowKindFiltersToShared(settings);
    }
    if (ImGui::SliderFloat(
            "Local PCSS strength##Shadow",
            &settings.localPcssStrength,
            0.0f,
            1.0f,
            "%.3f"
        )) {
        SyncLocalShadowKindFiltersToShared(settings);
    }
    ImGui::SliderFloat(
        "Local face blend##Shadow",
        &settings.localFaceBlendStrength,
        0.0f,
        1.0f,
        "%.3f"
    );
    ImGui::SliderFloat(
        "Rect local bias scale##Shadow",
        &settings.rectLightShadowBiasScale,
        0.0f,
        32.0f,
        "%.2f"
    );
    int rectSampleTiles = static_cast<int>(settings.rectLightShadowSampleTiles);
    if (ImGui::SliderInt(
            "Rect sample tiles##Shadow",
            &rectSampleTiles,
            2,
            4
        )) {
        settings.rectLightShadowSampleTiles =
            rectSampleTiles >= 4 ? 4u : 2u;
    }
    ImGui::SeparatorText("Local shadow per-light filters");
    const auto drawLocalFilterControls = [](const char* label, VulkanLocalShadowFilterSettings& filter) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SliderFloat("Bias min", &filter.biasMin, 0.0f, 0.02f, "%.5f");
        ImGui::SliderFloat("Bias slope", &filter.biasSlope, 0.0f, 0.05f, "%.5f");
        ImGui::SliderFloat("PCF radius", &filter.pcfRadius, 0.0f, 4.0f, "%.2f");
        int kernelRadius = static_cast<int>(filter.pcfKernelRadius);
        if (ImGui::SliderInt("PCF kernel radius", &kernelRadius, 0, 2)) {
            filter.pcfKernelRadius =
                static_cast<u32>(std::clamp(kernelRadius, 0, 2));
        }
        ImGui::SliderFloat("PCSS strength", &filter.pcssStrength, 0.0f, 1.0f, "%.3f");
        ImGui::PopID();
    };
    drawLocalFilterControls("Point lights", settings.pointLocalShadowFilter);
    drawLocalFilterControls("Spot lights", settings.spotLocalShadowFilter);
    drawLocalFilterControls("Rect lights", settings.rectLocalShadowFilter);

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
    case ForwardDebugView::DeferredAmbientDiffuse:
        return "Deferred Ambient Diffuse";
    case ForwardDebugView::DeferredAmbientSpecular:
        return "Deferred Ambient Specular";
    case ForwardDebugView::DeferredAmbientProbe:
        return "Deferred Ambient Probe";
    case ForwardDebugView::DeferredEnergyBalance:
        return "Lighting Energy";
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
    case ForwardDebugView::ShadowCascadeReceiver:
        return "Shadow Cascade Receiver";
    case ForwardDebugView::ShadowCascadeAtlas:
        return "Shadow Cascade Atlas";
    case ForwardDebugView::LocalShadowAtlas:
        return "Local Shadow Atlas";
    case ForwardDebugView::LocalShadowVisibility:
        return "Local Shadow Visibility";
    case ForwardDebugView::LocalShadowSelected:
        return "Local Shadow Selected";
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
    case ForwardDebugView::ProbeGrid:
        return "Probe Grid";
    case ForwardDebugView::ProbeGridCell:
        return "Probe Grid Cell";
    case ForwardDebugView::Taa:
        return "TAA";
    case ForwardDebugView::TaaRejection:
        return "TAA Rejection";
    case ForwardDebugView::TaaHistory:
        return "TAA History";
    case ForwardDebugView::TaaReprojection:
        return "TAA Reprojection";
    }

    return "Lit";
}

const char* ProbeGridFallbackReasonName(u32 reason) {
    switch (static_cast<RendererProbeGridFallbackReason>(reason)) {
    case RendererProbeGridFallbackReason::None:
        return "none";
    case RendererProbeGridFallbackReason::Disabled:
        return "disabled";
    case RendererProbeGridFallbackReason::BlendZero:
        return "blend zero";
    case RendererProbeGridFallbackReason::BufferUnavailable:
        return "buffer unavailable";
    case RendererProbeGridFallbackReason::InvalidLayout:
        return "invalid layout";
    case RendererProbeGridFallbackReason::FrameIndexOutOfRange:
        return "frame index out of range";
    }

    return "unknown";
}

const char* TemporalAntialiasingModeName(u32 mode) {
    switch (mode) {
    case 1:
        return "Native TAA";
    case 2:
        return "DLSS DLAA L";
    case 3:
        return "DLSS SR Quality 66.7%";
    case 4:
        return "DLSS SR Balanced 58%";
    case 5:
        return "DLSS SR Performance 50%";
    case 6:
        return "Off";
    case 0:
        return "Environment";
    default:
        return "Unknown";
    }
}

void DrawTemporalAntialiasingControls(
    u32 temporalAntialiasingMode,
    const VulkanImGuiLayer::TemporalAntialiasingModeCallback&
        temporalAntialiasingModeCallback
) {
    ImGui::SeparatorText("Anti-Aliasing");
    ImGui::Text(
        "Current: %s",
        TemporalAntialiasingModeName(temporalAntialiasingMode)
    );
    ImGui::TextUnformatted("Hotkey: F6");
    static constexpr std::array<std::pair<u32, const char*>, 6> kModes{{
        { 6u, "Off" },
        { 1u, "Native TAA" },
        { 2u, "DLSS DLAA L" },
        { 3u, "DLSS SR Quality 66.7%" },
        { 4u, "DLSS SR Balanced 58%" },
        { 5u, "DLSS SR Performance 50%" }
    }};
    if (temporalAntialiasingModeCallback) {
        for (const auto& [mode, label] : kModes) {
            if (ImGui::RadioButton(label, temporalAntialiasingMode == mode)) {
                temporalAntialiasingModeCallback(mode);
            }
        }
    } else {
        ImGui::BeginDisabled();
        for (const auto& [mode, label] : kModes) {
            ImGui::RadioButton(label, temporalAntialiasingMode == mode);
        }
        ImGui::EndDisabled();
    }
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
        ForwardDebugView::DeferredAmbientDiffuse,
        ForwardDebugView::DeferredAmbientSpecular,
        ForwardDebugView::DeferredAmbientProbe,
        ForwardDebugView::DeferredEnergyBalance,
        ForwardDebugView::DeferredSpecular,
        ForwardDebugView::DeferredLightComplexity,
        ForwardDebugView::DeferredTileOccupancy,
        ForwardDebugView::DeferredMaterialTable,
        ForwardDebugView::ShadowCascade,
        ForwardDebugView::ShadowCascadeReceiver,
        ForwardDebugView::ShadowCascadeAtlas,
        ForwardDebugView::LocalShadowAtlas,
        ForwardDebugView::LocalShadowVisibility,
        ForwardDebugView::LocalShadowSelected,
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
        ForwardDebugView::Sharpening,
        ForwardDebugView::ProbeGrid,
        ForwardDebugView::ProbeGridCell,
        ForwardDebugView::Taa,
        ForwardDebugView::TaaRejection,
        ForwardDebugView::TaaHistory,
        ForwardDebugView::TaaReprojection
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
        "Shadow cascades: %u active / %u configured, stable %s, lambda %.2f, PCF %ux%u, PCSS %.3f, blend %.3f, fade %.3f, receiver guard %.2f, range %.2f-%.2f",
        shadowCascades.activeCount,
        shadowCascades.configuredCount,
        shadowCascades.stableSnappingEnabled ? "yes" : "no",
        shadowCascades.splitLambda,
        shadowCascades.pcfKernelRadius * 2u + 1u,
        shadowCascades.pcfKernelRadius * 2u + 1u,
        shadowCascades.pcssStrength,
        shadowCascades.blendRatio,
        shadowCascades.fadeRatio,
        shadowCascades.receiverGuardRatio,
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
        "IBL settings: quality %s, source %s -> %s, source fallback %s, cache %s/%s",
        IblQualityName(stats.ibl.quality),
        IblSourceName(stats.ibl.requestedSource),
        IblSourceName(stats.ibl.actualSource),
        IblSourceFallbackReasonName(stats.ibl.sourceFallbackReason),
        IblCachePolicyName(stats.ibl.cachePolicy),
        IblCacheFallbackReasonName(stats.ibl.cacheFallbackReason)
    );
    ImGui::Text(
        "IBL source asset: specified %u, found %u, signature %llu, cache hit/runtime %u/%u",
        stats.ibl.sourceAssetSpecified,
        stats.ibl.sourceAssetFound,
        static_cast<unsigned long long>(stats.ibl.sourceSignature),
        stats.ibl.cacheHit,
        stats.ibl.runtimeGenerated
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
        "Probe grid: %s, shader %s, fallback %s, %ux%ux%u (%u probes, %u cells), %u vec4/probe, lobes %u, blend %.2f, updates %u",
        stats.probeGrid.enabled ? "enabled" : "off",
        stats.probeGrid.shaderIntegrationEnabled ? "on" : "off",
        ProbeGridFallbackReasonName(stats.probeGrid.fallbackReason),
        stats.probeGrid.sizeX,
        stats.probeGrid.sizeY,
        stats.probeGrid.sizeZ,
        stats.probeGrid.probeCount,
        stats.probeGrid.cellCount,
        stats.probeGrid.vec4sPerProbe,
        stats.probeGrid.directionalLobeCount,
        stats.probeGrid.blendStrength,
        stats.probeGrid.bufferUpdates
    );
    ImGui::Text(
        "Probe grid bounds: min %.1f %.1f %.1f, max %.1f %.1f %.1f, debug contribution %s, cells %s",
        stats.probeGrid.boundsMinX,
        stats.probeGrid.boundsMinY,
        stats.probeGrid.boundsMinZ,
        stats.probeGrid.boundsMaxX,
        stats.probeGrid.boundsMaxY,
        stats.probeGrid.boundsMaxZ,
        stats.probeGrid.debugViewEnabled ? "on" : "off",
        stats.probeGrid.cellDebugViewEnabled ? "on" : "off"
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
        "Reflection refresh top slots: [%u/%u/%u, %u/%u/%u, %u/%u/%u, %u/%u/%u]",
        stats.reflectionProbe.selectedRefreshPolicies[0],
        stats.reflectionProbe.selectedCapturedScenePlaceholderReady[0],
        stats.reflectionProbe.selectedCapturedSceneInvalidated[0],
        stats.reflectionProbe.selectedRefreshPolicies[1],
        stats.reflectionProbe.selectedCapturedScenePlaceholderReady[1],
        stats.reflectionProbe.selectedCapturedSceneInvalidated[1],
        stats.reflectionProbe.selectedRefreshPolicies[2],
        stats.reflectionProbe.selectedCapturedScenePlaceholderReady[2],
        stats.reflectionProbe.selectedCapturedSceneInvalidated[2],
        stats.reflectionProbe.selectedRefreshPolicies[3],
        stats.reflectionProbe.selectedCapturedScenePlaceholderReady[3],
        stats.reflectionProbe.selectedCapturedSceneInvalidated[3]
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
        "Reflection authored cubemaps: loaded %u, missing %u, failed %u, uploads %u, six-face %u, equirect %u/%u, hdr %u/%u, prefilter %u/%u/%u samples %u mode %u quality %s seam %s, irradiance %u/%u [%.2f %.2f %.2f], lobes %u/%u count %u mask 0x%X energy %.3f, cache hit/reload/check %u/%u/%u, face %u, mips %u, format %d, source %u",
        stats.reflectionProbe.authoredCubemapLoadedCount,
        stats.reflectionProbe.authoredCubemapMissingCount,
        stats.reflectionProbe.authoredCubemapLoadFailedCount,
        stats.reflectionProbe.authoredCubemapUploadCount,
        stats.reflectionProbe.authoredCubemapSixFaceLoadedCount,
        stats.reflectionProbe.authoredCubemapEquirectangularLoadedCount,
        stats.reflectionProbe.authoredCubemapEquirectangularConversionCount,
        stats.reflectionProbe.authoredCubemapHdrLoadedCount,
        stats.reflectionProbe.authoredCubemapHdr,
        stats.reflectionProbe.authoredCubemapPrefilteredLoadedCount,
        stats.reflectionProbe.authoredCubemapPrefilteredUploadCount,
        stats.reflectionProbe.authoredCubemapGeneratedMipCount,
        stats.reflectionProbe.authoredCubemapPrefilterSampleCount,
        stats.reflectionProbe.authoredCubemapPrefilterMode,
        AuthoredProbeFilterQualityName(
            stats.reflectionProbe.authoredCubemapFilterQuality
        ),
        stats.reflectionProbe.authoredCubemapSeamAwareFiltering ? "on" : "off",
        stats.reflectionProbe.authoredCubemapIrradianceReadyCount,
        stats.reflectionProbe.authoredCubemapIrradianceApplied,
        stats.reflectionProbe.authoredCubemapIrradianceR,
        stats.reflectionProbe.authoredCubemapIrradianceG,
        stats.reflectionProbe.authoredCubemapIrradianceB,
        stats.reflectionProbe.authoredCubemapDiffuseLobesReadyCount,
        stats.reflectionProbe.authoredCubemapDiffuseLobesApplied,
        stats.reflectionProbe.authoredCubemapDiffuseLobeCount,
        stats.reflectionProbe.selectedDiffuseLobeReadyMask,
        stats.reflectionProbe.authoredCubemapDiffuseLobeEnergy,
        stats.reflectionProbe.authoredCubemapCacheHitCount,
        stats.reflectionProbe.authoredCubemapReloadCount,
        stats.reflectionProbe.authoredCubemapRefreshCheckCount,
        stats.reflectionProbe.authoredCubemapFaceSize,
        stats.reflectionProbe.authoredCubemapMipCount,
        static_cast<int>(stats.reflectionProbe.authoredCubemapFormat),
        stats.reflectionProbe.authoredCubemapSourceType
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
        "Reflection capture: %s, refresh %s, ready %s, descriptor %s, fallback %s",
        ReflectionCaptureSourceName(stats.reflectionProbe.captureSourceType),
        ReflectionProbeRefreshPolicyName(stats.reflectionProbe.refreshPolicy),
        stats.reflectionProbe.captureResourceReady ? "yes" : "no",
        stats.reflectionProbe.captureDescriptorBound ? "bound" : "fallback",
        ReflectionCaptureFallbackReasonName(
            stats.reflectionProbe.captureFallbackReason
        )
    );
    ImGui::Text(
        "Captured scene: requested %u, allocated %u, ready %u, invalidated %u, refresh requests %u, force %u, scene dirty %u",
        stats.reflectionProbe.capturedSceneRequestedCount,
        stats.reflectionProbe.capturedScenePlaceholderAllocatedCount,
        stats.reflectionProbe.capturedScenePlaceholderReadyCount,
        stats.reflectionProbe.capturedSceneInvalidatedCount,
        stats.reflectionProbe.capturedSceneRefreshRequestedCount,
        stats.reflectionProbe.forcedRefreshRequested,
        stats.reflectionProbe.sceneDirtyRequested
    );
    ImGui::Text(
        "Captured scene backend: %s, faces %u, geometry %s, uploads/checks %u/%u",
        CapturedSceneCaptureBackendName(
            stats.reflectionProbe.capturedSceneCaptureBackend
        ),
        stats.reflectionProbe.capturedSceneFaceCount,
        stats.reflectionProbe.capturedSceneRasterizedGeometry ? "rasterized" : "analytic",
        stats.reflectionProbe.capturedSceneUploadCount,
        stats.reflectionProbe.capturedSceneRefreshCheckCount
    );
    ImGui::Text(
        "Captured scene resources: allocated %u, ready %u, in flight %u, distinct views %u, duplicate selected 0x%X",
        stats.reflectionProbe.capturedSceneProbeResourceCount,
        stats.reflectionProbe.capturedSceneReadyProbeCount,
        stats.reflectionProbe.capturedSceneInFlightProbeCount,
        stats.reflectionProbe.capturedSceneDistinctActiveViewCount,
        stats.reflectionProbe.selectedCapturedSceneDuplicateActiveViewMask
    );
    ImGui::Text(
        "Captured diffuse irradiance: %s, dispatches %u, samples %u, face %u",
        stats.reflectionProbe.capturedSceneDiffuseIrradianceReady ? "ready" : "pending",
        stats.reflectionProbe.capturedSceneDiffuseIrradianceDispatchCount,
        stats.reflectionProbe.capturedSceneDiffuseIrradianceSampleCount,
        stats.reflectionProbe.capturedSceneDiffuseIrradianceFaceSize
    );
    ImGui::Text(
        "Captured diffuse mapping: ready %u, distinct %u, match 0x%X, duplicate 0x%X",
        stats.reflectionProbe.capturedSceneDiffuseIrradianceReadyProbeCount,
        stats.reflectionProbe.capturedSceneDistinctActiveDiffuseIrradianceViewCount,
        stats.reflectionProbe
            .selectedCapturedSceneDiffuseIrradianceMapMatchesActiveMask,
        stats.reflectionProbe
            .selectedCapturedSceneDiffuseIrradianceDuplicateActiveViewMask
    );
    ImGui::Text(
        "Captured directional shadow: requested/ready %u/%u, pass/draw/casters %u/%u/%u, map %u, faces 0x%X, probe %d, camera independent %u, local tiles suppressed %u",
        stats.reflectionProbe.capturedSceneDirectionalShadowRequested,
        stats.reflectionProbe.capturedSceneDirectionalShadowReady,
        stats.reflectionProbe.capturedSceneDirectionalShadowPassCount,
        stats.reflectionProbe.capturedSceneDirectionalShadowDrawCount,
        stats.reflectionProbe.capturedSceneDirectionalShadowCasterCount,
        stats.reflectionProbe.capturedSceneDirectionalShadowMapSize,
        stats.reflectionProbe.capturedSceneDirectionalShadowFaceMask,
        stats.reflectionProbe.capturedSceneDirectionalShadowProbeSceneIndex,
        stats.reflectionProbe.capturedSceneDirectionalShadowCameraIndependent,
        stats.reflectionProbe.capturedSceneDirectionalShadowLocalTilesSuppressed
    );
    ImGui::Text(
        "Reflection probe sampled mips: [%u %u %u %u]",
        stats.reflectionProbe.selectedCaptureMipCounts[0],
        stats.reflectionProbe.selectedCaptureMipCounts[1],
        stats.reflectionProbe.selectedCaptureMipCounts[2],
        stats.reflectionProbe.selectedCaptureMipCounts[3]
    );
    ImGui::Text(
        "Captured scene GPU progress: resources %s, faces %u/%u pending %u, pass/draw/visible/culled %u/%u/%u/%u, mip %u ready %s, last face %u",
        stats.reflectionProbe.capturedSceneGpuResourcesAllocated ? "ready" : "off",
        stats.reflectionProbe.capturedSceneFacesRendered,
        stats.reflectionProbe.capturedSceneFaceCount,
        stats.reflectionProbe.capturedSceneFacesPending,
        stats.reflectionProbe.capturedSceneCapturePassCount,
        stats.reflectionProbe.capturedSceneCaptureDrawCount,
        stats.reflectionProbe.capturedSceneCaptureVisibleCount,
        stats.reflectionProbe.capturedSceneCaptureCulledCount,
        stats.reflectionProbe.capturedSceneMipGenerationCount,
        stats.reflectionProbe.capturedSceneMipChainReady ? "ready" : "pending",
        stats.reflectionProbe.capturedSceneLastCapturedFace
    );
    ImGui::Text(
        "Captured scene GGX prefilter: %s, dispatches %u, samples %u",
        stats.reflectionProbe.capturedSceneGgxPrefilterReady ? "ready" : "pending",
        stats.reflectionProbe.capturedSceneGgxPrefilterDispatchCount,
        stats.reflectionProbe.capturedSceneGgxPrefilterSampleCount
    );
    ImGui::Text(
        "Captured scene refresh: %s, last %s, performed %u, dirty 0x%X, signature %u -> %u, radiance %u",
        CapturedSceneRefreshReasonName(
            stats.reflectionProbe.capturedSceneRefreshReason
        ),
        CapturedSceneRefreshReasonName(
            stats.reflectionProbe.capturedSceneLastRefreshReason
        ),
        stats.reflectionProbe.capturedSceneRefreshPerformed,
        stats.reflectionProbe.capturedSceneDirtyMask,
        stats.reflectionProbe.capturedSceneActiveSignature,
        stats.reflectionProbe.capturedSceneRequestedSignature,
        stats.reflectionProbe.capturedSceneRadianceSignature
    );
    ImGui::Text(
        "Captured scene revisions: membership %llu, light %llu, render %llu",
        static_cast<unsigned long long>(
            stats.reflectionProbe.capturedSceneMembershipRevision
        ),
        static_cast<unsigned long long>(
            stats.reflectionProbe.capturedSceneLightRevision
        ),
        static_cast<unsigned long long>(
            stats.reflectionProbe.capturedSceneRenderRevision
        )
    );
    ImGui::Text(
        "Reflection probe spatial policy: box projection %s, parallax %s, influence mode %u",
        stats.reflectionProbe.boxProjectionEnabled ? "on" : "off",
        stats.reflectionProbe.parallaxCorrectionEnabled ? "on" : "off",
        stats.reflectionProbe.influenceMode
    );
    ImGui::Text(
        "Reflection blend masks: selected 0x%X, box 0x%X, scene 0x%X, influence 0x%X, normalized %.3f, fallback %u, weights [%.3f %.3f %.3f %.3f]",
        stats.reflectionProbe.selectedProbeMask,
        stats.reflectionProbe.selectedBoxProjectionMask,
        stats.reflectionProbe.selectedSceneOwnedMask,
        stats.reflectionProbe.selectedPositiveInfluenceMask,
        stats.reflectionProbe.normalizedBlendWeightSum,
        stats.reflectionProbe.blendWeightNormalizationFallbackCount,
        stats.reflectionProbe.selectedNormalizedBlendWeights[0],
        stats.reflectionProbe.selectedNormalizedBlendWeights[1],
        stats.reflectionProbe.selectedNormalizedBlendWeights[2],
        stats.reflectionProbe.selectedNormalizedBlendWeights[3]
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
        "Temporal: velocity %s, history %s reset %u reason %u, jitter %s applied %u [%.3f %.3f] px, aux %s, TAA %s/%s suppress %u history %s copies %u weight %.2f fallback %u reject %s clamp %s consumers ready 0x%X active 0x%X unsupported 0x%X",
        stats.temporal.velocityCameraMotionReady ? "camera-ready" : "cold",
        stats.temporal.historyValid ? "valid" : "cold",
        stats.temporal.historyReset,
        stats.temporal.historyResetReason,
        stats.temporal.jitterEnabled ? "prepared" : "off",
        stats.temporal.jitterApplied,
        stats.temporal.jitterPixelsX,
        stats.temporal.jitterPixelsY,
        stats.temporal.velocityMaterialAuxMigrated ? "split" : "missing",
        stats.temporal.taaResolveConfigured ? "configured" : "off",
        stats.temporal.taaResolveEnabled ? "enabled" : "fallback",
        stats.temporal.taaResolveSuppressedForUpscaler,
        stats.temporal.taaHistoryColorReady ? "ready" : "cold",
        stats.temporal.taaHistoryColorCopies,
        stats.temporal.taaHistoryWeight,
        stats.temporal.taaFallbackReason,
        stats.temporal.taaRejectionEnabled ? "on" : "off",
        stats.temporal.taaNeighborhoodClampEnabled ? "on" : "off",
        stats.temporal.temporalConsumerReadinessMask,
        stats.temporal.temporalConsumerActiveMask,
        stats.temporal.temporalConsumerUnsupportedMask
    );
    ImGui::Text(
        "Temporal upscale: scale req/active %.2f/%.2f applied %u, display %ux%u requested %ux%u active %ux%u, dynamic %u/%u TAAU %u upscale %u/%u input %u fallback %u contract %u inputs 0x%X/0x%X plugin %u/%u",
        stats.temporal.renderScaleRequested,
        stats.temporal.renderScaleActive,
        stats.temporal.renderScaleApplied,
        stats.temporal.temporalUpscaleDisplayWidth,
        stats.temporal.temporalUpscaleDisplayHeight,
        stats.temporal.temporalUpscaleRequestedWidth,
        stats.temporal.temporalUpscaleRequestedHeight,
        stats.temporal.temporalUpscaleActiveWidth,
        stats.temporal.temporalUpscaleActiveHeight,
        stats.temporal.dynamicResolutionRequested,
        stats.temporal.dynamicResolutionEnabled,
        stats.temporal.taauRequested,
        stats.temporal.temporalUpscaleRequested,
        stats.temporal.temporalUpscaleEnabled,
        stats.temporal.temporalUpscaleInputReady,
        stats.temporal.temporalUpscaleFallbackReason,
        stats.temporal.temporalUpscaleContractReady,
        stats.temporal.temporalUpscaleInputReadinessMask,
        stats.temporal.temporalUpscaleRequiredInputMask,
        stats.temporal.temporalUpscalerPluginRequested,
        stats.temporal.temporalUpscalerPluginAvailable
    );
    ImGui::Text(
        "Temporal upscaler package: provider %u fallback %u dir/header/lib/runtime %u/%u/%u/%u SR/FG/RR/transformer %u/%u/%u/%u version %u.%u.%u package %u adapter %u",
        stats.temporal.temporalUpscalerProviderKind,
        stats.temporal.temporalUpscalerPackageFallbackReason,
        stats.temporal.temporalUpscalerPackageDirectoryFound,
        stats.temporal.temporalUpscalerHeadersFound,
        stats.temporal.temporalUpscalerImportLibraryFound,
        stats.temporal.temporalUpscalerRuntimeFound,
        stats.temporal.temporalUpscalerDlssSuperResolutionSymbolsFound,
        stats.temporal.temporalUpscalerDlssFrameGenerationSymbolsFound,
        stats.temporal.temporalUpscalerDlssRayReconstructionSymbolsFound,
        stats.temporal.temporalUpscalerDlssTransformerPresetSymbolsFound,
        stats.temporal.temporalUpscalerSdkVersionMajor,
        stats.temporal.temporalUpscalerSdkVersionMinor,
        stats.temporal.temporalUpscalerSdkVersionPatch,
        stats.temporal.temporalUpscalerPackageReady,
        stats.temporal.temporalUpscalerEvaluateAdapterAvailable
    );
    ImGui::Text(
        "Temporal upscaler runtime: fallback %u compiled %u init %u/%u result 0x%X caps %u result 0x%X SR %u driver %u min %u.%u feature 0x%X quality %u preset %u opt %u result 0x%X %ux%u min %ux%u max %ux%u sharp %.3f",
        stats.temporal.temporalUpscalerRuntimeFallbackReason,
        stats.temporal.temporalUpscalerAdapterCompiled,
        stats.temporal.temporalUpscalerInitializationAttempted,
        stats.temporal.temporalUpscalerInitialized,
        stats.temporal.temporalUpscalerInitializationResult,
        stats.temporal.temporalUpscalerCapabilityParametersReady,
        stats.temporal.temporalUpscalerCapabilityQueryResult,
        stats.temporal.temporalUpscalerDlssSuperResolutionSupported,
        stats.temporal.temporalUpscalerNeedsUpdatedDriver,
        stats.temporal.temporalUpscalerMinDriverVersionMajor,
        stats.temporal.temporalUpscalerMinDriverVersionMinor,
        stats.temporal.temporalUpscalerFeatureInitResult,
        stats.temporal.temporalUpscalerDlssQualityMode,
        stats.temporal.temporalUpscalerDlssRecommendedPreset,
        stats.temporal.temporalUpscalerOptimalSettingsQueried,
        stats.temporal.temporalUpscalerOptimalSettingsResult,
        stats.temporal.temporalUpscalerOptimalRenderWidth,
        stats.temporal.temporalUpscalerOptimalRenderHeight,
        stats.temporal.temporalUpscalerMinRenderWidth,
        stats.temporal.temporalUpscalerMinRenderHeight,
        stats.temporal.temporalUpscalerMaxRenderWidth,
        stats.temporal.temporalUpscalerMaxRenderHeight,
        stats.temporal.temporalUpscalerSharpness
    );
    ImGui::Text(
        "Temporal upscaler evaluate: output %u %ux%u fmt %u requested/attempted %u/%u fallback %u params %u result 0x%X create %u/%u result 0x%X recreate %u reason %u eval %u result 0x%X ready %u render %ux%u flags 0x%X reset %u jitter %.3f %.3f mv %.2f %.2f sharp %.3f",
        stats.temporal.temporalUpscaleOutputAllocated,
        stats.temporal.temporalUpscaleOutputWidth,
        stats.temporal.temporalUpscaleOutputHeight,
        stats.temporal.temporalUpscaleOutputFormat,
        stats.temporal.temporalUpscalerEvaluateRequested,
        stats.temporal.temporalUpscalerEvaluateAttempted,
        stats.temporal.temporalUpscalerEvaluateFallbackReason,
        stats.temporal.temporalUpscalerEvaluateParametersAllocated,
        stats.temporal.temporalUpscalerEvaluateParameterAllocationResult,
        stats.temporal.temporalUpscalerFeatureCreateAttempted,
        stats.temporal.temporalUpscalerFeatureCreated,
        stats.temporal.temporalUpscalerFeatureCreateResult,
        stats.temporal.temporalUpscalerFeatureRecreated,
        stats.temporal.temporalUpscalerFeatureRecreationReason,
        stats.temporal.temporalUpscalerDlssEvaluateAttempted,
        stats.temporal.temporalUpscalerDlssEvaluateResult,
        stats.temporal.temporalUpscalerDlssOutputReady,
        stats.temporal.temporalUpscalerDlssRenderWidth,
        stats.temporal.temporalUpscalerDlssRenderHeight,
        stats.temporal.temporalUpscalerDlssCreateFlags,
        stats.temporal.temporalUpscalerDlssReset,
        stats.temporal.temporalUpscalerDlssJitterOffsetX,
        stats.temporal.temporalUpscalerDlssJitterOffsetY,
        stats.temporal.temporalUpscalerDlssMotionVectorScaleX,
        stats.temporal.temporalUpscalerDlssMotionVectorScaleY,
        stats.temporal.temporalUpscalerDlssEvaluateSharpness
    );
    ImGui::Text(
        "Temporal upscaler post source: requested %u active %u fallback %u",
        stats.temporal.temporalUpscalePostSourceRequested,
        stats.temporal.temporalUpscalePostSourceActive,
        stats.temporal.temporalUpscalePostSourceFallbackReason
    );
    ImGui::Text(
        "DLSS quality gate: requested %u ready %u fallback %u masks required/ready/blocker 0x%X/0x%X/0x%X output/camera/object/reactive/transparency/exposure/post/baseline %u/%u/%u/%u/%u/%u/%u/%u",
        stats.temporal.temporalUpscalerDlssQualityGateRequested,
        stats.temporal.temporalUpscalerDlssQualityGateReady,
        stats.temporal.temporalUpscalerDlssQualityGateFallbackReason,
        stats.temporal.temporalUpscalerDlssQualityRequiredMask,
        stats.temporal.temporalUpscalerDlssQualityReadyMask,
        stats.temporal.temporalUpscalerDlssQualityBlockerMask,
        stats.temporal.temporalUpscalerDlssQualityEvaluateOutputReady,
        stats.temporal.temporalUpscalerDlssQualityCameraMotionReady,
        stats.temporal.temporalUpscalerDlssQualityObjectMotionReady,
        stats.temporal.temporalUpscalerDlssQualityReactiveMaskReady,
        stats.temporal.temporalUpscalerDlssQualityTransparencyMaskReady,
        stats.temporal.temporalUpscalerDlssQualityExposurePolicyReady,
        stats.temporal.temporalUpscalerDlssQualityPostOrderingReady,
        stats.temporal.temporalUpscalerDlssQualityReferenceBaselineReady
    );
    ImGui::Text(
        "Temporal upscaler requirements: queried %u result 0x%X supported %u mask 0x%X minHW %u minOS %s inst %u/%u enabled %u missing avail/enabled %u/%u [%s] [%s] dev %u/%u enabled %u missing avail/enabled %u/%u [%s] [%s]",
        stats.temporal.temporalUpscalerFeatureRequirementsQueried,
        stats.temporal.temporalUpscalerFeatureRequirementsResult,
        stats.temporal.temporalUpscalerFeatureRequirementsSupported,
        stats.temporal.temporalUpscalerFeatureSupportedMask,
        stats.temporal.temporalUpscalerMinHardwareArchitecture,
        stats.temporal.temporalUpscalerMinOsVersion.c_str(),
        stats.temporal.temporalUpscalerInstanceExtensionAvailableCount,
        stats.temporal.temporalUpscalerInstanceExtensionRequirementCount,
        stats.temporal.temporalUpscalerInstanceExtensionEnabledCount,
        stats.temporal.temporalUpscalerInstanceExtensionMissingAvailableCount,
        stats.temporal.temporalUpscalerInstanceExtensionMissingEnabledCount,
        stats.temporal.temporalUpscalerInstanceExtensionMissingAvailable.c_str(),
        stats.temporal.temporalUpscalerInstanceExtensionMissingEnabled.c_str(),
        stats.temporal.temporalUpscalerDeviceExtensionAvailableCount,
        stats.temporal.temporalUpscalerDeviceExtensionRequirementCount,
        stats.temporal.temporalUpscalerDeviceExtensionEnabledCount,
        stats.temporal.temporalUpscalerDeviceExtensionMissingAvailableCount,
        stats.temporal.temporalUpscalerDeviceExtensionMissingEnabledCount,
        stats.temporal.temporalUpscalerDeviceExtensionMissingAvailable.c_str(),
        stats.temporal.temporalUpscalerDeviceExtensionMissingEnabled.c_str()
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
        "Local shadow budget: %u point / %u spot / %u rect, point faces %u, spot tiles %u, rect tiles %u, requested %u, assigned %u, dropped %u",
        localShadowAtlas.pointLightCount,
        localShadowAtlas.spotLightCount,
        localShadowAtlas.rectLightCount,
        localShadowAtlas.pointFaceTiles,
        localShadowAtlas.spotTiles,
        localShadowAtlas.rectTiles,
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
        "Local shadow cache misses: cold %u / layout %u / light %u / caster %u / skinned %u",
        localShadowAtlas.cacheColdTiles,
        localShadowAtlas.cacheTileLayoutChangedTiles,
        localShadowAtlas.cacheLightChangedTiles,
        localShadowAtlas.cacheCasterChangedTiles,
        localShadowAtlas.cacheDynamicSkinnedCasterTiles
    );
#if !defined(NDEBUG)
    if (!localShadowAtlas.cacheReasonSummary.empty()) {
        ImGui::TextWrapped(
            "Local shadow tile cache: %s",
            localShadowAtlas.cacheReasonSummary.c_str()
        );
    }
#endif
    ImGui::Text(
        "Local shadow filtering: bias %.5f / slope %.5f, PCF radius %.2f, kernel %ux%u, PCSS %.3f, face blend %.3f, rect bias scale %.2f",
        localShadowAtlas.biasMin,
        localShadowAtlas.biasSlope,
        localShadowAtlas.pcfRadius,
        localShadowAtlas.pcfKernelRadius * 2u + 1u,
        localShadowAtlas.pcfKernelRadius * 2u + 1u,
        localShadowAtlas.pcssStrength,
        localShadowAtlas.faceBlendStrength,
        localShadowAtlas.rectBiasScale
    );
    ImGui::Text(
        "Local point filter: bias %.5f / slope %.5f, PCF %.2f, kernel %ux%u, PCSS %.3f",
        localShadowAtlas.pointBiasMin,
        localShadowAtlas.pointBiasSlope,
        localShadowAtlas.pointPcfRadius,
        localShadowAtlas.pointPcfKernelRadius * 2u + 1u,
        localShadowAtlas.pointPcfKernelRadius * 2u + 1u,
        localShadowAtlas.pointPcssStrength
    );
    ImGui::Text(
        "Local spot filter: bias %.5f / slope %.5f, PCF %.2f, kernel %ux%u, PCSS %.3f",
        localShadowAtlas.spotBiasMin,
        localShadowAtlas.spotBiasSlope,
        localShadowAtlas.spotPcfRadius,
        localShadowAtlas.spotPcfKernelRadius * 2u + 1u,
        localShadowAtlas.spotPcfKernelRadius * 2u + 1u,
        localShadowAtlas.spotPcssStrength
    );
    ImGui::Text(
        "Local rect filter: bias %.5f / slope %.5f, PCF %.2f, kernel %ux%u, PCSS %.3f",
        localShadowAtlas.rectBiasMin,
        localShadowAtlas.rectBiasSlope,
        localShadowAtlas.rectPcfRadius,
        localShadowAtlas.rectPcfKernelRadius * 2u + 1u,
        localShadowAtlas.rectPcfKernelRadius * 2u + 1u,
        localShadowAtlas.rectPcssStrength
    );
    ImGui::Text(
        "Local shadow debug: point %s / spot %s / rect %s, only light index %d",
        localShadowAtlas.pointShadowEnabled ? "on" : "off",
        localShadowAtlas.spotShadowEnabled ? "on" : "off",
        localShadowAtlas.rectShadowEnabled ? "on" : "off",
        localShadowAtlas.debugLightIndex
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
    VulkanShadowSettings* shadowSettings,
    u32 temporalAntialiasingMode,
    TemporalAntialiasingModeCallback temporalAntialiasingModeCallback
) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 620.0f), ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);

    ImGui::Begin("SelfEngine");
    ImGui::Text("Vulkan renderer");
    ImGui::Separator();
    ImGui::Text("Frame time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    DrawTemporalAntialiasingControls(
        temporalAntialiasingMode,
        temporalAntialiasingModeCallback
    );
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
        DrawShadowQuickViews(*renderDebugSettings);
        if (rendererStats != nullptr) {
            DrawShadowDiagnostics(*rendererStats);
        }
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

    if (rendererStats != nullptr && renderDebugSettings != nullptr && scene3D != nullptr) {
        DrawLightingDebugOverlay(*rendererStats, *renderDebugSettings, *scene3D);
        DrawShadowDebugOverlay(
            *rendererStats,
            *renderDebugSettings,
            shadowSettings,
            *scene3D
        );
    }
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
