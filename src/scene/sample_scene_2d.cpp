#include "scene/sample_scene_2d.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/material_library.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "scene/mesh_factory.h"
#include "scene/renderable_2d.h"
#include "scene/scene_2d.h"

#include <utility>

namespace se {

namespace {

std::string AssetPath(const std::string& assetDirectory, std::string_view relativePath) {
    return assetDirectory + "/" + std::string(relativePath);
}

VulkanMesh CreateQuadMesh(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool
) {
    MeshData2D meshData = MeshFactory::CreateQuad();
    return VulkanMesh(
        device,
        physicalDevice,
        commandPool,
        std::move(meshData.vertices),
        std::move(meshData.indices)
    );
}

}

SampleScene2D::SampleScene2D(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    VulkanMaterialLibrary& materialLibrary,
    VulkanRenderResources2D& renderResources,
    Scene2D& scene,
    std::string assetDirectory
) : m_QuadMesh(CreateQuadMesh(device, physicalDevice, commandPool)) {
    const std::string playerTexturePath = AssetPath(assetDirectory, "textures/player.png");
    const std::string checkerTexturePath = AssetPath(assetDirectory, "textures/checker.ppm");

    VulkanMaterial& playerMaterial = materialLibrary.Create(
        "PlayerMaterial",
        playerTexturePath
    );

    MaterialProperties leftMarkerProperties{};
    leftMarkerProperties.baseColorFactor = { 0.55f, 0.85f, 1.0f, 1.0f };
    VulkanMaterial& leftMarkerMaterial = materialLibrary.Create(
        "LeftMarkerMaterial",
        checkerTexturePath,
        leftMarkerProperties
    );

    MaterialProperties rightMarkerProperties{};
    rightMarkerProperties.baseColorFactor = { 1.0f, 0.65f, 0.65f, 1.0f };
    rightMarkerProperties.textureMix = 0.85f;
    VulkanMaterial& rightMarkerMaterial = materialLibrary.Create(
        "RightMarkerMaterial",
        playerTexturePath,
        rightMarkerProperties
    );

    renderResources.RegisterMesh("Quad", m_QuadMesh);
    renderResources.RegisterMaterial("PlayerMaterial", playerMaterial);
    renderResources.RegisterMaterial("LeftMarkerMaterial", leftMarkerMaterial);
    renderResources.RegisterMaterial("RightMarkerMaterial", rightMarkerMaterial);

    Renderable2D& player = scene.CreateRenderable("Player", "Quad", "PlayerMaterial");
    player.Transform().position = { 0.0f, 0.0f };
    player.Transform().scale = { 0.9f, 0.9f };
    player.SetDrawOrder(10);

    Renderable2D& leftMarker = scene.CreateRenderable(
        "Left Marker",
        "Quad",
        "LeftMarkerMaterial"
    );
    leftMarker.Transform().position = { -0.85f, -0.35f };
    leftMarker.Transform().scale = { 0.35f, 0.35f };
    leftMarker.Transform().rotationDegrees = -20.0f;
    leftMarker.Transform().animateRotation = false;
    leftMarker.SetDrawOrder(0);

    Renderable2D& rightMarker = scene.CreateRenderable(
        "Right Marker",
        "Quad",
        "RightMarkerMaterial"
    );
    rightMarker.Transform().position = { 0.85f, 0.35f };
    rightMarker.Transform().scale = { 0.45f, 0.45f };
    rightMarker.Transform().rotationDegrees = 20.0f;
    rightMarker.Transform().animateRotation = false;
    rightMarker.SetDrawOrder(20);
}

}
