#include "renderer/vulkan/gpu_timer.h"

#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

#include <array>

namespace se {

namespace {

struct TimestampQueryResult {
    u64 value = 0;
    u64 available = 0;
};

bool TimestampResultsAvailable(
    std::span<const TimestampQueryResult> results,
    GpuTimestamp start,
    GpuTimestamp end
) {
    return results[static_cast<u32>(start)].available != 0 &&
        results[static_cast<u32>(end)].available != 0;
}

}

VulkanGpuTimer::VulkanGpuTimer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t frameCount
) {
    Recreate(device, physicalDevice, frameCount);
}

VulkanGpuTimer::~VulkanGpuTimer() {
    Release();
}

bool VulkanGpuTimer::Supported() const {
    return m_Supported;
}

void VulkanGpuTimer::ResetFrame(VkCommandBuffer commandBuffer, std::size_t frameIndex) const {
    if (!m_Supported) {
        return;
    }

    vkCmdResetQueryPool(
        commandBuffer,
        m_QueryPool,
        QueryIndex(frameIndex, GpuTimestamp::FrameStart),
        kTimestampsPerFrame
    );
}

void VulkanGpuTimer::WriteTimestamp(
    VkCommandBuffer commandBuffer,
    std::size_t frameIndex,
    GpuTimestamp timestamp
) const {
    if (!m_Supported) {
        return;
    }

    vkCmdWriteTimestamp(
        commandBuffer,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        m_QueryPool,
        QueryIndex(frameIndex, timestamp)
    );
}

void VulkanGpuTimer::MarkFrameSubmitted(std::size_t frameIndex) {
    if (!m_Supported || frameIndex >= m_FrameSubmitted.size()) {
        return;
    }

    m_FrameSubmitted[frameIndex] = true;
}

RendererGpuStats VulkanGpuTimer::ReadFrameStats(std::size_t frameIndex) const {
    RendererGpuStats stats{};
    if (!m_Supported ||
        frameIndex >= m_FrameCount ||
        frameIndex >= m_FrameSubmitted.size() ||
        !m_FrameSubmitted[frameIndex]) {
        return stats;
    }

    std::array<TimestampQueryResult, kTimestampsPerFrame> results{};
    const VkResult result = vkGetQueryPoolResults(
        m_Device,
        m_QueryPool,
        QueryIndex(frameIndex, GpuTimestamp::FrameStart),
        kTimestampsPerFrame,
        sizeof(TimestampQueryResult) * results.size(),
        results.data(),
        sizeof(TimestampQueryResult),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
    );
    if (result != VK_SUCCESS && result != VK_NOT_READY) {
        return stats;
    }

    auto segmentMs = [&](GpuTimestamp start, GpuTimestamp end) {
        if (!TimestampResultsAvailable(results, start, end)) {
            return 0.0f;
        }

        return TimestampDeltaMs(
            results[static_cast<u32>(start)].value,
            results[static_cast<u32>(end)].value
        );
    };

    stats.available = TimestampResultsAvailable(
        results,
        GpuTimestamp::FrameStart,
        GpuTimestamp::FrameEnd
    );
    stats.shadowMs = segmentMs(GpuTimestamp::ShadowStart, GpuTimestamp::ShadowEnd);
    stats.mainMs = segmentMs(GpuTimestamp::MainStart, GpuTimestamp::MainEnd);
    stats.overlayMs = segmentMs(GpuTimestamp::OverlayStart, GpuTimestamp::OverlayEnd);
    stats.imguiMs = segmentMs(GpuTimestamp::ImGuiStart, GpuTimestamp::ImGuiEnd);
    stats.totalRecordedMs = segmentMs(GpuTimestamp::FrameStart, GpuTimestamp::FrameEnd);

    return stats;
}

void VulkanGpuTimer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t frameCount
) {
    Release();
    m_Device = device.Handle();
    m_FrameCount = frameCount;
    m_TimestampPeriod = physicalDevice.Properties().limits.timestampPeriod;

    const QueueFamilyIndices& queueFamilies = physicalDevice.QueueFamilies();
    m_Supported = queueFamilies.graphicsFamily.has_value() &&
        physicalDevice.Properties().limits.timestampComputeAndGraphics != VK_FALSE &&
        m_TimestampPeriod > 0.0f &&
        frameCount > 0;
    if (!m_Supported) {
        return;
    }
    m_FrameSubmitted.assign(frameCount, false);

    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = static_cast<u32>(frameCount) * kTimestampsPerFrame;

    if (vkCreateQueryPool(m_Device, &queryPoolInfo, nullptr, &m_QueryPool) != VK_SUCCESS) {
        m_Supported = false;
        m_QueryPool = VK_NULL_HANDLE;
        m_FrameSubmitted.clear();
    }
}

void VulkanGpuTimer::Release() {
    if (m_QueryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_Device, m_QueryPool, nullptr);
        m_QueryPool = VK_NULL_HANDLE;
    }

    m_FrameCount = 0;
    m_TimestampPeriod = 0.0f;
    m_Supported = false;
    m_FrameSubmitted.clear();
}

u32 VulkanGpuTimer::QueryIndex(std::size_t frameIndex, GpuTimestamp timestamp) const {
    return static_cast<u32>(frameIndex) * kTimestampsPerFrame +
        static_cast<u32>(timestamp);
}

f32 VulkanGpuTimer::TimestampDeltaMs(u64 start, u64 end) const {
    if (end <= start) {
        return 0.0f;
    }

    const f64 nanoseconds = static_cast<f64>(end - start) *
        static_cast<f64>(m_TimestampPeriod);
    return static_cast<f32>(nanoseconds / 1000000.0);
}

}
