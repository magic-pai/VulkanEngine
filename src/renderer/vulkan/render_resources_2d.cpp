#include "renderer/vulkan/render_resources_2d.h"

#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

namespace se {

namespace {

bool BonePalettesEqual(
    std::span<const glm::mat4> left,
    std::span<const glm::mat4> right
) {
    return left.size() == right.size() &&
        (left.empty() || std::memcmp(
            left.data(),
            right.data(),
            left.size_bytes()
        ) == 0);
}

}

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

void VulkanRenderResources2D::UpdateBonePalette(
    std::string_view id,
    std::vector<glm::mat4> previousPalette,
    std::vector<glm::mat4> currentPalette,
    u32 changedEntryCount,
    u32 ready
) {
    const auto found = m_BonePaletteIndexById.find(id);
    SE_ASSERT(found != m_BonePaletteIndexById.end(), "Bone palette resource was not found");

    BonePaletteResource& palette = m_BonePalettes[found->second].palette;
    const bool poseChanged = !BonePalettesEqual(
        palette.currentPalette,
        currentPalette
    );
    palette.previousPalette = std::move(previousPalette);
    palette.currentPalette = std::move(currentPalette);
    palette.changedEntryCount = changedEntryCount;
    palette.ready =
        ready != 0u &&
        !palette.previousPalette.empty() &&
        palette.previousPalette.size() == palette.currentPalette.size()
            ? 1u
            : 0u;
    if (poseChanged && palette.revision < std::numeric_limits<u64>::max()) {
        ++palette.revision;
    }
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

bool VulkanRenderResources2D::RecreateMaterialSamplers(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    f32 mipLodBias
) const {
    bool recreated = false;
    for (const MaterialEntry& entry : m_Materials) {
        SE_ASSERT(entry.material != nullptr, "2D material resource contains null material");
        if (std::abs(entry.material->Sampler().MipLodBias() - mipLodBias) <= 0.0001f) {
            continue;
        }

        entry.material->RecreateSampler(device, physicalDevice, mipLodBias);
        recreated = true;
    }

    return recreated;
}

void VulkanRenderResources2D::RegisterMeshLodChain(
    std::string id,
    MeshLodChain chain
) {
    SE_ASSERT(!id.empty(), "LOD mesh resource id must not be empty");
    SE_ASSERT(chain.Count() > 1u, "LOD chain must contain a base and lower level");
    SE_ASSERT(!HasLodChain(id), "LOD mesh resource id already exists");

    const MeshLodLevel& base = chain.levels.front();
    if (m_RegisteredLodResidencyMeshes.insert(base.mesh).second) {
        ++m_LodResidencyStats.chainCount;
        m_LodResidencyStats.levelCount += static_cast<u32>(chain.Count());
        m_LodResidencyStats.sourceVertexBytes +=
            static_cast<u64>(base.vertexCount) * sizeof(Vertex3D);
        m_LodResidencyStats.sourceIndexBytes +=
            static_cast<u64>(base.indexCount) * sizeof(u32);
        m_LodResidencyStats.residentVertexBytes += chain.residentVertexBytes;
        m_LodResidencyStats.residentIndexBytes += chain.residentIndexBytes;
        const u64 baseVertexBytes =
            static_cast<u64>(base.vertexCount) * sizeof(Vertex3D);
        const u64 baseIndexBytes =
            static_cast<u64>(base.indexCount) * sizeof(u32);
        m_LodResidencyStats.extraVertexBytes +=
            chain.residentVertexBytes >= baseVertexBytes
                ? chain.residentVertexBytes - baseVertexBytes
                : 0u;
        m_LodResidencyStats.extraIndexBytes +=
            chain.residentIndexBytes >= baseIndexBytes
                ? chain.residentIndexBytes - baseIndexBytes
                : 0u;
        for (const MeshLodLevel& level : chain.levels) {
            m_LodResidencyStats.maxSimplificationError = std::max(
                m_LodResidencyStats.maxSimplificationError,
                level.simplificationError
            );
        }
    }

    m_LodChains.emplace(std::move(id), std::move(chain));
}

MeshLodSelection VulkanRenderResources2D::SelectLod(
    std::string_view id,
    f32 projectedDiameterPixels,
    u32 previousLod,
    bool previousLodValid,
    f32 targetPixelError,
    f32 hysteresisRatio
) const {
    const MeshLodChain* chain = LodChain(id);
    if (chain == nullptr) {
        return MeshLodSelection{};
    }
    return MeshLodGenerator::SelectLod(
        projectedDiameterPixels,
        *chain,
        previousLod,
        previousLodValid,
        targetPixelError,
        hysteresisRatio
    );
}

const MeshLodChain* VulkanRenderResources2D::LodChain(
    std::string_view id
) const {
    const auto found = m_LodChains.find(id);
    return found != m_LodChains.end() ? &found->second : nullptr;
}

bool VulkanRenderResources2D::HasLodChain(std::string_view id) const {
    return m_LodChains.find(id) != m_LodChains.end();
}

const MeshLodResidencyStats& VulkanRenderResources2D::LodResidencyStats() const {
    return m_LodResidencyStats;
}

}
