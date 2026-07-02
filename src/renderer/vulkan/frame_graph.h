#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

enum class RenderFramePassStatus {
    Active,
    Roadmap
};

enum class RenderFramePassQueue {
    Graphics,
    Compute,
    AsyncCompute,
    Transfer,
    Present
};

enum class RenderFramePassKind {
    FrameSetup,
    Visibility,
    DepthPrepass,
    VirtualGeometry,
    Shadow,
    GBuffer,
    DeferredLighting,
    ScreenSpaceAmbientOcclusion,
    Forward,
    GlobalIllumination,
    Reflections,
    Volumetrics,
    PostProcess,
    TemporalUpscale,
    UserInterface,
    Present
};

enum class RenderGraphResourceStatus {
    Physical,
    Planned
};

enum class RenderGraphResourceLifetime {
    Swapchain,
    PerFrame,
    PersistentHistory,
    PersistentCache
};

struct RenderGraphResource {
    u32 id = 0;
    RenderGraphResourceStatus status = RenderGraphResourceStatus::Planned;
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::PerFrame;
    std::string_view name;
    std::string_view format;
    std::string_view usage;
    std::string_view scale;
};

struct RenderFramePass {
    u32 id = 0;
    RenderFramePassKind kind = RenderFramePassKind::FrameSetup;
    RenderFramePassStatus status = RenderFramePassStatus::Roadmap;
    RenderFramePassQueue queue = RenderFramePassQueue::Graphics;
    std::string_view name;
    std::string_view reads;
    std::string_view writes;
    std::string_view purpose;
};

struct RenderFrameGraphValidation {
    u32 issueCount = 0;
    u32 unnamedPassCount = 0;
    u32 duplicatePassIdCount = 0;
    u32 unnamedResourceCount = 0;
    u32 duplicateResourceIdCount = 0;
};

struct RenderFrameGraphPlan {
    std::string_view name;
    std::string_view target;
    std::vector<RenderFramePass> passes;
    std::vector<RenderGraphResource> resources;
    RenderFrameGraphValidation validation;
    u32 activePassCount = 0;
    u32 roadmapPassCount = 0;
    u32 physicalResourceCount = 0;
    u32 plannedResourceCount = 0;
};

using RenderFrameGraphAppendCallback = void (*)(
    RenderFrameGraphPlan& plan,
    RenderFramePassKind stage,
    const void* userData
);

struct CurrentVulkanFrameGraphInputs {
    bool shadowPassEnabled = false;
    bool overlayPassEnabled = false;
    bool imguiPassEnabled = true;
    bool has3DMainPass = false;
    bool usesLegacyForwardMain = true;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    u32 swapchainImageCount = 0;
    u32 shadowMapSize = 0;
    u32 directionalShadowAtlasWidth = 0;
    u32 directionalShadowAtlasHeight = 0;
    u32 directionalShadowAtlasTileSize = 0;
    u32 directionalShadowAtlasCapacity = 0;
    u32 localShadowAtlasWidth = 0;
    u32 localShadowAtlasHeight = 0;
    u32 localShadowAtlasTileSize = 0;
    u32 localShadowAtlasCapacity = 0;
    u32 localShadowAtlasAssignedTiles = 0;
    u32 directionalShadowAtlasPasses = 0;
    u32 directionalShadowCascadeCount = 0;
    bool directionalShadowCascadeScaffoldEnabled = false;
    bool hdrSceneColorAllocated = false;
    VkFormat hdrSceneColorFormat = VK_FORMAT_UNDEFINED;
    bool hdrRenderPassAllocated = false;
    bool deferredLightingEnabled = false;
    RenderFrameGraphAppendCallback appendRenderFeatures = nullptr;
    const void* appendRenderFeaturesUserData = nullptr;
    bool lightTileCullComputeEnabled = false;
    bool gBufferDebugEnabled = false;
    bool weightedTranslucencyTargetsAllocated = false;
    VkFormat weightedTranslucencyAccumFormat = VK_FORMAT_UNDEFINED;
    VkFormat weightedTranslucencyRevealageFormat = VK_FORMAT_UNDEFINED;
    bool weightedTranslucencyRenderPassAllocated = false;
    u32 weightedTranslucencyFramebufferCount = 0;
    bool deferredTargetsAllocated = false;
    VkFormat sceneDepthFormat = VK_FORMAT_UNDEFINED;
    VkFormat velocityFormat = VK_FORMAT_UNDEFINED;
    VkFormat gBufferAlbedoFormat = VK_FORMAT_UNDEFINED;
    VkFormat gBufferNormalRoughnessFormat = VK_FORMAT_UNDEFINED;
    VkFormat gBufferMaterialFormat = VK_FORMAT_UNDEFINED;
    VkFormat gBufferEmissiveFormat = VK_FORMAT_UNDEFINED;
    bool gBufferRenderPassAllocated = false;
    bool gBufferGeometryEnabled = false;
};

std::string_view RenderFramePassStatusName(RenderFramePassStatus status);
std::string_view RenderFramePassQueueName(RenderFramePassQueue queue);
std::string_view RenderGraphResourceStatusName(RenderGraphResourceStatus status);
std::string_view RenderGraphResourceLifetimeName(RenderGraphResourceLifetime lifetime);

RenderFrameGraphPlan BuildCurrentVulkanFrameGraphPlan(
    CurrentVulkanFrameGraphInputs inputs
);
RenderFrameGraphPlan BuildAAAFrameGraphBlueprint();
void AppendRenderFrameGraphPass(
    RenderFrameGraphPlan& plan,
    RenderFramePassKind kind,
    RenderFramePassStatus status,
    RenderFramePassQueue queue,
    std::string_view name,
    std::string_view reads,
    std::string_view writes,
    std::string_view purpose
);

}
