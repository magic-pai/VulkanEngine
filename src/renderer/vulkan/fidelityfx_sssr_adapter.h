#pragma once

#include "renderer/vulkan/vulkan_common.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

namespace se {

class VulkanBuffer;
class VulkanCommandPool;
class VulkanDevice;
class VulkanImage;
class VulkanPhysicalDevice;
class VulkanSceneRenderTargets;

struct alignas(16) FfxSssrConstants {
    glm::mat4 invViewProj{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::mat4 invProj{ 1.0f };
    glm::mat4 view{ 1.0f };
    glm::mat4 invView{ 1.0f };
    glm::mat4 prevViewProj{ 1.0f };
    glm::uvec2 bufferDimensions{ 0u, 0u };
    glm::vec2 invBufferDimensions{ 0.0f, 0.0f };
    f32 temporalStabilityFactor = 0.95f;
    f32 depthBufferThickness = 0.1f;
    f32 roughnessThreshold = 0.6f;
    f32 temporalVarianceThreshold = 0.0f;
    u32 frameIndex = 0u;
    u32 maxTraversalIntersections = 32u;
    u32 minTraversalOccupancy = 4u;
    u32 mostDetailedMip = 0u;
    u32 samplesPerQuad = 1u;
    u32 temporalVarianceGuidedTracingEnabled = 0u;
    u32 padding0 = 0u;
    u32 padding1 = 0u;
};

static_assert(
    sizeof(FfxSssrConstants) == 448u,
    "FfxSssrConstants must match AMD Common.hlsl cbuffer packing"
);

class VulkanFfxSssrConstantsDescriptorSetLayout {
public:
    explicit VulkanFfxSssrConstantsDescriptorSetLayout(const VulkanDevice& device);
    ~VulkanFfxSssrConstantsDescriptorSetLayout();

    SE_DISABLE_COPY(VulkanFfxSssrConstantsDescriptorSetLayout);
    SE_DISABLE_MOVE(VulkanFfxSssrConstantsDescriptorSetLayout);

    VkDescriptorSetLayout Handle() const;
    void Release();

private:
    void CreateDescriptorSetLayout(const VulkanDevice& device);

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
};

class VulkanFfxSssrConstantsResources {
public:
    VulkanFfxSssrConstantsResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanFfxSssrConstantsDescriptorSetLayout& descriptorSetLayout,
        std::size_t count
    );
    ~VulkanFfxSssrConstantsResources();

    SE_DISABLE_COPY(VulkanFfxSssrConstantsResources);
    SE_DISABLE_MOVE(VulkanFfxSssrConstantsResources);

    VkDescriptorSet Handle(std::size_t imageIndex) const;
    std::size_t Count() const;
    u64 TotalMemoryBytes() const;
    void Update(std::size_t imageIndex, const FfxSssrConstants& constants) const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanFfxSssrConstantsDescriptorSetLayout& descriptorSetLayout,
        std::size_t count
    );
    void Release();

private:
    void CreateResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanFfxSssrConstantsDescriptorSetLayout& descriptorSetLayout,
        std::size_t count
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<VulkanBuffer>> m_Buffers;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};

class VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout {
public:
    explicit VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout(
        const VulkanDevice& device
    );
    ~VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout();

    SE_DISABLE_COPY(VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout);
    SE_DISABLE_MOVE(VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout);

    VkDescriptorSetLayout Handle() const;
    void Release();

private:
    void CreateDescriptorSetLayout(const VulkanDevice& device);

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
};

class VulkanFfxSssrPrepareIndirectArgsResources {
public:
    VulkanFfxSssrPrepareIndirectArgsResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout&
            descriptorSetLayout,
        std::size_t count
    );
    ~VulkanFfxSssrPrepareIndirectArgsResources();

    SE_DISABLE_COPY(VulkanFfxSssrPrepareIndirectArgsResources);
    SE_DISABLE_MOVE(VulkanFfxSssrPrepareIndirectArgsResources);

    VkDescriptorSet Handle(std::size_t imageIndex) const;
    VkBuffer RayCounterBuffer(std::size_t imageIndex) const;
    VkBuffer IndirectArgsBuffer(std::size_t imageIndex) const;
    VkBufferView RayCounterBufferView(std::size_t imageIndex) const;
    std::array<u32, 4> RayCounterValues(std::size_t imageIndex) const;
    std::size_t Count() const;
    VkDeviceSize RayCounterBufferSize() const;
    VkDeviceSize IndirectArgsBufferSize() const;
    u64 TotalMemoryBytes() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout&
            descriptorSetLayout,
        std::size_t count
    );
    void Release();

    static constexpr VkDeviceSize kRayCounterBufferSize = sizeof(u32) * 4u;
    static constexpr VkDeviceSize kIndirectArgsBufferSize = sizeof(u32) * 6u;

private:
    void CreateResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanFfxSssrPrepareIndirectArgsDescriptorSetLayout&
            descriptorSetLayout,
        std::size_t count
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<VulkanBuffer>> m_RayCounterBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> m_IndirectArgsBuffers;
    std::vector<VkBufferView> m_RayCounterBufferViews;
    std::vector<VkBufferView> m_IndirectArgsBufferViews;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};

class VulkanFfxSssrClassifyTilesDescriptorSetLayout {
public:
    explicit VulkanFfxSssrClassifyTilesDescriptorSetLayout(
        const VulkanDevice& device
    );
    ~VulkanFfxSssrClassifyTilesDescriptorSetLayout();

    SE_DISABLE_COPY(VulkanFfxSssrClassifyTilesDescriptorSetLayout);
    SE_DISABLE_MOVE(VulkanFfxSssrClassifyTilesDescriptorSetLayout);

    VkDescriptorSetLayout Handle() const;
    void Release();

private:
    void CreateDescriptorSetLayout(const VulkanDevice& device);

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
};

class VulkanFfxSssrClassifyTilesResources {
public:
    VulkanFfxSssrClassifyTilesResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        const VulkanFfxSssrClassifyTilesDescriptorSetLayout& descriptorSetLayout,
        const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
        const VulkanSceneRenderTargets& renderTargets,
        VkImageView environmentMapView,
        VkSampler environmentMapSampler
    );
    ~VulkanFfxSssrClassifyTilesResources();

    SE_DISABLE_COPY(VulkanFfxSssrClassifyTilesResources);
    SE_DISABLE_MOVE(VulkanFfxSssrClassifyTilesResources);

    VkDescriptorSet Handle(std::size_t imageIndex) const;
    VkBuffer RayListBuffer(std::size_t imageIndex) const;
    VkBuffer DenoiserTileListBuffer(std::size_t imageIndex) const;
    VkImage IntersectionOutputImage(std::size_t imageIndex) const;
    VkImage ExtractedRoughnessImage(std::size_t imageIndex) const;
    std::size_t Count() const;
    VkExtent2D Extent() const;
    u32 GroupCountX() const;
    u32 GroupCountY() const;
    u32 RayListCapacity() const;
    u32 DenoiserTileListCapacity() const;
    VkDeviceSize RayListBufferSize() const;
    VkDeviceSize DenoiserTileListBufferSize() const;
    u64 TotalMemoryBytes() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        const VulkanFfxSssrClassifyTilesDescriptorSetLayout& descriptorSetLayout,
        const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
        const VulkanSceneRenderTargets& renderTargets,
        VkImageView environmentMapView,
        VkSampler environmentMapSampler
    );
    void Release();

private:
    void CreateResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        const VulkanFfxSssrClassifyTilesDescriptorSetLayout& descriptorSetLayout,
        const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
        const VulkanSceneRenderTargets& renderTargets,
        VkImageView environmentMapView,
        VkSampler environmentMapSampler
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkExtent2D m_Extent{};
    u32 m_GroupCountX = 0u;
    u32 m_GroupCountY = 0u;
    u32 m_RayListCapacity = 0u;
    u32 m_DenoiserTileListCapacity = 0u;
    VkDeviceSize m_RayListBufferSize = 0;
    VkDeviceSize m_DenoiserTileListBufferSize = 0;
    std::vector<std::unique_ptr<VulkanBuffer>> m_RayListBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> m_DenoiserTileListBuffers;
    std::vector<VkBufferView> m_RayListBufferViews;
    std::vector<VkBufferView> m_DenoiserTileListBufferViews;
    std::vector<std::unique_ptr<VulkanImage>> m_IntersectionOutputImages;
    std::vector<std::unique_ptr<VulkanImage>> m_ExtractedRoughnessImages;
    std::vector<std::unique_ptr<VulkanImage>> m_VarianceHistoryImages;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};

}
