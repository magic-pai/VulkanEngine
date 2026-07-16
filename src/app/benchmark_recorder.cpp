#include "app/benchmark_recorder.h"

#include "renderer/vulkan/vertex.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

namespace se {

namespace {

BenchmarkSceneDiagnostics g_BenchmarkSceneDiagnostics{};

std::string ReadEnvironmentString(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
        return {};
    }

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

u32 ReadEnvironmentU32(const char* name, u32 fallback, bool allowZero = false) {
    const std::string value = ReadEnvironmentString(name);
    if (value.empty()) {
        return fallback;
    }

    const int parsed = std::atoi(value.c_str());
    if (parsed < 0 || (parsed == 0 && !allowZero)) {
        return fallback;
    }

    return static_cast<u32>(parsed);
}

void WriteGpuValue(std::ofstream& csv, bool available, f32 value) {
    if (available) {
        csv << value;
    }
}

void WriteCsvString(std::ofstream& csv, std::string_view value) {
    csv << '"';
    for (const char ch : value) {
        if (ch == '"') {
            csv << "\"\"";
        } else {
            csv << ch;
        }
    }
    csv << '"';
}

struct SkinnedVertexAttributeDiagnostics {
    u32 strideBytes = 0;
    u32 boneIndicesLocation = 0;
    u32 boneWeightsLocation = 0;
    u32 boneIndicesOffset = 0;
    u32 boneWeightsOffset = 0;
    u32 pathReady = 0;
};

SkinnedVertexAttributeDiagnostics GetSkinnedVertexAttributeDiagnostics() {
    const auto binding = Vertex3D::BindingDescription();
    const auto attributes = Vertex3D::SkinnedAttributeDescriptions();

    auto findAttribute = [&](u32 location) -> const VkVertexInputAttributeDescription* {
        for (const VkVertexInputAttributeDescription& attribute : attributes) {
            if (attribute.location == location) {
                return &attribute;
            }
        }
        return nullptr;
    };

    const VkVertexInputAttributeDescription* boneIndices =
        findAttribute(Vertex3D::BoneIndicesLocation);
    const VkVertexInputAttributeDescription* boneWeights =
        findAttribute(Vertex3D::BoneWeightsLocation);

    SkinnedVertexAttributeDiagnostics diagnostics{};
    diagnostics.strideBytes = binding.stride;
    diagnostics.boneIndicesLocation = Vertex3D::BoneIndicesLocation;
    diagnostics.boneWeightsLocation = Vertex3D::BoneWeightsLocation;
    diagnostics.boneIndicesOffset =
        static_cast<u32>(offsetof(Vertex3D, boneIndices));
    diagnostics.boneWeightsOffset =
        static_cast<u32>(offsetof(Vertex3D, boneWeights));
    diagnostics.pathReady =
        boneIndices != nullptr &&
        boneWeights != nullptr &&
        binding.stride == sizeof(Vertex3D) &&
        boneIndices->binding == 0u &&
        boneIndices->format == VK_FORMAT_R32G32B32A32_UINT &&
        boneIndices->offset == diagnostics.boneIndicesOffset &&
        boneWeights->binding == 0u &&
        boneWeights->format == VK_FORMAT_R32G32B32A32_SFLOAT &&
        boneWeights->offset == diagnostics.boneWeightsOffset
            ? 1u
            : 0u;

    return diagnostics;
}

}

void SetBenchmarkSceneDiagnostics(const BenchmarkSceneDiagnostics& diagnostics) {
    g_BenchmarkSceneDiagnostics = diagnostics;
}

const BenchmarkSceneDiagnostics& GetBenchmarkSceneDiagnostics() {
    return g_BenchmarkSceneDiagnostics;
}

BenchmarkRecorderConfig BenchmarkRecorder::ConfigFromEnvironment() {
    BenchmarkRecorderConfig config{};
    config.captureFrames = ReadEnvironmentU32("SE_BENCHMARK_FRAMES", 0);
    config.enabled = config.captureFrames > 0;
    if (!config.enabled) {
        return config;
    }

    config.warmupFrames = ReadEnvironmentU32(
        "SE_BENCHMARK_WARMUP_FRAMES",
        config.warmupFrames,
        true
    );
    const std::string csvPath = ReadEnvironmentString("SE_BENCHMARK_CSV");
    if (!csvPath.empty()) {
        config.csvPath = csvPath;
    }

    return config;
}

BenchmarkRecorder::BenchmarkRecorder(BenchmarkRecorderConfig config)
    : m_Config(std::move(config)) {
    if (m_Config.enabled) {
        OpenCsv();
        WriteHeader();
        std::cout << "Benchmark enabled: warmup=" << m_Config.warmupFrames
            << " capture=" << m_Config.captureFrames
            << " csv=" << m_Config.csvPath.string() << std::endl;
    }
}

BenchmarkRecorder::~BenchmarkRecorder() {
    if (m_Config.enabled) {
        std::cout << "Benchmark captured " << m_CapturedFrames
            << " frames to " << m_Config.csvPath.string() << std::endl;
    }
}

bool BenchmarkRecorder::Enabled() const {
    return m_Config.enabled;
}

bool BenchmarkRecorder::ShouldStop() const {
    return m_StopRequested;
}

void BenchmarkRecorder::RecordFrame(
    u32 renderedFrameIndex,
    f32 elapsedSeconds,
    const RendererStats& stats
) {
    if (!m_Config.enabled || m_StopRequested) {
        return;
    }

    if (renderedFrameIndex <= m_Config.warmupFrames) {
        return;
    }

    const RendererCpuStats& cpu = stats.cpu;
    const RendererDrawStats& draw = stats.draw;
    const RendererShadowCascadeStats& shadowCascades = stats.shadowCascades;
    const RendererLocalShadowAtlasStats& localShadowAtlas = stats.localShadowAtlas;
    const RendererWeightedTranslucencyStats& weightedTranslucency =
        stats.weightedTranslucency;
    const RendererSsaoStats& ssao = stats.ssao;
    const RendererSsrStats& ssr = stats.ssr;
    const RendererIblStats& ibl = stats.ibl;
    const RendererProbeGridStats& probeGrid = stats.probeGrid;
    const RendererBonePaletteDrawStats& bonePaletteDraw = stats.bonePaletteDraw;
    const RendererReflectionProbeStats& reflectionProbe = stats.reflectionProbe;
    const RendererHeightFogStats& heightFog = stats.heightFog;
    const RendererPostProcessStats& postProcess = stats.postProcess;
    const RendererBindStats& binds = stats.binds;
    const RendererGpuStats& gpu = stats.gpu;
    const RendererTemporalStats& temporal = stats.temporal;
    const BenchmarkSceneDiagnostics& sceneDiagnostics =
        GetBenchmarkSceneDiagnostics();
    const SkinnedVertexAttributeDiagnostics skinnedVertexAttributes =
        GetSkinnedVertexAttributeDiagnostics();

    m_Csv << m_CapturedFrames << ','
        << renderedFrameIndex << ','
        << elapsedSeconds << ','
        << stats.frameGraph.activePassCount << ','
        << stats.frameGraph.roadmapPassCount << ','
        << stats.frameGraph.physicalResourceCount << ','
        << stats.frameGraph.plannedResourceCount << ','
        << stats.frameGraph.validation.issueCount << ','
        << stats.frameGraph.validation.unnamedPassCount << ','
        << stats.frameGraph.validation.duplicatePassIdCount << ','
        << stats.frameGraph.validation.unnamedResourceCount << ','
        << stats.frameGraph.validation.duplicateResourceIdCount << ','
        << stats.frameGraph.validation.missingResourceRefCount << ','
        << stats.frameGraph.validation.readBeforeFirstWriteCount << ','
        << stats.frameGraph.validation.unusedPhysicalResourceCount << ','
        << stats.frameGraph.validation.writeOnlyRoadmapResourceCount << ','
        << stats.frameGraph.validation.activePassWritesPlannedResourceCount << ','
        << stats.frameGraph.references.readCount << ','
        << stats.frameGraph.references.writeCount << ','
        << stats.frameGraph.references.readSampledCount << ','
        << stats.frameGraph.references.readAttachmentCount << ','
        << stats.frameGraph.references.writeColorAttachmentCount << ','
        << stats.frameGraph.references.writeDepthAttachmentCount << ','
        << stats.frameGraph.references.writeStorageCount << ','
        << stats.frameGraph.references.presentCount << ','
        << stats.frameGraph.references.unstructuredReadTokenCount << ','
        << stats.frameGraph.references.unstructuredWriteTokenCount << ','
        << stats.frameGraph.dependencies.dependencyCount << ','
        << stats.frameGraph.dependencies.readAfterWriteCount << ','
        << stats.frameGraph.dependencies.writeAfterWriteCount << ','
        << stats.frameGraph.lifetimes.usedResourceCount << ','
        << stats.frameGraph.lifetimes.unusedResourceCount << ','
        << stats.frameGraph.lifetimes.readOnlyResourceCount << ','
        << stats.frameGraph.lifetimes.writeOnlyResourceCount << ','
        << stats.frameGraph.lifetimes.readWriteResourceCount << ','
        << stats.frameGraph.barriers.transitionCount << ','
        << stats.frameGraph.barriers.imageTransitionCount << ','
        << stats.frameGraph.barriers.bufferTransitionCount << ','
        << stats.frameGraph.barriers.layoutTransitionCount << ','
        << stats.frameGraph.barriers.queueOwnershipTransferCount << ','
        << stats.frameGraph.barriers.readAfterWriteTransitionCount << ','
        << stats.frameGraph.barriers.writeAfterWriteTransitionCount << ','
        << stats.frameGraph.barrierExecution.plannedBridgeBarrierCount << ','
        << stats.frameGraph.barrierExecution.executedBarrierCount << ','
        << stats.frameGraph.barrierExecution.fallbackBarrierCount << ','
        << stats.frameGraph.barrierExecution.mismatchCount << ','
        << sceneDiagnostics.ueBridgeRequested << ','
        << sceneDiagnostics.ueBridgeManifestLoaded << ','
        << sceneDiagnostics.ueBridgeSceneFound << ','
        << sceneDiagnostics.ueBridgeExportedSceneReady << ','
        << sceneDiagnostics.ueBridgeMeshInstanceCount << ','
        << sceneDiagnostics.ueBridgeMeshInstanceLoadedCount << ','
        << sceneDiagnostics.ueBridgeMeshExportReadyCount << ','
        << sceneDiagnostics.ueBridgeMeshExportMissingCount << ','
        << sceneDiagnostics.ueBridgeManifestMeshExportReadyCount << ','
        << sceneDiagnostics.ueBridgeManifestMeshExportMissingCount << ','
        << sceneDiagnostics.ueBridgeCameraCount << ','
        << sceneDiagnostics.ueBridgeCameraApplied << ','
        << sceneDiagnostics.ueBridgeLightCount << ','
        << sceneDiagnostics.ueBridgeLightsApplied << ','
        << sceneDiagnostics.ueBridgeSkyLightCount << ','
        << sceneDiagnostics.ueBridgeSkyLightApplied << ','
        << sceneDiagnostics.ueBridgeReferenceCaptureCount << ','
        << sceneDiagnostics.ueBridgeVisualParityReady << ','
        << sceneDiagnostics.ueBridgeBlockedMissingManifest << ','
        << sceneDiagnostics.ueBridgeBlockedSceneMissing << ','
        << sceneDiagnostics.ueBridgeBlockedNoMeshInstances << ','
        << sceneDiagnostics.ueBridgeBlockedMeshExports << ','
        << sceneDiagnostics.ueBridgeBlockedMeshLoads << ','
        << sceneDiagnostics.ueBridgeBlockedCamera << ','
        << sceneDiagnostics.ueBridgeBlockedLights << ','
        << sceneDiagnostics.ueBridgeBlockedReferenceCapture << ','
        << sceneDiagnostics.runtimeImportModelRequested << ','
        << sceneDiagnostics.runtimeImportModelLoaded << ','
        << sceneDiagnostics.runtimeImportCacheHit << ','
        << sceneDiagnostics.runtimeImportMeshCount << ','
        << sceneDiagnostics.runtimeImportMaterialCount << ','
        << sceneDiagnostics.runtimeImportNodeCount << ','
        << sceneDiagnostics.runtimeImportBoneNodeCount << ','
        << sceneDiagnostics.runtimeImportAnimationChannelBoundCount << ','
        << sceneDiagnostics.runtimeImportAnimationChannelUnboundCount << ','
        << sceneDiagnostics.runtimeImportBoneNameMatchedNodeCount << ','
        << sceneDiagnostics.runtimeImportBoneNameUnmatchedCount << ','
        << sceneDiagnostics.runtimeImportAnimationCount << ','
        << sceneDiagnostics.runtimeImportAnimationChannelCount << ','
        << sceneDiagnostics.runtimeImportAnimationPositionKeyCount << ','
        << sceneDiagnostics.runtimeImportAnimationRotationKeyCount << ','
        << sceneDiagnostics.runtimeImportAnimationScaleKeyCount << ','
        << sceneDiagnostics.runtimeImportAnimationKeyCount << ','
        << sceneDiagnostics.runtimeImportMaxAnimationKeysPerChannel << ','
        << sceneDiagnostics.runtimeImportPoseSampledClipCount << ','
        << sceneDiagnostics.runtimeImportPoseSampledChannelCount << ','
        << sceneDiagnostics.runtimeImportPoseSampledNodeCount << ','
        << sceneDiagnostics.runtimeImportPoseAnimatedNodeCount << ','
        << sceneDiagnostics.runtimeImportPoseBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportPosePreviousBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportPoseChangedBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportPoseBonePaletteReady << ','
        << sceneDiagnostics.runtimeImportPoseCarrierBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportPoseCarrierPreviousBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportPoseCarrierChangedBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportPoseCarrierReady << ','
        << sceneDiagnostics.runtimeImportRendererPosePaletteRegistered << ','
        << sceneDiagnostics.runtimeImportRendererPosePaletteBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportRendererPosePalettePreviousBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportRendererPosePaletteChangedBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportRendererPosePaletteReady << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteBufferAllocated << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteBufferUploaded << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorInfoReady << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorSetAllocated << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorSetWritten << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorSetReady << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorBinding << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorRangeBytes << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteBufferBytes << ','
        << sceneDiagnostics.runtimeImportGpuPosePaletteCurrentEntryCount << ','
        << sceneDiagnostics.runtimeImportGpuPosePalettePreviousEntryCount << ','
        << sceneDiagnostics.runtimeImportMeshWithBonesCount << ','
        << sceneDiagnostics.runtimeImportBoneCount << ','
        << sceneDiagnostics.runtimeImportSkinnedVertexCount << ','
        << sceneDiagnostics.runtimeImportBoneInfluenceCount << ','
        << sceneDiagnostics.runtimeImportMaxBoneInfluencesPerVertex << ','
        << sceneDiagnostics.runtimeImportSkinnedVertexAttributeCount << ','
        << sceneDiagnostics.runtimeImportBoneAttributeInfluenceCount << ','
        << sceneDiagnostics.runtimeImportMaxBoneAttributeInfluencesPerVertex << ','
        << sceneDiagnostics.runtimeImportBoneInfluenceOverflowCount << ','
        << sceneDiagnostics.runtimeImportSkinnedVertexAttributeReady << ','
        << sceneDiagnostics.runtimeImportSkinnedAnimationSpaceReady << ','
        << sceneDiagnostics.runtimeImportSkinnedAnimationSpaceBlockerMask << ','
        << sceneDiagnostics.runtimeImportSkinnedAnimationRenderableBound << ','
        << sceneDiagnostics.runtimeImportSkinnedAnimationSupportReady << ','
        << sceneDiagnostics.runtimeImportSkinnedAnimationSupportBlockerMask << ','
        << sceneDiagnostics.runtimeImportAnimationDiagnosticPoseOnly << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackReady << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackCandidateModelCount << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackReadyModelCount << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackFrameCount << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackLoopWrapCount << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackPreviousPoseCollapsedCount << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackChangedBonePaletteEntryCount << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackRendererPaletteReady << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackGpuUploadReady << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackBlockerMask << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackClockMode << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackPreviousTimeTicks << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackCurrentTimeTicks << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackPreviousAbsoluteSeconds << ','
        << sceneDiagnostics.runtimeImportAnimationPlaybackCurrentAbsoluteSeconds << ','
        << skinnedVertexAttributes.strideBytes << ','
        << skinnedVertexAttributes.boneIndicesLocation << ','
        << skinnedVertexAttributes.boneWeightsLocation << ','
        << skinnedVertexAttributes.boneIndicesOffset << ','
        << skinnedVertexAttributes.boneWeightsOffset << ','
        << skinnedVertexAttributes.pathReady << ','
        << sceneDiagnostics.runtimeImportSkinnedAnimationUnsupported << ','
        << sceneDiagnostics.benchmarkCameraMotionTimeSeconds << ','
        << sceneDiagnostics.benchmarkObjectMotionTimeSeconds << ','
        << stats.renderDebug.forwardView << ','
        << stats.renderDebug.deferredPbrDebugView << ','
        << stats.renderDebug.usesDeferredHdrComposite << ','
        << stats.renderDebug.temporalReconstructionBypassed << ','
        << stats.renderDebug.lightingEnergyViewEnabled << ','
        << cpu.totalFrameMs << ','
        << cpu.waitAcquireMs << ','
        << cpu.imguiMs << ','
        << cpu.pickingMs << ','
        << cpu.queueBuildMs << ','
        << cpu.uniformUpdateMs << ','
        << cpu.commandRecordMs << ','
        << cpu.submitPresentMs << ','
        << (gpu.available ? 1 : 0) << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.totalRecordedMs);
    m_Csv << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.shadowMs);
    m_Csv << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.mainMs);
    m_Csv << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.overlayMs);
    m_Csv << ',';
    WriteGpuValue(m_Csv, gpu.available, gpu.imguiMs);
    m_Csv << ','
        << draw.mainDraws << ','
        << draw.gBufferDraws << ','
        << draw.overlayDraws << ','
        << draw.shadowDraws << ','
        << draw.hybridDeferredOpaqueDraws << ','
        << draw.hybridForwardTransparentDraws << ','
        << draw.hybridForwardSpecialDraws << ','
        << draw.hybridWeightedTranslucencyDraws << ','
        << draw.hybridWeightedTranslucencySortOps << ','
        << draw.hybridWeightedTranslucencySortedTransparentDraws << ','
        << draw.hybridForwardResidualDraws << ','
        << draw.hybridForwardResidualSortOps << ','
        << draw.hybridForwardResidualSortedTransparentDraws << ','
        << draw.hybridForwardResidualStableSpecialDraws << ','
        << draw.mainTriangles << ','
        << draw.gBufferTriangles << ','
        << draw.overlayTriangles << ','
        << draw.shadowTriangles << ','
        << draw.hybridDeferredOpaqueTriangles << ','
        << draw.hybridWeightedTranslucencyTriangles << ','
        << draw.hybridForwardResidualTriangles << ','
        << draw.matrixRecalculations << ','
        << draw.mainBoundsCacheHits << ','
        << draw.mainBoundsCacheMisses << ','
        << draw.mainCommandCacheHits << ','
        << draw.mainCommandCacheMisses << ','
        << draw.mainVisibilityCacheHits << ','
        << draw.mainVisibilityCacheMisses << ','
        << draw.mainQueueCacheHits << ','
        << draw.mainQueueCacheMisses << ','
        << draw.overlayBoundsCacheHits << ','
        << draw.overlayBoundsCacheMisses << ','
        << draw.overlayCommandCacheHits << ','
        << draw.overlayCommandCacheMisses << ','
        << draw.overlayVisibilityCacheHits << ','
        << draw.overlayVisibilityCacheMisses << ','
        << draw.overlayQueueCacheHits << ','
        << draw.overlayQueueCacheMisses << ','
        << draw.mainInstancedDraws << ','
        << draw.mainInstancedInstances << ','
        << draw.mainInstanceBatchCacheHits << ','
        << draw.mainInstanceBatchCacheMisses << ','
        << draw.mainSkinnedConservativeBounds << ','
        << draw.shadowSkinnedConservativeBounds << ','
        << draw.mainVisible << ','
        << draw.mainCulled << ','
        << draw.overlayVisible << ','
        << draw.overlayCulled << ','
        << draw.shadowVisible << ','
        << draw.shadowCulled << ','
        << shadowCascades.configuredCount << ','
        << shadowCascades.activeCount << ','
        << shadowCascades.stableSnappingEnabled << ','
        << shadowCascades.pcfKernelRadius << ','
        << shadowCascades.pcssStrength << ','
        << shadowCascades.splitLambda << ','
        << shadowCascades.blendRatio << ','
        << shadowCascades.fadeRatio << ','
        << shadowCascades.receiverGuardRatio << ','
        << shadowCascades.contactShadowStrength << ','
        << shadowCascades.contactShadowLength << ','
        << shadowCascades.contactShadowThickness << ','
        << shadowCascades.contactShadowSteps << ','
        << shadowCascades.contactShadowJitterStrength << ','
        << shadowCascades.contactShadowEdgeFadePixels << ','
        << ssao.enabled << ','
        << ssao.strength << ','
        << ssao.radius << ','
        << ssao.bias << ','
        << ssao.sampleCount << ','
        << ssr.enabled << ','
        << ssr.colorResolveEnabled << ','
        << ssr.strength << ','
        << ssr.rayLength << ','
        << ssr.thickness << ','
        << ssr.stepCount << ','
        << ibl.quality << ','
        << ibl.requestedSource << ','
        << ibl.actualSource << ','
        << ibl.sourceFallbackReason << ','
        << ibl.cachePolicy << ','
        << ibl.cacheFallbackReason << ','
        << ibl.cacheHit << ','
        << ibl.runtimeGenerated << ','
        << ibl.sourceAssetSpecified << ','
        << ibl.sourceAssetFound << ','
        << ibl.sourceSignature << ','
        << ibl.brdfLutAllocated << ','
        << ibl.brdfLutSize << ','
        << static_cast<int>(ibl.brdfLutFormat) << ','
        << ibl.irradianceMapAllocated << ','
        << ibl.irradianceFaceSize << ','
        << static_cast<int>(ibl.irradianceFormat) << ','
        << ibl.prefilteredMapAllocated << ','
        << ibl.prefilteredFaceSize << ','
        << ibl.prefilteredMipCount << ','
        << static_cast<int>(ibl.prefilteredFormat) << ','
        << ibl.descriptorSetsBound << ','
        << ibl.shaderIntegrationEnabled << ','
        << probeGrid.allocated << ','
        << probeGrid.enabled << ','
        << probeGrid.shaderIntegrationEnabled << ','
        << probeGrid.bufferUpdates << ','
        << probeGrid.fallbackCount << ','
        << probeGrid.probeCount << ','
        << probeGrid.sizeX << ','
        << probeGrid.sizeY << ','
        << probeGrid.sizeZ << ','
        << probeGrid.vec4sPerProbe << ','
        << probeGrid.directionalLobeCount << ','
        << probeGrid.originX << ','
        << probeGrid.originY << ','
        << probeGrid.originZ << ','
        << probeGrid.spacing << ','
        << probeGrid.blendStrength << ','
        << probeGrid.fallbackReason << ','
        << probeGrid.cellCount << ','
        << probeGrid.boundsMinX << ','
        << probeGrid.boundsMinY << ','
        << probeGrid.boundsMinZ << ','
        << probeGrid.boundsMaxX << ','
        << probeGrid.boundsMaxY << ','
        << probeGrid.boundsMaxZ << ','
        << probeGrid.debugViewEnabled << ','
        << probeGrid.cellDebugViewEnabled << ','
        << bonePaletteDraw.commandCount << ','
        << bonePaletteDraw.readyCommandCount << ','
        << bonePaletteDraw.resourceCount << ','
        << bonePaletteDraw.readyResourceCount << ','
        << bonePaletteDraw.currentEntryCount << ','
        << bonePaletteDraw.previousEntryCount << ','
        << bonePaletteDraw.changedEntryCount << ','
        << bonePaletteDraw.drawPathReady << ','
        << bonePaletteDraw.descriptorCommandCount << ','
        << bonePaletteDraw.descriptorReadyCommandCount << ','
        << bonePaletteDraw.descriptorResourceCount << ','
        << bonePaletteDraw.descriptorReadyResourceCount << ','
        << bonePaletteDraw.descriptorSetIndex << ','
        << bonePaletteDraw.descriptorBinding << ','
        << bonePaletteDraw.descriptorRangeBytes << ','
        << bonePaletteDraw.descriptorPathReady << ','
        << bonePaletteDraw.shaderConsumerCommandCount << ','
        << bonePaletteDraw.shaderConsumerReadyCommandCount << ','
        << bonePaletteDraw.shaderConsumerFallbackDescriptorReady << ','
        << bonePaletteDraw.shaderConsumerPathReady << ','
        << bonePaletteDraw.shaderSkinningCommandCount << ','
        << bonePaletteDraw.shaderSkinningReadyCommandCount << ','
        << bonePaletteDraw.shaderSkinningCurrentPaletteOffset << ','
        << bonePaletteDraw.shaderSkinningCurrentEntryCount << ','
        << bonePaletteDraw.shaderSkinningPathReady << ','
        << bonePaletteDraw.shaderVelocityCommandCount << ','
        << bonePaletteDraw.shaderVelocityReadyCommandCount << ','
        << bonePaletteDraw.shaderVelocityPreviousPaletteOffset << ','
        << bonePaletteDraw.shaderVelocityPreviousEntryCount << ','
        << bonePaletteDraw.shaderVelocityPathReady << ','
        << reflectionProbe.fallbackEnabled << ','
        << reflectionProbe.diffuseIntensity << ','
        << reflectionProbe.specularIntensity << ','
        << reflectionProbe.horizonBlend << ','
        << reflectionProbe.globalIblCubemapSamplingEnabled << ','
        << reflectionProbe.sceneProbeCount << ','
        << reflectionProbe.activeProbeCount << ','
        << reflectionProbe.sceneEligibleProbeCount << ','
        << reflectionProbe.selectedProbeCount << ','
        << reflectionProbe.blendedProbeCount << ','
        << reflectionProbe.selectedCaptureSlotCount << ','
        << reflectionProbe.selectedCaptureResourceReadyCount << ','
        << reflectionProbe.selectedCaptureFallbackCount << ','
        << reflectionProbe.selectedCubemapSamplingCount << ','
        << reflectionProbe.selectedCaptureReadyMask << ','
        << reflectionProbe.selectedCaptureFallbackMask << ','
        << reflectionProbe.selectedCubemapSamplingMask << ','
        << reflectionProbe.selectedAuthoredAssetSpecifiedCount << ','
        << reflectionProbe.selectedAuthoredAssetFoundCount << ','
        << reflectionProbe.selectedAuthoredAssetMissingCount << ','
        << reflectionProbe.selectedAuthoredAssetSpecifiedMask << ','
        << reflectionProbe.selectedAuthoredAssetFoundMask << ','
        << reflectionProbe.selectedAuthoredAssetMissingMask << ','
        << reflectionProbe.capturedSceneRequestedCount << ','
        << reflectionProbe.capturedScenePlaceholderAllocatedCount << ','
        << reflectionProbe.capturedScenePlaceholderReadyCount << ','
        << reflectionProbe.capturedSceneInvalidatedCount << ','
        << reflectionProbe.capturedSceneRefreshRequestedCount << ','
        << reflectionProbe.capturedSceneCaptureBackend << ','
        << reflectionProbe.capturedSceneFaceCount << ','
        << reflectionProbe.capturedSceneFacesRendered << ','
        << reflectionProbe.capturedSceneFacesPending << ','
        << reflectionProbe.capturedSceneCapturePassCount << ','
        << reflectionProbe.capturedSceneCaptureDrawCount << ','
        << reflectionProbe.capturedSceneCaptureVisibleCount << ','
        << reflectionProbe.capturedSceneCaptureCulledCount << ','
        << reflectionProbe.capturedSceneMipGenerationCount << ','
        << reflectionProbe.capturedSceneGgxPrefilterDispatchCount << ','
        << reflectionProbe.capturedSceneGgxPrefilterSampleCount << ','
        << reflectionProbe.capturedSceneDiffuseIrradianceDispatchCount << ','
        << reflectionProbe.capturedSceneDiffuseIrradianceSampleCount << ','
        << reflectionProbe.capturedSceneDiffuseIrradianceFaceSize << ','
        << reflectionProbe.capturedSceneDirectionalShadowRequested << ','
        << reflectionProbe.capturedSceneDirectionalShadowReady << ','
        << reflectionProbe.capturedSceneDirectionalShadowPassCount << ','
        << reflectionProbe.capturedSceneDirectionalShadowDrawCount << ','
        << reflectionProbe.capturedSceneDirectionalShadowCasterCount << ','
        << reflectionProbe.capturedSceneDirectionalShadowMapSize << ','
        << reflectionProbe.capturedSceneDirectionalShadowFaceMask << ','
        << reflectionProbe.capturedSceneDirectionalShadowCameraIndependent << ','
        << reflectionProbe.capturedSceneDirectionalShadowLocalTilesSuppressed << ','
        << reflectionProbe.capturedSceneDirectionalShadowProbeSceneIndex << ','
        << reflectionProbe.capturedSceneLocalShadowRequested << ','
        << reflectionProbe.capturedSceneLocalShadowReady << ','
        << reflectionProbe.capturedSceneLocalShadowPassCount << ','
        << reflectionProbe.capturedSceneLocalShadowDrawCount << ','
        << reflectionProbe.capturedSceneLocalShadowCasterCount << ','
        << reflectionProbe.capturedSceneLocalShadowTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowPointFaceTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowSpotTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowRectTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowRequestedTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowDroppedTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowRectRequestedTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowRectMaximumTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowRectExtraSampleTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowRectBudgetLimitedSampleTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowRectDroppedTileCount << ','
        << reflectionProbe.capturedSceneLocalShadowMapTileSize << ','
        << reflectionProbe.capturedSceneLocalShadowFaceMask << ','
        << reflectionProbe.capturedSceneLocalShadowSupportedKindMask << ','
        << reflectionProbe.capturedSceneLocalShadowSuppressedKindMask << ','
        << reflectionProbe.capturedSceneLocalShadowCameraIndependent << ','
        << reflectionProbe.capturedSceneLocalShadowProbeSceneIndex << ','
        << reflectionProbe.capturedSceneShadowSnapshotBuildCount << ','
        << reflectionProbe.capturedSceneShadowSnapshotReuseFaceCount << ','
        << reflectionProbe.capturedSceneShadowSnapshotSavedDirectionalPassCount << ','
        << reflectionProbe.capturedSceneShadowSnapshotSavedLocalTilePassCount << ','
        << reflectionProbe.capturedSceneShadowSnapshotSavedLocalDrawCount << ','
        << reflectionProbe.capturedSceneShadowSnapshotBuildFaceMask << ','
        << reflectionProbe.capturedSceneShadowSnapshotReuseFaceMask << ','
        << reflectionProbe.capturedSceneShadowSnapshotProbeSceneIndex << ','
        << reflectionProbe.capturedSceneShadowSnapshotReady << ','
        << reflectionProbe.capturedSceneShadowSnapshotCameraIndependent << ','
        << reflectionProbe.capturedSceneShadowSnapshotEnabled << ','
        << reflectionProbe.capturedSceneShadowSnapshotFallbackActive << ','
        << reflectionProbe.capturedSceneLastCapturedFace << ','
        << reflectionProbe.capturedSceneRasterizedGeometry << ','
        << reflectionProbe.capturedSceneGpuResourcesAllocated << ','
        << reflectionProbe.capturedSceneGpuCaptureInProgress << ','
        << reflectionProbe.capturedSceneMipChainReady << ','
        << reflectionProbe.capturedSceneGgxPrefilterReady << ','
        << reflectionProbe.capturedSceneDiffuseIrradianceReady << ','
        << reflectionProbe.capturedSceneProbeSceneIndex << ','
        << reflectionProbe.selectedCapturedSceneMapMatchesActiveMask << ','
        << reflectionProbe.selectedCapturedSceneDuplicateActiveViewMask << ','
        << reflectionProbe.selectedCapturedSceneDiffuseIrradianceMapMatchesActiveMask << ','
        << reflectionProbe.selectedCapturedSceneDiffuseIrradianceDuplicateActiveViewMask << ','
        << reflectionProbe.capturedSceneProbeResourceCount << ','
        << reflectionProbe.capturedSceneReadyProbeCount << ','
        << reflectionProbe.capturedSceneInFlightProbeCount << ','
        << reflectionProbe.capturedSceneDistinctActiveViewCount << ','
        << reflectionProbe.capturedSceneDiffuseIrradianceReadyProbeCount << ','
        << reflectionProbe.capturedSceneDistinctActiveDiffuseIrradianceViewCount << ','
        << reflectionProbe.capturedSceneUploadCount << ','
        << reflectionProbe.capturedSceneRefreshCheckCount << ','
        << reflectionProbe.capturedSceneRefreshPerformed << ','
        << reflectionProbe.capturedSceneRefreshReason << ','
        << reflectionProbe.capturedSceneLastRefreshReason << ','
        << reflectionProbe.capturedSceneDirtyMask << ','
        << reflectionProbe.capturedSceneActiveSignature << ','
        << reflectionProbe.capturedSceneRequestedSignature << ','
        << reflectionProbe.capturedSceneRadianceSignature << ','
        << reflectionProbe.capturedSceneMembershipRevision << ','
        << reflectionProbe.capturedSceneLightRevision << ','
        << reflectionProbe.capturedSceneRenderRevision << ','
        << reflectionProbe.forcedRefreshRequested << ','
        << reflectionProbe.sceneDirtyRequested << ','
        << reflectionProbe.authoredCubemapLoadedCount << ','
        << reflectionProbe.authoredCubemapMissingCount << ','
        << reflectionProbe.authoredCubemapLoadFailedCount << ','
        << reflectionProbe.authoredCubemapUploadCount << ','
        << reflectionProbe.authoredCubemapSixFaceLoadedCount << ','
        << reflectionProbe.authoredCubemapEquirectangularLoadedCount << ','
        << reflectionProbe.authoredCubemapEquirectangularConversionCount << ','
        << reflectionProbe.authoredCubemapHdrLoadedCount << ','
        << reflectionProbe.authoredCubemapPrefilteredLoadedCount << ','
        << reflectionProbe.authoredCubemapPrefilteredUploadCount << ','
        << reflectionProbe.authoredCubemapCacheHitCount << ','
        << reflectionProbe.authoredCubemapReloadCount << ','
        << reflectionProbe.authoredCubemapRefreshCheckCount << ','
        << reflectionProbe.authoredCubemapFaceSize << ','
        << reflectionProbe.authoredCubemapMipCount << ','
        << static_cast<int>(reflectionProbe.authoredCubemapFormat) << ','
        << reflectionProbe.authoredCubemapSourceType << ','
        << reflectionProbe.authoredCubemapHdr << ','
        << reflectionProbe.authoredCubemapPrefiltered << ','
        << reflectionProbe.authoredCubemapGeneratedMipCount << ','
        << reflectionProbe.authoredCubemapPrefilterSampleCount << ','
        << reflectionProbe.authoredCubemapPrefilterMode << ','
        << reflectionProbe.authoredCubemapFilterQuality << ','
        << reflectionProbe.authoredCubemapSeamAwareFiltering << ','
        << reflectionProbe.authoredCubemapIrradianceReadyCount << ','
        << reflectionProbe.authoredCubemapIrradianceApplied << ','
        << reflectionProbe.authoredCubemapIrradianceR << ','
        << reflectionProbe.authoredCubemapIrradianceG << ','
        << reflectionProbe.authoredCubemapIrradianceB << ','
        << reflectionProbe.authoredCubemapDiffuseLobesReadyCount << ','
        << reflectionProbe.authoredCubemapDiffuseLobesApplied << ','
        << reflectionProbe.authoredCubemapDiffuseLobeCount << ','
        << reflectionProbe.selectedDiffuseLobeReadyMask << ','
        << reflectionProbe.selectedCapturedSceneDiffuseIrradianceReadyMask << ','
        << reflectionProbe.authoredCubemapDiffuseLobeEnergy << ','
        << reflectionProbe.selectedCaptureSlots[0] << ','
        << reflectionProbe.selectedCaptureSlots[1] << ','
        << reflectionProbe.selectedCaptureSlots[2] << ','
        << reflectionProbe.selectedCaptureSlots[3] << ','
        << reflectionProbe.selectedAuthoredAssetHashes[0] << ','
        << reflectionProbe.selectedAuthoredAssetHashes[1] << ','
        << reflectionProbe.selectedAuthoredAssetHashes[2] << ','
        << reflectionProbe.selectedAuthoredAssetHashes[3] << ','
        << reflectionProbe.selectedCaptureSourceTypes[0] << ','
        << reflectionProbe.selectedCaptureSourceTypes[1] << ','
        << reflectionProbe.selectedCaptureSourceTypes[2] << ','
        << reflectionProbe.selectedCaptureSourceTypes[3] << ','
        << reflectionProbe.selectedCaptureFallbackReasons[0] << ','
        << reflectionProbe.selectedCaptureFallbackReasons[1] << ','
        << reflectionProbe.selectedCaptureFallbackReasons[2] << ','
        << reflectionProbe.selectedCaptureFallbackReasons[3] << ','
        << reflectionProbe.selectedRefreshPolicies[0] << ','
        << reflectionProbe.selectedRefreshPolicies[1] << ','
        << reflectionProbe.selectedRefreshPolicies[2] << ','
        << reflectionProbe.selectedRefreshPolicies[3] << ','
        << reflectionProbe.selectedCapturedScenePlaceholderReady[0] << ','
        << reflectionProbe.selectedCapturedScenePlaceholderReady[1] << ','
        << reflectionProbe.selectedCapturedScenePlaceholderReady[2] << ','
        << reflectionProbe.selectedCapturedScenePlaceholderReady[3] << ','
        << reflectionProbe.selectedCapturedSceneInvalidated[0] << ','
        << reflectionProbe.selectedCapturedSceneInvalidated[1] << ','
        << reflectionProbe.selectedCapturedSceneInvalidated[2] << ','
        << reflectionProbe.selectedCapturedSceneInvalidated[3] << ','
        << reflectionProbe.selectedCaptureMipCounts[0] << ','
        << reflectionProbe.selectedCaptureMipCounts[1] << ','
        << reflectionProbe.selectedCaptureMipCounts[2] << ','
        << reflectionProbe.selectedCaptureMipCounts[3] << ','
        << reflectionProbe.selectedProbeIndex << ','
        << reflectionProbe.droppedProbeCount << ','
        << reflectionProbe.maxBlendWeight << ','
        << reflectionProbe.totalBlendWeight << ','
        << reflectionProbe.normalizedBlendWeightSum << ','
        << reflectionProbe.blendWeightNormalizationFallbackCount << ','
        << reflectionProbe.selectedProbeMask << ','
        << reflectionProbe.selectedBoxProjectionMask << ','
        << reflectionProbe.selectedSceneOwnedMask << ','
        << reflectionProbe.selectedPositiveInfluenceMask << ','
        << reflectionProbe.selectedBlendWeights[0] << ','
        << reflectionProbe.selectedBlendWeights[1] << ','
        << reflectionProbe.selectedBlendWeights[2] << ','
        << reflectionProbe.selectedBlendWeights[3] << ','
        << reflectionProbe.selectedNormalizedBlendWeights[0] << ','
        << reflectionProbe.selectedNormalizedBlendWeights[1] << ','
        << reflectionProbe.selectedNormalizedBlendWeights[2] << ','
        << reflectionProbe.selectedNormalizedBlendWeights[3] << ','
        << reflectionProbe.multiBlendEnabled << ','
        << reflectionProbe.localEnabled << ','
        << reflectionProbe.localSceneOwned << ','
        << reflectionProbe.localRadius << ','
        << reflectionProbe.localBoxExtentX << ','
        << reflectionProbe.localBoxExtentY << ','
        << reflectionProbe.localBoxExtentZ << ','
        << reflectionProbe.localIntensity << ','
        << reflectionProbe.localBlendStrength << ','
        << reflectionProbe.localFalloff << ','
        << reflectionProbe.localCubemapAllocated << ','
        << reflectionProbe.localCubemapFaceSize << ','
        << reflectionProbe.localCubemapMipCount << ','
        << static_cast<int>(reflectionProbe.localCubemapFormat) << ','
        << reflectionProbe.localCubemapDescriptorSetsBound << ','
        << reflectionProbe.localCubemapShaderSamplingEnabled << ','
        << reflectionProbe.localCubemapSourceType << ','
        << reflectionProbe.captureSourceType << ','
        << reflectionProbe.refreshPolicy << ','
        << reflectionProbe.captureResourceReady << ','
        << reflectionProbe.captureFallbackReason << ','
        << reflectionProbe.captureDescriptorBound << ','
        << reflectionProbe.boxProjectionEnabled << ','
        << reflectionProbe.influenceMode << ','
        << reflectionProbe.parallaxCorrectionEnabled << ','
        << heightFog.enabled << ','
        << heightFog.density << ','
        << heightFog.heightFalloff << ','
        << heightFog.startDistance << ','
        << heightFog.maxOpacity << ','
        << postProcess.bloomEnabled << ','
        << postProcess.bloomIntensity << ','
        << postProcess.bloomThreshold << ','
        << postProcess.bloomRadiusPixels << ','
        << postProcess.bloomPyramidEnabled << ','
        << postProcess.bloomPyramidMipCount << ','
        << postProcess.bloomPyramidFallbacks << ','
        << postProcess.toneMappingEnabled << ','
        << postProcess.toneMapMode << ','
        << postProcess.exposure << ','
        << postProcess.toneMapWhitePoint << ','
        << postProcess.autoExposureEnabled << ','
        << postProcess.autoExposureTargetLuminance << ','
        << postProcess.autoExposureMin << ','
        << postProcess.autoExposureMax << ','
        << postProcess.autoExposureAdaptation << ','
        << postProcess.autoExposureHistogramEnabled << ','
        << postProcess.autoExposureHistoryValid << ','
        << postProcess.autoExposureGpuExposure << ','
        << postProcess.autoExposureGpuTargetExposure << ','
        << postProcess.autoExposureGpuAverageLuminance << ','
        << postProcess.autoExposureFallbacks << ','
        << postProcess.colorGradingEnabled << ','
        << postProcess.colorGradingSaturation << ','
        << postProcess.colorGradingContrast << ','
        << postProcess.colorGradingGamma << ','
        << postProcess.colorGradingLutEnabled << ','
        << postProcess.colorGradingLutSize << ','
        << postProcess.colorGradingLutStrength << ','
        << postProcess.colorGradingLutFallbacks << ','
        << postProcess.sharpeningEnabled << ','
        << postProcess.sharpeningStrength << ','
        << postProcess.sharpeningRadiusPixels << ','
        << shadowCascades.maxDistance << ','
        << shadowCascades.nearDepth << ','
        << shadowCascades.farDepth << ','
        << shadowCascades.splitDepths[0] << ','
        << shadowCascades.splitDepths[1] << ','
        << shadowCascades.splitDepths[2] << ','
        << shadowCascades.splitDepths[3] << ','
        << shadowCascades.texelWorldSizes[0] << ','
        << shadowCascades.texelWorldSizes[1] << ','
        << shadowCascades.texelWorldSizes[2] << ','
        << shadowCascades.texelWorldSizes[3] << ','
        << shadowCascades.atlasAllocated << ','
        << shadowCascades.atlasTileSize << ','
        << shadowCascades.atlasWidth << ','
        << shadowCascades.atlasHeight << ','
        << shadowCascades.atlasTileColumns << ','
        << shadowCascades.atlasTileRows << ','
        << shadowCascades.atlasCascadeCapacity << ','
        << localShadowAtlas.allocated << ','
        << localShadowAtlas.tileSize << ','
        << localShadowAtlas.atlasWidth << ','
        << localShadowAtlas.atlasHeight << ','
        << localShadowAtlas.tileColumns << ','
        << localShadowAtlas.tileRows << ','
        << localShadowAtlas.tileCapacity << ','
        << localShadowAtlas.shadowableLocalLights << ','
        << localShadowAtlas.pointLightCount << ','
        << localShadowAtlas.spotLightCount << ','
        << localShadowAtlas.rectLightCount << ','
        << localShadowAtlas.pointFaceTiles << ','
        << localShadowAtlas.spotTiles << ','
        << localShadowAtlas.rectTiles << ','
        << localShadowAtlas.requestedTiles << ','
        << localShadowAtlas.assignedTiles << ','
        << localShadowAtlas.droppedTiles << ','
        << localShadowAtlas.recordedTilePasses << ','
        << localShadowAtlas.recordedDraws << ','
        << localShadowAtlas.recordedMeshBinds << ','
        << localShadowAtlas.cacheEligibleTiles << ','
        << localShadowAtlas.cacheHitTiles << ','
        << localShadowAtlas.cacheMissTiles << ','
        << localShadowAtlas.cacheSkippedTiles << ','
        << localShadowAtlas.cacheColdTiles << ','
        << localShadowAtlas.cacheTileLayoutChangedTiles << ','
        << localShadowAtlas.cacheLightChangedTiles << ','
        << localShadowAtlas.cacheCasterChangedTiles << ','
        << localShadowAtlas.cacheDynamicSkinnedCasterTiles << ',';
    WriteCsvString(m_Csv, localShadowAtlas.cacheReasonSummary);
    m_Csv << ','
        << localShadowAtlas.biasMin << ','
        << localShadowAtlas.biasSlope << ','
        << localShadowAtlas.pcfRadius << ','
        << localShadowAtlas.pcfKernelRadius << ','
        << localShadowAtlas.pcssStrength << ','
        << localShadowAtlas.faceBlendStrength << ','
        << localShadowAtlas.rectBiasScale << ','
        << localShadowAtlas.pointBiasMin << ','
        << localShadowAtlas.pointBiasSlope << ','
        << localShadowAtlas.pointPcfRadius << ','
        << localShadowAtlas.pointPcfKernelRadius << ','
        << localShadowAtlas.pointPcssStrength << ','
        << localShadowAtlas.spotBiasMin << ','
        << localShadowAtlas.spotBiasSlope << ','
        << localShadowAtlas.spotPcfRadius << ','
        << localShadowAtlas.spotPcfKernelRadius << ','
        << localShadowAtlas.spotPcssStrength << ','
        << localShadowAtlas.rectBiasMin << ','
        << localShadowAtlas.rectBiasSlope << ','
        << localShadowAtlas.rectPcfRadius << ','
        << localShadowAtlas.rectPcfKernelRadius << ','
        << localShadowAtlas.rectPcssStrength << ','
        << localShadowAtlas.rectShadowBaseSampleTiles << ','
        << localShadowAtlas.rectShadowMaxSampleTiles << ','
        << localShadowAtlas.rectShadowSamplePattern << ','
        << localShadowAtlas.rectShadowExtraSampleTiles << ','
        << localShadowAtlas.rectShadowBudgetLimitedSampleTiles << ','
        << localShadowAtlas.pointShadowEnabled << ','
        << localShadowAtlas.spotShadowEnabled << ','
        << localShadowAtlas.rectShadowEnabled << ','
        << localShadowAtlas.debugLightIndex << ','
        << localShadowAtlas.attributionLightIndex << ','
        << localShadowAtlas.attributionLightValid << ','
        << localShadowAtlas.attributionLightKind << ','
        << localShadowAtlas.attributionExpectedTiles << ','
        << localShadowAtlas.attributionRequestedTiles << ','
        << localShadowAtlas.attributionAssignedTiles << ','
        << localShadowAtlas.attributionDroppedTiles << ','
        << localShadowAtlas.attributionCacheHitTiles << ','
        << localShadowAtlas.attributionCacheMissTiles << ','
        << localShadowAtlas.attributionRecordedTilePasses << ','
        << localShadowAtlas.attributionRecordedDraws << ','
        << localShadowAtlas.attributionCandidateDraws << ','
        << localShadowAtlas.attributionUniqueCasters << ','
        << localShadowAtlas.attributionCasterSignature << ',';
    WriteCsvString(m_Csv, localShadowAtlas.attributionTileCandidateDraws);
    m_Csv << ',';
    WriteCsvString(m_Csv, localShadowAtlas.attributionCasterSummary);
    m_Csv << ','
        << localShadowAtlas.attributionShadowEnabled << ','
        << localShadowAtlas.attributionMatchesGenerationFilter << ','
        << weightedTranslucency.allocated << ','
        << weightedTranslucency.accumWidth << ','
        << weightedTranslucency.accumHeight << ','
        << weightedTranslucency.revealageWidth << ','
        << weightedTranslucency.revealageHeight << ','
        << static_cast<int>(weightedTranslucency.accumFormat) << ','
        << static_cast<int>(weightedTranslucency.revealageFormat) << ','
        << weightedTranslucency.renderPassAllocated << ','
        << weightedTranslucency.framebufferCount << ','
        << weightedTranslucency.clearPasses << ','
        << weightedTranslucency.draws << ','
        << weightedTranslucency.sharedLightListDraws << ','
        << weightedTranslucency.shadowReadyDraws << ','
        << weightedTranslucency.resolveDraws << ','
        << temporal.antialiasingMode << ','
        << temporal.velocityTargetAllocated << ','
        << static_cast<int>(temporal.velocityFormat) << ','
        << temporal.velocityCameraMotionEnabled << ','
        << temporal.velocityCameraMotionReady << ','
        << temporal.velocityObjectMotionReady << ','
        << temporal.velocityMaterialAuxTargetAllocated << ','
        << static_cast<int>(temporal.velocityMaterialAuxFormat) << ','
        << temporal.velocityMaterialAuxMigrated << ','
        << temporal.historyValid << ','
        << temporal.historyReset << ','
        << temporal.historyResetReason << ','
        << temporal.jitterEnabled << ','
        << temporal.jitterApplied << ','
        << temporal.jitterSequenceIndex << ','
        << temporal.jitterPixelsX << ','
        << temporal.jitterPixelsY << ','
        << temporal.jitterUvX << ','
        << temporal.jitterUvY << ','
        << temporal.velocityJitteredHistoryPolicy << ','
        << temporal.velocityPreviousJitterApplied << ','
        << temporal.previousJitterPixelsX << ','
        << temporal.previousJitterPixelsY << ','
        << temporal.previousJitterUvX << ','
        << temporal.previousJitterUvY << ','
        << temporal.taaResolveEnabled << ','
        << temporal.taaResolveConfigured << ','
        << temporal.taaResolveSuppressedForUpscaler << ','
        << temporal.taaHistoryColorTargetAllocated << ','
        << static_cast<int>(temporal.taaHistoryColorFormat) << ','
        << temporal.taaHistoryColorReady << ','
        << temporal.taaHistoryColorCopies << ','
        << temporal.taaHistoryWeight << ','
        << temporal.taaVelocityReprojectionEnabled << ','
        << temporal.taaFallbackReason << ','
        << temporal.taaDebugViewEnabled << ','
        << temporal.taaRejectionEnabled << ','
        << temporal.taaNeighborhoodClampEnabled << ','
        << temporal.taaVelocityRejectionThreshold << ','
        << temporal.taaDepthRejectionThreshold << ','
        << temporal.taaRejectionDebugViewEnabled << ','
        << temporal.taaHistoryDebugViewEnabled << ','
        << temporal.taaReprojectionDebugViewEnabled << ','
        << temporal.temporalConsumerReadinessMask << ','
        << temporal.temporalConsumerActiveMask << ','
        << temporal.temporalConsumerUnsupportedMask << ','
        << temporal.temporalConsumerSsrReady << ','
        << temporal.temporalConsumerSsrActive << ','
        << temporal.temporalConsumerGtaoReady << ','
        << temporal.temporalConsumerMotionBlurReady << ','
        << temporal.temporalConsumerDynamicResolutionReady << ','
        << temporal.temporalConsumerUpscalerReady << ','
        << temporal.renderScaleRequested << ','
        << temporal.renderScaleActive << ','
        << temporal.renderScaleApplied << ','
        << temporal.temporalUpscaleDisplayWidth << ','
        << temporal.temporalUpscaleDisplayHeight << ','
        << temporal.temporalUpscaleRequestedWidth << ','
        << temporal.temporalUpscaleRequestedHeight << ','
        << temporal.temporalUpscaleActiveWidth << ','
        << temporal.temporalUpscaleActiveHeight << ','
        << temporal.temporalUpscaleOutputAllocated << ','
        << temporal.temporalUpscaleOutputFormat << ','
        << temporal.temporalUpscaleOutputWidth << ','
        << temporal.temporalUpscaleOutputHeight << ','
        << temporal.temporalUpscalePostSourceRequested << ','
        << temporal.temporalUpscalePostSourceActive << ','
        << temporal.temporalUpscalePostSourceFallbackReason << ','
        << temporal.dynamicResolutionRequested << ','
        << temporal.dynamicResolutionEnabled << ','
        << temporal.taauRequested << ','
        << temporal.temporalUpscaleRequested << ','
        << temporal.temporalUpscaleEnabled << ','
        << temporal.temporalUpscaleInputReady << ','
        << temporal.temporalUpscaleFallbackReason << ','
        << temporal.temporalUpscaleInputReadinessMask << ','
        << temporal.temporalUpscaleRequiredInputMask << ','
        << temporal.temporalUpscaleContractReady << ','
        << temporal.temporalUpscalerPluginRequested << ','
        << temporal.temporalUpscalerPluginAvailable << ','
        << temporal.temporalUpscalerProviderKind << ','
        << temporal.temporalUpscalerPackageFallbackReason << ','
        << temporal.temporalUpscalerPackageDirectoryFound << ','
        << temporal.temporalUpscalerHeadersFound << ','
        << temporal.temporalUpscalerImportLibraryFound << ','
        << temporal.temporalUpscalerRuntimeFound << ','
        << temporal.temporalUpscalerDlssSuperResolutionSymbolsFound << ','
        << temporal.temporalUpscalerDlssFrameGenerationSymbolsFound << ','
        << temporal.temporalUpscalerDlssRayReconstructionSymbolsFound << ','
        << temporal.temporalUpscalerDlssTransformerPresetSymbolsFound << ','
        << temporal.temporalUpscalerSdkVersionMajor << ','
        << temporal.temporalUpscalerSdkVersionMinor << ','
        << temporal.temporalUpscalerSdkVersionPatch << ','
        << temporal.temporalUpscalerPackageReady << ','
        << temporal.temporalUpscalerEvaluateAdapterAvailable << ','
        << temporal.temporalUpscalerRuntimeFallbackReason << ','
        << temporal.temporalUpscalerAdapterCompiled << ','
        << temporal.temporalUpscalerInitializationAttempted << ','
        << temporal.temporalUpscalerInitialized << ','
        << temporal.temporalUpscalerInitializationResult << ','
        << temporal.temporalUpscalerCapabilityParametersReady << ','
        << temporal.temporalUpscalerCapabilityQueryResult << ','
        << temporal.temporalUpscalerFeatureRequirementsQueried << ','
        << temporal.temporalUpscalerFeatureRequirementsResult << ','
        << temporal.temporalUpscalerFeatureSupportedMask << ','
        << temporal.temporalUpscalerFeatureRequirementsSupported << ','
        << temporal.temporalUpscalerMinHardwareArchitecture << ','
        << temporal.temporalUpscalerMinOsVersion << ','
        << temporal.temporalUpscalerInstanceExtensionRequirementsQueried << ','
        << temporal.temporalUpscalerInstanceExtensionRequirementsResult << ','
        << temporal.temporalUpscalerInstanceExtensionRequirementCount << ','
        << temporal.temporalUpscalerInstanceExtensionAvailableCount << ','
        << temporal.temporalUpscalerInstanceExtensionMissingAvailableCount << ','
        << temporal.temporalUpscalerInstanceExtensionEnabledCount << ','
        << temporal.temporalUpscalerInstanceExtensionMissingEnabledCount << ','
        << temporal.temporalUpscalerInstanceExtensionRequirements << ','
        << temporal.temporalUpscalerInstanceExtensionMissingAvailable << ','
        << temporal.temporalUpscalerInstanceExtensionMissingEnabled << ','
        << temporal.temporalUpscalerDeviceExtensionRequirementsQueried << ','
        << temporal.temporalUpscalerDeviceExtensionRequirementsResult << ','
        << temporal.temporalUpscalerDeviceExtensionRequirementCount << ','
        << temporal.temporalUpscalerDeviceExtensionAvailableCount << ','
        << temporal.temporalUpscalerDeviceExtensionMissingAvailableCount << ','
        << temporal.temporalUpscalerDeviceExtensionEnabledCount << ','
        << temporal.temporalUpscalerDeviceExtensionMissingEnabledCount << ','
        << temporal.temporalUpscalerDeviceExtensionRequirements << ','
        << temporal.temporalUpscalerDeviceExtensionMissingAvailable << ','
        << temporal.temporalUpscalerDeviceExtensionMissingEnabled << ','
        << temporal.temporalUpscalerRuntimeFlavor << ','
        << temporal.temporalUpscalerRuntimePathOverridden << ','
        << temporal.temporalUpscalerRuntimePathFound << ','
        << temporal.temporalUpscalerRuntimePath << ','
        << temporal.temporalUpscalerRuntimeDllFound << ','
        << temporal.temporalUpscalerRuntimeDllSizeBytes << ','
        << temporal.temporalUpscalerRuntimeDllHash << ','
        << temporal.temporalUpscalerDlssSuperResolutionSupported << ','
        << temporal.temporalUpscalerNeedsUpdatedDriver << ','
        << temporal.temporalUpscalerMinDriverVersionMajor << ','
        << temporal.temporalUpscalerMinDriverVersionMinor << ','
        << temporal.temporalUpscalerFeatureInitResult << ','
        << temporal.temporalUpscalerDlssQualityMode << ','
        << temporal.temporalUpscalerDlssRecommendedPreset << ','
        << temporal.temporalUpscalerOptimalSettingsQueried << ','
        << temporal.temporalUpscalerOptimalSettingsResult << ','
        << temporal.temporalUpscalerOptimalRenderWidth << ','
        << temporal.temporalUpscalerOptimalRenderHeight << ','
        << temporal.temporalUpscalerMinRenderWidth << ','
        << temporal.temporalUpscalerMinRenderHeight << ','
        << temporal.temporalUpscalerMaxRenderWidth << ','
        << temporal.temporalUpscalerMaxRenderHeight << ','
        << temporal.temporalUpscalerSharpness << ','
        << temporal.temporalUpscalerEvaluateRequested << ','
        << temporal.temporalUpscalerEvaluateAttempted << ','
        << temporal.temporalUpscalerEvaluateFallbackReason << ','
        << temporal.temporalUpscalerEvaluateParametersAllocated << ','
        << temporal.temporalUpscalerEvaluateParameterAllocationResult << ','
        << temporal.temporalUpscalerFeatureCreateAttempted << ','
        << temporal.temporalUpscalerFeatureCreated << ','
        << temporal.temporalUpscalerFeatureCreateResult << ','
        << temporal.temporalUpscalerFeatureRecreated << ','
        << temporal.temporalUpscalerFeatureRecreationReason << ','
        << temporal.temporalUpscalerDlssEvaluateAttempted << ','
        << temporal.temporalUpscalerDlssEvaluateResult << ','
        << temporal.temporalUpscalerDlssOutputReady << ','
        << temporal.temporalUpscalerDlssRenderWidth << ','
        << temporal.temporalUpscalerDlssRenderHeight << ','
        << temporal.temporalUpscalerDlssOutputWidth << ','
        << temporal.temporalUpscalerDlssOutputHeight << ','
        << temporal.temporalUpscalerDlssCreateFlags << ','
        << temporal.temporalUpscalerDlssCreateFlagIsHdr << ','
        << temporal.temporalUpscalerDlssCreateFlagMvLowRes << ','
        << temporal.temporalUpscalerDlssCreateFlagMvJittered << ','
        << temporal.temporalUpscalerDlssCreateFlagDepthInverted << ','
        << temporal.temporalUpscalerDlssCreateFlagAutoExposure << ','
        << temporal.temporalUpscalerDlssInputColorFormat << ','
        << temporal.temporalUpscalerDlssInputDepthFormat << ','
        << temporal.temporalUpscalerDlssInputMotionVectorFormat << ','
        << temporal.temporalUpscalerDlssInputColorWidth << ','
        << temporal.temporalUpscalerDlssInputColorHeight << ','
        << temporal.temporalUpscalerDlssInputDepthWidth << ','
        << temporal.temporalUpscalerDlssInputDepthHeight << ','
        << temporal.temporalUpscalerDlssInputMotionVectorWidth << ','
        << temporal.temporalUpscalerDlssInputMotionVectorHeight << ','
        << temporal.temporalUpscalerDlssInputDepthAspectMask << ','
        << temporal.temporalUpscalerDlssInputMotionVectorAspectMask << ','
        << temporal.temporalUpscalerDlssInputDepthMatchesRenderExtent << ','
        << temporal.temporalUpscalerDlssInputMotionVectorMatchesRenderExtent << ','
        << temporal.temporalUpscalerDlssMotionVectorScalePixelSpace << ','
        << temporal.temporalUpscalerDlssMotionVectorScaleUnitSpace << ','
        << temporal.temporalUpscalerDlssMotionVectorScaleMatchesRenderExtent << ','
        << temporal.temporalUpscalerDlssReset << ','
        << temporal.temporalUpscalerDlssJitterOffsetX << ','
        << temporal.temporalUpscalerDlssJitterOffsetY << ','
        << temporal.temporalUpscalerDlssMotionVectorScaleX << ','
        << temporal.temporalUpscalerDlssMotionVectorScaleY << ','
        << temporal.temporalUpscalerDlssEvaluateSharpness << ','
        << temporal.temporalUpscalerDlssQualityGateRequested << ','
        << temporal.temporalUpscalerDlssQualityGateReady << ','
        << temporal.temporalUpscalerDlssQualityGateFallbackReason << ','
        << temporal.temporalUpscalerDlssQualityRequiredMask << ','
        << temporal.temporalUpscalerDlssQualityReadyMask << ','
        << temporal.temporalUpscalerDlssQualityBlockerMask << ','
        << temporal.temporalUpscalerDlssQualityEvaluateOutputReady << ','
        << temporal.temporalUpscalerDlssQualityCameraMotionReady << ','
        << temporal.temporalUpscalerDlssQualityObjectMotionReady << ','
        << temporal.temporalUpscalerDlssQualitySceneContentMotionSupported << ','
        << temporal.temporalUpscalerDlssQualityReactiveMaskReady << ','
        << temporal.temporalUpscalerDlssQualityTransparencyMaskReady << ','
        << temporal.temporalUpscalerDlssQualityExposurePolicyReady << ','
        << temporal.temporalUpscalerDlssQualityPostOrderingReady << ','
        << temporal.temporalUpscalerDlssQualityReferenceBaselineReady << ','
        << binds.mainMaterialBinds << ','
        << binds.mainMeshBinds << ','
        << binds.gBufferMaterialBinds << ','
        << binds.gBufferMeshBinds << ','
        << binds.mainBonePaletteDescriptorBinds << ','
        << binds.gBufferBonePaletteDescriptorBinds << ','
        << binds.bonePaletteDescriptorBinds << ','
        << binds.gBufferBonePaletteFallbackDescriptorBinds << ','
        << binds.bonePaletteFallbackDescriptorBinds << ','
        << binds.deferredLightingDraws << ','
        << binds.deferredLightingFrameBinds << ','
        << binds.deferredLightingGBufferBinds << ','
        << binds.deferredPbrDebugDraws << ','
        << binds.deferredPbrDebugFrameBinds << ','
        << binds.deferredPbrDebugGBufferBinds << ','
        << binds.hdrCompositeDraws << ','
        << binds.hdrCompositeFrameBinds << ','
        << binds.hdrCompositeTextureBinds << ','
        << binds.gBufferDebugDraws << ','
        << binds.gBufferDebugFrameBinds << ','
        << binds.gBufferDebugTextureBinds << ','
        << binds.deferredShadowDebugDraws << ','
        << binds.deferredShadowDebugFrameBinds << ','
        << binds.deferredShadowDebugTextureBinds << ','
        << binds.shadowCascadeDebugDraws << ','
        << binds.shadowCascadeDebugFrameBinds << ','
        << binds.shadowCascadeDebugTextureBinds << ','
        << binds.localShadowAtlasDebugDraws << ','
        << binds.localShadowAtlasDebugFrameBinds << ','
        << binds.localShadowAtlasDebugTextureBinds << ','
        << binds.localShadowVisibilityDebugDraws << ','
        << binds.localShadowVisibilityDebugFrameBinds << ','
        << binds.localShadowVisibilityDebugTextureBinds << ','
        << binds.localShadowFaceDebugDraws << ','
        << binds.localShadowFaceDebugFrameBinds << ','
        << binds.localShadowFaceDebugTextureBinds << ','
        << binds.contactShadowDebugDraws << ','
        << binds.contactShadowDebugFrameBinds << ','
        << binds.contactShadowDebugGBufferBinds << ','
        << binds.ssaoDebugDraws << ','
        << binds.ssaoDebugFrameBinds << ','
        << binds.ssaoDebugGBufferBinds << ','
        << binds.ssrDebugDraws << ','
        << binds.ssrDebugFrameBinds << ','
        << binds.ssrDebugGBufferBinds << ','
        << binds.reflectionProbeDebugDraws << ','
        << binds.reflectionProbeDebugFrameBinds << ','
        << binds.reflectionProbeDebugGBufferBinds << ','
        << binds.heightFogDebugDraws << ','
        << binds.heightFogDebugFrameBinds << ','
        << binds.heightFogDebugGBufferBinds << ','
        << binds.probeGridDebugDraws << ','
        << binds.probeGridDebugFrameBinds << ','
        << binds.probeGridDebugGBufferBinds << ','
        << binds.probeGridCellDebugDraws << ','
        << binds.probeGridCellDebugFrameBinds << ','
        << binds.probeGridCellDebugGBufferBinds << ','
        << binds.bloomDebugDraws << ','
        << binds.bloomDebugFrameBinds << ','
        << binds.bloomDebugTextureBinds << ','
        << binds.bloomDownsampleDraws << ','
        << binds.bloomDownsampleFrameBinds << ','
        << binds.bloomDownsampleTextureBinds << ','
        << binds.bloomUpsampleDraws << ','
        << binds.bloomUpsampleFrameBinds << ','
        << binds.bloomUpsampleTextureBinds << ','
        << binds.toneMappingDebugDraws << ','
        << binds.toneMappingDebugFrameBinds << ','
        << binds.toneMappingDebugTextureBinds << ','
        << binds.autoExposureDebugDraws << ','
        << binds.autoExposureDebugFrameBinds << ','
        << binds.autoExposureDebugTextureBinds << ','
        << binds.colorGradingDebugDraws << ','
        << binds.colorGradingDebugFrameBinds << ','
        << binds.colorGradingDebugTextureBinds << ','
        << binds.sharpeningDebugDraws << ','
        << binds.sharpeningDebugFrameBinds << ','
        << binds.sharpeningDebugTextureBinds << ','
        << binds.lightTileCullComputeDispatches << ','
        << binds.lightTileCullComputeFrameBinds << ','
        << binds.lightTileCullComputeGroupsX << ','
        << binds.lightTileCullComputeGroupsY << ','
        << binds.autoExposureHistogramDispatches << ','
        << binds.autoExposureHistogramFrameBinds << ','
        << binds.autoExposureHistogramTextureBinds << ','
        << binds.autoExposureHistogramGroupsX << ','
        << binds.autoExposureHistogramGroupsY << ','
        << binds.depthCopyOps << ','
        << binds.depthPrefillDraws << ','
        << binds.depthPrefillMeshBinds << ','
        << binds.weightedTranslucencyClearPasses << ','
        << binds.weightedTranslucencyDraws << ','
        << binds.weightedTranslucencySharedLightListDraws << ','
        << binds.weightedTranslucencyShadowReadyDraws << ','
        << binds.weightedTranslucencyMaterialBinds << ','
        << binds.weightedTranslucencyMeshBinds << ','
        << binds.weightedTranslucencyResolveDraws << ','
        << binds.weightedTranslucencyResolveFrameBinds << ','
        << binds.weightedTranslucencyResolveTextureBinds << ','
        << binds.weightedTranslucencyDebugDraws << ','
        << binds.weightedTranslucencyDebugFrameBinds << ','
        << binds.weightedTranslucencyDebugTextureBinds << ','
        << binds.weightedTranslucencyAlphaReferenceMismatchDraws << ','
        << binds.weightedTranslucencyVelocityDraws << ','
        << binds.weightedTranslucencyVelocityMaterialBinds << ','
        << binds.weightedTranslucencyVelocityMeshBinds << ','
        << binds.dlssMaskDraws << ','
        << binds.dlssMaskWeightedTranslucencyDraws << ','
        << binds.dlssMaskForwardResidualDraws << ','
        << binds.dlssMaskMaterialBinds << ','
        << binds.dlssMaskMeshBinds << ','
        << binds.forwardResidualAlphaReferenceEnabled << ','
        << binds.forwardResidualDraws << ','
        << binds.forwardResidualFrameBinds << ','
        << binds.forwardResidualSharedLightListDraws << ','
        << binds.forwardResidualMaterialBinds << ','
        << binds.forwardResidualMeshBinds << ','
        << binds.forwardResidualVelocityDraws << ','
        << binds.forwardResidualVelocityMaterialBinds << ','
        << binds.forwardResidualVelocityMeshBinds << ','
        << binds.overlayMaterialBinds << ','
        << binds.overlayMeshBinds << ','
        << binds.shadowMeshBinds << ','
        << binds.shadowCascadeAtlasPasses << ','
        << binds.shadowCascadeAtlasDraws << ','
        << binds.shadowCascadeAtlasMeshBinds << ','
        << binds.localShadowAtlasPasses << ','
        << binds.localShadowAtlasDraws << ','
        << binds.localShadowAtlasMeshBinds << ','
        << binds.localShadowResolveEnabled << ','
        << binds.shadowCascadeBufferUpdates << ','
        << binds.localShadowBufferUpdates << ','
        << binds.frameLightConstantUpdates << ','
        << binds.frameLightBufferUpdates << ','
        << binds.frameLightTotalCount << ','
        << binds.frameDirectionalLightCount << ','
        << binds.frameLocalLightCount << ','
        << binds.frameRectLightCount << ','
        << binds.frameLightTileSize << ','
        << binds.frameLightTileCountX << ','
        << binds.frameLightTileCountY << ','
        << binds.frameLightTileCount << ','
        << binds.frameLightTileAssignments << ','
        << binds.frameLightTileAssignmentCapacity << ','
        << binds.frameLightTileOverflowAssignments << ','
        << binds.frameLightTileOverflowCapacity << ','
        << binds.frameLightTileOverflowTiles << ','
        << binds.frameLightTileOverflowDropped << ','
        << binds.frameLightTileAssignmentFallbacks << ','
        << binds.frameLightTileGpuReadbackValid << ','
        << binds.frameLightTileGpuSaturatedTiles << ','
        << binds.frameLightTileGpuMaxCandidates << ','
        << binds.frameLightTileGpuRawCandidates << ','
        << binds.frameLightTileGpuOverflowTiles << ','
        << binds.frameLightTileGpuOverflowDroppedTiles << ','
        << binds.frameLightTileGpuOverflowStored << ','
        << binds.frameLightTileGpuOverflowDropped << ','
        << binds.frameMaterialBufferUpdates << ','
        << binds.frameMaterialCount << ','
        << binds.frameMaterialCapacity << ','
        << binds.frameMaterialOverflowCount << ','
        << binds.frameMaterialOpaqueCount << ','
        << binds.frameMaterialTransparentCount << ','
        << binds.frameMaterialForwardSpecialCount << ','
        << binds.frameMaterialEmissiveHintCount << ','
        << binds.frameMaterialSpecularHintCount << ','
        << binds.frameMaterialSpecularTextureCount << ','
        << binds.frameMaterialAlphaMaskCount << ','
        << binds.frameMaterialAlphaBlendCount << ','
        << binds.frameMaterialUvTransformCount << ','
        << binds.frameMaterialDoubleSidedCount << ','
        << binds.frameMaterialClearcoatCount << ','
        << binds.frameMaterialClearcoatTextureCount << ','
        << binds.frameMaterialClearcoatRoughnessTextureCount << ','
        << binds.frameMaterialTransmissionCount << ','
        << binds.frameMaterialTransmissionTextureCount << ','
        << binds.frameMaterialVolumeCount << ','
        << binds.frameMaterialOpacityTextureCount << ','
        << binds.frameMaterialTexturedCount << ','
        << binds.frameMaterialTextureMipLodBias << ','
        << binds.mainInstanceBufferUploads << ','
        << binds.mainInstanceBufferUploadSkips << ','
        << binds.pushConstantUpdates << ','
        << binds.pushConstantBytes << '\n';

    ++m_CapturedFrames;
    if (m_CapturedFrames >= m_Config.captureFrames) {
        m_StopRequested = true;
    }
}

void BenchmarkRecorder::OpenCsv() {
    const std::filesystem::path parentPath = m_Config.csvPath.parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath);
    }

    m_Csv.open(m_Config.csvPath, std::ios::out | std::ios::trunc);
    if (!m_Csv) {
        throw std::runtime_error("Failed to open benchmark CSV: " + m_Config.csvPath.string());
    }
}

void BenchmarkRecorder::WriteHeader() {
    m_Csv
        << "sample_frame,rendered_frame,elapsed_seconds,"
        << "framegraph_active_passes,framegraph_roadmap_passes,"
        << "framegraph_physical_resources,framegraph_planned_resources,"
        << "framegraph_validation_issues,"
        << "framegraph_validation_unnamed_passes,"
        << "framegraph_validation_duplicate_pass_ids,"
        << "framegraph_validation_unnamed_resources,"
        << "framegraph_validation_duplicate_resource_ids,"
        << "framegraph_validation_missing_resource_refs,"
        << "framegraph_validation_read_before_first_write,"
        << "framegraph_validation_unused_physical_resources,"
        << "framegraph_validation_write_only_roadmap_resources,"
        << "framegraph_validation_active_writes_planned_resources,"
        << "framegraph_read_refs,framegraph_write_refs,"
        << "framegraph_read_sampled_refs,framegraph_read_attachment_refs,"
        << "framegraph_write_color_refs,framegraph_write_depth_refs,"
        << "framegraph_write_storage_refs,framegraph_present_refs,"
        << "framegraph_unstructured_read_tokens,framegraph_unstructured_write_tokens,"
        << "framegraph_dependencies,framegraph_read_after_write_dependencies,"
        << "framegraph_write_after_write_dependencies,"
        << "framegraph_used_resources,framegraph_unused_resources,"
        << "framegraph_read_only_resources,framegraph_write_only_resources,"
        << "framegraph_read_write_resources,"
        << "framegraph_barrier_transitions,framegraph_barrier_image_transitions,"
        << "framegraph_barrier_buffer_transitions,"
        << "framegraph_barrier_layout_transitions,"
        << "framegraph_barrier_queue_transfers,"
        << "framegraph_barrier_read_after_write_transitions,"
        << "framegraph_barrier_write_after_write_transitions,"
        << "framegraph_barrier_planned_bridge_transitions,"
        << "framegraph_executed_barriers,"
        << "framegraph_barrier_fallbacks,"
        << "framegraph_barrier_mismatches,"
        << "ue_bridge_requested,ue_bridge_manifest_loaded,ue_bridge_scene_found,"
        << "ue_bridge_exported_scene_ready,ue_bridge_mesh_instance_count,"
        << "ue_bridge_mesh_instance_loaded_count,"
        << "ue_bridge_mesh_export_ready_count,ue_bridge_mesh_export_missing_count,"
        << "ue_bridge_manifest_mesh_export_ready_count,"
        << "ue_bridge_manifest_mesh_export_missing_count,"
        << "ue_bridge_camera_count,ue_bridge_camera_applied,"
        << "ue_bridge_light_count,ue_bridge_lights_applied,"
        << "ue_bridge_sky_light_count,ue_bridge_sky_light_applied,"
        << "ue_bridge_reference_capture_count,ue_bridge_visual_parity_ready,"
        << "ue_bridge_blocked_missing_manifest,ue_bridge_blocked_scene_missing,"
        << "ue_bridge_blocked_no_mesh_instances,"
        << "ue_bridge_blocked_mesh_exports,ue_bridge_blocked_mesh_loads,"
        << "ue_bridge_blocked_camera,"
        << "ue_bridge_blocked_lights,ue_bridge_blocked_reference_capture,"
        << "runtime_import_model_requested,runtime_import_model_loaded,"
        << "runtime_import_cache_hit,runtime_import_mesh_count,"
        << "runtime_import_material_count,runtime_import_node_count,"
        << "runtime_import_bone_node_count,"
        << "runtime_import_animation_channel_bound_count,"
        << "runtime_import_animation_channel_unbound_count,"
        << "runtime_import_bone_name_matched_node_count,"
        << "runtime_import_bone_name_unmatched_count,"
        << "runtime_import_animation_count,"
        << "runtime_import_animation_channel_count,"
        << "runtime_import_animation_position_key_count,"
        << "runtime_import_animation_rotation_key_count,"
        << "runtime_import_animation_scale_key_count,"
        << "runtime_import_animation_key_count,"
        << "runtime_import_max_animation_keys_per_channel,"
        << "runtime_import_pose_sampled_clip_count,"
        << "runtime_import_pose_sampled_channel_count,"
        << "runtime_import_pose_sampled_node_count,"
        << "runtime_import_pose_animated_node_count,"
        << "runtime_import_pose_bone_palette_entry_count,"
        << "runtime_import_pose_previous_bone_palette_entry_count,"
        << "runtime_import_pose_changed_bone_palette_entry_count,"
        << "runtime_import_pose_bone_palette_ready,"
        << "runtime_import_pose_carrier_bone_palette_entry_count,"
        << "runtime_import_pose_carrier_previous_bone_palette_entry_count,"
        << "runtime_import_pose_carrier_changed_bone_palette_entry_count,"
        << "runtime_import_pose_carrier_ready,"
        << "runtime_import_renderer_pose_palette_registered,"
        << "runtime_import_renderer_pose_palette_bone_palette_entry_count,"
        << "runtime_import_renderer_pose_palette_previous_bone_palette_entry_count,"
        << "runtime_import_renderer_pose_palette_changed_bone_palette_entry_count,"
        << "runtime_import_renderer_pose_palette_ready,"
        << "runtime_import_gpu_pose_palette_buffer_allocated,"
        << "runtime_import_gpu_pose_palette_buffer_uploaded,"
        << "runtime_import_gpu_pose_palette_descriptor_info_ready,"
        << "runtime_import_gpu_pose_palette_descriptor_set_allocated,"
        << "runtime_import_gpu_pose_palette_descriptor_set_written,"
        << "runtime_import_gpu_pose_palette_descriptor_set_ready,"
        << "runtime_import_gpu_pose_palette_descriptor_binding,"
        << "runtime_import_gpu_pose_palette_descriptor_range_bytes,"
        << "runtime_import_gpu_pose_palette_buffer_bytes,"
        << "runtime_import_gpu_pose_palette_current_entry_count,"
        << "runtime_import_gpu_pose_palette_previous_entry_count,"
        << "runtime_import_mesh_with_bones_count,runtime_import_bone_count,"
        << "runtime_import_skinned_vertex_count,"
        << "runtime_import_bone_influence_count,"
        << "runtime_import_max_bone_influences_per_vertex,"
        << "runtime_import_skinned_vertex_attribute_count,"
        << "runtime_import_bone_attribute_influence_count,"
        << "runtime_import_max_bone_attribute_influences_per_vertex,"
        << "runtime_import_bone_influence_overflow_count,"
        << "runtime_import_skinned_vertex_attribute_ready,"
        << "runtime_import_skinned_animation_space_ready,"
        << "runtime_import_skinned_animation_space_blocker_mask,"
        << "runtime_import_skinned_animation_renderable_bound,"
        << "runtime_import_skinned_animation_support_ready,"
        << "runtime_import_skinned_animation_support_blocker_mask,"
        << "runtime_import_animation_diagnostic_pose_only,"
        << "runtime_import_animation_playback_ready,"
        << "runtime_import_animation_playback_candidate_model_count,"
        << "runtime_import_animation_playback_ready_model_count,"
        << "runtime_import_animation_playback_frame_count,"
        << "runtime_import_animation_playback_loop_wrap_count,"
        << "runtime_import_animation_playback_previous_pose_collapsed_count,"
        << "runtime_import_animation_playback_changed_bone_palette_entry_count,"
        << "runtime_import_animation_playback_renderer_palette_ready,"
        << "runtime_import_animation_playback_gpu_upload_ready,"
        << "runtime_import_animation_playback_blocker_mask,"
        << "runtime_import_animation_playback_clock_mode,"
        << "runtime_import_animation_playback_previous_time_ticks,"
        << "runtime_import_animation_playback_current_time_ticks,"
        << "runtime_import_animation_playback_previous_absolute_seconds,"
        << "runtime_import_animation_playback_current_absolute_seconds,"
        << "renderer_skinned_vertex_attribute_stride_bytes,"
        << "renderer_skinned_vertex_attribute_bone_indices_location,"
        << "renderer_skinned_vertex_attribute_bone_weights_location,"
        << "renderer_skinned_vertex_attribute_bone_indices_offset,"
        << "renderer_skinned_vertex_attribute_bone_weights_offset,"
        << "renderer_skinned_vertex_attribute_path_ready,"
        << "runtime_import_skinned_animation_unsupported,"
        << "benchmark_camera_motion_time_seconds,"
        << "benchmark_object_motion_time_seconds,"
        << "render_debug_forward_view,render_debug_deferred_pbr_view,"
        << "render_debug_uses_deferred_hdr_composite,"
        << "render_debug_temporal_reconstruction_bypassed,"
        << "render_debug_lighting_energy_view_enabled,"
        << "cpu_total_ms,cpu_wait_acquire_ms,cpu_imgui_ms,cpu_picking_ms,"
        << "cpu_queue_build_ms,cpu_uniform_update_ms,cpu_command_record_ms,cpu_submit_present_ms,"
        << "gpu_available,gpu_total_recorded_ms,gpu_shadow_ms,gpu_main_ms,gpu_overlay_ms,gpu_imgui_ms,"
        << "main_draws,gbuffer_draws,overlay_draws,shadow_draws,"
        << "hybrid_deferred_opaque_draws,hybrid_forward_transparent_draws,"
        << "hybrid_forward_special_draws,hybrid_weighted_translucency_draws,"
        << "hybrid_weighted_translucency_sort_ops,"
        << "hybrid_weighted_translucency_sorted_transparent_draws,"
        << "hybrid_forward_residual_draws,"
        << "hybrid_forward_residual_sort_ops,"
        << "hybrid_forward_residual_sorted_transparent_draws,"
        << "hybrid_forward_residual_stable_special_draws,"
        << "main_triangles,gbuffer_triangles,overlay_triangles,shadow_triangles,"
        << "hybrid_deferred_opaque_triangles,hybrid_weighted_translucency_triangles,"
        << "hybrid_forward_residual_triangles,"
        << "matrix_recalculations,"
        << "main_bounds_cache_hits,main_bounds_cache_misses,"
        << "main_command_cache_hits,main_command_cache_misses,"
        << "main_visibility_cache_hits,main_visibility_cache_misses,"
        << "main_queue_cache_hits,main_queue_cache_misses,"
        << "overlay_bounds_cache_hits,overlay_bounds_cache_misses,"
        << "overlay_command_cache_hits,overlay_command_cache_misses,"
        << "overlay_visibility_cache_hits,overlay_visibility_cache_misses,"
        << "overlay_queue_cache_hits,overlay_queue_cache_misses,"
        << "main_instanced_draws,main_instanced_instances,"
        << "main_instance_batch_cache_hits,main_instance_batch_cache_misses,"
        << "main_skinned_conservative_bounds,shadow_skinned_conservative_bounds,"
        << "main_visible,main_culled,overlay_visible,overlay_culled,shadow_visible,shadow_culled,"
        << "shadow_cascade_configured_count,shadow_cascade_active_count,"
        << "shadow_cascade_stable_snapping,shadow_pcf_kernel_radius,shadow_pcss_strength,"
        << "shadow_cascade_split_lambda,"
        << "shadow_cascade_blend_ratio,shadow_cascade_fade_ratio,"
        << "shadow_cascade_receiver_guard,"
        << "shadow_contact_strength,shadow_contact_length,"
        << "shadow_contact_thickness,shadow_contact_steps,"
        << "shadow_contact_jitter_strength,shadow_contact_edge_fade_pixels,"
        << "ssao_enabled,ssao_strength,ssao_radius,ssao_bias,ssao_sample_count,"
        << "ssr_enabled,ssr_color_resolve_enabled,ssr_strength,"
        << "ssr_ray_length,ssr_thickness,ssr_step_count,"
        << "ibl_quality,ibl_requested_source,ibl_actual_source,"
        << "ibl_source_fallback_reason,ibl_cache_policy,"
        << "ibl_cache_fallback_reason,ibl_cache_hit,ibl_runtime_generated,"
        << "ibl_source_asset_specified,ibl_source_asset_found,"
        << "ibl_source_signature,"
        << "ibl_brdf_lut_allocated,ibl_brdf_lut_size,ibl_brdf_lut_format,"
        << "ibl_irradiance_map_allocated,ibl_irradiance_face_size,"
        << "ibl_irradiance_format,ibl_prefiltered_map_allocated,"
        << "ibl_prefiltered_face_size,ibl_prefiltered_mip_count,"
        << "ibl_prefiltered_format,ibl_descriptor_sets_bound,"
        << "ibl_shader_integration_enabled,"
        << "probe_grid_allocated,probe_grid_enabled,"
        << "probe_grid_shader_integration_enabled,probe_grid_buffer_updates,"
        << "probe_grid_fallback_count,probe_grid_probe_count,"
        << "probe_grid_size_x,probe_grid_size_y,probe_grid_size_z,"
        << "probe_grid_vec4s_per_probe,probe_grid_directional_lobe_count,"
        << "probe_grid_origin_x,probe_grid_origin_y,probe_grid_origin_z,"
        << "probe_grid_spacing,probe_grid_blend_strength,"
        << "probe_grid_fallback_reason,probe_grid_cell_count,"
        << "probe_grid_bounds_min_x,probe_grid_bounds_min_y,probe_grid_bounds_min_z,"
        << "probe_grid_bounds_max_x,probe_grid_bounds_max_y,probe_grid_bounds_max_z,"
        << "probe_grid_debug_view_enabled,probe_grid_cell_debug_view_enabled,"
        << "bone_palette_draw_command_count,bone_palette_draw_ready_command_count,"
        << "bone_palette_draw_resource_count,bone_palette_draw_ready_resource_count,"
        << "bone_palette_draw_current_entry_count,"
        << "bone_palette_draw_previous_entry_count,"
        << "bone_palette_draw_changed_entry_count,bone_palette_draw_path_ready,"
        << "bone_palette_draw_descriptor_command_count,"
        << "bone_palette_draw_descriptor_ready_command_count,"
        << "bone_palette_draw_descriptor_resource_count,"
        << "bone_palette_draw_descriptor_ready_resource_count,"
        << "bone_palette_draw_descriptor_set_index,"
        << "bone_palette_draw_descriptor_binding,"
        << "bone_palette_draw_descriptor_range_bytes,"
        << "bone_palette_draw_descriptor_path_ready,"
        << "bone_palette_shader_consumer_command_count,"
        << "bone_palette_shader_consumer_ready_command_count,"
        << "bone_palette_shader_consumer_fallback_descriptor_ready,"
        << "bone_palette_shader_consumer_path_ready,"
        << "bone_palette_shader_skinning_command_count,"
        << "bone_palette_shader_skinning_ready_command_count,"
        << "bone_palette_shader_skinning_current_palette_offset,"
        << "bone_palette_shader_skinning_current_entry_count,"
        << "bone_palette_shader_skinning_path_ready,"
        << "bone_palette_shader_velocity_command_count,"
        << "bone_palette_shader_velocity_ready_command_count,"
        << "bone_palette_shader_velocity_previous_palette_offset,"
        << "bone_palette_shader_velocity_previous_entry_count,"
        << "bone_palette_shader_velocity_path_ready,"
        << "reflection_probe_fallback_enabled,reflection_probe_diffuse_intensity,"
        << "reflection_probe_specular_intensity,reflection_probe_horizon_blend,"
        << "reflection_probe_global_ibl_cubemap_sampling_enabled,"
        << "reflection_probe_scene_probe_count,reflection_probe_active_probe_count,"
        << "reflection_probe_scene_eligible_probe_count,"
        << "reflection_probe_selected_probe_count,"
        << "reflection_probe_blended_probe_count,"
        << "reflection_probe_capture_slot_count,"
        << "reflection_probe_selected_capture_ready_count,"
        << "reflection_probe_selected_capture_fallback_count,"
        << "reflection_probe_selected_cubemap_sampling_count,"
        << "reflection_probe_selected_capture_ready_mask,"
        << "reflection_probe_selected_capture_fallback_mask,"
        << "reflection_probe_selected_cubemap_sampling_mask,"
        << "reflection_probe_authored_asset_specified_count,"
        << "reflection_probe_authored_asset_found_count,"
        << "reflection_probe_authored_asset_missing_count,"
        << "reflection_probe_authored_asset_specified_mask,"
        << "reflection_probe_authored_asset_found_mask,"
        << "reflection_probe_authored_asset_missing_mask,"
        << "reflection_probe_captured_scene_requested_count,"
        << "reflection_probe_captured_scene_placeholder_allocated_count,"
        << "reflection_probe_captured_scene_placeholder_ready_count,"
        << "reflection_probe_captured_scene_invalidated_count,"
        << "reflection_probe_captured_scene_refresh_requested_count,"
        << "reflection_probe_captured_scene_capture_backend,"
        << "reflection_probe_captured_scene_face_count,"
        << "reflection_probe_captured_scene_faces_rendered,"
        << "reflection_probe_captured_scene_faces_pending,"
        << "reflection_probe_captured_scene_capture_pass_count,"
        << "reflection_probe_captured_scene_capture_draw_count,"
        << "reflection_probe_captured_scene_capture_visible_count,"
        << "reflection_probe_captured_scene_capture_culled_count,"
        << "reflection_probe_captured_scene_mip_generation_count,"
        << "reflection_probe_captured_scene_ggx_prefilter_dispatch_count,"
        << "reflection_probe_captured_scene_ggx_prefilter_sample_count,"
        << "reflection_probe_captured_scene_diffuse_irradiance_dispatch_count,"
        << "reflection_probe_captured_scene_diffuse_irradiance_sample_count,"
        << "reflection_probe_captured_scene_diffuse_irradiance_face_size,"
        << "reflection_probe_captured_scene_directional_shadow_requested,"
        << "reflection_probe_captured_scene_directional_shadow_ready,"
        << "reflection_probe_captured_scene_directional_shadow_pass_count,"
        << "reflection_probe_captured_scene_directional_shadow_draw_count,"
        << "reflection_probe_captured_scene_directional_shadow_caster_count,"
        << "reflection_probe_captured_scene_directional_shadow_map_size,"
        << "reflection_probe_captured_scene_directional_shadow_face_mask,"
        << "reflection_probe_captured_scene_directional_shadow_camera_independent,"
        << "reflection_probe_captured_scene_directional_shadow_local_tiles_suppressed,"
        << "reflection_probe_captured_scene_directional_shadow_probe_scene_index,"
        << "reflection_probe_captured_scene_local_shadow_requested,"
        << "reflection_probe_captured_scene_local_shadow_ready,"
        << "reflection_probe_captured_scene_local_shadow_pass_count,"
        << "reflection_probe_captured_scene_local_shadow_draw_count,"
        << "reflection_probe_captured_scene_local_shadow_caster_count,"
        << "reflection_probe_captured_scene_local_shadow_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_point_face_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_spot_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_rect_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_requested_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_dropped_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_rect_requested_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_rect_maximum_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_rect_extra_sample_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_rect_budget_limited_sample_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_rect_dropped_tile_count,"
        << "reflection_probe_captured_scene_local_shadow_map_tile_size,"
        << "reflection_probe_captured_scene_local_shadow_face_mask,"
        << "reflection_probe_captured_scene_local_shadow_supported_kind_mask,"
        << "reflection_probe_captured_scene_local_shadow_suppressed_kind_mask,"
        << "reflection_probe_captured_scene_local_shadow_camera_independent,"
        << "reflection_probe_captured_scene_local_shadow_probe_scene_index,"
        << "reflection_probe_captured_scene_shadow_snapshot_build_count,"
        << "reflection_probe_captured_scene_shadow_snapshot_reuse_face_count,"
        << "reflection_probe_captured_scene_shadow_snapshot_saved_directional_pass_count,"
        << "reflection_probe_captured_scene_shadow_snapshot_saved_local_tile_pass_count,"
        << "reflection_probe_captured_scene_shadow_snapshot_saved_local_draw_count,"
        << "reflection_probe_captured_scene_shadow_snapshot_build_face_mask,"
        << "reflection_probe_captured_scene_shadow_snapshot_reuse_face_mask,"
        << "reflection_probe_captured_scene_shadow_snapshot_probe_scene_index,"
        << "reflection_probe_captured_scene_shadow_snapshot_ready,"
        << "reflection_probe_captured_scene_shadow_snapshot_camera_independent,"
        << "reflection_probe_captured_scene_shadow_snapshot_enabled,"
        << "reflection_probe_captured_scene_shadow_snapshot_fallback_active,"
        << "reflection_probe_captured_scene_last_captured_face,"
        << "reflection_probe_captured_scene_rasterized_geometry,"
        << "reflection_probe_captured_scene_gpu_resources_allocated,"
        << "reflection_probe_captured_scene_gpu_capture_in_progress,"
        << "reflection_probe_captured_scene_mip_chain_ready,"
        << "reflection_probe_captured_scene_ggx_prefilter_ready,"
        << "reflection_probe_captured_scene_diffuse_irradiance_ready,"
        << "reflection_probe_captured_scene_probe_scene_index,"
        << "reflection_probe_selected_captured_scene_map_matches_active_mask,"
        << "reflection_probe_selected_captured_scene_duplicate_active_view_mask,"
        << "reflection_probe_selected_captured_scene_diffuse_irradiance_map_matches_active_mask,"
        << "reflection_probe_selected_captured_scene_diffuse_irradiance_duplicate_active_view_mask,"
        << "reflection_probe_captured_scene_probe_resource_count,"
        << "reflection_probe_captured_scene_ready_probe_count,"
        << "reflection_probe_captured_scene_in_flight_probe_count,"
        << "reflection_probe_captured_scene_distinct_active_view_count,"
        << "reflection_probe_captured_scene_diffuse_irradiance_ready_probe_count,"
        << "reflection_probe_captured_scene_distinct_active_diffuse_irradiance_view_count,"
        << "reflection_probe_captured_scene_upload_count,"
        << "reflection_probe_captured_scene_refresh_check_count,"
        << "reflection_probe_captured_scene_refresh_performed,"
        << "reflection_probe_captured_scene_refresh_reason,"
        << "reflection_probe_captured_scene_last_refresh_reason,"
        << "reflection_probe_captured_scene_dirty_mask,"
        << "reflection_probe_captured_scene_active_signature,"
        << "reflection_probe_captured_scene_requested_signature,"
        << "reflection_probe_captured_scene_radiance_signature,"
        << "reflection_probe_captured_scene_membership_revision,"
        << "reflection_probe_captured_scene_light_revision,"
        << "reflection_probe_captured_scene_render_revision,"
        << "reflection_probe_forced_refresh_requested,"
        << "reflection_probe_scene_dirty_requested,"
        << "reflection_probe_authored_cubemap_loaded_count,"
        << "reflection_probe_authored_cubemap_missing_count,"
        << "reflection_probe_authored_cubemap_load_failed_count,"
        << "reflection_probe_authored_cubemap_upload_count,"
        << "reflection_probe_authored_cubemap_six_face_loaded_count,"
        << "reflection_probe_authored_cubemap_equirectangular_loaded_count,"
        << "reflection_probe_authored_cubemap_equirectangular_conversion_count,"
        << "reflection_probe_authored_cubemap_hdr_loaded_count,"
        << "reflection_probe_authored_cubemap_prefiltered_loaded_count,"
        << "reflection_probe_authored_cubemap_prefiltered_upload_count,"
        << "reflection_probe_authored_cubemap_cache_hit_count,"
        << "reflection_probe_authored_cubemap_reload_count,"
        << "reflection_probe_authored_cubemap_refresh_check_count,"
        << "reflection_probe_authored_cubemap_face_size,"
        << "reflection_probe_authored_cubemap_mip_count,"
        << "reflection_probe_authored_cubemap_format,"
        << "reflection_probe_authored_cubemap_source_type,"
        << "reflection_probe_authored_cubemap_hdr,"
        << "reflection_probe_authored_cubemap_prefiltered,"
        << "reflection_probe_authored_cubemap_generated_mip_count,"
        << "reflection_probe_authored_cubemap_prefilter_sample_count,"
        << "reflection_probe_authored_cubemap_prefilter_mode,"
        << "reflection_probe_authored_cubemap_filter_quality,"
        << "reflection_probe_authored_cubemap_seam_aware_filtering,"
        << "reflection_probe_authored_cubemap_irradiance_ready_count,"
        << "reflection_probe_authored_cubemap_irradiance_applied,"
        << "reflection_probe_authored_cubemap_irradiance_r,"
        << "reflection_probe_authored_cubemap_irradiance_g,"
        << "reflection_probe_authored_cubemap_irradiance_b,"
        << "reflection_probe_authored_cubemap_diffuse_lobes_ready_count,"
        << "reflection_probe_authored_cubemap_diffuse_lobes_applied,"
        << "reflection_probe_authored_cubemap_diffuse_lobe_count,"
        << "reflection_probe_selected_diffuse_lobe_ready_mask,"
        << "reflection_probe_selected_captured_scene_diffuse_irradiance_ready_mask,"
        << "reflection_probe_authored_cubemap_diffuse_lobe_energy,"
        << "reflection_probe_selected_capture_slot_0,"
        << "reflection_probe_selected_capture_slot_1,"
        << "reflection_probe_selected_capture_slot_2,"
        << "reflection_probe_selected_capture_slot_3,"
        << "reflection_probe_selected_authored_asset_hash_0,"
        << "reflection_probe_selected_authored_asset_hash_1,"
        << "reflection_probe_selected_authored_asset_hash_2,"
        << "reflection_probe_selected_authored_asset_hash_3,"
        << "reflection_probe_selected_capture_source_type_0,"
        << "reflection_probe_selected_capture_source_type_1,"
        << "reflection_probe_selected_capture_source_type_2,"
        << "reflection_probe_selected_capture_source_type_3,"
        << "reflection_probe_selected_capture_fallback_reason_0,"
        << "reflection_probe_selected_capture_fallback_reason_1,"
        << "reflection_probe_selected_capture_fallback_reason_2,"
        << "reflection_probe_selected_capture_fallback_reason_3,"
        << "reflection_probe_selected_refresh_policy_0,"
        << "reflection_probe_selected_refresh_policy_1,"
        << "reflection_probe_selected_refresh_policy_2,"
        << "reflection_probe_selected_refresh_policy_3,"
        << "reflection_probe_selected_captured_scene_placeholder_ready_0,"
        << "reflection_probe_selected_captured_scene_placeholder_ready_1,"
        << "reflection_probe_selected_captured_scene_placeholder_ready_2,"
        << "reflection_probe_selected_captured_scene_placeholder_ready_3,"
        << "reflection_probe_selected_captured_scene_invalidated_0,"
        << "reflection_probe_selected_captured_scene_invalidated_1,"
        << "reflection_probe_selected_captured_scene_invalidated_2,"
        << "reflection_probe_selected_captured_scene_invalidated_3,"
        << "reflection_probe_selected_capture_mip_count_0,"
        << "reflection_probe_selected_capture_mip_count_1,"
        << "reflection_probe_selected_capture_mip_count_2,"
        << "reflection_probe_selected_capture_mip_count_3,"
        << "reflection_probe_selected_probe_index,"
        << "reflection_probe_dropped_probe_count,"
        << "reflection_probe_max_blend_weight,"
        << "reflection_probe_total_blend_weight,"
        << "reflection_probe_normalized_blend_weight_sum,"
        << "reflection_probe_blend_weight_normalization_fallback_count,"
        << "reflection_probe_selected_probe_mask,"
        << "reflection_probe_selected_box_projection_mask,"
        << "reflection_probe_selected_scene_owned_mask,"
        << "reflection_probe_selected_positive_influence_mask,"
        << "reflection_probe_selected_blend_weight_0,"
        << "reflection_probe_selected_blend_weight_1,"
        << "reflection_probe_selected_blend_weight_2,"
        << "reflection_probe_selected_blend_weight_3,"
        << "reflection_probe_selected_normalized_blend_weight_0,"
        << "reflection_probe_selected_normalized_blend_weight_1,"
        << "reflection_probe_selected_normalized_blend_weight_2,"
        << "reflection_probe_selected_normalized_blend_weight_3,"
        << "reflection_probe_multi_blend_enabled,"
        << "reflection_probe_local_enabled,reflection_probe_local_scene_owned,"
        << "reflection_probe_local_radius,"
        << "reflection_probe_local_box_extent_x,reflection_probe_local_box_extent_y,"
        << "reflection_probe_local_box_extent_z,"
        << "reflection_probe_local_intensity,reflection_probe_local_blend_strength,"
        << "reflection_probe_local_falloff,"
        << "reflection_probe_cubemap_allocated,reflection_probe_cubemap_face_size,"
        << "reflection_probe_cubemap_mip_count,reflection_probe_cubemap_format,"
        << "reflection_probe_cubemap_descriptor_sets_bound,"
        << "reflection_probe_cubemap_shader_sampling_enabled,"
        << "reflection_probe_cubemap_source_type,"
        << "reflection_probe_capture_source_type,"
        << "reflection_probe_refresh_policy,"
        << "reflection_probe_capture_resource_ready,"
        << "reflection_probe_capture_fallback_reason,"
        << "reflection_probe_capture_descriptor_bound,"
        << "reflection_probe_box_projection_enabled,"
        << "reflection_probe_influence_mode,"
        << "reflection_probe_parallax_correction_enabled,"
        << "height_fog_enabled,height_fog_density,height_fog_height_falloff,"
        << "height_fog_start_distance,height_fog_max_opacity,"
        << "bloom_enabled,bloom_intensity,bloom_threshold,bloom_radius_pixels,"
        << "bloom_pyramid_enabled,bloom_pyramid_mip_count,bloom_pyramid_fallbacks,"
        << "tone_mapping_enabled,tone_map_mode,exposure,tone_map_white_point,"
        << "auto_exposure_enabled,auto_exposure_target_luminance,"
        << "auto_exposure_min,auto_exposure_max,auto_exposure_adaptation,"
        << "auto_exposure_histogram_enabled,auto_exposure_history_valid,"
        << "auto_exposure_gpu_exposure,auto_exposure_gpu_target_exposure,"
        << "auto_exposure_gpu_average_luminance,auto_exposure_gpu_fallbacks,"
        << "color_grading_enabled,color_grading_saturation,"
        << "color_grading_contrast,color_grading_gamma,"
        << "color_grading_lut_enabled,color_grading_lut_size,"
        << "color_grading_lut_strength,color_grading_lut_fallbacks,"
        << "sharpening_enabled,sharpening_strength,sharpening_radius_pixels,"
        << "shadow_cascade_max_distance,shadow_cascade_near_depth,shadow_cascade_far_depth,"
        << "shadow_cascade_split0,shadow_cascade_split1,"
        << "shadow_cascade_split2,shadow_cascade_split3,"
        << "shadow_cascade_texel0,shadow_cascade_texel1,"
        << "shadow_cascade_texel2,shadow_cascade_texel3,"
        << "shadow_cascade_atlas_allocated,shadow_cascade_atlas_tile_size,"
        << "shadow_cascade_atlas_width,shadow_cascade_atlas_height,"
        << "shadow_cascade_atlas_tile_columns,shadow_cascade_atlas_tile_rows,"
        << "shadow_cascade_atlas_capacity,"
        << "local_shadow_atlas_allocated,local_shadow_atlas_tile_size,"
        << "local_shadow_atlas_width,local_shadow_atlas_height,"
        << "local_shadow_atlas_tile_columns,local_shadow_atlas_tile_rows,"
        << "local_shadow_atlas_capacity,local_shadow_shadowable_light_count,"
        << "local_shadow_point_light_count,local_shadow_spot_light_count,"
        << "local_shadow_rect_light_count,local_shadow_point_face_tiles,"
        << "local_shadow_spot_tiles,local_shadow_rect_tiles,"
        << "local_shadow_requested_tiles,local_shadow_assigned_tiles,"
        << "local_shadow_dropped_tiles,local_shadow_recorded_tile_passes,"
        << "local_shadow_recorded_draws,local_shadow_recorded_mesh_binds,"
        << "local_shadow_cache_eligible_tiles,local_shadow_cache_hit_tiles,"
        << "local_shadow_cache_miss_tiles,local_shadow_cache_skipped_tiles,"
        << "local_shadow_cache_cold_tiles,"
        << "local_shadow_cache_tile_layout_changed_tiles,"
        << "local_shadow_cache_light_changed_tiles,"
        << "local_shadow_cache_caster_changed_tiles,"
        << "local_shadow_cache_dynamic_skinned_caster_tiles,"
        << "local_shadow_cache_reason_summary,"
        << "local_shadow_bias_min,local_shadow_bias_slope,"
        << "local_shadow_pcf_radius,local_shadow_pcf_kernel_radius,"
        << "local_shadow_pcss_strength,local_shadow_face_blend_strength,"
        << "local_shadow_rect_bias_scale,"
        << "local_shadow_point_bias_min,local_shadow_point_bias_slope,"
        << "local_shadow_point_pcf_radius,local_shadow_point_pcf_kernel_radius,"
        << "local_shadow_point_pcss_strength,"
        << "local_shadow_spot_bias_min,local_shadow_spot_bias_slope,"
        << "local_shadow_spot_pcf_radius,local_shadow_spot_pcf_kernel_radius,"
        << "local_shadow_spot_pcss_strength,"
        << "local_shadow_rect_bias_min,local_shadow_rect_bias_slope,"
        << "local_shadow_rect_pcf_radius,local_shadow_rect_pcf_kernel_radius,"
        << "local_shadow_rect_pcss_strength,"
        << "local_shadow_rect_base_sample_tiles,"
        << "local_shadow_rect_max_sample_tiles,"
        << "local_shadow_rect_sample_pattern,"
        << "local_shadow_rect_extra_sample_tiles,"
        << "local_shadow_rect_budget_limited_sample_tiles,"
        << "local_shadow_point_enabled,local_shadow_spot_enabled,"
        << "local_shadow_rect_enabled,local_shadow_debug_light_index,"
        << "local_shadow_attribution_light_index,local_shadow_attribution_light_valid,"
        << "local_shadow_attribution_light_kind,"
        << "local_shadow_attribution_expected_tiles,"
        << "local_shadow_attribution_requested_tiles,"
        << "local_shadow_attribution_assigned_tiles,"
        << "local_shadow_attribution_dropped_tiles,"
        << "local_shadow_attribution_cache_hit_tiles,"
        << "local_shadow_attribution_cache_miss_tiles,"
        << "local_shadow_attribution_recorded_tile_passes,"
        << "local_shadow_attribution_recorded_draws,"
        << "local_shadow_attribution_candidate_draws,"
        << "local_shadow_attribution_unique_casters,"
        << "local_shadow_attribution_caster_signature,"
        << "local_shadow_attribution_tile_candidate_draws,"
        << "local_shadow_attribution_caster_summary,"
        << "local_shadow_attribution_shadow_enabled,"
        << "local_shadow_attribution_matches_generation_filter,"
        << "weighted_translucency_allocated,"
        << "weighted_translucency_accum_width,weighted_translucency_accum_height,"
        << "weighted_translucency_revealage_width,weighted_translucency_revealage_height,"
        << "weighted_translucency_accum_format,weighted_translucency_revealage_format,"
        << "weighted_translucency_render_pass_allocated,"
        << "weighted_translucency_framebuffer_count,weighted_translucency_clear_passes,"
        << "weighted_translucency_draws,weighted_translucency_shared_light_list_draws,"
        << "weighted_translucency_shadow_ready_draws,weighted_translucency_resolve_draws,"
        << "temporal_antialiasing_mode,"
        << "temporal_velocity_target_allocated,temporal_velocity_format,"
        << "temporal_velocity_camera_motion_enabled,"
        << "temporal_velocity_camera_motion_ready,"
        << "temporal_velocity_object_motion_ready,"
        << "temporal_velocity_material_aux_target_allocated,"
        << "temporal_velocity_material_aux_format,"
        << "temporal_velocity_material_aux_migrated,"
        << "temporal_history_valid,temporal_history_reset,"
        << "temporal_history_reset_reason,"
        << "temporal_jitter_enabled,temporal_jitter_applied,"
        << "temporal_jitter_sequence_index,"
        << "temporal_jitter_pixels_x,temporal_jitter_pixels_y,"
        << "temporal_jitter_uv_x,temporal_jitter_uv_y,"
        << "temporal_velocity_jittered_history_policy,"
        << "temporal_velocity_previous_jitter_applied,"
        << "temporal_previous_jitter_pixels_x,temporal_previous_jitter_pixels_y,"
        << "temporal_previous_jitter_uv_x,temporal_previous_jitter_uv_y,"
        << "temporal_taa_resolve_enabled,"
        << "temporal_taa_resolve_configured,"
        << "temporal_taa_resolve_suppressed_for_upscaler,"
        << "temporal_taa_history_color_target_allocated,"
        << "temporal_taa_history_color_format,"
        << "temporal_taa_history_color_ready,"
        << "temporal_taa_history_color_copies,"
        << "temporal_taa_history_weight,"
        << "temporal_taa_velocity_reprojection_enabled,"
        << "temporal_taa_fallback_reason,"
        << "temporal_taa_debug_view_enabled,"
        << "temporal_taa_rejection_enabled,"
        << "temporal_taa_neighborhood_clamp_enabled,"
        << "temporal_taa_velocity_rejection_threshold,"
        << "temporal_taa_depth_rejection_threshold,"
        << "temporal_taa_rejection_debug_view_enabled,"
        << "temporal_taa_history_debug_view_enabled,"
        << "temporal_taa_reprojection_debug_view_enabled,"
        << "temporal_consumer_readiness_mask,"
        << "temporal_consumer_active_mask,"
        << "temporal_consumer_unsupported_mask,"
        << "temporal_consumer_ssr_ready,"
        << "temporal_consumer_ssr_active,"
        << "temporal_consumer_gtao_ready,"
        << "temporal_consumer_motion_blur_ready,"
        << "temporal_consumer_dynamic_resolution_ready,"
        << "temporal_consumer_upscaler_ready,"
        << "temporal_render_scale_requested,"
        << "temporal_render_scale_active,"
        << "temporal_render_scale_applied,"
        << "temporal_upscale_display_width,"
        << "temporal_upscale_display_height,"
        << "temporal_upscale_requested_width,"
        << "temporal_upscale_requested_height,"
        << "temporal_upscale_active_width,"
        << "temporal_upscale_active_height,"
        << "temporal_upscale_output_allocated,"
        << "temporal_upscale_output_format,"
        << "temporal_upscale_output_width,"
        << "temporal_upscale_output_height,"
        << "temporal_upscale_post_source_requested,"
        << "temporal_upscale_post_source_active,"
        << "temporal_upscale_post_source_fallback_reason,"
        << "temporal_dynamic_resolution_requested,"
        << "temporal_dynamic_resolution_enabled,"
        << "temporal_taau_requested,"
        << "temporal_upscale_requested,"
        << "temporal_upscale_enabled,"
        << "temporal_upscale_input_ready,"
        << "temporal_upscale_fallback_reason,"
        << "temporal_upscale_input_readiness_mask,"
        << "temporal_upscale_required_input_mask,"
        << "temporal_upscale_contract_ready,"
        << "temporal_upscaler_plugin_requested,"
        << "temporal_upscaler_plugin_available,"
        << "temporal_upscaler_provider_kind,"
        << "temporal_upscaler_package_fallback_reason,"
        << "temporal_upscaler_package_directory_found,"
        << "temporal_upscaler_headers_found,"
        << "temporal_upscaler_import_library_found,"
        << "temporal_upscaler_runtime_found,"
        << "temporal_upscaler_dlss_super_resolution_symbols_found,"
        << "temporal_upscaler_dlss_frame_generation_symbols_found,"
        << "temporal_upscaler_dlss_ray_reconstruction_symbols_found,"
        << "temporal_upscaler_dlss_transformer_preset_symbols_found,"
        << "temporal_upscaler_sdk_version_major,"
        << "temporal_upscaler_sdk_version_minor,"
        << "temporal_upscaler_sdk_version_patch,"
        << "temporal_upscaler_package_ready,"
        << "temporal_upscaler_evaluate_adapter_available,"
        << "temporal_upscaler_runtime_fallback_reason,"
        << "temporal_upscaler_adapter_compiled,"
        << "temporal_upscaler_initialization_attempted,"
        << "temporal_upscaler_initialized,"
        << "temporal_upscaler_initialization_result,"
        << "temporal_upscaler_capability_parameters_ready,"
        << "temporal_upscaler_capability_query_result,"
        << "temporal_upscaler_feature_requirements_queried,"
        << "temporal_upscaler_feature_requirements_result,"
        << "temporal_upscaler_feature_supported_mask,"
        << "temporal_upscaler_feature_requirements_supported,"
        << "temporal_upscaler_min_hardware_architecture,"
        << "temporal_upscaler_min_os_version,"
        << "temporal_upscaler_instance_extension_requirements_queried,"
        << "temporal_upscaler_instance_extension_requirements_result,"
        << "temporal_upscaler_instance_extension_requirement_count,"
        << "temporal_upscaler_instance_extension_available_count,"
        << "temporal_upscaler_instance_extension_missing_available_count,"
        << "temporal_upscaler_instance_extension_enabled_count,"
        << "temporal_upscaler_instance_extension_missing_enabled_count,"
        << "temporal_upscaler_instance_extension_requirements,"
        << "temporal_upscaler_instance_extension_missing_available,"
        << "temporal_upscaler_instance_extension_missing_enabled,"
        << "temporal_upscaler_device_extension_requirements_queried,"
        << "temporal_upscaler_device_extension_requirements_result,"
        << "temporal_upscaler_device_extension_requirement_count,"
        << "temporal_upscaler_device_extension_available_count,"
        << "temporal_upscaler_device_extension_missing_available_count,"
        << "temporal_upscaler_device_extension_enabled_count,"
        << "temporal_upscaler_device_extension_missing_enabled_count,"
        << "temporal_upscaler_device_extension_requirements,"
        << "temporal_upscaler_device_extension_missing_available,"
        << "temporal_upscaler_device_extension_missing_enabled,"
        << "temporal_upscaler_runtime_flavor,"
        << "temporal_upscaler_runtime_path_overridden,"
        << "temporal_upscaler_runtime_path_found,"
        << "temporal_upscaler_runtime_path,"
        << "temporal_upscaler_runtime_dll_found,"
        << "temporal_upscaler_runtime_dll_size_bytes,"
        << "temporal_upscaler_runtime_dll_hash,"
        << "temporal_upscaler_dlss_super_resolution_supported,"
        << "temporal_upscaler_needs_updated_driver,"
        << "temporal_upscaler_min_driver_version_major,"
        << "temporal_upscaler_min_driver_version_minor,"
        << "temporal_upscaler_feature_init_result,"
        << "temporal_upscaler_dlss_quality_mode,"
        << "temporal_upscaler_dlss_recommended_preset,"
        << "temporal_upscaler_optimal_settings_queried,"
        << "temporal_upscaler_optimal_settings_result,"
        << "temporal_upscaler_optimal_render_width,"
        << "temporal_upscaler_optimal_render_height,"
        << "temporal_upscaler_min_render_width,"
        << "temporal_upscaler_min_render_height,"
        << "temporal_upscaler_max_render_width,"
        << "temporal_upscaler_max_render_height,"
        << "temporal_upscaler_sharpness,"
        << "temporal_upscaler_evaluate_requested,"
        << "temporal_upscaler_evaluate_attempted,"
        << "temporal_upscaler_evaluate_fallback_reason,"
        << "temporal_upscaler_evaluate_parameters_allocated,"
        << "temporal_upscaler_evaluate_parameter_allocation_result,"
        << "temporal_upscaler_feature_create_attempted,"
        << "temporal_upscaler_feature_created,"
        << "temporal_upscaler_feature_create_result,"
        << "temporal_upscaler_feature_recreated,"
        << "temporal_upscaler_feature_recreation_reason,"
        << "temporal_upscaler_dlss_evaluate_attempted,"
        << "temporal_upscaler_dlss_evaluate_result,"
        << "temporal_upscaler_dlss_output_ready,"
        << "temporal_upscaler_dlss_render_width,"
        << "temporal_upscaler_dlss_render_height,"
        << "temporal_upscaler_dlss_output_width,"
        << "temporal_upscaler_dlss_output_height,"
        << "temporal_upscaler_dlss_create_flags,"
        << "temporal_upscaler_dlss_create_flag_is_hdr,"
        << "temporal_upscaler_dlss_create_flag_mv_low_res,"
        << "temporal_upscaler_dlss_create_flag_mv_jittered,"
        << "temporal_upscaler_dlss_create_flag_depth_inverted,"
        << "temporal_upscaler_dlss_create_flag_auto_exposure,"
        << "temporal_upscaler_dlss_input_color_format,"
        << "temporal_upscaler_dlss_input_depth_format,"
        << "temporal_upscaler_dlss_input_motion_vector_format,"
        << "temporal_upscaler_dlss_input_color_width,"
        << "temporal_upscaler_dlss_input_color_height,"
        << "temporal_upscaler_dlss_input_depth_width,"
        << "temporal_upscaler_dlss_input_depth_height,"
        << "temporal_upscaler_dlss_input_motion_vector_width,"
        << "temporal_upscaler_dlss_input_motion_vector_height,"
        << "temporal_upscaler_dlss_input_depth_aspect_mask,"
        << "temporal_upscaler_dlss_input_motion_vector_aspect_mask,"
        << "temporal_upscaler_dlss_input_depth_matches_render_extent,"
        << "temporal_upscaler_dlss_input_motion_vector_matches_render_extent,"
        << "temporal_upscaler_dlss_motion_vector_scale_pixel_space,"
        << "temporal_upscaler_dlss_motion_vector_scale_unit_space,"
        << "temporal_upscaler_dlss_motion_vector_scale_matches_render_extent,"
        << "temporal_upscaler_dlss_reset,"
        << "temporal_upscaler_dlss_jitter_offset_x,"
        << "temporal_upscaler_dlss_jitter_offset_y,"
        << "temporal_upscaler_dlss_motion_vector_scale_x,"
        << "temporal_upscaler_dlss_motion_vector_scale_y,"
        << "temporal_upscaler_dlss_evaluate_sharpness,"
        << "temporal_upscaler_dlss_quality_gate_requested,"
        << "temporal_upscaler_dlss_quality_gate_ready,"
        << "temporal_upscaler_dlss_quality_gate_fallback_reason,"
        << "temporal_upscaler_dlss_quality_required_mask,"
        << "temporal_upscaler_dlss_quality_ready_mask,"
        << "temporal_upscaler_dlss_quality_blocker_mask,"
        << "temporal_upscaler_dlss_quality_evaluate_output_ready,"
        << "temporal_upscaler_dlss_quality_camera_motion_ready,"
        << "temporal_upscaler_dlss_quality_object_motion_ready,"
        << "temporal_upscaler_dlss_quality_scene_content_motion_supported,"
        << "temporal_upscaler_dlss_quality_reactive_mask_ready,"
        << "temporal_upscaler_dlss_quality_transparency_mask_ready,"
        << "temporal_upscaler_dlss_quality_exposure_policy_ready,"
        << "temporal_upscaler_dlss_quality_post_ordering_ready,"
        << "temporal_upscaler_dlss_quality_reference_baseline_ready,"
        << "main_material_binds,main_mesh_binds,gbuffer_material_binds,gbuffer_mesh_binds,"
        << "main_bone_palette_descriptor_binds,gbuffer_bone_palette_descriptor_binds,"
        << "bone_palette_descriptor_binds,"
        << "gbuffer_bone_palette_fallback_descriptor_binds,"
        << "bone_palette_fallback_descriptor_binds,"
        << "deferred_lighting_draws,deferred_lighting_frame_binds,deferred_lighting_gbuffer_binds,"
        << "deferred_pbr_debug_draws,deferred_pbr_debug_frame_binds,deferred_pbr_debug_gbuffer_binds,"
        << "hdr_composite_draws,hdr_composite_frame_binds,hdr_composite_texture_binds,"
        << "gbuffer_debug_draws,gbuffer_debug_frame_binds,gbuffer_debug_texture_binds,"
        << "deferred_shadow_debug_draws,deferred_shadow_debug_frame_binds,deferred_shadow_debug_texture_binds,"
        << "shadow_cascade_debug_draws,shadow_cascade_debug_frame_binds,shadow_cascade_debug_texture_binds,"
        << "local_shadow_atlas_debug_draws,local_shadow_atlas_debug_frame_binds,"
        << "local_shadow_atlas_debug_texture_binds,"
        << "local_shadow_visibility_debug_draws,local_shadow_visibility_debug_frame_binds,"
        << "local_shadow_visibility_debug_texture_binds,"
        << "local_shadow_face_debug_draws,local_shadow_face_debug_frame_binds,"
        << "local_shadow_face_debug_texture_binds,"
        << "contact_shadow_debug_draws,contact_shadow_debug_frame_binds,"
        << "contact_shadow_debug_gbuffer_binds,"
        << "ssao_debug_draws,ssao_debug_frame_binds,ssao_debug_gbuffer_binds,"
        << "ssr_debug_draws,ssr_debug_frame_binds,ssr_debug_gbuffer_binds,"
        << "reflection_probe_debug_draws,reflection_probe_debug_frame_binds,"
        << "reflection_probe_debug_gbuffer_binds,"
        << "height_fog_debug_draws,height_fog_debug_frame_binds,"
        << "height_fog_debug_gbuffer_binds,"
        << "probe_grid_debug_draws,probe_grid_debug_frame_binds,"
        << "probe_grid_debug_gbuffer_binds,"
        << "probe_grid_cell_debug_draws,probe_grid_cell_debug_frame_binds,"
        << "probe_grid_cell_debug_gbuffer_binds,"
        << "bloom_debug_draws,bloom_debug_frame_binds,"
        << "bloom_debug_texture_binds,"
        << "bloom_downsample_draws,bloom_downsample_frame_binds,"
        << "bloom_downsample_texture_binds,bloom_upsample_draws,"
        << "bloom_upsample_frame_binds,bloom_upsample_texture_binds,"
        << "tone_mapping_debug_draws,tone_mapping_debug_frame_binds,"
        << "tone_mapping_debug_texture_binds,"
        << "auto_exposure_debug_draws,auto_exposure_debug_frame_binds,"
        << "auto_exposure_debug_texture_binds,"
        << "color_grading_debug_draws,color_grading_debug_frame_binds,"
        << "color_grading_debug_texture_binds,"
        << "sharpening_debug_draws,sharpening_debug_frame_binds,"
        << "sharpening_debug_texture_binds,"
        << "light_tile_cull_compute_dispatches,light_tile_cull_compute_frame_binds,"
        << "light_tile_cull_compute_groups_x,light_tile_cull_compute_groups_y,"
        << "auto_exposure_histogram_dispatches,auto_exposure_histogram_frame_binds,"
        << "auto_exposure_histogram_texture_binds,"
        << "auto_exposure_histogram_groups_x,auto_exposure_histogram_groups_y,"
        << "depth_copy_ops,depth_prefill_draws,depth_prefill_mesh_binds,"
        << "weighted_translucency_bind_clear_passes,"
        << "weighted_translucency_bind_draws,"
        << "weighted_translucency_bind_shared_light_list_draws,"
        << "weighted_translucency_bind_shadow_ready_draws,"
        << "weighted_translucency_material_binds,"
        << "weighted_translucency_mesh_binds,weighted_translucency_resolve_bind_draws,"
        << "weighted_translucency_resolve_frame_binds,"
        << "weighted_translucency_resolve_texture_binds,"
        << "weighted_translucency_debug_draws,"
        << "weighted_translucency_debug_frame_binds,"
        << "weighted_translucency_debug_texture_binds,"
        << "weighted_translucency_alpha_reference_mismatch_draws,"
        << "weighted_translucency_velocity_draws,"
        << "weighted_translucency_velocity_material_binds,"
        << "weighted_translucency_velocity_mesh_binds,"
        << "dlss_mask_draws,"
        << "dlss_mask_weighted_translucency_draws,"
        << "dlss_mask_forward_residual_draws,"
        << "dlss_mask_material_binds,"
        << "dlss_mask_mesh_binds,"
        << "forward_residual_alpha_reference_enabled,"
        << "forward_residual_draws,forward_residual_frame_binds,"
        << "forward_residual_shared_light_list_draws,"
        << "forward_residual_material_binds,forward_residual_mesh_binds,"
        << "forward_residual_velocity_draws,"
        << "forward_residual_velocity_material_binds,"
        << "forward_residual_velocity_mesh_binds,"
        << "overlay_material_binds,overlay_mesh_binds,"
        << "shadow_mesh_binds,shadow_cascade_atlas_passes,"
        << "shadow_cascade_atlas_draws,shadow_cascade_atlas_mesh_binds,"
        << "local_shadow_atlas_passes,local_shadow_atlas_draws,"
        << "local_shadow_atlas_mesh_binds,"
        << "local_shadow_resolve_enabled,"
        << "shadow_cascade_buffer_updates,local_shadow_buffer_updates,"
        << "frame_light_constant_updates,frame_light_buffer_updates,"
        << "frame_light_total_count,frame_directional_light_count,frame_local_light_count,"
        << "frame_rect_light_count,"
        << "frame_light_tile_size,frame_light_tile_count_x,frame_light_tile_count_y,"
        << "frame_light_tile_count,frame_light_tile_assignments,"
        << "frame_light_tile_assignment_capacity,"
        << "frame_light_tile_overflow_assignments,frame_light_tile_overflow_capacity,"
        << "frame_light_tile_overflow_tiles,frame_light_tile_overflow_dropped,"
        << "frame_light_tile_assignment_fallbacks,"
        << "frame_light_tile_gpu_readback_valid,frame_light_tile_gpu_saturated_tiles,"
        << "frame_light_tile_gpu_max_candidates,frame_light_tile_gpu_raw_candidates,"
        << "frame_light_tile_gpu_overflow_tiles,"
        << "frame_light_tile_gpu_overflow_dropped_tiles,"
        << "frame_light_tile_gpu_overflow_stored,frame_light_tile_gpu_overflow_dropped,"
        << "frame_material_buffer_updates,frame_material_count,"
        << "frame_material_capacity,frame_material_overflow_count,"
        << "frame_material_opaque_count,frame_material_transparent_count,"
        << "frame_material_forward_special_count,frame_material_emissive_hint_count,"
        << "frame_material_specular_hint_count,"
        << "frame_material_specular_texture_count,"
        << "frame_material_alpha_mask_count,frame_material_alpha_blend_count,"
        << "frame_material_uv_transform_count,"
        << "frame_material_double_sided_count,"
        << "frame_material_clearcoat_count,"
        << "frame_material_clearcoat_texture_count,"
        << "frame_material_clearcoat_roughness_texture_count,"
        << "frame_material_transmission_count,"
        << "frame_material_transmission_texture_count,"
        << "frame_material_volume_count,"
        << "frame_material_opacity_texture_count,"
        << "frame_material_textured_count,frame_material_texture_mip_lod_bias,"
        << "main_instance_buffer_uploads,main_instance_buffer_upload_skips,"
        << "push_constant_updates,push_constant_bytes\n";
}

}
