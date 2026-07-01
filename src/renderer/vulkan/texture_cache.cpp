#include "renderer/vulkan/texture_cache.h"
#include "renderer/vulkan/texture_2d.h"
#include "renderer/vulkan/upload_batch.h"

namespace se {

VulkanTextureCache::VulkanTextureCache(const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice, const VulkanCommandPool& commandPool)
    : m_Device(device), m_PhysicalDevice(physicalDevice), m_CommandPool(commandPool) {}

VulkanTextureCache::~VulkanTextureCache() = default;

const VulkanTexture2D* VulkanTextureCache::GetOrCreate(std::string path,
    bool srgb, bool generateMipmaps, bool flipVertically, VulkanUploadBatch* uploadBatch)
{
    if (path.empty()) return nullptr;
    Key key{path, srgb, generateMipmaps, flipVertically};
    auto it = m_Cache.find(key);
    if (it != m_Cache.end()) return it->second.get();
    auto tex = std::make_unique<VulkanTexture2D>(m_Device, m_PhysicalDevice, m_CommandPool,
        path, srgb, generateMipmaps, flipVertically, uploadBatch);
    auto* ptr = tex.get();
    m_Cache.emplace(std::move(key), std::move(tex));
    return ptr;
}

std::size_t VulkanTextureCache::Count() const { return m_Cache.size(); }

}
