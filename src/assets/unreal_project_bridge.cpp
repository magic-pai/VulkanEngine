#include "assets/unreal_project_bridge.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace se {

namespace {

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::filesystem::path PathFromUtf8(const std::string& text) {
    if (text.empty()) {
        return {};
    }

#ifdef _WIN32
    const int wideSize = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (wideSize > 0) {
        std::wstring wide(static_cast<std::size_t>(wideSize), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            wideSize
        );
        return std::filesystem::path(wide);
    }
#endif

    return std::filesystem::path(text);
}

std::string ExtensionLower(const std::filesystem::path& path) {
    return LowerAscii(path.extension().string());
}

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

bool IsDirectory(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error) && !error;
}

std::optional<std::filesystem::path> FindFirstUProject(
    const std::filesystem::path& directory
) {
    std::error_code error;
    std::filesystem::directory_iterator iterator(
        directory,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    if (error) {
        return std::nullopt;
    }

    for (const std::filesystem::directory_entry& entry : iterator) {
        const std::filesystem::path path = entry.path();
        if (IsRegularFile(path) && ExtensionLower(path) == ".uproject") {
            return path;
        }
    }

    return std::nullopt;
}

bool IsExportedModelExtension(const std::string& extension) {
    static constexpr std::array<const char*, 8> kExtensions{
        ".gltf",
        ".glb",
        ".fbx",
        ".usd",
        ".usda",
        ".usdc",
        ".obj",
        ".dae"
    };

    return std::find(kExtensions.begin(), kExtensions.end(), extension) != kExtensions.end();
}

bool IsTextureExtension(const std::string& extension) {
    static constexpr std::array<const char*, 12> kExtensions{
        ".png",
        ".jpg",
        ".jpeg",
        ".tga",
        ".bmp",
        ".exr",
        ".hdr",
        ".dds",
        ".ktx",
        ".ktx2",
        ".tif",
        ".tiff"
    };

    return std::find(kExtensions.begin(), kExtensions.end(), extension) != kExtensions.end();
}

void PushMessage(
    std::vector<UnrealProjectBridgeMessage>& messages,
    UnrealProjectBridgeSeverity severity,
    std::string text,
    std::filesystem::path path = {}
) {
    messages.push_back({
        severity,
        std::move(text),
        std::move(path)
    });
}

void PushSamplePath(
    std::vector<std::filesystem::path>& samples,
    const std::filesystem::path& path,
    u32 maxSamplePaths
) {
    if (samples.size() < static_cast<std::size_t>(maxSamplePaths)) {
        samples.push_back(path);
    }
}

const nlohmann::json* FindField(
    const nlohmann::json& object,
    std::initializer_list<const char*> names
) {
    if (!object.is_object()) {
        return nullptr;
    }

    for (const char* name : names) {
        const auto found = object.find(name);
        if (found != object.end()) {
            return &(*found);
        }
    }

    return nullptr;
}

std::string ReadString(
    const nlohmann::json& object,
    std::initializer_list<const char*> names
) {
    const nlohmann::json* value = FindField(object, names);
    if (value == nullptr || !value->is_string()) {
        return {};
    }

    return value->get<std::string>();
}

std::filesystem::path ReadPath(
    const nlohmann::json& object,
    std::initializer_list<const char*> names
) {
    return PathFromUtf8(ReadString(object, names));
}

float ReadNumber(const nlohmann::json& value, float fallback) {
    return value.is_number() ? value.get<float>() : fallback;
}

float ReadFloat(
    const nlohmann::json& object,
    std::initializer_list<const char*> names,
    float fallback = 0.0f
) {
    const nlohmann::json* value = FindField(object, names);
    return value == nullptr ? fallback : ReadNumber(*value, fallback);
}

glm::vec3 ReadVec3(
    const nlohmann::json& object,
    std::initializer_list<const char*> names,
    glm::vec3 fallback
) {
    const nlohmann::json* value = FindField(object, names);
    if (value == nullptr) {
        return fallback;
    }
    if (value->is_array() && value->size() >= 3) {
        return glm::vec3(
            ReadNumber(value->at(0), fallback.x),
            ReadNumber(value->at(1), fallback.y),
            ReadNumber(value->at(2), fallback.z)
        );
    }
    if (value->is_object()) {
        return glm::vec3(
            ReadFloat(*value, { "x", "X", "r", "R" }, fallback.x),
            ReadFloat(*value, { "y", "Y", "g", "G" }, fallback.y),
            ReadFloat(*value, { "z", "Z", "b", "B" }, fallback.z)
        );
    }

    return fallback;
}

glm::vec3 ReadColor3(
    const nlohmann::json& object,
    std::initializer_list<const char*> names,
    glm::vec3 fallback = glm::vec3(1.0f)
) {
    glm::vec3 color = ReadVec3(object, names, fallback);
    const float maxChannel = std::max({ color.x, color.y, color.z });
    if (maxChannel > 1.0f && maxChannel <= 255.0f) {
        color /= 255.0f;
    }

    return glm::max(color, glm::vec3(0.0f));
}

u32 SaturatingU32(std::size_t value) {
    return value > static_cast<std::size_t>(std::numeric_limits<u32>::max()) ?
        std::numeric_limits<u32>::max() :
        static_cast<u32>(value);
}

u32 CountArrayNumberOrObjectField(
    const nlohmann::json& object,
    std::initializer_list<const char*> names
) {
    const nlohmann::json* value = FindField(object, names);
    if (value == nullptr) {
        return 0;
    }
    if (value->is_array()) {
        return SaturatingU32(std::count_if(value->begin(), value->end(), [](const nlohmann::json& item) {
            return !item.is_null();
        }));
    }
    if (value->is_object()) {
        return SaturatingU32(value->size());
    }
    if (value->is_number_unsigned()) {
        return value->get<u32>();
    }
    if (value->is_number_integer()) {
        const i64 parsed = value->get<i64>();
        return parsed > 0 ? static_cast<u32>(parsed) : 0;
    }

    return 0;
}

void CountAssetArray(
    const nlohmann::json& assets,
    UnrealBridgeManifestSummary& summary
) {
    if (!assets.is_array()) {
        return;
    }

    for (const nlohmann::json& asset : assets) {
        const std::string kind = LowerAscii(ReadString(asset, {
            "kind",
            "type",
            "assetKind",
            "category",
            "class"
        }));
        if (kind.find("mesh") != std::string::npos) {
            ++summary.meshAssetCount;
        } else if (kind.find("material") != std::string::npos ||
            kind.find("mat") != std::string::npos) {
            ++summary.materialAssetCount;
        } else if (kind.find("texture") != std::string::npos ||
            kind.find("tex") != std::string::npos) {
            ++summary.textureAssetCount;
        } else {
            ++summary.otherAssetCount;
        }
    }
}

void CountAssetObject(
    const nlohmann::json& root,
    UnrealBridgeManifestSummary& summary
) {
    const nlohmann::json* assets = FindField(root, { "assets", "assetRegistry" });
    if (assets != nullptr && assets->is_array()) {
        CountAssetArray(*assets, summary);
        return;
    }

    const nlohmann::json& assetRoot =
        (assets != nullptr && assets->is_object()) ? *assets : root;
    summary.meshAssetCount += CountArrayNumberOrObjectField(assetRoot, {
        "meshes",
        "meshAssets",
        "staticMeshes",
        "skeletalMeshes"
    });
    summary.materialAssetCount += CountArrayNumberOrObjectField(assetRoot, {
        "materials",
        "materialAssets",
        "materialInstances"
    });
    summary.textureAssetCount += CountArrayNumberOrObjectField(assetRoot, {
        "textures",
        "textureAssets"
    });
    summary.otherAssetCount += CountArrayNumberOrObjectField(assetRoot, {
        "otherAssets",
        "unsupportedAssets"
    });
}

void CountMeshExportArray(
    const nlohmann::json& root,
    UnrealBridgeManifestSummary& summary
) {
    const nlohmann::json* meshExports = FindField(root, { "meshExports", "exportedMeshes" });
    if (meshExports == nullptr || !meshExports->is_array()) {
        return;
    }

    summary.meshExportCount = SaturatingU32(meshExports->size());
    for (const nlohmann::json& exportRecord : *meshExports) {
        if (!exportRecord.is_object()) {
            continue;
        }

        const std::string exportedPath = ReadString(exportRecord, {
            "exportedPath",
            "path",
            "filename"
        });
        const std::string status = LowerAscii(ReadString(exportRecord, {
            "exportStatus",
            "status"
        }));
        if (!exportedPath.empty()) {
            ++summary.meshExportReadyCount;
        } else if (status.find("failed") != std::string::npos) {
            ++summary.meshExportFailedCount;
        } else {
            ++summary.meshExportMissingCount;
        }
    }
}

std::vector<UnrealBridgeMeshInstanceInfo> ReadMeshInstances(const nlohmann::json& scene);
std::vector<UnrealBridgeCameraInfo> ReadCameras(const nlohmann::json& scene);
std::vector<UnrealBridgeLightInfo> ReadLights(const nlohmann::json& scene);

UnrealBridgeSceneInfo ReadSceneInfo(const nlohmann::json& scene) {
    UnrealBridgeSceneInfo info{};
    info.id = ReadString(scene, { "id", "sourceId", "stableId", "mapId" });
    info.name = ReadString(scene, { "name", "displayName", "mapName", "sceneName" });
    info.sourceMapPath = ReadPath(scene, { "sourceMapPath", "sourceMap", "mapPath" });
    info.exportedScenePath = ReadPath(scene, {
        "exportedScenePath",
        "exportedPath",
        "scenePath",
        "gltfPath",
        "usdPath"
    });
    info.plannedExportedScenePath = ReadPath(scene, {
        "plannedExportedScenePath",
        "plannedExportPath",
        "plannedScenePath"
    });
    info.exportStatus = ReadString(scene, { "exportStatus", "status" });
    info.actorCount = CountArrayNumberOrObjectField(scene, { "actorCount", "actors" });
    info.meshInstanceCount = CountArrayNumberOrObjectField(scene, {
        "meshInstanceCount",
        "meshInstances",
        "staticMeshComponents",
        "renderComponents"
    });
    info.meshExportReadyCount = CountArrayNumberOrObjectField(scene, {
        "meshExportReadyCount",
        "meshExportsReady"
    });
    info.meshExportMissingCount = CountArrayNumberOrObjectField(scene, {
        "meshExportMissingCount",
        "meshExportsMissing"
    });
    info.lightCount = CountArrayNumberOrObjectField(scene, { "lightCount", "lights" });
    info.cameraCount = CountArrayNumberOrObjectField(scene, { "cameraCount", "cameras" });
    info.referenceCaptureCount = CountArrayNumberOrObjectField(scene, {
        "referenceCaptureCount",
        "referenceCaptures",
        "ueViewportReferences",
        "viewportReferences"
    });
    info.meshInstances = ReadMeshInstances(scene);
    info.cameras = ReadCameras(scene);
    info.lights = ReadLights(scene);
    if (info.meshInstanceCount == 0) {
        info.meshInstanceCount = SaturatingU32(info.meshInstances.size());
    }
    if (info.cameraCount == 0) {
        info.cameraCount = SaturatingU32(info.cameras.size());
    }
    if (info.lightCount == 0) {
        info.lightCount = SaturatingU32(info.lights.size());
    }

    if (info.name.empty()) {
        info.name = info.sourceMapPath.stem().empty() ?
            info.exportedScenePath.stem().string() :
            UnrealPathToUtf8(info.sourceMapPath.stem());
    }

    return info;
}

void ReadSceneArray(
    const nlohmann::json& root,
    UnrealBridgeManifestSummary& summary
) {
    const nlohmann::json* scenes = FindField(root, { "scenes", "maps", "levels" });
    if (scenes == nullptr || !scenes->is_array()) {
        return;
    }

    summary.scenes.reserve(scenes->size());
    for (const nlohmann::json& scene : *scenes) {
        UnrealBridgeSceneInfo info = ReadSceneInfo(scene);
        summary.actorCount += info.actorCount;
        summary.meshInstanceCount += info.meshInstanceCount;
        summary.lightCount += info.lightCount;
        summary.cameraCount += info.cameraCount;
        summary.referenceCaptureCount += info.referenceCaptureCount;
        if (summary.meshExportCount == 0) {
            summary.meshExportReadyCount += info.meshExportReadyCount;
            summary.meshExportMissingCount += info.meshExportMissingCount;
        }
        summary.scenes.push_back(std::move(info));
    }

    if (summary.meshExportCount == 0) {
        summary.meshExportCount =
            summary.meshExportReadyCount + summary.meshExportMissingCount;
    }
}

std::vector<UnrealBridgeMeshInstanceInfo> ReadMeshInstances(const nlohmann::json& scene) {
    std::vector<UnrealBridgeMeshInstanceInfo> instances;
    const nlohmann::json* meshInstances = FindField(scene, {
        "meshInstances",
        "staticMeshComponents",
        "renderComponents"
    });
    if (meshInstances == nullptr || !meshInstances->is_array()) {
        return instances;
    }

    instances.reserve(meshInstances->size());
    for (const nlohmann::json& instance : *meshInstances) {
        if (!instance.is_object()) {
            continue;
        }

        UnrealBridgeMeshInstanceInfo info{};
        info.id = ReadString(instance, { "id", "componentId", "name" });
        info.sourceAssetId = ReadString(instance, { "sourceAssetId", "mesh", "meshAsset" });
        info.exportedPath = ReadPath(instance, {
            "exportedPath",
            "exportedScenePath",
            "gltfPath",
            "usdPath"
        });
        info.plannedExportedPath = ReadPath(instance, {
            "plannedExportedPath",
            "plannedExportPath"
        });
        info.exportStatus = ReadString(instance, { "exportStatus", "status" });

        const nlohmann::json* transform = FindField(instance, { "transform", "worldTransform" });
        if (transform != nullptr && transform->is_object()) {
            info.position = ReadVec3(*transform, { "position", "translation", "location" }, info.position);
            info.rotationDegrees = ReadVec3(
                *transform,
                { "rotationDegrees", "rotation", "eulerDegrees" },
                info.rotationDegrees
            );
            info.scale = ReadVec3(*transform, { "scale", "scale3d" }, info.scale);
        }

        instances.push_back(std::move(info));
    }

    return instances;
}

std::vector<UnrealBridgeCameraInfo> ReadCameras(const nlohmann::json& scene) {
    std::vector<UnrealBridgeCameraInfo> cameras;
    const nlohmann::json* cameraArray = FindField(scene, {
        "cameras",
        "cameraComponents",
        "viewportCameras"
    });
    if (cameraArray == nullptr || !cameraArray->is_array()) {
        return cameras;
    }

    cameras.reserve(cameraArray->size());
    for (const nlohmann::json& camera : *cameraArray) {
        if (!camera.is_object()) {
            continue;
        }

        UnrealBridgeCameraInfo info{};
        info.id = ReadString(camera, { "id", "componentId", "name" });
        info.actorId = ReadString(camera, { "actorId", "ownerId" });
        info.actorName = ReadString(camera, { "actorName", "ownerName" });
        info.componentName = ReadString(camera, { "componentName", "name" });
        info.componentClass = ReadString(camera, { "componentClass", "class", "type" });

        const nlohmann::json* transform = FindField(camera, { "transform", "worldTransform" });
        if (transform != nullptr && transform->is_object()) {
            info.position = ReadVec3(*transform, { "position", "translation", "location" }, info.position);
            info.rotationDegrees = ReadVec3(
                *transform,
                { "rotationDegrees", "rotation", "eulerDegrees" },
                info.rotationDegrees
            );
            info.forward = ReadVec3(*transform, { "forward", "direction" }, info.forward);
            info.up = ReadVec3(*transform, { "up" }, info.up);
        }

        info.position = ReadVec3(camera, { "position", "translation", "location" }, info.position);
        info.rotationDegrees = ReadVec3(
            camera,
            { "rotationDegrees", "rotation", "eulerDegrees" },
            info.rotationDegrees
        );
        info.forward = ReadVec3(camera, { "forward", "direction" }, info.forward);
        info.up = ReadVec3(camera, { "up" }, info.up);
        info.fieldOfViewDegrees = ReadFloat(camera, { "fieldOfView", "fieldOfViewDegrees", "fov" });
        info.aspectRatio = ReadFloat(camera, { "aspectRatio" });
        info.nearClipPlane = ReadFloat(camera, { "nearClipPlane" });

        cameras.push_back(std::move(info));
    }

    return cameras;
}

std::vector<UnrealBridgeLightInfo> ReadLights(const nlohmann::json& scene) {
    std::vector<UnrealBridgeLightInfo> lights;
    const nlohmann::json* lightArray = FindField(scene, {
        "lights",
        "lightComponents"
    });
    if (lightArray == nullptr || !lightArray->is_array()) {
        return lights;
    }

    lights.reserve(lightArray->size());
    for (const nlohmann::json& light : *lightArray) {
        if (!light.is_object()) {
            continue;
        }

        UnrealBridgeLightInfo info{};
        info.id = ReadString(light, { "id", "componentId", "name" });
        info.actorId = ReadString(light, { "actorId", "ownerId" });
        info.actorName = ReadString(light, { "actorName", "ownerName" });
        info.componentName = ReadString(light, { "componentName", "name" });
        info.componentClass = ReadString(light, { "componentClass", "class", "type" });

        const nlohmann::json* transform = FindField(light, { "transform", "worldTransform" });
        if (transform != nullptr && transform->is_object()) {
            info.position = ReadVec3(*transform, { "position", "translation", "location" }, info.position);
            info.rotationDegrees = ReadVec3(
                *transform,
                { "rotationDegrees", "rotation", "eulerDegrees" },
                info.rotationDegrees
            );
            info.direction = ReadVec3(*transform, { "forward", "direction" }, info.direction);
        }

        info.position = ReadVec3(light, { "position", "translation", "location" }, info.position);
        info.rotationDegrees = ReadVec3(
            light,
            { "rotationDegrees", "rotation", "eulerDegrees" },
            info.rotationDegrees
        );
        info.direction = ReadVec3(light, { "forward", "direction" }, info.direction);
        info.color = ReadColor3(light, { "lightColor", "color", "linearColor" }, info.color);
        info.intensity = ReadFloat(light, { "intensity" });
        info.attenuationRadius = ReadFloat(light, { "attenuationRadius", "radius" });
        info.sourceRadius = ReadFloat(light, { "sourceRadius" });
        info.sourceWidth = ReadFloat(light, { "sourceWidth", "rectWidth", "width" });
        info.sourceHeight = ReadFloat(light, { "sourceHeight", "rectHeight", "height" });
        info.innerConeAngleDegrees = ReadFloat(light, { "innerConeAngle", "innerConeAngleDegrees" });
        info.outerConeAngleDegrees = ReadFloat(light, { "outerConeAngle", "outerConeAngleDegrees" });

        lights.push_back(std::move(info));
    }

    return lights;
}

void CountContentFiles(UnrealProjectInfo& info, const UnrealProjectDiscoveryOptions& options) {
    if (!IsDirectory(info.contentPath)) {
        return;
    }

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        info.contentPath,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    if (error) {
        PushMessage(
            info.messages,
            UnrealProjectBridgeSeverity::Warning,
            "Content scan could not start",
            info.contentPath
        );
        return;
    }

    for (const std::filesystem::directory_entry& entry : iterator) {
        const std::filesystem::path path = entry.path();
        if (entry.is_directory(error) && !error) {
            ++info.directoryCount;
            continue;
        }
        error.clear();
        if (!entry.is_regular_file(error) || error) {
            continue;
        }

        const std::string extension = ExtensionLower(path);
        if (extension == ".umap") {
            ++info.mapCount;
            PushSamplePath(info.sampleMapPaths, path, options.maxSamplePaths);
        } else if (extension == ".uasset") {
            ++info.uassetCount;
        } else if (IsExportedModelExtension(extension)) {
            ++info.exportedModelCount;
            PushSamplePath(info.sampleExportedModelPaths, path, options.maxSamplePaths);
        } else if (IsTextureExtension(extension)) {
            ++info.textureFileCount;
        }
    }
}

std::string ProjectDisplayName(
    const std::filesystem::path& candidatePath,
    const std::optional<std::filesystem::path>& uprojectPath
) {
    if (uprojectPath.has_value()) {
        const std::string stem = UnrealPathToUtf8(uprojectPath->stem());
        if (!stem.empty()) {
            return stem;
        }
    }

    const std::string filename = UnrealPathToUtf8(candidatePath.filename());
    return filename.empty() ? UnrealPathToUtf8(candidatePath) : filename;
}

std::optional<UnrealProjectInfo> InspectCandidate(
    const std::filesystem::path& candidatePath,
    const UnrealProjectDiscoveryOptions& options
) {
    if (!IsDirectory(candidatePath)) {
        return std::nullopt;
    }

    const std::optional<std::filesystem::path> uprojectPath =
        FindFirstUProject(candidatePath);
    const std::filesystem::path rootManifestPath =
        candidatePath / options.bridgeManifestFileName;
    const std::filesystem::path savedManifestPath =
        candidatePath / "Saved" / options.bridgeManifestFileName;
    const bool hasRootManifest = IsRegularFile(rootManifestPath);
    const bool hasSavedManifest = IsRegularFile(savedManifestPath);
    const bool hasManifest = hasRootManifest || hasSavedManifest;
    const std::filesystem::path contentPath =
        IsDirectory(candidatePath / "Content") ? candidatePath / "Content" : candidatePath;
    const bool hasContent = IsDirectory(contentPath);
    const bool hasConfig = IsDirectory(candidatePath / "Config");
    const bool candidateLooksLikeContentRoot =
        LowerAscii(candidatePath.filename().string()) == "content";

    if (!uprojectPath.has_value() && !hasManifest && !hasConfig &&
        !candidateLooksLikeContentRoot && !IsDirectory(candidatePath / "Content")) {
        return std::nullopt;
    }

    UnrealProjectInfo info{};
    info.rootPath = candidatePath;
    info.displayName = ProjectDisplayName(candidatePath, uprojectPath);
    info.uprojectPath = uprojectPath.value_or(std::filesystem::path{});
    info.bridgeManifestPath = hasRootManifest ? rootManifestPath :
        (hasSavedManifest ? savedManifestPath : rootManifestPath);
    info.contentPath = contentPath;
    info.configPath = candidatePath / "Config";
    info.kind = uprojectPath.has_value() ? UnrealProjectCandidateKind::UnrealProject :
        (hasManifest ? UnrealProjectCandidateKind::BridgeManifestOnly :
            UnrealProjectCandidateKind::ContentOnlyCandidate);

    if (uprojectPath.has_value()) {
        PushMessage(
            info.messages,
            UnrealProjectBridgeSeverity::Info,
            "Found .uproject root",
            *uprojectPath
        );
    } else if (hasManifest) {
        PushMessage(
            info.messages,
            UnrealProjectBridgeSeverity::Info,
            "Found bridge manifest without a .uproject file beside it",
            info.bridgeManifestPath
        );
    } else {
        PushMessage(
            info.messages,
            UnrealProjectBridgeSeverity::Warning,
            "Candidate has UE-like folders but no .uproject; treat as content-only until a bridge manifest exists",
            candidatePath
        );
    }

    if (!hasContent) {
        PushMessage(
            info.messages,
            UnrealProjectBridgeSeverity::Warning,
            "Content directory is missing or not readable",
            contentPath
        );
    }
    if (!hasConfig && uprojectPath.has_value()) {
        PushMessage(
            info.messages,
            UnrealProjectBridgeSeverity::Warning,
            "Config directory is missing or not readable",
            info.configPath
        );
    }

    info.bridgeManifest = LoadUnrealBridgeManifest(info.bridgeManifestPath);
    info.messages.insert(
        info.messages.end(),
        info.bridgeManifest.messages.begin(),
        info.bridgeManifest.messages.end()
    );

    CountContentFiles(info, options);
    return info;
}

void AddRootMessage(
    UnrealProjectDiscoveryResult& result,
    UnrealProjectBridgeSeverity severity,
    std::string text,
    std::filesystem::path path
) {
    PushMessage(result.messages, severity, std::move(text), std::move(path));
}

}

UnrealProjectDiscoveryResult DiscoverUnrealProjects(
    const UnrealProjectDiscoveryOptions& options
) {
    UnrealProjectDiscoveryResult result{};
    result.rootPath = options.rootPath.empty() ?
        std::filesystem::path("D:\\UEProject") :
        options.rootPath;

    if (!IsDirectory(result.rootPath)) {
        AddRootMessage(
            result,
            UnrealProjectBridgeSeverity::Error,
            "UE project discovery root is missing or not readable",
            result.rootPath
        );
        return result;
    }

    if (std::optional<UnrealProjectInfo> rootProject =
        InspectCandidate(result.rootPath, options)) {
        result.projects.push_back(std::move(*rootProject));
        return result;
    }

    std::error_code error;
    std::filesystem::directory_iterator iterator(
        result.rootPath,
        std::filesystem::directory_options::skip_permission_denied,
        error
    );
    if (error) {
        AddRootMessage(
            result,
            UnrealProjectBridgeSeverity::Error,
            "UE project discovery root could not be enumerated",
            result.rootPath
        );
        return result;
    }

    for (const std::filesystem::directory_entry& entry : iterator) {
        if (!entry.is_directory(error) || error) {
            error.clear();
            continue;
        }

        const std::filesystem::path candidatePath = entry.path();
        if (candidatePath == result.rootPath) {
            continue;
        }
        if (std::optional<UnrealProjectInfo> project =
            InspectCandidate(candidatePath, options)) {
            result.projects.push_back(std::move(*project));
        }
    }

    if (result.projects.empty()) {
        AddRootMessage(
            result,
            UnrealProjectBridgeSeverity::Warning,
            "No UE project roots or bridge manifests were discovered",
            result.rootPath
        );
    }

    return result;
}

UnrealBridgeManifestSummary LoadUnrealBridgeManifest(
    const std::filesystem::path& manifestPath
) {
    UnrealBridgeManifestSummary summary{};
    summary.manifestPath = manifestPath;

    if (manifestPath.empty() || !IsRegularFile(manifestPath)) {
        PushMessage(
            summary.messages,
            UnrealProjectBridgeSeverity::Warning,
            "Bridge manifest is missing; direct .uasset/.umap parsing is not the Phase A path",
            manifestPath
        );
        return summary;
    }

    summary.present = true;
    std::ifstream input(manifestPath);
    if (!input) {
        PushMessage(
            summary.messages,
            UnrealProjectBridgeSeverity::Error,
            "Bridge manifest exists but could not be opened",
            manifestPath
        );
        return summary;
    }

    nlohmann::json document;
    try {
        input >> document;
    } catch (const nlohmann::json::exception& exception) {
        PushMessage(
            summary.messages,
            UnrealProjectBridgeSeverity::Error,
            std::string("Bridge manifest JSON parse failed: ") + exception.what(),
            manifestPath
        );
        return summary;
    }

    if (!document.is_object()) {
        PushMessage(
            summary.messages,
            UnrealProjectBridgeSeverity::Error,
            "Bridge manifest root must be a JSON object",
            manifestPath
        );
        return summary;
    }

    summary.loaded = true;
    summary.schemaVersion = ReadString(document, { "schemaVersion", "version", "schema" });
    summary.sourceProjectPath = ReadPath(document, {
        "sourceProjectPath",
        "sourceProject",
        "ueProjectPath",
        "projectPath"
    });
    summary.exportedRootPath = ReadPath(document, {
        "exportedRootPath",
        "exportRoot",
        "exportedRoot",
        "assetRoot"
    });

    CountAssetObject(document, summary);
    CountMeshExportArray(document, summary);
    ReadSceneArray(document, summary);

    if (summary.actorCount == 0) {
        summary.actorCount = CountArrayNumberOrObjectField(document, { "actorCount", "actors" });
    }
    if (summary.meshInstanceCount == 0) {
        summary.meshInstanceCount = CountArrayNumberOrObjectField(document, {
            "meshInstanceCount",
            "meshInstances",
            "staticMeshComponents",
            "renderComponents"
        });
    }
    if (summary.lightCount == 0) {
        summary.lightCount = CountArrayNumberOrObjectField(document, { "lightCount", "lights" });
    }
    if (summary.cameraCount == 0) {
        summary.cameraCount = CountArrayNumberOrObjectField(document, { "cameraCount", "cameras" });
    }
    if (summary.referenceCaptureCount == 0) {
        summary.referenceCaptureCount = CountArrayNumberOrObjectField(document, {
            "referenceCaptureCount",
            "referenceCaptures",
            "ueViewportReferences",
            "viewportReferences"
        });
    }

    PushMessage(
        summary.messages,
        UnrealProjectBridgeSeverity::Info,
        "Loaded bridge manifest summary",
        manifestPath
    );
    if (summary.schemaVersion.empty()) {
        PushMessage(
            summary.messages,
            UnrealProjectBridgeSeverity::Warning,
            "Bridge manifest has no schemaVersion/version field",
            manifestPath
        );
    }
    if (summary.scenes.empty()) {
        PushMessage(
            summary.messages,
            UnrealProjectBridgeSeverity::Warning,
            "Bridge manifest has no scenes/maps array yet",
            manifestPath
        );
    }

    return summary;
}

const char* ToString(UnrealProjectCandidateKind kind) {
    switch (kind) {
    case UnrealProjectCandidateKind::UnrealProject:
        return "UnrealProject";
    case UnrealProjectCandidateKind::BridgeManifestOnly:
        return "BridgeManifestOnly";
    case UnrealProjectCandidateKind::ContentOnlyCandidate:
        return "ContentOnlyCandidate";
    }

    return "Unknown";
}

const char* ToString(UnrealProjectBridgeSeverity severity) {
    switch (severity) {
    case UnrealProjectBridgeSeverity::Info:
        return "info";
    case UnrealProjectBridgeSeverity::Warning:
        return "warning";
    case UnrealProjectBridgeSeverity::Error:
        return "error";
    }

    return "unknown";
}

std::string UnrealPathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (size <= 0) {
        return path.string();
    }

    std::string text(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        text.data(),
        size,
        nullptr,
        nullptr
    );
    return text;
#else
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
#endif
}

}
