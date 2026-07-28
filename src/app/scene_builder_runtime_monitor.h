#pragma once

#include "core.h"

#include <filesystem>
#include <string>

namespace se {

class Camera3D;
class Scene3D;
class SceneBuilder;
class VulkanRenderResources2D;
struct RendererStats;
struct RendererFrameMonitorSnapshot;

// Writes one atomic, inspectable snapshot after every completed Scene Builder
// render frame. This is intentionally separate from the saved scene document:
// it describes runtime state and is overwritten continuously.
class SceneBuilderRuntimeMonitor {
public:
    explicit SceneBuilderRuntimeMonitor(
        std::filesystem::path path = DefaultPath()
    );

    void RecordFrame(
        u32 renderedFrameIndex,
        f32 elapsedSeconds,
        const Camera3D& camera,
        const Scene3D& scene,
        const SceneBuilder& builder,
        const VulkanRenderResources2D& renderResources,
        const RendererStats& rendererStats,
        const RendererFrameMonitorSnapshot& rendererSnapshot
    );

    static std::filesystem::path DefaultPath();
    const std::filesystem::path& Path() const;

private:
    bool WriteAtomically(const std::string& document);

    struct PreviousFrameState {
        bool available = false;
        u64 membershipRevision = 0;
        u64 renderRevision = 0;
        u64 lightRevision = 0;
        u32 antialiasingModeId = 0;
        u32 hybridActive = 0;
        u32 hybridFallbackReason = 0;
        u32 ssrBackendActiveProvider = 0;
        u32 ssrHierarchicalFallbackReason = 0;
    };

    std::filesystem::path m_Path;
    u64 m_SnapshotSequence = 0;
    bool m_PreviousWriteSucceeded = true;
    std::string m_PreviousWriteError;
    u64 m_WriteFailureCount = 0;
    u64 m_Utf8SanitizationCount = 0;
    u64 m_LastSnapshotBytes = 0;
    f32 m_LastWriteDurationMs = 0.0f;
    PreviousFrameState m_PreviousFrameState{};
};

}
