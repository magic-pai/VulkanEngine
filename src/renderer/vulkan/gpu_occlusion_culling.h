#pragma once

#include "renderer/vulkan/vulkan_common.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <memory>
#include <span>
#include <vector>

namespace se {

class VulkanBuffer;
class VulkanComputePipeline;
class VulkanDepthPyramid;
class VulkanDevice;
class VulkanOcclusionCullDescriptorSetLayout;
class VulkanPhysicalDevice;
class VulkanSampler;
struct RenderCommand;
struct RendererGpuOcclusionStats;

struct GpuOcclusionPrepareResult {
    u32 commandCount = 0;
    u32 validBoundsCount = 0;
    u32 invalidBoundsCount = 0;
    u32 zeroIdentityCount = 0;
    u32 capacityDroppedCount = 0;
    u32 uploadedCandidateCount = 0;
    u64 uploadedCandidateBytes = 0;
    u64 candidateIdentityHash = 0;
    u64 candidateContentHash = 0;
    u64 actualTriangleCount = 0;
    u32 extentWidth = 0;
    u32 extentHeight = 0;
    f32 classificationJitterPixelsX = 0.0f;
    f32 classificationJitterPixelsY = 0.0f;
    f32 reuseJitterGuardPixels = 0.0f;
};

struct GpuOcclusionConsumeStatus {
    bool ready = false;
    bool candidateCountMatches = false;
    bool candidateContentMatches = false;
    bool canonicalViewProjectionMatches = false;
    bool projectionExtentMatches = false;
    bool jitterDeltaWithinGuard = false;
    u32 candidateCount = 0;
    u32 fallbackReason = 0;
    f32 jitterDeltaPixelsX = 0.0f;
    f32 jitterDeltaPixelsY = 0.0f;
    f32 jitterGuardPixels = 0.0f;
};

struct GpuOcclusionReadbackResult {
    bool ready = false;
    bool valid = false;
    u32 candidateCount = 0;
    u32 visibleCount = 0;
    u32 occludedCount = 0;
    u32 uncertainCount = 0;
    u32 invalidResultCount = 0;
    u32 cameraInsideExcludedCount = 0;
    u32 nearPlaneExcludedCount = 0;
    u32 invalidProjectionCount = 0;
    u32 invalidRectCount = 0;
    u32 invalidMipCount = 0;
    u32 maxSelectedMip = 0;
    u64 sampledTexelCount = 0;
    u64 wouldCullTriangleCount = 0;
    u64 expectedIdentityHash = 0;
    u64 resultIdentityHash = 0;
};

class VulkanGpuOcclusionAudit {
public:
    static constexpr u32 kContractVersion = 3u;
    static constexpr u32 kMaxCandidates = 4096u;
    static constexpr u32 kWorkgroupSize = 64u;

    VulkanGpuOcclusionAudit(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanOcclusionCullDescriptorSetLayout& descriptorSetLayout,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanSampler& sampler,
        const std::string& computeShaderPath,
        bool diagnosticsEnabled
    );
    ~VulkanGpuOcclusionAudit();

    SE_DISABLE_COPY(VulkanGpuOcclusionAudit);
    SE_DISABLE_MOVE(VulkanGpuOcclusionAudit);

    bool Ready() const;
    std::size_t Count() const;
    u64 BufferMemoryBytes() const;

    GpuOcclusionPrepareResult PrepareFrame(
        std::size_t imageIndex,
        std::span<const RenderCommand> commands,
        const glm::mat4& classificationViewProjection,
        const glm::mat4& canonicalViewProjection,
        const glm::vec3& cameraPosition,
        const glm::vec2& temporalJitterPixels,
        f32 reuseJitterGuardPixels,
        VkExtent2D extent,
        u32 mipCount,
        f32 depthEpsilon
    );
    GpuOcclusionReadbackResult Readback(std::size_t imageIndex);
    GpuOcclusionConsumeStatus ConsumeStatus(
        std::size_t imageIndex,
        const GpuOcclusionPrepareResult& prepared,
        const glm::mat4& canonicalViewProjection,
        const glm::vec2& temporalJitterPixels
    ) const;
    bool CanConsumeCommand(
        std::size_t imageIndex,
        u32 commandIndex
    ) const;
    VkBuffer IndirectBuffer(std::size_t imageIndex) const;
    bool RecordDispatch(
        VkCommandBuffer commandBuffer,
        std::size_t imageIndex,
        RendererGpuOcclusionStats* stats
    );

    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanOcclusionCullDescriptorSetLayout& descriptorSetLayout,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanSampler& sampler,
        bool diagnosticsEnabled
    );
    void Release();

private:
    struct FrameState {
        u32 candidateCount = 0;
        u64 expectedIdentityHash = 0;
        u64 candidateContentHash = 0;
        glm::mat4 canonicalViewProjection{ 1.0f };
        glm::vec2 temporalJitterPixels{ 0.0f };
        VkExtent2D projectionExtent{};
        f32 reuseJitterGuardPixels = 0.0f;
        bool submitted = false;
        bool indirectReady = false;
        u32 readyCandidateCount = 0;
        u64 readyContentHash = 0;
        glm::mat4 readyCanonicalViewProjection{ 1.0f };
        glm::vec2 readyTemporalJitterPixels{ 0.0f };
        VkExtent2D readyProjectionExtent{};
        f32 readyReuseJitterGuardPixels = 0.0f;
    };

    void CreateResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanOcclusionCullDescriptorSetLayout& descriptorSetLayout,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanSampler& sampler,
        bool diagnosticsEnabled
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    bool m_DiagnosticsEnabled = false;
    std::unique_ptr<VulkanComputePipeline> m_Pipeline;
    std::vector<std::unique_ptr<VulkanBuffer>> m_InputBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> m_ResultBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> m_IndirectBuffers;
    std::vector<VkDescriptorSet> m_DescriptorSets;
    std::vector<FrameState> m_FrameStates;
};

}
