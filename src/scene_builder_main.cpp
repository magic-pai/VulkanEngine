#include "app/application.h"
#include "app/scene_builder_runtime_monitor.h"
#include "renderer/render_queue.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/pipeline_spec.h"
#include "renderer/vulkan/renderer.h"
#include "renderer/vulkan/scene_builder_gizmo.h"
#include "scene/camera_3d.h"
#include "scene/mesh_factory.h"
#include "scene/runtime_model_loader.h"
#include "scene/scene_3d.h"
#include "scene/scene_builder.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_inverse.hpp>

#ifndef SE_SHADER_DIR
#define SE_SHADER_DIR "shaders"
#endif

#ifndef SE_ASSET_DIR
#define SE_ASSET_DIR "assets"
#endif

namespace {

struct PickRay {
    glm::vec3 origin{ 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
};

struct PickClickState {
    bool trackingClick = false;
    se::f32 heldSeconds = 0.0f;
    std::array<se::f64, 2> pressPosition{};
};

bool ImGuiWantsMouse() {
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiWantsKeyboard() {
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
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

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

bool EnvironmentFlagEnabled(const char* name) {
    const std::string value = LowerAscii(ReadEnvironmentString(name));
    return value == "1" || value == "true" || value == "on" || value == "yes";
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

int ReadEnvironmentInt(const char* name, int fallback, int minimum, int maximum) {
    const std::string value = ReadEnvironmentString(name);
    return value.empty()
        ? fallback
        : std::clamp(std::atoi(value.c_str()), minimum, maximum);
}

se::f32 ReadEnvironmentFloat(
    const char* name,
    se::f32 fallback,
    se::f32 minimum,
    se::f32 maximum
) {
    const std::string value = ReadEnvironmentString(name);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const se::f32 parsed = std::strtof(value.c_str(), &end);
    return end == value.c_str()
        ? fallback
        : std::clamp(parsed, minimum, maximum);
}

se::RendererTemporalAntialiasingMode StartupAntialiasingMode() {
    const std::string value = LowerAscii(ReadEnvironmentString("SE_FORWARD3D_AA_MODE"));
    if (value == "off" || value == "none" || value == "0") {
        return se::RendererTemporalAntialiasingMode::Off;
    }
    if (value == "taa" || value == "native-taa") {
        return se::RendererTemporalAntialiasingMode::NativeTaa;
    }
    if (value == "dlaa" || value == "dlss-dlaa") {
        return se::RendererTemporalAntialiasingMode::DlssDlaa;
    }
    if (value == "sr-quality" || value == "dlss-quality") {
        return se::RendererTemporalAntialiasingMode::DlssSrQuality;
    }
    if (value == "sr-balanced" || value == "dlss-balanced") {
        return se::RendererTemporalAntialiasingMode::DlssSrBalanced;
    }
    return se::RendererTemporalAntialiasingMode::DlssSrPerformance;
}

void ConfigureProcessDefaults(se::RendererTemporalAntialiasingMode mode) {
    SetEnvironmentDefault("SE_ENABLE_DLSS_VULKAN_EXTENSIONS", "1");
    SetEnvironmentDefault("SE_UPSCALER_PLUGIN", "dlss");
    SetEnvironmentDefault("SE_DLSS_PRESET", "l");
    SetEnvironmentDefault("SE_DLSS_PRESENT", "1");
    SetEnvironmentDefault("SE_DLSS_SHARPNESS", "0.0");
    SetEnvironmentDefault("SE_TAA", "1");
    SetEnvironmentDefault("SE_TEMPORAL_JITTER", "1");
    SetEnvironmentDefault("SE_TAA_APPLY_JITTER", "1");
    SetEnvironmentDefault("SE_RENDER_SCALE_APPLY", "1");
    SetEnvironmentDefault("SE_TEMPORAL_VELOCITY_JITTER_POLICY", "jittered");
    SetEnvironmentDefault("SE_HYBRID_REFLECTIONS_RT", "1");
    SetEnvironmentDefault("SE_SSR", "0");
    SetEnvironmentDefault("SE_SSR_BACKEND", "selfengine");

    switch (mode) {
        case se::RendererTemporalAntialiasingMode::DlssSrQuality:
            SetEnvironmentDefault("SE_DLSS_QUALITY", "quality");
            SetEnvironmentDefault("SE_RENDER_SCALE", "0.666667");
            SetEnvironmentDefault("SE_TEXTURE_MIP_LOD_BIAS", "-1.58496");
            break;
        case se::RendererTemporalAntialiasingMode::DlssSrBalanced:
            SetEnvironmentDefault("SE_DLSS_QUALITY", "balanced");
            SetEnvironmentDefault("SE_RENDER_SCALE", "0.58");
            SetEnvironmentDefault("SE_TEXTURE_MIP_LOD_BIAS", "-1.78588");
            break;
        case se::RendererTemporalAntialiasingMode::DlssSrPerformance:
            SetEnvironmentDefault("SE_DLSS_QUALITY", "performance");
            SetEnvironmentDefault("SE_RENDER_SCALE", "0.5");
            SetEnvironmentDefault("SE_TEXTURE_MIP_LOD_BIAS", "-2.0");
            break;
        case se::RendererTemporalAntialiasingMode::DlssDlaa:
            SetEnvironmentDefault("SE_DLSS_QUALITY", "dlaa");
            SetEnvironmentDefault("SE_RENDER_SCALE", "1.0");
            break;
        default:
            break;
    }
}

PickRay CursorPickRay(const se::Window& window, const se::Camera3D& camera) {
    const std::array<se::f64, 2> cursor = window.CursorPosition();
    const std::array<int, 2> logicalSize = window.WindowSize();
    const se::f32 width = static_cast<se::f32>(std::max(logicalSize[0], 1));
    const se::f32 height = static_cast<se::f32>(std::max(logicalSize[1], 1));
    const se::f32 aspect = static_cast<se::f32>(std::max(window.GetWidth(), 1)) /
        static_cast<se::f32>(std::max(window.GetHeight(), 1));
    const se::f32 ndcX = static_cast<se::f32>(cursor[0]) / width * 2.0f - 1.0f;
    const se::f32 ndcY = static_cast<se::f32>(cursor[1]) / height * 2.0f - 1.0f;
    const glm::mat4 inverseViewProjection = glm::inverse(
        camera.ProjectionMatrix(aspect) * camera.ViewMatrix()
    );
    const glm::vec4 nearH = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farH = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 nearPosition = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPosition = glm::vec3(farH) / farH.w;
    return PickRay{ nearPosition, glm::normalize(farPosition - nearPosition) };
}

void HandleScenePicking(
    se::Window& window,
    const se::Camera3D& camera,
    se::SceneBuilder& builder,
    se::f32 deltaSeconds,
    PickClickState& clickState,
    bool gizmoCapturesMouse
) {
    constexpr se::f32 kMaxClickSeconds = 0.22f;
    constexpr se::f64 kMaxClickMovementSquared = 36.0;
    const bool editorOwnsMouse = ImGuiWantsMouse() || gizmoCapturesMouse;

    if (window.WasLeftMousePressed()) {
        clickState.trackingClick = !editorOwnsMouse;
        clickState.heldSeconds = 0.0f;
        clickState.pressPosition = window.LeftMousePressPosition();
    }
    if (window.IsLeftMouseDown() && clickState.trackingClick) {
        clickState.heldSeconds += std::max(deltaSeconds, 0.0f);
    }
    if (!window.WasLeftMouseReleased()) {
        return;
    }

    if (clickState.trackingClick && !editorOwnsMouse) {
        const std::array<se::f64, 2> release = window.LeftMouseReleasePosition();
        const se::f64 deltaX = release[0] - clickState.pressPosition[0];
        const se::f64 deltaY = release[1] - clickState.pressPosition[1];
        if (clickState.heldSeconds <= kMaxClickSeconds &&
            deltaX * deltaX + deltaY * deltaY <= kMaxClickMovementSquared) {
            const PickRay ray = CursorPickRay(window, camera);
            if (!se::SceneBuilderGizmo::SelectLightIconAtCursor(builder, camera, window)) {
                builder.SelectAlongRay(ray.origin, ray.direction);
            }
        }
    }
    clickState = {};
}

glm::vec3 CameraKeyLightDirection(const se::Camera3D& camera) {
    const glm::vec3 worldUp{ 0.0f, 1.0f, 0.0f };
    glm::vec3 right = glm::cross(camera.Forward(), worldUp);
    right = glm::dot(right, right) < 0.0001f
        ? glm::vec3(1.0f, 0.0f, 0.0f)
        : glm::normalize(right);
    const glm::vec3 up = glm::normalize(glm::cross(right, camera.Forward()));
    return -glm::normalize(-camera.Forward() + up * 0.55f - right * 0.25f);
}

void ApplyCameraToMaterial(const se::Camera3D& camera, se::VulkanMaterial& material) {
    se::MaterialProperties& properties = material.Properties();
    const glm::vec3 position = camera.Position();
    const glm::vec3 direction = camera.Forward();
    const glm::vec3 lightDirection = CameraKeyLightDirection(camera);
    properties.cameraPosition = { position.x, position.y, position.z, 0.0f };
    properties.cameraDirection = { direction.x, direction.y, direction.z, 0.0f };
    properties.custom[0] = lightDirection.x;
    properties.custom[1] = lightDirection.y;
    properties.custom[2] = lightDirection.z;
}

void ApplySceneBuilderRendererSettings(se::VulkanRenderer& renderer) {
    se::VulkanShadowSettings& shadow = renderer.ShadowSettings();
    se::ApplyShadowQualityPreset(shadow, se::VulkanShadowQuality::Medium);
    shadow.cascadeMaxDistance = 45.0f;
    shadow.directionalCoverageMode =
        se::VulkanDirectionalShadowCoverageMode::SceneBounds;
    shadow.ssrStrength = 0.0f;
    shadow.ssrRayLength = 0.0f;
    shadow.ssrStepCount = 0u;
    shadow.ssrFidelityFxBackendRequested = false;
    shadow.rayQueryReflectionCarrierEnabled =
        EnvironmentFlagEnabled("SE_HYBRID_REFLECTIONS_RT") &&
        !EnvironmentFlagEnabled("SE_HYBRID_REFLECTIONS_RT_OFF");
    shadow.reflectionProbeFallbackEnabled = true;
    shadow.globalIblCubemapEnabled = true;
    shadow.reflectionProbeCubemapEnabled = true;
}

void RegisterPrimitiveMeshes(
    se::Application& app,
    std::vector<std::unique_ptr<se::VulkanMesh>>& meshes
) {
    const auto registerMesh = [&app, &meshes](std::string id, se::MeshData3D data) {
        auto mesh = std::make_unique<se::VulkanMesh>(
            app.Device(),
            app.PhysicalDevice(),
            app.CommandPool(),
            std::move(data.vertices),
            std::move(data.indices)
        );
        app.RenderResources().RegisterMesh(std::move(id), *mesh);
        meshes.push_back(std::move(mesh));
    };
    registerMesh("Cube", se::MeshFactory::CreateCube());
    registerMesh("Plane", se::MeshFactory::CreatePlane());
    registerMesh("Sphere", se::MeshFactory::CreateUvSphere(192, 96));
    registerMesh("Cone", se::MeshFactory::CreateCone(64));
}

void InstallImportedAssetCreator(
    se::SceneBuilder& builder,
    se::RuntimeModelLoader& loader,
    se::Scene3D& scene
) {
    builder.SetImportedAssetCreator(
        [&loader, &scene](
            se::SceneBuilderPrimitive primitive,
            const se::SceneBuilderObjectEdit& initialEdit
        ) {
            se::SceneBuilderImportedAsset imported{};
            const std::string_view assetPath =
                se::SceneBuilder::PrimitiveAssetPath(primitive);
            if (assetPath.empty()) {
                imported.message = "The requested editor asset is not registered.";
                return imported;
            }
            const std::size_t firstRenderable = scene.Count();
            const se::RuntimeModelLoadResult result = loader.LoadIntoScene(
                std::filesystem::path(std::string(assetPath)),
                initialEdit.position,
                initialEdit.rotationDegrees,
                2.1f,
                initialEdit.scale
            );
            imported.loaded = result.loaded;
            imported.message = result.message;
            imported.materialResourcesChanged = result.loaded && !result.cacheHit;
            if (!result.loaded) {
                return imported;
            }
            const std::span<se::Renderable3D* const> renderables = scene.Renderables();
            for (std::size_t index = firstRenderable; index < renderables.size(); ++index) {
                if (renderables[index] != nullptr) {
                    imported.renderIdentities.push_back(
                        renderables[index]->RenderIdentity()
                    );
                }
            }
            imported.loaded = !imported.renderIdentities.empty();
            if (!imported.loaded) {
                imported.message = "The model importer created no scene renderables.";
            }
            return imported;
        }
    );
}

void LoadSavedScene(se::SceneBuilder& builder, se::Camera3D& camera) {
    const std::filesystem::path path = se::SceneBuilder::DefaultDocumentPath();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return;
    }
    std::optional<se::Camera3DState> cameraState;
    const bool loaded = builder.LoadFromFile(path, cameraState);
    const bool cameraRestored = loaded && cameraState.has_value() &&
        camera.RestoreState(*cameraState);
    std::cout << (loaded ? "Scene loaded: " : "Scene load failed: ")
        << builder.LastDocumentStatus();
    if (loaded && cameraState.has_value()) {
        std::cout << (cameraRestored ? " (camera restored)" : " (camera restore failed)");
    }
    std::cout << std::endl;
}

void ConfigureRenderQueues(
    se::Application& app,
    se::Scene3D& scene,
    se::Camera3D& camera
) {
    const bool lodEnabled = !EnvironmentFlagEnabled("SE_MESH_LOD_OFF");
    const se::f32 targetPixelError = ReadEnvironmentFloat(
        "SE_MESH_LOD_TARGET_PIXEL_ERROR",
        1.0f,
        0.25f,
        4.0f
    );
    app.Renderer()->SetFrameMatricesProvider([&camera](se::f32 aspectRatio) {
        return se::FrameMatrices{
            camera.ViewMatrix(),
            camera.ProjectionMatrix(aspectRatio)
        };
    });
    app.Renderer()->SetRenderQueueBuilder(
        [&app, &scene, &camera, lodEnabled, targetPixelError](
            se::RenderQueue& queue,
            const se::RenderQueueContext& context
        ) {
            se::RenderQueueBuildOptions options{};
            options.frustum = context.frustum;
            options.cullingStats = context.cullingStats;
            options.cacheStats = context.cacheStats;
            options.lodStats = context.lodStats;
            if (context.allowMeshLod) {
                options.lodOptions.enabled = lodEnabled;
                options.lodOptions.cameraPosition = camera.Position();
                options.lodOptions.screenHeight = static_cast<se::f32>(
                    std::max(app.WindowHandle().GetHeight(), 1)
                );
                options.lodOptions.fovYRadians =
                    2.0f * std::atan(camera.FovScale() * 0.5f);
                options.lodOptions.targetPixelError = targetPixelError;
            }
            options.sceneIdentity = &scene;
            options.sceneMembershipRevision = scene.MembershipRevision();
            options.sceneRenderRevision = scene.RenderRevision();
            options.useSceneRevisions = true;
            queue.BuildFromScene3D(
                app.RenderResources(),
                scene.Renderables(),
                scene.SelectedRenderable(),
                options
            );
            if (context.shadowRenderQueue == nullptr) {
                return;
            }
            se::RenderQueueBuildOptions shadowOptions{};
            shadowOptions.shadowCastersOnly = true;
            shadowOptions.cullingStats = context.shadowCullingStats;
            shadowOptions.sceneIdentity = &scene;
            shadowOptions.sceneMembershipRevision = scene.MembershipRevision();
            shadowOptions.sceneRenderRevision = scene.RenderRevision();
            shadowOptions.useSceneRevisions = true;
            context.shadowRenderQueue->BuildFromScene3D(
                app.RenderResources(),
                scene.Renderables(),
                scene.SelectedRenderable(),
                shadowOptions
            );
        }
    );
}

int RunSceneBuilder() {
    const se::RendererTemporalAntialiasingMode antialiasingMode =
        StartupAntialiasingMode();
    ConfigureProcessDefaults(antialiasingMode);

    const std::string vertexShader = std::string(SE_SHADER_DIR) + "/forward_3d.vert.spv";
    const std::string fragmentShader = std::string(SE_SHADER_DIR) + "/forward_3d.frag.spv";
    const std::string fallbackTexture = std::string(SE_ASSET_DIR) + "/textures/checker.ppm";
    se::Application app(
        ReadEnvironmentInt("SE_WINDOW_WIDTH", 1280, 320, 7680),
        ReadEnvironmentInt("SE_WINDOW_HEIGHT", 720, 240, 4320),
        "SelfEngine Scene Builder",
        1,
        se::PipelineSpec::DefaultForward3D(vertexShader, fragmentShader)
    );

    std::vector<std::unique_ptr<se::VulkanMesh>> primitiveMeshes;
    RegisterPrimitiveMeshes(app, primitiveMeshes);

    constexpr std::array<se::u8, 4> kFallbackTexel{ 255, 255, 255, 255 };
    se::VulkanMaterial& fallbackMaterial = app.MaterialLibrary().Create(
        "SceneBuilderRuntimeFallback",
        se::VulkanTexturePixels{
            std::span<const se::u8>(kFallbackTexel.data(), kFallbackTexel.size()),
            1,
            1
        },
        se::MaterialProperties{},
        false,
        false
    );
    app.RenderResources().RegisterMaterial(
        "SceneBuilderRuntimeFallback",
        fallbackMaterial
    );

    se::Scene3D scene;
    scene.SetPrimaryDirectionalLight(
        "Scene Builder Key Light",
        { -0.45f, -0.82f, -0.35f },
        2.35f,
        0.16f,
        0.32f
    );
    se::Camera3D camera;
    camera.SetPose({ 0.0f, 1.85f, 7.2f }, { 0.0f, -0.22f, -1.0f });
    camera.SetFovScale(0.72f);
    camera.SetMoveSpeed(3.4f);
    camera.SetOrbitInputEnabled(false);

    se::RuntimeModelLoader modelLoader(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        app.MaterialLibrary(),
        app.RenderResources(),
        scene,
        fallbackTexture
    );
    se::SceneBuilder builder(app.MaterialLibrary(), app.RenderResources(), scene);
    InstallImportedAssetCreator(builder, modelLoader, scene);
    LoadSavedScene(builder, camera);

    app.CreateRenderer();
    SE_ASSERT(app.Renderer() != nullptr, "Scene Builder requires a renderer");
    ApplySceneBuilderRendererSettings(*app.Renderer());
    app.Renderer()->SetTemporalAntialiasingMode(antialiasingMode);
    app.Renderer()->SetDlssQualitySceneContentMotionSupported(true);
    app.Renderer()->SetImGui3DContext(&scene, &camera);
    app.Renderer()->SetImGuiSceneBuilder(&builder);
    app.Renderer()->ApplyEnvironmentRenderSettings();
    ConfigureRenderQueues(app, scene, camera);

    builder.SetMaterialsChangedCallback([&app]() {
        if (app.Renderer() != nullptr) {
            app.Renderer()->RefreshMaterialDescriptors();
        }
    });

    se::SceneBuilderRuntimeMonitor runtimeMonitor;
    app.SetFrameCompletedCallback(
        [&app, &camera, &scene, &builder, &runtimeMonitor](
            se::u32 frameIndex,
            float elapsedSeconds,
            const se::RendererStats& stats,
            const se::RendererFrameMonitorSnapshot& monitor
        ) {
            runtimeMonitor.RecordFrame(
                frameIndex,
                elapsedSeconds,
                camera,
                scene,
                builder,
                app.RenderResources(),
                stats,
                monitor
            );
        }
    );

    PickClickState clickState{};
    app.Run([&](float deltaSeconds, float) {
        se::Window& window = app.WindowHandle();
        const se::f32 clampedDelta = std::clamp(deltaSeconds, 0.0f, 0.05f);

        if (window.WasKeyPressed(GLFW_KEY_F6)) {
            app.Renderer()->ToggleTemporalAntialiasingMode();
        }
        if (!ImGuiWantsKeyboard() && window.WasKeyPressed(GLFW_KEY_DELETE)) {
            builder.DeleteSelectedEntity();
        }
        if (!ImGuiWantsKeyboard() && !window.IsRightMouseDown()) {
            if (window.WasKeyPressed(GLFW_KEY_Q)) {
                app.Renderer()->SetSceneBuilderGizmoModeFromShortcut(
                    se::SceneBuilderGizmoMode::Translate
                );
            } else if (window.WasKeyPressed(GLFW_KEY_W)) {
                app.Renderer()->SetSceneBuilderGizmoModeFromShortcut(
                    se::SceneBuilderGizmoMode::Rotate
                );
            } else if (window.WasKeyPressed(GLFW_KEY_E)) {
                app.Renderer()->SetSceneBuilderGizmoModeFromShortcut(
                    se::SceneBuilderGizmoMode::Scale
                );
            }
        }

        const bool gizmoCapturesMouse =
            app.Renderer()->SceneBuilderGizmoCapturesMouse();
        const bool cameraInputBlocked = se::SceneBuilderGizmo::BlocksCameraInput(
            ImGuiWantsMouse(),
            window.IsRightMouseDown(),
            gizmoCapturesMouse
        );
        camera.Update(window, clampedDelta, cameraInputBlocked);
        HandleScenePicking(
            window,
            camera,
            builder,
            clampedDelta,
            clickState,
            gizmoCapturesMouse
        );
        modelLoader.UpdateAnimationPlayback(clampedDelta);
        modelLoader.ForEachMaterial([&camera](se::VulkanMaterial& material) {
            ApplyCameraToMaterial(camera, material);
        });
        scene.Update(clampedDelta);
    });

    app.WindowHandle().SetCursorCaptured(false);
    app.DestroyRenderer();
    return 0;
}

}

int main() {
    try {
        return RunSceneBuilder();
    } catch (const std::exception& error) {
        std::cerr << "SelfEngine Scene Builder fatal error: " << error.what()
            << std::endl;
    } catch (...) {
        std::cerr << "SelfEngine Scene Builder fatal error: unknown exception"
            << std::endl;
    }
    return EXIT_FAILURE;
}
