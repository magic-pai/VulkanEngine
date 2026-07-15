#pragma once

#include "renderer/vulkan/mesh_lod.h"
#include "renderer/vulkan/vulkan_common.h"

#include <glm/glm.hpp>
#include <unordered_map>

namespace se {

class VulkanMaterial;
class VulkanMesh;
class VulkanDevice;
class VulkanPhysicalDevice;

class VulkanRenderResources2D {
public:
    struct BonePaletteResource {
        std::vector<glm::mat4> previousPalette;
        std::vector<glm::mat4> currentPalette;
        u32 changedEntryCount = 0;
        u32 ready = 0;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        u32 descriptorSetReady = 0;
        u32 descriptorSetIndex = 0;
        u32 descriptorBinding = 0;
        u32 descriptorRangeBytes = 0;
    };

    void RegisterMesh(std::string id, const VulkanMesh& mesh);
    void RegisterMaterial(std::string id, VulkanMaterial& material);
    void RegisterBonePalette(std::string id, BonePaletteResource palette);
    void UpdateBonePalette(
        std::string_view id,
        std::vector<glm::mat4> previousPalette,
        std::vector<glm::mat4> currentPalette,
        u32 changedEntryCount,
        u32 ready
    );
    void UpdateBonePaletteDescriptor(
        std::string_view id,
        VkDescriptorSet descriptorSet,
        u32 descriptorSetReady,
        u32 descriptorSetIndex,
        u32 descriptorBinding,
        u32 descriptorRangeBytes
    );

    const VulkanMesh& Mesh(std::string_view id) const;
    VulkanMaterial& Material(std::string_view id) const;
    const BonePaletteResource& BonePalette(std::string_view id) const;
    bool ContainsMesh(std::string_view id) const;
    bool ContainsMaterial(std::string_view id) const;
    bool ContainsBonePalette(std::string_view id) const;

    std::vector<const VulkanMaterial*> Materials() const;
    bool RecreateMaterialSamplers(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        f32 mipLodBias
    ) const;

    // LOD support
    void RegisterMeshLodChain(std::string_view baseMeshId, const MeshLodChain& chain);
    u32 SelectLod(std::string_view meshId, f32 screenFraction, u32 prevLod) const;
    bool HasLodChain(std::string_view baseMeshId) const;

private:
    struct StringViewHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view value) const noexcept;
        std::size_t operator()(const std::string& value) const noexcept;
        std::size_t operator()(const char* value) const noexcept;
    };

    struct MeshEntry {
        std::string id;
        const VulkanMesh* mesh = nullptr;
    };

    struct MaterialEntry {
        std::string id;
        VulkanMaterial* material = nullptr;
    };

    struct BonePaletteEntry {
        std::string id;
        BonePaletteResource palette;
    };

    std::vector<MeshEntry> m_Meshes;
    std::vector<MaterialEntry> m_Materials;
    std::vector<BonePaletteEntry> m_BonePalettes;
    std::unordered_map<std::string, std::size_t, StringViewHash, std::equal_to<>> m_MeshIndexById;
    std::unordered_map<std::string, std::size_t, StringViewHash, std::equal_to<>> m_MaterialIndexById;
    std::unordered_map<std::string, std::size_t, StringViewHash, std::equal_to<>> m_BonePaletteIndexById;
    std::unordered_map<std::string, MeshLodChain, StringViewHash, std::equal_to<>> m_LodChains;
};

}
