#pragma once

#include "core.h"
#include "renderer/vulkan/renderer_stats.h"

#include <filesystem>
#include <fstream>

namespace se {

struct BenchmarkRecorderConfig {
    bool enabled = false;
    u32 warmupFrames = 30;
    u32 captureFrames = 0;
    std::filesystem::path csvPath{ "selfengine_benchmark.csv" };
};

struct BenchmarkSceneDiagnostics {
    u32 ueBridgeRequested = 0;
    u32 ueBridgeManifestLoaded = 0;
    u32 ueBridgeSceneFound = 0;
    u32 ueBridgeExportedSceneReady = 0;
    u32 ueBridgeMeshInstanceCount = 0;
    u32 ueBridgeMeshInstanceLoadedCount = 0;
    u32 ueBridgeMeshExportReadyCount = 0;
    u32 ueBridgeMeshExportMissingCount = 0;
    u32 ueBridgeManifestMeshExportReadyCount = 0;
    u32 ueBridgeManifestMeshExportMissingCount = 0;
    u32 ueBridgeCameraCount = 0;
    u32 ueBridgeCameraApplied = 0;
    u32 ueBridgeLightCount = 0;
    u32 ueBridgeLightsApplied = 0;
    u32 ueBridgeSkyLightCount = 0;
    u32 ueBridgeSkyLightApplied = 0;
    u32 ueBridgeReferenceCaptureCount = 0;
    u32 ueBridgeVisualParityReady = 0;
    u32 ueBridgeBlockedMissingManifest = 0;
    u32 ueBridgeBlockedSceneMissing = 0;
    u32 ueBridgeBlockedNoMeshInstances = 0;
    u32 ueBridgeBlockedMeshExports = 0;
    u32 ueBridgeBlockedMeshLoads = 0;
    u32 ueBridgeBlockedCamera = 0;
    u32 ueBridgeBlockedLights = 0;
    u32 ueBridgeBlockedReferenceCapture = 0;
    u32 runtimeImportModelRequested = 0;
    u32 runtimeImportModelLoaded = 0;
    u32 runtimeImportCacheHit = 0;
    u32 runtimeImportMeshCount = 0;
    u32 runtimeImportMaterialCount = 0;
    u32 runtimeImportSourceVertexCount = 0;
    u32 runtimeImportSourceTriangleCount = 0;
    u32 runtimeImportSourceTangentVertexCount = 0;
    u32 runtimeImportSourceTangentGenerationEnabled = 0;
    u32 runtimeImportSourceTexturedMaterialCount = 0;
    u32 runtimeImportSourceBaseColorTextureMaterialCount = 0;
    u32 runtimeImportSourceNormalTextureMaterialCount = 0;
    u32 runtimeImportSourceMetallicRoughnessTextureMaterialCount = 0;
    u32 runtimeImportNodeCount = 0;
    u32 runtimeImportBoneNodeCount = 0;
    u32 runtimeImportAnimationChannelBoundCount = 0;
    u32 runtimeImportAnimationChannelUnboundCount = 0;
    u32 runtimeImportBoneNameMatchedNodeCount = 0;
    u32 runtimeImportBoneNameUnmatchedCount = 0;
    u32 runtimeImportAnimationCount = 0;
    u32 runtimeImportAnimationChannelCount = 0;
    u32 runtimeImportAnimationPositionKeyCount = 0;
    u32 runtimeImportAnimationRotationKeyCount = 0;
    u32 runtimeImportAnimationScaleKeyCount = 0;
    u32 runtimeImportAnimationKeyCount = 0;
    u32 runtimeImportMaxAnimationKeysPerChannel = 0;
    u32 runtimeImportPoseSampledClipCount = 0;
    u32 runtimeImportPoseSampledChannelCount = 0;
    u32 runtimeImportPoseSampledNodeCount = 0;
    u32 runtimeImportPoseAnimatedNodeCount = 0;
    u32 runtimeImportPoseBonePaletteEntryCount = 0;
    u32 runtimeImportPosePreviousBonePaletteEntryCount = 0;
    u32 runtimeImportPoseChangedBonePaletteEntryCount = 0;
    u32 runtimeImportPoseBonePaletteReady = 0;
    u32 runtimeImportPoseCarrierBonePaletteEntryCount = 0;
    u32 runtimeImportPoseCarrierPreviousBonePaletteEntryCount = 0;
    u32 runtimeImportPoseCarrierChangedBonePaletteEntryCount = 0;
    u32 runtimeImportPoseCarrierReady = 0;
    u32 runtimeImportRendererPosePaletteRegistered = 0;
    u32 runtimeImportRendererPosePaletteBonePaletteEntryCount = 0;
    u32 runtimeImportRendererPosePalettePreviousBonePaletteEntryCount = 0;
    u32 runtimeImportRendererPosePaletteChangedBonePaletteEntryCount = 0;
    u32 runtimeImportRendererPosePaletteReady = 0;
    u32 runtimeImportGpuPosePaletteBufferAllocated = 0;
    u32 runtimeImportGpuPosePaletteBufferUploaded = 0;
    u32 runtimeImportGpuPosePaletteDescriptorInfoReady = 0;
    u32 runtimeImportGpuPosePaletteDescriptorSetAllocated = 0;
    u32 runtimeImportGpuPosePaletteDescriptorSetWritten = 0;
    u32 runtimeImportGpuPosePaletteDescriptorSetReady = 0;
    u32 runtimeImportGpuPosePaletteDescriptorBinding = 0;
    u32 runtimeImportGpuPosePaletteDescriptorRangeBytes = 0;
    u32 runtimeImportGpuPosePaletteBufferBytes = 0;
    u32 runtimeImportGpuPosePaletteCurrentEntryCount = 0;
    u32 runtimeImportGpuPosePalettePreviousEntryCount = 0;
    u32 runtimeImportMeshWithBonesCount = 0;
    u32 runtimeImportBoneCount = 0;
    u32 runtimeImportSkinnedVertexCount = 0;
    u32 runtimeImportBoneInfluenceCount = 0;
    u32 runtimeImportMaxBoneInfluencesPerVertex = 0;
    u32 runtimeImportSkinnedVertexAttributeCount = 0;
    u32 runtimeImportBoneAttributeInfluenceCount = 0;
    u32 runtimeImportMaxBoneAttributeInfluencesPerVertex = 0;
    u32 runtimeImportBoneInfluenceOverflowCount = 0;
    u32 runtimeImportSkinnedVertexAttributeReady = 0;
    u32 runtimeImportSkinnedAnimationSpaceReady = 0;
    u32 runtimeImportSkinnedAnimationSpaceBlockerMask = 0;
    u32 runtimeImportSkinnedAnimationRenderableBound = 0;
    u32 runtimeImportSkinnedAnimationSupportReady = 0;
    u32 runtimeImportSkinnedAnimationSupportBlockerMask = 0;
    u32 runtimeImportAnimationDiagnosticPoseOnly = 0;
    u32 runtimeImportAnimationPlaybackReady = 0;
    u32 runtimeImportAnimationPlaybackCandidateModelCount = 0;
    u32 runtimeImportAnimationPlaybackReadyModelCount = 0;
    u32 runtimeImportAnimationPlaybackFrameCount = 0;
    u32 runtimeImportAnimationPlaybackLoopWrapCount = 0;
    u32 runtimeImportAnimationPlaybackPreviousPoseCollapsedCount = 0;
    u32 runtimeImportAnimationPlaybackChangedBonePaletteEntryCount = 0;
    u32 runtimeImportAnimationPlaybackRendererPaletteReady = 0;
    u32 runtimeImportAnimationPlaybackGpuUploadReady = 0;
    u32 runtimeImportAnimationPlaybackBlockerMask = 0;
    u32 runtimeImportAnimationPlaybackClockMode = 0;
    f64 runtimeImportAnimationPlaybackPreviousTimeTicks = 0.0;
    f64 runtimeImportAnimationPlaybackCurrentTimeTicks = 0.0;
    f64 runtimeImportAnimationPlaybackPreviousAbsoluteSeconds = -1.0;
    f64 runtimeImportAnimationPlaybackCurrentAbsoluteSeconds = -1.0;
    u32 runtimeImportSkinnedAnimationUnsupported = 0;
    f32 benchmarkCameraMotionTimeSeconds = 0.0f;
    f32 benchmarkObjectMotionTimeSeconds = 0.0f;
};

void SetBenchmarkSceneDiagnostics(const BenchmarkSceneDiagnostics& diagnostics);
const BenchmarkSceneDiagnostics& GetBenchmarkSceneDiagnostics();

class BenchmarkRecorder {
public:
    static BenchmarkRecorderConfig ConfigFromEnvironment();

    explicit BenchmarkRecorder(BenchmarkRecorderConfig config);
    ~BenchmarkRecorder();

    SE_DISABLE_COPY(BenchmarkRecorder);
    SE_DISABLE_MOVE(BenchmarkRecorder);

    bool Enabled() const;
    bool ShouldStop() const;
    void RecordFrame(u32 renderedFrameIndex, f32 elapsedSeconds, const RendererStats& stats);

private:
    void OpenCsv();
    void WriteHeader();

private:
    BenchmarkRecorderConfig m_Config{};
    std::ofstream m_Csv;
    u32 m_CapturedFrames = 0;
    bool m_StopRequested = false;
};

}
