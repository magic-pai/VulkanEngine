#include "renderer/vulkan/material_library.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

#include <utility>

namespace se {

VulkanMaterialLibrary::VulkanMaterialLibrary(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool
) : m_Device(device),
    m_PhysicalDevice(physicalDevice),
    m_CommandPool(commandPool),
    m_TextureCache(device, physicalDevice, commandPool) {
}

VulkanTextureCache& VulkanMaterialLibrary::TextureCache() {
    return m_TextureCache;
}

VulkanMaterial& VulkanMaterialLibrary::Create(
    std::string name,
    std::string albedoTexturePath,
    MaterialProperties properties,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    SE_ASSERT(!name.empty(), "Material name must not be empty");
    SE_ASSERT(!Contains(name), "Material name already exists");

    auto material = std::make_unique<VulkanMaterial>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        std::move(albedoTexturePath),
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
    material->Properties() = properties;

    m_Materials.push_back(MaterialEntry{
        std::move(name),
        std::move(material)
    });

    return *m_Materials.back().material;
}

VulkanMaterial& VulkanMaterialLibrary::Create(
    std::string name,
    VulkanEncodedTextureBytes albedoTextureBytes,
    MaterialProperties properties,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    SE_ASSERT(!name.empty(), "Material name must not be empty");
    SE_ASSERT(!Contains(name), "Material name already exists");

    auto material = std::make_unique<VulkanMaterial>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        albedoTextureBytes,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
    material->Properties() = properties;

    m_Materials.push_back(MaterialEntry{
        std::move(name),
        std::move(material)
    });

    return *m_Materials.back().material;
}

VulkanMaterial& VulkanMaterialLibrary::Create(
    std::string name,
    VulkanTexturePixels albedoTexturePixels,
    MaterialProperties properties,
    bool generateMipmaps,
    bool flipVertically,
    VulkanUploadBatch* uploadBatch
) {
    SE_ASSERT(!name.empty(), "Material name must not be empty");
    SE_ASSERT(!Contains(name), "Material name already exists");

    auto material = std::make_unique<VulkanMaterial>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        albedoTexturePixels,
        generateMipmaps,
        flipVertically,
        uploadBatch
    );
    material->Properties() = properties;

    m_Materials.push_back(MaterialEntry{
        std::move(name),
        std::move(material)
    });

    return *m_Materials.back().material;
}

VulkanMaterial& VulkanMaterialLibrary::CreateBlackHoleMaterial(
    std::string name,
    std::string fallbackTexturePath,
    std::string colorMapPath,
    std::string cubemapDirectory,
    MaterialProperties properties
) {
    VulkanMaterial& material = Create(
        std::move(name),
        std::move(fallbackTexturePath),
        properties
    );
    material.SetColorMap(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        std::move(colorMapPath)
    );
    material.SetSkyboxCubemap(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        std::move(cubemapDirectory)
    );

    return material;
}

VulkanMaterial& VulkanMaterialLibrary::Get(std::string_view name) {
    const std::optional<std::size_t> index = FindIndex(name);
    SE_ASSERT(index.has_value(), "Material was not found");

    return *m_Materials[*index].material;
}

const VulkanMaterial& VulkanMaterialLibrary::Get(std::string_view name) const {
    const std::optional<std::size_t> index = FindIndex(name);
    SE_ASSERT(index.has_value(), "Material was not found");

    return *m_Materials[*index].material;
}

bool VulkanMaterialLibrary::Contains(std::string_view name) const {
    return FindIndex(name).has_value();
}

std::size_t VulkanMaterialLibrary::Count() const {
    return m_Materials.size();
}

std::optional<std::size_t> VulkanMaterialLibrary::FindIndex(std::string_view name) const {
    for (std::size_t index = 0; index < m_Materials.size(); ++index) {
        if (m_Materials[index].name == name) {
            return index;
        }
    }

    return std::nullopt;
}

}
