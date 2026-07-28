#include "app/scene_builder_runtime_monitor.h"

#include "renderer/vulkan/material.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "renderer/vulkan/renderer_runtime_monitor.h"
#include "renderer/vulkan/renderer_stats.h"
#include "scene/camera_3d.h"
#include "scene/scene_3d.h"
#include "scene/scene_builder.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace se {
namespace {

using json = nlohmann::json;

json Vec3(const glm::vec3& value) {
    return { value.x, value.y, value.z };
}

json Vec4(const glm::vec4& value) {
    return { value.x, value.y, value.z, value.w };
}

json Matrix(const glm::mat4& value) {
    json result = json::array();
    for (u32 column = 0; column < 4; ++column) {
        result.push_back({
            value[column].x,
            value[column].y,
            value[column].z,
            value[column].w
        });
    }
    return result;
}

json SerializeRendererSession(
    const RendererMonitorSessionSnapshot& session
) {
    return {
        { "deviceName", session.deviceName },
        { "buildConfiguration", session.buildConfiguration },
        { "vendorId", session.vendorId },
        { "deviceId", session.deviceId },
        { "driverVersion", session.driverVersion },
        { "vulkanApiVersion", session.apiVersion },
        { "graphicsQueueFamily", session.graphicsQueueFamily },
        { "presentQueueFamily", session.presentQueueFamily },
        { "rayQueryHardwareReady", session.rayQueryHardwareReady },
        { "accelerationStructureSupported",
            session.accelerationStructureSupported },
        { "rayTracingPipelineSupported",
            session.rayTracingPipelineSupported }
    };
}

json SerializeRendererFrameContext(
    const RendererMonitorFrameContext& frame
) {
    return {
        { "swapchainImageIndex", frame.swapchainImageIndex },
        { "displayWidth", frame.displayWidth },
        { "displayHeight", frame.displayHeight },
        { "renderWidth", frame.renderWidth },
        { "renderHeight", frame.renderHeight },
        { "swapchainFormat", frame.swapchainFormat },
        { "antialiasingMode", frame.antialiasingMode },
        { "antialiasingModeId", frame.antialiasingModeId },
        { "instantaneousFps", frame.instantaneousFps },
        { "matricesAvailable", frame.matricesAvailable },
        { "view", Matrix(frame.view) },
        { "projection", Matrix(frame.projection) }
    };
}

json SerializeRendererQueues(
    const std::vector<RendererMonitorQueueSnapshot>& queues
) {
    json result = json::array();
    for (const RendererMonitorQueueSnapshot& queue : queues) {
        json commands = json::array();
        for (const RendererMonitorCommandSnapshot& command : queue.commands) {
            commands.push_back({
                { "renderIdentity", command.renderIdentity },
                { "debugName", command.debugName },
                { "submissionIndex", command.submissionIndex },
                { "vertexCount", command.vertexCount },
                { "indexCount", command.indexCount },
                { "triangleCount", command.triangleCount },
                { "vertexStride", command.vertexStride },
                { "drawOrder", command.drawOrder },
                { "lodLevel", command.lodLevel },
                { "lodScreenFraction", command.lodScreenFraction },
                { "castShadow", command.castShadow },
                { "reflectionCaptureVisible", command.reflectionCaptureVisible },
                { "boundsValid", command.boundsValid },
                { "boundsMin", Vec3(command.boundsMin) },
                { "boundsMax", Vec3(command.boundsMax) },
                { "gpuOcclusionCandidateIndex",
                    command.gpuOcclusionCandidateIndex },
                { "reflectionProbeAssignmentCode",
                    command.reflectionProbeAssignmentCode },
                { "reflectionProbeSceneIndex", command.reflectionProbeSceneIndex },
                { "bonePaletteResourceId", command.bonePaletteResourceId },
                { "bonePaletteReady", command.bonePaletteReady },
                { "bonePaletteDescriptorReady",
                    command.bonePaletteDescriptorReady },
                { "bonePaletteRevision", command.bonePaletteRevision }
            });
        }
        result.push_back({
            { "name", queue.name },
            { "commandCount", queue.commands.size() },
            { "submittedTriangles", queue.submittedTriangles },
            { "commands", std::move(commands) }
        });
    }
    return result;
}

json SerializeRendererResources(
    const std::vector<RendererMonitorResourceSnapshot>& resources
) {
    json result = json::array();
    for (const RendererMonitorResourceSnapshot& resource : resources) {
        result.push_back({
            { "id", resource.id },
            { "name", resource.name },
            { "status", resource.status },
            { "lifetime", resource.lifetime },
            { "format", resource.format },
            { "usage", resource.usage },
            { "scale", resource.scale },
            { "firstUsePassId", resource.firstUsePassId },
            { "lastUsePassId", resource.lastUsePassId },
            { "readCount", resource.readCount },
            { "writeCount", resource.writeCount }
        });
    }
    return result;
}

json SerializeRendererPasses(
    const std::vector<RendererMonitorPassExecutionSnapshot>& passes
) {
    json result = json::array();
    for (const RendererMonitorPassExecutionSnapshot& pass : passes) {
        result.push_back({
            { "id", pass.id },
            { "name", pass.name },
            { "queue", pass.queue },
            { "status", pass.status },
            { "kind", pass.kind },
            { "executionKnown", pass.executionKnown },
            { "drawCount", pass.drawCount },
            { "triangleCount", pass.triangleCount },
            { "dispatchCount", pass.dispatchCount },
            { "executionSource", pass.executionSource }
        });
    }
    return result;
}

std::string Text(std::string_view value) {
    return std::string(value);
}

std::string_view PrimitiveName(SceneBuilderPrimitive primitive) {
    return SceneBuilder::PrimitiveName(primitive);
}

std::string_view CaptureSourceName(ReflectionProbeCaptureSource source) {
    switch (source) {
    case ReflectionProbeCaptureSource::None:
        return "None";
    case ReflectionProbeCaptureSource::BuiltInProcedural:
        return "BuiltInProcedural";
    case ReflectionProbeCaptureSource::AuthoredCubemap:
        return "AuthoredCubemap";
    case ReflectionProbeCaptureSource::CapturedScene:
        return "CapturedScene";
    }
    return "Unknown";
}

std::string_view RefreshPolicyName(ReflectionProbeRefreshPolicy policy) {
    switch (policy) {
    case ReflectionProbeRefreshPolicy::Static:
        return "Static";
    case ReflectionProbeRefreshPolicy::FileSignature:
        return "FileSignature";
    case ReflectionProbeRefreshPolicy::Forced:
        return "Forced";
    case ReflectionProbeRefreshPolicy::SceneDirty:
        return "SceneDirty";
    }
    return "Unknown";
}

std::string_view LightingAssetName(SceneEnvironmentLightingAsset asset) {
    switch (asset) {
    case SceneEnvironmentLightingAsset::RendererDefault:
        return "RendererDefault";
    case SceneEnvironmentLightingAsset::StudioPanorama:
        return "StudioPanorama";
    }
    return "Unknown";
}

std::string_view GizmoModeName(u32 mode) {
    switch (mode) {
    case 0u:
        return "Translate";
    case 1u:
        return "Rotate";
    case 2u:
        return "Scale";
    default:
        return "Unknown";
    }
}

json SerializeMaterial(const MaterialProperties& material) {
    return {
        { "baseColorFactor", material.baseColorFactor },
        { "custom", material.custom },
        { "pbrFactors", material.pbrFactors },
        { "emissiveFactor", material.emissiveFactor },
        { "specularFactor", material.specularFactor },
        { "uvTransform", material.uvTransform },
        { "uvControls", material.uvControls },
        { "viewControls", material.viewControls },
        { "cameraControls", material.cameraControls },
        { "cameraPosition", material.cameraPosition },
        { "cameraDirection", material.cameraDirection },
        { "textureMix", material.textureMix },
        { "alphaCutoff", material.alphaCutoff },
        { "clearcoatFactor", material.clearcoatFactor },
        { "clearcoatRoughness", material.clearcoatRoughness },
        { "transmissionFactor", material.transmissionFactor },
        { "volumeThicknessFactor", material.volumeThicknessFactor },
        { "volumeAttenuationDistance", material.volumeAttenuationDistance },
        { "volumeAttenuationColor", material.volumeAttenuationColor },
        { "doubleSided", material.doubleSided },
        { "alphaMode", static_cast<u32>(material.alphaMode) },
        { "renderClass", static_cast<u32>(material.renderClass) }
    };
}

json SerializeBuilderStats(const SceneBuilderStats& stats) {
    return {
        { "available", stats.available },
        { "primitiveAvailabilityMask", stats.primitiveAvailabilityMask },
        { "objectCount", stats.objectCount },
        { "createdObjectCount", stats.createdObjectCount },
        { "destroyedObjectCount", stats.destroyedObjectCount },
        { "lightCount", stats.lightCount },
        { "createdLightCount", stats.createdLightCount },
        { "destroyedLightCount", stats.destroyedLightCount },
        { "liveMaterialCount", stats.liveMaterialCount },
        { "materialLibraryCount", stats.materialLibraryCount },
        { "frameMaterialBudget", stats.frameMaterialBudget },
        { "sceneRenderableCount", stats.sceneRenderableCount },
        { "selectedIdentity", stats.selectedIdentity },
        { "selectedLightIdentity", stats.selectedLightIdentity },
        { "selectedPrimitive", stats.selectedPrimitive },
        { "editRevision", stats.editRevision },
        { "transformEditCount", stats.transformEditCount },
        { "materialEditCount", stats.materialEditCount },
        { "lightEditCount", stats.lightEditCount },
        { "lightIconOverlayCount", stats.lightIconOverlayCount },
        { "lightIconHitTestCount", stats.lightIconHitTestCount },
        { "lightIconSelectionCount", stats.lightIconSelectionCount },
        { "renameCount", stats.renameCount },
        { "selectionSyncCount", stats.selectionSyncCount },
        { "selectionShortcutDeleteCount", stats.selectionShortcutDeleteCount },
        { "selectionRayQueryCount", stats.selectionRayQueryCount },
        { "selectionRayHitCount", stats.selectionRayHitCount },
        { "materialDescriptorRefreshCount", stats.materialDescriptorRefreshCount },
        { "lastCreateFailure", stats.lastCreateFailure },
        { "createFailureCount", stats.createFailureCount },
        { "environmentIblEnabled", stats.environmentIblEnabled },
        { "environmentSkyboxEnabled", stats.environmentSkyboxEnabled },
        { "environmentDiffuseIntensity", stats.environmentDiffuseIntensity },
        { "environmentSpecularIntensity", stats.environmentSpecularIntensity },
        { "environmentHorizonBlend", stats.environmentHorizonBlend },
        { "environmentSkyboxIntensity", stats.environmentSkyboxIntensity },
        { "environmentSkyboxBlur", stats.environmentSkyboxBlur },
        { "environmentLightingAsset", stats.environmentLightingAsset },
        { "reflectionProbeCount", stats.reflectionProbeCount },
        { "reflectionProbeEditCount", stats.reflectionProbeEditCount },
        { "reflectionProbeCapturedSceneCount", stats.reflectionProbeCapturedSceneCount },
        { "reflectionProbeStaticRefreshCount", stats.reflectionProbeStaticRefreshCount },
        { "reflectionProbeExcludedRenderableCount", stats.reflectionProbeExcludedRenderableCount },
        { "blendObjectCount", stats.blendObjectCount },
        { "selfTestRan", stats.selfTestRan },
        { "selfTestPassed", stats.selfTestPassed },
        { "selfTestFailedCheckMask", stats.selfTestFailedCheckMask },
        { "primitiveCounts", stats.primitiveCounts },
        { "lightCounts", stats.lightCounts }
    };
}

json SerializeSceneBuilderGizmo(
    const RendererSceneBuilderGizmoStats& stats
) {
    return {
        { "enabled", stats.enabled },
        { "mode", stats.mode },
        { "modeName", GizmoModeName(stats.mode) },
        { "shortcutModeSwitchCount", stats.shortcutModeSwitchCount },
        { "shortcutModeMask", stats.shortcutModeMask }
    };
}

json SerializeCpu(const RendererCpuStats& stats) {
    return {
        { "waitAcquireMs", stats.waitAcquireMs },
        { "imguiMs", stats.imguiMs },
        { "pickingMs", stats.pickingMs },
        { "queueBuildMs", stats.queueBuildMs },
        { "uniformUpdateMs", stats.uniformUpdateMs },
        { "commandRecordMs", stats.commandRecordMs },
        { "submitPresentMs", stats.submitPresentMs },
        { "totalFrameMs", stats.totalFrameMs }
    };
}

json SerializeDraw(const RendererDrawStats& stats) {
    return {
        { "mainDraws", stats.mainDraws },
        { "gBufferDraws", stats.gBufferDraws },
        { "overlayDraws", stats.overlayDraws },
        { "shadowDraws", stats.shadowDraws },
        { "transparentObjectsEnabled", stats.transparentObjectsEnabled },
        { "transparentObjectSkippedDraws", stats.transparentObjectSkippedDraws },
        { "transparentObjectMainSkippedDraws", stats.transparentObjectMainSkippedDraws },
        { "transparentObjectShadowSkippedDraws", stats.transparentObjectShadowSkippedDraws },
        { "transparentObjectReflectionSkippedDraws", stats.transparentObjectReflectionSkippedDraws },
        { "transparentObjectOverlaySkippedDraws", stats.transparentObjectOverlaySkippedDraws },
        { "hybridDeferredOpaqueDraws", stats.hybridDeferredOpaqueDraws },
        { "hybridForwardTransparentDraws", stats.hybridForwardTransparentDraws },
        { "hybridForwardSpecialDraws", stats.hybridForwardSpecialDraws },
        { "hybridWeightedTranslucencyDraws", stats.hybridWeightedTranslucencyDraws },
        { "hybridWeightedTranslucencySortOps", stats.hybridWeightedTranslucencySortOps },
        { "hybridForwardResidualDraws", stats.hybridForwardResidualDraws },
        { "hybridForwardResidualSortOps", stats.hybridForwardResidualSortOps },
        { "mainTriangles", stats.mainTriangles },
        { "gBufferTriangles", stats.gBufferTriangles },
        { "overlayTriangles", stats.overlayTriangles },
        { "shadowTriangles", stats.shadowTriangles },
        { "transparentObjectSkippedTriangles", stats.transparentObjectSkippedTriangles },
        { "hybridDeferredOpaqueTriangles", stats.hybridDeferredOpaqueTriangles },
        { "hybridWeightedTranslucencyTriangles", stats.hybridWeightedTranslucencyTriangles },
        { "hybridForwardResidualTriangles", stats.hybridForwardResidualTriangles },
        { "matrixRecalculations", stats.matrixRecalculations },
        { "mainVisible", stats.mainVisible },
        { "mainCulled", stats.mainCulled },
        { "overlayVisible", stats.overlayVisible },
        { "overlayCulled", stats.overlayCulled },
        { "shadowVisible", stats.shadowVisible },
        { "shadowCulled", stats.shadowCulled },
        { "mainBoundsCacheHits", stats.mainBoundsCacheHits },
        { "mainBoundsCacheMisses", stats.mainBoundsCacheMisses },
        { "mainCommandCacheHits", stats.mainCommandCacheHits },
        { "mainCommandCacheMisses", stats.mainCommandCacheMisses },
        { "mainVisibilityCacheHits", stats.mainVisibilityCacheHits },
        { "mainVisibilityCacheMisses", stats.mainVisibilityCacheMisses },
        { "mainQueueCacheHits", stats.mainQueueCacheHits },
        { "mainQueueCacheMisses", stats.mainQueueCacheMisses },
        { "mainInstancedDraws", stats.mainInstancedDraws },
        { "mainInstancedInstances", stats.mainInstancedInstances },
        { "mainInstanceBatchCacheHits", stats.mainInstanceBatchCacheHits },
        { "mainInstanceBatchCacheMisses", stats.mainInstanceBatchCacheMisses },
        { "mainSkinnedConservativeBounds", stats.mainSkinnedConservativeBounds },
        { "shadowSkinnedConservativeBounds", stats.shadowSkinnedConservativeBounds }
    };
}

json SerializeMeshLod(const RendererMeshLodStats& stats) {
    return {
        { "enabled", stats.enabled },
        { "eligibleCommands", stats.eligibleCommands },
        { "selectedCommands", stats.selectedCommands },
        { "reducedCommands", stats.reducedCommands },
        { "transitionCount", stats.transitionCount },
        { "skinnedExcludedCommands", stats.skinnedExcludedCommands },
        { "levelCounts", stats.levelCounts },
        { "sourceTriangles", stats.sourceTriangles },
        { "renderedTriangles", stats.renderedTriangles },
        { "savedTriangles", stats.savedTriangles },
        { "residentChainCount", stats.residentChainCount },
        { "residentLevelCount", stats.residentLevelCount },
        { "sourceVertexBytes", stats.sourceVertexBytes },
        { "sourceIndexBytes", stats.sourceIndexBytes },
        { "residentVertexBytes", stats.residentVertexBytes },
        { "residentIndexBytes", stats.residentIndexBytes },
        { "extraVertexBytes", stats.extraVertexBytes },
        { "extraIndexBytes", stats.extraIndexBytes },
        { "minScreenFraction", stats.minScreenFraction },
        { "maxScreenFraction", stats.maxScreenFraction },
        { "maxSelectedErrorPixels", stats.maxSelectedErrorPixels },
        { "targetPixelError", stats.targetPixelError }
    };
}

json SerializeOcclusion(const RendererGpuOcclusionStats& stats) {
    return {
        { "contractVersion", stats.contractVersion },
        { "requested", stats.requested },
        { "diagnosticsRequested", stats.diagnosticsRequested },
        { "active", stats.active },
        { "fallbackReason", stats.fallbackReason },
        { "actualDrawsUnchanged", stats.actualDrawsUnchanged },
        { "commandCount", stats.commandCount },
        { "validBoundsCount", stats.validBoundsCount },
        { "invalidBoundsCount", stats.invalidBoundsCount },
        { "zeroIdentityCount", stats.zeroIdentityCount },
        { "capacity", stats.capacity },
        { "capacityDroppedCount", stats.capacityDroppedCount },
        { "uploadedCandidateCount", stats.uploadedCandidateCount },
        { "uploadedCandidateBytes", stats.uploadedCandidateBytes },
        { "candidateIdentityHash", stats.candidateIdentityHash },
        { "candidateContentHash", stats.candidateContentHash },
        { "classificationJitterPixelsX", stats.classificationJitterPixelsX },
        { "classificationJitterPixelsY", stats.classificationJitterPixelsY },
        { "classificationJitterGuardPixels", stats.classificationJitterGuardPixels },
        { "depthPyramidAllocated", stats.depthPyramidAllocated },
        { "depthPyramidWidth", stats.depthPyramidWidth },
        { "depthPyramidHeight", stats.depthPyramidHeight },
        { "depthPyramidMipCount", stats.depthPyramidMipCount },
        { "depthPyramidImageCount", stats.depthPyramidImageCount },
        { "depthPyramidFormat", static_cast<i32>(stats.depthPyramidFormat) },
        { "depthPyramidMemoryBytes", stats.depthPyramidMemoryBytes },
        { "depthPyramidBuildDispatchCount", stats.depthPyramidBuildDispatchCount },
        { "classificationDispatchCount", stats.classificationDispatchCount },
        { "classificationGroupCount", stats.classificationGroupCount },
        { "readbackReady", stats.readbackReady },
        { "readbackValid", stats.readbackValid },
        { "readbackStale", stats.readbackStale },
        { "readbackInvalidCount", stats.readbackInvalidCount },
        { "readbackCandidateCount", stats.readbackCandidateCount },
        { "classifiedVisibleCount", stats.classifiedVisibleCount },
        { "classifiedOccludedCount", stats.classifiedOccludedCount },
        { "classifiedUncertainCount", stats.classifiedUncertainCount },
        { "cameraInsideExcludedCount", stats.cameraInsideExcludedCount },
        { "nearPlaneExcludedCount", stats.nearPlaneExcludedCount },
        { "invalidProjectionCount", stats.invalidProjectionCount },
        { "invalidRectCount", stats.invalidRectCount },
        { "invalidMipCount", stats.invalidMipCount },
        { "maxSelectedMip", stats.maxSelectedMip },
        { "sampledTexelCount", stats.sampledTexelCount },
        { "classificationConserved", stats.classificationConserved },
        { "historyValid", stats.historyValid },
        { "historyReset", stats.historyReset },
        { "historyResetReason", stats.historyResetReason },
        { "wouldCullDrawCount", stats.wouldCullDrawCount },
        { "wouldCullTriangleCount", stats.wouldCullTriangleCount },
        { "actualDrawCount", stats.actualDrawCount },
        { "actualTriangleCount", stats.actualTriangleCount },
        { "auditBufferMemoryBytes", stats.auditBufferMemoryBytes },
        { "indirectConsumerReady", stats.indirectConsumerReady },
        { "indirectConsumerActive", stats.indirectConsumerActive },
        { "indirectFallbackReason", stats.indirectFallbackReason },
        { "indirectSubmittedDrawCount", stats.indirectSubmittedDrawCount },
        { "indirectDirectFallbackDrawCount", stats.indirectDirectFallbackDrawCount }
    };
}

json SerializeShadowCascades(const RendererShadowCascadeStats& stats) {
    return {
        { "budgetContractVersion", stats.budgetContractVersion },
        { "budgetResourceContractValid", stats.budgetResourceContractValid },
        { "budgetFallbackReason", stats.budgetFallbackReason },
        { "budgetSwapchainImageCount", stats.budgetSwapchainImageCount },
        { "budgetGenerationMaxPasses", stats.budgetGenerationMaxPasses },
        { "budgetDirectionalReceiverSamples", stats.budgetDirectionalReceiverSamples },
        { "budgetPointProjectionSamples", stats.budgetPointProjectionSamples },
        { "budgetSpotProjectionSamples", stats.budgetSpotProjectionSamples },
        { "budgetRectProjectionSamples", stats.budgetRectProjectionSamples },
        { "budgetContactSamples", stats.budgetContactSamples },
        { "quality", stats.quality },
        { "configuredCount", stats.configuredCount },
        { "activeCount", stats.activeCount },
        { "directionalReceiveEnabled", stats.directionalReceiveEnabled },
        { "stableSnappingEnabled", stats.stableSnappingEnabled },
        { "requestedCoverageMode", stats.requestedCoverageMode },
        { "activeCoverageMode", stats.activeCoverageMode },
        { "cameraIndependent", stats.cameraIndependent },
        { "sceneBoundsValid", stats.sceneBoundsValid },
        { "coverageFallbackReason", stats.coverageFallbackReason },
        { "projectionHash", std::to_string(stats.projectionHash) },
        { "projectionRevision", stats.projectionRevision },
        { "atlasAllocated", stats.atlasAllocated },
        { "atlasTileSize", stats.atlasTileSize },
        { "atlasWidth", stats.atlasWidth },
        { "atlasHeight", stats.atlasHeight },
        { "atlasCascadeCapacity", stats.atlasCascadeCapacity },
        { "filterMode", stats.filterMode },
        { "filterSampleCount", stats.filterSampleCount },
        { "pcssEnabled", stats.pcssEnabled },
        { "pcssStrength", stats.pcssStrength },
        { "pcssSearchRadiusTexels", stats.pcssSearchRadiusTexels },
        { "pcssMaxPenumbraTexels", stats.pcssMaxPenumbraTexels },
        { "pcssLightAngularRadiusRadians", stats.pcssLightAngularRadiusRadians },
        { "receiverPlaneBiasEnabled", stats.receiverPlaneBiasEnabled },
        { "normalOffsetBiasEnabled", stats.normalOffsetBiasEnabled },
        { "slopeOffsetBiasEnabled", stats.slopeOffsetBiasEnabled },
        { "casterDepthBiasEnabled", stats.casterDepthBiasEnabled },
        { "splitLambda", stats.splitLambda },
        { "maxDistance", stats.maxDistance },
        { "blendRatio", stats.blendRatio },
        { "fadeRatio", stats.fadeRatio },
        { "contactShadowStrength", stats.contactShadowStrength },
        { "contactShadowLength", stats.contactShadowLength },
        { "contactShadowThickness", stats.contactShadowThickness },
        { "contactShadowSteps", stats.contactShadowSteps },
        { "splitDepths", stats.splitDepths },
        { "texelWorldSizes", stats.texelWorldSizes },
        { "lightDepthWorldSpans", stats.lightDepthWorldSpans }
    };
}

json SerializeLocalShadowAtlas(const RendererLocalShadowAtlasStats& stats) {
    return {
        { "allocated", stats.allocated },
        { "tileSize", stats.tileSize },
        { "atlasWidth", stats.atlasWidth },
        { "atlasHeight", stats.atlasHeight },
        { "tileCapacity", stats.tileCapacity },
        { "shadowableLocalLights", stats.shadowableLocalLights },
        { "pointLightCount", stats.pointLightCount },
        { "spotLightCount", stats.spotLightCount },
        { "rectLightCount", stats.rectLightCount },
        { "requestedTiles", stats.requestedTiles },
        { "assignedTiles", stats.assignedTiles },
        { "droppedTiles", stats.droppedTiles },
        { "recordedTilePasses", stats.recordedTilePasses },
        { "recordedDraws", stats.recordedDraws },
        { "cacheEligibleTiles", stats.cacheEligibleTiles },
        { "cacheHitTiles", stats.cacheHitTiles },
        { "cacheMissTiles", stats.cacheMissTiles },
        { "cacheReasonSummary", stats.cacheReasonSummary },
        { "productionFilterEnabled", stats.productionFilterEnabled },
        { "productionFilterReady", stats.productionFilterReady },
        { "productionFilterActive", stats.productionFilterActive },
        { "productionFilterFallbackReason", stats.productionFilterFallbackReason },
        { "pointShadowEnabled", stats.pointShadowEnabled },
        { "spotShadowEnabled", stats.spotShadowEnabled },
        { "rectShadowEnabled", stats.rectShadowEnabled },
        { "attributionLightIndex", stats.attributionLightIndex },
        { "attributionLightValid", stats.attributionLightValid },
        { "attributionLightKind", stats.attributionLightKind },
        { "attributionExpectedTiles", stats.attributionExpectedTiles },
        { "attributionAssignedTiles", stats.attributionAssignedTiles },
        { "attributionDroppedTiles", stats.attributionDroppedTiles },
        { "attributionRecordedDraws", stats.attributionRecordedDraws }
    };
}

json SerializeSsr(const RendererSsrStats& stats) {
    return {
        { "enabled", stats.enabled },
        { "colorResolveEnabled", stats.colorResolveEnabled },
        { "traceInputsReady", stats.traceInputsReady },
        { "hierarchicalRequested", stats.hierarchicalRequested },
        { "hierarchicalActive", stats.hierarchicalActive },
        { "hierarchicalFallbackReason", stats.hierarchicalFallbackReason },
        { "fixedStepFallbackActive", stats.fixedStepFallbackActive },
        { "depthPyramidAllocated", stats.depthPyramidAllocated },
        { "depthPyramidReady", stats.depthPyramidReady },
        { "depthPyramidWidth", stats.depthPyramidWidth },
        { "depthPyramidHeight", stats.depthPyramidHeight },
        { "depthPyramidMipCount", stats.depthPyramidMipCount },
        { "depthPyramidImageCount", stats.depthPyramidImageCount },
        { "depthPyramidFormat", static_cast<i32>(stats.depthPyramidFormat) },
        { "depthPyramidMemoryBytes", stats.depthPyramidMemoryBytes },
        { "depthPyramidBuildDispatchCount", stats.depthPyramidBuildDispatchCount },
        { "traversalMaxMip", stats.traversalMaxMip },
        { "refinementEnabled", stats.refinementEnabled },
        { "refinementStepCount", stats.refinementStepCount },
        { "hitValidationRequested", stats.hitValidationRequested },
        { "hitValidationActive", stats.hitValidationActive },
        { "reconstructionRequested", stats.reconstructionRequested },
        { "reconstructionActive", stats.reconstructionActive },
        { "reconstructionTargetsAllocated", stats.reconstructionTargetsAllocated },
        { "reconstructionDescriptorSetsReady", stats.reconstructionDescriptorSetsReady },
        { "reconstructionTraceDispatches", stats.reconstructionTraceDispatches },
        { "reconstructionTemporalDispatches", stats.reconstructionTemporalDispatches },
        { "reconstructionSpatialDispatches", stats.reconstructionSpatialDispatches },
        { "reconstructionHistoryCopies", stats.reconstructionHistoryCopies },
        { "reconstructionHistoryReset", stats.reconstructionHistoryReset },
        { "reconstructionHistoryDescriptorUpdated", stats.reconstructionHistoryDescriptorUpdated },
        { "reconstructionHistorySourceImageIndex", stats.reconstructionHistorySourceImageIndex },
        { "reconstructionHistorySourceMatchesSceneColorHistory", stats.reconstructionHistorySourceMatchesSceneColorHistory },
        { "fallbackBlendRequested", stats.fallbackBlendRequested },
        { "fallbackBlendActive", stats.fallbackBlendActive },
        { "reflectionProbeFallbackEnabled", stats.reflectionProbeFallbackEnabled },
        { "sceneColorHistoryRequested", stats.sceneColorHistoryRequested },
        { "sceneColorHistoryReady", stats.sceneColorHistoryReady },
        { "sceneColorHistoryActive", stats.sceneColorHistoryActive },
        { "sceneColorHistoryFallbackReason", stats.sceneColorHistoryFallbackReason },
        { "sceneColorHistorySourceValid", stats.sceneColorHistorySourceValid },
        { "sceneColorHistoryCurrentImageIndex", stats.sceneColorHistoryCurrentImageIndex },
        { "sceneColorHistorySourceImageIndex", stats.sceneColorHistorySourceImageIndex },
        { "sceneColorHistoryFrameAge", stats.sceneColorHistoryFrameAge },
        { "radianceSource", stats.radianceSource },
        { "backendRequestedProvider", stats.backendRequestedProvider },
        { "backendActiveProvider", stats.backendActiveProvider },
        { "fidelityFxSssrContractVersion", stats.fidelityFxSssrContractVersion },
        { "fidelityFxSssrSourceReady", stats.fidelityFxSssrSourceReady },
        { "fidelityFxSssrRuntimeDispatchReady", stats.fidelityFxSssrRuntimeDispatchReady },
        { "fidelityFxSssrRuntimeActive", stats.fidelityFxSssrRuntimeActive },
        { "fidelityFxSssrFallbackReason", stats.fidelityFxSssrFallbackReason },
        { "fidelityFxSssrClassifyTilesDispatches", stats.fidelityFxSssrClassifyTilesDispatches },
        { "fidelityFxSssrIntersectDispatches", stats.fidelityFxSssrIntersectDispatches },
        { "fidelityFxSssrReprojectDispatches", stats.fidelityFxSssrReprojectDispatches },
        { "fidelityFxSssrPrefilterDispatches", stats.fidelityFxSssrPrefilterDispatches },
        { "fidelityFxSssrResolveTemporalDispatches", stats.fidelityFxSssrResolveTemporalDispatches },
        { "fidelityFxSssrResolveTemporalHistoryCopies", stats.fidelityFxSssrResolveTemporalHistoryCopies },
        { "fidelityFxSssrReceiverHistoryUpdates", stats.fidelityFxSssrReceiverHistoryUpdates },
        { "fidelityFxSssrSameFrameCompositeActive", stats.fidelityFxSssrSameFrameCompositeActive },
        { "fidelityFxSssrExclusiveReflectionOwnerActive", stats.fidelityFxSssrExclusiveReflectionOwnerActive },
        { "fidelityFxSssrHitAttributionReadbackValid", stats.fidelityFxSssrHitAttributionReadbackValid },
        { "fidelityFxSssrHighConfidenceHitSamples", stats.fidelityFxSssrHighConfidenceHitSamples },
        { "fidelityFxSssrPartialHitSamples", stats.fidelityFxSssrPartialHitSamples },
        { "fidelityFxSssrEnvironmentFallbackSamples", stats.fidelityFxSssrEnvironmentFallbackSamples },
        { "strength", stats.strength },
        { "rayLength", stats.rayLength },
        { "thickness", stats.thickness },
        { "stepCount", stats.stepCount },
        { "holeDiagnosticsRequested", stats.holeDiagnosticsRequested },
        { "holeDiagnosticsActive", stats.holeDiagnosticsActive },
        { "holeDiagnosticsReadbackValid", stats.holeDiagnosticsReadbackValid },
        { "holeDiagnosticsPixelCount", stats.holeDiagnosticsPixelCount },
        { "holeDiagnosticsRawHitPixels", stats.holeDiagnosticsRawHitPixels },
        { "holeDiagnosticsRawHighConfidencePixels", stats.holeDiagnosticsRawHighConfidencePixels },
        { "holeDiagnosticsTemporalValidPixels", stats.holeDiagnosticsTemporalValidPixels },
        { "holeDiagnosticsResolvedValidPixels", stats.holeDiagnosticsResolvedValidPixels },
        { "holeDiagnosticsIsolatedRawHitPixels", stats.holeDiagnosticsIsolatedRawHitPixels },
        { "holeDiagnosticsCenterMissNeighborHitPixels", stats.holeDiagnosticsCenterMissNeighborHitPixels },
        { "holeDiagnosticsResolvedHolePixels", stats.holeDiagnosticsResolvedHolePixels },
        { "holeDiagnosticsRawHitTemporalRejectedPixels", stats.holeDiagnosticsRawHitTemporalRejectedPixels },
        { "holeDiagnosticsRawHitSpatialRejectedPixels", stats.holeDiagnosticsRawHitSpatialRejectedPixels },
        { "holeDiagnosticsTemporalMissCarriedPixels", stats.holeDiagnosticsTemporalMissCarriedPixels },
        { "holeDiagnosticsContractVersion", stats.holeDiagnosticsContractVersion }
    };
}

json SerializeHybridReflections(const RendererHybridReflectionStats& stats) {
    return {
        { "capabilityContractVersion", stats.capabilityContractVersion },
        { "accelerationStructureContractVersion", stats.accelerationStructureContractVersion },
        { "rayQueryConsumerContractVersion", stats.rayQueryConsumerContractVersion },
        { "rayQueryHitAttributeContractVersion", stats.rayQueryHitAttributeContractVersion },
        { "rayQueryMaterialTableContractVersion", stats.rayQueryMaterialTableContractVersion },
        { "rayQueryHitLightingContractVersion", stats.rayQueryHitLightingContractVersion },
        { "rayQueryShadowVisibilityContractVersion", stats.rayQueryShadowVisibilityContractVersion },
        { "rayQueryDenoiserBridgeContractVersion", stats.rayQueryDenoiserBridgeContractVersion },
        { "requested", stats.requested },
        { "controlDisabled", stats.controlDisabled },
        { "rayQueryConsumerRequested", stats.rayQueryConsumerRequested },
        { "bufferDeviceAddressExtensionSupported", stats.bufferDeviceAddressExtensionSupported },
        { "accelerationStructureExtensionSupported", stats.accelerationStructureExtensionSupported },
        { "rayQueryExtensionSupported", stats.rayQueryExtensionSupported },
        { "bufferDeviceAddressFeatureSupported", stats.bufferDeviceAddressFeatureSupported },
        { "accelerationStructureFeatureSupported", stats.accelerationStructureFeatureSupported },
        { "rayQueryFeatureSupported", stats.rayQueryFeatureSupported },
        { "rayQueryHardwareReady", stats.rayQueryHardwareReady },
        { "rayQueryDeviceEnabled", stats.rayQueryDeviceEnabled },
        { "fullSceneCommandCount", stats.fullSceneCommandCount },
        { "opaqueRigidCommandCount", stats.opaqueRigidCommandCount },
        { "skinnedFallbackCount", stats.skinnedFallbackCount },
        { "alphaFallbackCount", stats.alphaFallbackCount },
        { "invalidGeometryCount", stats.invalidGeometryCount },
        { "instanceOverflowCount", stats.instanceOverflowCount },
        { "blasCacheCount", stats.blasCacheCount },
        { "blasReadyCount", stats.blasReadyCount },
        { "blasBuildCount", stats.blasBuildCount },
        { "blasReuseCount", stats.blasReuseCount },
        { "blasStorageBytes", stats.blasStorageBytes },
        { "blasScratchBytes", stats.blasScratchBytes },
        { "tlasInstanceCount", stats.tlasInstanceCount },
        { "tlasInstanceCapacity", stats.tlasInstanceCapacity },
        { "tlasBuildCount", stats.tlasBuildCount },
        { "tlasUpdateCount", stats.tlasUpdateCount },
        { "tlasStorageBytes", stats.tlasStorageBytes },
        { "tlasScratchBytes", stats.tlasScratchBytes },
        { "tlasAddressReady", stats.tlasAddressReady },
        { "runtimeResourcesReady", stats.runtimeResourcesReady },
        { "rayQueryResourcesReady", stats.rayQueryResourcesReady },
        { "rayQueryTlasDescriptorReady", stats.rayQueryTlasDescriptorReady },
        { "rayQueryDispatchReady", stats.rayQueryDispatchReady },
        { "rayQueryDispatchCount", stats.rayQueryDispatchCount },
        { "rayQueryDescriptorBindCount", stats.rayQueryDescriptorBindCount },
        { "rayQueryResultWidth", stats.rayQueryResultWidth },
        { "rayQueryResultHeight", stats.rayQueryResultHeight },
        { "rayQueryMemoryBytes", stats.rayQueryMemoryBytes },
        { "rayQueryInstanceMetadataCount", stats.rayQueryInstanceMetadataCount },
        { "rayQueryMaterialTableCount", stats.rayQueryMaterialTableCount },
        { "rayQueryTextureDescriptorCount", stats.rayQueryTextureDescriptorCount },
        { "rayQuerySamplerDescriptorCount", stats.rayQuerySamplerDescriptorCount },
        { "rayQueryHitIblEnabled", stats.rayQueryHitIblEnabled },
        { "rayQueryHitIblDiffuseIntensityMilliunits", stats.rayQueryHitIblDiffuseIntensityMilliunits },
        { "rayQueryHitIblSpecularIntensityMilliunits", stats.rayQueryHitIblSpecularIntensityMilliunits },
        { "rayQueryGlobalIblEnabled", stats.rayQueryGlobalIblEnabled },
        { "rayQueryGlobalSpecularVisible", stats.rayQueryGlobalSpecularVisible },
        { "rayQueryCullBackFacingTriangles", stats.rayQueryCullBackFacingTriangles },
        { "rayQueryLocalProbeIblEnabled", stats.rayQueryLocalProbeIblEnabled },
        { "rayQueryLocalProbeCount", stats.rayQueryLocalProbeCount },
        { "rayQuerySourceFusionEnabled", stats.rayQuerySourceFusionEnabled },
        { "rayQueryDirectMirrorEnabled", stats.rayQueryDirectMirrorEnabled },
        { "rayQueryDirectionalLightCount", stats.rayQueryDirectionalLightCount },
        { "rayQueryLocalLightCount", stats.rayQueryLocalLightCount },
        { "rayQueryHitLightingVisibilityMode", stats.rayQueryHitLightingVisibilityMode },
        { "rayQueryShadowVisibilityResourcesReady", stats.rayQueryShadowVisibilityResourcesReady },
        { "rayQueryReadbackValid", stats.rayQueryReadbackValid },
        { "rayQueryCandidateRayCount", stats.rayQueryCandidateRayCount },
        { "rayQueryScreenHitAcceptedCount", stats.rayQueryScreenHitAcceptedCount },
        { "rayQueryTraceCount", stats.rayQueryTraceCount },
        { "rayQueryCommittedHitCount", stats.rayQueryCommittedHitCount },
        { "rayQueryMissCount", stats.rayQueryMissCount },
        { "rayQueryInvalidRayCount", stats.rayQueryInvalidRayCount },
        { "rayQueryHitAttributeResolvedCount", stats.rayQueryHitAttributeResolvedCount },
        { "rayQueryHitAttributeInvalidInstanceCount", stats.rayQueryHitAttributeInvalidInstanceCount },
        { "rayQueryHitLightingResolvedCount", stats.rayQueryHitLightingResolvedCount },
        { "rayQueryHitLightingInvalidCount", stats.rayQueryHitLightingInvalidCount },
        { "rayQueryLocalProbeIblResolvedCount", stats.rayQueryLocalProbeIblResolvedCount },
        { "rayQueryGlobalIblFallbackCount", stats.rayQueryGlobalIblFallbackCount },
        { "rayQueryShadowRayCount", stats.rayQueryShadowRayCount },
        { "rayQueryShadowVisibleCount", stats.rayQueryShadowVisibleCount },
        { "rayQueryShadowOccludedCount", stats.rayQueryShadowOccludedCount },
        { "rayQueryDenoiserInjectionEnabled", stats.rayQueryDenoiserInjectionEnabled },
        { "active", stats.active },
        { "fallbackReason", stats.fallbackReason }
    };
}

json SerializeReflectionProbeStats(const RendererReflectionProbeStats& stats) {
    return {
        { "fallbackEnabled", stats.fallbackEnabled },
        { "diffuseIntensity", stats.diffuseIntensity },
        { "specularIntensity", stats.specularIntensity },
        { "horizonBlend", stats.horizonBlend },
        { "globalIblCubemapSamplingEnabled", stats.globalIblCubemapSamplingEnabled },
        { "sceneProbeCount", stats.sceneProbeCount },
        { "activeProbeCount", stats.activeProbeCount },
        { "sceneEligibleProbeCount", stats.sceneEligibleProbeCount },
        { "selectedProbeCount", stats.selectedProbeCount },
        { "blendedProbeCount", stats.blendedProbeCount },
        { "selectedCaptureSlotCount", stats.selectedCaptureSlotCount },
        { "selectedCaptureResourceReadyCount", stats.selectedCaptureResourceReadyCount },
        { "selectedCaptureFallbackCount", stats.selectedCaptureFallbackCount },
        { "selectedCubemapSamplingCount", stats.selectedCubemapSamplingCount },
        { "selectedCaptureReadyMask", stats.selectedCaptureReadyMask },
        { "selectedCaptureFallbackMask", stats.selectedCaptureFallbackMask },
        { "capturedSceneRequestedCount", stats.capturedSceneRequestedCount },
        { "capturedScenePlaceholderAllocatedCount", stats.capturedScenePlaceholderAllocatedCount },
        { "capturedScenePlaceholderReadyCount", stats.capturedScenePlaceholderReadyCount },
        { "capturedSceneInvalidatedCount", stats.capturedSceneInvalidatedCount },
        { "capturedSceneRefreshRequestedCount", stats.capturedSceneRefreshRequestedCount },
        { "capturedSceneCaptureBackend", stats.capturedSceneCaptureBackend },
        { "capturedSceneFaceCount", stats.capturedSceneFaceCount },
        { "capturedSceneFacesRendered", stats.capturedSceneFacesRendered },
        { "capturedSceneFacesPending", stats.capturedSceneFacesPending },
        { "capturedSceneCapturePassCount", stats.capturedSceneCapturePassCount },
        { "capturedSceneCaptureDrawCount", stats.capturedSceneCaptureDrawCount },
        { "capturedSceneCaptureVisibleCount", stats.capturedSceneCaptureVisibleCount },
        { "capturedSceneCaptureCulledCount", stats.capturedSceneCaptureCulledCount },
        { "capturedSceneSelfCaptureExcludedCount", stats.capturedSceneSelfCaptureExcludedCount },
        { "capturedSceneExplicitProbeExcludedCount", stats.capturedSceneExplicitProbeExcludedCount },
        { "capturedSceneGgxPrefilterReady", stats.capturedSceneGgxPrefilterReady },
        { "capturedSceneDiffuseIrradianceReady", stats.capturedSceneDiffuseIrradianceReady },
        { "capturedSceneReadyProbeCount", stats.capturedSceneReadyProbeCount },
        { "capturedSceneInFlightProbeCount", stats.capturedSceneInFlightProbeCount },
        { "capturedSceneRefreshPerformed", stats.capturedSceneRefreshPerformed },
        { "capturedSceneRefreshReason", stats.capturedSceneRefreshReason },
        { "capturedSceneDirtyMask", stats.capturedSceneDirtyMask },
        { "capturedSceneMembershipRevision", stats.capturedSceneMembershipRevision },
        { "capturedSceneLightRevision", stats.capturedSceneLightRevision },
        { "capturedSceneRenderRevision", stats.capturedSceneRenderRevision },
        { "capturedSceneSchedulerFrame", stats.capturedSceneSchedulerFrame },
        { "capturedSceneLastRefreshCompletedFrame", stats.capturedSceneLastRefreshCompletedFrame },
        { "authoredCubemapLoadedCount", stats.authoredCubemapLoadedCount },
        { "authoredCubemapMissingCount", stats.authoredCubemapMissingCount },
        { "authoredCubemapLoadFailedCount", stats.authoredCubemapLoadFailedCount },
        { "authoredCubemapUploadCount", stats.authoredCubemapUploadCount },
        { "authoredCubemapCacheHitCount", stats.authoredCubemapCacheHitCount },
        { "authoredCubemapFaceSize", stats.authoredCubemapFaceSize },
        { "authoredCubemapMipCount", stats.authoredCubemapMipCount },
        { "authoredCubemapFormat", static_cast<i32>(stats.authoredCubemapFormat) },
        { "droppedProbeCount", stats.droppedProbeCount },
        { "selectedProbeIndex", stats.selectedProbeIndex },
        { "selectedProbeMask", stats.selectedProbeMask },
        { "selectedBoxProjectionMask", stats.selectedBoxProjectionMask },
        { "selectedBoxProjectionRayHitMask", stats.selectedBoxProjectionRayHitMask },
        { "selectedBoxProjectionDirectionChangedMask", stats.selectedBoxProjectionDirectionChangedMask },
        { "selectedBoxProjectionOutsideFallbackMask", stats.selectedBoxProjectionOutsideFallbackMask },
        { "selectedSceneOwnedMask", stats.selectedSceneOwnedMask },
        { "selectedPositiveInfluenceMask", stats.selectedPositiveInfluenceMask },
        { "blendWeightNormalizationFallbackCount", stats.blendWeightNormalizationFallbackCount },
        { "selectedProbeIndices", stats.selectedProbeIndices },
        { "selectedCaptureSlots", stats.selectedCaptureSlots },
        { "selectedCaptureSourceTypes", stats.selectedCaptureSourceTypes },
        { "selectedCaptureFallbackReasons", stats.selectedCaptureFallbackReasons },
        { "selectedRefreshPolicies", stats.selectedRefreshPolicies },
        { "selectedCaptureMipCounts", stats.selectedCaptureMipCounts },
        { "maxBlendWeight", stats.maxBlendWeight },
        { "totalBlendWeight", stats.totalBlendWeight },
        { "normalizedBlendWeightSum", stats.normalizedBlendWeightSum },
        { "normalizedBlendWeightError", stats.normalizedBlendWeightError },
        { "selectedBlendWeights", stats.selectedBlendWeights },
        { "selectedNormalizedBlendWeights", stats.selectedNormalizedBlendWeights },
        { "spatialContractFailureMask", stats.spatialContractFailureMask },
        { "spatialContractValid", stats.spatialContractValid },
        { "multiBlendEnabled", stats.multiBlendEnabled },
        { "localEnabled", stats.localEnabled },
        { "localSceneOwned", stats.localSceneOwned },
        { "localRadius", stats.localRadius },
        { "localBoxExtentX", stats.localBoxExtentX },
        { "localBoxExtentY", stats.localBoxExtentY },
        { "localBoxExtentZ", stats.localBoxExtentZ },
        { "localIntensity", stats.localIntensity },
        { "localBlendStrength", stats.localBlendStrength },
        { "localFalloff", stats.localFalloff },
        { "localCubemapAllocated", stats.localCubemapAllocated },
        { "localCubemapFaceSize", stats.localCubemapFaceSize },
        { "localCubemapMipCount", stats.localCubemapMipCount },
        { "localCubemapFormat", static_cast<i32>(stats.localCubemapFormat) },
        { "localCubemapDescriptorSetsBound", stats.localCubemapDescriptorSetsBound },
        { "localCubemapShaderSamplingEnabled", stats.localCubemapShaderSamplingEnabled },
        { "captureSourceType", stats.captureSourceType },
        { "refreshPolicy", stats.refreshPolicy },
        { "captureResourceReady", stats.captureResourceReady },
        { "captureFallbackReason", stats.captureFallbackReason },
        { "captureDescriptorBound", stats.captureDescriptorBound },
        { "boxProjectionEnabled", stats.boxProjectionEnabled },
        { "parallaxCorrectionEnabled", stats.parallaxCorrectionEnabled }
    };
}

json SerializePostProcess(const RendererPostProcessStats& stats) {
    return {
        { "bloomEnabled", stats.bloomEnabled },
        { "bloomIntensity", stats.bloomIntensity },
        { "bloomThreshold", stats.bloomThreshold },
        { "bloomRadiusPixels", stats.bloomRadiusPixels },
        { "bloomPyramidEnabled", stats.bloomPyramidEnabled },
        { "bloomPyramidMipCount", stats.bloomPyramidMipCount },
        { "bloomPyramidFallbacks", stats.bloomPyramidFallbacks },
        { "toneMappingEnabled", stats.toneMappingEnabled },
        { "toneMapMode", stats.toneMapMode },
        { "exposure", stats.exposure },
        { "toneMapWhitePoint", stats.toneMapWhitePoint },
        { "autoExposureEnabled", stats.autoExposureEnabled },
        { "autoExposureTargetLuminance", stats.autoExposureTargetLuminance },
        { "autoExposureMin", stats.autoExposureMin },
        { "autoExposureMax", stats.autoExposureMax },
        { "autoExposureAdaptation", stats.autoExposureAdaptation },
        { "autoExposureHistogramEnabled", stats.autoExposureHistogramEnabled },
        { "autoExposureHistoryValid", stats.autoExposureHistoryValid },
        { "autoExposureFallbacks", stats.autoExposureFallbacks },
        { "autoExposureGpuExposure", stats.autoExposureGpuExposure },
        { "autoExposureGpuTargetExposure", stats.autoExposureGpuTargetExposure },
        { "autoExposureGpuAverageLuminance", stats.autoExposureGpuAverageLuminance },
        { "colorGradingEnabled", stats.colorGradingEnabled },
        { "colorGradingSaturation", stats.colorGradingSaturation },
        { "colorGradingContrast", stats.colorGradingContrast },
        { "colorGradingGamma", stats.colorGradingGamma },
        { "colorGradingLutEnabled", stats.colorGradingLutEnabled },
        { "colorGradingLutSize", stats.colorGradingLutSize },
        { "colorGradingLutStrength", stats.colorGradingLutStrength },
        { "sharpeningEnabled", stats.sharpeningEnabled },
        { "sharpeningStrength", stats.sharpeningStrength },
        { "sharpeningRadiusPixels", stats.sharpeningRadiusPixels }
    };
}

json SerializeTemporal(const RendererTemporalStats& stats) {
    return {
        { "antialiasingMode", stats.antialiasingMode },
        { "velocityTargetAllocated", stats.velocityTargetAllocated },
        { "velocityFormat", static_cast<i32>(stats.velocityFormat) },
        { "velocityCameraMotionEnabled", stats.velocityCameraMotionEnabled },
        { "velocityCameraMotionReady", stats.velocityCameraMotionReady },
        { "velocityObjectMotionReady", stats.velocityObjectMotionReady },
        { "velocityMaterialAuxTargetAllocated", stats.velocityMaterialAuxTargetAllocated },
        { "velocityMaterialAuxMigrated", stats.velocityMaterialAuxMigrated },
        { "historyValid", stats.historyValid },
        { "historyReset", stats.historyReset },
        { "historyResetReason", stats.historyResetReason },
        { "jitterEnabled", stats.jitterEnabled },
        { "jitterApplied", stats.jitterApplied },
        { "jitterSequenceIndex", stats.jitterSequenceIndex },
        { "jitterPixelsX", stats.jitterPixelsX },
        { "jitterPixelsY", stats.jitterPixelsY },
        { "jitterUvX", stats.jitterUvX },
        { "jitterUvY", stats.jitterUvY },
        { "previousJitterPixelsX", stats.previousJitterPixelsX },
        { "previousJitterPixelsY", stats.previousJitterPixelsY },
        { "taaResolveConfigured", stats.taaResolveConfigured },
        { "taaResolveEnabled", stats.taaResolveEnabled },
        { "taaResolveSuppressedForUpscaler", stats.taaResolveSuppressedForUpscaler },
        { "taaHistoryColorTargetAllocated", stats.taaHistoryColorTargetAllocated },
        { "taaHistoryColorReady", stats.taaHistoryColorReady },
        { "taaHistoryColorCopies", stats.taaHistoryColorCopies },
        { "taaHistoryWeight", stats.taaHistoryWeight },
        { "taaVelocityReprojectionEnabled", stats.taaVelocityReprojectionEnabled },
        { "taaFallbackReason", stats.taaFallbackReason },
        { "temporalConsumerReadinessMask", stats.temporalConsumerReadinessMask },
        { "temporalConsumerActiveMask", stats.temporalConsumerActiveMask },
        { "temporalConsumerUnsupportedMask", stats.temporalConsumerUnsupportedMask },
        { "renderScaleRequested", stats.renderScaleRequested },
        { "renderScaleActive", stats.renderScaleActive },
        { "renderScaleApplied", stats.renderScaleApplied },
        { "temporalUpscaleDisplayWidth", stats.temporalUpscaleDisplayWidth },
        { "temporalUpscaleDisplayHeight", stats.temporalUpscaleDisplayHeight },
        { "temporalUpscaleActiveWidth", stats.temporalUpscaleActiveWidth },
        { "temporalUpscaleActiveHeight", stats.temporalUpscaleActiveHeight },
        { "temporalUpscaleOutputAllocated", stats.temporalUpscaleOutputAllocated },
        { "temporalUpscalePostSourceActive", stats.temporalUpscalePostSourceActive },
        { "dynamicResolutionRequested", stats.dynamicResolutionRequested },
        { "dynamicResolutionEnabled", stats.dynamicResolutionEnabled },
        { "taauRequested", stats.taauRequested },
        { "temporalUpscaleRequested", stats.temporalUpscaleRequested },
        { "temporalUpscaleEnabled", stats.temporalUpscaleEnabled },
        { "temporalUpscaleInputReady", stats.temporalUpscaleInputReady },
        { "temporalUpscaleFallbackReason", stats.temporalUpscaleFallbackReason },
        { "temporalUpscaleContractReady", stats.temporalUpscaleContractReady },
        { "temporalUpscalerPluginRequested", stats.temporalUpscalerPluginRequested },
        { "temporalUpscalerPluginAvailable", stats.temporalUpscalerPluginAvailable },
        { "temporalUpscalerProviderKind", stats.temporalUpscalerProviderKind },
        { "temporalUpscalerPackageReady", stats.temporalUpscalerPackageReady },
        { "temporalUpscalerRuntimeFallbackReason", stats.temporalUpscalerRuntimeFallbackReason },
        { "temporalUpscalerAdapterCompiled", stats.temporalUpscalerAdapterCompiled },
        { "temporalUpscalerInitialized", stats.temporalUpscalerInitialized },
        { "temporalUpscalerFeatureSupportedMask", stats.temporalUpscalerFeatureSupportedMask },
        { "temporalUpscalerDlssSuperResolutionSupported", stats.temporalUpscalerDlssSuperResolutionSupported },
        { "temporalUpscalerDlssQualityMode", stats.temporalUpscalerDlssQualityMode },
        { "temporalUpscalerDlssRecommendedPreset", stats.temporalUpscalerDlssRecommendedPreset },
        { "temporalUpscalerOptimalRenderWidth", stats.temporalUpscalerOptimalRenderWidth },
        { "temporalUpscalerOptimalRenderHeight", stats.temporalUpscalerOptimalRenderHeight },
        { "temporalUpscalerSharpness", stats.temporalUpscalerSharpness },
        { "temporalUpscalerEvaluateRequested", stats.temporalUpscalerEvaluateRequested },
        { "temporalUpscalerEvaluateAttempted", stats.temporalUpscalerEvaluateAttempted },
        { "temporalUpscalerEvaluateFallbackReason", stats.temporalUpscalerEvaluateFallbackReason },
        { "temporalUpscalerFeatureCreated", stats.temporalUpscalerFeatureCreated },
        { "temporalUpscalerDlssEvaluateAttempted", stats.temporalUpscalerDlssEvaluateAttempted },
        { "temporalUpscalerDlssEvaluateResult", stats.temporalUpscalerDlssEvaluateResult },
        { "temporalUpscalerDlssOutputReady", stats.temporalUpscalerDlssOutputReady },
        { "temporalUpscalerDlssRenderWidth", stats.temporalUpscalerDlssRenderWidth },
        { "temporalUpscalerDlssRenderHeight", stats.temporalUpscalerDlssRenderHeight },
        { "temporalUpscalerDlssOutputWidth", stats.temporalUpscalerDlssOutputWidth },
        { "temporalUpscalerDlssOutputHeight", stats.temporalUpscalerDlssOutputHeight },
        { "temporalUpscalerDlssQualityGateRequested", stats.temporalUpscalerDlssQualityGateRequested },
        { "temporalUpscalerDlssQualityGateReady", stats.temporalUpscalerDlssQualityGateReady },
        { "temporalUpscalerDlssQualityGateFallbackReason", stats.temporalUpscalerDlssQualityGateFallbackReason },
        { "temporalUpscalerDlssQualityRequiredMask", stats.temporalUpscalerDlssQualityRequiredMask },
        { "temporalUpscalerDlssQualityReadyMask", stats.temporalUpscalerDlssQualityReadyMask },
        { "temporalUpscalerDlssQualityBlockerMask", stats.temporalUpscalerDlssQualityBlockerMask }
    };
}

json SerializeFrameGraph(const RenderFrameGraphPlan& graph) {
    json passes = json::array();
    for (const RenderFramePass& pass : graph.passes) {
        json passJson = {
            { "id", pass.id },
            { "kind", static_cast<u32>(pass.kind) },
            { "status", Text(RenderFramePassStatusName(pass.status)) },
            { "queue", Text(RenderFramePassQueueName(pass.queue)) },
            { "name", Text(pass.name) },
            { "reads", Text(pass.reads) },
            { "writes", Text(pass.writes) },
            { "purpose", Text(pass.purpose) }
        };
        passJson["readResources"] = json::array();
        for (const RenderFrameGraphResourceRef& resource : pass.readResources) {
            passJson["readResources"].push_back({
                { "id", resource.resourceId },
                { "name", Text(resource.name) },
                { "access", Text(RenderFrameGraphResourceAccessName(resource.access)) }
            });
        }
        passJson["writeResources"] = json::array();
        for (const RenderFrameGraphResourceRef& resource : pass.writeResources) {
            passJson["writeResources"].push_back({
                { "id", resource.resourceId },
                { "name", Text(resource.name) },
                { "access", Text(RenderFrameGraphResourceAccessName(resource.access)) }
            });
        }
        passJson["dependencies"] = json::array();
        for (const RenderFrameGraphPassDependency& dependency : pass.dependencies) {
            passJson["dependencies"].push_back({
                { "passId", dependency.passId },
                { "passName", Text(dependency.passName) },
                { "resourceId", dependency.resourceId },
                { "resourceName", Text(dependency.resourceName) },
                { "writeDependency", dependency.writeDependency }
            });
        }
        passes.push_back(std::move(passJson));
    }

    json resources = json::array();
    for (const RenderGraphResource& resource : graph.resources) {
        resources.push_back({
            { "id", resource.id },
            { "status", Text(RenderGraphResourceStatusName(resource.status)) },
            { "lifetime", Text(RenderGraphResourceLifetimeName(resource.lifetime)) },
            { "name", Text(resource.name) },
            { "format", Text(resource.format) },
            { "usage", Text(resource.usage) },
            { "scale", Text(resource.scale) },
            { "firstUsePassId", resource.firstUsePassId },
            { "firstUsePassName", Text(resource.firstUsePassName) },
            { "lastUsePassId", resource.lastUsePassId },
            { "lastUsePassName", Text(resource.lastUsePassName) },
            { "readCount", resource.readCount },
            { "writeCount", resource.writeCount }
        });
    }

    json barriers = json::array();
    for (const RenderFrameGraphBarrierTransition& barrier : graph.barrierTransitions) {
        barriers.push_back({
            { "producerPassId", barrier.producerPassId },
            { "producerPassName", Text(barrier.producerPassName) },
            { "producerQueue", Text(RenderFramePassQueueName(barrier.producerQueue)) },
            { "consumerPassId", barrier.consumerPassId },
            { "consumerPassName", Text(barrier.consumerPassName) },
            { "consumerQueue", Text(RenderFramePassQueueName(barrier.consumerQueue)) },
            { "resourceId", barrier.resourceId },
            { "resourceName", Text(barrier.resourceName) },
            { "resourceKind", Text(RenderFrameGraphBarrierResourceKindName(barrier.resourceKind)) },
            { "srcAccess", Text(RenderFrameGraphResourceAccessName(barrier.srcAccess)) },
            { "dstAccess", Text(RenderFrameGraphResourceAccessName(barrier.dstAccess)) },
            { "srcStage", Text(barrier.srcStage) },
            { "dstStage", Text(barrier.dstStage) },
            { "oldLayout", Text(barrier.oldLayout) },
            { "newLayout", Text(barrier.newLayout) },
            { "layoutTransition", barrier.layoutTransition },
            { "queueOwnershipTransfer", barrier.queueOwnershipTransfer },
            { "writeDependency", barrier.writeDependency }
        });
    }

    json issues = json::array();
    for (const RenderFrameGraphValidationIssue& issue : graph.validation.issues) {
        issues.push_back({
            { "kind", Text(RenderFrameGraphValidationIssueKindName(issue.kind)) },
            { "passId", issue.passId },
            { "passName", issue.passName },
            { "resourceId", issue.resourceId },
            { "resourceName", issue.resourceName },
            { "writeRef", issue.writeRef }
        });
    }

    return {
        { "name", Text(graph.name) },
        { "target", Text(graph.target) },
        { "activePassCount", graph.activePassCount },
        { "roadmapPassCount", graph.roadmapPassCount },
        { "physicalResourceCount", graph.physicalResourceCount },
        { "plannedResourceCount", graph.plannedResourceCount },
        { "passes", std::move(passes) },
        { "resources", std::move(resources) },
        { "barriers", std::move(barriers) },
        { "validation", {
            { "issueCount", graph.validation.issueCount },
            { "unnamedPassCount", graph.validation.unnamedPassCount },
            { "duplicatePassIdCount", graph.validation.duplicatePassIdCount },
            { "unnamedResourceCount", graph.validation.unnamedResourceCount },
            { "duplicateResourceIdCount", graph.validation.duplicateResourceIdCount },
            { "missingResourceRefCount", graph.validation.missingResourceRefCount },
            { "readBeforeFirstWriteCount", graph.validation.readBeforeFirstWriteCount },
            { "unusedPhysicalResourceCount", graph.validation.unusedPhysicalResourceCount },
            { "writeOnlyRoadmapResourceCount", graph.validation.writeOnlyRoadmapResourceCount },
            { "activePassWritesPlannedResourceCount", graph.validation.activePassWritesPlannedResourceCount },
            { "issues", std::move(issues) }
        } },
        { "references", {
            { "readCount", graph.references.readCount },
            { "writeCount", graph.references.writeCount },
            { "readSampledCount", graph.references.readSampledCount },
            { "readAttachmentCount", graph.references.readAttachmentCount },
            { "writeColorAttachmentCount", graph.references.writeColorAttachmentCount },
            { "writeDepthAttachmentCount", graph.references.writeDepthAttachmentCount },
            { "writeStorageCount", graph.references.writeStorageCount },
            { "presentCount", graph.references.presentCount },
            { "unstructuredReadTokenCount", graph.references.unstructuredReadTokenCount },
            { "unstructuredWriteTokenCount", graph.references.unstructuredWriteTokenCount }
        } },
        { "dependencies", {
            { "dependencyCount", graph.dependencies.dependencyCount },
            { "readAfterWriteCount", graph.dependencies.readAfterWriteCount },
            { "writeAfterWriteCount", graph.dependencies.writeAfterWriteCount }
        } },
        { "lifetimes", {
            { "usedResourceCount", graph.lifetimes.usedResourceCount },
            { "unusedResourceCount", graph.lifetimes.unusedResourceCount },
            { "readOnlyResourceCount", graph.lifetimes.readOnlyResourceCount },
            { "writeOnlyResourceCount", graph.lifetimes.writeOnlyResourceCount },
            { "readWriteResourceCount", graph.lifetimes.readWriteResourceCount }
        } },
        { "barrierStats", {
            { "transitionCount", graph.barriers.transitionCount },
            { "imageTransitionCount", graph.barriers.imageTransitionCount },
            { "bufferTransitionCount", graph.barriers.bufferTransitionCount },
            { "layoutTransitionCount", graph.barriers.layoutTransitionCount },
            { "queueOwnershipTransferCount", graph.barriers.queueOwnershipTransferCount },
            { "readAfterWriteTransitionCount", graph.barriers.readAfterWriteTransitionCount },
            { "writeAfterWriteTransitionCount", graph.barriers.writeAfterWriteTransitionCount }
        } },
        { "barrierExecution", {
            { "plannedBridgeBarrierCount", graph.barrierExecution.plannedBridgeBarrierCount },
            { "executedBarrierCount", graph.barrierExecution.executedBarrierCount },
            { "fallbackBarrierCount", graph.barrierExecution.fallbackBarrierCount },
            { "mismatchCount", graph.barrierExecution.mismatchCount }
        } }
    };
}

json SerializeBinds(const RendererBindStats& stats) {
    return {
        { "mainMaterialBinds", stats.mainMaterialBinds },
        { "mainMeshBinds", stats.mainMeshBinds },
        { "gBufferMaterialBinds", stats.gBufferMaterialBinds },
        { "gBufferMeshBinds", stats.gBufferMeshBinds },
        { "deferredLightingDraws", stats.deferredLightingDraws },
        { "hdrCompositeDraws", stats.hdrCompositeDraws },
        { "ssrHiZBuildDispatches", stats.ssrHiZBuildDispatches },
        { "ssrReconstructionTraceDispatches", stats.ssrReconstructionTraceDispatches },
        { "ssrReconstructionTemporalDispatches", stats.ssrReconstructionTemporalDispatches },
        { "ssrReconstructionSpatialDispatches", stats.ssrReconstructionSpatialDispatches },
        { "ffxSssrClassifyTilesDispatches", stats.ffxSssrClassifyTilesDispatches },
        { "ffxSssrIntersectDispatches", stats.ffxSssrIntersectDispatches },
        { "ffxSssrReprojectDispatches", stats.ffxSssrReprojectDispatches },
        { "ffxSssrPrefilterDispatches", stats.ffxSssrPrefilterDispatches },
        { "ffxSssrResolveTemporalDispatches", stats.ffxSssrResolveTemporalDispatches },
        { "ffxSssrApplyDraws", stats.ffxSssrApplyDraws },
        { "rayQueryApplyDraws", stats.rayQueryApplyDraws },
        { "reflectionProbeDebugDraws", stats.reflectionProbeDebugDraws },
        { "lightTileCullComputeDispatches", stats.lightTileCullComputeDispatches },
        { "autoExposureHistogramDispatches", stats.autoExposureHistogramDispatches },
        { "depthCopyOps", stats.depthCopyOps },
        { "depthPrefillDraws", stats.depthPrefillDraws },
        { "weightedTranslucencyClearPasses", stats.weightedTranslucencyClearPasses },
        { "weightedTranslucencyDraws", stats.weightedTranslucencyDraws },
        { "weightedTranslucencyResolveDraws", stats.weightedTranslucencyResolveDraws },
        { "dlssMaskDraws", stats.dlssMaskDraws },
        { "forwardResidualDraws", stats.forwardResidualDraws },
        { "shadowCascadeAtlasPasses", stats.shadowCascadeAtlasPasses },
        { "shadowCascadeAtlasDraws", stats.shadowCascadeAtlasDraws },
        { "localShadowAtlasPasses", stats.localShadowAtlasPasses },
        { "localShadowAtlasDraws", stats.localShadowAtlasDraws },
        { "frameLightConstantUpdates", stats.frameLightConstantUpdates },
        { "frameLightBufferUpdates", stats.frameLightBufferUpdates },
        { "frameLightTotalCount", stats.frameLightTotalCount },
        { "frameDirectionalLightCount", stats.frameDirectionalLightCount },
        { "frameLocalLightCount", stats.frameLocalLightCount },
        { "frameRectLightCount", stats.frameRectLightCount },
        { "framePointLightCount", stats.framePointLightCount },
        { "frameSpotLightCount", stats.frameSpotLightCount },
        { "frameLightTileSize", stats.frameLightTileSize },
        { "frameLightTileCountX", stats.frameLightTileCountX },
        { "frameLightTileCountY", stats.frameLightTileCountY },
        { "frameLightTileAssignments", stats.frameLightTileAssignments },
        { "frameLightTileOverflowAssignments", stats.frameLightTileOverflowAssignments },
        { "frameMaterialBufferUpdates", stats.frameMaterialBufferUpdates },
        { "frameMaterialCount", stats.frameMaterialCount },
        { "frameMaterialCapacity", stats.frameMaterialCapacity },
        { "frameMaterialOverflowCount", stats.frameMaterialOverflowCount },
        { "frameMaterialOpaqueCount", stats.frameMaterialOpaqueCount },
        { "frameMaterialTransparentCount", stats.frameMaterialTransparentCount },
        { "frameMaterialForwardSpecialCount", stats.frameMaterialForwardSpecialCount },
        { "frameMaterialTexturedCount", stats.frameMaterialTexturedCount },
        { "frameMaterialDoubleSidedCount", stats.frameMaterialDoubleSidedCount },
        { "frameMaterialClearcoatCount", stats.frameMaterialClearcoatCount },
        { "frameMaterialTransmissionCount", stats.frameMaterialTransmissionCount },
        { "mainInstancedDraws", stats.mainInstancedDraws },
        { "mainInstancedInstances", stats.mainInstancedInstances },
        { "mainInstanceBufferUploads", stats.mainInstanceBufferUploads },
        { "pushConstantUpdates", stats.pushConstantUpdates },
        { "pushConstantBytes", stats.pushConstantBytes }
    };
}

json SerializePipeline(const RendererStats& stats) {
    return {
        { "cpu", SerializeCpu(stats.cpu) },
        { "renderDebug", {
            { "forwardView", stats.renderDebug.forwardView },
            { "deferredPbrDebugView", stats.renderDebug.deferredPbrDebugView },
            { "usesDeferredHdrComposite", stats.renderDebug.usesDeferredHdrComposite },
            { "temporalReconstructionBypassed", stats.renderDebug.temporalReconstructionBypassed },
            { "lightingEnergyViewEnabled", stats.renderDebug.lightingEnergyViewEnabled }
        } },
        { "draw", SerializeDraw(stats.draw) },
        { "meshLod", SerializeMeshLod(stats.meshLod) },
        { "occlusion", SerializeOcclusion(stats.gpuOcclusion) },
        { "shadowCascades", SerializeShadowCascades(stats.shadowCascades) },
        { "localShadowAtlas", SerializeLocalShadowAtlas(stats.localShadowAtlas) },
        { "weightedTranslucency", {
            { "allocated", stats.weightedTranslucency.allocated },
            { "accumWidth", stats.weightedTranslucency.accumWidth },
            { "accumHeight", stats.weightedTranslucency.accumHeight },
            { "revealageWidth", stats.weightedTranslucency.revealageWidth },
            { "revealageHeight", stats.weightedTranslucency.revealageHeight },
            { "accumFormat", static_cast<i32>(stats.weightedTranslucency.accumFormat) },
            { "revealageFormat", static_cast<i32>(stats.weightedTranslucency.revealageFormat) },
            { "renderPassAllocated", stats.weightedTranslucency.renderPassAllocated },
            { "framebufferCount", stats.weightedTranslucency.framebufferCount },
            { "clearPasses", stats.weightedTranslucency.clearPasses },
            { "draws", stats.weightedTranslucency.draws },
            { "resolveDraws", stats.weightedTranslucency.resolveDraws }
        } },
        { "ssao", {
            { "enabled", stats.ssao.enabled },
            { "strength", stats.ssao.strength },
            { "radius", stats.ssao.radius },
            { "bias", stats.ssao.bias },
            { "sampleCount", stats.ssao.sampleCount }
        } },
        { "ssr", SerializeSsr(stats.ssr) },
        { "hybridReflections", SerializeHybridReflections(stats.hybridReflections) },
        { "ibl", {
            { "quality", stats.ibl.quality },
            { "requestedSource", stats.ibl.requestedSource },
            { "actualSource", stats.ibl.actualSource },
            { "sourceFallbackReason", stats.ibl.sourceFallbackReason },
            { "cachePolicy", stats.ibl.cachePolicy },
            { "cacheFallbackReason", stats.ibl.cacheFallbackReason },
            { "cacheHit", stats.ibl.cacheHit },
            { "runtimeGenerated", stats.ibl.runtimeGenerated },
            { "sourceAssetSpecified", stats.ibl.sourceAssetSpecified },
            { "sourceAssetFound", stats.ibl.sourceAssetFound },
            { "sourceSignature", stats.ibl.sourceSignature },
            { "brdfLutAllocated", stats.ibl.brdfLutAllocated },
            { "brdfLutSize", stats.ibl.brdfLutSize },
            { "brdfLutFormat", static_cast<i32>(stats.ibl.brdfLutFormat) },
            { "irradianceMapAllocated", stats.ibl.irradianceMapAllocated },
            { "irradianceFaceSize", stats.ibl.irradianceFaceSize },
            { "irradianceFormat", static_cast<i32>(stats.ibl.irradianceFormat) },
            { "prefilteredMapAllocated", stats.ibl.prefilteredMapAllocated },
            { "prefilteredFaceSize", stats.ibl.prefilteredFaceSize },
            { "prefilteredMipCount", stats.ibl.prefilteredMipCount },
            { "prefilteredFormat", static_cast<i32>(stats.ibl.prefilteredFormat) },
            { "descriptorSetsBound", stats.ibl.descriptorSetsBound },
            { "shaderIntegrationEnabled", stats.ibl.shaderIntegrationEnabled }
        } },
        { "environment", {
            { "iblEnabled", stats.environment.iblEnabled },
            { "diffuseIntensity", stats.environment.diffuseIntensity },
            { "specularIntensity", stats.environment.specularIntensity },
            { "horizonBlend", stats.environment.horizonBlend },
            { "skyboxEnabled", stats.environment.skyboxEnabled },
            { "skyboxIntensity", stats.environment.skyboxIntensity },
            { "skyboxBlur", stats.environment.skyboxBlur },
            { "lightingAsset", stats.environment.lightingAsset },
            { "visibleSkyboxUsesActiveIbl", stats.environment.visibleSkyboxUsesActiveIbl },
            { "visibleSkyboxSourceTextureReady", stats.environment.visibleSkyboxSourceTextureReady },
            { "iblReloadCount", stats.environment.iblReloadCount },
            { "iblReloadFailureCount", stats.environment.iblReloadFailureCount }
        } },
        { "probeGrid", {
            { "allocated", stats.probeGrid.allocated },
            { "enabled", stats.probeGrid.enabled },
            { "shaderIntegrationEnabled", stats.probeGrid.shaderIntegrationEnabled },
            { "bufferUpdates", stats.probeGrid.bufferUpdates },
            { "fallbackCount", stats.probeGrid.fallbackCount },
            { "fallbackReason", stats.probeGrid.fallbackReason },
            { "probeCount", stats.probeGrid.probeCount },
            { "sizeX", stats.probeGrid.sizeX },
            { "sizeY", stats.probeGrid.sizeY },
            { "sizeZ", stats.probeGrid.sizeZ },
            { "vec4sPerProbe", stats.probeGrid.vec4sPerProbe },
            { "directionalLobeCount", stats.probeGrid.directionalLobeCount },
            { "cellCount", stats.probeGrid.cellCount },
            { "origin", { stats.probeGrid.originX, stats.probeGrid.originY, stats.probeGrid.originZ } },
            { "boundsMin", { stats.probeGrid.boundsMinX, stats.probeGrid.boundsMinY, stats.probeGrid.boundsMinZ } },
            { "boundsMax", { stats.probeGrid.boundsMaxX, stats.probeGrid.boundsMaxY, stats.probeGrid.boundsMaxZ } },
            { "spacing", stats.probeGrid.spacing },
            { "blendStrength", stats.probeGrid.blendStrength },
            { "debugViewEnabled", stats.probeGrid.debugViewEnabled },
            { "cellDebugViewEnabled", stats.probeGrid.cellDebugViewEnabled }
        } },
        { "bonePalette", {
            { "commandCount", stats.bonePaletteDraw.commandCount },
            { "readyCommandCount", stats.bonePaletteDraw.readyCommandCount },
            { "resourceCount", stats.bonePaletteDraw.resourceCount },
            { "readyResourceCount", stats.bonePaletteDraw.readyResourceCount },
            { "currentEntryCount", stats.bonePaletteDraw.currentEntryCount },
            { "previousEntryCount", stats.bonePaletteDraw.previousEntryCount },
            { "changedEntryCount", stats.bonePaletteDraw.changedEntryCount },
            { "drawPathReady", stats.bonePaletteDraw.drawPathReady },
            { "descriptorPathReady", stats.bonePaletteDraw.descriptorPathReady },
            { "shaderConsumerPathReady", stats.bonePaletteDraw.shaderConsumerPathReady },
            { "shaderSkinningPathReady", stats.bonePaletteDraw.shaderSkinningPathReady },
            { "shaderVelocityPathReady", stats.bonePaletteDraw.shaderVelocityPathReady }
        } },
        { "reflectionProbe", SerializeReflectionProbeStats(stats.reflectionProbe) },
        { "heightFog", {
            { "enabled", stats.heightFog.enabled },
            { "density", stats.heightFog.density },
            { "heightFalloff", stats.heightFog.heightFalloff },
            { "startDistance", stats.heightFog.startDistance },
            { "maxOpacity", stats.heightFog.maxOpacity }
        } },
        { "postProcess", SerializePostProcess(stats.postProcess) },
        { "binds", SerializeBinds(stats.binds) },
        { "gpu", {
            { "available", stats.gpu.available },
            { "shadowMs", stats.gpu.shadowMs },
            { "mainMs", stats.gpu.mainMs },
            { "overlayMs", stats.gpu.overlayMs },
            { "imguiMs", stats.gpu.imguiMs },
            { "totalRecordedMs", stats.gpu.totalRecordedMs }
        } },
        { "temporal", SerializeTemporal(stats.temporal) },
        { "frameGraph", SerializeFrameGraph(stats.frameGraph) }
    };
}

} // namespace

SceneBuilderRuntimeMonitor::SceneBuilderRuntimeMonitor(std::filesystem::path path)
    : m_Path(std::move(path)) {}

std::filesystem::path SceneBuilderRuntimeMonitor::DefaultPath() {
    return std::filesystem::path(".selfengine") / "scene_builder" /
        "runtime_monitor.json";
}

const std::filesystem::path& SceneBuilderRuntimeMonitor::Path() const {
    return m_Path;
}

void SceneBuilderRuntimeMonitor::RecordFrame(
    u32 renderedFrameIndex,
    f32 elapsedSeconds,
    const Camera3D& camera,
    const Scene3D& scene,
    const SceneBuilder& builder,
    const VulkanRenderResources2D& renderResources,
    const RendererStats& rendererStats,
    const RendererFrameMonitorSnapshot& rendererSnapshot
) {
    try {
        const Camera3DState cameraState = camera.State();
        json renderables = json::array();
        for (const Renderable3D* renderable : scene.Renderables()) {
            if (renderable == nullptr) {
                continue;
            }

            const Transform3D& transform = renderable->Transform();
            json entity = {
                { "name", renderable->Name() },
                { "renderIdentity", renderable->RenderIdentity() },
                { "meshId", Text(renderable->MeshId()) },
                { "materialId", Text(renderable->MaterialId()) },
                { "bonePaletteResourceId", Text(renderable->BonePaletteResourceId()) },
                { "drawOrder", renderable->DrawOrder() },
                { "renderStateVersion", renderable->RenderStateVersion() },
                { "pickable", renderable->Pickable() },
                { "castShadow", renderable->CastShadow() },
                { "reflectionCaptureVisible", renderable->ReflectionCaptureVisible() },
                { "selected", scene.SelectedRenderable() == renderable },
                { "transform", {
                    { "position", Vec3(transform.Position()) },
                    { "rotationDegrees", Vec3(transform.RotationDegrees()) },
                    { "rotationSpeedDegreesPerSecond", Vec3(transform.RotationSpeedDegreesPerSecond()) },
                    { "scale", Vec3(transform.Scale()) },
                    { "animateRotation", transform.AnimateRotation() },
                    { "matrixVersion", transform.MatrixVersion() },
                    { "matrix", Matrix(transform.Matrix()) }
                } }
            };

            if (const SceneBuilderObject* object =
                    builder.FindObject(renderable->RenderIdentity())) {
                entity["builderOwned"] = true;
                entity["builderPrimitive"] = Text(PrimitiveName(object->primitive));
                entity["builderPrimitiveId"] = static_cast<u32>(object->primitive);
                entity["builderObjectIdentity"] = object->renderIdentity;
                entity["builderAssetPath"] = object->assetPath.empty()
                    ? json(nullptr)
                    : json(object->assetPath);
            } else {
                entity["builderOwned"] = false;
                entity["builderPrimitive"] = nullptr;
                entity["builderPrimitiveId"] = nullptr;
                entity["builderObjectIdentity"] = nullptr;
                entity["builderAssetPath"] = nullptr;
            }
            if (renderResources.ContainsMaterial(renderable->MaterialId())) {
                entity["material"] = SerializeMaterial(
                    renderResources.Material(renderable->MaterialId()).Properties()
                );
            } else {
                entity["material"] = nullptr;
            }
            renderables.push_back(std::move(entity));
        }

        json pointLights = json::array();
        for (const PointLight3D& light : scene.PointLights()) {
            pointLights.push_back({
                { "name", light.name },
                { "identity", light.identity },
                { "enabled", light.enabled },
                { "position", Vec3(light.position) },
                { "radius", light.radius },
                { "color", Vec3(light.color) },
                { "intensity", light.intensity },
                { "sourceRadius", light.sourceRadius },
                { "builderOwned", builder.FindLight(light.identity) != nullptr }
            });
        }
        json spotLights = json::array();
        for (const SpotLight3D& light : scene.SpotLights()) {
            spotLights.push_back({
                { "name", light.name },
                { "identity", light.identity },
                { "enabled", light.enabled },
                { "position", Vec3(light.position) },
                { "direction", Vec3(light.direction) },
                { "radius", light.radius },
                { "color", Vec3(light.color) },
                { "intensity", light.intensity },
                { "innerConeDegrees", light.innerConeDegrees },
                { "outerConeDegrees", light.outerConeDegrees },
                { "sourceRadius", light.sourceRadius },
                { "builderOwned", builder.FindLight(light.identity) != nullptr }
            });
        }
        json rectLights = json::array();
        for (const RectLight3D& light : scene.RectLights()) {
            rectLights.push_back({
                { "name", light.name },
                { "identity", light.identity },
                { "enabled", light.enabled },
                { "position", Vec3(light.position) },
                { "direction", Vec3(light.direction) },
                { "width", light.width },
                { "height", light.height },
                { "radius", light.radius },
                { "color", Vec3(light.color) },
                { "intensity", light.intensity },
                { "specular", light.specular },
                { "builderOwned", builder.FindLight(light.identity) != nullptr }
            });
        }
        json probes = json::array();
        for (const ReflectionProbe3D& probe : scene.ReflectionProbes()) {
            probes.push_back({
                { "name", probe.name },
                { "enabled", probe.enabled },
                { "capturePosition", Vec3(probe.center) },
                { "radius", probe.radius },
                { "boxCenter", Vec3(probe.boxCenter) },
                { "boxExtents", Vec3(probe.boxExtents) },
                { "color", Vec3(probe.color) },
                { "intensity", probe.intensity },
                { "blendStrength", probe.blendStrength },
                { "falloff", probe.falloff },
                { "captureSource", Text(CaptureSourceName(probe.captureSource)) },
                { "captureSourceId", static_cast<u32>(probe.captureSource) },
                { "captureAssetId", probe.captureAssetId },
                { "refreshPolicy", Text(RefreshPolicyName(probe.refreshPolicy)) },
                { "refreshPolicyId", static_cast<u32>(probe.refreshPolicy) },
                { "captureExcludedRenderableIdentities",
                    probe.captureExcludedRenderableIdentities }
            });
        }

        json directionalLight = nullptr;
        if (const DirectionalLight3D* light = scene.PrimaryDirectionalLight()) {
            directionalLight = {
                { "name", light->name },
                { "identity", light->identity },
                { "enabled", light->enabled },
                { "direction", Vec3(light->direction) },
                { "intensity", light->intensity },
                { "ambient", light->ambient },
                { "specular", light->specular },
                { "angularRadiusRadians", light->angularRadiusRadians },
                { "builderOwned", builder.FindLight(light->identity) != nullptr },
                { "gizmoPosition", [&builder, light]() -> json {
                    if (const SceneBuilderLight* builderLight =
                            builder.FindLight(light->identity)) {
                        return Vec3(builderLight->gizmoPosition);
                    }
                    return nullptr;
                }() }
            };
        }

        const SceneEnvironment3D& environment = scene.Environment();
        const PreviousFrameState& previous = m_PreviousFrameState;
        const bool membershipRevisionChanged = previous.available &&
            previous.membershipRevision != scene.MembershipRevision();
        const bool renderRevisionChanged = previous.available &&
            previous.renderRevision != scene.RenderRevision();
        const bool lightRevisionChanged = previous.available &&
            previous.lightRevision != scene.LightRevision();
        const bool antialiasingModeChanged = previous.available &&
            previous.antialiasingModeId !=
                rendererSnapshot.frame.antialiasingModeId;
        const bool hybridReflectionStateChanged = previous.available &&
            (previous.hybridActive != rendererStats.hybridReflections.active ||
             previous.hybridFallbackReason !=
                rendererStats.hybridReflections.fallbackReason);
        const bool ssrStateChanged = previous.available &&
            (previous.ssrBackendActiveProvider !=
                rendererStats.ssr.backendActiveProvider ||
             previous.ssrHierarchicalFallbackReason !=
                rendererStats.ssr.hierarchicalFallbackReason);
        json events = {
            { "baseline", !previous.available },
            { "sceneRevisions", {
                { "membershipChanged", membershipRevisionChanged },
                { "membershipPrevious", previous.membershipRevision },
                { "membershipCurrent", scene.MembershipRevision() },
                { "renderChanged", renderRevisionChanged },
                { "renderPrevious", previous.renderRevision },
                { "renderCurrent", scene.RenderRevision() },
                { "lightChanged", lightRevisionChanged },
                { "lightPrevious", previous.lightRevision },
                { "lightCurrent", scene.LightRevision() }
            } },
            { "temporal", {
                { "historyReset", rendererStats.temporal.historyReset },
                { "historyResetReason", rendererStats.temporal.historyResetReason },
                { "antialiasingModeChanged", antialiasingModeChanged },
                { "antialiasingModePrevious", previous.antialiasingModeId },
                { "antialiasingModeCurrent",
                    rendererSnapshot.frame.antialiasingModeId }
            } },
            { "reflections", {
                { "hybridStateChanged", hybridReflectionStateChanged },
                { "hybridActive", rendererStats.hybridReflections.active },
                { "hybridFallbackReason",
                    rendererStats.hybridReflections.fallbackReason },
                { "ssrStateChanged", ssrStateChanged },
                { "ssrBackendActiveProvider",
                    rendererStats.ssr.backendActiveProvider },
                { "ssrHierarchicalFallbackReason",
                    rendererStats.ssr.hierarchicalFallbackReason },
                { "probeRefreshPerformed",
                    rendererStats.reflectionProbe.capturedSceneRefreshPerformed },
                { "probeInvalidatedCount",
                    rendererStats.reflectionProbe.capturedSceneInvalidatedCount },
                { "probeRefreshReason",
                    rendererStats.reflectionProbe.capturedSceneRefreshReason },
                { "probeRefreshDeferredByBudget",
                    rendererStats.reflectionProbe
                        .capturedSceneRefreshDeferredByBudget }
            } }
        };
        json document = {
            { "format", "SelfEngineSceneBuilderRuntimeMonitor" },
            { "version", 3 },
            { "frame", {
                { "renderedIndex", renderedFrameIndex },
                { "elapsedSeconds", elapsedSeconds },
                { "snapshotSequence", ++m_SnapshotSequence }
            } },
            { "session", SerializeRendererSession(rendererSnapshot.session) },
            { "frameContext", SerializeRendererFrameContext(rendererSnapshot.frame) },
            { "writer", {
                { "path", m_Path.generic_string() },
                { "previousWriteSucceeded", m_PreviousWriteSucceeded },
                { "previousWriteError", m_PreviousWriteError },
                { "lastCompletedSnapshotBytes", m_LastSnapshotBytes },
                { "lastCompletedWriteDurationMs", m_LastWriteDurationMs },
                { "writeFailureCount", m_WriteFailureCount },
                { "utf8SanitizationCount", m_Utf8SanitizationCount },
                { "snapshotBytes", 0 },
                { "utf8Sanitized", false },
                { "utf8SanitizedError", "" }
            } },
            { "camera", {
                { "position", Vec3(cameraState.position) },
                { "forward", Vec3(cameraState.forward) },
                { "distance", cameraState.distance },
                { "fovScale", cameraState.fovScale },
                { "freeLookActive", cameraState.freeLookActive },
                { "orbitInputEnabled", camera.OrbitInputEnabled() },
                { "nearClip", camera.NearClip() },
                { "farClip", camera.FarClip() }
            } },
            { "scene", {
                { "revisions", {
                    { "membership", scene.MembershipRevision() },
                    { "render", scene.RenderRevision() },
                    { "light", scene.LightRevision() }
                } },
                { "environment", {
                    { "iblEnabled", environment.iblEnabled },
                    { "diffuseIntensity", environment.diffuseIntensity },
                    { "specularIntensity", environment.specularIntensity },
                    { "horizonBlend", environment.horizonBlend },
                    { "skyboxEnabled", environment.skyboxEnabled },
                    { "skyboxIntensity", environment.skyboxIntensity },
                    { "skyboxBlur", environment.skyboxBlur },
                    { "lightingAsset", Text(LightingAssetName(environment.lightingAsset)) },
                    { "lightingAssetId", static_cast<u32>(environment.lightingAsset) },
                    { "authored", scene.EnvironmentAuthored() }
                } },
                { "entities", {
                    { "renderables", std::move(renderables) },
                    { "lights", {
                        { "directional", std::move(directionalLight) },
                        { "point", std::move(pointLights) },
                        { "spot", std::move(spotLights) },
                        { "rect", std::move(rectLights) }
                    } },
                    { "reflectionProbes", std::move(probes) }
                } },
                { "builder", SerializeBuilderStats(builder.Stats()) },
                { "gizmo", SerializeSceneBuilderGizmo(
                    rendererStats.sceneBuilderGizmo
                ) }
            } },
            { "execution", {
                { "queues", SerializeRendererQueues(rendererSnapshot.queues) },
                { "passes", SerializeRendererPasses(rendererSnapshot.passes) }
            } },
            { "resources", {
                { "inventory", SerializeRendererResources(rendererSnapshot.resources) }
            } },
            { "events", std::move(events) },
            { "pipeline", SerializePipeline(rendererStats) }
        };

        std::string compactDocument;
        bool utf8Sanitized = false;
        const auto dumpDocument = [&document, &utf8Sanitized, this]() {
            if (utf8Sanitized) {
                return document.dump(
                    -1,
                    ' ',
                    false,
                    nlohmann::json::error_handler_t::replace
                );
            }
            try {
                return document.dump();
            } catch (const nlohmann::json::exception& error) {
                utf8Sanitized = true;
                ++m_Utf8SanitizationCount;
                document["writer"]["utf8Sanitized"] = true;
                document["writer"]["utf8SanitizedError"] = error.what();
                document["writer"]["utf8SanitizationCount"] =
                    m_Utf8SanitizationCount;
                return document.dump(
                    -1,
                    ' ',
                    false,
                    nlohmann::json::error_handler_t::replace
                );
            }
        };
        try {
            compactDocument = dumpDocument();
            for (u32 attempt = 0; attempt < 3u; ++attempt) {
                document["writer"]["snapshotBytes"] =
                    static_cast<u64>(compactDocument.size());
                compactDocument = dumpDocument();
            }
            const auto writeStart = std::chrono::steady_clock::now();
            const bool writeSucceeded = WriteAtomically(compactDocument);
            m_LastWriteDurationMs = std::chrono::duration<f32, std::milli>(
                std::chrono::steady_clock::now() - writeStart
            ).count();
            m_LastSnapshotBytes = static_cast<u64>(compactDocument.size());
            if (writeSucceeded) {
                m_PreviousWriteSucceeded = true;
                m_PreviousWriteError.clear();
                m_PreviousFrameState = PreviousFrameState{
                    true,
                    scene.MembershipRevision(),
                    scene.RenderRevision(),
                    scene.LightRevision(),
                    rendererSnapshot.frame.antialiasingModeId,
                    rendererStats.hybridReflections.active,
                    rendererStats.hybridReflections.fallbackReason,
                    rendererStats.ssr.backendActiveProvider,
                    rendererStats.ssr.hierarchicalFallbackReason
                };
            } else {
                ++m_WriteFailureCount;
            }
        } catch (const std::exception& error) {
            m_PreviousWriteSucceeded = false;
            m_PreviousWriteError = error.what();
            ++m_WriteFailureCount;
        }
    } catch (const std::exception& error) {
        m_PreviousWriteSucceeded = false;
        m_PreviousWriteError = error.what();
        ++m_WriteFailureCount;
    }
}

bool SceneBuilderRuntimeMonitor::WriteAtomically(const std::string& document) {
    const std::filesystem::path parentPath = m_Path.parent_path();
    std::error_code error;
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath, error);
        if (error) {
            m_PreviousWriteSucceeded = false;
            m_PreviousWriteError = error.message();
            return false;
        }
    }

    std::filesystem::path temporaryPath = m_Path;
    temporaryPath += ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            m_PreviousWriteSucceeded = false;
            m_PreviousWriteError = "could not open runtime monitor temporary file";
            return false;
        }
        output.write(document.data(), static_cast<std::streamsize>(document.size()));
        output.flush();
        if (!output) {
            m_PreviousWriteSucceeded = false;
            m_PreviousWriteError = "could not write runtime monitor temporary file";
            return false;
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(
            temporaryPath.c_str(),
            m_Path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        )) {
        const DWORD errorCode = GetLastError();
        std::filesystem::remove(temporaryPath, error);
        m_PreviousWriteSucceeded = false;
        m_PreviousWriteError = "MoveFileExW failed: " +
            std::to_string(static_cast<unsigned long>(errorCode));
        return false;
    }
#else
    std::filesystem::rename(temporaryPath, m_Path, error);
    if (error) {
        std::filesystem::remove(temporaryPath, error);
        m_PreviousWriteSucceeded = false;
        m_PreviousWriteError = error.message();
        return false;
    }
#endif
    return true;
}

}
