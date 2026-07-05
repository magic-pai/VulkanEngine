#include "renderer/vulkan/render_resources_2d.h"

#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"

#include <functional>
#include <utility>

namespace se {

std::size_t VulkanRenderResources2D::StringViewHash::operator()(
    std::string_view value
) const noexcept {
    return std::hash<std::string_view>{}(value);
}

std::size_t VulkanRenderResources2D::StringViewHash::operator()(
    const std::string& value
) const noexcept {
    return std::hash<std::string_view>{}(value);
}

std::size_t VulkanRenderResources2D::StringViewHash::operator()(
    const char* value
) const noexcept {
    return std::hash<std::string_view>{}(value);
}

void VulkanRenderResources2D::RegisterMesh(std::string id, const VulkanMesh& mesh) {
    SE_ASSERT(!id.empty(), "2D mesh resource id must not be empty");
    SE_ASSERT(!ContainsMesh(id), "2D mesh resource id already exists");

    const std::size_t index = m_Meshes.size();
    m_Meshes.push_back(MeshEntry{
        id,
        &mesh
    });
    m_MeshIndexById.emplace(std::move(id), index);
}

void VulkanRenderResources2D::RegisterMaterial(std::string id, VulkanMaterial& material) {
    SE_ASSERT(!id.empty(), "2D material resource id must not be empty");
    SE_ASSERT(!ContainsMaterial(id), "2D material resource id already exists");

    const std::size_t index = m_Materials.size();
    m_Materials.push_back(MaterialEntry{
        id,
        &material
    });
    m_MaterialIndexById.emplace(std::move(id), index);
}

void VulkanRenderResources2D::RegisterBonePalette(
    std::string id,
    BonePaletteResource palette
) {
    SE_ASSERT(!id.empty(), "Bone palette resource id must not be empty");
    SE_ASSERT(!ContainsBonePalette(id), "Bone palette resource id already exists");

    const std::size_t index = m_BonePalettes.size();
    m_BonePalettes.push_back(BonePaletteEntry{
        id,
        std::move(palette)
    });
    m_BonePaletteIndexById.emplace(std::move(id), index);
}

void VulkanRenderResources2D::UpdateBonePaletteDescriptor(
    std::string_view id,
    VkDescriptorSet descriptorSet,
    u32 descriptorSetReady,
    u32 descriptorSetIndex,
    u32 descriptorBinding,
    u32 descriptorRangeBytes
) {
    const auto found = m_BonePaletteIndexById.find(id);
    SE_ASSERT(found != m_BonePaletteIndexById.end(), "Bone palette resource was not found");

    BonePaletteResource& palette = m_BonePalettes[found->second].palette;
    palette.descriptorSet = descriptorSet;
    palette.descriptorSetReady =
        descriptorSet != VK_NULL_HANDLE && descriptorSetReady != 0u ? 1u : 0u;
    palette.descriptorSetIndex = descriptorSetIndex;
    palette.descriptorBinding = descriptorBinding;
    palette.descriptorRangeBytes = descriptorRangeBytes;
}

const VulkanMesh& VulkanRenderResources2D::Mesh(std::string_view id) const {
    const auto found = m_MeshIndexById.find(id);
    SE_ASSERT(found != m_MeshIndexById.end(), "2D mesh resource was not found");

    return *m_Meshes[found->second].mesh;
}

VulkanMaterial& VulkanRenderResources2D::Material(std::string_view id) const {
    const auto found = m_MaterialIndexById.find(id);
    SE_ASSERT(found != m_MaterialIndexById.end(), "2D material resource was not found");

    return *m_Materials[found->second].material;
}

const VulkanRenderResources2D::BonePaletteResource&
VulkanRenderResources2D::BonePalette(std::string_view id) const {
    const auto found = m_BonePaletteIndexById.find(id);
    SE_ASSERT(found != m_BonePaletteIndexById.end(), "Bone palette resource was not found");

    return m_BonePalettes[found->second].palette;
}

bool VulkanRenderResources2D::ContainsMesh(std::string_view id) const {
    return m_MeshIndexById.find(id) != m_MeshIndexById.end();
}

bool VulkanRenderResources2D::ContainsMaterial(std::string_view id) const {
    return m_MaterialIndexById.find(id) != m_MaterialIndexById.end();
}

bool VulkanRenderResources2D::ContainsBonePalette(std::string_view id) const {
    return m_BonePaletteIndexById.find(id) != m_BonePaletteIndexById.end();
}

std::vector<const VulkanMaterial*> VulkanRenderResources2D::Materials() const {
    std::vector<const VulkanMaterial*> materials;
    materials.reserve(m_Materials.size());

    for (const MaterialEntry& entry : m_Materials) {
        SE_ASSERT(entry.material != nullptr, "2D material resource contains null material");
        materials.push_back(entry.material);
    }

    return materials;
}

void VulkanRenderResources2D::RegisterMeshLodChain(std::string_view id, const MeshLodChain& chain) {
    m_LodChains[std::string(id)] = chain;
}

u32 VulkanRenderResources2D::SelectLod(std::string_view id, f32 sf, u32 prev) const {
    auto it = m_LodChains.find(id);
    if (it == m_LodChains.end()) return 0;
    return MeshLodGenerator::SelectLod(sf, it->second, prev);
}

bool VulkanRenderResources2D::HasLodChain(std::string_view id) const {
    return m_LodChains.find(id) != m_LodChains.end();
}

}
