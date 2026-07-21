#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;
class VulkanMaterial;
class VulkanPhysicalDevice;
struct RenderCommand;
struct RendererHybridReflectionStats;

inline constexpr u32 kMaxHybridReflectionInstances = 4096u;

struct alignas(8) HybridReflectionInstanceMetadata {
    std::array<u32, 2> vertexAddress{};
    std::array<u32, 2> indexAddress{};
    u32 vertexCount = 0u;
    u32 indexCount = 0u;
    u32 vertexStride = 0u;
    u32 materialIndex = 0u;
};

static_assert(sizeof(HybridReflectionInstanceMetadata) == 32u);
static_assert(offsetof(HybridReflectionInstanceMetadata, vertexAddress) == 0u);
static_assert(offsetof(HybridReflectionInstanceMetadata, indexAddress) == 8u);
static_assert(offsetof(HybridReflectionInstanceMetadata, vertexCount) == 16u);
static_assert(offsetof(HybridReflectionInstanceMetadata, indexCount) == 20u);
static_assert(offsetof(HybridReflectionInstanceMetadata, vertexStride) == 24u);
static_assert(offsetof(HybridReflectionInstanceMetadata, materialIndex) == 28u);

class VulkanHybridReflectionAccelerationStructures {
public:
    VulkanHybridReflectionAccelerationStructures(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        u32 frameCount
    );
    ~VulkanHybridReflectionAccelerationStructures();

    SE_DISABLE_COPY(VulkanHybridReflectionAccelerationStructures);
    SE_DISABLE_MOVE(VulkanHybridReflectionAccelerationStructures);

    void PrepareFrame(
        u32 frameIndex,
        std::span<const RenderCommand> renderCommands,
        RendererHybridReflectionStats& stats
    );
    void RecordBuilds(
        VkCommandBuffer commandBuffer,
        u32 frameIndex,
        RendererHybridReflectionStats& stats
    );

    VkAccelerationStructureKHR TopLevelHandle(u32 frameIndex) const;
    std::span<const HybridReflectionInstanceMetadata> InstanceMetadata(
        u32 frameIndex
    ) const;
    std::span<const VulkanMaterial* const> InstanceMaterials(
        u32 frameIndex
    ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}
