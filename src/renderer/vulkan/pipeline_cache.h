#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <filesystem>

namespace se {

class VulkanPipelineCache {
public:
    VulkanPipelineCache(
        VkDevice device,
        const VkPhysicalDeviceProperties& physicalDeviceProperties
    );
    ~VulkanPipelineCache();

    SE_DISABLE_COPY(VulkanPipelineCache);
    SE_DISABLE_MOVE(VulkanPipelineCache);

    VkPipelineCache Handle() const;
    bool Enabled() const;
    bool LoadedFromDisk() const;
    bool SavedToDisk() const;
    const std::filesystem::path& CachePath() const;

    void Save();
    void Release();

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkPipelineCache m_PipelineCache = VK_NULL_HANDLE;
    std::filesystem::path m_CachePath;
    bool m_Enabled = false;
    bool m_LoadedFromDisk = false;
    bool m_SavedToDisk = false;
};

}
