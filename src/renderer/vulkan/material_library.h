#pragma once

#include "renderer/vulkan/material.h"
#include "renderer/vulkan/texture_cache.h"

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanUploadBatch;

class VulkanMaterialLibrary {
public:
    VulkanMaterialLibrary(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool
    );

    ~VulkanMaterialLibrary() = default;

    SE_DISABLE_COPY(VulkanMaterialLibrary);
    SE_DISABLE_MOVE(VulkanMaterialLibrary);

    VulkanMaterial& Create(
        std::string name,
        std::string albedoTexturePath,
        MaterialProperties properties = {},
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    VulkanMaterial& Create(
        std::string name,
        VulkanEncodedTextureBytes albedoTextureBytes,
        MaterialProperties properties = {},
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    VulkanMaterial& Create(
        std::string name,
        VulkanTexturePixels albedoTexturePixels,
        MaterialProperties properties = {},
        bool generateMipmaps = true,
        bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr
    );
    VulkanMaterial& CreateBlackHoleMaterial(
        std::string name,
        std::string fallbackTexturePath,
        std::string colorMapPath,
        std::string cubemapDirectory,
        MaterialProperties properties = {}
    );

    VulkanMaterial& Get(std::string_view name);
    const VulkanMaterial& Get(std::string_view name) const;
    VulkanTextureCache& TextureCache();
    bool Contains(std::string_view name) const;
    std::size_t Count() const;

private:
    struct MaterialEntry {
        std::string name;
        std::unique_ptr<VulkanMaterial> material;
    };

    std::optional<std::size_t> FindIndex(std::string_view name) const;

private:
    const VulkanDevice& m_Device;
    const VulkanPhysicalDevice& m_PhysicalDevice;
    const VulkanCommandPool& m_CommandPool;
    std::vector<MaterialEntry> m_Materials;
    VulkanTextureCache m_TextureCache;
};

}
