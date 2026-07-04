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
    u32 runtimeImportMeshWithBonesCount = 0;
    u32 runtimeImportBoneCount = 0;
    u32 runtimeImportSkinnedVertexCount = 0;
    u32 runtimeImportBoneInfluenceCount = 0;
    u32 runtimeImportMaxBoneInfluencesPerVertex = 0;
    u32 runtimeImportSkinnedAnimationUnsupported = 0;
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
