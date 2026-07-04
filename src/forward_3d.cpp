#include "assets/unreal_project_bridge.h"
#include "app/application.h"
#include "app/benchmark_recorder.h"
#include "renderer/vulkan/material_library.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/pipeline_spec.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "renderer/vulkan/renderer.h"
#include "renderer/render_queue.h"
#include "scene/camera_3d.h"
#include "scene/mesh_factory.h"
#include "scene/renderable_3d.h"
#include "scene/runtime_model_loader.h"
#include "scene/scene_3d.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifndef SE_SHADER_DIR
#define SE_SHADER_DIR "shaders"
#endif

#ifndef SE_ASSET_DIR
#define SE_ASSET_DIR "assets"
#endif

namespace {

bool ImGuiWantsMouse() {
    return ImGui::GetCurrentContext() != nullptr &&
        ImGui::GetIO().WantCaptureMouse;
}

struct PickRay {
    glm::vec3 origin{ 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
};

struct PickClickState {
    bool previousLeftDown = false;
    bool trackingClick = false;
    se::f32 heldSeconds = 0.0f;
    std::array<se::f64, 2> pressPosition{};
};

struct StartupBridgeScene {
    bool requested = false;
    bool manifestLoaded = false;
    bool sceneFound = false;
    bool exportedSceneReady = false;
    std::string requestedScene;
    std::filesystem::path manifestPath;
    std::string sceneId;
    std::string sceneName;
    std::filesystem::path exportedScenePath;
    std::filesystem::path plannedExportedScenePath;
    std::string exportStatus;
    std::vector<se::UnrealBridgeMeshInstanceInfo> meshInstances;
    std::vector<se::UnrealBridgeCameraInfo> cameras;
    std::vector<se::UnrealBridgeLightInfo> lights;
    se::u32 referenceCaptureCount = 0;
    se::u32 manifestMeshExportReadyCount = 0;
    se::u32 manifestMeshExportMissingCount = 0;
    se::u32 meshInstanceExportReadyCount = 0;
    se::u32 meshInstanceExportMissingCount = 0;
    std::vector<se::UnrealProjectBridgeMessage> messages;
};

struct BridgeSceneLightApplyResult {
    bool anyApplied = false;
    bool directionalApplied = false;
    se::u32 pointCount = 0;
    se::u32 spotCount = 0;
    se::u32 rectCount = 0;
    se::u32 skyLightCount = 0;
    bool skyLightApplied = false;
    se::f32 skyLightIntensity = 1.0f;
};

PickRay CursorPickRay(
    const se::Window& window,
    const se::Camera3D& camera
) {
    const std::array<se::f64, 2> cursorPosition = window.CursorPosition();
    const std::array<int, 2> windowSize = window.WindowSize();
    const se::f32 windowWidth = static_cast<se::f32>(std::max(windowSize[0], 1));
    const se::f32 windowHeight = static_cast<se::f32>(std::max(windowSize[1], 1));
    const se::f32 framebufferWidth = static_cast<se::f32>(std::max(window.GetWidth(), 1));
    const se::f32 framebufferHeight = static_cast<se::f32>(std::max(window.GetHeight(), 1));
    const se::f32 aspectRatio = framebufferWidth / framebufferHeight;

    const se::f32 ndcX =
        (static_cast<se::f32>(cursorPosition[0]) / windowWidth) * 2.0f - 1.0f;
    const se::f32 ndcY =
        (static_cast<se::f32>(cursorPosition[1]) / windowHeight) * 2.0f - 1.0f;

    const glm::mat4 inverseViewProjection =
        glm::inverse(camera.ProjectionMatrix(aspectRatio) * camera.ViewMatrix());
    const glm::vec4 nearPositionH =
        inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farPositionH =
        inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 nearPosition = glm::vec3(nearPositionH) / nearPositionH.w;
    const glm::vec3 farPosition = glm::vec3(farPositionH) / farPositionH.w;

    return PickRay{
        nearPosition,
        glm::normalize(farPosition - nearPosition)
    };
}

void HandleScenePicking(
    se::Window& window,
    const se::Camera3D& camera,
    se::Scene3D& scene,
    se::f32 deltaSeconds,
    PickClickState& clickState
) {
    constexpr se::f32 kMaxClickSeconds = 0.22f;
    constexpr se::f64 kMaxClickMovementPixels = 6.0;
    constexpr se::f64 kMaxClickMovementSquared =
        kMaxClickMovementPixels * kMaxClickMovementPixels;

    const bool leftDown = window.IsLeftMouseDown();
    const bool imguiWantsMouse = ImGuiWantsMouse();

    if (leftDown && !clickState.previousLeftDown) {
        clickState.trackingClick = !imguiWantsMouse;
        clickState.heldSeconds = 0.0f;
        clickState.pressPosition = window.CursorPosition();
    }

    if (leftDown && clickState.trackingClick) {
        clickState.heldSeconds += std::max(deltaSeconds, 0.0f);
    }

    if (!leftDown && clickState.previousLeftDown) {
        if (clickState.trackingClick && !imguiWantsMouse) {
            const std::array<se::f64, 2> releasePosition = window.CursorPosition();
            const se::f64 deltaX = releasePosition[0] - clickState.pressPosition[0];
            const se::f64 deltaY = releasePosition[1] - clickState.pressPosition[1];
            const se::f64 movementSquared = deltaX * deltaX + deltaY * deltaY;

            if (clickState.heldSeconds <= kMaxClickSeconds &&
                movementSquared <= kMaxClickMovementSquared) {
                const PickRay ray = CursorPickRay(window, camera);
                scene.SelectAlongRay(ray.origin, ray.direction);
            }
        }

        clickState.trackingClick = false;
        clickState.heldSeconds = 0.0f;
    }

    clickState.previousLeftDown = leftDown;
}

se::MaterialProperties ForwardMaterial(
    std::array<se::f32, 4> baseColor,
    se::f32 textureMix,
    se::f32 ambient,
    se::f32 diffuse,
    se::f32 specular,
    se::f32 shininess
) {
    se::MaterialProperties properties{};
    properties.baseColorFactor = baseColor;
    properties.textureMix = textureMix;
    properties.custom = { -0.45f, -0.82f, -0.35f, ambient };
    properties.viewControls = { diffuse, specular, shininess, 0.0f };
    properties.cameraControls = { 0.0f, 0.65f, 0.0f, 0.0f };

    return properties;
}

glm::vec3 SceneKeyLightDirection(const se::Camera3D& camera) {
    const glm::vec3& cameraDirection = camera.Forward();
    const glm::vec3 worldUp{ 0.0f, 1.0f, 0.0f };
    glm::vec3 right = glm::cross(cameraDirection, worldUp);
    if (glm::dot(right, right) < 0.0001f) {
        right = { 1.0f, 0.0f, 0.0f };
    } else {
        right = glm::normalize(right);
    }
    const glm::vec3 up = glm::normalize(glm::cross(right, cameraDirection));
    const glm::vec3 directionToLight =
        glm::normalize(-cameraDirection + up * 0.55f - right * 0.25f);

    return -directionToLight;
}

void ApplySceneDirectionalLight(
    se::Scene3D& scene,
    const se::Camera3D& camera
) {
    scene.SetPrimaryDirectionalLight(
        "Camera Key Directional Light",
        SceneKeyLightDirection(camera),
        0.78f,
        0.22f,
        0.24f
    );
}

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

glm::vec3 DirectionFromRotationDegrees(glm::vec3 rotationDegrees) {
    glm::mat4 rotation{ 1.0f };
    rotation = glm::rotate(
        rotation,
        glm::radians(rotationDegrees.x),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );
    rotation = glm::rotate(
        rotation,
        glm::radians(rotationDegrees.y),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    rotation = glm::rotate(
        rotation,
        glm::radians(rotationDegrees.z),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    glm::vec3 direction = glm::vec3(rotation * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    if (glm::dot(direction, direction) <= 0.0001f) {
        return { 0.0f, 0.0f, -1.0f };
    }

    return glm::normalize(direction);
}

glm::vec3 BridgeForwardDirection(
    const glm::vec3& exportedForward,
    const glm::vec3& rotationDegrees,
    glm::vec3 fallback
) {
    if (glm::dot(exportedForward, exportedForward) > 0.0001f) {
        return glm::normalize(exportedForward);
    }

    const glm::vec3 rotationDirection = DirectionFromRotationDegrees(rotationDegrees);
    if (glm::dot(rotationDirection, rotationDirection) > 0.0001f) {
        return rotationDirection;
    }

    if (glm::dot(fallback, fallback) <= 0.0001f) {
        fallback = { 0.0f, 0.0f, -1.0f };
    }

    return glm::normalize(fallback);
}

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

std::filesystem::path ImportedModelPath() {
    const std::string overridePath = ReadEnvironmentString("SELFENGINE_MODEL_PATH");
    return overridePath.empty() ? std::filesystem::path{} : std::filesystem::path(overridePath);
}

std::filesystem::path ExplicitUnrealBridgeManifestPath() {
    const std::string path = ReadEnvironmentString("SE_UE_BRIDGE_MANIFEST");
    return path.empty() ? std::filesystem::path{} : std::filesystem::path(path);
}

bool BenchmarkGridSceneRequested() {
    const std::string sceneName = ReadEnvironmentString("SE_BENCHMARK_SCENE");
    return sceneName == "1" ||
        sceneName == "grid" ||
        sceneName == "forward-grid" ||
        sceneName == "ForwardGrid";
}

bool BenchmarkTransparentMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_TRANSPARENT_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkForwardSpecialMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_FORWARD_SPECIAL_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkLightOverflowRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_LIGHT_OVERFLOW");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkSceneReflectionProbeRequested() {
    const std::string value = ReadEnvironmentString("SE_SCENE_REFLECTION_PROBE");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkSceneReflectionProbeMultiRequested() {
    const std::string value =
        ReadEnvironmentString("SE_SCENE_REFLECTION_PROBE_MULTI");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkSceneReflectionProbeOverflowRequested() {
    const std::string value =
        ReadEnvironmentString("SE_SCENE_REFLECTION_PROBE_OVERFLOW");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkSceneReflectionProbeMixedSourcesRequested() {
    const std::string value =
        ReadEnvironmentString("SE_SCENE_REFLECTION_PROBE_MIXED_SOURCES");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkSceneReflectionProbeCapturedRequested() {
    const std::string value =
        ReadEnvironmentString("SE_SCENE_REFLECTION_PROBE_CAPTURED");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

se::ReflectionProbeRefreshPolicy DefaultReflectionProbeRefreshPolicy(
    se::ReflectionProbeCaptureSource source
) {
    switch (source) {
    case se::ReflectionProbeCaptureSource::AuthoredCubemap:
        return se::ReflectionProbeRefreshPolicy::FileSignature;
    case se::ReflectionProbeCaptureSource::CapturedScene:
        return se::ReflectionProbeRefreshPolicy::SceneDirty;
    case se::ReflectionProbeCaptureSource::None:
    case se::ReflectionProbeCaptureSource::BuiltInProcedural:
        return se::ReflectionProbeRefreshPolicy::Static;
    }

    return se::ReflectionProbeRefreshPolicy::Static;
}

std::string BenchmarkSceneReflectionProbeAuthoredAssetId() {
    return ReadEnvironmentString("SE_SCENE_REFLECTION_PROBE_AUTHORED_ASSET");
}

bool BenchmarkPartialLocalShadowCacheRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_PARTIAL_LOCAL_SHADOW_CACHE");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkConstantEmissiveMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_CONSTANT_EMISSIVE_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkSpecularMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_SPECULAR_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkSpecularTextureMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_SPECULAR_TEXTURE_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkAlphaMaskMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_ALPHA_MASK_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkAlphaBlendMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_ALPHA_BLEND_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkUvTransformMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_UV_TRANSFORM_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkOpacityTextureMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_OPACITY_TEXTURE_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkOpacityBlendTextureMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_OPACITY_BLEND_TEXTURE_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkDoubleSidedMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_DOUBLE_SIDED_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkClearcoatMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_CLEARCOAT_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkClearcoatTextureMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_CLEARCOAT_TEXTURE_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkClearcoatRoughnessTextureMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_CLEARCOAT_ROUGHNESS_TEXTURE_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkTransmissionMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_TRANSMISSION_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkTransmissionTextureMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_TRANSMISSION_TEXTURE_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool BenchmarkVolumeMaterialRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_VOLUME_MATERIAL");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool EnvironmentFlagEnabled(const char* name) {
    const std::string value = ReadEnvironmentString(name);
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool EnvironmentFlagDisabled(const char* name) {
    const std::string value = ReadEnvironmentString(name);
    return value == "0" ||
        value == "false" ||
        value == "FALSE" ||
        value == "off" ||
        value == "OFF" ||
        value == "no" ||
        value == "NO";
}

se::f32 ReadEnvironmentF32(const char* name, se::f32 fallback) {
    const std::string value = ReadEnvironmentString(name);
    if (value.empty()) {
        return fallback;
    }

    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    return end != value.c_str() ? parsed : fallback;
}

bool BenchmarkCameraMotionRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_CAMERA_MOTION");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES" ||
        value == "orbit" ||
        value == "Orbit" ||
        value == "ORBIT";
}

void ApplyBenchmarkCameraMotion(se::Camera3D& camera, se::f32 elapsedSeconds) {
    const se::f32 speed =
        std::max(ReadEnvironmentF32("SE_BENCHMARK_CAMERA_MOTION_SPEED", 0.65f), 0.0f);
    const se::f32 yawAmplitude =
        std::max(ReadEnvironmentF32("SE_BENCHMARK_CAMERA_MOTION_YAW", 0.18f), 0.0f);
    const se::f32 pitchAmplitude =
        std::max(ReadEnvironmentF32("SE_BENCHMARK_CAMERA_MOTION_PITCH", 0.035f), 0.0f);
    const se::f32 distance = std::clamp(
        ReadEnvironmentF32("SE_BENCHMARK_CAMERA_MOTION_DISTANCE", 5.0f),
        3.0f,
        35.0f
    );
    const se::f32 motionPhase = elapsedSeconds * speed;
    camera.SetOrbit(
        3.75f + std::sin(motionPhase) * yawAmplitude,
        0.34f + std::sin(motionPhase * 0.73f) * pitchAmplitude,
        distance
    );
}

std::filesystem::path UnrealProjectRootPath() {
    const std::string overridePath = ReadEnvironmentString("SE_UE_PROJECT_ROOT");
    return overridePath.empty() ?
        std::filesystem::path("D:\\UEProject") :
        std::filesystem::path(overridePath);
}

void PrintUnrealDiscoveryMessage(const se::UnrealProjectBridgeMessage& message) {
    std::cout << "  [" << se::ToString(message.severity) << "] "
        << message.text;
    if (!message.path.empty()) {
        std::cout << ": " << se::UnrealPathToUtf8(message.path);
    }
    std::cout << std::endl;
}

void PrintUnrealProjectDiscovery(
    const se::UnrealProjectDiscoveryResult& discovery
) {
    std::cout << "UE project discovery root: "
        << se::UnrealPathToUtf8(discovery.rootPath)
        << std::endl;
    std::cout << "UE project candidates: "
        << discovery.projects.size()
        << std::endl;

    for (const se::UnrealProjectBridgeMessage& message : discovery.messages) {
        PrintUnrealDiscoveryMessage(message);
    }

    for (const se::UnrealProjectInfo& project : discovery.projects) {
        std::cout << "- " << project.displayName
            << " [" << se::ToString(project.kind) << "]"
            << std::endl;
        std::cout << "  root=" << se::UnrealPathToUtf8(project.rootPath)
            << std::endl;
        if (!project.uprojectPath.empty()) {
            std::cout << "  uproject=" << se::UnrealPathToUtf8(project.uprojectPath)
                << std::endl;
        }
        std::cout << "  bridge_manifest="
            << se::UnrealPathToUtf8(project.bridgeManifestPath)
            << std::endl;
        if (project.bridgeManifest.loaded) {
            std::cout << "  bridge_summary: schema="
                << (project.bridgeManifest.schemaVersion.empty() ?
                    "<missing>" : project.bridgeManifest.schemaVersion)
                << " scenes=" << project.bridgeManifest.scenes.size()
                << " mesh_assets=" << project.bridgeManifest.meshAssetCount
                << " material_assets=" << project.bridgeManifest.materialAssetCount
                << " texture_assets=" << project.bridgeManifest.textureAssetCount
                << " actors=" << project.bridgeManifest.actorCount
                << " mesh_instances=" << project.bridgeManifest.meshInstanceCount
                << " lights=" << project.bridgeManifest.lightCount
                << " cameras=" << project.bridgeManifest.cameraCount
                << " references=" << project.bridgeManifest.referenceCaptureCount
                << " mesh_exports=" << project.bridgeManifest.meshExportCount
                << " mesh_exports_ready=" << project.bridgeManifest.meshExportReadyCount
                << " mesh_exports_failed=" << project.bridgeManifest.meshExportFailedCount
                << " mesh_exports_missing=" << project.bridgeManifest.meshExportMissingCount
                << std::endl;
            if (!project.bridgeManifest.sourceProjectPath.empty()) {
                std::cout << "  bridge_source_project="
                    << se::UnrealPathToUtf8(project.bridgeManifest.sourceProjectPath)
                    << std::endl;
            }
            if (!project.bridgeManifest.exportedRootPath.empty()) {
                std::cout << "  bridge_exported_root="
                    << se::UnrealPathToUtf8(project.bridgeManifest.exportedRootPath)
                    << std::endl;
            }
            if (!project.bridgeManifest.scenes.empty()) {
                std::cout << "  bridge_scenes:" << std::endl;
                for (const se::UnrealBridgeSceneInfo& scene : project.bridgeManifest.scenes) {
                    std::cout << "    " << (scene.name.empty() ? "<unnamed>" : scene.name)
                        << " actors=" << scene.actorCount
                        << " mesh_instances=" << scene.meshInstanceCount
                        << " lights=" << scene.lightCount
                        << " cameras=" << scene.cameraCount
                        << " references=" << scene.referenceCaptureCount;
                    if (!scene.exportedScenePath.empty()) {
                        std::cout << " exported="
                            << se::UnrealPathToUtf8(scene.exportedScenePath);
                    }
                    if (!scene.plannedExportedScenePath.empty()) {
                        std::cout << " planned_export="
                            << se::UnrealPathToUtf8(scene.plannedExportedScenePath);
                    }
                    if (!scene.exportStatus.empty()) {
                        std::cout << " status=" << scene.exportStatus;
                    }
                    std::cout << std::endl;
                }
            }
        }
        std::cout << "  content=" << se::UnrealPathToUtf8(project.contentPath)
            << std::endl;
        std::cout << "  counts: maps=" << project.mapCount
            << " uassets=" << project.uassetCount
            << " exported_models=" << project.exportedModelCount
            << " texture_files=" << project.textureFileCount
            << " directories=" << project.directoryCount
            << std::endl;
        if (!project.sampleMapPaths.empty()) {
            std::cout << "  sample_maps:" << std::endl;
            for (const std::filesystem::path& path : project.sampleMapPaths) {
                std::cout << "    " << se::UnrealPathToUtf8(path) << std::endl;
            }
        }
        if (!project.sampleExportedModelPaths.empty()) {
            std::cout << "  sample_exported_models:" << std::endl;
            for (const std::filesystem::path& path : project.sampleExportedModelPaths) {
                std::cout << "    " << se::UnrealPathToUtf8(path) << std::endl;
            }
        }
        for (const se::UnrealProjectBridgeMessage& message : project.messages) {
            PrintUnrealDiscoveryMessage(message);
        }
    }
}

bool SceneMatchesRequest(
    const se::UnrealBridgeSceneInfo& scene,
    const std::string& request
) {
    if (request.empty()) {
        return true;
    }
    return scene.id == request ||
        scene.name == request ||
        scene.id.find(request) != std::string::npos ||
        scene.name.find(request) != std::string::npos;
}

StartupBridgeScene ResolveStartupBridgeScene() {
    StartupBridgeScene startup{};
    startup.requestedScene = ReadEnvironmentString("SE_UE_BRIDGE_SCENE");
    const bool loadFirstScene = EnvironmentFlagEnabled("SE_LOAD_UE_BRIDGE_FIRST_SCENE");
    startup.requested = !startup.requestedScene.empty() || loadFirstScene;
    if (!startup.requested) {
        return startup;
    }

    se::UnrealBridgeManifestSummary manifest{};
    const std::filesystem::path explicitManifestPath = ExplicitUnrealBridgeManifestPath();
    if (!explicitManifestPath.empty()) {
        startup.manifestPath = explicitManifestPath;
        manifest = se::LoadUnrealBridgeManifest(explicitManifestPath);
    } else {
        se::UnrealProjectDiscoveryOptions discoveryOptions{};
        discoveryOptions.rootPath = UnrealProjectRootPath();
        const se::UnrealProjectDiscoveryResult discovery =
            se::DiscoverUnrealProjects(discoveryOptions);
        if (!discovery.messages.empty()) {
            startup.messages.insert(
                startup.messages.end(),
                discovery.messages.begin(),
                discovery.messages.end()
            );
        }
        for (const se::UnrealProjectInfo& project : discovery.projects) {
            if (project.bridgeManifest.loaded) {
                startup.manifestPath = project.bridgeManifest.manifestPath;
                manifest = project.bridgeManifest;
                break;
            }
        }
        if (startup.manifestPath.empty() && !discovery.projects.empty()) {
            startup.manifestPath = discovery.projects.front().bridgeManifestPath;
            startup.messages = discovery.projects.front().messages;
        }
    }

    startup.manifestLoaded = manifest.loaded;
    startup.messages.insert(
        startup.messages.end(),
        manifest.messages.begin(),
        manifest.messages.end()
    );
    if (!manifest.loaded) {
        return startup;
    }

    for (const se::UnrealBridgeSceneInfo& scene : manifest.scenes) {
        if (!SceneMatchesRequest(scene, startup.requestedScene)) {
            continue;
        }

        startup.sceneFound = true;
        startup.sceneId = scene.id;
        startup.sceneName = scene.name;
        startup.exportedScenePath = scene.exportedScenePath;
        startup.plannedExportedScenePath = scene.plannedExportedScenePath;
        startup.exportStatus = scene.exportStatus;
        startup.meshInstances = scene.meshInstances;
        startup.cameras = scene.cameras;
        startup.lights = scene.lights;
        startup.referenceCaptureCount = scene.referenceCaptureCount;
        startup.manifestMeshExportReadyCount = scene.meshExportReadyCount;
        startup.manifestMeshExportMissingCount = scene.meshExportMissingCount;
        break;
    }

    if (!startup.sceneFound) {
        return startup;
    }

    std::error_code error;
    startup.exportedSceneReady =
        !startup.exportedScenePath.empty() &&
        std::filesystem::is_regular_file(startup.exportedScenePath, error) &&
        !error;
    for (const se::UnrealBridgeMeshInstanceInfo& instance : startup.meshInstances) {
        error.clear();
        const bool ready = !instance.exportedPath.empty() &&
            std::filesystem::is_regular_file(instance.exportedPath, error) &&
            !error;
        if (ready) {
            ++startup.meshInstanceExportReadyCount;
        } else {
            ++startup.meshInstanceExportMissingCount;
        }
    }

    return startup;
}

void PrintStartupBridgeScene(const StartupBridgeScene& startup) {
    if (!startup.requested) {
        return;
    }

    std::cout << "UE bridge scene request: "
        << (startup.requestedScene.empty() ? "<first scene>" : startup.requestedScene)
        << std::endl;
    if (!startup.manifestPath.empty()) {
        std::cout << "  manifest=" << se::UnrealPathToUtf8(startup.manifestPath)
            << std::endl;
    }
    std::cout << "  manifest_loaded=" << (startup.manifestLoaded ? 1 : 0)
        << " scene_found=" << (startup.sceneFound ? 1 : 0)
        << " exported_ready=" << (startup.exportedSceneReady ? 1 : 0)
        << std::endl;
    if (startup.sceneFound) {
        std::cout << "  scene=" << startup.sceneId
            << " name=" << startup.sceneName
            << " status=" << startup.exportStatus
            << std::endl;
        std::cout << "  mesh_instances=" << startup.meshInstances.size()
            << " mesh_exports_ready=" << startup.meshInstanceExportReadyCount
            << " mesh_exports_missing=" << startup.meshInstanceExportMissingCount
            << std::endl;
        std::cout << "  manifest_mesh_exports_ready=" << startup.manifestMeshExportReadyCount
            << " manifest_mesh_exports_missing=" << startup.manifestMeshExportMissingCount
            << std::endl;
        std::cout << "  cameras=" << startup.cameras.size()
            << " lights=" << startup.lights.size()
            << " references=" << startup.referenceCaptureCount
            << std::endl;
        if (!startup.cameras.empty()) {
            const se::UnrealBridgeCameraInfo& camera = startup.cameras.front();
            std::cout << "  first_camera="
                << (camera.actorName.empty() ? camera.componentName : camera.actorName)
                << " fov=" << camera.fieldOfViewDegrees
                << std::endl;
        }
        if (!startup.lights.empty()) {
            const se::UnrealBridgeLightInfo& light = startup.lights.front();
            std::cout << "  first_light="
                << (light.actorName.empty() ? light.componentName : light.actorName)
                << " class=" << light.componentClass
                << " intensity=" << light.intensity
                << std::endl;
        }
        if (!startup.exportedScenePath.empty()) {
            std::cout << "  exported_scene="
                << se::UnrealPathToUtf8(startup.exportedScenePath)
                << std::endl;
        }
        if (!startup.plannedExportedScenePath.empty()) {
            std::cout << "  planned_exported_scene="
                << se::UnrealPathToUtf8(startup.plannedExportedScenePath)
                << std::endl;
        }
        if (!startup.exportedSceneReady) {
            std::cout << "  [warning] UE bridge scene has no exported model file yet; "
                << "run scripts/Run-UnrealBridgeExport.ps1 and fill exportedScenePath before runtime scene preview."
                << std::endl;
        }
        if (startup.meshInstances.empty()) {
            std::cout << "  [warning] UE bridge scene has no meshInstances yet; "
                << "UE Editor automation must fill per-component mesh exports before instance-level reconstruction."
                << std::endl;
        } else if (startup.meshInstanceExportMissingCount > 0) {
            std::cout << "  [warning] UE bridge scene has missing mesh instance exports; "
                << "ready=" << startup.meshInstanceExportReadyCount
                << " missing=" << startup.meshInstanceExportMissingCount
                << std::endl;
        }
        if (startup.cameras.empty()) {
            std::cout << "  [warning] UE bridge scene has no cameras yet; "
                << "SelfEngine will use a fallback camera instead of a UE viewport/camera bookmark."
                << std::endl;
        }
        if (startup.lights.empty()) {
            std::cout << "  [warning] UE bridge scene has no lights yet; "
                << "SelfEngine will use a fallback camera key light instead of UE scene lighting."
                << std::endl;
        }
        if (startup.referenceCaptureCount == 0) {
            std::cout << "  [warning] UE bridge scene has no UE reference capture yet; "
                << "visual parity cannot be measured honestly until a matching screenshot is recorded."
                << std::endl;
        }
    }
    for (const se::UnrealProjectBridgeMessage& message : startup.messages) {
        PrintUnrealDiscoveryMessage(message);
    }
}

se::BenchmarkSceneDiagnostics BenchmarkDiagnosticsForStartupBridgeScene(
    const StartupBridgeScene& startup,
    se::u32 loadedBridgeMeshInstances,
    bool bridgeCameraApplied,
    const BridgeSceneLightApplyResult& bridgeLights
) {
    se::BenchmarkSceneDiagnostics diagnostics{};
    diagnostics.ueBridgeRequested = startup.requested ? 1u : 0u;
    diagnostics.ueBridgeManifestLoaded = startup.manifestLoaded ? 1u : 0u;
    diagnostics.ueBridgeSceneFound = startup.sceneFound ? 1u : 0u;
    diagnostics.ueBridgeExportedSceneReady = startup.exportedSceneReady ? 1u : 0u;
    diagnostics.ueBridgeMeshInstanceCount =
        static_cast<se::u32>(startup.meshInstances.size());
    diagnostics.ueBridgeMeshInstanceLoadedCount = loadedBridgeMeshInstances;
    diagnostics.ueBridgeMeshExportReadyCount =
        startup.meshInstanceExportReadyCount;
    diagnostics.ueBridgeMeshExportMissingCount =
        startup.meshInstanceExportMissingCount;
    diagnostics.ueBridgeManifestMeshExportReadyCount =
        startup.manifestMeshExportReadyCount;
    diagnostics.ueBridgeManifestMeshExportMissingCount =
        startup.manifestMeshExportMissingCount;
    diagnostics.ueBridgeCameraCount =
        static_cast<se::u32>(startup.cameras.size());
    diagnostics.ueBridgeCameraApplied = bridgeCameraApplied ? 1u : 0u;
    diagnostics.ueBridgeLightCount =
        static_cast<se::u32>(startup.lights.size());
    diagnostics.ueBridgeLightsApplied = bridgeLights.anyApplied ? 1u : 0u;
    diagnostics.ueBridgeSkyLightCount = bridgeLights.skyLightCount;
    diagnostics.ueBridgeSkyLightApplied = bridgeLights.skyLightApplied ? 1u : 0u;
    diagnostics.ueBridgeReferenceCaptureCount = startup.referenceCaptureCount;
    diagnostics.ueBridgeBlockedMissingManifest =
        startup.requested && !startup.manifestLoaded ? 1u : 0u;
    diagnostics.ueBridgeBlockedSceneMissing =
        startup.requested && startup.manifestLoaded && !startup.sceneFound ? 1u : 0u;
    diagnostics.ueBridgeBlockedNoMeshInstances =
        startup.sceneFound && startup.meshInstances.empty() ? 1u : 0u;
    diagnostics.ueBridgeBlockedMeshExports =
        startup.sceneFound &&
        (!startup.meshInstances.empty() &&
            startup.meshInstanceExportMissingCount > 0)
            ? 1u
            : 0u;
    diagnostics.ueBridgeBlockedMeshLoads =
        startup.sceneFound &&
        !startup.meshInstances.empty() &&
        loadedBridgeMeshInstances < startup.meshInstances.size()
            ? 1u
            : 0u;
    diagnostics.ueBridgeBlockedCamera =
        startup.sceneFound && !bridgeCameraApplied ? 1u : 0u;
    diagnostics.ueBridgeBlockedLights =
        startup.sceneFound && !bridgeLights.anyApplied ? 1u : 0u;
    diagnostics.ueBridgeBlockedReferenceCapture =
        startup.sceneFound && startup.referenceCaptureCount == 0 ? 1u : 0u;
    diagnostics.ueBridgeVisualParityReady =
        startup.sceneFound &&
        startup.meshInstanceExportMissingCount == 0 &&
        !startup.meshInstances.empty() &&
        loadedBridgeMeshInstances == startup.meshInstances.size() &&
        bridgeCameraApplied &&
        bridgeLights.anyApplied &&
        startup.referenceCaptureCount > 0
            ? 1u
            : 0u;
    return diagnostics;
}

int BenchmarkGridSize() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_GRID_SIZE");
    if (value.empty()) {
        return 16;
    }

    return std::clamp(std::atoi(value.c_str()), 2, 64);
}

void BuildBenchmarkGridScene(se::Scene3D& scene, int gridSize) {
    se::Renderable3D& ground = scene.CreateRenderable(
        "Benchmark Ground",
        "Plane",
        "GroundMaterial"
    );
    ground.Transform().SetPosition({ 0.0f, -1.15f, 0.0f });
    ground.Transform().SetScale({
        static_cast<se::f32>(gridSize) * 0.78f,
        1.0f,
        static_cast<se::f32>(gridSize) * 0.78f
    });
    ground.Transform().SetAnimateRotation(false);
    ground.SetDrawOrder(-10);
    ground.SetPickable(false);

    const std::array<const char*, 3> materials{
        "WarmCubeMaterial",
        "BlueCubeMaterial",
        "GreenCubeMaterial"
    };
    constexpr se::f32 kSpacing = 1.18f;
    const se::f32 centerOffset =
        (static_cast<se::f32>(gridSize) - 1.0f) * kSpacing * 0.5f;

    int cubeIndex = 0;
    for (int z = 0; z < gridSize; ++z) {
        for (int x = 0; x < gridSize; ++x) {
            const int pattern = (x * 17 + z * 13) % 5;
            const se::f32 scale = 0.42f + static_cast<se::f32>(pattern) * 0.055f;
            se::Renderable3D& cube = scene.CreateRenderable(
                "Benchmark Cube " + std::to_string(cubeIndex),
                "Cube",
                materials[static_cast<std::size_t>((x + z) % materials.size())]
            );
            cube.Transform().SetPosition({
                static_cast<se::f32>(x) * kSpacing - centerOffset,
                -1.15f + scale * 0.5f,
                static_cast<se::f32>(z) * kSpacing - centerOffset
            });
            cube.Transform().SetScale({ scale, scale, scale });
            cube.Transform().SetRotationDegrees({
                static_cast<se::f32>((x * 11) % 37),
                static_cast<se::f32>((z * 19 + x * 7) % 180),
                0.0f
            });
            cube.Transform().SetAnimateRotation(false);
            cube.SetPickable(false);
            ++cubeIndex;
        }
    }

    scene.CreatePointLight(
        "Benchmark Warm Point Light",
        { -2.2f, 1.1f, -1.8f },
        4.8f,
        { 1.0f, 0.38f, 0.22f },
        4.2f
    );
    scene.CreatePointLight(
        "Benchmark Cool Point Light",
        { 1.8f, 1.0f, 0.9f },
        4.5f,
        { 0.24f, 0.48f, 1.0f },
        3.8f
    );
    scene.CreatePointLight(
        "Benchmark Fill Point Light",
        { 0.1f, 1.6f, 2.8f },
        5.2f,
        { 0.36f, 1.0f, 0.44f },
        3.2f
    );
    scene.CreateSpotLight(
        "Benchmark Spot Light",
        { 0.0f, 4.4f, 3.6f },
        { 0.0f, -0.82f, -0.58f },
        7.5f,
        { 1.0f, 0.92f, 0.62f },
        5.0f,
        14.0f,
        28.0f
    );
    scene.CreateRectLight(
        "Benchmark Rect Light",
        { -3.0f, 2.6f, 2.4f },
        { 0.45f, -0.55f, -0.70f },
        3.0f,
        1.2f,
        7.0f,
        { 0.55f, 0.78f, 1.0f },
        3.4f
    );

    if (BenchmarkLightOverflowRequested()) {
        constexpr int kOverflowLightCount = 24;
        constexpr se::f32 kPi = 3.14159265359f;
        for (int index = 0; index < kOverflowLightCount; ++index) {
            const se::f32 angle =
                (static_cast<se::f32>(index) / static_cast<se::f32>(kOverflowLightCount)) *
                kPi * 2.0f;
            const se::f32 ring =
                1.35f + static_cast<se::f32>(index % 4) * 0.42f;
            const se::f32 height =
                1.35f + static_cast<se::f32>((index * 5) % 7) * 0.11f;
            const glm::vec3 position{
                std::cos(angle) * ring,
                height,
                std::sin(angle) * ring
            };
            const glm::vec3 color{
                0.48f + 0.42f * std::sin(angle * 1.7f + 0.4f),
                0.52f + 0.38f * std::sin(angle * 2.3f + 1.2f),
                0.58f + 0.34f * std::sin(angle * 1.1f + 2.1f)
            };
            scene.CreatePointLight(
                "Benchmark Tile Overflow Point Light " + std::to_string(index),
                position,
                12.0f,
                glm::clamp(color, glm::vec3(0.12f), glm::vec3(1.0f)),
                1.45f
            );
        }
    }

    if (BenchmarkSceneReflectionProbeRequested()) {
        const bool multiProbeRequested = BenchmarkSceneReflectionProbeMultiRequested();
        const bool overflowProbeRequested =
            BenchmarkSceneReflectionProbeOverflowRequested();
        const bool mixedSourceProbeRequested =
            BenchmarkSceneReflectionProbeMixedSourcesRequested();
        const bool capturedSceneProbeRequested =
            BenchmarkSceneReflectionProbeCapturedRequested();
        const std::string authoredAssetId =
            BenchmarkSceneReflectionProbeAuthoredAssetId();
        const bool authoredAssetProbeRequested = !authoredAssetId.empty();
        const se::ReflectionProbeCaptureSource baseProbeCaptureSource =
            capturedSceneProbeRequested
                ? se::ReflectionProbeCaptureSource::CapturedScene
            : authoredAssetProbeRequested
                ? se::ReflectionProbeCaptureSource::AuthoredCubemap
                : se::ReflectionProbeCaptureSource::BuiltInProcedural;
        scene.CreateReflectionProbe(
            "Benchmark Scene Reflection Probe",
            { 0.0f, 1.2f, 0.0f },
            6.25f,
            { 5.2f, 3.4f, 5.2f },
            { 1.0f, 0.86f, 0.68f },
            1.35f,
            0.72f,
            1.75f,
            baseProbeCaptureSource,
            authoredAssetId,
            DefaultReflectionProbeRefreshPolicy(baseProbeCaptureSource)
        );
        if (multiProbeRequested || overflowProbeRequested || mixedSourceProbeRequested) {
            const se::ReflectionProbeCaptureSource warmProbeCaptureSource =
                mixedSourceProbeRequested
                    ? se::ReflectionProbeCaptureSource::AuthoredCubemap
                    : se::ReflectionProbeCaptureSource::BuiltInProcedural;
            scene.CreateReflectionProbe(
                "Benchmark Warm Reflection Probe",
                { -3.8f, 1.1f, -2.4f },
                4.8f,
                { 3.4f, 2.6f, 3.0f },
                { 1.0f, 0.68f, 0.48f },
                1.05f,
                0.58f,
                1.55f,
                warmProbeCaptureSource,
                mixedSourceProbeRequested ? authoredAssetId : std::string{},
                DefaultReflectionProbeRefreshPolicy(warmProbeCaptureSource)
            );
            const se::ReflectionProbeCaptureSource coolProbeCaptureSource =
                mixedSourceProbeRequested
                    ? se::ReflectionProbeCaptureSource::CapturedScene
                    : se::ReflectionProbeCaptureSource::BuiltInProcedural;
            scene.CreateReflectionProbe(
                "Benchmark Cool Reflection Probe",
                { 3.6f, 1.4f, 2.6f },
                5.2f,
                { 3.8f, 2.8f, 3.6f },
                { 0.58f, 0.78f, 1.0f },
                1.12f,
                0.62f,
                1.65f,
                coolProbeCaptureSource,
                {},
                DefaultReflectionProbeRefreshPolicy(coolProbeCaptureSource)
            );
        }
        if (overflowProbeRequested || mixedSourceProbeRequested) {
            const se::ReflectionProbeCaptureSource gardenProbeCaptureSource =
                mixedSourceProbeRequested
                    ? se::ReflectionProbeCaptureSource::None
                    : se::ReflectionProbeCaptureSource::BuiltInProcedural;
            scene.CreateReflectionProbe(
                "Benchmark Garden Reflection Probe",
                { -1.6f, 1.0f, 3.8f },
                4.4f,
                { 2.8f, 2.4f, 3.4f },
                { 0.62f, 1.0f, 0.72f },
                0.96f,
                0.54f,
                1.5f,
                gardenProbeCaptureSource,
                {},
                DefaultReflectionProbeRefreshPolicy(gardenProbeCaptureSource)
            );
        }
        if (overflowProbeRequested) {
            scene.CreateReflectionProbe(
                "Benchmark Ember Reflection Probe",
                { 2.4f, 1.3f, -3.9f },
                4.6f,
                { 3.2f, 2.5f, 3.2f },
                { 1.0f, 0.52f, 0.36f },
                1.0f,
                0.56f,
                1.6f
            );
            scene.CreateReflectionProbe(
                "Benchmark Neutral Reflection Probe",
                { 4.4f, 1.2f, -0.4f },
                4.2f,
                { 2.8f, 2.4f, 2.8f },
                { 0.82f, 0.86f, 0.9f },
                0.9f,
                0.5f,
                1.45f
            );
        }
    }
}

void ApplyCameraToMaterial(
    const se::Camera3D& camera,
    se::VulkanMaterial& material
) {
    const glm::vec3& cameraPosition = camera.Position();
    const glm::vec3& cameraDirection = camera.Forward();
    se::MaterialProperties& properties = material.Properties();
    const glm::vec3 lightDirection = SceneKeyLightDirection(camera);

    properties.cameraPosition = {
        cameraPosition.x,
        cameraPosition.y,
        cameraPosition.z,
        0.0f
    };
    properties.cameraDirection = {
        cameraDirection.x,
        cameraDirection.y,
        cameraDirection.z,
        0.0f
    };
    properties.custom[0] = lightDirection.x;
    properties.custom[1] = lightDirection.y;
    properties.custom[2] = lightDirection.z;
}

struct BenchmarkTexturePixels {
    std::vector<se::u8> rgba;
    se::u32 width = 0;
    se::u32 height = 0;
};

BenchmarkTexturePixels CreateBenchmarkOpacityTexture() {
    constexpr se::u32 kSize = 8;
    std::vector<se::u8> pixels;
    pixels.resize(kSize * kSize * 4);
    for (se::u32 y = 0; y < kSize; ++y) {
        for (se::u32 x = 0; x < kSize; ++x) {
            const bool visible = ((x + y) % 3) != 0;
            const se::u8 opacity = visible ? 255 : 48;
            const std::size_t offset = static_cast<std::size_t>((y * kSize + x) * 4);
            pixels[offset + 0] = opacity;
            pixels[offset + 1] = opacity;
            pixels[offset + 2] = opacity;
            pixels[offset + 3] = 255;
        }
    }

    return BenchmarkTexturePixels{
        std::move(pixels),
        kSize,
        kSize
    };
}

BenchmarkTexturePixels CreateBenchmarkSpecularTexture() {
    constexpr se::u32 kSize = 8;
    std::vector<se::u8> pixels;
    pixels.resize(kSize * kSize * 4);
    for (se::u32 y = 0; y < kSize; ++y) {
        for (se::u32 x = 0; x < kSize; ++x) {
            const bool hot = ((x * 5 + y * 3) % 7) < 3;
            const std::size_t offset = static_cast<std::size_t>((y * kSize + x) * 4);
            pixels[offset + 0] = hot ? 255 : 72;
            pixels[offset + 1] = hot ? 210 : 48;
            pixels[offset + 2] = hot ? 96 : 32;
            pixels[offset + 3] = 255;
        }
    }

    return BenchmarkTexturePixels{
        std::move(pixels),
        kSize,
        kSize
    };
}

BenchmarkTexturePixels CreateBenchmarkClearcoatTexture() {
    constexpr se::u32 kSize = 8;
    std::vector<se::u8> pixels;
    pixels.resize(kSize * kSize * 4);
    for (se::u32 y = 0; y < kSize; ++y) {
        for (se::u32 x = 0; x < kSize; ++x) {
            const bool glossy = ((x + y * 2) % 5) < 3;
            const se::u8 clearcoat = glossy ? 255 : 36;
            const std::size_t offset = static_cast<std::size_t>((y * kSize + x) * 4);
            pixels[offset + 0] = clearcoat;
            pixels[offset + 1] = clearcoat;
            pixels[offset + 2] = clearcoat;
            pixels[offset + 3] = 255;
        }
    }

    return BenchmarkTexturePixels{
        std::move(pixels),
        kSize,
        kSize
    };
}

BenchmarkTexturePixels CreateBenchmarkClearcoatRoughnessTexture() {
    constexpr se::u32 kSize = 8;
    std::vector<se::u8> pixels;
    pixels.resize(kSize * kSize * 4);
    for (se::u32 y = 0; y < kSize; ++y) {
        for (se::u32 x = 0; x < kSize; ++x) {
            const bool polished = ((x * 3 + y) % 5) < 2;
            const se::u8 roughness = polished ? 34 : 210;
            const std::size_t offset = static_cast<std::size_t>((y * kSize + x) * 4);
            pixels[offset + 0] = roughness;
            pixels[offset + 1] = roughness;
            pixels[offset + 2] = roughness;
            pixels[offset + 3] = 255;
        }
    }

    return BenchmarkTexturePixels{
        std::move(pixels),
        kSize,
        kSize
    };
}

BenchmarkTexturePixels CreateBenchmarkTransmissionTexture() {
    constexpr se::u32 kSize = 8;
    std::vector<se::u8> pixels;
    pixels.resize(kSize * kSize * 4);
    for (se::u32 y = 0; y < kSize; ++y) {
        for (se::u32 x = 0; x < kSize; ++x) {
            const bool transmissive = ((x * 2 + y * 3) % 6) < 4;
            const se::u8 transmission = transmissive ? 230 : 28;
            const std::size_t offset = static_cast<std::size_t>((y * kSize + x) * 4);
            pixels[offset + 0] = transmission;
            pixels[offset + 1] = transmission;
            pixels[offset + 2] = transmission;
            pixels[offset + 3] = 255;
        }
    }

    return BenchmarkTexturePixels{
        std::move(pixels),
        kSize,
        kSize
    };
}

void LoadDroppedModels(
    se::Application& app,
    se::RuntimeModelLoader& loader
) {
    for (const std::filesystem::path& path : app.WindowHandle().ConsumeDroppedPaths()) {
        const se::RuntimeModelLoadResult result = loader.LoadIntoScene(
            path,
            { 0.0f, 0.15f, -1.85f },
            { 0.0f, 22.0f, 0.0f }
        );
        if (result.loaded && app.Renderer() != nullptr) {
            app.Renderer()->RefreshMaterialDescriptors();
        }
    }
}

se::u32 LoadBridgeMeshInstances(
    const StartupBridgeScene& startup,
    se::RuntimeModelLoader& loader
) {
    if (!startup.sceneFound || startup.meshInstances.empty()) {
        return 0;
    }

    se::u32 loadedCount = 0;
    for (const se::UnrealBridgeMeshInstanceInfo& instance : startup.meshInstances) {
        std::error_code error;
        if (instance.exportedPath.empty() ||
            !std::filesystem::is_regular_file(instance.exportedPath, error) ||
            error) {
            continue;
        }

        const se::RuntimeModelLoadResult result = loader.LoadIntoScene(
            instance.exportedPath,
            instance.position,
            instance.rotationDegrees,
            0.0f,
            instance.scale
        );
        if (result.loaded) {
            ++loadedCount;
        } else {
            std::cout << "[warning] UE bridge mesh instance load failed: "
                << instance.id
                << " path=" << se::UnrealPathToUtf8(instance.exportedPath)
                << " message=" << result.message
                << std::endl;
        }
    }

    if (loadedCount > 0) {
        std::cout << "UE bridge mesh instances loaded: "
            << loadedCount
            << " / " << startup.meshInstances.size()
            << std::endl;
    }

    return loadedCount;
}

bool ApplyBridgeSceneCamera(
    const StartupBridgeScene& startup,
    se::Camera3D& camera
) {
    if (!startup.sceneFound || startup.cameras.empty()) {
        return false;
    }

    const se::UnrealBridgeCameraInfo& bridgeCamera = startup.cameras.front();
    const glm::vec3 forward = BridgeForwardDirection(
        bridgeCamera.forward,
        bridgeCamera.rotationDegrees,
        camera.Forward()
    );

    camera.SetPose(bridgeCamera.position, forward);
    if (bridgeCamera.fieldOfViewDegrees > 0.1f) {
        const se::f32 clampedFovDegrees =
            std::clamp(bridgeCamera.fieldOfViewDegrees, 1.0f, 170.0f);
        const se::f32 horizontalFovRadians = glm::radians(clampedFovDegrees);
        const se::f32 verticalFovRadians = bridgeCamera.aspectRatio > 0.01f ?
            2.0f * std::atan(std::tan(horizontalFovRadians * 0.5f) / bridgeCamera.aspectRatio) :
            horizontalFovRadians;
        camera.SetFovScale(2.0f * std::tan(verticalFovRadians * 0.5f));
    }
    camera.SetMoveSpeed(std::max(camera.MoveSpeed(), std::max(glm::length(bridgeCamera.position) * 0.02f, 25.0f)));

    std::cout << "UE bridge camera applied: "
        << (bridgeCamera.actorName.empty() ? bridgeCamera.componentName : bridgeCamera.actorName)
        << " position=(" << bridgeCamera.position.x
        << ", " << bridgeCamera.position.y
        << ", " << bridgeCamera.position.z
        << ") forward=(" << forward.x
        << ", " << forward.y
        << ", " << forward.z
        << ") fov=" << bridgeCamera.fieldOfViewDegrees
        << std::endl;

    return true;
}

void ApplyBridgeSceneClipPlanes(
    const StartupBridgeScene& startup,
    se::Camera3D& camera
) {
    if (!startup.sceneFound) {
        return;
    }

    constexpr se::f32 kUnrealPreviewNearClip = 1.0f;
    constexpr se::f32 kUnrealPreviewFarClip = 500000.0f;
    camera.SetClipPlanes(kUnrealPreviewNearClip, kUnrealPreviewFarClip);

    std::cout << "UE bridge camera clip planes: near="
        << camera.NearClip()
        << " far=" << camera.FarClip()
        << std::endl;
}

se::f32 PreviewDirectionalIntensity(se::f32 unrealIntensity) {
    if (unrealIntensity <= 0.0f) {
        return 0.78f;
    }
    const se::f32 scaled = unrealIntensity > 2.0f ? unrealIntensity / 10.0f : unrealIntensity;
    return std::clamp(scaled, 0.0f, 8.0f);
}

se::f32 PreviewLocalIntensity(se::f32 unrealIntensity) {
    if (unrealIntensity <= 0.0f) {
        return 1.0f;
    }
    const se::f32 scaled = unrealIntensity > 64.0f ? unrealIntensity / 1000.0f : unrealIntensity;
    return std::clamp(scaled, 0.0f, 32.0f);
}

se::f32 PreviewRectIntensity(se::f32 unrealIntensity) {
    if (unrealIntensity <= 0.0f) {
        return 1.0f;
    }
    const se::f32 scaled = unrealIntensity > 64.0f ? unrealIntensity / 250.0f : unrealIntensity;
    return std::clamp(scaled, 0.0f, 32.0f);
}

se::f32 PreviewSkyLightIntensity(se::f32 unrealIntensity, glm::vec3 color) {
    const se::f32 sourceIntensity = unrealIntensity > 0.0f ? unrealIntensity : 1.0f;
    const se::f32 luminance =
        color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
    return std::clamp(sourceIntensity * std::max(luminance, 0.05f), 0.0f, 4.0f);
}

se::f32 PositiveOrFallback(se::f32 value, se::f32 fallback) {
    return value > 0.0f ? value : fallback;
}

BridgeSceneLightApplyResult ApplyBridgeSceneLights(
    const StartupBridgeScene& startup,
    se::Scene3D& scene,
    const se::Camera3D& camera
) {
    BridgeSceneLightApplyResult result{};
    if (!startup.sceneFound || startup.lights.empty()) {
        return result;
    }

    bool appliedDirectional = false;
    se::u32 appliedPointCount = 0;
    se::u32 appliedSpotCount = 0;
    se::u32 appliedRectCount = 0;
    se::u32 skippedCount = 0;

    for (const se::UnrealBridgeLightInfo& light : startup.lights) {
        const std::string lightClass = LowerAscii(light.componentClass);
        const glm::vec3 direction = BridgeForwardDirection(
            light.direction,
            light.rotationDegrees,
            { 0.0f, -1.0f, 0.0f }
        );
        const std::string name = light.actorName.empty() ?
            (light.componentName.empty() ? light.id : light.componentName) :
            light.actorName;

        if (lightClass.find("skylight") != std::string::npos ||
            LowerAscii(light.actorName).find("skylight") != std::string::npos) {
            if (result.skyLightCount == 0) {
                result.skyLightIntensity =
                    PreviewSkyLightIntensity(light.intensity, light.color);
            }
            ++result.skyLightCount;
            continue;
        }

        if (lightClass.find("directional") != std::string::npos) {
            if (!appliedDirectional) {
                scene.SetPrimaryDirectionalLight(
                    name.empty() ? "UE Bridge Directional Light" : name,
                    direction,
                    PreviewDirectionalIntensity(light.intensity),
                    0.08f,
                    0.24f
                );
                appliedDirectional = true;
            } else {
                ++skippedCount;
            }
            continue;
        }

        if (lightClass.find("spot") != std::string::npos) {
            const se::f32 radius = PositiveOrFallback(light.attenuationRadius, 600.0f);
            const se::f32 outerCone = PositiveOrFallback(light.outerConeAngleDegrees, 44.0f);
            const se::f32 innerCone = light.innerConeAngleDegrees > 0.0f ?
                std::min(light.innerConeAngleDegrees, outerCone) :
                std::max(outerCone * 0.6f, 1.0f);
            scene.CreateSpotLight(
                name.empty() ? "UE Bridge Spot Light" : name,
                light.position,
                direction,
                radius,
                light.color,
                PreviewLocalIntensity(light.intensity),
                innerCone,
                outerCone
            );
            ++appliedSpotCount;
            continue;
        }

        if (lightClass.find("rect") != std::string::npos) {
            const se::f32 width = PositiveOrFallback(
                light.sourceWidth,
                PositiveOrFallback(light.sourceRadius * 2.0f, 100.0f)
            );
            const se::f32 height = PositiveOrFallback(
                light.sourceHeight,
                PositiveOrFallback(light.sourceRadius * 2.0f, width)
            );
            scene.CreateRectLight(
                name.empty() ? "UE Bridge Rect Light" : name,
                light.position,
                direction,
                width,
                height,
                PositiveOrFallback(light.attenuationRadius, 800.0f),
                light.color,
                PreviewRectIntensity(light.intensity)
            );
            ++appliedRectCount;
            continue;
        }

        if (lightClass.find("point") != std::string::npos ||
            lightClass.find("lightcomponent") != std::string::npos) {
            scene.CreatePointLight(
                name.empty() ? "UE Bridge Point Light" : name,
                light.position,
                PositiveOrFallback(light.attenuationRadius, 600.0f),
                light.color,
                PreviewLocalIntensity(light.intensity)
            );
            ++appliedPointCount;
            continue;
        }

        ++skippedCount;
    }

    if (!appliedDirectional) {
        scene.SetPrimaryDirectionalLight(
            "UE Bridge No Directional Light",
            SceneKeyLightDirection(camera),
            0.0f,
            0.06f,
            0.0f
        );
    }

    std::cout << "UE bridge lights applied: directional="
        << (appliedDirectional ? 1 : 0)
        << " point=" << appliedPointCount
        << " spot=" << appliedSpotCount
        << " rect=" << appliedRectCount
        << " skylight=" << result.skyLightCount
        << " skipped=" << skippedCount
        << std::endl;

    result.directionalApplied = appliedDirectional;
    result.pointCount = appliedPointCount;
    result.spotCount = appliedSpotCount;
    result.rectCount = appliedRectCount;
    result.anyApplied =
        appliedDirectional ||
        appliedPointCount > 0 ||
        appliedSpotCount > 0 ||
        appliedRectCount > 0 ||
        result.skyLightCount > 0;
    return result;
}

bool ApplyBridgeSkyLightToRenderer(
    const BridgeSceneLightApplyResult& bridgeLights,
    se::VulkanRenderer& renderer
) {
    if (bridgeLights.skyLightCount == 0 ||
        EnvironmentFlagDisabled("SE_REFLECTION_PROBE_FALLBACK")) {
        return false;
    }

    se::VulkanShadowSettings& settings = renderer.ShadowSettings();
    settings.reflectionProbeFallbackEnabled = true;
    settings.reflectionProbeDiffuseIntensity = bridgeLights.skyLightIntensity;
    settings.reflectionProbeSpecularIntensity = bridgeLights.skyLightIntensity;

    std::cout << "UE bridge SkyLight applied to reflection fallback: intensity="
        << bridgeLights.skyLightIntensity
        << std::endl;
    return true;
}

}

int main() {
    constexpr int kDisplay1 = 1;
    if (EnvironmentFlagEnabled("SE_DISCOVER_UE_PROJECTS")) {
        se::UnrealProjectDiscoveryOptions discoveryOptions{};
        discoveryOptions.rootPath = UnrealProjectRootPath();
        const std::string bridgeManifestFileName =
            ReadEnvironmentString("SE_UE_BRIDGE_MANIFEST_NAME");
        if (!bridgeManifestFileName.empty()) {
            discoveryOptions.bridgeManifestFileName = bridgeManifestFileName;
        }
        PrintUnrealProjectDiscovery(se::DiscoverUnrealProjects(discoveryOptions));
    }

    const std::string vertexShaderPath = std::string(SE_SHADER_DIR) + "/forward_3d.vert.spv";
    const std::string fragmentShaderPath = std::string(SE_SHADER_DIR) + "/forward_3d.frag.spv";
    const std::string checkerTexturePath = std::string(SE_ASSET_DIR) + "/textures/checker.ppm";
    const StartupBridgeScene startupBridgeScene = ResolveStartupBridgeScene();
    PrintStartupBridgeScene(startupBridgeScene);
    const std::filesystem::path importedModelPath = ImportedModelPath();
    const std::filesystem::path bridgeModelPath =
        startupBridgeScene.exportedSceneReady ? startupBridgeScene.exportedScenePath : std::filesystem::path{};
    const std::filesystem::path startupModelPath =
        !importedModelPath.empty() ? importedModelPath : bridgeModelPath;
    const bool hasStartupModel = !startupModelPath.empty();
    const bool useBenchmarkScene = !hasStartupModel && BenchmarkGridSceneRequested();
    const bool animateBenchmarkPartialLocalShadowCache =
        useBenchmarkScene && BenchmarkPartialLocalShadowCacheRequested();

    se::Application app(
        1280,
        720,
        "SelfEngine Forward 3D",
        kDisplay1,
        se::PipelineSpec::DefaultForward3D(vertexShaderPath, fragmentShaderPath)
    );

    se::MeshData3D cubeData = se::MeshFactory::CreateCube();
    auto cubeMesh = std::make_unique<se::VulkanMesh>(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        std::move(cubeData.vertices),
        std::move(cubeData.indices)
    );
    app.RenderResources().RegisterMesh("Cube", *cubeMesh);

    se::MeshData3D planeData = se::MeshFactory::CreatePlane();
    auto planeMesh = std::make_unique<se::VulkanMesh>(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        std::move(planeData.vertices),
        std::move(planeData.indices)
    );
    app.RenderResources().RegisterMesh("Plane", *planeMesh);

    se::MeshData3D gridData = se::MeshFactory::CreateGrid(16, 0.25f, 0.012f);
    auto gridMesh = std::make_unique<se::VulkanMesh>(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        std::move(gridData.vertices),
        std::move(gridData.indices)
    );
    app.RenderResources().RegisterMesh("Grid", *gridMesh);

    se::VulkanMaterial& cubeMaterial = app.MaterialLibrary().Create(
        "WarmCubeMaterial",
        checkerTexturePath,
        ForwardMaterial({ 1.0f, 0.82f, 0.68f, 1.0f }, 0.0f, 0.22f, 0.78f, 0.24f, 48.0f)
    );
    se::VulkanMaterial& blueCubeMaterial = app.MaterialLibrary().Create(
        "BlueCubeMaterial",
        checkerTexturePath,
        ForwardMaterial({ 0.64f, 0.78f, 1.0f, 1.0f }, 0.0f, 0.18f, 0.86f, 0.18f, 32.0f)
    );
    se::VulkanMaterial& greenCubeMaterial = app.MaterialLibrary().Create(
        "GreenCubeMaterial",
        checkerTexturePath,
        ForwardMaterial({ 0.58f, 0.95f, 0.72f, 1.0f }, 0.0f, 0.20f, 0.74f, 0.34f, 64.0f)
    );
    se::VulkanMaterial& groundMaterial = app.MaterialLibrary().Create(
        "GroundMaterial",
        checkerTexturePath,
        ForwardMaterial({ 0.72f, 0.76f, 0.78f, 1.0f }, 0.35f, 0.28f, 0.62f, 0.05f, 16.0f)
    );
    se::VulkanMaterial& gridMaterial = app.MaterialLibrary().Create(
        "GridMaterial",
        checkerTexturePath,
        ForwardMaterial({ 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 0.65f, 0.25f, 0.0f, 8.0f)
    );
    if (useBenchmarkScene && BenchmarkTransparentMaterialRequested()) {
        se::MaterialProperties& transparentProperties = blueCubeMaterial.Properties();
        transparentProperties.baseColorFactor[3] = 0.42f;
        transparentProperties.renderClass = se::MaterialRenderClass::Transparent;
    }
    if (useBenchmarkScene && BenchmarkForwardSpecialMaterialRequested()) {
        se::MaterialProperties& forwardSpecialProperties = greenCubeMaterial.Properties();
        forwardSpecialProperties.renderClass = se::MaterialRenderClass::ForwardSpecial;
        forwardSpecialProperties.emissiveFactor = { 0.12f, 0.55f, 0.26f, 1.0f };
        forwardSpecialProperties.viewControls[1] = 1.12f;
    }
    if (useBenchmarkScene && BenchmarkConstantEmissiveMaterialRequested()) {
        se::MaterialProperties& emissiveProperties = greenCubeMaterial.Properties();
        emissiveProperties.emissiveFactor = { 0.0f, 1.25f, 0.42f, 1.0f };
        emissiveProperties.pbrFactors = { 1.0f, 0.45f, 0.0f, 0.0f };
    }
    if (useBenchmarkScene && BenchmarkSpecularMaterialRequested()) {
        se::MaterialProperties& specularProperties = cubeMaterial.Properties();
        specularProperties.specularFactor = { 1.85f, 0.42f, 0.24f, 1.0f };
        specularProperties.viewControls[1] = 1.25f;
    }
    if (useBenchmarkScene && BenchmarkSpecularTextureMaterialRequested()) {
        se::MaterialProperties& specularTextureProperties = blueCubeMaterial.Properties();
        specularTextureProperties.specularFactor = { 1.15f, 0.82f, 0.38f, 1.0f };
        specularTextureProperties.viewControls[1] = 1.15f;
        BenchmarkTexturePixels specularPixels = CreateBenchmarkSpecularTexture();
        blueCubeMaterial.SetSpecularMap(
            app.Device(),
            app.PhysicalDevice(),
            app.CommandPool(),
            se::VulkanTexturePixels{
                std::span<const se::u8>(specularPixels.rgba.data(), specularPixels.rgba.size()),
                specularPixels.width,
                specularPixels.height
            },
            true,
            false,
            false
        );
        specularTextureProperties.cameraControls[3] += se::kMaterialTextureFlagSpecular;
    }
    if (useBenchmarkScene && BenchmarkAlphaMaskMaterialRequested()) {
        se::MaterialProperties& maskProperties = cubeMaterial.Properties();
        maskProperties.alphaMode = se::MaterialAlphaMode::Mask;
        maskProperties.alphaCutoff = 0.65f;
        maskProperties.baseColorFactor[3] = 0.58f;
    }
    if (useBenchmarkScene && BenchmarkAlphaBlendMaterialRequested()) {
        se::MaterialProperties& blendProperties = blueCubeMaterial.Properties();
        blendProperties.alphaMode = se::MaterialAlphaMode::Blend;
        blendProperties.baseColorFactor[3] = 0.42f;
    }
    if (useBenchmarkScene && BenchmarkUvTransformMaterialRequested()) {
        se::MaterialProperties& uvProperties = groundMaterial.Properties();
        uvProperties.uvTransform = { 0.125f, 0.25f, 2.0f, 1.5f };
        uvProperties.uvControls = { 0.7853982f, 1.0f, 0.0f, 0.0f };
    }
    if (useBenchmarkScene && BenchmarkDoubleSidedMaterialRequested()) {
        se::MaterialProperties& doubleSidedProperties = greenCubeMaterial.Properties();
        doubleSidedProperties.doubleSided = true;
    }
    if (useBenchmarkScene && BenchmarkClearcoatMaterialRequested()) {
        se::MaterialProperties& clearcoatProperties = cubeMaterial.Properties();
        clearcoatProperties.clearcoatFactor = 1.0f;
        clearcoatProperties.clearcoatRoughness = 0.12f;
        clearcoatProperties.viewControls[1] = 1.25f;
    }
    if (useBenchmarkScene && BenchmarkClearcoatTextureMaterialRequested()) {
        se::MaterialProperties& clearcoatTextureProperties = cubeMaterial.Properties();
        clearcoatTextureProperties.clearcoatFactor = 1.0f;
        clearcoatTextureProperties.clearcoatRoughness = 0.18f;
        clearcoatTextureProperties.viewControls[1] = 1.2f;
        BenchmarkTexturePixels clearcoatPixels = CreateBenchmarkClearcoatTexture();
        cubeMaterial.SetClearcoatMap(
            app.Device(),
            app.PhysicalDevice(),
            app.CommandPool(),
            se::VulkanTexturePixels{
                std::span<const se::u8>(clearcoatPixels.rgba.data(), clearcoatPixels.rgba.size()),
                clearcoatPixels.width,
                clearcoatPixels.height
            },
            false,
            false
        );
        clearcoatTextureProperties.cameraControls[3] += se::kMaterialTextureFlagClearcoat;
    }
    if (useBenchmarkScene && BenchmarkClearcoatRoughnessTextureMaterialRequested()) {
        se::MaterialProperties& clearcoatRoughnessProperties = cubeMaterial.Properties();
        clearcoatRoughnessProperties.clearcoatFactor = 1.0f;
        clearcoatRoughnessProperties.clearcoatRoughness = 1.0f;
        clearcoatRoughnessProperties.viewControls[1] = 1.22f;
        BenchmarkTexturePixels clearcoatRoughnessPixels = CreateBenchmarkClearcoatRoughnessTexture();
        cubeMaterial.SetClearcoatRoughnessMap(
            app.Device(),
            app.PhysicalDevice(),
            app.CommandPool(),
            se::VulkanTexturePixels{
                std::span<const se::u8>(
                    clearcoatRoughnessPixels.rgba.data(),
                    clearcoatRoughnessPixels.rgba.size()
                ),
                clearcoatRoughnessPixels.width,
                clearcoatRoughnessPixels.height
            },
            false,
            false
        );
        clearcoatRoughnessProperties.cameraControls[3] +=
            se::kMaterialTextureFlagClearcoatRoughness;
    }
    if (useBenchmarkScene && BenchmarkTransmissionMaterialRequested()) {
        se::MaterialProperties& transmissionProperties = blueCubeMaterial.Properties();
        transmissionProperties.transmissionFactor = 0.72f;
        transmissionProperties.viewControls[1] = 1.1f;
        transmissionProperties.cameraControls[1] = 0.18f;
    }
    if (useBenchmarkScene && BenchmarkTransmissionTextureMaterialRequested()) {
        se::MaterialProperties& transmissionTextureProperties = blueCubeMaterial.Properties();
        transmissionTextureProperties.transmissionFactor = 1.0f;
        transmissionTextureProperties.viewControls[1] = 1.08f;
        transmissionTextureProperties.cameraControls[1] = 0.2f;
        BenchmarkTexturePixels transmissionPixels = CreateBenchmarkTransmissionTexture();
        blueCubeMaterial.SetTransmissionMap(
            app.Device(),
            app.PhysicalDevice(),
            app.CommandPool(),
            se::VulkanTexturePixels{
                std::span<const se::u8>(transmissionPixels.rgba.data(), transmissionPixels.rgba.size()),
                transmissionPixels.width,
                transmissionPixels.height
            },
            false,
            false
        );
        transmissionTextureProperties.cameraControls[3] += se::kMaterialTextureFlagTransmission;
    }
    if (useBenchmarkScene && BenchmarkVolumeMaterialRequested()) {
        se::MaterialProperties& volumeProperties = blueCubeMaterial.Properties();
        volumeProperties.transmissionFactor = std::max(volumeProperties.transmissionFactor, 0.82f);
        volumeProperties.volumeThicknessFactor = 2.8f;
        volumeProperties.volumeAttenuationDistance = 1.45f;
        volumeProperties.volumeAttenuationColor = { 0.42f, 0.82f, 1.0f, 1.0f };
        volumeProperties.viewControls[1] = 1.05f;
        volumeProperties.cameraControls[1] = 0.22f;
    }
    if (useBenchmarkScene && (BenchmarkOpacityTextureMaterialRequested() ||
        BenchmarkOpacityBlendTextureMaterialRequested())) {
        se::MaterialProperties& opacityProperties = cubeMaterial.Properties();
        if (BenchmarkOpacityBlendTextureMaterialRequested()) {
            opacityProperties.alphaMode = se::MaterialAlphaMode::Blend;
            opacityProperties.renderClass = se::MaterialRenderClass::Transparent;
        } else {
            opacityProperties.alphaMode = se::MaterialAlphaMode::Mask;
            opacityProperties.alphaCutoff = 0.6f;
        }
        BenchmarkTexturePixels opacityPixels = CreateBenchmarkOpacityTexture();
        cubeMaterial.SetOpacityMap(
            app.Device(),
            app.PhysicalDevice(),
            app.CommandPool(),
            se::VulkanTexturePixels{
                std::span<const se::u8>(opacityPixels.rgba.data(), opacityPixels.rgba.size()),
                opacityPixels.width,
                opacityPixels.height
            },
            false,
            false
        );
        opacityProperties.cameraControls[3] += se::kMaterialTextureFlagOpacity;
    }

    app.RenderResources().RegisterMaterial("WarmCubeMaterial", cubeMaterial);
    app.RenderResources().RegisterMaterial("BlueCubeMaterial", blueCubeMaterial);
    app.RenderResources().RegisterMaterial("GreenCubeMaterial", greenCubeMaterial);
    app.RenderResources().RegisterMaterial("GroundMaterial", groundMaterial);
    app.RenderResources().RegisterMaterial("GridMaterial", gridMaterial);

    se::Scene3D scene;
    se::RuntimeModelLoader runtimeModelLoader(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        app.MaterialLibrary(),
        app.RenderResources(),
        scene,
        checkerTexturePath
    );
    if (useBenchmarkScene) {
        BuildBenchmarkGridScene(scene, BenchmarkGridSize());
    } else if (!hasStartupModel) {
        se::Renderable3D& ground = scene.CreateRenderable(
            "Ground",
            "Plane",
            "GroundMaterial"
        );
        ground.Transform().SetPosition({ 0.0f, -1.15f, 0.0f });
        ground.Transform().SetScale({ 8.0f, 1.0f, 8.0f });
        ground.Transform().SetAnimateRotation(false);
        ground.SetDrawOrder(-10);

        se::Renderable3D& centerCube = scene.CreateRenderable(
            "Center Cube",
            "Cube",
            "WarmCubeMaterial"
        );
        centerCube.Transform().SetPosition({ 0.0f, 0.0f, 0.0f });
        centerCube.Transform().SetScale({ 1.5f, 1.5f, 1.5f });
        centerCube.Transform().SetRotationSpeedDegreesPerSecond({ 8.0f, 24.0f, 0.0f });

        se::Renderable3D& leftCube = scene.CreateRenderable(
            "Left Cube",
            "Cube",
            "BlueCubeMaterial"
        );
        leftCube.Transform().SetPosition({ -2.1f, -0.35f, -0.85f });
        leftCube.Transform().SetScale({ 0.9f, 0.9f, 0.9f });
        leftCube.Transform().SetRotationDegrees({ 0.0f, 28.0f, 0.0f });
        leftCube.Transform().SetRotationSpeedDegreesPerSecond({ 0.0f, -18.0f, 10.0f });

        se::Renderable3D& rightCube = scene.CreateRenderable(
            "Right Cube",
            "Cube",
            "GreenCubeMaterial"
        );
        rightCube.Transform().SetPosition({ 2.0f, -0.15f, 0.9f });
        rightCube.Transform().SetScale({ 1.1f, 1.1f, 1.1f });
        rightCube.Transform().SetRotationDegrees({ 0.0f, -18.0f, 0.0f });
        rightCube.Transform().SetRotationSpeedDegreesPerSecond({ 12.0f, 14.0f, 0.0f });

        scene.CreatePointLight(
            "Warm Key Point Light",
            { -1.8f, 1.2f, -1.4f },
            4.6f,
            { 1.0f, 0.42f, 0.24f },
            4.0f
        );
        scene.CreatePointLight(
            "Cool Rim Point Light",
            { 2.2f, 0.85f, 0.7f },
            4.2f,
            { 0.22f, 0.46f, 1.0f },
            3.6f
        );
        scene.CreatePointLight(
            "Green Fill Point Light",
            { 0.0f, 1.5f, 2.6f },
            5.0f,
            { 0.34f, 1.0f, 0.48f },
            2.8f
        );
        scene.CreateSpotLight(
            "Center Spot Light",
            { 0.0f, 3.7f, 3.2f },
            { 0.0f, -0.78f, -0.62f },
            6.8f,
            { 1.0f, 0.90f, 0.60f },
            4.6f,
            13.0f,
            27.0f
        );
        scene.CreateRectLight(
            "Cool Rect Area Light",
            { -2.7f, 2.4f, 2.1f },
            { 0.48f, -0.55f, -0.68f },
            2.8f,
            1.1f,
            6.6f,
            { 0.55f, 0.78f, 1.0f },
            3.2f
        );
        scene.CreateReflectionProbe(
            "Default Scene Reflection Probe",
            { 0.0f, 1.2f, 0.0f },
            5.5f,
            { 4.8f, 3.0f, 4.8f },
            { 1.0f, 0.82f, 0.62f },
            1.25f,
            0.65f,
            2.0f
        );
    }

    if (hasStartupModel) {
        std::cout << "Loading startup model: "
            << se::UnrealPathToUtf8(startupModelPath)
            << std::endl;
        const se::RuntimeModelLoadResult defaultModelLoad =
            runtimeModelLoader.LoadIntoScene(
            startupModelPath,
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f }
        );
        if (defaultModelLoad.loaded) {
            std::cout << "Startup model loaded: "
                << defaultModelLoad.message
                << std::endl;
        } else {
            std::cout << "[warning] Startup model load failed: "
                << defaultModelLoad.message
                << std::endl;
        }
    }
    const se::u32 loadedBridgeMeshInstances =
        LoadBridgeMeshInstances(startupBridgeScene, runtimeModelLoader);
    (void)loadedBridgeMeshInstances;

    if (!hasStartupModel && !useBenchmarkScene) {
        se::Renderable3D& grid = scene.CreateRenderable(
            "Viewport Grid",
            "Grid",
            "GridMaterial"
        );
        grid.Transform().SetPosition({ 0.0f, -1.125f, 0.0f });
        grid.Transform().SetAnimateRotation(false);
        grid.SetDrawOrder(-9);
        grid.SetPickable(false);
    }

    se::Camera3D camera;
    if (useBenchmarkScene) {
        camera.SetPose(
            { 0.0f, 5.8f, 15.5f },
            { 0.0f, -0.38f, -1.0f }
        );
        camera.SetFovScale(0.78f);
    } else {
        camera.SetOrbit(3.75f, 0.34f, 5.0f);
    }
    const bool bridgeCameraApplied =
        !useBenchmarkScene && ApplyBridgeSceneCamera(startupBridgeScene, camera);
    (void)bridgeCameraApplied;
    if (!useBenchmarkScene) {
        ApplyBridgeSceneClipPlanes(startupBridgeScene, camera);
    }
    BridgeSceneLightApplyResult bridgeLights =
        !useBenchmarkScene
            ? ApplyBridgeSceneLights(startupBridgeScene, scene, camera)
            : BridgeSceneLightApplyResult{};
    if (!bridgeLights.anyApplied) {
        ApplySceneDirectionalLight(scene, camera);
    }
    const bool benchmarkCameraMotionRequested =
        !useBenchmarkScene && BenchmarkCameraMotionRequested();
    const glm::vec3 benchmarkPartialLocalShadowBasePosition{
        -2.2f,
        1.1f,
        -1.8f
    };
    se::f32 benchmarkPartialLocalShadowTime = 0.0f;
    se::f32 benchmarkCameraMotionTime = 0.0f;
    PickClickState pickClickState{};

    app.CreateRenderer();
    SE_ASSERT(app.Renderer() != nullptr, "Forward 3D demo needs a renderer");
    bridgeLights.skyLightApplied =
        ApplyBridgeSkyLightToRenderer(bridgeLights, *app.Renderer());
    se::SetBenchmarkSceneDiagnostics(
        BenchmarkDiagnosticsForStartupBridgeScene(
            startupBridgeScene,
            loadedBridgeMeshInstances,
            bridgeCameraApplied,
            bridgeLights
        )
    );
    app.Renderer()->SetFrameMatricesProvider([&](se::f32 aspectRatio) {
        return se::FrameMatrices{
            camera.ViewMatrix(),
            camera.ProjectionMatrix(aspectRatio)
        };
    });
    app.Renderer()->SetRenderQueueBuilder([&](
        se::RenderQueue& renderQueue,
        const se::RenderQueueContext& context
    ) {
        se::RenderQueueBuildOptions buildOptions{};
        buildOptions.frustum = context.frustum;
        buildOptions.cullingStats = context.cullingStats;
        buildOptions.cacheStats = context.cacheStats;
        buildOptions.sceneIdentity = &scene;
        buildOptions.sceneMembershipRevision = scene.MembershipRevision();
        buildOptions.sceneRenderRevision = scene.RenderRevision();
        buildOptions.useSceneRevisions = true;
        renderQueue.BuildFromScene3D(
            app.RenderResources(),
            scene.Renderables(),
            scene.SelectedRenderable(),
            buildOptions
        );
        if (context.shadowRenderQueue != nullptr) {
            context.shadowRenderQueue->BuildShadowCastersFrom(
                renderQueue,
                context.shadowCullingStats
            );
            if (context.shadowCullingStats != nullptr &&
                context.cullingStats != nullptr) {
                context.shadowCullingStats->culled += context.cullingStats->culled;
            }
        }
    });
    app.Renderer()->SetImGui3DContext(&scene, &camera);

    app.Run([&](float deltaSeconds, float) {
        const float clampedDeltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.05f);
        if (!useBenchmarkScene) {
            if (benchmarkCameraMotionRequested) {
                benchmarkCameraMotionTime += std::max(
                    clampedDeltaSeconds,
                    1.0f / 60.0f
                );
                ApplyBenchmarkCameraMotion(camera, benchmarkCameraMotionTime);
            } else {
                camera.Update(app.WindowHandle(), clampedDeltaSeconds, ImGuiWantsMouse());
                HandleScenePicking(
                    app.WindowHandle(),
                    camera,
                    scene,
                    clampedDeltaSeconds,
                    pickClickState
                );
            }
            if (!bridgeLights.anyApplied) {
                ApplySceneDirectionalLight(scene, camera);
            }
        } else if (animateBenchmarkPartialLocalShadowCache) {
            benchmarkPartialLocalShadowTime += std::max(
                clampedDeltaSeconds,
                1.0f / 60.0f
            );
            const glm::vec3 offset{
                std::sin(benchmarkPartialLocalShadowTime * 1.7f) * 0.18f,
                0.0f,
                std::cos(benchmarkPartialLocalShadowTime * 1.3f) * 0.12f
            };
            scene.MovePointLight(0, benchmarkPartialLocalShadowBasePosition + offset);
        }
        ApplyCameraToMaterial(camera, cubeMaterial);
        ApplyCameraToMaterial(camera, blueCubeMaterial);
        ApplyCameraToMaterial(camera, greenCubeMaterial);
        ApplyCameraToMaterial(camera, groundMaterial);
        ApplyCameraToMaterial(camera, gridMaterial);
        if (!useBenchmarkScene) {
            LoadDroppedModels(app, runtimeModelLoader);
        }
        runtimeModelLoader.ForEachMaterial([&](se::VulkanMaterial& material) {
            ApplyCameraToMaterial(camera, material);
        });
        scene.Update(clampedDeltaSeconds);
    });

    app.WindowHandle().SetCursorCaptured(false);

    return 0;
}
