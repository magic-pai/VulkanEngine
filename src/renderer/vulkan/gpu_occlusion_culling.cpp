#include "renderer/vulkan/gpu_occlusion_culling.h"

#include "renderer/render_queue.h"
#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/compute_pipeline.h"
#include "renderer/vulkan/descriptor_set_layout.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/render_targets.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/sampler.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace se {

namespace {

constexpr u32 kClassificationVisible = 1u;
constexpr u32 kClassificationOccluded = 2u;
constexpr u32 kClassificationUncertain = 3u;

constexpr u32 kReasonNone = 0u;
constexpr u32 kReasonCameraInside = 1u;
constexpr u32 kReasonNearPlane = 2u;
constexpr u32 kReasonInvalidProjection = 3u;
constexpr u32 kReasonInvalidRect = 4u;
constexpr u32 kReasonInvalidMip = 5u;

constexpr u64 kFnvOffsetBasis = 14695981039346656037ull;
constexpr u64 kFnvPrime = 1099511628211ull;

struct alignas(16) GpuOcclusionInputHeader {
    glm::mat4 viewProjection{ 1.0f };
    glm::vec4 cameraPositionEpsilon{ 0.0f };
    glm::uvec4 info{ 0u };
};

struct alignas(16) GpuOcclusionCandidate {
    glm::vec4 boundsMin{ 0.0f };
    glm::vec4 boundsMax{ 0.0f };
    glm::uvec4 metadata{ 0u };
};

struct alignas(16) GpuOcclusionResult {
    glm::uvec4 classification{ 0u };
    glm::uvec4 metadata{ 0u };
};

static_assert(sizeof(GpuOcclusionInputHeader) == 96u);
static_assert(sizeof(GpuOcclusionCandidate) == 48u);
static_assert(sizeof(GpuOcclusionResult) == 32u);

constexpr VkDeviceSize kInputBufferBytes =
    sizeof(GpuOcclusionInputHeader) +
    sizeof(GpuOcclusionCandidate) * VulkanGpuOcclusionAudit::kMaxCandidates;
constexpr VkDeviceSize kResultBufferBytes =
    sizeof(GpuOcclusionResult) * VulkanGpuOcclusionAudit::kMaxCandidates;

bool FiniteVec3(const glm::vec3& value) {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

u64 HashWord(u64 hash, u32 value) {
    for (u32 byteIndex = 0u; byteIndex < 4u; ++byteIndex) {
        hash ^= static_cast<u8>((value >> (byteIndex * 8u)) & 0xffu);
        hash *= kFnvPrime;
    }
    return hash;
}

u64 HashIdentity(u64 hash, u32 identityLow, u32 identityHigh, u32 index) {
    hash = HashWord(hash, identityLow);
    hash = HashWord(hash, identityHigh);
    return HashWord(hash, index);
}

void NameBuffer(
    const VulkanDevice& device,
    const VulkanBuffer& buffer,
    const char* kind,
    std::size_t imageIndex
) {
    const std::string name = "SelfEngine.GpuOcclusion." +
        std::string(kind) + "[" + std::to_string(imageIndex) + "]";
    SetVulkanDebugObjectName(
        device.Handle(),
        VK_OBJECT_TYPE_BUFFER,
        buffer.Handle(),
        name.c_str()
    );
}

}

VulkanGpuOcclusionAudit::VulkanGpuOcclusionAudit(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanOcclusionCullDescriptorSetLayout& descriptorSetLayout,
    const VulkanDepthPyramid& depthPyramid,
    const VulkanSampler& sampler,
    const std::string& computeShaderPath
) : m_Device(device.Handle()) {
    const std::array<VkDescriptorSetLayout, 1> layouts = {
        descriptorSetLayout.Handle()
    };
    m_Pipeline = std::make_unique<VulkanComputePipeline>(
        device,
        std::span<const VkDescriptorSetLayout>(layouts),
        computeShaderPath
    );
    CreateResources(
        device,
        physicalDevice,
        descriptorSetLayout,
        depthPyramid,
        sampler
    );
}

VulkanGpuOcclusionAudit::~VulkanGpuOcclusionAudit() {
    Release();
}

bool VulkanGpuOcclusionAudit::Ready() const {
    return m_Pipeline != nullptr &&
        m_Pipeline->Handle() != VK_NULL_HANDLE &&
        !m_DescriptorSets.empty() &&
        m_DescriptorSets.size() == m_InputBuffers.size() &&
        m_DescriptorSets.size() == m_ResultBuffers.size();
}

std::size_t VulkanGpuOcclusionAudit::Count() const {
    return m_DescriptorSets.size();
}

u64 VulkanGpuOcclusionAudit::BufferMemoryBytes() const {
    return static_cast<u64>(Count()) *
        static_cast<u64>(kInputBufferBytes + kResultBufferBytes);
}

GpuOcclusionPrepareResult VulkanGpuOcclusionAudit::PrepareFrame(
    std::size_t imageIndex,
    std::span<const RenderCommand> commands,
    const glm::mat4& viewProjection,
    const glm::vec3& cameraPosition,
    VkExtent2D extent,
    u32 mipCount,
    f32 depthEpsilon
) {
    SE_ASSERT(imageIndex < m_InputBuffers.size(), "GPU occlusion input image index is out of range");
    SE_ASSERT(imageIndex < m_FrameStates.size(), "GPU occlusion frame-state image index is out of range");

    GpuOcclusionPrepareResult prepared{};
    prepared.commandCount = static_cast<u32>(std::min<std::size_t>(
        commands.size(),
        std::numeric_limits<u32>::max()
    ));

    std::vector<GpuOcclusionCandidate> candidates;
    candidates.reserve(std::min<std::size_t>(commands.size(), kMaxCandidates));
    u64 identityHash = kFnvOffsetBasis;
    for (const RenderCommand& command : commands) {
        if (command.mesh != nullptr) {
            prepared.actualTriangleCount += command.mesh->IndexCount() / 3u;
        }
        if (!command.worldBounds.valid ||
            !FiniteVec3(command.worldBounds.min) ||
            !FiniteVec3(command.worldBounds.max) ||
            glm::any(glm::greaterThan(command.worldBounds.min, command.worldBounds.max))) {
            ++prepared.invalidBoundsCount;
            continue;
        }

        ++prepared.validBoundsCount;
        if (command.renderableIdentity == 0u) {
            ++prepared.zeroIdentityCount;
        }
        if (candidates.size() >= kMaxCandidates) {
            ++prepared.capacityDroppedCount;
            continue;
        }

        const u32 candidateIndex = static_cast<u32>(candidates.size());
        const u32 identityLow = static_cast<u32>(command.renderableIdentity);
        const u32 identityHigh = static_cast<u32>(command.renderableIdentity >> 32u);
        const u32 triangleCount = command.mesh != nullptr
            ? command.mesh->IndexCount() / 3u
            : 0u;
        candidates.push_back(GpuOcclusionCandidate{
            glm::vec4(command.worldBounds.min, 0.0f),
            glm::vec4(command.worldBounds.max, 0.0f),
            glm::uvec4(identityLow, identityHigh, triangleCount, candidateIndex)
        });
        identityHash = HashIdentity(
            identityHash,
            identityLow,
            identityHigh,
            candidateIndex
        );
    }

    prepared.uploadedCandidateCount = static_cast<u32>(candidates.size());
    prepared.uploadedCandidateBytes = sizeof(GpuOcclusionInputHeader) +
        static_cast<u64>(candidates.size()) * sizeof(GpuOcclusionCandidate);
    prepared.candidateIdentityHash = identityHash;

    GpuOcclusionInputHeader header{};
    header.viewProjection = viewProjection;
    header.cameraPositionEpsilon = glm::vec4(
        cameraPosition,
        std::clamp(depthEpsilon, 0.0f, 0.01f)
    );
    header.info = glm::uvec4(
        prepared.uploadedCandidateCount,
        extent.width,
        extent.height,
        mipCount
    );

    std::vector<std::byte> uploadBytes(
        static_cast<std::size_t>(prepared.uploadedCandidateBytes)
    );
    std::memcpy(uploadBytes.data(), &header, sizeof(header));
    if (!candidates.empty()) {
        std::memcpy(
            uploadBytes.data() + sizeof(header),
            candidates.data(),
            candidates.size() * sizeof(GpuOcclusionCandidate)
        );
    }
    m_InputBuffers[imageIndex]->Upload(uploadBytes);

    FrameState& frame = m_FrameStates[imageIndex];
    frame.candidateCount = prepared.uploadedCandidateCount;
    frame.expectedIdentityHash = prepared.candidateIdentityHash;
    frame.submitted = false;
    return prepared;
}

GpuOcclusionReadbackResult VulkanGpuOcclusionAudit::Readback(
    std::size_t imageIndex
) {
    SE_ASSERT(imageIndex < m_ResultBuffers.size(), "GPU occlusion result image index is out of range");
    SE_ASSERT(imageIndex < m_FrameStates.size(), "GPU occlusion frame-state image index is out of range");

    GpuOcclusionReadbackResult readback{};
    FrameState& frame = m_FrameStates[imageIndex];
    if (!frame.submitted || frame.candidateCount == 0u) {
        return readback;
    }

    readback.ready = true;
    readback.candidateCount = frame.candidateCount;
    readback.expectedIdentityHash = frame.expectedIdentityHash;
    std::vector<GpuOcclusionResult> results(frame.candidateCount);
    m_ResultBuffers[imageIndex]->Download(std::as_writable_bytes(
        std::span<GpuOcclusionResult>(results)
    ));

    u64 identityHash = kFnvOffsetBasis;
    for (u32 index = 0u; index < readback.candidateCount; ++index) {
        const GpuOcclusionResult& result = results[index];
        const u32 classification = result.classification.x;
        const u32 reason = result.classification.y;
        if (classification == kClassificationVisible) {
            ++readback.visibleCount;
        } else if (classification == kClassificationOccluded) {
            ++readback.occludedCount;
            readback.wouldCullTriangleCount += result.metadata.z;
        } else if (classification == kClassificationUncertain) {
            ++readback.uncertainCount;
            if (reason == kReasonCameraInside) {
                ++readback.cameraInsideExcludedCount;
            } else if (reason == kReasonNearPlane) {
                ++readback.nearPlaneExcludedCount;
            } else if (reason == kReasonInvalidProjection) {
                ++readback.invalidProjectionCount;
            } else if (reason == kReasonInvalidRect) {
                ++readback.invalidRectCount;
            } else if (reason == kReasonInvalidMip) {
                ++readback.invalidMipCount;
            }
        } else {
            ++readback.invalidResultCount;
        }

        readback.maxSelectedMip = std::max(
            readback.maxSelectedMip,
            result.classification.z
        );
        readback.sampledTexelCount += result.classification.w;
        if (result.metadata.w != index) {
            ++readback.invalidResultCount;
        }
        identityHash = HashIdentity(
            identityHash,
            result.metadata.x,
            result.metadata.y,
            result.metadata.w
        );
    }

    readback.resultIdentityHash = identityHash;
    readback.valid =
        readback.invalidResultCount == 0u &&
        readback.visibleCount + readback.occludedCount +
            readback.uncertainCount == readback.candidateCount &&
        readback.resultIdentityHash == readback.expectedIdentityHash;
    frame.submitted = false;
    return readback;
}

bool VulkanGpuOcclusionAudit::RecordDispatch(
    VkCommandBuffer commandBuffer,
    std::size_t imageIndex,
    RendererGpuOcclusionStats* stats
) {
    if (!Ready() || imageIndex >= m_FrameStates.size() ||
        m_FrameStates[imageIndex].candidateCount == 0u) {
        return false;
    }

    BeginVulkanDebugLabel(
        m_Device,
        commandBuffer,
        "GPU Occlusion Audit",
        0.18f,
        0.72f,
        0.34f
    );
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_Pipeline->Handle()
    );
    const VkDescriptorSet descriptorSet = m_DescriptorSets[imageIndex];
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_Pipeline->Layout(),
        0u,
        1u,
        &descriptorSet,
        0u,
        nullptr
    );
    const u32 groupCount =
        (m_FrameStates[imageIndex].candidateCount + kWorkgroupSize - 1u) /
        kWorkgroupSize;
    vkCmdDispatch(commandBuffer, groupCount, 1u, 1u);

    VkBufferMemoryBarrier hostReadBarrier{};
    hostReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hostReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostReadBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostReadBarrier.buffer = m_ResultBuffers[imageIndex]->Handle();
    hostReadBarrier.offset = 0u;
    hostReadBarrier.size = sizeof(GpuOcclusionResult) *
        m_FrameStates[imageIndex].candidateCount;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        0u,
        nullptr,
        1u,
        &hostReadBarrier,
        0u,
        nullptr
    );
    EndVulkanDebugLabel(m_Device, commandBuffer);

    m_FrameStates[imageIndex].submitted = true;
    if (stats != nullptr) {
        ++stats->classificationDispatchCount;
        stats->classificationGroupCount += groupCount;
    }
    return true;
}

void VulkanGpuOcclusionAudit::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanOcclusionCullDescriptorSetLayout& descriptorSetLayout,
    const VulkanDepthPyramid& depthPyramid,
    const VulkanSampler& sampler
) {
    Release();
    m_Device = device.Handle();
    CreateResources(
        device,
        physicalDevice,
        descriptorSetLayout,
        depthPyramid,
        sampler
    );
}

void VulkanGpuOcclusionAudit::Release() {
    m_DescriptorSets.clear();
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    m_ResultBuffers.clear();
    m_InputBuffers.clear();
    m_FrameStates.clear();
}

void VulkanGpuOcclusionAudit::CreateResources(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanOcclusionCullDescriptorSetLayout& descriptorSetLayout,
    const VulkanDepthPyramid& depthPyramid,
    const VulkanSampler& sampler
) {
    const std::size_t count = depthPyramid.Count();
    SE_ASSERT(count > 0u, "GPU occlusion resources require swapchain images");

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<u32>(count);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = static_cast<u32>(count * 2u);
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(count);
    if (vkCreateDescriptorPool(
            device.Handle(),
            &poolInfo,
            nullptr,
            &m_DescriptorPool
        ) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU occlusion descriptor pool");
    }

    std::vector<VkDescriptorSetLayout> layouts(count, descriptorSetLayout.Handle());
    m_DescriptorSets.resize(count);
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = m_DescriptorPool;
    allocateInfo.descriptorSetCount = static_cast<u32>(count);
    allocateInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(
            device.Handle(),
            &allocateInfo,
            m_DescriptorSets.data()
        ) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GPU occlusion descriptor sets");
    }

    m_InputBuffers.reserve(count);
    m_ResultBuffers.reserve(count);
    m_FrameStates.resize(count);
    for (std::size_t imageIndex = 0u; imageIndex < count; ++imageIndex) {
        m_InputBuffers.push_back(std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            kInputBufferBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));
        m_ResultBuffers.push_back(std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            kResultBufferBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));
        NameBuffer(device, *m_InputBuffers.back(), "Candidates", imageIndex);
        NameBuffer(device, *m_ResultBuffers.back(), "Results", imageIndex);

        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = sampler.Handle();
        depthInfo.imageView = depthPyramid.View(imageIndex);
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo inputInfo{};
        inputInfo.buffer = m_InputBuffers.back()->Handle();
        inputInfo.offset = 0u;
        inputInfo.range = kInputBufferBytes;
        VkDescriptorBufferInfo resultInfo{};
        resultInfo.buffer = m_ResultBuffers.back()->Handle();
        resultInfo.offset = 0u;
        resultInfo.range = kResultBufferBytes;
        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_DescriptorSets[imageIndex];
        writes[0].dstBinding = 0u;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1u;
        writes[0].pImageInfo = &depthInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_DescriptorSets[imageIndex];
        writes[1].dstBinding = 1u;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1u;
        writes[1].pBufferInfo = &inputInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m_DescriptorSets[imageIndex];
        writes[2].dstBinding = 2u;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1u;
        writes[2].pBufferInfo = &resultInfo;
        vkUpdateDescriptorSets(
            device.Handle(),
            static_cast<u32>(writes.size()),
            writes.data(),
            0u,
            nullptr
        );
    }
}

}
