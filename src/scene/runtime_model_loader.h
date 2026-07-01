#pragma once

#include "assets/model_importer.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/mesh_lod.h"
#include "scene/scene_3d.h"

#include <functional>
#include <unordered_map>

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanMaterialLibrary;
class VulkanPhysicalDevice;
class VulkanRenderResources2D;

struct RuntimeModelLoadResult {
    bool loaded = false;
    std::string message;
    bool cacheHit = false;
};

class RuntimeModelLoader {
public:
    RuntimeModelLoader(
        VulkanDevice& device,
        VulkanPhysicalDevice& physicalDevice,
        VulkanCommandPool& commandPool,
        VulkanMaterialLibrary& materialLibrary,
        VulkanRenderResources2D& renderResources,
        Scene3D& scene,
        std::string fallbackTexturePath
    );

    RuntimeModelLoadResult LoadIntoScene(
        const std::filesystem::path& modelPath,
        glm::vec3 position,
        glm::vec3 rotationDegrees = glm::vec3(0.0f),
        // Use <= 0 to preserve imported scene scale for bridge/exported content.
        f32 targetMaxExtent = 2.1f,
        glm::vec3 scale = glm::vec3(1.0f)
    );
    void ForEachMaterial(const std::function<void(VulkanMaterial&)>& visitor) const;

private:
    struct LoadedRuntimeModel {
        std::vector<std::unique_ptr<VulkanMesh>> meshes;
        std::vector<VulkanMaterial*> materials;
        std::vector<std::string> meshIds;
        std::vector<std::string> materialIds;
        std::vector<MeshLodChain> lodChains;
    };

    VulkanDevice& m_Device;
    VulkanPhysicalDevice& m_PhysicalDevice;
    VulkanCommandPool& m_CommandPool;
    VulkanMaterialLibrary& m_MaterialLibrary;
    VulkanRenderResources2D& m_RenderResources;
    Scene3D& m_Scene;
    std::string m_FallbackTexturePath;
    std::vector<std::unique_ptr<LoadedRuntimeModel>> m_LoadedModels;
    std::unordered_map<std::string, std::size_t> m_ModelCache;
    u32 m_NextModelId = 0;
};

}
