#pragma once

#include "renderer/vulkan/vulkan_common.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanTexture2D;
class VulkanUploadBatch;

class VulkanTextureCache {
public:
    VulkanTextureCache(const VulkanDevice& device, const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool);
    ~VulkanTextureCache();
    SE_DISABLE_COPY(VulkanTextureCache);
    SE_DISABLE_MOVE(VulkanTextureCache);

    const VulkanTexture2D* GetOrCreate(std::string path, bool srgb = true,
        bool generateMipmaps = true, bool flipVertically = false,
        VulkanUploadBatch* uploadBatch = nullptr);
    std::size_t Count() const;

private:
    struct Key { std::string path; bool srgb; bool mips; bool flip;
        bool operator==(const Key& o) const { return path==o.path && srgb==o.srgb && mips==o.mips && flip==o.flip; }
    };
    struct KeyHash { std::size_t operator()(const Key& k) const noexcept {
        std::size_t h = std::hash<std::string>{}(k.path);
        h ^= std::hash<bool>{}(k.srgb) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<bool>{}(k.mips) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<bool>{}(k.flip) + 0x9e3779b9 + (h<<6) + (h>>2);
        return h;
    }};

    const VulkanDevice& m_Device;
    const VulkanPhysicalDevice& m_PhysicalDevice;
    const VulkanCommandPool& m_CommandPool;
    std::unordered_map<Key, std::unique_ptr<VulkanTexture2D>, KeyHash> m_Cache;
};

}
