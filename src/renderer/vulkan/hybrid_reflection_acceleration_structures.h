#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDevice;
class VulkanPhysicalDevice;
struct RenderCommand;
struct RendererHybridReflectionStats;

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

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}
