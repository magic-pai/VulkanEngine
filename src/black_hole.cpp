#include "app/application_2d.h"
#include "platform/window.h"
#include "renderer/vulkan/material_library.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/pipeline_spec.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "renderer/vulkan/renderer.h"
#include "renderer/render_queue.h"
#include "scene/camera_2d.h"
#include "scene/camera_3d.h"
#include "scene/mesh_factory.h"
#include "scene/renderable_2d.h"
#include "scene/runtime_model_loader.h"
#include "scene/scene_2d.h"
#include "scene/scene_3d.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>

#ifndef SE_SHADER_DIR
#define SE_SHADER_DIR "shaders"
#endif

#ifndef SE_ASSET_DIR
#define SE_ASSET_DIR "assets"
#endif

namespace {

constexpr float kBlackHoleModelOcclusionRadius = 3.0f;
constexpr float kBlackHoleModelOcclusionDepthBias = 0.03f;
constexpr glm::vec3 kBlackHoleImportedModelPosition{ 8.84f, 0.15f, 1.0f };
constexpr glm::vec3 kBlackHoleImportedModelRotation{ 0.0f, -97.297f, 0.0f };
constexpr glm::vec3 kBlackHoleImportedModelScale{ 0.01f, 0.01f, 0.01f };
constexpr float kBlackHoleImportedModelMaxExtent = 15.5f;
constexpr glm::vec3 kBlackHoleDefaultCameraPosition{ 9.0f, 0.22f, 1.08f };
constexpr glm::vec3 kBlackHoleDefaultCameraForward{ -0.93f, -0.04f, -0.35f };

bool ImGuiWantsMouse() {
    return ImGui::GetCurrentContext() != nullptr &&
        ImGui::GetIO().WantCaptureMouse;
}

std::filesystem::path DefaultBlackHoleModelPath() {
    return std::filesystem::path(SE_ASSET_DIR) / "models" / "plane.glb";
}

void LoadDefaultBlackHoleModel(se::RuntimeModelLoader& loader) {
    const std::filesystem::path modelPath = DefaultBlackHoleModelPath();
    std::error_code error;
    if (!std::filesystem::is_regular_file(modelPath, error) || error) {
        std::cerr << "Black hole default model not found: "
            << modelPath.string() << std::endl;
        return;
    }

    const se::RuntimeModelLoadResult result = loader.LoadIntoScene(
        modelPath,
        kBlackHoleImportedModelPosition,
        kBlackHoleImportedModelRotation,
        kBlackHoleImportedModelMaxExtent,
        kBlackHoleImportedModelScale
    );
    if (!result.loaded) {
        std::cerr << result.message << std::endl;
    }
}

void FitRenderableToCameraView(
    se::Renderable2D& renderable,
    const se::Camera2D& camera,
    const se::Window& window
) {
    const float width = static_cast<float>(std::max(window.GetWidth(), 1));
    const float height = static_cast<float>(std::max(window.GetHeight(), 1));
    const float aspectRatio = width / height;
    const float zoom = std::max(camera.Zoom(), 0.001f);

    se::Transform2D& transform = renderable.Transform();
    transform.position = camera.Position();
    transform.rotationDegrees = 0.0f;
    transform.scale = {
        (2.0f * aspectRatio / zoom) + 0.02f,
        (2.0f / zoom) + 0.02f
    };
}

void ApplyBlackHoleCamera(
    se::Camera3D& camera,
    se::MaterialProperties& properties,
    float time
) {
    camera.SetDistance(properties.cameraControls[0]);
    camera.SetFovScale(properties.cameraControls[1]);

    properties.custom[0] = time;
    properties.cameraControls[0] = camera.Distance();
    properties.cameraControls[1] = camera.FovScale();

    const se::Camera3DState cameraState = camera.State();
    properties.cameraPosition = {
        cameraState.position.x,
        cameraState.position.y,
        cameraState.position.z,
        0.0f
    };
    properties.cameraDirection = {
        cameraState.forward.x,
        cameraState.forward.y,
        cameraState.forward.z,
        0.0f
    };
}

void ApplyCameraToMaterial(
    const se::Camera3D& camera,
    se::VulkanMaterial& material
) {
    const glm::vec3& cameraPosition = camera.Position();
    const glm::vec3& cameraDirection = camera.Forward();
    se::MaterialProperties& properties = material.Properties();
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
    const glm::vec3 lightDirection = -directionToLight;

    properties.cameraPosition = {
        cameraPosition.x,
        cameraPosition.y,
        cameraPosition.z,
        kBlackHoleModelOcclusionDepthBias
    };
    properties.cameraDirection = {
        cameraDirection.x,
        cameraDirection.y,
        cameraDirection.z,
        kBlackHoleModelOcclusionRadius
    };
    properties.custom[0] = lightDirection.x;
    properties.custom[1] = lightDirection.y;
    properties.custom[2] = lightDirection.z;
    properties.custom[3] = std::min(properties.custom[3], 0.045f);
    properties.viewControls[0] = std::max(properties.viewControls[0], 2.7f);
    properties.viewControls[1] = std::min(properties.viewControls[1], 0.42f);
}

void SyncOverlayCameraToBlackHoleCamera(
    const se::Camera3D& blackHoleCamera,
    se::Camera3D& overlayCamera
) {
    const se::Camera3DState cameraState = blackHoleCamera.State();
    overlayCamera.SetPose(cameraState.position, cameraState.forward);
    overlayCamera.SetFovScale(blackHoleCamera.FovScale());
}

void LoadDroppedModels(
    se::Application2D& app,
    se::RuntimeModelLoader& loader
) {
    for (const std::filesystem::path& path : app.WindowHandle().ConsumeDroppedPaths()) {
        const se::RuntimeModelLoadResult result = loader.LoadIntoScene(
            path,
            kBlackHoleImportedModelPosition,
            kBlackHoleImportedModelRotation,
            kBlackHoleImportedModelMaxExtent,
            kBlackHoleImportedModelScale
        );
        if (result.loaded && app.Renderer() != nullptr) {
            app.Renderer()->RefreshMaterialDescriptors();
        }
    }
}

}

int main() {
    constexpr int kDisplay1 = 1;
    const std::string vertexShaderPath = std::string(SE_SHADER_DIR) + "/triangle.vert.spv";
    const std::string fragmentShaderPath = std::string(SE_SHADER_DIR) + "/black_hole.frag.spv";
    const std::string forwardVertexShaderPath = std::string(SE_SHADER_DIR) + "/forward_3d.vert.spv";
    const std::string forwardFragmentShaderPath = std::string(SE_SHADER_DIR) + "/forward_3d.frag.spv";
    const std::string fallbackTexturePath = std::string(SE_ASSET_DIR) + "/textures/checker.ppm";
    const std::string colorMapPath = std::string(SE_ASSET_DIR) + "/blackhole/color_map.png";
    const std::string cubemapPath = std::string(SE_ASSET_DIR) + "/blackhole/skybox_nebula_dark";

    se::Application2D app(
        1280,
        720,
        "SelfEngine Black Hole",
        kDisplay1,
        se::PipelineSpec::Default2D(vertexShaderPath, fragmentShaderPath)
    );

    se::MeshData2D quadMeshData = se::MeshFactory::CreateQuad();
    se::VulkanMesh fullscreenQuad(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        std::move(quadMeshData.vertices),
        std::move(quadMeshData.indices)
    );

    se::MaterialProperties blackHoleProperties{};
    blackHoleProperties.baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
    blackHoleProperties.custom = {
        0.0f,
        1.0f,
        0.25f,
        0.55f
    };
    blackHoleProperties.viewControls = {
        1.0f,
        1.05f,
        0.12f,
        0.0f
    };
    blackHoleProperties.cameraControls = {
        glm::length(kBlackHoleDefaultCameraPosition),
        1.0f,
        0.8f,
        0.5f
    };
    blackHoleProperties.textureMix = 0.7f;
    se::VulkanMaterial& blackHoleMaterial = app.MaterialLibrary().CreateBlackHoleMaterial(
        "BlackHoleMaterial",
        fallbackTexturePath,
        colorMapPath,
        cubemapPath,
        blackHoleProperties
    );

    app.RenderResources().RegisterMesh("FullscreenQuad", fullscreenQuad);
    app.RenderResources().RegisterMaterial("BlackHoleMaterial", blackHoleMaterial);

    se::Renderable2D& blackHole = app.Scene().CreateRenderable(
        "Black Hole",
        "FullscreenQuad",
        "BlackHoleMaterial"
    );
    blackHole.Transform().animateRotation = false;
    blackHole.SetHighlightEnabled(false);

    se::Camera3D blackHoleCamera{};
    blackHoleCamera.SetPose(kBlackHoleDefaultCameraPosition, kBlackHoleDefaultCameraForward);
    se::Camera3D overlayCamera{};
    SyncOverlayCameraToBlackHoleCamera(blackHoleCamera, overlayCamera);
    se::Scene3D overlayScene;
    se::RuntimeModelLoader runtimeModelLoader(
        app.Device(),
        app.PhysicalDevice(),
        app.CommandPool(),
        app.MaterialLibrary(),
        app.RenderResources(),
        overlayScene,
        fallbackTexturePath
    );
    LoadDefaultBlackHoleModel(runtimeModelLoader);

    app.CreateRenderer();
    SE_ASSERT(app.Renderer() != nullptr, "Black hole demo needs a renderer");
    app.Renderer()->SetOverlay3DContext(
        &overlayScene,
        &overlayCamera,
        se::PipelineSpec::DefaultForward3D(forwardVertexShaderPath, forwardFragmentShaderPath)
    );
    app.Renderer()->SetImGui3DContext(&overlayScene, &blackHoleCamera);
    app.Run([&](float deltaSeconds, float elapsedSeconds) {
        const float clampedDeltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.05f);
        blackHoleCamera.Update(app.WindowHandle(), clampedDeltaSeconds, ImGuiWantsMouse());
        SyncOverlayCameraToBlackHoleCamera(blackHoleCamera, overlayCamera);
        ApplyBlackHoleCamera(
            blackHoleCamera,
            blackHoleMaterial.Properties(),
            elapsedSeconds
        );
        LoadDroppedModels(app, runtimeModelLoader);
        runtimeModelLoader.ForEachMaterial([&](se::VulkanMaterial& material) {
            ApplyCameraToMaterial(overlayCamera, material);
        });
        overlayScene.Update(clampedDeltaSeconds);
        FitRenderableToCameraView(blackHole, app.Camera(), app.WindowHandle());
    });

    app.WindowHandle().SetCursorCaptured(false);

    return 0;
}
