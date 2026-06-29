#pragma once

#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/vulkan_common.h"

#include <vector>

namespace se {

class VulkanDevice;
class VulkanPhysicalDevice;

enum class GpuTimestamp : u32 {
    FrameStart = 0,
    ShadowStart,
    ShadowEnd,
    MainStart,
    MainEnd,
    OverlayStart,
    OverlayEnd,
    ImGuiStart,
    ImGuiEnd,
    FrameEnd,
    Count
};

class VulkanGpuTimer {
public:
    VulkanGpuTimer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t frameCount
    );
    ~VulkanGpuTimer();

    SE_DISABLE_COPY(VulkanGpuTimer);
    SE_DISABLE_MOVE(VulkanGpuTimer);

    bool Supported() const;
    void ResetFrame(VkCommandBuffer commandBuffer, std::size_t frameIndex) const;
    void WriteTimestamp(
        VkCommandBuffer commandBuffer,
        std::size_t frameIndex,
        GpuTimestamp timestamp
    ) const;
    void MarkFrameSubmitted(std::size_t frameIndex);
    RendererGpuStats ReadFrameStats(std::size_t frameIndex) const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t frameCount
    );
    void Release();

private:
    static constexpr u32 kTimestampsPerFrame =
        static_cast<u32>(GpuTimestamp::Count);

    u32 QueryIndex(std::size_t frameIndex, GpuTimestamp timestamp) const;
    f32 TimestampDeltaMs(u64 start, u64 end) const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueryPool m_QueryPool = VK_NULL_HANDLE;
    std::size_t m_FrameCount = 0;
    f32 m_TimestampPeriod = 0.0f;
    bool m_Supported = false;
    std::vector<bool> m_FrameSubmitted;
};

}
