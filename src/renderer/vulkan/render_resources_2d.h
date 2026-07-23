#pragma once

#include "renderer/vulkan/mesh_lod.h"
#include "renderer/vulkan/vulkan_common.h"

#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>

namespace se {

class VulkanMaterial;
class VulkanMesh;
class VulkanDevice;
class VulkanPhysicalDevice;

struct MeshLodResidencyStats {
    u32 chainCount = 0;
    u32 levelCount = 0;
    u64 sourceVertexBytes = 0;
    u64 sourceIndexBytes = 0;
    u64 residentVertexBytes = 0;
    u64 residentIndexBytes = 0;
    u64 extraVertexBytes = 0;
    u64 extraIndexBytes = 0;
    f32 maxSimplificationError = 0.0f;
};

class VulkanRenderResources2D {
public:
    struct BonePaletteResource {
        std::vector<glm::mat4> previousPalette;
        std::vector<glm::mat4> currentPalette;
        u32 changedEntryCount = 0;
        u32 ready = 0;
        u64 revision = 1u;
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

    void RegisterMeshLodChain(std::string baseMeshId, MeshLodChain chain);
    MeshLodSelection SelectLod(
        std::string_view meshId,
        f32 projectedDiameterPixels,
        u32 previousLod,
        bool previousLodValid,
        f32 targetPixelError,
        f32 hysteresisRatio
    ) const;
    const MeshLodChain* LodChain(std::string_view baseMeshId) const;
    bool HasLodChain(std::string_view baseMeshId) const;
    const MeshLodResidencyStats& LodResidencyStats() const;

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
    std::unordered_set<const VulkanMesh*> m_RegisteredLodResidencyMeshes;
    MeshLodResidencyStats m_LodResidencyStats{};
};

}
