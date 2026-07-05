#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;

constexpr u32 kBonePaletteDescriptorSetIndex = 2;
constexpr u32 kBonePaletteDescriptorBinding = 0;

VkDescriptorSetLayoutBinding BonePaletteDescriptorSetLayoutBinding();

// Set 0: per-frame data shared by every draw call, such as camera matrices.
class VulkanDescriptorSetLayout {
public:
    explicit VulkanDescriptorSetLayout(const VulkanDevice& device);
    ~VulkanDescriptorSetLayout();

    SE_DISABLE_COPY(VulkanDescriptorSetLayout);
    SE_DISABLE_MOVE(VulkanDescriptorSetLayout);

    VkDescriptorSetLayout Handle() const;
    void Release();

private:
    void CreateDescriptorSetLayout(const VulkanDevice& device);

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
};

// Set 1: per-material resources, such as an albedo texture and sampler.
class VulkanMaterialDescriptorSetLayout {
public:
    explicit VulkanMaterialDescriptorSetLayout(const VulkanDevice& device);
    ~VulkanMaterialDescriptorSetLayout();

    SE_DISABLE_COPY(VulkanMaterialDescriptorSetLayout);
    SE_DISABLE_MOVE(VulkanMaterialDescriptorSetLayout);

    VkDescriptorSetLayout Handle() const;
    void Release();

private:
    void CreateDescriptorSetLayout(const VulkanDevice& device);

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
};

// Hi-Z depth pyramid: one storage image per mip level
class VulkanHiZDescriptorSetLayout {
public:
    explicit VulkanHiZDescriptorSetLayout(const VulkanDevice& device);
    ~VulkanHiZDescriptorSetLayout();
    SE_DISABLE_COPY(VulkanHiZDescriptorSetLayout);
    SE_DISABLE_MOVE(VulkanHiZDescriptorSetLayout);
    VkDescriptorSetLayout Handle() const;
    void Release();
private:
    void CreateDescriptorSetLayout(const VulkanDevice& device);
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
};

// Occlusion culling: Hi-Z texture + instance bounds SSBO + visibility SSBO
class VulkanOcclusionCullDescriptorSetLayout {
public:
    static constexpr u32 kMaxInstances = 4096;
    explicit VulkanOcclusionCullDescriptorSetLayout(const VulkanDevice& device);
    ~VulkanOcclusionCullDescriptorSetLayout();
    SE_DISABLE_COPY(VulkanOcclusionCullDescriptorSetLayout);
    SE_DISABLE_MOVE(VulkanOcclusionCullDescriptorSetLayout);
    VkDescriptorSetLayout Handle() const;
    void Release();
private:
    void CreateDescriptorSetLayout(const VulkanDevice& device);
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
};

}
