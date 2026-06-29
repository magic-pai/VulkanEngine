#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <unordered_map>

namespace se {

class VulkanMaterial;
class VulkanMesh;

class VulkanRenderResources2D {
public:
    void RegisterMesh(std::string id, const VulkanMesh& mesh);
    void RegisterMaterial(std::string id, VulkanMaterial& material);

    const VulkanMesh& Mesh(std::string_view id) const;
    VulkanMaterial& Material(std::string_view id) const;
    bool ContainsMesh(std::string_view id) const;
    bool ContainsMaterial(std::string_view id) const;

    std::vector<const VulkanMaterial*> Materials() const;

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

    std::vector<MeshEntry> m_Meshes;
    std::vector<MaterialEntry> m_Materials;
    std::unordered_map<std::string, std::size_t, StringViewHash, std::equal_to<>> m_MeshIndexById;
    std::unordered_map<std::string, std::size_t, StringViewHash, std::equal_to<>> m_MaterialIndexById;
};

}
