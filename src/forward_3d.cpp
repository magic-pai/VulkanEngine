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
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <cmath>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifndef SE_SHADER_DIR
#define SE_SHADER_DIR "shaders"
#endif

#ifndef SE_ASSET_DIR
#define SE_ASSET_DIR "assets"
#endif

#ifndef SE_FORCE_LIGHTING_SHOWCASE
#define SE_FORCE_LIGHTING_SHOWCASE 0
#endif

#ifndef SE_FORCE_PBR_MODEL_SHOWCASE
#define SE_FORCE_PBR_MODEL_SHOWCASE 0
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
    const se::Camera3D& camera,
    bool previewLighting = false
) {
    scene.SetPrimaryDirectionalLight(
        previewLighting
            ? "Camera Preview Directional Light"
            : "Camera Key Directional Light",
        SceneKeyLightDirection(camera),
        previewLighting ? 1.15f : 0.78f,
        previewLighting ? 0.38f : 0.22f,
        previewLighting ? 0.32f : 0.24f
    );
}

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool UsesReferenceModelLighting(const std::filesystem::path& modelPath) {
    const std::string filename = LowerAscii(modelPath.filename().string());
    return filename == "demo_crystal.obj" ||
        filename == "articulated_links.obj" ||
        filename == "skinned_probe.dae";
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

glm::vec3 RotationDegreesFromLocalPositiveYToDirection(glm::vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        return {};
    }

    direction = glm::normalize(direction);
    const se::f32 zRadians = std::asin(std::clamp(-direction.x, -1.0f, 1.0f));
    const se::f32 cosZ = std::cos(zRadians);
    const se::f32 xRadians = std::abs(cosZ) > 0.0001f
        ? std::atan2(direction.z, direction.y)
        : 0.0f;

    return {
        glm::degrees(xRadians),
        0.0f,
        glm::degrees(zRadians)
    };
}

void ConfigureLightGizmoRenderable(
    se::Renderable3D& renderable,
    glm::vec3 position,
    glm::vec3 rotationDegrees,
    glm::vec3 scale,
    se::i32 drawOrder
) {
    renderable.Transform().SetPosition(position);
    renderable.Transform().SetRotationDegrees(rotationDegrees);
    renderable.Transform().SetScale(scale);
    renderable.Transform().SetAnimateRotation(false);
    renderable.SetDrawOrder(drawOrder);
    renderable.SetPickable(false);
    renderable.SetCastShadow(false);
}

constexpr se::u32 kLightIndexDigitTextureWidth = 96;
constexpr se::u32 kLightIndexDigitTextureHeight = 144;

se::f32 Smoothstep(se::f32 edge0, se::f32 edge1, se::f32 value) {
    const se::f32 t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::string LightIndexDigitMaterialName(se::u32 digit) {
    return "LightIndexDigit" + std::to_string(digit % 10u) + "Material";
}

std::vector<se::u8> CreateLightIndexDigitPixels(se::u32 digit) {
    constexpr std::array<se::u8, 10> kDigitSegments{
        0b01110111, // 0
        0b00010010, // 1
        0b01011101, // 2
        0b01011011, // 3
        0b00111010, // 4
        0b01101011, // 5
        0b01101111, // 6
        0b01010010, // 7
        0b01111111, // 8
        0b01111011  // 9
    };
    struct SegmentRect {
        se::u8 bit = 0;
        glm::vec2 center{ 0.0f };
        glm::vec2 halfSize{ 0.0f };
    };
    constexpr std::array<SegmentRect, 7> kSegments{{
        { 6u, { 0.0f, 0.73f }, { 0.46f, 0.090f } },
        { 5u, { -0.47f, 0.36f }, { 0.092f, 0.36f } },
        { 4u, { 0.47f, 0.36f }, { 0.092f, 0.36f } },
        { 3u, { 0.0f, 0.0f }, { 0.46f, 0.088f } },
        { 2u, { -0.47f, -0.36f }, { 0.092f, 0.36f } },
        { 1u, { 0.47f, -0.36f }, { 0.092f, 0.36f } },
        { 0u, { 0.0f, -0.73f }, { 0.46f, 0.090f } }
    }};

    auto roundedBoxDistance = [](glm::vec2 point, glm::vec2 center, glm::vec2 halfSize) {
        constexpr se::f32 kCornerRadius = 0.055f;
        const glm::vec2 q =
            glm::abs(point - center) - glm::max(halfSize - glm::vec2(kCornerRadius), glm::vec2(0.0f));
        const se::f32 outside = glm::length(glm::max(q, glm::vec2(0.0f)));
        const se::f32 inside = std::min(std::max(q.x, q.y), 0.0f);
        return outside + inside - kCornerRadius;
    };

    std::vector<se::u8> pixels(
        static_cast<std::size_t>(kLightIndexDigitTextureWidth) *
            kLightIndexDigitTextureHeight * 4u,
        0u
    );
    const se::u8 mask = kDigitSegments[digit % 10u];
    for (se::u32 y = 0; y < kLightIndexDigitTextureHeight; ++y) {
        for (se::u32 x = 0; x < kLightIndexDigitTextureWidth; ++x) {
            const se::f32 nx =
                (static_cast<se::f32>(x) + 0.5f) /
                    static_cast<se::f32>(kLightIndexDigitTextureWidth) * 2.0f - 1.0f;
            const se::f32 ny =
                1.0f - (static_cast<se::f32>(y) + 0.5f) /
                    static_cast<se::f32>(kLightIndexDigitTextureHeight) * 2.0f;
            const glm::vec2 point{ nx, ny };
            se::f32 coverage = 0.0f;
            for (const SegmentRect& segment : kSegments) {
                if ((mask & (1u << segment.bit)) == 0u) {
                    continue;
                }
                const se::f32 distance =
                    roundedBoxDistance(point, segment.center, segment.halfSize);
                coverage = std::max(coverage, 1.0f - Smoothstep(-0.030f, 0.030f, distance));
            }

            const se::u8 alpha = static_cast<se::u8>(
                std::clamp(std::round(coverage * 245.0f), 0.0f, 245.0f)
            );
            const std::size_t offset =
                (static_cast<std::size_t>(y) * kLightIndexDigitTextureWidth + x) * 4u;
            pixels[offset + 0u] = 238u;
            pixels[offset + 1u] = 248u;
            pixels[offset + 2u] = 255u;
            pixels[offset + 3u] = alpha;
        }
    }

    return pixels;
}

void AddLightIndexLabel(
    se::Scene3D& scene,
    std::string prefix,
    se::u32 localLightIndex,
    glm::vec3 center,
    se::i32 drawOrder
) {
    const se::u32 tens = localLightIndex / 10u;
    const se::u32 ones = localLightIndex % 10u;
    const bool twoDigits = localLightIndex >= 10u;
    const se::f32 digitStep = 0.185f;
    const se::f32 labelWidth = twoDigits ? 0.48f : 0.29f;

    se::Renderable3D& backplate = scene.CreateRenderable(
        prefix + " Light Index Backplate " + std::to_string(localLightIndex),
        "Cube",
        "LightIndexLabelBackplateMaterial"
    );
    ConfigureLightGizmoRenderable(
        backplate,
        center + glm::vec3{ 0.0f, 0.0f, -0.018f },
        {},
        { labelWidth, 0.37f, 0.018f },
        drawOrder
    );

    auto addDigit = [&](se::u32 digit, se::f32 xOffset, const std::string& tag) {
        se::Renderable3D& piece = scene.CreateRenderable(
            prefix + " Light Index " + std::to_string(localLightIndex) +
                " " + tag + " Digit " + std::to_string(digit % 10u),
            "Plane",
            LightIndexDigitMaterialName(digit)
        );
        ConfigureLightGizmoRenderable(
            piece,
            center + glm::vec3{ xOffset, 0.0f, 0.014f },
            { 90.0f, 0.0f, 0.0f },
            { 0.155f, 1.0f, 0.255f },
            drawOrder + 1
        );
    };

    if (twoDigits) {
        addDigit(tens, -digitStep * 0.5f, "Tens");
        addDigit(ones, digitStep * 0.5f, "Ones");
    } else {
        addDigit(ones, 0.0f, "Ones");
    }
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

std::filesystem::path PbrModelShowcasePath() {
    const std::string overridePath =
        ReadEnvironmentString("SE_PBR_MODEL_SHOWCASE_PATH");
    return overridePath.empty()
        ? std::filesystem::path(SE_ASSET_DIR) / "models" / "lvjuren.glb"
        : std::filesystem::path(overridePath);
}

std::filesystem::path ExplicitUnrealBridgeManifestPath() {
    const std::string path = ReadEnvironmentString("SE_UE_BRIDGE_MANIFEST");
    return path.empty() ? std::filesystem::path{} : std::filesystem::path(path);
}

bool Forward3DDebugDirectorySceneDefaultActive() {
    if (!ReadEnvironmentString("SE_BENCHMARK_SCENE").empty()) {
        return false;
    }

    const std::string overrideValue =
        LowerAscii(ReadEnvironmentString("SE_FORWARD3D_DEBUG_DEFAULT_SCENE"));
    if (!overrideValue.empty()) {
        return overrideValue == "shadow" ||
            overrideValue == "shadows" ||
            overrideValue == "shadow-regression" ||
            overrideValue == "shadow_regression" ||
            overrideValue == "forward-shadow-regression" ||
            overrideValue == "forward_shadow_regression";
    }

    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    if (error) {
        return false;
    }

    return LowerAscii(cwd.filename().string()) == "debug" &&
        LowerAscii(cwd.parent_path().filename().string()) == "build";
}

std::string Forward3DDebugDirectorySceneOverrideName() {
    const std::string overrideValue =
        LowerAscii(ReadEnvironmentString("SE_FORWARD3D_DEBUG_DEFAULT_SCENE"));
    if (overrideValue.empty()) {
        return {};
    }

    if (overrideValue == "shadow" ||
        overrideValue == "shadows" ||
        overrideValue == "shadow-regression" ||
        overrideValue == "shadow_regression" ||
        overrideValue == "forward-shadow-regression" ||
        overrideValue == "forward_shadow_regression") {
        return "shadow-regression";
    }

    if (overrideValue == "lighting" ||
        overrideValue == "showcase" ||
        overrideValue == "lighting-showcase" ||
        overrideValue == "lighting_showcase") {
        return "lighting-showcase";
    }

    return {};
}

std::string Forward3DBenchmarkSceneName() {
    const std::string sceneName = ReadEnvironmentString("SE_BENCHMARK_SCENE");
    if (!sceneName.empty()) {
        return sceneName;
    }

    const std::string debugOverrideScene = Forward3DDebugDirectorySceneOverrideName();
    if (!debugOverrideScene.empty()) {
        return debugOverrideScene;
    }

    return Forward3DDebugDirectorySceneDefaultActive()
        ? std::string("shadow-regression")
        : std::string{};
}

bool BenchmarkGridSceneRequested() {
    const std::string sceneName = Forward3DBenchmarkSceneName();
    return sceneName == "1" ||
        sceneName == "grid" ||
        sceneName == "forward-grid" ||
        sceneName == "ForwardGrid";
}

bool LightingShowcaseSceneRequested() {
#if SE_FORCE_LIGHTING_SHOWCASE
#if !defined(NDEBUG)
    const std::string forceOff = LowerAscii(
        ReadEnvironmentString("SE_LIGHTING_SHOWCASE_FORCE_OFF")
    );
    const std::string sceneName = Forward3DBenchmarkSceneName();
    const bool explicitlyRequestsShowcase =
        sceneName == "lighting-showcase" ||
        sceneName == "lighting_showcase" ||
        sceneName == "showcase" ||
        sceneName == "lighting" ||
        sceneName == "LightingShowcase";
    if (!explicitlyRequestsShowcase &&
        (forceOff == "1" || forceOff == "true" || forceOff == "on" ||
            forceOff == "yes")) {
        return false;
    }
#endif
    return true;
#else
    const std::string sceneName = Forward3DBenchmarkSceneName();
    return sceneName == "lighting-showcase" ||
        sceneName == "lighting_showcase" ||
        sceneName == "showcase" ||
        sceneName == "lighting" ||
        sceneName == "LightingShowcase";
#endif
}

bool ShadowRegressionSceneRequested() {
    const std::string sceneName = LowerAscii(Forward3DBenchmarkSceneName());
    return sceneName == "shadow" ||
        sceneName == "shadows" ||
        sceneName == "shadow-regression" ||
        sceneName == "shadow_regression" ||
        sceneName == "forward-shadow-regression" ||
        sceneName == "forward_shadow_regression";
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

std::string LightingShowcaseReflectionProbeAssetId() {
    std::string assetId =
        ReadEnvironmentString("SE_SHOWCASE_REFLECTION_PROBE_ASSET");
    if (assetId.empty()) {
        assetId = ReadEnvironmentString("SE_LIGHTING_SHOWCASE_REFLECTION_PROBE_ASSET");
    }
    if (!assetId.empty()) {
        return assetId;
    }

    return (std::filesystem::path(SE_ASSET_DIR) / "skybox" / "bk.jpg").string();
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

#if !defined(NDEBUG)
bool BenchmarkReflectionCaptureLocalityControlRequested() {
    const std::string value = ReadEnvironmentString(
        "SE_REFLECTION_CAPTURE_LOCALITY_CONTROL"
    );
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}
#else
constexpr bool BenchmarkReflectionCaptureLocalityControlRequested() {
    return false;
}
#endif

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

bool ForwardShutdownTraceEnabled() {
    return EnvironmentFlagEnabled("SE_SHUTDOWN_TRACE") ||
        EnvironmentFlagEnabled("SE_FORWARD3D_SHUTDOWN_TRACE");
}

se::f64 ForwardShutdownElapsedMilliseconds(
    std::chrono::steady_clock::time_point startTime
) {
    return std::chrono::duration<se::f64, std::milli>(
        std::chrono::steady_clock::now() - startTime
    ).count();
}

void TraceForwardShutdown(
    const char* label,
    std::chrono::steady_clock::time_point startTime
) {
    if (!ForwardShutdownTraceEnabled()) {
        return;
    }

    std::cout << "[shutdown] forward3d " << label << " +"
        << ForwardShutdownElapsedMilliseconds(startTime) << "ms"
        << std::endl;
}

class ForwardShutdownScopeTrace {
public:
    ForwardShutdownScopeTrace(
        const char* label,
        std::chrono::steady_clock::time_point startTime
    ) : m_Label(label),
        m_StartTime(startTime) {
    }

    ~ForwardShutdownScopeTrace() {
        TraceForwardShutdown(m_Label, m_StartTime);
    }

private:
    const char* m_Label = "";
    std::chrono::steady_clock::time_point m_StartTime;
};

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

bool ForwardFastProcessExitEnabled() {
    if (EnvironmentFlagEnabled("SE_CLEAN_SHUTDOWN") ||
        EnvironmentFlagEnabled("SE_VULKAN_CLEAN_SHUTDOWN")) {
        return false;
    }
    if (EnvironmentFlagDisabled("SE_FAST_PROCESS_EXIT") ||
        EnvironmentFlagDisabled("SE_FAST_EXIT")) {
        return false;
    }

    return true;
}

bool Forward3DProductionShadowProfileRequested() {
    const std::string value =
        LowerAscii(ReadEnvironmentString("SE_FORWARD3D_SHADOW_PROFILE"));
    if (value == "generic" ||
        value == "legacy" ||
        value == "raw" ||
        value == "renderer" ||
        value == "renderer-default") {
        return false;
    }

    return true;
}

void SetEnvironmentDefault(const char* name, const char* value) {
    if (!ReadEnvironmentString(name).empty()) {
        return;
    }

#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 0);
#endif
}

void SetTextureMipLodBiasEnvironmentDefault(const char* value) {
    if (!ReadEnvironmentString("SE_TEXTURE_MIP_LOD_BIAS").empty() ||
        !ReadEnvironmentString("SE_MATERIAL_TEXTURE_MIP_BIAS").empty() ||
        !ReadEnvironmentString("SE_TEXTURE_MIP_BIAS").empty()) {
        return;
    }

    SetEnvironmentDefault("SE_TEXTURE_MIP_LOD_BIAS", value);
}

void SetGlobalIblSourceEnvironmentDefault(const char* value) {
    if (!ReadEnvironmentString("SE_GLOBAL_IBL_SOURCE").empty() ||
        !ReadEnvironmentString("SE_IBL_SOURCE").empty()) {
        return;
    }

    SetEnvironmentDefault("SE_GLOBAL_IBL_SOURCE", value);
}

void SetGlobalIblCachePolicyEnvironmentDefault(const char* value) {
    if (!ReadEnvironmentString("SE_GLOBAL_IBL_CACHE_POLICY").empty() ||
        !ReadEnvironmentString("SE_GLOBAL_IBL_CACHE").empty() ||
        !ReadEnvironmentString("SE_IBL_CACHE_POLICY").empty() ||
        !ReadEnvironmentString("SE_IBL_CACHE").empty()) {
        return;
    }

    SetEnvironmentDefault("SE_GLOBAL_IBL_CACHE_POLICY", value);
}

void SetGlobalIblSourceAssetEnvironmentDefault(const std::string& value) {
    if (!ReadEnvironmentString("SE_GLOBAL_IBL_ASSET").empty() ||
        !ReadEnvironmentString("SE_GLOBAL_IBL_SOURCE_ASSET").empty() ||
        !ReadEnvironmentString("SE_GLOBAL_IBL_SOURCE_PATH").empty() ||
        !ReadEnvironmentString("SE_IBL_ASSET").empty()) {
        return;
    }

    SetEnvironmentDefault("SE_GLOBAL_IBL_ASSET", value.c_str());
}

void ApplyLightingShowcaseIblEnvironmentDefaults() {
    SetGlobalIblSourceEnvironmentDefault("authored-equirectangular");
    SetGlobalIblCachePolicyEnvironmentDefault("prefer-offline");
    SetGlobalIblSourceAssetEnvironmentDefault(
        LightingShowcaseReflectionProbeAssetId()
    );
}

bool DefaultSceneSkinnedFbxProductionRequested() {
    if (EnvironmentFlagDisabled("SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION") ||
        EnvironmentFlagDisabled("SE_DEFAULT_SCENE_SKINNED_FBX") ||
        EnvironmentFlagDisabled("SE_DEFAULT_SCENE_BIND_SKINNED_FBX")) {
        return false;
    }

    if (EnvironmentFlagEnabled("SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION") ||
        EnvironmentFlagEnabled("SE_DEFAULT_SCENE_SKINNED_FBX") ||
        EnvironmentFlagEnabled("SE_DEFAULT_SCENE_BIND_SKINNED_FBX")) {
        return true;
    }

    return true;
}

se::RendererTemporalAntialiasingMode ForwardTemporalAntialiasingModeFromEnvironment() {
    const std::string value = LowerAscii(ReadEnvironmentString("SE_FORWARD3D_AA_MODE"));
    if (value.empty() || value == "default") {
        return se::RendererTemporalAntialiasingMode::DlssSrPerformance;
    }
    if (value == "off" ||
        value == "none" ||
        value == "disabled" ||
        value == "disable" ||
        value == "0") {
        return se::RendererTemporalAntialiasingMode::Off;
    }
    if (value == "taa" ||
        value == "native-taa" ||
        value == "native_taa" ||
        value == "temporal-aa" ||
        value == "temporal_aa") {
        return se::RendererTemporalAntialiasingMode::NativeTaa;
    }
    if (value == "sr-quality" ||
        value == "sr_quality" ||
        value == "dlss-quality" ||
        value == "dlss_quality" ||
        value == "dlss-sr-quality" ||
        value == "dlss_sr_quality") {
        return se::RendererTemporalAntialiasingMode::DlssSrQuality;
    }
    if (value == "sr-balanced" ||
        value == "sr_balanced" ||
        value == "dlss-balanced" ||
        value == "dlss_balanced" ||
        value == "dlss-sr-balanced" ||
        value == "dlss_sr_balanced") {
        return se::RendererTemporalAntialiasingMode::DlssSrBalanced;
    }
    if (value == "sr-performance" ||
        value == "sr_performance" ||
        value == "dlss-performance" ||
        value == "dlss_performance" ||
        value == "dlss-sr-performance" ||
        value == "dlss_sr_performance") {
        return se::RendererTemporalAntialiasingMode::DlssSrPerformance;
    }
    if (value == "dlaa" ||
        value == "dlss" ||
        value == "dlss-dlaa" ||
        value == "dlss_dlaa" ||
        value == "dlss-dlaa-l" ||
        value == "dlss_dlaa_l") {
        return se::RendererTemporalAntialiasingMode::DlssDlaa;
    }
    if (value == "env" || value == "environment") {
        return se::RendererTemporalAntialiasingMode::Environment;
    }

    return se::RendererTemporalAntialiasingMode::DlssSrPerformance;
}

bool ForwardTemporalAntialiasingModeIsDlss(
    se::RendererTemporalAntialiasingMode mode
) {
    return mode == se::RendererTemporalAntialiasingMode::DlssDlaa ||
        mode == se::RendererTemporalAntialiasingMode::DlssSrQuality ||
        mode == se::RendererTemporalAntialiasingMode::DlssSrBalanced ||
        mode == se::RendererTemporalAntialiasingMode::DlssSrPerformance;
}

void ApplyForwardTemporalAntialiasingEnvironmentDefaults(
    se::RendererTemporalAntialiasingMode mode
) {
    // F6 can hot-switch from Native TAA into DLSS after Vulkan device creation.
    // Request the optional DLSS Vulkan requirements up front, while leaving the
    // per-mode temporal contracts below to the active AA mode.
    SetEnvironmentDefault("SE_ENABLE_DLSS_VULKAN_EXTENSIONS", "1");
    SetEnvironmentDefault("SE_UPSCALER_PLUGIN", "dlss");

    if (!ForwardTemporalAntialiasingModeIsDlss(mode)) {
        return;
    }

    switch (mode) {
    case se::RendererTemporalAntialiasingMode::DlssSrQuality:
        SetEnvironmentDefault("SE_DLSS_QUALITY", "quality");
        SetEnvironmentDefault("SE_RENDER_SCALE", "0.666667");
        SetTextureMipLodBiasEnvironmentDefault("-1.58496");
        break;
    case se::RendererTemporalAntialiasingMode::DlssSrBalanced:
        SetEnvironmentDefault("SE_DLSS_QUALITY", "balanced");
        SetEnvironmentDefault("SE_RENDER_SCALE", "0.58");
        SetTextureMipLodBiasEnvironmentDefault("-1.78588");
        break;
    case se::RendererTemporalAntialiasingMode::DlssSrPerformance:
        SetEnvironmentDefault("SE_DLSS_QUALITY", "performance");
        SetEnvironmentDefault("SE_RENDER_SCALE", "0.5");
        SetTextureMipLodBiasEnvironmentDefault("-2.0");
        break;
    case se::RendererTemporalAntialiasingMode::DlssDlaa:
    default:
        SetEnvironmentDefault("SE_DLSS_QUALITY", "dlaa");
        SetEnvironmentDefault("SE_RENDER_SCALE", "1.0");
        break;
    }
    SetEnvironmentDefault("SE_DLSS_PRESET", "l");
    SetEnvironmentDefault("SE_DLSS_PRESENT", "1");
    SetEnvironmentDefault("SE_DLSS_SHARPNESS", "0.0");
    SetEnvironmentDefault("SE_TAA", "1");
    SetEnvironmentDefault("SE_TEMPORAL_JITTER", "1");
    SetEnvironmentDefault("SE_TAA_APPLY_JITTER", "1");
    SetEnvironmentDefault("SE_RENDER_SCALE_APPLY", "1");
    SetEnvironmentDefault("SE_TEMPORAL_VELOCITY_JITTER_POLICY", "jittered");
}

const char* TemporalAntialiasingModeName(
    se::RendererTemporalAntialiasingMode mode
) {
    switch (mode) {
    case se::RendererTemporalAntialiasingMode::NativeTaa:
        return "Native TAA";
    case se::RendererTemporalAntialiasingMode::DlssDlaa:
        return "DLSS DLAA L";
    case se::RendererTemporalAntialiasingMode::DlssSrQuality:
        return "DLSS SR Quality 66.7%";
    case se::RendererTemporalAntialiasingMode::DlssSrBalanced:
        return "DLSS SR Balanced 58%";
    case se::RendererTemporalAntialiasingMode::DlssSrPerformance:
        return "DLSS SR Performance 50%";
    case se::RendererTemporalAntialiasingMode::Off:
        return "Off";
    case se::RendererTemporalAntialiasingMode::Environment:
    default:
        return "Environment";
    }
}

int ReadEnvironmentInt(
    const char* primaryName,
    const char* aliasName,
    int fallback,
    int minValue,
    int maxValue
) {
    std::string value = ReadEnvironmentString(primaryName);
    if (value.empty() && aliasName != nullptr) {
        value = ReadEnvironmentString(aliasName);
    }
    if (value.empty()) {
        return fallback;
    }

    return std::clamp(std::atoi(value.c_str()), minValue, maxValue);
}

int ForwardWindowWidth() {
    return ReadEnvironmentInt(
        "SE_WINDOW_WIDTH",
        "SE_FORWARD3D_WINDOW_WIDTH",
        1280,
        320,
        7680
    );
}

int ForwardWindowHeight() {
    return ReadEnvironmentInt(
        "SE_WINDOW_HEIGHT",
        "SE_FORWARD3D_WINDOW_HEIGHT",
        720,
        240,
        4320
    );
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

bool BenchmarkObjectMotionRequested() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_OBJECT_MOTION");
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

enum class BenchmarkObjectMotionMode {
    None,
    Orbit,
    Articulated
};

BenchmarkObjectMotionMode BenchmarkObjectMotionModeFromEnvironment() {
    const std::string value = ReadEnvironmentString("SE_BENCHMARK_OBJECT_MOTION");
    if (value == "articulated" ||
        value == "Articulated" ||
        value == "ARTICULATED") {
        return BenchmarkObjectMotionMode::Articulated;
    }

    return BenchmarkObjectMotionRequested()
        ? BenchmarkObjectMotionMode::Orbit
        : BenchmarkObjectMotionMode::None;
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

bool MeshLodEnabledFromEnvironment() {
    const std::string value = ReadEnvironmentString("SE_MESH_LOD");
    if (value.empty()) {
        return true;
    }
    return value != "0" &&
        value != "false" && value != "FALSE" &&
        value != "off" && value != "OFF" &&
        value != "no" && value != "NO";
}

void ApplyBenchmarkObjectMotion(
    se::Renderable3D& renderable,
    const glm::vec3& basePosition,
    const glm::vec3& baseRotationDegrees,
    se::f32 elapsedSeconds
) {
    const se::f32 speed =
        std::max(ReadEnvironmentF32("SE_BENCHMARK_OBJECT_MOTION_SPEED", 0.9f), 0.0f);
    const se::f32 radius =
        std::clamp(ReadEnvironmentF32("SE_BENCHMARK_OBJECT_MOTION_RADIUS", 0.42f), 0.0f, 2.0f);
    const se::f32 phase = elapsedSeconds * speed;
    renderable.Transform().SetPosition(basePosition + glm::vec3{
        std::sin(phase) * radius,
        std::sin(phase * 1.37f) * radius * 0.18f,
        std::cos(phase * 0.83f) * radius * 0.45f
    });
    renderable.Transform().SetRotationDegrees(baseRotationDegrees + glm::vec3{
        std::sin(phase * 0.67f) * 10.0f,
        phase * 57.29578f,
        std::cos(phase * 0.51f) * 8.0f
    });
}

void ApplyBenchmarkArticulatedObjectMotion(
    se::Renderable3D& renderable,
    const glm::vec3& basePosition,
    const glm::vec3& baseRotationDegrees,
    se::f32 elapsedSeconds,
    std::size_t objectIndex,
    std::size_t objectCount
) {
    const se::f32 speed =
        std::max(ReadEnvironmentF32("SE_BENCHMARK_OBJECT_MOTION_SPEED", 1.1f), 0.0f);
    const se::f32 radius =
        std::clamp(ReadEnvironmentF32("SE_BENCHMARK_OBJECT_MOTION_RADIUS", 0.32f), 0.0f, 2.0f);
    const se::f32 index = static_cast<se::f32>(objectIndex);
    const se::f32 count = static_cast<se::f32>(std::max<std::size_t>(objectCount, 1));
    const se::f32 centeredIndex = index - (count - 1.0f) * 0.5f;
    const se::f32 phase = elapsedSeconds * speed + index * 1.37f;
    renderable.Transform().SetPosition(basePosition + glm::vec3{
        std::sin(phase) * radius * (0.38f + index * 0.08f),
        std::sin(phase * 1.43f) * radius * 0.26f,
        std::cos(phase * 0.91f) * radius * (0.28f + std::abs(centeredIndex) * 0.10f)
    });
    renderable.Transform().SetRotationDegrees(baseRotationDegrees + glm::vec3{
        std::sin(phase * 0.77f) * 18.0f,
        phase * 42.0f + centeredIndex * 18.0f,
        std::cos(phase * 1.09f) * 14.0f
    });
}

struct BenchmarkMovingObject {
    se::Renderable3D* renderable = nullptr;
    glm::vec3 basePosition{ 0.0f };
    glm::vec3 baseRotationDegrees{ 0.0f };
};

void AddBenchmarkMovingObject(
    std::vector<BenchmarkMovingObject>& movingObjects,
    se::Renderable3D& renderable
) {
    movingObjects.push_back(BenchmarkMovingObject{
        &renderable,
        renderable.Transform().Position(),
        renderable.Transform().RotationDegrees()
    });
}

void AddBenchmarkMovingObjectsFromSceneRange(
    std::vector<BenchmarkMovingObject>& movingObjects,
    se::Scene3D& scene,
    std::size_t firstRenderableIndex
) {
    const std::span<se::Renderable3D* const> renderables = scene.Renderables();
    if (firstRenderableIndex >= renderables.size()) {
        return;
    }

    for (std::size_t index = firstRenderableIndex; index < renderables.size(); ++index) {
        if (renderables[index] != nullptr) {
            AddBenchmarkMovingObject(movingObjects, *renderables[index]);
        }
    }
}

void DisableBenchmarkAutoRotation(
    const std::vector<BenchmarkMovingObject>& movingObjects
) {
    for (const BenchmarkMovingObject& movingObject : movingObjects) {
        if (movingObject.renderable == nullptr) {
            continue;
        }

        movingObject.renderable->Transform().SetAnimateRotation(false);
        movingObject.renderable->Transform().SetRotationSpeedDegreesPerSecond({
            0.0f,
            0.0f,
            0.0f
        });
    }
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
        if (capturedSceneProbeRequested &&
            BenchmarkReflectionCaptureLocalityControlRequested()) {
            scene.CreateReflectionProbe(
                "Benchmark Distant Locality Control Probe",
                { 64.0f, 1.2f, 0.0f },
                4.5f,
                { 3.0f, 2.6f, 3.0f },
                { 0.82f, 0.88f, 1.0f },
                0.9f,
                0.55f,
                1.5f,
                se::ReflectionProbeCaptureSource::CapturedScene,
                {},
                se::ReflectionProbeRefreshPolicy::SceneDirty
            );
        }
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

void BuildLightingShowcaseScene(
    se::Scene3D& scene,
    se::Scene3D* lightIndexOverlayScene = nullptr
) {
#if !defined(NDEBUG)
    const bool showcaseWallsOff =
        EnvironmentFlagEnabled("SE_SHOWCASE_WALLS_OFF") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_WALLS_OFF");
#else
    constexpr bool showcaseWallsOff = false;
#endif
#if !defined(NDEBUG)
    const bool showcaseSupportSurfacesOff =
        EnvironmentFlagEnabled("SE_SHOWCASE_SUPPORT_SURFACES_OFF") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_SUPPORT_SURFACES_OFF");
#else
    constexpr bool showcaseSupportSurfacesOff = false;
#endif
    const bool showcaseLocalLightsOff =
        EnvironmentFlagEnabled("SE_SHOWCASE_LOCAL_LIGHTS_OFF") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_LOCAL_LIGHTS_OFF");
    const bool showcaseSphereShadowCastersOn =
        EnvironmentFlagEnabled("SE_SHOWCASE_SPHERE_SHADOW_CASTERS_ON") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_SPHERE_SHADOW_CASTERS_ON");
    const bool showcaseSphereShadowCastersOff =
        !showcaseSphereShadowCastersOn &&
        (
            EnvironmentFlagEnabled("SE_SHOWCASE_SPHERE_SHADOW_CASTERS_OFF") ||
            EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_SPHERE_SHADOW_CASTERS_OFF")
        );
    const bool showcasePropShadowCastersOff =
        EnvironmentFlagEnabled("SE_SHOWCASE_PROP_SHADOW_CASTERS_OFF") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_PROP_SHADOW_CASTERS_OFF");
    const bool showcaseFloorShadowCasterOff =
        EnvironmentFlagEnabled("SE_SHOWCASE_FLOOR_SHADOW_CASTER_OFF") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_FLOOR_SHADOW_CASTER_OFF");
    const bool showcasePlainSpheres =
        EnvironmentFlagEnabled("SE_SHOWCASE_PLAIN_SPHERES") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_PLAIN_SPHERES");
    const bool showcaseReflectionProbesOff =
        EnvironmentFlagEnabled("SE_SHOWCASE_REFLECTION_PROBES_OFF") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_REFLECTION_PROBES_OFF");
    const bool showcaseCapturedReflectionProbesOff =
        EnvironmentFlagEnabled("SE_SHOWCASE_CAPTURED_REFLECTION_PROBES_OFF") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_CAPTURED_REFLECTION_PROBES_OFF");
    const bool showcaseAuthoredReflectionProbesOn =
        EnvironmentFlagEnabled("SE_SHOWCASE_AUTHORED_REFLECTION_PROBES") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_AUTHORED_REFLECTION_PROBES");
    const bool showcaseFrontBounceLightsOn =
        EnvironmentFlagEnabled("SE_SHOWCASE_FRONT_BOUNCE_LIGHTS_ON") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_FRONT_BOUNCE_LIGHTS_ON");
    const bool showcaseOverheadFillOn =
        EnvironmentFlagEnabled("SE_SHOWCASE_OVERHEAD_FILL_ON") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_OVERHEAD_FILL_ON");
    const bool showcaseLightIndexLabelsOn =
        EnvironmentFlagEnabled("SE_LIGHT_INDEX_LABELS_ON") ||
        EnvironmentFlagEnabled("SE_SHOWCASE_LIGHT_INDEX_LABELS_ON") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_LIGHT_INDEX_LABELS_ON") ||
        EnvironmentFlagEnabled("SE_LOCAL_LIGHT_INDEX_LABELS_ON");
    const bool showcaseLightIndexLabelsOff =
        EnvironmentFlagEnabled("SE_LIGHT_INDEX_LABELS_OFF") ||
        EnvironmentFlagEnabled("SE_SHOWCASE_LIGHT_INDEX_LABELS_OFF") ||
        EnvironmentFlagEnabled("SE_LIGHTING_SHOWCASE_LIGHT_INDEX_LABELS_OFF");
    const bool showcaseLightIndexLabelsVisible =
        showcaseLightIndexLabelsOn && !showcaseLightIndexLabelsOff;
    // BuildFrameLightSet packs local lights by kind: points, then spots, then
    // rects. Keep the optional debug labels on that same frame-light index.
    constexpr se::u32 kShowcasePointLightCount = 1u;
    constexpr se::u32 kShowcaseSpotLightCount = 2u;
    se::u32 nextPointLightIndex = 0u;
    se::u32 nextSpotLightIndex = kShowcasePointLightCount;
    se::u32 nextRectLightIndex =
        kShowcasePointLightCount + kShowcaseSpotLightCount;

    auto place = [](
        se::Renderable3D& renderable,
        glm::vec3 position,
        glm::vec3 scale,
        glm::vec3 rotationDegrees = glm::vec3(0.0f),
        se::i32 drawOrder = 0
    ) {
        renderable.Transform().SetPosition(position);
        renderable.Transform().SetScale(scale);
        renderable.Transform().SetRotationDegrees(rotationDegrees);
        renderable.Transform().SetAnimateRotation(false);
        renderable.SetDrawOrder(drawOrder);
    };

    if (!showcaseSupportSurfacesOff) {
        se::Renderable3D& floor = scene.CreateRenderable(
            "Showcase Charcoal Floor",
            "Plane",
            "ShowcaseCharcoalMaterial"
        );
        place(floor, { 0.0f, -1.05f, 0.0f }, { 8.5f, 1.0f, 6.2f }, {}, -30);
        floor.SetPickable(false);
        if (showcaseFloorShadowCasterOff) {
            floor.SetCastShadow(false);
        }
    }

    if (!showcaseWallsOff) {
        se::Renderable3D& backWall = scene.CreateRenderable(
            "Showcase Back Wall",
            "Cube",
            "ShowcaseBackdropMaterial"
        );
        place(backWall, { 0.0f, 1.05f, -2.35f }, { 8.5f, 4.2f, 0.16f }, {}, -25);
        backWall.SetPickable(false);
        backWall.SetCastShadow(false);

        se::Renderable3D& leftWall = scene.CreateRenderable(
            "Showcase Left Shadow Wall",
            "Cube",
            "ShowcaseBackdropMaterial"
        );
        place(leftWall, { -4.15f, 0.7f, 0.0f }, { 0.12f, 3.5f, 5.0f }, {}, -24);
        leftWall.SetPickable(false);
        leftWall.SetCastShadow(false);

        se::Renderable3D& rightWall = scene.CreateRenderable(
            "Showcase Right Shadow Wall",
            "Cube",
            "ShowcaseBackdropMaterial"
        );
        place(rightWall, { 4.15f, 0.7f, 0.0f }, { 0.12f, 3.5f, 5.0f }, {}, -24);
        rightWall.SetPickable(false);
        rightWall.SetCastShadow(false);
    }

    se::Renderable3D& ceiling = scene.CreateRenderable(
        "Showcase Soft Ceiling",
        "Cube",
        "ShowcaseBackdropMaterial"
    );
    place(ceiling, { 0.0f, 2.95f, 0.0f }, { 8.5f, 0.12f, 5.0f }, {}, -23);
    ceiling.SetPickable(false);
    ceiling.SetCastShadow(false);

    const std::array<glm::vec3, 3> pedestalPositions{
        glm::vec3{ -2.15f, -0.8f, 0.15f },
        glm::vec3{ 0.0f, -0.78f, -0.15f },
        glm::vec3{ 2.15f, -0.8f, 0.15f }
    };
    if (!showcaseSupportSurfacesOff) {
        for (std::size_t index = 0; index < pedestalPositions.size(); ++index) {
            se::Renderable3D& pedestal = scene.CreateRenderable(
                "Showcase Pedestal " + std::to_string(index),
                "Cube",
                index == 1 ? "ShowcaseWarmStoneMaterial" : "ShowcaseCharcoalMaterial"
            );
            place(
                pedestal,
                pedestalPositions[index],
                { 1.36f, 0.48f, 1.2f },
                { 0.0f, index == 1 ? 0.0f : (index == 0 ? -8.0f : 8.0f), 0.0f },
                -10
            );
            if (showcasePropShadowCastersOff) {
                pedestal.SetCastShadow(false);
            }
        }
    }

    se::Renderable3D& metalSphere = scene.CreateRenderable(
        "Showcase Polished Metal Sphere",
        "Sphere",
        showcasePlainSpheres ? "ShowcaseWarmStoneMaterial" : "ShowcaseReflectionTestSilverMaterial"
    );
    place(metalSphere, { -2.15f, 0.18f, 0.15f }, { 1.18f, 1.18f, 1.18f });
    metalSphere.SetReflectionCaptureVisible(false);
    if (showcaseSphereShadowCastersOff) {
        metalSphere.SetCastShadow(false);
    }

    se::Renderable3D& mirrorSphere = scene.CreateRenderable(
        "Showcase Center Mirror Sphere",
        "Sphere",
        showcasePlainSpheres ? "ShowcaseWarmStoneMaterial" : "ShowcasePerfectMirrorMaterial"
    );
    place(mirrorSphere, { 0.0f, 0.15f, -0.15f }, { 1.08f, 1.08f, 1.08f });
    mirrorSphere.SetReflectionCaptureVisible(false);
    if (showcaseSphereShadowCastersOff) {
        mirrorSphere.SetCastShadow(false);
    }

    se::Renderable3D& lacquerCube = scene.CreateRenderable(
        "Showcase Gloss Black Cube",
        "Cube",
        "ShowcaseGlossBlackMaterial"
    );
    place(lacquerCube, { 2.15f, -0.02f, 0.15f }, { 0.95f, 0.95f, 0.95f }, { 0.0f, -24.0f, 0.0f });
    if (showcasePropShadowCastersOff) {
        lacquerCube.SetCastShadow(false);
    }

    se::Renderable3D& warmBlock = scene.CreateRenderable(
        "Showcase Warm Rough Block",
        "Cube",
        "ShowcaseWarmStoneMaterial"
    );
    place(warmBlock, { -0.95f, -0.47f, -1.32f }, { 0.8f, 0.8f, 0.8f }, { 0.0f, 18.0f, 0.0f });
    if (showcasePropShadowCastersOff) {
        warmBlock.SetCastShadow(false);
    }

    se::Renderable3D& thinMetal = scene.CreateRenderable(
        "Showcase Thin Metal Slab",
        "Cube",
        "ShowcaseBrushedMetalMaterial"
    );
    place(thinMetal, { 0.85f, -0.46f, -1.25f }, { 0.32f, 1.15f, 0.76f }, { 0.0f, -28.0f, 0.0f });
    if (showcasePropShadowCastersOff) {
        thinMetal.SetCastShadow(false);
    }

    const std::array<glm::vec3, 5> materialSamplePositions{
        glm::vec3{ -1.9f, -0.72f, 2.12f },
        glm::vec3{ -0.95f, -0.72f, 2.20f },
        glm::vec3{ 0.0f, -0.72f, 2.24f },
        glm::vec3{ 0.95f, -0.72f, 2.20f },
        glm::vec3{ 1.9f, -0.72f, 2.12f }
    };
    const std::array<const char*, 5> materialSampleMaterials{
        "ShowcaseReferenceChromeMaterial",
        "ShowcaseReferenceSatinMetalMaterial",
        "ShowcaseReferencePorcelainMaterial",
        "ShowcaseReferenceGlossRedMaterial",
        "ShowcaseReferenceMatteRubberMaterial"
    };
    for (std::size_t index = 0; index < materialSamplePositions.size(); ++index) {
        const glm::vec3& samplePosition = materialSamplePositions[index];
        if (!showcaseSupportSurfacesOff) {
            se::Renderable3D& base = scene.CreateRenderable(
                "Showcase Material Sample Base " + std::to_string(index),
                "Cube",
                "ShowcaseCharcoalMaterial"
            );
            place(base, { samplePosition.x, -1.0f, samplePosition.z }, { 0.66f, 0.12f, 0.46f }, {}, -12);
            base.SetPickable(false);
            if (showcasePropShadowCastersOff) {
                base.SetCastShadow(false);
            }
        }

        se::Renderable3D& sample = scene.CreateRenderable(
            "Showcase Material Sample " + std::to_string(index),
            "Sphere",
            materialSampleMaterials[index]
        );
        place(sample, { samplePosition.x, -0.60f, samplePosition.z }, { 0.52f, 0.52f, 0.52f }, {}, -9);
        sample.SetPickable(false);
        if (showcaseSphereShadowCastersOff) {
            sample.SetCastShadow(false);
        }
    }

    auto createFixturePart = [&](
        const std::string& name,
        const char* meshName,
        const char* materialName,
        glm::vec3 position,
        glm::vec3 scale,
        glm::vec3 rotationDegrees,
        se::i32 drawOrder
    ) -> se::Renderable3D& {
        se::Renderable3D& part = scene.CreateRenderable(
            name,
            meshName,
            materialName
        );
        ConfigureLightGizmoRenderable(
            part,
            position,
            rotationDegrees,
            scale,
            drawOrder
        );
        return part;
    };

    auto createFramedRectLightFixture = [&](
        const std::string& name,
        glm::vec3 position,
        glm::vec3 direction,
        se::f32 width,
        se::f32 height,
        se::f32 radius,
        glm::vec3 color,
        se::f32 intensity,
        const char* materialName,
        se::i32 drawOrder,
        se::f32 visualWidth,
        se::f32 visualHeight,
        bool standingFixture,
        glm::vec3 labelPosition
    ) {
        direction = glm::normalize(direction);
        if (!showcaseLocalLightsOff) {
            const se::u32 localLightIndex = nextRectLightIndex++;
            scene.CreateRectLight(
                name,
                position,
                direction,
                width,
                height,
                radius,
                color,
                intensity,
                0.0f
            );
            if (showcaseLightIndexLabelsVisible && lightIndexOverlayScene != nullptr) {
                AddLightIndexLabel(
                    *lightIndexOverlayScene,
                    name,
                    localLightIndex,
                    labelPosition,
                    220 + static_cast<se::i32>(localLightIndex) * 3
                );
            }
        }

        const glm::vec3 rotationDegrees =
            RotationDegreesFromLocalPositiveYToDirection(direction);

        createFixturePart(
            name + " Fixture Housing",
            "Cube",
            "ShowcaseLampFixtureMaterial",
            position - direction * 0.045f,
            { visualWidth + 0.22f, 0.08f, visualHeight + 0.22f },
            rotationDegrees,
            drawOrder - 2
        );
        createFixturePart(
            name + " Diffuser",
            "Cube",
            materialName,
            position + direction * 0.018f,
            { visualWidth, 0.035f, visualHeight },
            rotationDegrees,
            drawOrder
        );

        if (standingFixture) {
            constexpr se::f32 kFloorY = -1.05f;
            const se::f32 poleHeight =
                std::max(position.y - kFloorY - 0.24f, 0.36f);
            const glm::vec3 polePosition{
                position.x - direction.x * 0.12f,
                kFloorY + poleHeight * 0.5f,
                position.z - direction.z * 0.12f
            };
            createFixturePart(
                name + " Fixture Stand",
                "Cube",
                "ShowcaseLampFixtureMaterial",
                polePosition,
                { 0.07f, poleHeight, 0.07f },
                {},
                drawOrder - 3
            );
            createFixturePart(
                name + " Fixture Base",
                "Cube",
                "ShowcaseLampFixtureMaterial",
                { polePosition.x, kFloorY + 0.035f, polePosition.z },
                { 0.58f, 0.07f, 0.44f },
                {},
                drawOrder - 4
            );
        }
    };

    auto createCeilingPointLightFixture = [&](
        const std::string& name,
        glm::vec3 position,
        se::f32 radius,
        glm::vec3 color,
        se::f32 intensity,
        const char* diffuserMaterial,
        se::i32 drawOrder,
        glm::vec3 labelPosition
    ) {
        if (!showcaseLocalLightsOff) {
            const se::u32 localLightIndex = nextPointLightIndex++;
            scene.CreatePointLight(name, position, radius, color, intensity);
            if (showcaseLightIndexLabelsVisible && lightIndexOverlayScene != nullptr) {
                AddLightIndexLabel(
                    *lightIndexOverlayScene,
                    name,
                    localLightIndex,
                    labelPosition,
                    220 + static_cast<se::i32>(localLightIndex) * 3
                );
            }
        }

        createFixturePart(
            name + " Fixture Housing",
            "Cube",
            "ShowcaseLampFixtureMaterial",
            position + glm::vec3{ 0.0f, 0.10f, 0.0f },
            { 0.46f, 0.10f, 0.46f },
            {},
            drawOrder - 1
        );
        createFixturePart(
            name + " Diffuser",
            "Sphere",
            diffuserMaterial,
            position,
            { 0.19f, 0.19f, 0.19f },
            {},
            drawOrder
        );
    };

    auto createCeilingSpotLightFixture = [&](
        const std::string& name,
        glm::vec3 position,
        glm::vec3 direction,
        se::f32 radius,
        glm::vec3 color,
        se::f32 intensity,
        se::f32 innerConeDegrees,
        se::f32 outerConeDegrees,
        const char* diffuserMaterial,
        se::i32 drawOrder,
        glm::vec3 labelPosition
    ) {
        direction = glm::normalize(direction);
        if (!showcaseLocalLightsOff) {
            const se::u32 localLightIndex = nextSpotLightIndex++;
            scene.CreateSpotLight(
                name,
                position,
                direction,
                radius,
                color,
                intensity,
                innerConeDegrees,
                outerConeDegrees
            );
            if (showcaseLightIndexLabelsVisible && lightIndexOverlayScene != nullptr) {
                AddLightIndexLabel(
                    *lightIndexOverlayScene,
                    name,
                    localLightIndex,
                    labelPosition,
                    220 + static_cast<se::i32>(localLightIndex) * 3
                );
            }
        }

        const glm::vec3 rotationDegrees =
            RotationDegreesFromLocalPositiveYToDirection(direction);
        createFixturePart(
            name + " Fixture Housing",
            "Cube",
            "ShowcaseLampFixtureMaterial",
            position - direction * 0.055f,
            { 0.42f, 0.14f, 0.42f },
            rotationDegrees,
            drawOrder - 1
        );
        createFixturePart(
            name + " Cone Diffuser",
            "Cone",
            diffuserMaterial,
            position + direction * 0.15f,
            { 0.25f, 0.32f, 0.25f },
            rotationDegrees,
            drawOrder
        );
    };

    const std::array<glm::vec3, 3> stripPositions{
        glm::vec3{ -2.75f, 1.45f, -2.18f },
        glm::vec3{ 0.0f, 1.72f, -2.18f },
        glm::vec3{ 2.75f, 1.45f, -2.18f }
    };
    for (std::size_t index = 0; index < stripPositions.size(); ++index) {
        const std::string suffix = std::to_string(index);
        const char* stripMaterialName =
            index == 1
                ? "ShowcaseEmissiveMaterial"
                : "ShowcaseCoolStripLightMaterial";
        const glm::vec3 lightColor =
            index == 1
                ? glm::vec3{ 1.0f, 0.82f, 0.55f }
                : glm::vec3{ 0.88f, 0.78f, 1.0f };
        const glm::vec3 practicalDirection =
            glm::normalize(glm::vec3{ 0.0f, -0.20f, 1.0f });
        const glm::vec3 wallWashDirection{ 0.0f, 0.0f, -1.0f };
        const glm::vec3 wallWashPosition =
            stripPositions[index] + glm::vec3{ 0.0f, 0.0f, -0.055f };
        const glm::vec3 wallWashRotation =
            RotationDegreesFromLocalPositiveYToDirection(wallWashDirection);

        createFixturePart(
            "Showcase Wall Wash Diffuser " + suffix,
            "Cube",
            stripMaterialName,
            wallWashPosition,
            { 0.86f, 0.028f, 1.92f },
            wallWashRotation,
            -8
        );
        createFixturePart(
            "Showcase Wall Wash Frame " + suffix,
            "Cube",
            "ShowcaseLampFixtureMaterial",
            wallWashPosition + glm::vec3{ 0.0f, 0.0f, -0.018f },
            { 1.02f, 0.035f, 2.08f },
            wallWashRotation,
            -9
        );

        se::Renderable3D& backplate = scene.CreateRenderable(
            "Showcase Wall Strip Backplate " + suffix,
            "Cube",
            "ShowcaseLampFixtureMaterial"
        );
        place(
            backplate,
            stripPositions[index] + glm::vec3{ 0.0f, 0.0f, -0.018f },
            { 0.27f, 1.76f, 0.06f },
            {},
            -6
        );
        backplate.SetCastShadow(false);

        se::Renderable3D& strip = scene.CreateRenderable(
            "Showcase Wall Diffuser Strip " + suffix,
            "Cube",
            stripMaterialName
        );
        place(
            strip,
            stripPositions[index] + glm::vec3{ 0.0f, 0.0f, 0.035f },
            { 0.08f, 1.38f, 0.04f },
            {},
            -4
        );
        strip.SetCastShadow(false);

        createFixturePart(
            "Showcase Wall Strip Top Cap " + suffix,
            "Cube",
            "ShowcaseLampFixtureMaterial",
            stripPositions[index] + glm::vec3{ 0.0f, 0.78f, 0.0f },
            { 0.28f, 0.06f, 0.08f },
            {},
            -5
        );
        createFixturePart(
            "Showcase Wall Strip Bottom Cap " + suffix,
            "Cube",
            "ShowcaseLampFixtureMaterial",
            stripPositions[index] + glm::vec3{ 0.0f, -0.78f, 0.0f },
            { 0.28f, 0.06f, 0.08f },
            {},
            -5
        );

        if (!showcaseLocalLightsOff) {
            const se::u32 practicalLightIndex = nextRectLightIndex++;
            scene.CreateRectLight(
                "Showcase Practical Rect Light " + suffix,
                stripPositions[index] + glm::vec3{ 0.0f, 0.0f, 0.14f },
                practicalDirection,
                0.95f,
                2.05f,
                6.8f,
                lightColor,
                index == 1 ? 8.8f : 7.4f,
                0.0f
            );
            if (showcaseLightIndexLabelsVisible && lightIndexOverlayScene != nullptr) {
                AddLightIndexLabel(
                    *lightIndexOverlayScene,
                    "Showcase Practical Rect Light " + suffix,
                    practicalLightIndex,
                    stripPositions[index] + glm::vec3{ 0.32f, 0.93f, 0.24f },
                    200 + static_cast<se::i32>(practicalLightIndex) * 3
                );
            }

            const se::u32 wallWashLightIndex = nextRectLightIndex++;
            scene.CreateRectLight(
                "Showcase Practical Wall Wash Light " + suffix,
                wallWashPosition,
                wallWashDirection,
                0.86f,
                2.05f,
                2.45f,
                lightColor,
                index == 1 ? 2.8f : 2.35f,
                0.0f
            );
            if (showcaseLightIndexLabelsVisible && lightIndexOverlayScene != nullptr) {
                AddLightIndexLabel(
                    *lightIndexOverlayScene,
                    "Showcase Practical Wall Wash Light " + suffix,
                    wallWashLightIndex,
                    stripPositions[index] + glm::vec3{ -0.32f, -0.93f, 0.24f },
                    200 + static_cast<se::i32>(wallWashLightIndex) * 3
                );
            }
        }
    }

    const glm::vec3 sunDirection =
        glm::normalize(glm::vec3{ -0.42f, -0.78f, -0.46f });
    scene.SetPrimaryDirectionalLight(
        "Showcase Low Sun Directional",
        sunDirection,
        0.86f,
        0.62f,
        0.52f
    );

    createFramedRectLightFixture(
        "Showcase Warm Key Area",
        { -3.88f, 1.38f, 0.84f },
        glm::normalize(glm::vec3{ 1.0f, -0.22f, -0.20f }),
        3.2f,
        2.0f,
        7.6f,
        { 1.0f, 0.76f, 0.48f },
        4.1f,
        "ShowcaseWarmKeyLightMaterial",
        20,
        0.96f,
        1.18f,
        false,
        { -3.34f, 1.92f, 1.12f }
    );
    createFramedRectLightFixture(
        "Showcase Cool Rim Rect",
        { 3.88f, 1.38f, 0.84f },
        glm::normalize(glm::vec3{ -1.0f, -0.20f, -0.22f }),
        2.8f,
        1.55f,
        7.0f,
        { 0.48f, 0.68f, 1.0f },
        4.8f,
        "ShowcaseCoolRimLightMaterial",
        21,
        0.92f,
        1.08f,
        false,
        { 3.34f, 1.92f, 1.12f }
    );
    createCeilingPointLightFixture(
        "Showcase Ceiling Warm Point",
        { 0.0f, 2.70f, 1.58f },
        5.2f,
        { 1.0f, 0.78f, 0.56f },
        1.18f,
        "ShowcaseWarmKeyLightMaterial",
        22,
        { 0.0f, 2.34f, 1.58f }
    );
    createCeilingSpotLightFixture(
        "Showcase Ceiling Warm Spot",
        { -2.28f, 2.72f, 0.58f },
        glm::normalize(glm::vec3{ 0.42f, -0.84f, -0.34f }),
        6.2f,
        { 1.0f, 0.64f, 0.38f },
        2.75f,
        15.0f,
        30.0f,
        "ShowcaseWarmKeyLightMaterial",
        23,
        { -2.28f, 2.30f, 0.58f }
    );
    createCeilingSpotLightFixture(
        "Showcase Ceiling Cool Spot",
        { 2.28f, 2.72f, 0.58f },
        glm::normalize(glm::vec3{ -0.42f, -0.84f, -0.34f }),
        6.2f,
        { 0.42f, 0.66f, 1.0f },
        2.85f,
        15.0f,
        30.0f,
        "ShowcaseCoolRimLightMaterial",
        24,
        { 2.28f, 2.30f, 0.58f }
    );
    if (showcaseOverheadFillOn) {
        createFramedRectLightFixture(
            "Showcase Soft Overhead Fill",
            { 0.0f, 2.84f, -0.10f },
            glm::normalize(glm::vec3{ 0.0f, -1.0f, -0.02f }),
            5.0f,
            2.2f,
            8.0f,
            { 0.70f, 0.80f, 1.0f },
            2.45f,
            "ShowcaseSoftFillLightMaterial",
            22,
            2.85f,
            0.46f,
            false,
            { 0.0f, 2.48f, 1.04f }
        );
    }
    if (showcaseFrontBounceLightsOn) {
        createFramedRectLightFixture(
            "Showcase Low Warm Bounce",
            { -2.82f, -0.88f, 2.18f },
            glm::normalize(glm::vec3{ 0.38f, 0.64f, -0.66f }),
            1.55f,
            0.75f,
            4.6f,
            { 1.0f, 0.36f, 0.16f },
            1.45f,
            "ShowcaseLowWarmLightMaterial",
            23,
            0.62f,
            0.24f,
            false,
            { -2.42f, -0.50f, 2.56f }
        );
        createFramedRectLightFixture(
            "Showcase Low Cool Bounce",
            { 2.82f, -0.88f, 2.18f },
            glm::normalize(glm::vec3{ -0.38f, 0.64f, -0.66f }),
            1.55f,
            0.75f,
            4.8f,
            { 0.24f, 0.56f, 1.0f },
            1.30f,
            "ShowcaseLowCoolLightMaterial",
            24,
            0.62f,
            0.24f,
            false,
            { 2.42f, -0.50f, 2.56f }
        );
    }
    if (!showcaseReflectionProbesOff) {
        const std::string showcaseReflectionProbeAssetId =
            LightingShowcaseReflectionProbeAssetId();
        const se::ReflectionProbeCaptureSource showcaseReflectionProbeCaptureSource =
            !showcaseCapturedReflectionProbesOff &&
                    !showcaseAuthoredReflectionProbesOn
                ? se::ReflectionProbeCaptureSource::CapturedScene
            : showcaseReflectionProbeAssetId.empty()
                ? se::ReflectionProbeCaptureSource::BuiltInProcedural
                : se::ReflectionProbeCaptureSource::AuthoredCubemap;
        const se::ReflectionProbeRefreshPolicy showcaseReflectionProbeRefreshPolicy =
            DefaultReflectionProbeRefreshPolicy(showcaseReflectionProbeCaptureSource);
        const std::string showcaseReflectionProbeCaptureAssetId =
            showcaseReflectionProbeCaptureSource ==
                    se::ReflectionProbeCaptureSource::AuthoredCubemap
                ? showcaseReflectionProbeAssetId
                : std::string{};
        scene.CreateReflectionProbe(
            "Showcase Center Warm Probe",
            { -1.15f, 0.45f, 0.15f },
            4.4f,
            { 3.5f, 2.5f, 3.2f },
            { 1.0f, 0.72f, 0.48f },
            1.18f,
            0.58f,
            2.15f,
            showcaseReflectionProbeCaptureSource,
            showcaseReflectionProbeCaptureAssetId,
            showcaseReflectionProbeRefreshPolicy
        );
        scene.CreateReflectionProbe(
            "Showcase Cool Rim Probe",
            { 2.1f, 0.55f, 0.1f },
            4.0f,
            { 3.0f, 2.4f, 3.0f },
            { 0.48f, 0.66f, 1.0f },
            1.08f,
            0.52f,
            2.0f,
            showcaseReflectionProbeCaptureSource,
            showcaseReflectionProbeCaptureAssetId,
            showcaseReflectionProbeRefreshPolicy
        );
        scene.CreateReflectionProbe(
            "Showcase Dark Ground Probe",
            { 0.0f, -0.05f, 0.35f },
            5.2f,
            { 5.4f, 2.1f, 4.2f },
            { 0.66f, 0.68f, 0.70f },
            0.92f,
            0.48f,
            1.75f,
            showcaseReflectionProbeCaptureSource,
            showcaseReflectionProbeCaptureAssetId,
            showcaseReflectionProbeRefreshPolicy
        );
    }

    std::cout << "Lighting showcase enabled: deferred HDR lighting scene"
        << std::endl;
}

void BuildPbrModelShowcaseEnvironment(se::Scene3D& scene) {
    auto place = [](
        se::Renderable3D& renderable,
        glm::vec3 position,
        glm::vec3 scale,
        glm::vec3 rotationDegrees = glm::vec3(0.0f),
        se::i32 drawOrder = 0
    ) {
        renderable.Transform().SetPosition(position);
        renderable.Transform().SetScale(scale);
        renderable.Transform().SetRotationDegrees(rotationDegrees);
        renderable.Transform().SetAnimateRotation(false);
        renderable.SetDrawOrder(drawOrder);
        renderable.SetPickable(false);
    };

    se::Renderable3D& floor = scene.CreateRenderable(
        "PBR Model Studio Floor",
        "Plane",
        "ShowcaseCharcoalMaterial"
    );
    place(floor, { 0.0f, -1.08f, 0.0f }, { 6.4f, 1.0f, 5.2f }, {}, -30);
    floor.SetCastShadow(false);

    se::Renderable3D& backdrop = scene.CreateRenderable(
        "PBR Model Studio Backdrop",
        "Cube",
        "ShowcaseBackdropMaterial"
    );
    place(backdrop, { 0.0f, 1.0f, -2.45f }, { 6.4f, 4.2f, 0.12f }, {}, -29);
    backdrop.SetCastShadow(false);

    scene.SetPrimaryDirectionalLight(
        "PBR Model Neutral Directional",
        glm::normalize(glm::vec3{ -0.42f, -0.78f, -0.46f }),
        0.72f,
        0.14f,
        0.30f,
        glm::radians(0.45f)
    );
    scene.CreateRectLight(
        "PBR Model Soft Key",
        { -2.5f, 2.35f, 2.0f },
        glm::normalize(glm::vec3{ 2.5f, -2.15f, -2.0f }),
        1.8f,
        1.1f,
        5.6f,
        { 1.0f, 0.91f, 0.80f },
        5.2f,
        1.0f
    );
    scene.CreateRectLight(
        "PBR Model Cool Fill",
        { 2.4f, 1.25f, 1.65f },
        glm::normalize(glm::vec3{ -2.4f, -1.05f, -1.65f }),
        1.5f,
        0.9f,
        5.0f,
        { 0.62f, 0.78f, 1.0f },
        2.8f,
        0.75f
    );
    scene.CreateSpotLight(
        "PBR Model Rim Spot",
        { 0.4f, 2.1f, -1.75f },
        glm::normalize(glm::vec3{ -0.4f, -1.65f, 1.75f }),
        4.6f,
        { 0.78f, 0.88f, 1.0f },
        3.4f,
        18.0f,
        34.0f,
        0.12f
    );
    scene.CreateReflectionProbe(
        "PBR Model Studio Probe",
        { 0.0f, 0.15f, 0.0f },
        4.6f,
        { 3.8f, 2.5f, 3.2f },
        { 0.92f, 0.94f, 1.0f },
        1.0f,
        0.58f,
        2.0f
    );

    std::cout << "PBR model showcase environment enabled" << std::endl;
}

void BuildShadowRegressionScene(
    se::Scene3D& scene,
    se::Scene3D* lightGizmoOverlayScene = nullptr
) {
    const bool rectLightOnly =
        EnvironmentFlagEnabled("SE_SHADOW_REGRESSION_RECT_LIGHT_ONLY");
    const bool lightGizmosEnabled =
        !EnvironmentFlagEnabled("SE_SHADOW_REGRESSION_LIGHT_GIZMOS_OFF");
    auto place = [](
        se::Renderable3D& renderable,
        glm::vec3 position,
        glm::vec3 scale,
        glm::vec3 rotationDegrees = glm::vec3(0.0f),
        se::i32 drawOrder = 0
    ) {
        renderable.Transform().SetPosition(position);
        renderable.Transform().SetScale(scale);
        renderable.Transform().SetRotationDegrees(rotationDegrees);
        renderable.Transform().SetAnimateRotation(false);
        renderable.SetDrawOrder(drawOrder);
    };

    se::Renderable3D& ground = scene.CreateRenderable(
        "Shadow Regression Ground",
        "Plane",
        "DefaultGroundMaterial"
    );
    place(ground, { 0.0f, -1.1f, 0.0f }, { 9.0f, 1.0f, 8.0f }, {}, -40);
    ground.SetPickable(false);

    se::Renderable3D& backWall = scene.CreateRenderable(
        "Shadow Regression Back Wall",
        "Cube",
        "GroundMaterial"
    );
    place(backWall, { 0.0f, 1.0f, -3.25f }, { 9.0f, 3.8f, 0.16f }, {}, -35);
    backWall.SetPickable(false);
    backWall.SetCastShadow(false);

    se::Renderable3D& lowStep = scene.CreateRenderable(
        "Shadow Regression Low Step",
        "Cube",
        "WarmCubeMaterial"
    );
    place(lowStep, { 0.2f, -0.86f, -0.8f }, { 2.2f, 0.42f, 1.2f }, { 0.0f, 8.0f, 0.0f }, -15);

    se::Renderable3D& tallCaster = scene.CreateRenderable(
        "Shadow Regression Tall Caster",
        "Cube",
        "BlueCubeMaterial"
    );
    place(tallCaster, { 2.1f, -0.15f, 0.05f }, { 0.68f, 1.9f, 0.68f }, { 0.0f, -24.0f, 0.0f });

    se::Renderable3D& nearCaster = scene.CreateRenderable(
        "Shadow Regression Near Caster",
        "Cube",
        "GreenCubeMaterial"
    );
    place(nearCaster, { -2.2f, -0.42f, 0.95f }, { 0.82f, 1.05f, 0.82f }, { 0.0f, 18.0f, 0.0f });

    se::Renderable3D& roundCaster = scene.CreateRenderable(
        "Shadow Regression Round Caster",
        "Sphere",
        "ShowcaseCoolCeramicMaterial"
    );
    place(roundCaster, { 1.15f, -0.36f, 1.35f }, { 0.74f, 0.74f, 0.74f });

    se::Renderable3D& offscreenCasterA = scene.CreateRenderable(
        "Shadow Regression Offscreen Caster A",
        "Cube",
        "BlueCubeMaterial"
    );
    place(
        offscreenCasterA,
        { 6.7f, -0.24f, -0.15f },
        { 0.82f, 1.72f, 0.82f },
        { 0.0f, -18.0f, 0.0f }
    );

    se::Renderable3D& offscreenCasterB = scene.CreateRenderable(
        "Shadow Regression Offscreen Caster B",
        "Cube",
        "GreenCubeMaterial"
    );
    place(
        offscreenCasterB,
        { -6.65f, -0.32f, 0.3f },
        { 0.76f, 1.55f, 0.76f },
        { 0.0f, 22.0f, 0.0f }
    );

    scene.SetPrimaryDirectionalLight(
        "Shadow Regression Directional Light",
        glm::normalize(glm::vec3{ -0.36f, -0.82f, -0.43f }),
        rectLightOnly ? 0.16f : 0.92f,
        rectLightOnly ? 0.08f : 0.18f,
        rectLightOnly ? 0.08f : 0.28f
    );

    const glm::vec3 pointLightPosition{ -2.65f, 1.25f, 1.25f };
    const glm::vec3 pointLightColor{ 1.0f, 0.58f, 0.34f };
    const glm::vec3 spotLightPosition{ 2.9f, 3.0f, 2.6f };
    const glm::vec3 spotLightDirection =
        glm::normalize(glm::vec3{ -0.55f, -0.75f, -0.38f });
    const glm::vec3 spotLightColor{ 0.62f, 0.76f, 1.0f };
    const glm::vec3 rectLightPosition{ 0.0f, 2.6f, 2.4f };
    const glm::vec3 rectLightDirection =
        glm::normalize(glm::vec3{ 0.0f, -0.72f, -0.70f });
    const glm::vec3 rectLightColor{ 1.0f, 0.88f, 0.64f };
    constexpr se::f32 kRectLightWidth = 3.6f;
    constexpr se::f32 kRectLightHeight = 1.4f;
    if (!EnvironmentFlagEnabled("SE_SHADOW_REGRESSION_LOCAL_LIGHTS_OFF")) {
        if (!rectLightOnly) {
            scene.CreatePointLight(
                "Shadow Regression Warm Local Light",
                pointLightPosition,
                5.2f,
                pointLightColor,
                4.2f
            );
            scene.CreateSpotLight(
                "Shadow Regression Cool Spot Light",
                spotLightPosition,
                spotLightDirection,
                7.2f,
                spotLightColor,
                3.4f,
                15.0f,
                30.0f
            );
        }
        scene.CreateRectLight(
            "Shadow Regression Soft Rect Light",
            rectLightPosition,
            rectLightDirection,
            kRectLightWidth,
            kRectLightHeight,
            6.8f,
            rectLightColor,
            rectLightOnly ? 4.4f : 2.6f
        );

        if (lightGizmosEnabled) {
            se::Scene3D& lightGizmoScene =
                lightGizmoOverlayScene != nullptr ? *lightGizmoOverlayScene : scene;
            if (!rectLightOnly) {
                se::Renderable3D& pointGizmo = lightGizmoScene.CreateRenderable(
                    "Shadow Regression Point Light Gizmo",
                    "Sphere",
                    "PointLightGizmoMaterial"
                );
                ConfigureLightGizmoRenderable(
                    pointGizmo,
                    pointLightPosition,
                    {},
                    { 0.34f, 0.34f, 0.34f },
                    90
                );

                se::Renderable3D& spotGizmo = lightGizmoScene.CreateRenderable(
                    "Shadow Regression Spot Light Gizmo",
                    "Cone",
                    "SpotLightGizmoMaterial"
                );
                ConfigureLightGizmoRenderable(
                    spotGizmo,
                    spotLightPosition,
                    RotationDegreesFromLocalPositiveYToDirection(spotLightDirection),
                    { 1.32f, 1.15f, 1.32f },
                    91
                );
            }

            se::Renderable3D& rectGizmo = lightGizmoScene.CreateRenderable(
                "Shadow Regression Rect Light Gizmo",
                "Plane",
                "RectLightGizmoMaterial"
            );
            ConfigureLightGizmoRenderable(
                rectGizmo,
                rectLightPosition,
                RotationDegreesFromLocalPositiveYToDirection(rectLightDirection),
                { kRectLightWidth, 1.0f, kRectLightHeight },
                92
            );
        }
    }
    scene.CreateReflectionProbe(
        "Shadow Regression Reflection Probe",
        { 0.0f, 0.75f, 0.0f },
        6.0f,
        { 4.8f, 3.2f, 4.8f },
        { 0.85f, 0.76f, 0.66f },
        0.9f,
        0.42f,
        1.8f
    );

    std::cout << "Shadow regression scene enabled: fixed camera, animated FBX, CSM/local/contact coverage"
        << std::endl;
}

std::filesystem::path DefaultSceneSkinnedFbxPath() {
    return std::filesystem::path(SE_ASSET_DIR) / "models" / "Fist Fight B.fbx";
}

void ApplyDefaultSceneFbxMaterialFallbacks(se::RuntimeModelLoader& loader) {
    loader.ForEachMaterial([](se::VulkanMaterial& material) {
        se::MaterialProperties& properties = material.Properties();
        if (properties.textureMix <= 0.001f) {
            properties.baseColorFactor = { 0.90f, 0.78f, 0.62f, 1.0f };
            properties.textureMix = 0.0f;
            properties.pbrFactors = { 1.0f, 0.42f, 0.0f, 0.0f };
            properties.emissiveFactor = { 0.16f, 0.11f, 0.06f, 1.0f };
        }
        properties.viewControls[1] = std::max(properties.viewControls[1], 1.28f);
    });
}

se::RuntimeModelLoadResult LoadDefaultSceneSkinnedFbx(
    se::RuntimeModelLoader& loader,
    glm::vec3 position,
    glm::vec3 rotationDegrees,
    se::f32 targetMaxExtent,
    glm::vec3 scale,
    bool bindSkinning,
    const char* label
) {
    se::RuntimeModelLoadResult result{};
    const std::filesystem::path modelPath = DefaultSceneSkinnedFbxPath();
    if (!std::filesystem::exists(modelPath)) {
        result.message = "Default-scene FBX model not found";
        std::cout << "[warning] " << result.message << ": "
            << se::UnrealPathToUtf8(modelPath)
            << std::endl;
        return result;
    }

    std::cout << "Loading " << label << " FBX model: "
        << se::UnrealPathToUtf8(modelPath)
        << (bindSkinning ? " [skinned production audit]" : " [rigid preview]")
        << std::endl;
    result = loader.LoadIntoScene(
        modelPath,
        position,
        rotationDegrees,
        targetMaxExtent,
        scale,
        bindSkinning
    );
    if (result.loaded) {
        std::cout << label << " FBX model loaded: "
            << result.message
            << std::endl;
        ApplyDefaultSceneFbxMaterialFallbacks(loader);
    } else {
        std::cout << "[warning] " << label << " FBX model load failed: "
            << result.message
            << std::endl;
    }

    return result;
}

void ApplyLightingShowcaseRendererSettings(se::VulkanRenderer& renderer) {
    se::VulkanShadowSettings& shadow = renderer.ShadowSettings();
    se::ApplyShadowQualityPreset(shadow, se::VulkanShadowQuality::Ultra);
    shadow.ambientStrength = 0.10f;
    shadow.strength = 1.0f;
    shadow.pcssStrength = 1.0f;
    shadow.localPcssStrength = 0.24f;
    shadow.contactShadowStrength = 0.0f;
    shadow.contactShadowLength = 0.0f;
    shadow.contactShadowThickness = 0.0f;
    shadow.contactShadowSteps = 0u;
    shadow.ssaoStrength = 0.12f;
    shadow.ssaoRadius = 1.55f;
    shadow.ssaoBias = 0.022f;
    shadow.ssaoSampleCount = 16u;
    shadow.reflectionProbeFallbackEnabled = true;
    shadow.reflectionProbeCubemapEnabled = true;
    shadow.reflectionProbeDiffuseIntensity = 1.32f;
    shadow.reflectionProbeSpecularIntensity = 0.16f;
    shadow.reflectionProbeHorizonBlend = 0.42f;
    shadow.skyboxEnabled = true;
    shadow.skyboxIntensity = 1.0f;
    shadow.skyboxBlur = 0.0f;
    shadow.heightFogEnabled = false;
    shadow.heightFogDensity = 0.0f;
    shadow.heightFogHeightFalloff = 0.16f;
    shadow.heightFogStartDistance = 4.2f;
    shadow.heightFogMaxOpacity = 0.0f;
    shadow.heightFogColorR = 0.50f;
    shadow.heightFogColorG = 0.58f;
    shadow.heightFogColorB = 0.68f;

    se::VulkanRenderDebugSettings& debug = renderer.RenderDebugSettings();
    debug.forwardView = se::ForwardDebugView::DeferredHdr;
    debug.exposure = 1.16f;
    debug.toneMapMode = 0u;
    debug.toneMapWhitePoint = 5.0f;
    debug.autoExposureEnabled = false;
    debug.bloomEnabled = true;
    debug.bloomIntensity = 0.08f;
    debug.bloomThreshold = 1.35f;
    debug.bloomRadiusPixels = 4.5f;
    debug.colorGradingEnabled = true;
    debug.colorGradingSaturation = 1.06f;
    debug.colorGradingContrast = 1.08f;
    debug.colorGradingGamma = 1.0f;
    debug.colorGradingLutStrength = 1.0f;
    debug.sharpeningEnabled = false;
    debug.sharpeningStrength = 0.0f;
    debug.sharpeningRadiusPixels = 0.85f;
}

void ApplyForward3DRendererSettings(se::VulkanRenderer& renderer) {
    se::VulkanShadowSettings& shadow = renderer.ShadowSettings();
    if (Forward3DProductionShadowProfileRequested()) {
        se::ApplyForward3DProductionShadowPreset(shadow);
    } else {
        se::ApplyShadowQualityPreset(shadow, se::VulkanShadowQuality::Ultra);
    }
}

bool ApplyEnvironmentF32Override(
    se::f32& target,
    const char* name,
    se::f32 minValue,
    se::f32 maxValue
) {
    if (ReadEnvironmentString(name).empty()) {
        return false;
    }

    target = std::clamp(ReadEnvironmentF32(name, target), minValue, maxValue);
    return true;
}

bool ApplyEnvironmentU32Override(
    se::u32& target,
    const char* name,
    se::u32 minValue,
    se::u32 maxValue
) {
    if (ReadEnvironmentString(name).empty()) {
        return false;
    }

    target = std::clamp(
        static_cast<se::u32>(ReadEnvironmentF32(name, static_cast<se::f32>(target)) + 0.5f),
        minValue,
        maxValue
    );
    return true;
}

void ApplyLocalShadowFilterEnvironmentOverrides(
    se::VulkanLocalShadowFilterSettings& filter,
    const char* biasMinName,
    const char* biasSlopeName,
    const char* pcfRadiusName,
    const char* pcfKernelRadiusName,
    const char* pcssStrengthName
) {
    (void)ApplyEnvironmentF32Override(filter.biasMin, biasMinName, 0.0f, 0.02f);
    (void)ApplyEnvironmentF32Override(filter.biasSlope, biasSlopeName, 0.0f, 0.05f);
    (void)ApplyEnvironmentF32Override(filter.pcfRadius, pcfRadiusName, 0.0f, 4.0f);
    (void)ApplyEnvironmentU32Override(filter.pcfKernelRadius, pcfKernelRadiusName, 0u, 2u);
    (void)ApplyEnvironmentF32Override(filter.pcssStrength, pcssStrengthName, 0.0f, 1.0f);
}

void ApplyForward3DLocalShadowEnvironmentOverrides(se::VulkanShadowSettings& shadow) {
    bool sharedFilterOverridden = false;
    sharedFilterOverridden |= ApplyEnvironmentF32Override(
        shadow.localBiasMin,
        "SE_LOCAL_SHADOW_BIAS_MIN",
        0.0f,
        0.02f
    );
    sharedFilterOverridden |= ApplyEnvironmentF32Override(
        shadow.localBiasSlope,
        "SE_LOCAL_SHADOW_BIAS_SLOPE",
        0.0f,
        0.05f
    );
    sharedFilterOverridden |= ApplyEnvironmentF32Override(
        shadow.localPcfRadius,
        "SE_LOCAL_SHADOW_PCF_RADIUS",
        0.0f,
        4.0f
    );
    sharedFilterOverridden |= ApplyEnvironmentU32Override(
        shadow.localPcfKernelRadius,
        "SE_LOCAL_SHADOW_PCF_KERNEL_RADIUS",
        0u,
        2u
    );
    sharedFilterOverridden |= ApplyEnvironmentF32Override(
        shadow.localPcssStrength,
        "SE_LOCAL_SHADOW_PCSS_STRENGTH",
        0.0f,
        1.0f
    );
    if (sharedFilterOverridden) {
        se::SyncLocalShadowKindFiltersToShared(shadow);
    }

    ApplyLocalShadowFilterEnvironmentOverrides(
        shadow.pointLocalShadowFilter,
        "SE_LOCAL_SHADOW_POINT_BIAS_MIN",
        "SE_LOCAL_SHADOW_POINT_BIAS_SLOPE",
        "SE_LOCAL_SHADOW_POINT_PCF_RADIUS",
        "SE_LOCAL_SHADOW_POINT_PCF_KERNEL_RADIUS",
        "SE_LOCAL_SHADOW_POINT_PCSS_STRENGTH"
    );
    ApplyLocalShadowFilterEnvironmentOverrides(
        shadow.spotLocalShadowFilter,
        "SE_LOCAL_SHADOW_SPOT_BIAS_MIN",
        "SE_LOCAL_SHADOW_SPOT_BIAS_SLOPE",
        "SE_LOCAL_SHADOW_SPOT_PCF_RADIUS",
        "SE_LOCAL_SHADOW_SPOT_PCF_KERNEL_RADIUS",
        "SE_LOCAL_SHADOW_SPOT_PCSS_STRENGTH"
    );
    ApplyLocalShadowFilterEnvironmentOverrides(
        shadow.rectLocalShadowFilter,
        "SE_LOCAL_SHADOW_RECT_BIAS_MIN",
        "SE_LOCAL_SHADOW_RECT_BIAS_SLOPE",
        "SE_LOCAL_SHADOW_RECT_PCF_RADIUS",
        "SE_LOCAL_SHADOW_RECT_PCF_KERNEL_RADIUS",
        "SE_LOCAL_SHADOW_RECT_PCSS_STRENGTH"
    );

    (void)ApplyEnvironmentF32Override(
        shadow.localFaceBlendStrength,
        "SE_LOCAL_SHADOW_FACE_BLEND",
        0.0f,
        1.0f
    );
    if (!ReadEnvironmentString("SE_RECT_SHADOW_BIAS_SCALE").empty()) {
        (void)ApplyEnvironmentF32Override(
            shadow.rectLightShadowBiasScale,
            "SE_RECT_SHADOW_BIAS_SCALE",
            0.0f,
            32.0f
        );
    } else {
        (void)ApplyEnvironmentF32Override(
            shadow.rectLightShadowBiasScale,
            "SE_LOCAL_SHADOW_RECT_BIAS_SCALE",
            0.0f,
            32.0f
        );
    }
    if (ApplyEnvironmentU32Override(
            shadow.rectLightShadowSampleTiles,
            "SE_LOCAL_SHADOW_RECT_SAMPLE_TILES",
            2u,
            4u
        )) {
        shadow.rectLightShadowSampleTiles =
            shadow.rectLightShadowSampleTiles >= 4u ? 4u : 2u;
    }
}

void ApplyForward3DDirectionalShadowEnvironmentOverrides(
    se::VulkanShadowSettings& shadow
) {
    (void)ApplyEnvironmentF32Override(
        shadow.pcssStrength,
        "SE_SHADOW_PCSS_STRENGTH",
        0.0f,
        1.0f
    );
    if (EnvironmentFlagEnabled("SE_DIRECTIONAL_PCSS_OFF")) {
        shadow.pcssStrength = 0.0f;
    }
    (void)ApplyEnvironmentU32Override(
        shadow.directionalPcssBlockerSampleCount,
        "SE_DIRECTIONAL_PCSS_BLOCKER_SAMPLES",
        0u,
        16u
    );
    (void)ApplyEnvironmentU32Override(
        shadow.directionalPcssFilterSampleCount,
        "SE_DIRECTIONAL_PCSS_FILTER_SAMPLES",
        0u,
        16u
    );
    (void)ApplyEnvironmentF32Override(
        shadow.directionalPcssSearchRadiusTexels,
        "SE_DIRECTIONAL_PCSS_SEARCH_RADIUS_TEXELS",
        0.0f,
        16.0f
    );
    (void)ApplyEnvironmentF32Override(
        shadow.directionalPcssMaxPenumbraTexels,
        "SE_DIRECTIONAL_PCSS_MAX_PENUMBRA_TEXELS",
        0.0f,
        16.0f
    );
    (void)ApplyEnvironmentF32Override(
        shadow.directionalPcssGrazingFadeStart,
        "SE_DIRECTIONAL_PCSS_GRAZING_FADE_START",
        0.0f,
        0.95f
    );
    (void)ApplyEnvironmentF32Override(
        shadow.directionalPcssGrazingFadeEnd,
        "SE_DIRECTIONAL_PCSS_GRAZING_FADE_END",
        0.01f,
        1.0f
    );
    shadow.directionalPcssGrazingFadeEnd = std::max(
        shadow.directionalPcssGrazingFadeEnd,
        std::min(shadow.directionalPcssGrazingFadeStart + 0.01f, 1.0f)
    );
    if (EnvironmentFlagEnabled("SE_DIRECTIONAL_PCSS_GRAZING_FADE_OFF")) {
        shadow.directionalPcssGrazingFadeEnabled = false;
    }

    se::u32 filterMode = static_cast<se::u32>(shadow.directionalFilterMode);
    if (ApplyEnvironmentU32Override(
            filterMode,
            "SE_DIRECTIONAL_SHADOW_FILTER_MODE",
            0u,
            1u
        )) {
        shadow.directionalFilterMode = static_cast<se::VulkanDirectionalShadowFilterMode>(
            filterMode
        );
    }
    shadow.directionalFilterSampleCount = 9u;
    (void)ApplyEnvironmentU32Override(
        shadow.directionalFilterKernelWidth,
        "SE_DIRECTIONAL_SHADOW_FILTER_KERNEL_WIDTH",
        3u,
        5u
    );
    shadow.directionalFilterKernelWidth =
        shadow.directionalFilterKernelWidth >= 5u ? 5u : 3u;
    (void)ApplyEnvironmentF32Override(
        shadow.directionalFilterReceiverBiasExtentTexels,
        "SE_DIRECTIONAL_SHADOW_FILTER_RECEIVER_BIAS_EXTENT_TEXELS",
        0.0f,
        4.0f
    );
}

void ApplyForward3DEnvironmentShadowDefaults(se::VulkanRenderer& renderer) {
    se::VulkanShadowSettings& shadow = renderer.ShadowSettings();
    if (Forward3DProductionShadowProfileRequested()) {
        se::ApplyForward3DShadowProductionOverrides(shadow);
    }
    ApplyForward3DLocalShadowEnvironmentOverrides(shadow);
    ApplyForward3DDirectionalShadowEnvironmentOverrides(shadow);

    if (!ReadEnvironmentString("SE_SHADOW_CASCADE_MAX_DISTANCE").empty()) {
        shadow.cascadeMaxDistance = std::clamp(
            ReadEnvironmentF32(
                "SE_SHADOW_CASCADE_MAX_DISTANCE",
                se::kForward3DShadowCascadeMaxDistance
            ),
            10.0f,
            2000.0f
        );
    }

    std::cout << "Forward 3D shadow profile: "
        << (Forward3DProductionShadowProfileRequested()
            ? "production"
            : "generic")
        << ", CSM max distance=" << shadow.cascadeMaxDistance
        << "m"
        << std::endl;
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
                outerCone,
                PositiveOrFallback(light.sourceRadius, 0.05f)
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
                PreviewLocalIntensity(light.intensity),
                PositiveOrFallback(light.sourceRadius, 0.05f)
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
    constexpr bool kForceLightingShowcase =
        SE_FORCE_LIGHTING_SHOWCASE != 0;
    constexpr bool kForcePbrModelShowcase =
        SE_FORCE_PBR_MODEL_SHOWCASE != 0;
    if constexpr (kForceLightingShowcase || kForcePbrModelShowcase) {
        SetEnvironmentDefault("SE_HIDE_IMGUI", "1");
    }
#if !SE_FORCE_LIGHTING_SHOWCASE
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
#endif

    const std::string vertexShaderPath = std::string(SE_SHADER_DIR) + "/forward_3d.vert.spv";
    const std::string fragmentShaderPath = std::string(SE_SHADER_DIR) + "/forward_3d.frag.spv";
    const std::string checkerTexturePath = std::string(SE_ASSET_DIR) + "/textures/checker.ppm";
    const StartupBridgeScene startupBridgeScene =
        kForceLightingShowcase || kForcePbrModelShowcase
            ? StartupBridgeScene{}
            : ResolveStartupBridgeScene();
    PrintStartupBridgeScene(startupBridgeScene);
    const std::filesystem::path importedModelPath =
        kForceLightingShowcase
            ? std::filesystem::path{}
            : (kForcePbrModelShowcase ? PbrModelShowcasePath() : ImportedModelPath());
    const std::filesystem::path bridgeModelPath =
        startupBridgeScene.exportedSceneReady ? startupBridgeScene.exportedScenePath : std::filesystem::path{};
    const std::filesystem::path startupModelPath =
        !importedModelPath.empty() ? importedModelPath : bridgeModelPath;
    const bool hasStartupModel = !startupModelPath.empty();
    const bool lightingShowcaseSceneRequested =
        !hasStartupModel && LightingShowcaseSceneRequested();
    const bool shadowRegressionSceneRequested =
        !hasStartupModel && ShadowRegressionSceneRequested();
    const bool useBenchmarkScene =
        !hasStartupModel &&
        (
            BenchmarkGridSceneRequested() ||
            lightingShowcaseSceneRequested ||
            shadowRegressionSceneRequested
        );
    const bool previewStartupModelLighting =
        !importedModelPath.empty() && !UsesReferenceModelLighting(importedModelPath);
    const bool animateBenchmarkPartialLocalShadowCache =
        useBenchmarkScene && BenchmarkPartialLocalShadowCacheRequested();
#if !defined(NDEBUG)
    const bool reflectionCaptureCameraInvariantControl =
        EnvironmentFlagEnabled("SE_REFLECTION_CAPTURE_CAMERA_INVARIANT_CONTROL");
#else
    constexpr bool reflectionCaptureCameraInvariantControl = false;
#endif
    const bool showcaseCameraControlsEnabled =
        lightingShowcaseSceneRequested ||
        (
            shadowRegressionSceneRequested &&
            (
                EnvironmentFlagEnabled("SE_SHADOW_REGRESSION_CAMERA_CONTROLS") ||
                Forward3DDebugDirectorySceneDefaultActive()
            )
        );
    const int windowWidth = ForwardWindowWidth();
    const int windowHeight = ForwardWindowHeight();
    const se::RendererTemporalAntialiasingMode startupAntialiasingMode =
        ForwardTemporalAntialiasingModeFromEnvironment();
    ApplyForwardTemporalAntialiasingEnvironmentDefaults(startupAntialiasingMode);
    if (lightingShowcaseSceneRequested || kForcePbrModelShowcase) {
        ApplyLightingShowcaseIblEnvironmentDefaults();
    }
    se::Application app(
        windowWidth,
        windowHeight,
        kForceLightingShowcase
            ? "SelfEngine Lighting Showcase"
            : (kForcePbrModelShowcase
                ? "SelfEngine PBR Model Showcase"
                : "SelfEngine Forward 3D"),
        kDisplay1,
        se::PipelineSpec::DefaultForward3D(vertexShaderPath, fragmentShaderPath)
    );
    const auto forwardShutdownTraceStartTime = std::chrono::steady_clock::now();
    ForwardShutdownScopeTrace forwardShutdownScope(
        "scene_resources_destroyed",
        forwardShutdownTraceStartTime
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

    se::MeshData3D sphereData = se::MeshFactory::CreateUvSphere(192, 96);
    auto sphereMesh = std::make_unique<se::VulkanMesh>(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        std::move(sphereData.vertices),
        std::move(sphereData.indices)
    );
    app.RenderResources().RegisterMesh("Sphere", *sphereMesh);

    se::MeshData3D coneData = se::MeshFactory::CreateCone(64);
    auto coneMesh = std::make_unique<se::VulkanMesh>(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        std::move(coneData.vertices),
        std::move(coneData.indices)
    );
    app.RenderResources().RegisterMesh("Cone", *coneMesh);

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
        ForwardMaterial({ 0.72f, 0.76f, 0.78f, 1.0f }, 0.0f, 0.28f, 0.62f, 0.05f, 16.0f)
    );
    constexpr std::array<se::u8, 4> kDefaultGroundTexel{
        72, 78, 82, 255
    };
    se::VulkanMaterial& defaultGroundMaterial = app.MaterialLibrary().Create(
        "DefaultGroundMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kDefaultGroundTexel.data(), kDefaultGroundTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 0.45f, 0.48f, 0.50f, 1.0f }, 0.0f, 0.26f, 0.58f, 0.04f, 18.0f),
        false,
        false
    );
    se::VulkanMaterial& gridMaterial = app.MaterialLibrary().Create(
        "GridMaterial",
        checkerTexturePath,
        ForwardMaterial({ 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 0.65f, 0.25f, 0.0f, 8.0f)
    );
    constexpr std::array<se::u8, 4> kShowcaseCharcoalTexel{
        168, 174, 182, 255
    };
    se::VulkanMaterial& showcaseCharcoalMaterial = app.MaterialLibrary().Create(
        "ShowcaseCharcoalMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kShowcaseCharcoalTexel.data(), kShowcaseCharcoalTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 0.56f, 0.58f, 0.60f, 1.0f }, 1.0f, 0.24f, 0.64f, 0.08f, 18.0f),
        false,
        false
    );
    showcaseCharcoalMaterial.Properties().cameraControls = { 0.0f, 0.76f, 0.0f, 0.0f };
    showcaseCharcoalMaterial.Properties().pbrFactors = { 1.0f, 0.48f, 0.0f, 0.0f };

    constexpr std::array<se::u8, 4> kShowcaseBackdropTexel{
        190, 188, 184, 255
    };
    se::VulkanMaterial& showcaseBackdropMaterial = app.MaterialLibrary().Create(
        "ShowcaseBackdropMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kShowcaseBackdropTexel.data(), kShowcaseBackdropTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 0.58f, 0.58f, 0.56f, 1.0f }, 1.0f, 0.28f, 0.62f, 0.06f, 16.0f),
        false,
        false
    );
    showcaseBackdropMaterial.Properties().cameraControls = { 0.0f, 0.84f, 0.0f, 0.0f };
    showcaseBackdropMaterial.Properties().pbrFactors = { 1.0f, 0.42f, 0.0f, 0.0f };

    constexpr std::array<se::u8, 4> kShowcaseLampFixtureTexel{
        96, 100, 104, 255
    };
    se::VulkanMaterial& showcaseLampFixtureMaterial = app.MaterialLibrary().Create(
        "ShowcaseLampFixtureMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kShowcaseLampFixtureTexel.data(), kShowcaseLampFixtureTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 0.44f, 0.46f, 0.48f, 1.0f }, 1.0f, 0.10f, 0.42f, 0.22f, 48.0f),
        false,
        false
    );
    showcaseLampFixtureMaterial.Properties().cameraControls = { 1.0f, 0.34f, 0.0f, 0.0f };
    showcaseLampFixtureMaterial.Properties().specularFactor = { 0.62f, 0.66f, 0.70f, 1.0f };
    showcaseLampFixtureMaterial.Properties().pbrFactors = { 1.0f, 0.62f, 0.0f, 0.0f };

    constexpr std::array<se::u8, 4> kShowcaseWarmStoneTexel{
        166, 118, 76, 255
    };
    se::VulkanMaterial& showcaseWarmStoneMaterial = app.MaterialLibrary().Create(
        "ShowcaseWarmStoneMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kShowcaseWarmStoneTexel.data(), kShowcaseWarmStoneTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 0.72f, 0.50f, 0.31f, 1.0f }, 1.0f, 0.10f, 0.68f, 0.18f, 36.0f),
        false,
        false
    );
    showcaseWarmStoneMaterial.Properties().cameraControls = { 0.0f, 0.68f, 0.0f, 0.0f };
    showcaseWarmStoneMaterial.Properties().pbrFactors = { 1.0f, 0.64f, 0.0f, 0.0f };

    constexpr std::array<se::u8, 4> kShowcaseMetalTexel{
        194, 186, 170, 255
    };
    se::VulkanMaterial& showcaseBrushedMetalMaterial = app.MaterialLibrary().Create(
        "ShowcaseBrushedMetalMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kShowcaseMetalTexel.data(), kShowcaseMetalTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 0.88f, 0.82f, 0.72f, 1.0f }, 1.0f, 0.05f, 0.48f, 0.68f, 128.0f),
        false,
        false
    );
    showcaseBrushedMetalMaterial.Properties().cameraControls = { 1.0f, 0.58f, 0.0f, 0.0f };
    showcaseBrushedMetalMaterial.Properties().specularFactor = { 1.02f, 0.98f, 0.92f, 1.0f };
    showcaseBrushedMetalMaterial.Properties().pbrFactors = { 1.0f, 0.54f, 0.0f, 0.0f };

    constexpr std::array<se::u8, 4> kShowcaseGlossBlackTexel{
        28, 28, 34, 255
    };
    se::VulkanMaterial& showcaseGlossBlackMaterial = app.MaterialLibrary().Create(
        "ShowcaseGlossBlackMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kShowcaseGlossBlackTexel.data(), kShowcaseGlossBlackTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 0.14f, 0.135f, 0.15f, 1.0f }, 1.0f, 0.03f, 0.38f, 0.88f, 160.0f),
        false,
        false
    );
    showcaseGlossBlackMaterial.Properties().cameraControls = { 0.0f, 0.18f, 0.0f, 0.0f };
    showcaseGlossBlackMaterial.Properties().specularFactor = { 1.55f, 1.48f, 1.36f, 1.0f };
    showcaseGlossBlackMaterial.Properties().clearcoatFactor = 1.0f;
    showcaseGlossBlackMaterial.Properties().clearcoatRoughness = 0.08f;

    constexpr std::array<se::u8, 4> kShowcaseCoolCeramicTexel{
        112, 150, 210, 255
    };
    se::VulkanMaterial& showcaseCoolCeramicMaterial = app.MaterialLibrary().Create(
        "ShowcaseCoolCeramicMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kShowcaseCoolCeramicTexel.data(), kShowcaseCoolCeramicTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 0.46f, 0.62f, 0.88f, 1.0f }, 1.0f, 0.08f, 0.68f, 0.46f, 72.0f),
        false,
        false
    );
    showcaseCoolCeramicMaterial.Properties().cameraControls = { 0.0f, 0.66f, 0.0f, 0.0f };
    showcaseCoolCeramicMaterial.Properties().specularFactor = { 0.72f, 0.78f, 0.88f, 1.0f };
    showcaseCoolCeramicMaterial.Properties().clearcoatFactor = 0.0f;
    showcaseCoolCeramicMaterial.Properties().clearcoatRoughness = 0.5f;

    auto createShowcaseReferenceMaterial = [&](
        const char* name,
        std::array<se::u8, 4> texel,
        std::array<se::f32, 4> baseColor,
        se::f32 metallic,
        se::f32 roughness,
        std::array<se::f32, 4> specular,
        se::f32 clearcoat,
        se::f32 clearcoatRoughness
    ) -> se::VulkanMaterial& {
        se::VulkanMaterial& material = app.MaterialLibrary().Create(
            name,
            se::VulkanTexturePixels{
                std::span<const se::u8>(texel.data(), texel.size()),
                1,
                1
            },
            ForwardMaterial(baseColor, 1.0f, 0.06f, 0.54f, 0.46f, 96.0f),
            false,
            false
        );
        material.Properties().cameraControls = { metallic, roughness, 0.0f, 0.0f };
        material.Properties().specularFactor = specular;
        material.Properties().clearcoatFactor = clearcoat;
        material.Properties().clearcoatRoughness = clearcoatRoughness;
        material.Properties().pbrFactors = { 1.0f, 0.42f, 0.0f, 0.0f };
        return material;
    };

    se::VulkanMaterial& showcaseReferenceChromeMaterial = createShowcaseReferenceMaterial(
        "ShowcaseReferenceChromeMaterial",
        { 224, 224, 218, 255 },
        { 0.92f, 0.92f, 0.88f, 1.0f },
        1.0f,
        0.28f,
        { 1.08f, 1.06f, 1.02f, 1.0f },
        0.0f,
        0.0f
    );
    se::VulkanMaterial& showcasePerfectMirrorMaterial = createShowcaseReferenceMaterial(
        "ShowcasePerfectMirrorMaterial",
        { 242, 244, 248, 255 },
        { 0.95f, 0.96f, 0.98f, 1.0f },
        1.0f,
        0.0f,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        0.0f,
        0.0f
    );
    se::VulkanMaterial& showcaseReflectionTestSilverMaterial =
        createShowcaseReferenceMaterial(
            "ShowcaseReflectionTestSilverMaterial",
            { 214, 220, 228, 255 },
            { 0.78f, 0.82f, 0.88f, 1.0f },
            0.68f,
            0.24f,
            { 0.96f, 0.98f, 1.0f, 1.0f },
            0.0f,
            0.0f
        );
    se::VulkanMaterial& showcaseReferenceSatinMetalMaterial = createShowcaseReferenceMaterial(
        "ShowcaseReferenceSatinMetalMaterial",
        { 186, 176, 158, 255 },
        { 0.82f, 0.76f, 0.66f, 1.0f },
        1.0f,
        0.56f,
        { 0.88f, 0.84f, 0.76f, 1.0f },
        0.0f,
        0.0f
    );
    se::VulkanMaterial& showcaseReferencePorcelainMaterial = createShowcaseReferenceMaterial(
        "ShowcaseReferencePorcelainMaterial",
        { 232, 238, 242, 255 },
        { 0.86f, 0.90f, 0.92f, 1.0f },
        0.0f,
        0.38f,
        { 0.72f, 0.76f, 0.82f, 1.0f },
        0.25f,
        0.22f
    );
    se::VulkanMaterial& showcaseReferenceGlossRedMaterial = createShowcaseReferenceMaterial(
        "ShowcaseReferenceGlossRedMaterial",
        { 190, 38, 28, 255 },
        { 0.78f, 0.12f, 0.08f, 1.0f },
        0.0f,
        0.20f,
        { 1.05f, 0.86f, 0.78f, 1.0f },
        1.0f,
        0.10f
    );
    se::VulkanMaterial& showcaseReferenceMatteRubberMaterial = createShowcaseReferenceMaterial(
        "ShowcaseReferenceMatteRubberMaterial",
        { 42, 44, 48, 255 },
        { 0.22f, 0.23f, 0.25f, 1.0f },
        0.0f,
        0.88f,
        { 0.34f, 0.34f, 0.36f, 1.0f },
        0.0f,
        0.0f
    );

    constexpr std::array<se::u8, 4> kShowcaseEmissiveTexel{
        255, 152, 42, 255
    };
    se::VulkanMaterial& showcaseEmissiveMaterial = app.MaterialLibrary().Create(
        "ShowcaseEmissiveMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kShowcaseEmissiveTexel.data(), kShowcaseEmissiveTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 1.0f, 0.52f, 0.18f, 1.0f }, 1.0f, 0.0f, 0.15f, 0.12f, 16.0f),
        false,
        false
    );
    showcaseEmissiveMaterial.Properties().cameraControls = { 0.0f, 0.42f, 0.0f, 0.0f };
    showcaseEmissiveMaterial.Properties().emissiveFactor = { 2.5f, 1.25f, 0.34f, 1.0f };

    auto createShowcaseEmitterMaterial = [&](
        const char* name,
        std::array<se::u8, 4> texel,
        std::array<se::f32, 4> baseColor,
        std::array<se::f32, 4> emissive
    ) -> se::VulkanMaterial& {
        se::VulkanMaterial& material = app.MaterialLibrary().Create(
            name,
            se::VulkanTexturePixels{
                std::span<const se::u8>(texel.data(), texel.size()),
                1,
                1
            },
            ForwardMaterial(baseColor, 1.0f, 0.0f, 0.12f, 0.08f, 10.0f),
            false,
            false
        );
        material.Properties().doubleSided = true;
        material.Properties().pbrFactors = { 1.0f, 0.34f, 0.0f, 0.0f };
        material.Properties().emissiveFactor = emissive;
        return material;
    };

    se::VulkanMaterial& showcaseCoolStripLightMaterial = createShowcaseEmitterMaterial(
        "ShowcaseCoolStripLightMaterial",
        { 186, 202, 255, 255 },
        { 0.78f, 0.84f, 1.0f, 1.0f },
        { 0.48f, 0.60f, 1.05f, 1.0f }
    );
    se::VulkanMaterial& showcaseWarmKeyLightMaterial = createShowcaseEmitterMaterial(
        "ShowcaseWarmKeyLightMaterial",
        { 255, 194, 122, 255 },
        { 1.0f, 0.72f, 0.42f, 1.0f },
        { 0.88f, 0.54f, 0.26f, 1.0f }
    );
    se::VulkanMaterial& showcaseCoolRimLightMaterial = createShowcaseEmitterMaterial(
        "ShowcaseCoolRimLightMaterial",
        { 116, 156, 255, 255 },
        { 0.48f, 0.64f, 1.0f, 1.0f },
        { 0.28f, 0.46f, 0.98f, 1.0f }
    );
    se::VulkanMaterial& showcaseSoftFillLightMaterial = createShowcaseEmitterMaterial(
        "ShowcaseSoftFillLightMaterial",
        { 182, 210, 255, 255 },
        { 0.72f, 0.82f, 1.0f, 1.0f },
        { 0.34f, 0.44f, 0.72f, 1.0f }
    );
    se::VulkanMaterial& showcaseLowWarmLightMaterial = createShowcaseEmitterMaterial(
        "ShowcaseLowWarmLightMaterial",
        { 255, 96, 42, 255 },
        { 1.0f, 0.34f, 0.16f, 1.0f },
        { 0.55f, 0.20f, 0.10f, 1.0f }
    );
    se::VulkanMaterial& showcaseLowCoolLightMaterial = createShowcaseEmitterMaterial(
        "ShowcaseLowCoolLightMaterial",
        { 70, 132, 255, 255 },
        { 0.28f, 0.52f, 1.0f, 1.0f },
        { 0.15f, 0.34f, 0.78f, 1.0f }
    );

    constexpr std::array<se::u8, 4> kPointLightGizmoTexel{
        255, 150, 72, 210
    };
    se::VulkanMaterial& pointLightGizmoMaterial = app.MaterialLibrary().Create(
        "PointLightGizmoMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kPointLightGizmoTexel.data(), kPointLightGizmoTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 1.0f, 0.58f, 0.24f, 0.64f }, 1.0f, 0.0f, 0.36f, 0.12f, 16.0f),
        false,
        false
    );
    pointLightGizmoMaterial.Properties().alphaMode = se::MaterialAlphaMode::Blend;
    pointLightGizmoMaterial.Properties().renderClass = se::MaterialRenderClass::Transparent;
    pointLightGizmoMaterial.Properties().emissiveFactor = { 2.2f, 0.95f, 0.24f, 1.0f };

    constexpr std::array<se::u8, 4> kSpotLightGizmoTexel{
        112, 166, 255, 180
    };
    se::VulkanMaterial& spotLightGizmoMaterial = app.MaterialLibrary().Create(
        "SpotLightGizmoMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kSpotLightGizmoTexel.data(), kSpotLightGizmoTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 0.42f, 0.66f, 1.0f, 0.48f }, 1.0f, 0.0f, 0.30f, 0.08f, 12.0f),
        false,
        false
    );
    spotLightGizmoMaterial.Properties().alphaMode = se::MaterialAlphaMode::Blend;
    spotLightGizmoMaterial.Properties().renderClass = se::MaterialRenderClass::Transparent;
    spotLightGizmoMaterial.Properties().doubleSided = true;
    spotLightGizmoMaterial.Properties().emissiveFactor = { 0.42f, 0.88f, 2.4f, 1.0f };

    constexpr std::array<se::u8, 4> kRectLightGizmoTexel{
        255, 226, 132, 190
    };
    se::VulkanMaterial& rectLightGizmoMaterial = app.MaterialLibrary().Create(
        "RectLightGizmoMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kRectLightGizmoTexel.data(), kRectLightGizmoTexel.size()),
            1,
            1
        },
        ForwardMaterial({ 1.0f, 0.86f, 0.44f, 0.56f }, 1.0f, 0.0f, 0.28f, 0.06f, 10.0f),
        false,
        false
    );
    rectLightGizmoMaterial.Properties().alphaMode = se::MaterialAlphaMode::Blend;
    rectLightGizmoMaterial.Properties().renderClass = se::MaterialRenderClass::Transparent;
    rectLightGizmoMaterial.Properties().doubleSided = true;
    rectLightGizmoMaterial.Properties().emissiveFactor = { 2.8f, 2.0f, 0.62f, 1.0f };

    std::array<se::VulkanMaterial*, 10> lightIndexDigitMaterials{};
    for (se::u32 digit = 0; digit < lightIndexDigitMaterials.size(); ++digit) {
        std::vector<se::u8> digitPixels = CreateLightIndexDigitPixels(digit);
        se::VulkanMaterial& digitMaterial = app.MaterialLibrary().Create(
            LightIndexDigitMaterialName(digit),
            se::VulkanTexturePixels{
                std::span<const se::u8>(digitPixels.data(), digitPixels.size()),
                kLightIndexDigitTextureWidth,
                kLightIndexDigitTextureHeight
            },
            ForwardMaterial({ 0.92f, 0.98f, 1.0f, 1.0f }, 1.0f, 0.0f, 0.18f, 0.04f, 8.0f),
            true,
            false
        );
        digitMaterial.Properties().alphaMode = se::MaterialAlphaMode::Blend;
        digitMaterial.Properties().renderClass = se::MaterialRenderClass::Transparent;
        digitMaterial.Properties().doubleSided = true;
        digitMaterial.Properties().emissiveFactor = { 1.8f, 2.2f, 2.6f, 1.0f };
        lightIndexDigitMaterials[digit] = &digitMaterial;
    }

    constexpr std::array<se::u8, 4> kLightIndexLabelBackplateTexel{
        8, 12, 18, 180
    };
    se::VulkanMaterial& lightIndexLabelBackplateMaterial = app.MaterialLibrary().Create(
        "LightIndexLabelBackplateMaterial",
        se::VulkanTexturePixels{
            std::span<const se::u8>(
                kLightIndexLabelBackplateTexel.data(),
                kLightIndexLabelBackplateTexel.size()
            ),
            1,
            1
        },
        ForwardMaterial({ 0.02f, 0.025f, 0.035f, 0.70f }, 1.0f, 0.0f, 0.08f, 0.02f, 4.0f),
        false,
        false
    );
    lightIndexLabelBackplateMaterial.Properties().alphaMode = se::MaterialAlphaMode::Blend;
    lightIndexLabelBackplateMaterial.Properties().renderClass = se::MaterialRenderClass::Transparent;
    lightIndexLabelBackplateMaterial.Properties().emissiveFactor = { 0.02f, 0.03f, 0.04f, 1.0f };
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
    app.RenderResources().RegisterMaterial("DefaultGroundMaterial", defaultGroundMaterial);
    app.RenderResources().RegisterMaterial("GridMaterial", gridMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseCharcoalMaterial", showcaseCharcoalMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseBackdropMaterial", showcaseBackdropMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseLampFixtureMaterial", showcaseLampFixtureMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseWarmStoneMaterial", showcaseWarmStoneMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseBrushedMetalMaterial", showcaseBrushedMetalMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseGlossBlackMaterial", showcaseGlossBlackMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseCoolCeramicMaterial", showcaseCoolCeramicMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseReferenceChromeMaterial", showcaseReferenceChromeMaterial);
    app.RenderResources().RegisterMaterial("ShowcasePerfectMirrorMaterial", showcasePerfectMirrorMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseReflectionTestSilverMaterial", showcaseReflectionTestSilverMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseReferenceSatinMetalMaterial", showcaseReferenceSatinMetalMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseReferencePorcelainMaterial", showcaseReferencePorcelainMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseReferenceGlossRedMaterial", showcaseReferenceGlossRedMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseReferenceMatteRubberMaterial", showcaseReferenceMatteRubberMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseEmissiveMaterial", showcaseEmissiveMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseCoolStripLightMaterial", showcaseCoolStripLightMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseWarmKeyLightMaterial", showcaseWarmKeyLightMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseCoolRimLightMaterial", showcaseCoolRimLightMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseSoftFillLightMaterial", showcaseSoftFillLightMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseLowWarmLightMaterial", showcaseLowWarmLightMaterial);
    app.RenderResources().RegisterMaterial("ShowcaseLowCoolLightMaterial", showcaseLowCoolLightMaterial);
    app.RenderResources().RegisterMaterial("PointLightGizmoMaterial", pointLightGizmoMaterial);
    app.RenderResources().RegisterMaterial("SpotLightGizmoMaterial", spotLightGizmoMaterial);
    app.RenderResources().RegisterMaterial("RectLightGizmoMaterial", rectLightGizmoMaterial);
    for (se::u32 digit = 0; digit < lightIndexDigitMaterials.size(); ++digit) {
        app.RenderResources().RegisterMaterial(
            LightIndexDigitMaterialName(digit),
            *lightIndexDigitMaterials[digit]
        );
    }
    app.RenderResources().RegisterMaterial(
        "LightIndexLabelBackplateMaterial",
        lightIndexLabelBackplateMaterial
    );

    se::Scene3D scene;
    se::Scene3D overlayScene;
    std::vector<BenchmarkMovingObject> benchmarkMovingObjects;
    se::RuntimeModelLoader runtimeModelLoader(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        app.MaterialLibrary(),
        app.RenderResources(),
        scene,
        checkerTexturePath
    );
    se::RuntimeModelLoadResult defaultModelLoad{};
    bool defaultModelLoadRequested = false;
    if (lightingShowcaseSceneRequested) {
        BuildLightingShowcaseScene(scene, &overlayScene);
    } else if (shadowRegressionSceneRequested) {
        BuildShadowRegressionScene(scene, &overlayScene);
        defaultModelLoadRequested = true;
        defaultModelLoad = LoadDefaultSceneSkinnedFbx(
            runtimeModelLoader,
            { -1.18f, -0.50f, 0.05f },
            { 0.0f, 180.0f, 0.0f },
            1.34f,
            { 1.0f, 1.0f, 1.0f },
            true,
            "Shadow regression"
        );
    } else if (useBenchmarkScene) {
        BuildBenchmarkGridScene(scene, BenchmarkGridSize());
    } else if (!hasStartupModel) {
        se::Renderable3D& ground = scene.CreateRenderable(
            "Ground",
            "Plane",
            "DefaultGroundMaterial"
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
        AddBenchmarkMovingObject(benchmarkMovingObjects, rightCube);

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

        defaultModelLoadRequested = true;
        defaultModelLoad = LoadDefaultSceneSkinnedFbx(
            runtimeModelLoader,
            { -1.35f, -0.48f, 0.45f },
            { 0.0f, 180.0f, 0.0f },
            1.35f,
            { 1.0f, 1.0f, 1.0f },
            DefaultSceneSkinnedFbxProductionRequested(),
            "Default-scene"
        );
    }

    if (hasStartupModel) {
        std::cout << "Loading startup model: "
            << se::UnrealPathToUtf8(startupModelPath)
            << std::endl;
        defaultModelLoadRequested = true;
        const std::size_t startupModelFirstRenderableIndex = scene.Count();
        defaultModelLoad = runtimeModelLoader.LoadIntoScene(
            startupModelPath,
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f }
        );
        if (defaultModelLoad.loaded) {
            std::cout << "Startup model loaded: "
                << defaultModelLoad.message
                << std::endl;
            AddBenchmarkMovingObjectsFromSceneRange(
                benchmarkMovingObjects,
                scene,
                startupModelFirstRenderableIndex
            );
            if (previewStartupModelLighting) {
                if (kForcePbrModelShowcase) {
                    BuildPbrModelShowcaseEnvironment(scene);
                } else {
                    se::Renderable3D& previewGround = scene.CreateRenderable(
                        "Imported Preview Ground",
                        "Plane",
                        "DefaultGroundMaterial"
                    );
                    previewGround.Transform().SetPosition({ 0.0f, -1.12f, 0.0f });
                    previewGround.Transform().SetScale({ 7.0f, 1.0f, 7.0f });
                    previewGround.Transform().SetAnimateRotation(false);
                    previewGround.SetDrawOrder(-20);
                    previewGround.SetPickable(false);
                    previewGround.SetCastShadow(false);
                }
            }
        } else {
            std::cout << "[warning] Startup model load failed: "
                << defaultModelLoad.message
                << std::endl;
        }
    }
    const std::size_t bridgeFirstRenderableIndex = scene.Count();
    const se::u32 loadedBridgeMeshInstances =
        LoadBridgeMeshInstances(startupBridgeScene, runtimeModelLoader);
    (void)loadedBridgeMeshInstances;
    if (loadedBridgeMeshInstances > 0) {
        AddBenchmarkMovingObjectsFromSceneRange(
            benchmarkMovingObjects,
            scene,
            bridgeFirstRenderableIndex
        );
    }

    se::Camera3D camera;
    if (lightingShowcaseSceneRequested) {
        camera.SetPose(
            { 0.2f, 1.35f, 6.6f },
            { -0.02f, -0.20f, -1.0f }
        );
        camera.SetFovScale(0.72f);
        camera.SetMoveSpeed(3.0f);
    } else if (kForcePbrModelShowcase) {
        camera.SetPose(
            { 0.1f, 0.28f, 4.15f },
            { -0.02f, -0.06f, -1.0f }
        );
        camera.SetFovScale(0.72f);
        camera.SetMoveSpeed(2.4f);
    } else if (shadowRegressionSceneRequested) {
        camera.SetPose(
            { 0.25f, 1.55f, 6.45f },
            { -0.04f, -0.24f, -1.0f }
        );
        camera.SetFovScale(0.70f);
    } else if (useBenchmarkScene) {
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
    if (
        !lightingShowcaseSceneRequested &&
        !shadowRegressionSceneRequested &&
        !kForcePbrModelShowcase &&
        !bridgeLights.anyApplied
    ) {
        ApplySceneDirectionalLight(scene, camera, previewStartupModelLighting);
    }
    // An explicit benchmark camera path is valid for every scene type. The
    // normal benchmark camera remains fixed unless this opt-in is supplied.
    const bool benchmarkCameraMotionRequested = BenchmarkCameraMotionRequested();
    const bool cameraFreezeRequested =
        EnvironmentFlagEnabled("SE_CAMERA_FREEZE") ||
        EnvironmentFlagEnabled("SE_FREEZE_CAMERA");
    const bool sceneUpdateFreezeRequested =
        EnvironmentFlagEnabled("SE_SCENE_UPDATE_FREEZE") ||
        EnvironmentFlagEnabled("SE_FREEZE_SCENE_UPDATE");
    const BenchmarkObjectMotionMode benchmarkObjectMotionMode =
        !useBenchmarkScene && !benchmarkMovingObjects.empty()
            ? BenchmarkObjectMotionModeFromEnvironment()
            : BenchmarkObjectMotionMode::None;
    const bool benchmarkObjectMotionRequested =
        benchmarkObjectMotionMode != BenchmarkObjectMotionMode::None;
    if (benchmarkObjectMotionRequested) {
        DisableBenchmarkAutoRotation(benchmarkMovingObjects);
    }
    const bool useElapsedTimeRuntimeAnimationClock =
        shadowRegressionSceneRequested ||
        (
            !useBenchmarkScene &&
            (
                benchmarkCameraMotionRequested ||
                benchmarkObjectMotionRequested ||
                DefaultSceneSkinnedFbxProductionRequested()
            )
        );
    const glm::vec3 benchmarkPartialLocalShadowBasePosition{
        -2.2f,
        1.1f,
        -1.8f
    };
    se::f32 benchmarkPartialLocalShadowTime = 0.0f;
    se::f32 benchmarkCameraMotionTime = 0.0f;
    se::f32 benchmarkObjectMotionTime = 0.0f;
    PickClickState pickClickState{};

    app.CreateRenderer();
    SE_ASSERT(app.Renderer() != nullptr, "Forward 3D demo needs a renderer");
    if (lightingShowcaseSceneRequested) {
        ApplyLightingShowcaseRendererSettings(*app.Renderer());
    } else {
        ApplyForward3DRendererSettings(*app.Renderer());
    }
    app.Renderer()->SetTemporalAntialiasingMode(startupAntialiasingMode);
    if (overlayScene.Count() > 0) {
        se::PipelineSpec lightGizmoOverlaySpec =
            se::PipelineSpec::DoubleSided(
                se::PipelineSpec::ForwardResidual3D(
                    vertexShaderPath,
                    fragmentShaderPath
                )
            );
        lightGizmoOverlaySpec.depthTestEnabled = false;
        lightGizmoOverlaySpec.depthWriteEnabled = false;
        app.Renderer()->SetOverlay3DContext(
            &overlayScene,
            &camera,
            std::move(lightGizmoOverlaySpec)
        );
    }
    std::cout << "Forward 3D antialiasing mode: "
        << TemporalAntialiasingModeName(app.Renderer()->TemporalAntialiasingMode())
        << " (F6 cycles Native TAA / DLSS DLAA L / DLSS SR)" << std::endl;
    bridgeLights.skyLightApplied =
        ApplyBridgeSkyLightToRenderer(bridgeLights, *app.Renderer());
    app.Renderer()->ApplyEnvironmentRenderSettings();
    if (!lightingShowcaseSceneRequested) {
        ApplyForward3DEnvironmentShadowDefaults(*app.Renderer());
    }
    se::BenchmarkSceneDiagnostics sceneDiagnostics =
        BenchmarkDiagnosticsForStartupBridgeScene(
            startupBridgeScene,
            loadedBridgeMeshInstances,
            bridgeCameraApplied,
            bridgeLights
        );
    sceneDiagnostics.runtimeImportModelRequested =
        defaultModelLoadRequested ? 1u : 0u;
    sceneDiagnostics.runtimeImportModelLoaded = defaultModelLoad.loaded ? 1u : 0u;
    sceneDiagnostics.runtimeImportCacheHit = defaultModelLoad.cacheHit ? 1u : 0u;
    sceneDiagnostics.runtimeImportMeshCount = defaultModelLoad.meshCount;
    sceneDiagnostics.runtimeImportMaterialCount = defaultModelLoad.materialCount;
    sceneDiagnostics.runtimeImportSourceVertexCount =
        defaultModelLoad.sourceVertexCount;
    sceneDiagnostics.runtimeImportSourceTriangleCount =
        defaultModelLoad.sourceTriangleCount;
    sceneDiagnostics.runtimeImportSourceTangentVertexCount =
        defaultModelLoad.sourceTangentVertexCount;
    sceneDiagnostics.runtimeImportSourceTangentGenerationEnabled =
        defaultModelLoad.sourceTangentGenerationEnabled;
    sceneDiagnostics.runtimeImportSourceTexturedMaterialCount =
        defaultModelLoad.sourceTexturedMaterialCount;
    sceneDiagnostics.runtimeImportSourceBaseColorTextureMaterialCount =
        defaultModelLoad.sourceBaseColorTextureMaterialCount;
    sceneDiagnostics.runtimeImportSourceNormalTextureMaterialCount =
        defaultModelLoad.sourceNormalTextureMaterialCount;
    sceneDiagnostics.runtimeImportSourceMetallicRoughnessTextureMaterialCount =
        defaultModelLoad.sourceMetallicRoughnessTextureMaterialCount;
    sceneDiagnostics.runtimeImportLodCacheRequested =
        defaultModelLoad.meshLodCache.requested;
    sceneDiagnostics.runtimeImportLodCacheHit =
        defaultModelLoad.meshLodCache.hit;
    sceneDiagnostics.runtimeImportLodCacheMiss =
        defaultModelLoad.meshLodCache.miss;
    sceneDiagnostics.runtimeImportLodCacheRejected =
        defaultModelLoad.meshLodCache.rejected;
    sceneDiagnostics.runtimeImportLodCacheWritten =
        defaultModelLoad.meshLodCache.written;
    sceneDiagnostics.runtimeImportLodCacheFallbackReason =
        static_cast<se::u32>(defaultModelLoad.meshLodCache.fallbackReason);
    sceneDiagnostics.runtimeImportLodCacheableMeshCount =
        defaultModelLoad.meshLodCache.cacheableMeshCount;
    sceneDiagnostics.runtimeImportLodCacheDecodedChainCount =
        defaultModelLoad.meshLodCache.decodedChainCount;
    sceneDiagnostics.runtimeImportLodCacheDecodedLevelCount =
        defaultModelLoad.meshLodCache.decodedLevelCount;
    sceneDiagnostics.runtimeImportLodCacheSourceFileBytes =
        defaultModelLoad.meshLodCache.sourceFileBytes;
    sceneDiagnostics.runtimeImportLodCacheSourceHash =
        defaultModelLoad.meshLodCache.sourceHash;
    sceneDiagnostics.runtimeImportLodCacheSettingsHash =
        defaultModelLoad.meshLodCache.settingsHash;
    sceneDiagnostics.runtimeImportLodCacheKeyHash =
        defaultModelLoad.meshLodCache.cacheKeyHash;
    sceneDiagnostics.runtimeImportLodCacheRawBytes =
        defaultModelLoad.meshLodCache.rawBytes;
    sceneDiagnostics.runtimeImportLodCacheEncodedBytes =
        defaultModelLoad.meshLodCache.encodedBytes;
    sceneDiagnostics.runtimeImportLodCacheFileBytes =
        defaultModelLoad.meshLodCache.fileBytes;
    sceneDiagnostics.runtimeImportLodCacheSourceHashMicroseconds =
        defaultModelLoad.meshLodCache.sourceHashMicroseconds;
    sceneDiagnostics.runtimeImportLodCacheReadMicroseconds =
        defaultModelLoad.meshLodCache.readMicroseconds;
    sceneDiagnostics.runtimeImportLodCacheDecodeMicroseconds =
        defaultModelLoad.meshLodCache.decodeMicroseconds;
    sceneDiagnostics.runtimeImportLodCacheBuildMicroseconds =
        defaultModelLoad.meshLodCache.buildMicroseconds;
    sceneDiagnostics.runtimeImportLodCacheWriteMicroseconds =
        defaultModelLoad.meshLodCache.writeMicroseconds;
    sceneDiagnostics.runtimeImportLodCacheImportMicroseconds =
        defaultModelLoad.meshLodCache.importMicroseconds;
    sceneDiagnostics.runtimeImportLodCacheTotalLoadMicroseconds =
        defaultModelLoad.meshLodCache.totalLoadMicroseconds;
    sceneDiagnostics.runtimeImportNodeCount = defaultModelLoad.sourceNodeCount;
    sceneDiagnostics.runtimeImportBoneNodeCount = defaultModelLoad.sourceBoneNodeCount;
    sceneDiagnostics.runtimeImportAnimationChannelBoundCount =
        defaultModelLoad.sourceAnimationChannelBoundCount;
    sceneDiagnostics.runtimeImportAnimationChannelUnboundCount =
        defaultModelLoad.sourceAnimationChannelUnboundCount;
    sceneDiagnostics.runtimeImportBoneNameMatchedNodeCount =
        defaultModelLoad.sourceBoneNameMatchedNodeCount;
    sceneDiagnostics.runtimeImportBoneNameUnmatchedCount =
        defaultModelLoad.sourceBoneNameUnmatchedCount;
    sceneDiagnostics.runtimeImportAnimationCount = defaultModelLoad.sourceAnimationCount;
    sceneDiagnostics.runtimeImportAnimationChannelCount =
        defaultModelLoad.sourceAnimationChannelCount;
    sceneDiagnostics.runtimeImportAnimationPositionKeyCount =
        defaultModelLoad.sourceAnimationPositionKeyCount;
    sceneDiagnostics.runtimeImportAnimationRotationKeyCount =
        defaultModelLoad.sourceAnimationRotationKeyCount;
    sceneDiagnostics.runtimeImportAnimationScaleKeyCount =
        defaultModelLoad.sourceAnimationScaleKeyCount;
    sceneDiagnostics.runtimeImportAnimationKeyCount =
        defaultModelLoad.sourceAnimationKeyCount;
    sceneDiagnostics.runtimeImportMaxAnimationKeysPerChannel =
        defaultModelLoad.sourceMaxAnimationKeysPerChannel;
    sceneDiagnostics.runtimeImportPoseSampledClipCount =
        defaultModelLoad.sourcePoseSampledClipCount;
    sceneDiagnostics.runtimeImportPoseSampledChannelCount =
        defaultModelLoad.sourcePoseSampledChannelCount;
    sceneDiagnostics.runtimeImportPoseSampledNodeCount =
        defaultModelLoad.sourcePoseSampledNodeCount;
    sceneDiagnostics.runtimeImportPoseAnimatedNodeCount =
        defaultModelLoad.sourcePoseAnimatedNodeCount;
    sceneDiagnostics.runtimeImportPoseBonePaletteEntryCount =
        defaultModelLoad.sourcePoseBonePaletteEntryCount;
    sceneDiagnostics.runtimeImportPosePreviousBonePaletteEntryCount =
        defaultModelLoad.sourcePosePreviousBonePaletteEntryCount;
    sceneDiagnostics.runtimeImportPoseChangedBonePaletteEntryCount =
        defaultModelLoad.sourcePoseChangedBonePaletteEntryCount;
    sceneDiagnostics.runtimeImportPoseBonePaletteReady =
        defaultModelLoad.sourcePoseBonePaletteReady;
    sceneDiagnostics.runtimeImportPoseCarrierBonePaletteEntryCount =
        defaultModelLoad.runtimePoseCarrierBonePaletteEntryCount;
    sceneDiagnostics.runtimeImportPoseCarrierPreviousBonePaletteEntryCount =
        defaultModelLoad.runtimePoseCarrierPreviousBonePaletteEntryCount;
    sceneDiagnostics.runtimeImportPoseCarrierChangedBonePaletteEntryCount =
        defaultModelLoad.runtimePoseCarrierChangedBonePaletteEntryCount;
    sceneDiagnostics.runtimeImportPoseCarrierReady =
        defaultModelLoad.runtimePoseCarrierReady;
    sceneDiagnostics.runtimeImportRendererPosePaletteRegistered =
        defaultModelLoad.rendererPosePaletteRegistered;
    sceneDiagnostics.runtimeImportRendererPosePaletteBonePaletteEntryCount =
        defaultModelLoad.rendererPosePaletteBonePaletteEntryCount;
    sceneDiagnostics.runtimeImportRendererPosePalettePreviousBonePaletteEntryCount =
        defaultModelLoad.rendererPosePalettePreviousBonePaletteEntryCount;
    sceneDiagnostics.runtimeImportRendererPosePaletteChangedBonePaletteEntryCount =
        defaultModelLoad.rendererPosePaletteChangedBonePaletteEntryCount;
    sceneDiagnostics.runtimeImportRendererPosePaletteReady =
        defaultModelLoad.rendererPosePaletteReady;
    sceneDiagnostics.runtimeImportGpuPosePaletteBufferAllocated =
        defaultModelLoad.gpuPosePaletteBufferAllocated;
    sceneDiagnostics.runtimeImportGpuPosePaletteBufferUploaded =
        defaultModelLoad.gpuPosePaletteBufferUploaded;
    sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorInfoReady =
        defaultModelLoad.gpuPosePaletteDescriptorInfoReady;
    sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorSetAllocated =
        defaultModelLoad.gpuPosePaletteDescriptorSetAllocated;
    sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorSetWritten =
        defaultModelLoad.gpuPosePaletteDescriptorSetWritten;
    sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorSetReady =
        defaultModelLoad.gpuPosePaletteDescriptorSetReady;
    sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorBinding =
        defaultModelLoad.gpuPosePaletteDescriptorBinding;
    sceneDiagnostics.runtimeImportGpuPosePaletteDescriptorRangeBytes =
        defaultModelLoad.gpuPosePaletteDescriptorRangeBytes;
    sceneDiagnostics.runtimeImportGpuPosePaletteBufferBytes =
        defaultModelLoad.gpuPosePaletteBufferBytes;
    sceneDiagnostics.runtimeImportGpuPosePaletteCurrentEntryCount =
        defaultModelLoad.gpuPosePaletteCurrentEntryCount;
    sceneDiagnostics.runtimeImportGpuPosePalettePreviousEntryCount =
        defaultModelLoad.gpuPosePalettePreviousEntryCount;
    sceneDiagnostics.runtimeImportMeshWithBonesCount =
        defaultModelLoad.sourceMeshWithBonesCount;
    sceneDiagnostics.runtimeImportBoneCount = defaultModelLoad.sourceBoneCount;
    sceneDiagnostics.runtimeImportSkinnedVertexCount =
        defaultModelLoad.sourceSkinnedVertexCount;
    sceneDiagnostics.runtimeImportBoneInfluenceCount =
        defaultModelLoad.sourceBoneInfluenceCount;
    sceneDiagnostics.runtimeImportMaxBoneInfluencesPerVertex =
        defaultModelLoad.sourceMaxBoneInfluencesPerVertex;
    sceneDiagnostics.runtimeImportSkinnedVertexAttributeCount =
        defaultModelLoad.sourceSkinnedVertexAttributeCount;
    sceneDiagnostics.runtimeImportBoneAttributeInfluenceCount =
        defaultModelLoad.sourceBoneAttributeInfluenceCount;
    sceneDiagnostics.runtimeImportMaxBoneAttributeInfluencesPerVertex =
        defaultModelLoad.sourceMaxBoneAttributeInfluencesPerVertex;
    sceneDiagnostics.runtimeImportBoneInfluenceOverflowCount =
        defaultModelLoad.sourceBoneInfluenceOverflowCount;
    sceneDiagnostics.runtimeImportSkinnedVertexAttributeReady =
        defaultModelLoad.sourceSkinnedVertexAttributeReady;
    sceneDiagnostics.runtimeImportSkinnedAnimationSpaceReady =
        defaultModelLoad.runtimeSkinnedAnimationSpaceReady;
    sceneDiagnostics.runtimeImportSkinnedAnimationSpaceBlockerMask =
        defaultModelLoad.runtimeSkinnedAnimationSpaceBlockerMask;
    sceneDiagnostics.runtimeImportSkinnedAnimationRenderableBound =
        defaultModelLoad.runtimeSkinnedAnimationRenderableBound;
    const bool runtimeImportHasSkinnedAnimationContent =
        defaultModelLoad.sourceAnimationCount > 0u &&
        defaultModelLoad.sourceMeshWithBonesCount > 0u &&
        defaultModelLoad.sourceBoneCount > 0u;
    auto refreshRuntimeAnimationDiagnostics = [&]() {
        const se::RuntimeModelAnimationPlaybackDiagnostics playbackDiagnostics =
            runtimeModelLoader.AnimationPlaybackDiagnostics();
        sceneDiagnostics.runtimeImportAnimationPlaybackReady =
            playbackDiagnostics.ready;
        sceneDiagnostics.runtimeImportAnimationPlaybackCandidateModelCount =
            playbackDiagnostics.candidateModelCount;
        sceneDiagnostics.runtimeImportAnimationPlaybackReadyModelCount =
            playbackDiagnostics.readyModelCount;
        sceneDiagnostics.runtimeImportAnimationPlaybackFrameCount =
            playbackDiagnostics.frameCount;
        sceneDiagnostics.runtimeImportAnimationPlaybackLoopWrapCount =
            playbackDiagnostics.loopWrapCount;
        sceneDiagnostics.runtimeImportAnimationPlaybackPreviousPoseCollapsedCount =
            playbackDiagnostics.previousPoseCollapsedCount;
        sceneDiagnostics.runtimeImportAnimationPlaybackChangedBonePaletteEntryCount =
            playbackDiagnostics.changedBonePaletteEntryCount;
        sceneDiagnostics.runtimeImportAnimationPlaybackRendererPaletteReady =
            playbackDiagnostics.rendererPaletteReady;
        sceneDiagnostics.runtimeImportAnimationPlaybackGpuUploadReady =
            playbackDiagnostics.gpuUploadReady;
        sceneDiagnostics.runtimeImportAnimationPlaybackClockMode =
            useElapsedTimeRuntimeAnimationClock ? 1u : 0u;
        sceneDiagnostics.runtimeImportAnimationPlaybackPreviousTimeTicks =
            playbackDiagnostics.previousTimeTicks;
        sceneDiagnostics.runtimeImportAnimationPlaybackCurrentTimeTicks =
            playbackDiagnostics.currentTimeTicks;
        sceneDiagnostics.runtimeImportAnimationPlaybackPreviousAbsoluteSeconds =
            playbackDiagnostics.previousAbsoluteSeconds;
        sceneDiagnostics.runtimeImportAnimationPlaybackCurrentAbsoluteSeconds =
            playbackDiagnostics.currentAbsoluteSeconds;
        sceneDiagnostics.runtimeImportAnimationDiagnosticPoseOnly =
            runtimeImportHasSkinnedAnimationContent &&
            defaultModelLoad.sourcePoseSampledClipCount > 0u &&
            defaultModelLoad.runtimePoseCarrierReady != 0u &&
            playbackDiagnostics.ready == 0u
                ? 1u
                : 0u;
        sceneDiagnostics.runtimeImportSkinnedAnimationSupportBlockerMask = 0u;
        if (runtimeImportHasSkinnedAnimationContent) {
            constexpr se::u32 kRenderableNotBoundBlocker = 1u << 0u;
            constexpr se::u32 kPlaybackNotReadyBlocker = 1u << 1u;
            constexpr se::u32 kSpaceNotReadyBlocker = 1u << 2u;
            constexpr se::u32 kSkinnedVertexAttributeBlocker = 1u << 3u;
            constexpr se::u32 kRendererPaletteNotReadyBlocker = 1u << 4u;
            constexpr se::u32 kGpuUploadNotReadyBlocker = 1u << 5u;
            sceneDiagnostics.runtimeImportSkinnedAnimationSupportBlockerMask =
                (sceneDiagnostics.runtimeImportSkinnedAnimationRenderableBound == 0u
                    ? kRenderableNotBoundBlocker
                    : 0u) |
                (playbackDiagnostics.ready == 0u
                    ? kPlaybackNotReadyBlocker
                    : 0u) |
                (sceneDiagnostics.runtimeImportSkinnedAnimationSpaceReady == 0u
                    ? kSpaceNotReadyBlocker
                    : 0u) |
                (defaultModelLoad.sourceSkinnedVertexAttributeReady == 0u
                    ? kSkinnedVertexAttributeBlocker
                    : 0u) |
                (playbackDiagnostics.rendererPaletteReady == 0u
                    ? kRendererPaletteNotReadyBlocker
                    : 0u) |
                (playbackDiagnostics.gpuUploadReady == 0u
                    ? kGpuUploadNotReadyBlocker
                    : 0u);
        }
        sceneDiagnostics.runtimeImportSkinnedAnimationSupportReady =
            runtimeImportHasSkinnedAnimationContent
                ? (sceneDiagnostics.runtimeImportSkinnedAnimationSupportBlockerMask == 0u
                    ? 1u
                    : 0u)
                : 0u;
        sceneDiagnostics.runtimeImportSkinnedAnimationUnsupported =
            runtimeImportHasSkinnedAnimationContent &&
            sceneDiagnostics.runtimeImportSkinnedAnimationSupportReady != 0u
                ? 0u
                : defaultModelLoad.skinnedAnimationUnsupported;
        sceneDiagnostics.runtimeImportAnimationPlaybackBlockerMask = 0u;
        if (runtimeImportHasSkinnedAnimationContent) {
            constexpr se::u32 kDiagnosticPoseOnlyBlocker = 1u << 0u;
            constexpr se::u32 kNoRuntimePlaybackBlocker = 1u << 1u;
            constexpr se::u32 kUnsupportedContentBlocker = 1u << 2u;
            constexpr se::u32 kSkinnedAnimationSpaceBlocker = 1u << 3u;
            sceneDiagnostics.runtimeImportAnimationPlaybackBlockerMask =
                (playbackDiagnostics.ready == 0u
                    ? kNoRuntimePlaybackBlocker
                    : 0u) |
                (sceneDiagnostics.runtimeImportAnimationDiagnosticPoseOnly != 0u
                    ? kDiagnosticPoseOnlyBlocker
                    : 0u) |
                (sceneDiagnostics.runtimeImportSkinnedAnimationUnsupported != 0u
                    ? kUnsupportedContentBlocker
                    : 0u) |
                (sceneDiagnostics.runtimeImportSkinnedAnimationSpaceReady == 0u
                    ? kSkinnedAnimationSpaceBlocker
                    : 0u);
        }

        se::SetBenchmarkSceneDiagnostics(sceneDiagnostics);
        app.Renderer()->SetDlssQualitySceneContentMotionSupported(
            sceneDiagnostics.runtimeImportSkinnedAnimationUnsupported == 0u &&
            (
                !runtimeImportHasSkinnedAnimationContent ||
                sceneDiagnostics.runtimeImportAnimationPlaybackReady != 0u
            )
        );
    };
    refreshRuntimeAnimationDiagnostics();
    const bool meshLodEnabled = MeshLodEnabledFromEnvironment();
    const se::f32 meshLodTargetPixelError = std::clamp(
        ReadEnvironmentF32("SE_MESH_LOD_TARGET_PIXEL_ERROR", 1.0f),
        0.25f,
        4.0f
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
        buildOptions.lodStats = context.lodStats;
        if (context.allowMeshLod) {
            buildOptions.lodOptions.enabled = meshLodEnabled;
            buildOptions.lodOptions.cameraPosition = camera.Position();
            buildOptions.lodOptions.screenHeight = static_cast<se::f32>(
                std::max(app.WindowHandle().GetHeight(), 1)
            );
            buildOptions.lodOptions.fovYRadians =
                2.0f * std::atan(camera.FovScale() * 0.5f);
            buildOptions.lodOptions.targetPixelError = meshLodTargetPixelError;
        }
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
            se::RenderQueueBuildOptions shadowBuildOptions{};
            shadowBuildOptions.shadowCastersOnly = true;
            shadowBuildOptions.cullingStats = context.shadowCullingStats;
            shadowBuildOptions.sceneIdentity = &scene;
            shadowBuildOptions.sceneMembershipRevision = scene.MembershipRevision();
            shadowBuildOptions.sceneRenderRevision = scene.RenderRevision();
            shadowBuildOptions.useSceneRevisions = true;
            context.shadowRenderQueue->BuildFromScene3D(
                app.RenderResources(),
                scene.Renderables(),
                scene.SelectedRenderable(),
                shadowBuildOptions
            );
        }
    });
    app.Renderer()->SetImGui3DContext(&scene, &camera);

    const int automaticAaToggleFrame = ReadEnvironmentInt(
        "SE_FORWARD3D_AA_AUTO_TOGGLE_FRAME",
        "SE_FORWARD3D_AUTO_F6_FRAME",
        0,
        0,
        1000000
    );
    int forwardUpdateFrame = 0;

    app.Run([&](float deltaSeconds, float elapsedSeconds) {
        ++forwardUpdateFrame;
        const bool automaticAaToggle =
            automaticAaToggleFrame > 0 &&
            forwardUpdateFrame == automaticAaToggleFrame;
        if (app.WindowHandle().WasKeyPressed(GLFW_KEY_F6) || automaticAaToggle) {
            app.Renderer()->ToggleTemporalAntialiasingMode();
            std::cout << "Forward 3D antialiasing mode: "
                << TemporalAntialiasingModeName(
                    app.Renderer()->TemporalAntialiasingMode()
                ) << std::endl;
        }

        const float clampedDeltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.05f);
        if (!cameraFreezeRequested &&
            (benchmarkCameraMotionRequested ||
                !useBenchmarkScene || showcaseCameraControlsEnabled)) {
            if (benchmarkCameraMotionRequested) {
                benchmarkCameraMotionTime = std::max(elapsedSeconds, 0.0f);
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
            if (
                !lightingShowcaseSceneRequested &&
                !shadowRegressionSceneRequested &&
                !kForcePbrModelShowcase &&
                !bridgeLights.anyApplied &&
                !reflectionCaptureCameraInvariantControl
            ) {
                ApplySceneDirectionalLight(scene, camera, previewStartupModelLighting);
            }
        }
        if (benchmarkObjectMotionRequested) {
            benchmarkObjectMotionTime = std::max(elapsedSeconds, 0.0f);
            for (std::size_t movingObjectIndex = 0;
                 movingObjectIndex < benchmarkMovingObjects.size();
                 ++movingObjectIndex) {
                const BenchmarkMovingObject& movingObject =
                    benchmarkMovingObjects[movingObjectIndex];
                if (movingObject.renderable == nullptr) {
                    continue;
                }

                if (benchmarkObjectMotionMode == BenchmarkObjectMotionMode::Articulated) {
                    ApplyBenchmarkArticulatedObjectMotion(
                        *movingObject.renderable,
                        movingObject.basePosition,
                        movingObject.baseRotationDegrees,
                        benchmarkObjectMotionTime,
                        movingObjectIndex,
                        benchmarkMovingObjects.size()
                    );
                } else {
                    ApplyBenchmarkObjectMotion(
                        *movingObject.renderable,
                        movingObject.basePosition,
                        movingObject.baseRotationDegrees,
                        benchmarkObjectMotionTime
                    );
                }
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
        sceneDiagnostics.benchmarkCameraMotionTimeSeconds =
            benchmarkCameraMotionRequested ? benchmarkCameraMotionTime : 0.0f;
        sceneDiagnostics.benchmarkObjectMotionTimeSeconds =
            benchmarkObjectMotionRequested ? benchmarkObjectMotionTime : 0.0f;
        if (EnvironmentFlagEnabled("SE_RUNTIME_ANIMATION_FREEZE") ||
            EnvironmentFlagEnabled("SE_FBX_ANIMATION_FREEZE")) {
            runtimeModelLoader.UpdateAnimationPlaybackAtTime(0.0f);
        } else if (useElapsedTimeRuntimeAnimationClock) {
            runtimeModelLoader.UpdateAnimationPlaybackAtTime(
                std::max(elapsedSeconds, 0.0f)
            );
        } else {
            runtimeModelLoader.UpdateAnimationPlayback(clampedDeltaSeconds);
        }
        refreshRuntimeAnimationDiagnostics();
        ApplyCameraToMaterial(camera, cubeMaterial);
        ApplyCameraToMaterial(camera, blueCubeMaterial);
        ApplyCameraToMaterial(camera, greenCubeMaterial);
        ApplyCameraToMaterial(camera, groundMaterial);
        ApplyCameraToMaterial(camera, defaultGroundMaterial);
        ApplyCameraToMaterial(camera, gridMaterial);
        if (!useBenchmarkScene) {
            LoadDroppedModels(app, runtimeModelLoader);
        }
        runtimeModelLoader.ForEachMaterial([&](se::VulkanMaterial& material) {
            ApplyCameraToMaterial(camera, material);
        });
        if (!sceneUpdateFreezeRequested) {
            scene.Update(clampedDeltaSeconds);
        }
    });
    TraceForwardShutdown("run_return", forwardShutdownTraceStartTime);

    app.WindowHandle().SetCursorCaptured(false);
    TraceForwardShutdown("cursor_released", forwardShutdownTraceStartTime);
    app.DestroyRenderer();
    TraceForwardShutdown("renderer_destroyed", forwardShutdownTraceStartTime);
    if (ForwardFastProcessExitEnabled()) {
        app.Device().SavePipelineCache();
        TraceForwardShutdown("fast_process_exit", forwardShutdownTraceStartTime);
        std::cout.flush();
        std::cerr.flush();
        std::exit(0);
    }

    return 0;
}
