#pragma once

#include "core.h"

#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace se {

enum class UnrealProjectCandidateKind {
    UnrealProject,
    BridgeManifestOnly,
    ContentOnlyCandidate
};

enum class UnrealProjectBridgeSeverity {
    Info,
    Warning,
    Error
};

struct UnrealProjectBridgeMessage {
    UnrealProjectBridgeSeverity severity = UnrealProjectBridgeSeverity::Info;
    std::string text;
    std::filesystem::path path;
};

struct UnrealBridgeMeshInstanceInfo {
    std::string id;
    std::string sourceAssetId;
    std::filesystem::path exportedPath;
    std::filesystem::path plannedExportedPath;
    std::string exportStatus;
    glm::vec3 position{ 0.0f };
    glm::vec3 rotationDegrees{ 0.0f };
    glm::vec3 scale{ 1.0f };
};

struct UnrealBridgeCameraInfo {
    std::string id;
    std::string actorId;
    std::string actorName;
    std::string componentName;
    std::string componentClass;
    glm::vec3 position{ 0.0f };
    glm::vec3 rotationDegrees{ 0.0f };
    glm::vec3 forward{ 0.0f };
    glm::vec3 up{ 0.0f };
    f32 fieldOfViewDegrees = 0.0f;
    f32 aspectRatio = 0.0f;
    f32 nearClipPlane = 0.0f;
};

struct UnrealBridgeLightInfo {
    std::string id;
    std::string actorId;
    std::string actorName;
    std::string componentName;
    std::string componentClass;
    glm::vec3 position{ 0.0f };
    glm::vec3 rotationDegrees{ 0.0f };
    glm::vec3 direction{ 0.0f };
    glm::vec3 color{ 1.0f };
    f32 intensity = 0.0f;
    f32 attenuationRadius = 0.0f;
    f32 sourceRadius = 0.0f;
    f32 sourceWidth = 0.0f;
    f32 sourceHeight = 0.0f;
    f32 innerConeAngleDegrees = 0.0f;
    f32 outerConeAngleDegrees = 0.0f;
};

struct UnrealBridgeSceneInfo {
    std::string id;
    std::string name;
    std::filesystem::path sourceMapPath;
    std::filesystem::path exportedScenePath;
    std::filesystem::path plannedExportedScenePath;
    std::string exportStatus;
    u32 actorCount = 0;
    u32 meshInstanceCount = 0;
    u32 meshExportReadyCount = 0;
    u32 meshExportMissingCount = 0;
    u32 lightCount = 0;
    u32 cameraCount = 0;
    u32 referenceCaptureCount = 0;
    std::vector<UnrealBridgeMeshInstanceInfo> meshInstances;
    std::vector<UnrealBridgeCameraInfo> cameras;
    std::vector<UnrealBridgeLightInfo> lights;
};

struct UnrealBridgeManifestSummary {
    bool present = false;
    bool loaded = false;
    std::filesystem::path manifestPath;
    std::string schemaVersion;
    std::filesystem::path sourceProjectPath;
    std::filesystem::path exportedRootPath;
    u32 meshAssetCount = 0;
    u32 materialAssetCount = 0;
    u32 textureAssetCount = 0;
    u32 otherAssetCount = 0;
    u32 actorCount = 0;
    u32 meshInstanceCount = 0;
    u32 lightCount = 0;
    u32 cameraCount = 0;
    u32 referenceCaptureCount = 0;
    u32 meshExportCount = 0;
    u32 meshExportReadyCount = 0;
    u32 meshExportFailedCount = 0;
    u32 meshExportMissingCount = 0;
    std::vector<UnrealBridgeSceneInfo> scenes;
    std::vector<UnrealProjectBridgeMessage> messages;
};

struct UnrealProjectInfo {
    UnrealProjectCandidateKind kind = UnrealProjectCandidateKind::ContentOnlyCandidate;
    std::filesystem::path rootPath;
    std::string displayName;
    std::filesystem::path uprojectPath;
    std::filesystem::path bridgeManifestPath;
    std::filesystem::path contentPath;
    std::filesystem::path configPath;
    u32 mapCount = 0;
    u32 uassetCount = 0;
    u32 exportedModelCount = 0;
    u32 textureFileCount = 0;
    u32 directoryCount = 0;
    std::vector<std::filesystem::path> sampleMapPaths;
    std::vector<std::filesystem::path> sampleExportedModelPaths;
    UnrealBridgeManifestSummary bridgeManifest;
    std::vector<UnrealProjectBridgeMessage> messages;
};

struct UnrealProjectDiscoveryOptions {
    std::filesystem::path rootPath{ "D:\\UEProject" };
    std::string bridgeManifestFileName{ "SelfEngineBridge.json" };
    u32 maxSamplePaths = 8;
};

struct UnrealProjectDiscoveryResult {
    std::filesystem::path rootPath;
    std::vector<UnrealProjectInfo> projects;
    std::vector<UnrealProjectBridgeMessage> messages;
};

UnrealProjectDiscoveryResult DiscoverUnrealProjects(
    const UnrealProjectDiscoveryOptions& options = UnrealProjectDiscoveryOptions{}
);

UnrealBridgeManifestSummary LoadUnrealBridgeManifest(
    const std::filesystem::path& manifestPath
);

const char* ToString(UnrealProjectCandidateKind kind);
const char* ToString(UnrealProjectBridgeSeverity severity);
std::string UnrealPathToUtf8(const std::filesystem::path& path);

}
