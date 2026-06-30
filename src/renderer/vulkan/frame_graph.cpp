#include "renderer/vulkan/frame_graph.h"

namespace se {

namespace {

void AppendPass(
    RenderFrameGraphPlan& plan,
    RenderFramePassKind kind,
    RenderFramePassStatus status,
    RenderFramePassQueue queue,
    std::string_view name,
    std::string_view reads,
    std::string_view writes,
    std::string_view purpose
) {
    plan.passes.push_back(RenderFramePass{
        kind,
        status,
        queue,
        name,
        reads,
        writes,
        purpose
    });

    if (status == RenderFramePassStatus::Active) {
        ++plan.activePassCount;
    } else {
        ++plan.roadmapPassCount;
    }
}

void AppendResource(
    RenderFrameGraphPlan& plan,
    RenderGraphResourceStatus status,
    RenderGraphResourceLifetime lifetime,
    std::string_view name,
    std::string_view format,
    std::string_view usage,
    std::string_view scale
) {
    plan.resources.push_back(RenderGraphResource{
        status,
        lifetime,
        name,
        format,
        usage,
        scale
    });

    if (status == RenderGraphResourceStatus::Physical) {
        ++plan.physicalResourceCount;
    } else {
        ++plan.plannedResourceCount;
    }
}

std::string_view VulkanFormatName(VkFormat format) {
    switch (format) {
    case VK_FORMAT_UNDEFINED:
        return "undefined";
    case VK_FORMAT_B8G8R8A8_SRGB:
        return "B8G8R8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:
        return "R8G8B8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "R16G16B16A16_SFLOAT";
    case VK_FORMAT_R16_SFLOAT:
        return "R16_SFLOAT";
    case VK_FORMAT_R16G16_SFLOAT:
        return "R16G16_SFLOAT";
    case VK_FORMAT_R16G16B16A16_UNORM:
        return "R16G16B16A16_UNORM";
    case VK_FORMAT_R32_UINT:
        return "R32_UINT";
    case VK_FORMAT_D32_SFLOAT:
        return "D32_SFLOAT";
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return "D32_SFLOAT_S8_UINT";
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT";
    default:
        return "VkFormat";
    }
}

void AppendAAARoadmapPasses(RenderFrameGraphPlan& plan) {
    AppendPass(
        plan,
        RenderFramePassKind::FrameSetup,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "FrameSetup",
        "camera, scene, settings",
        "frame constants, jitter, history ids",
        "Own per-frame state before any render pass records work."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Visibility,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Compute,
        "GPUVisibility",
        "scene bounds, previous depth",
        "visible instances, compact draw lists, Hi-Z",
        "Move large-scene culling and draw preparation toward GPU-driven rendering."
    );
    AppendPass(
        plan,
        RenderFramePassKind::VirtualGeometry,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Compute,
        "VirtualGeometryClusters",
        "cluster hierarchy, streaming cache",
        "visibility buffer, material ranges",
        "Nanite-like cluster LOD, culling, streaming, and visibility output."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Shadow,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "VirtualShadowMaps",
        "visible lights, page cache, casters",
        "shadow page atlas, page tables",
        "VSM clipmaps and local-light pages with cache invalidation."
    );
    AppendPass(
        plan,
        RenderFramePassKind::DepthPrepass,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "DepthVelocity",
        "opaque geometry, previous transforms",
        "depth, velocity, hierarchical depth",
        "Feed TAA/TSR, occlusion, motion blur, and screen-space tracing."
    );
    AppendPass(
        plan,
        RenderFramePassKind::GBuffer,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "GBuffer",
        "opaque visible geometry, material buffers",
        "albedo, normal, ORM, emissive, material id",
        "Default opaque path for deferred PBR lighting."
    );
    AppendPass(
        plan,
        RenderFramePassKind::DeferredLighting,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "DeferredLighting",
        "GBuffer, clustered lights, shadows, probes",
        "HDR scene color",
        "Direct PBR lighting with tiled or clustered light lists."
    );
    AppendPass(
        plan,
        RenderFramePassKind::GlobalIllumination,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::AsyncCompute,
        "DynamicGI",
        "surface cache, screen traces, SDF/BVH",
        "diffuse indirect, radiance history",
        "Lumen-like software and hardware trace paths with temporal denoising."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Reflections,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::AsyncCompute,
        "Reflections",
        "scene color, depth, normal, surface cache",
        "specular indirect, reflection history",
        "Screen, probe, software trace, and hardware ray tracing reflection tiers."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Forward,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "ForwardPlus",
        "HDR color, depth, clustered lights",
        "HDR scene color",
        "Transparent, special, particle, black-hole, and unsupported deferred materials."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Volumetrics,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "VolumetricsAtmosphere",
        "lights, shadows, atmosphere, cloud media",
        "HDR scene color, aerial perspective",
        "Fog, clouds, light shafts, and volumetric shadows."
    );
    AppendPass(
        plan,
        RenderFramePassKind::PostProcess,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "PostProcess",
        "HDR scene color, exposure",
        "tonemapped color",
        "Auto exposure, bloom, color grading, DOF, motion blur, and lens effects."
    );
    AppendPass(
        plan,
        RenderFramePassKind::TemporalUpscale,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::AsyncCompute,
        "TemporalUpscale",
        "color, depth, velocity, history, exposure",
        "display-resolution color, updated history",
        "TSR/FSR-style dynamic-resolution upscaling and anti-aliasing."
    );
}

void AppendAAAResourceBlueprint(
    RenderFrameGraphPlan& plan,
    bool includeHdrSceneColor = true,
    bool includeDeferredTargets = true,
    bool includeWeightedTranslucencyTargets = true
) {
    if (includeHdrSceneColor) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "HDRSceneColor",
            "R16G16B16A16_SFLOAT",
            "color attachment, sampled, storage",
            "dynamic internal resolution"
        );
    }
    if (includeDeferredTargets) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "SceneDepth",
            "D32_SFLOAT",
            "depth attachment, sampled, Hi-Z source",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "Velocity",
            "R16G16_SFLOAT",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferAlbedo",
            "R8G8B8A8_SRGB",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferNormalRoughness",
            "R16G16B16A16_SFLOAT",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferMaterial",
            "R8G8B8A8_UNORM",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferEmissive",
            "R16G16B16A16_SFLOAT",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
    }
    if (includeWeightedTranslucencyTargets) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "WeightedTranslucencyAccum",
            "R16G16B16A16_SFLOAT",
            "color attachment, sampled, storage",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "WeightedTranslucencyRevealage",
            "R16_SFLOAT",
            "color attachment, sampled, storage",
            "dynamic internal resolution"
        );
    }
    AppendResource(
        plan,
        RenderGraphResourceStatus::Planned,
        RenderGraphResourceLifetime::PersistentHistory,
        "TemporalHistory",
        "R16G16B16A16_SFLOAT",
        "sampled, storage",
        "display resolution"
    );
    AppendResource(
        plan,
        RenderGraphResourceStatus::Planned,
        RenderGraphResourceLifetime::PersistentCache,
        "VirtualShadowPhysicalPages",
        "D32_SFLOAT",
        "depth attachment, sampled",
        "page atlas"
    );
    AppendResource(
        plan,
        RenderGraphResourceStatus::Planned,
        RenderGraphResourceLifetime::PersistentCache,
        "SurfaceCacheCards",
        "runtime selected",
        "sampled, storage",
        "card atlas"
    );
    AppendResource(
        plan,
        RenderGraphResourceStatus::Planned,
        RenderGraphResourceLifetime::PersistentCache,
        "VirtualGeometryClusters",
        "structured buffers",
        "storage, indirect args",
        "streaming cache"
    );
}

} // namespace

std::string_view RenderFramePassStatusName(RenderFramePassStatus status) {
    switch (status) {
    case RenderFramePassStatus::Active:
        return "active";
    case RenderFramePassStatus::Roadmap:
        return "roadmap";
    }

    return "unknown";
}

std::string_view RenderFramePassQueueName(RenderFramePassQueue queue) {
    switch (queue) {
    case RenderFramePassQueue::Graphics:
        return "graphics";
    case RenderFramePassQueue::Compute:
        return "compute";
    case RenderFramePassQueue::AsyncCompute:
        return "async compute";
    case RenderFramePassQueue::Transfer:
        return "transfer";
    case RenderFramePassQueue::Present:
        return "present";
    }

    return "unknown";
}

std::string_view RenderGraphResourceStatusName(RenderGraphResourceStatus status) {
    switch (status) {
    case RenderGraphResourceStatus::Physical:
        return "physical";
    case RenderGraphResourceStatus::Planned:
        return "planned";
    }

    return "unknown";
}

std::string_view RenderGraphResourceLifetimeName(RenderGraphResourceLifetime lifetime) {
    switch (lifetime) {
    case RenderGraphResourceLifetime::Swapchain:
        return "swapchain";
    case RenderGraphResourceLifetime::PerFrame:
        return "per frame";
    case RenderGraphResourceLifetime::PersistentHistory:
        return "history";
    case RenderGraphResourceLifetime::PersistentCache:
        return "cache";
    }

    return "unknown";
}

RenderFrameGraphPlan BuildCurrentVulkanFrameGraphPlan(
    CurrentVulkanFrameGraphInputs inputs
) {
    RenderFrameGraphPlan plan{};
    plan.name = "SelfEngine Hybrid Renderer";
    plan.target = "UE5-class AAA frame graph";

    AppendResource(
        plan,
        RenderGraphResourceStatus::Physical,
        RenderGraphResourceLifetime::Swapchain,
        "SwapchainColor",
        VulkanFormatName(inputs.swapchainFormat),
        "present color attachment",
        inputs.extent.width > 0 && inputs.extent.height > 0
            ? "window extent"
            : "unknown extent"
    );
    AppendResource(
        plan,
        RenderGraphResourceStatus::Physical,
        RenderGraphResourceLifetime::PerFrame,
        "LegacyDepth",
        VulkanFormatName(inputs.depthFormat),
        "depth attachment",
        "window extent"
    );
    if (inputs.shadowMapSize > 0) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "LegacyShadowMap",
            "D32_SFLOAT",
            "sampled depth attachment",
            "fixed square"
        );
    }
    if (inputs.directionalShadowAtlasWidth > 0 &&
        inputs.directionalShadowAtlasHeight > 0 &&
        inputs.directionalShadowAtlasTileSize > 0) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "DirectionalShadowCascades",
            "depth atlas",
            "sampled depth attachment, cascade tiles",
            "shadow tile grid"
        );
    } else if (inputs.directionalShadowCascadeScaffoldEnabled &&
        inputs.directionalShadowCascadeCount > 0) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "DirectionalShadowCascades",
            "CPU matrices + split depths",
            "shadow cascade selection metadata",
            "camera split count"
        );
    }
    if (inputs.localShadowAtlasWidth > 0 &&
        inputs.localShadowAtlasHeight > 0 &&
        inputs.localShadowAtlasTileSize > 0) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "LocalShadowAtlas",
            "depth atlas",
            "sampled depth attachment, point/spot light tiles",
            "shadow tile grid"
        );
        AppendPass(
            plan,
            RenderFramePassKind::Shadow,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "LocalShadowAtlasBudget",
            "local light list, atlas budget",
            inputs.localShadowAtlasAssignedTiles > 0
                ? "local shadow tile assignments"
                : "local shadow atlas capacity",
            "Physical atlas resource and occupancy diagnostics for upcoming point/spot shadow rendering."
        );
    }
    if (inputs.hdrSceneColorAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "HDRSceneColor",
            VulkanFormatName(inputs.hdrSceneColorFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
    }
    if (inputs.weightedTranslucencyTargetsAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "WeightedTranslucencyAccum",
            VulkanFormatName(inputs.weightedTranslucencyAccumFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "WeightedTranslucencyRevealage",
            VulkanFormatName(inputs.weightedTranslucencyRevealageFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
    }
    if (inputs.deferredTargetsAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "SceneDepth",
            VulkanFormatName(inputs.sceneDepthFormat),
            "depth attachment, sampled, Hi-Z source",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "Velocity",
            VulkanFormatName(inputs.velocityFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferAlbedo",
            VulkanFormatName(inputs.gBufferAlbedoFormat),
            "color attachment, sampled",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferNormalRoughness",
            VulkanFormatName(inputs.gBufferNormalRoughnessFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferMaterial",
            VulkanFormatName(inputs.gBufferMaterialFormat),
            "color attachment, sampled",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferEmissive",
            VulkanFormatName(inputs.gBufferEmissiveFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
    }
    if (inputs.gBufferRenderPassAllocated) {
        AppendPass(
            plan,
            RenderFramePassKind::GBuffer,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            inputs.gBufferGeometryEnabled ? "GBufferOpaque" : "GBufferTarget",
            inputs.gBufferGeometryEnabled
                ? "opaque render queue, frame UBO, material descriptors"
                : "frame graph setup",
            "SceneDepth, Velocity, GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive",
            inputs.gBufferGeometryEnabled
                ? "Writes the first deferred opaque material data while legacy forward still owns the visible image."
                : "Recorded clear-only GBuffer pass; opaque geometry migration follows."
        );
    }
    if (inputs.lightTileCullComputeEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::DeferredLighting,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Compute,
            "LightTileCull",
            "frame matrices, local light records",
            "tile light lists",
            "First compute-backed tiled light-list write feeding deferred lighting."
        );
    }
    if (inputs.ssaoEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::ScreenSpaceAmbientOcclusion,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "SSAOIntegrated",
            "GBufferNormalRoughness, SceneDepth, frame matrices",
            "deferred ambient occlusion factor",
            "First screen-space ambient occlusion tier integrated into deferred ambient with a debug view; a standalone AO target and temporal filter follow later."
        );
    }
    if (inputs.ssrEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::Reflections,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "SSRIntegrated",
            "GBufferNormalRoughness, SceneDepth, frame matrices",
            "deferred reflection radiance and screen-space hit confidence",
            "First screen-space reflection color tier integrated into deferred environment specular with a debug view; hierarchy, temporal accumulation, denoising, and probe fallback follow later."
        );
    }
    if (inputs.reflectionProbeFallbackEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::Reflections,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "GlobalReflectionFallback",
            "frame reflection-probe fallback controls, sky directional basis",
            "diffuse and specular fallback radiance",
            "Procedural global reflection fallback used by deferred, forward, and WBOIT lighting until imported UE reflection captures and local probes are available."
        );
    }
    if (inputs.localReflectionProbeEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::Reflections,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "LocalReflectionProbeBlend",
            "frame local reflection probe center/radius/color controls",
            "world-position weighted reflection fallback blend",
            "First local reflection-probe influence volume blended into deferred, forward, and WBOIT environment lighting before real cubemap capture is added."
        );
    }
    if (inputs.heightFogEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::Volumetrics,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "HeightFogIntegrated",
            "frame height-fog controls, camera position, shaded world position",
            "fogged HDR/forward scene color",
            "First analytic height/distance fog tier integrated into deferred, legacy forward, and WBOIT shading before a full volumetric fog volume is added."
        );
    }
    if (inputs.weightedTranslucencyRenderPassAllocated &&
        inputs.weightedTranslucencyFramebufferCount > 0) {
        AppendPass(
            plan,
            RenderFramePassKind::Forward,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "WeightedTranslucencyForwardPlus",
            "transparent residual queue, compute-written frame light lists, shadow metadata",
            "WeightedTranslucencyAccum, WeightedTranslucencyRevealage",
            "Clears and writes weighted blended translucency accum/revealage targets after tiled light culling, then resolves them into HDR scene color."
        );
    }
    if (inputs.hdrRenderPassAllocated) {
        AppendPass(
            plan,
            inputs.deferredLightingEnabled
                ? RenderFramePassKind::DeferredLighting
                : RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            inputs.deferredLightingEnabled
                ? "DeferredLighting"
                : "HdrOffscreenTarget",
            inputs.deferredLightingEnabled
                ? "GBuffer, SceneDepth, frame constants"
                : "frame graph setup",
            "HDRSceneColor",
            inputs.deferredLightingEnabled
                ? "Fullscreen lighting pass consumes the first GBuffer and writes HDR scene color while legacy forward remains the visible reference."
                : "Recorded offscreen HDR clear pass; scene rendering migrates into this target in the next slice."
        );
    }

    if (inputs.hdrCompositeEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "HDRComposite",
            "HDRSceneColor, exposure",
            "swapchain color",
            "Debug-visible HDR composite path for deferred output before it becomes the default present path."
        );
    }

    if (inputs.gBufferDebugEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "GBufferDebug",
            "GBuffer, GBufferEmissive, SceneDepth, Velocity, ShadowMap, frame constants",
            "swapchain color",
            "Debug visualizer for deferred attachments and reconstructed deferred shadow visibility."
        );
    }

    if (inputs.shadowPassEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::Shadow,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "LegacyShadowDepth",
            "shadow caster queue, frame matrices",
            "single shadow depth map",
            "Current directional shadow-map path kept as the fallback tier."
        );
    }
    if (inputs.directionalShadowCascadeScaffoldEnabled &&
        inputs.directionalShadowCascadeCount > 1) {
        AppendPass(
            plan,
            RenderFramePassKind::Shadow,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "DirectionalCSMScaffold",
            "camera frustum, directional light, caster bounds, cascade atlas",
            inputs.directionalShadowAtlasPasses > 0
                ? "cascade split depths, light matrices, atlas depth tiles"
                : "cascade split depths, light matrices, texel metrics",
            inputs.directionalShadowAtlasPasses > 0
                ? "Records one shadow-depth tile pass per active directional cascade while the single shadow map remains the sampled fallback."
                : "CSM split and stable texel diagnostics feeding the current single-map fallback."
        );
    }

    AppendPass(
        plan,
        inputs.usesLegacyForwardMain
            ? RenderFramePassKind::Forward
            : RenderFramePassKind::GBuffer,
        RenderFramePassStatus::Active,
        RenderFramePassQueue::Graphics,
        inputs.has3DMainPass ? "LegacyForward3D" : "Legacy2D",
        "render queue, frame UBO, material descriptors",
        "swapchain color, depth",
        "Current compatibility path while HDR/deferred targets are introduced."
    );

    if (inputs.overlayPassEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::Forward,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "OverlayForward3D",
            "overlay queue, overlay camera UBO",
            "swapchain color, depth",
            "Current secondary 3D path used by the black-hole demo."
        );
    }

    if (inputs.imguiPassEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::UserInterface,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "ImGui",
            "debug UI draw data",
            "swapchain color",
            "Runtime controls, pass visibility, and performance diagnostics."
        );
    }

    AppendPass(
        plan,
        RenderFramePassKind::Present,
        RenderFramePassStatus::Active,
        RenderFramePassQueue::Present,
        "Present",
        "swapchain color",
        "display",
        "Final swapchain present."
    );

    AppendAAARoadmapPasses(plan);
    AppendAAAResourceBlueprint(
        plan,
        !inputs.hdrSceneColorAllocated,
        !inputs.deferredTargetsAllocated,
        !inputs.weightedTranslucencyTargetsAllocated
    );
    return plan;
}

RenderFrameGraphPlan BuildAAAFrameGraphBlueprint() {
    RenderFrameGraphPlan plan{};
    plan.name = "SelfEngine AAA Blueprint";
    plan.target = "Mainstream AAA hybrid renderer";
    AppendAAARoadmapPasses(plan);
    AppendAAAResourceBlueprint(plan);
    AppendPass(
        plan,
        RenderFramePassKind::UserInterface,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "DebugUI",
        "visualizers, capture controls",
        "display color",
        "Editor and runtime inspection over the graph output."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Present,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Present,
        "Present",
        "display color",
        "display",
        "Presentation after UI composition."
    );
    return plan;
}

}
