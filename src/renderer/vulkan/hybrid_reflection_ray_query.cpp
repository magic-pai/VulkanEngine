#include "renderer/vulkan/hybrid_reflection_ray_query.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/compute_pipeline.h"
#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/fidelityfx_sssr_adapter.h"
#include "renderer/vulkan/hybrid_reflection_acceleration_structures.h"
#include "renderer/vulkan/image.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/render_targets.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/vertex.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace se {

namespace {

constexpr u32 kRayQueryContractVersion = 2u;
constexpr u32 kHitAttributeContractVersion = 1u;
constexpr u32 kDiagnosticValueCount = 27u;
constexpr u32 kHitDistanceMinDiagnosticIndex = 7u;
constexpr u32 kNormalLengthMinDiagnosticIndex = 20u;
constexpr u32 kBarycentricSumMinDiagnosticIndex = 25u;
constexpr VkFormat kRayQueryResultFormat = VK_FORMAT_R32G32_UINT;

struct alignas(16) RayQueryControls {
    f32 maxRayDistance = 100.0f;
    f32 screenHitConfidenceThreshold = 0.75f;
    f32 originBiasMin = 0.002f;
    f32 originBiasScale = 0.00025f;
    f32 originBiasMax = 0.05f;
    u32 enabled = 0u;
    u32 contractVersion = kRayQueryContractVersion;
    u32 diagnosticsEnabled = 0u;
    u32 instanceMetadataCount = 0u;
    u32 instanceMaterialCount = 0u;
    u32 expectedVertexStride = sizeof(Vertex3D);
    u32 hitAttributesEnabled = 0u;
};

static_assert(sizeof(RayQueryControls) == 48u);
static_assert(offsetof(RayQueryControls, enabled) == 20u);
static_assert(offsetof(RayQueryControls, contractVersion) == 24u);
static_assert(offsetof(RayQueryControls, diagnosticsEnabled) == 28u);
static_assert(offsetof(RayQueryControls, instanceMetadataCount) == 32u);
static_assert(offsetof(RayQueryControls, instanceMaterialCount) == 36u);
static_assert(offsetof(RayQueryControls, expectedVertexStride) == 40u);
static_assert(offsetof(RayQueryControls, hitAttributesEnabled) == 44u);
static_assert(offsetof(Vertex3D, position) == 0u);
static_assert(offsetof(Vertex3D, normal) == 12u);
static_assert(offsetof(Vertex3D, texCoord) == 36u);

bool ExtentsDiffer(VkExtent2D lhs, VkExtent2D rhs) {
    return lhs.width != rhs.width || lhs.height != rhs.height;
}

}

struct VulkanHybridReflectionRayQuery::Impl {
    Impl(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        const VulkanFfxSssrConstantsDescriptorSetLayout& constantsLayout,
        const VulkanFfxSssrClassifyTilesResources& classifyResources,
        const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
        const VulkanFfxSssrBlueNoiseResources& blueNoiseResources,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanDepthPyramid& depthPyramid,
        const std::string& computeShaderPath
    ) : deviceHandle(device.Handle()),
        extent(renderTargets.Extent()),
        classifyResources(classifyResources),
        prepareResources(prepareResources) {
        const std::size_t count = renderTargets.Count();
        if (count == 0u || extent.width == 0u || extent.height == 0u) {
            throw std::runtime_error(
                "Hybrid reflection Ray Query requires non-empty frame resources"
            );
        }
        if (classifyResources.Count() != count ||
            prepareResources.Count() != count ||
            blueNoiseResources.Count() != count ||
            depthPyramid.Count() != count ||
            ExtentsDiffer(classifyResources.Extent(), extent) ||
            ExtentsDiffer(depthPyramid.Extent(), extent)) {
            throw std::runtime_error(
                "Hybrid reflection Ray Query producer resources do not match"
            );
        }

        CreateDescriptorSetLayout(device);
        CreateResources(
            device,
            physicalDevice,
            commandPool,
            classifyResources,
            prepareResources,
            blueNoiseResources,
            renderTargets
        );

        const std::array<VkDescriptorSetLayout, 2> layouts{
            constantsLayout.Handle(),
            descriptorSetLayout
        };
        pipeline = std::make_unique<VulkanComputePipeline>(
            device,
            std::span<const VkDescriptorSetLayout>(layouts),
            computeShaderPath
        );
    }

    ~Impl() {
        pipeline.reset();
        descriptorSets.clear();
        diagnosticsBuffers.clear();
        instanceMetadataBuffers.clear();
        controlsBuffers.clear();
        resultImages.clear();
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(deviceHandle, descriptorPool, nullptr);
        }
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(
                deviceHandle,
                descriptorSetLayout,
                nullptr
            );
        }
    }

    void CreateDescriptorSetLayout(const VulkanDevice& device) {
        std::array<VkDescriptorSetLayoutBinding, 12> bindings{};
        auto setBinding = [&](u32 binding, VkDescriptorType type) {
            bindings[binding].binding = binding;
            bindings[binding].descriptorType = type;
            bindings[binding].descriptorCount = 1u;
            bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        };
        setBinding(0u, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
        for (u32 binding = 1u; binding <= 5u; ++binding) {
            setBinding(binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        }
        setBinding(6u, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
        setBinding(7u, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
        setBinding(8u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        setBinding(9u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        setBinding(10u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        setBinding(11u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<u32>(bindings.size());
        createInfo.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(
                device.Handle(),
                &createInfo,
                nullptr,
                &descriptorSetLayout
            ) != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to create hybrid reflection Ray Query descriptor layout"
            );
        }
    }

    void CreateResources(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        const VulkanFfxSssrClassifyTilesResources& classify,
        const VulkanFfxSssrPrepareIndirectArgsResources& prepare,
        const VulkanFfxSssrBlueNoiseResources& blueNoise,
        const VulkanSceneRenderTargets& renderTargets
    ) {
        const std::size_t count = renderTargets.Count();
        constexpr VkMemoryPropertyFlags hostMemory =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        resultImages.reserve(count);
        controlsBuffers.reserve(count);
        diagnosticsBuffers.reserve(count);
        instanceMetadataBuffers.reserve(count);
        submitted.assign(count, false);
        frameEnabled.assign(count, false);
        tlasDescriptorReady.assign(count, false);

        std::array<u32, kDiagnosticValueCount> diagnostics{};
        diagnostics[kHitDistanceMinDiagnosticIndex] =
            std::numeric_limits<u32>::max();
        diagnostics[kNormalLengthMinDiagnosticIndex] =
            std::numeric_limits<u32>::max();
        diagnostics[kBarycentricSumMinDiagnosticIndex] =
            std::numeric_limits<u32>::max();
        const RayQueryControls defaultControls{};
        for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
            auto result = std::make_unique<VulkanImage>(
                device,
                physicalDevice,
                extent,
                kRayQueryResultFormat,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT
            );
            result->TransitionLayout(
                device,
                commandPool,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL
            );
            auto controls = std::make_unique<VulkanBuffer>(
                device,
                physicalDevice,
                sizeof(RayQueryControls),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                hostMemory
            );
            auto diagnosticBuffer = std::make_unique<VulkanBuffer>(
                device,
                physicalDevice,
                sizeof(diagnostics),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                hostMemory
            );
            auto instanceMetadataBuffer = std::make_unique<VulkanBuffer>(
                device,
                physicalDevice,
                sizeof(HybridReflectionInstanceMetadata) *
                    kMaxHybridReflectionInstances,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                hostMemory
            );
            controls->Upload(
                std::as_bytes(std::span{ &defaultControls, 1u })
            );
            diagnosticBuffer->Upload(
                std::as_bytes(std::span<const u32>(diagnostics))
            );
            resultImages.push_back(std::move(result));
            controlsBuffers.push_back(std::move(controls));
            diagnosticsBuffers.push_back(std::move(diagnosticBuffer));
            instanceMetadataBuffers.push_back(
                std::move(instanceMetadataBuffer)
            );
        }

        std::array<VkDescriptorPoolSize, 7> poolSizes{};
        poolSizes[0] = {
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            static_cast<u32>(count)
        };
        poolSizes[1] = {
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            static_cast<u32>(count * 5u)
        };
        poolSizes[2] = {
            VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
            static_cast<u32>(count)
        };
        poolSizes[3] = {
            VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
            static_cast<u32>(count)
        };
        poolSizes[4] = {
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            static_cast<u32>(count)
        };
        poolSizes[5] = {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            static_cast<u32>(count)
        };
        poolSizes[6] = {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            static_cast<u32>(count * 2u)
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<u32>(count);
        if (vkCreateDescriptorPool(
                device.Handle(),
                &poolInfo,
                nullptr,
                &descriptorPool
            ) != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to create hybrid reflection Ray Query descriptor pool"
            );
        }

        std::vector<VkDescriptorSetLayout> layouts(count, descriptorSetLayout);
        descriptorSets.resize(count);
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = static_cast<u32>(count);
        allocateInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(
                device.Handle(),
                &allocateInfo,
                descriptorSets.data()
            ) != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to allocate hybrid reflection Ray Query descriptor sets"
            );
        }

        for (std::size_t imageIndex = 0; imageIndex < count; ++imageIndex) {
            std::array<VkDescriptorImageInfo, 5> sampledImages{};
            sampledImages[0] = {
                VK_NULL_HANDLE,
                renderTargets.SceneDepthView(imageIndex),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            };
            sampledImages[1] = {
                VK_NULL_HANDLE,
                renderTargets.GBufferNormalRoughnessView(imageIndex),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            sampledImages[2] = {
                VK_NULL_HANDLE,
                classify.ExtractedRoughnessView(imageIndex),
                VK_IMAGE_LAYOUT_GENERAL
            };
            sampledImages[3] = {
                VK_NULL_HANDLE,
                blueNoise.BlueNoiseView(imageIndex),
                VK_IMAGE_LAYOUT_GENERAL
            };
            sampledImages[4] = {
                VK_NULL_HANDLE,
                classify.HitConfidenceView(imageIndex),
                VK_IMAGE_LAYOUT_GENERAL
            };
            VkBufferView rayListView = classify.RayListBufferView(imageIndex);
            VkBufferView rayCounterView =
                prepare.RayCounterBufferView(imageIndex);
            VkDescriptorImageInfo resultImage{
                VK_NULL_HANDLE,
                resultImages[imageIndex]->View(),
                VK_IMAGE_LAYOUT_GENERAL
            };
            VkDescriptorBufferInfo controlsInfo{
                controlsBuffers[imageIndex]->Handle(),
                0u,
                sizeof(RayQueryControls)
            };
            VkDescriptorBufferInfo diagnosticsInfo{
                diagnosticsBuffers[imageIndex]->Handle(),
                0u,
                VK_WHOLE_SIZE
            };
            VkDescriptorBufferInfo instanceMetadataInfo{
                instanceMetadataBuffers[imageIndex]->Handle(),
                0u,
                VK_WHOLE_SIZE
            };

            std::array<VkWriteDescriptorSet, 11> writes{};
            for (u32 sourceIndex = 0u; sourceIndex < sampledImages.size();
                ++sourceIndex) {
                VkWriteDescriptorSet& write = writes[sourceIndex];
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptorSets[imageIndex];
                write.dstBinding = sourceIndex + 1u;
                write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                write.descriptorCount = 1u;
                write.pImageInfo = &sampledImages[sourceIndex];
            }
            writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[5].dstSet = descriptorSets[imageIndex];
            writes[5].dstBinding = 6u;
            writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            writes[5].descriptorCount = 1u;
            writes[5].pTexelBufferView = &rayListView;
            writes[6] = writes[5];
            writes[6].dstBinding = 7u;
            writes[6].descriptorType =
                VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            writes[6].pTexelBufferView = &rayCounterView;
            writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[7].dstSet = descriptorSets[imageIndex];
            writes[7].dstBinding = 8u;
            writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[7].descriptorCount = 1u;
            writes[7].pImageInfo = &resultImage;
            writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[8].dstSet = descriptorSets[imageIndex];
            writes[8].dstBinding = 9u;
            writes[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[8].descriptorCount = 1u;
            writes[8].pBufferInfo = &controlsInfo;
            writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[9].dstSet = descriptorSets[imageIndex];
            writes[9].dstBinding = 10u;
            writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[9].descriptorCount = 1u;
            writes[9].pBufferInfo = &diagnosticsInfo;
            writes[10] = writes[9];
            writes[10].dstBinding = 11u;
            writes[10].pBufferInfo = &instanceMetadataInfo;
            vkUpdateDescriptorSets(
                device.Handle(),
                static_cast<u32>(writes.size()),
                writes.data(),
                0u,
                nullptr
            );
        }
    }

    void PrepareFrame(
        const VulkanDevice& device,
        u32 imageIndex,
        VkAccelerationStructureKHR topLevelAccelerationStructure,
        std::span<const HybridReflectionInstanceMetadata> instanceMetadata,
        u32 instanceMaterialCount,
        bool enabled,
        bool hitAttributesEnabled,
        const HybridReflectionRayQuerySettings& settings,
        RendererHybridReflectionStats& stats
    ) {
        if (imageIndex >= descriptorSets.size()) {
            throw std::runtime_error(
                "Hybrid reflection Ray Query frame index is out of range"
            );
        }
        if (instanceMetadata.size() > kMaxHybridReflectionInstances) {
            throw std::runtime_error(
                "Hybrid reflection instance metadata exceeds capacity"
            );
        }
        if (!instanceMetadata.empty()) {
            instanceMetadataBuffers[imageIndex]->Upload(
                std::as_bytes(instanceMetadata)
            );
        }

        RayQueryControls controls{};
        controls.maxRayDistance = std::max(settings.maxRayDistance, 0.01f);
        controls.screenHitConfidenceThreshold = std::clamp(
            settings.screenHitConfidenceThreshold,
            0.0f,
            1.0f
        );
        controls.originBiasMin = std::max(settings.originBiasMin, 0.0f);
        controls.originBiasScale = std::max(settings.originBiasScale, 0.0f);
        controls.originBiasMax = std::max(
            settings.originBiasMax,
            controls.originBiasMin
        );
        controls.enabled = enabled ? 1u : 0u;
        controls.instanceMetadataCount =
            static_cast<u32>(instanceMetadata.size());
        controls.instanceMaterialCount = instanceMaterialCount;
        controls.hitAttributesEnabled =
            enabled && hitAttributesEnabled ? 1u : 0u;
#if !defined(NDEBUG)
        controls.diagnosticsEnabled = enabled ? 1u : 0u;
#endif
        controlsBuffers[imageIndex]->Upload(
            std::as_bytes(std::span{ &controls, 1u })
        );

        std::array<u32, kDiagnosticValueCount> diagnostics{};
        diagnostics[kHitDistanceMinDiagnosticIndex] =
            std::numeric_limits<u32>::max();
        diagnostics[kNormalLengthMinDiagnosticIndex] =
            std::numeric_limits<u32>::max();
        diagnostics[kBarycentricSumMinDiagnosticIndex] =
            std::numeric_limits<u32>::max();
        diagnosticsBuffers[imageIndex]->Upload(
            std::as_bytes(std::span<const u32>(diagnostics))
        );
        submitted[imageIndex] = false;

        tlasDescriptorReady[imageIndex] =
            topLevelAccelerationStructure != VK_NULL_HANDLE;
        if (tlasDescriptorReady[imageIndex]) {
            VkWriteDescriptorSetAccelerationStructureKHR accelerationInfo{};
            accelerationInfo.sType =
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            accelerationInfo.accelerationStructureCount = 1u;
            accelerationInfo.pAccelerationStructures =
                &topLevelAccelerationStructure;
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext = &accelerationInfo;
            write.dstSet = descriptorSets[imageIndex];
            write.dstBinding = 0u;
            write.descriptorType =
                VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            write.descriptorCount = 1u;
            vkUpdateDescriptorSets(
                device.Handle(),
                1u,
                &write,
                0u,
                nullptr
            );
        }
        frameEnabled[imageIndex] = enabled && tlasDescriptorReady[imageIndex];

        stats.rayQueryConsumerContractVersion = kRayQueryContractVersion;
        stats.rayQueryHitAttributeContractVersion =
            kHitAttributeContractVersion;
        stats.rayQueryResourcesReady = 1u;
        stats.rayQueryTlasDescriptorReady =
            tlasDescriptorReady[imageIndex] ? 1u : 0u;
        stats.rayQueryDispatchReady = frameEnabled[imageIndex] ? 1u : 0u;
        stats.rayQueryResultWidth = extent.width;
        stats.rayQueryResultHeight = extent.height;
        stats.rayQueryResultFormat = static_cast<u32>(kRayQueryResultFormat);
        stats.rayQueryMemoryBytes = TotalMemoryBytes();
        stats.rayQueryInstanceMetadataResourcesReady = 1u;
        stats.rayQueryInstanceMetadataCapacity =
            kMaxHybridReflectionInstances;
        stats.rayQueryInstanceMetadataUploadCount =
            instanceMetadata.empty() ? 0u : 1u;
        stats.rayQueryInstanceMetadataBytes =
            static_cast<u64>(instanceMetadata.size()) *
            sizeof(HybridReflectionInstanceMetadata);
    }

    void Record(
        VkCommandBuffer commandBuffer,
        u32 imageIndex,
        VkDescriptorSet ffxConstantsDescriptorSet,
        VkBuffer indirectArgsBuffer,
        RendererHybridReflectionStats& stats
    ) {
        if (imageIndex >= descriptorSets.size() || !frameEnabled[imageIndex]) {
            return;
        }

        VkImageMemoryBarrier resultForClear{};
        resultForClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        resultForClear.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        resultForClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        resultForClear.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        resultForClear.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        resultForClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resultForClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resultForClear.image = resultImages[imageIndex]->Handle();
        resultForClear.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        resultForClear.subresourceRange.levelCount = 1u;
        resultForClear.subresourceRange.layerCount = 1u;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u,
            0u,
            nullptr,
            0u,
            nullptr,
            1u,
            &resultForClear
        );
        constexpr VkClearColorValue clearValue{};
        vkCmdClearColorImage(
            commandBuffer,
            resultImages[imageIndex]->Handle(),
            VK_IMAGE_LAYOUT_GENERAL,
            &clearValue,
            1u,
            &resultForClear.subresourceRange
        );

        VkImageMemoryBarrier resultForWrite = resultForClear;
        resultForWrite.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        resultForWrite.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        VkImageMemoryBarrier confidenceForRead = resultForClear;
        confidenceForRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        confidenceForRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        confidenceForRead.image =
            classifyResources.HitConfidenceImage(imageIndex);
        std::array<VkImageMemoryBarrier, 2> imageBarriers{
            resultForWrite,
            confidenceForRead
        };

        std::array<VkBufferMemoryBarrier, 4> bufferBarriers{};
        auto setBufferBarrier = [&] (
            VkBufferMemoryBarrier& barrier,
            VkBuffer buffer,
            VkDeviceSize size,
            VkAccessFlags sourceAccess,
            VkAccessFlags destinationAccess
        ) {
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = sourceAccess;
            barrier.dstAccessMask = destinationAccess;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer;
            barrier.offset = 0u;
            barrier.size = size;
        };
        setBufferBarrier(
            bufferBarriers[0],
            classifyResources.RayListBuffer(imageIndex),
            classifyResources.RayListBufferSize(),
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT
        );
        setBufferBarrier(
            bufferBarriers[1],
            prepareResources.RayCounterBuffer(imageIndex),
            prepareResources.RayCounterBufferSize(),
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT
        );
        setBufferBarrier(
            bufferBarriers[2],
            diagnosticsBuffers[imageIndex]->Handle(),
            diagnosticsBuffers[imageIndex]->Size(),
            VK_ACCESS_HOST_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
        );
        setBufferBarrier(
            bufferBarriers[3],
            instanceMetadataBuffers[imageIndex]->Handle(),
            instanceMetadataBuffers[imageIndex]->Size(),
            VK_ACCESS_HOST_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT
        );
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT |
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            0u,
            nullptr,
            static_cast<u32>(bufferBarriers.size()),
            bufferBarriers.data(),
            static_cast<u32>(imageBarriers.size()),
            imageBarriers.data()
        );

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline->Handle()
        );
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline->Layout(),
            0u,
            1u,
            &ffxConstantsDescriptorSet,
            0u,
            nullptr
        );
        const VkDescriptorSet internalDescriptorSet =
            descriptorSets[imageIndex];
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline->Layout(),
            1u,
            1u,
            &internalDescriptorSet,
            0u,
            nullptr
        );
        vkCmdDispatchIndirect(commandBuffer, indirectArgsBuffer, 0u);

        VkBufferMemoryBarrier diagnosticsForHost = bufferBarriers[2];
        diagnosticsForHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        diagnosticsForHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0u,
            0u,
            nullptr,
            1u,
            &diagnosticsForHost,
            0u,
            nullptr
        );

        submitted[imageIndex] = true;
        ++stats.rayQueryDispatchCount;
        stats.rayQueryDescriptorBindCount += 2u;
        ++stats.rayQueryResultClearCount;
        stats.active = 0u;
        stats.fallbackReason = 8u;
    }

    HybridReflectionRayQueryDiagnostics ReadDiagnostics(u32 imageIndex) const {
        HybridReflectionRayQueryDiagnostics result{};
#if defined(NDEBUG)
        static_cast<void>(imageIndex);
        return result;
#else
        if (imageIndex >= diagnosticsBuffers.size() || !submitted[imageIndex]) {
            return result;
        }

        std::array<u32, kDiagnosticValueCount> values{};
        diagnosticsBuffers[imageIndex]->Download(
            std::as_writable_bytes(std::span<u32>(values))
        );
        result.valid = true;
        result.candidateRayCount = values[0];
        result.screenHitAcceptedCount = values[1];
        result.traceCount = values[2];
        result.committedHitCount = values[3];
        result.missCount = values[4];
        result.invalidRayCount = values[5];
        result.hitDistanceSumMillimeters = values[6];
        result.hitDistanceMinMillimeters = result.committedHitCount > 0u
            ? values[7]
            : 0u;
        result.hitDistanceMaxMillimeters = values[8];
        result.resultPixelWriteCount = values[9];
        result.hitAttributeResolvedCount = values[10];
        result.invalidInstanceCount = values[11];
        result.invalidPrimitiveCount = values[12];
        result.invalidVertexCount = values[13];
        result.invalidBarycentricCount = values[14];
        result.invalidAttributeValueCount = values[15];
        result.materialResolvedCount = values[16];
        result.materialFallbackCount = values[17];
        result.positionMismatchCount = values[18];
        result.positionErrorMaxMicrometers = values[19];
        result.normalLengthMinPermille =
            result.hitAttributeResolvedCount > 0u ? values[20] : 0u;
        result.normalLengthMaxPermille = values[21];
        result.identityChecksum = values[22];
        result.primitiveChecksum = values[23];
        result.materialChecksum = values[24];
        result.barycentricSumMinPermille =
            result.hitAttributeResolvedCount > 0u ? values[25] : 0u;
        result.barycentricSumMaxPermille = values[26];
        return result;
#endif
    }

    u64 TotalMemoryBytes() const {
        const u64 resultBytes = static_cast<u64>(extent.width) *
            static_cast<u64>(extent.height) * sizeof(u32) * 2u;
        const u64 perFrameBytes = resultBytes + sizeof(RayQueryControls) +
            sizeof(u32) * kDiagnosticValueCount +
            sizeof(HybridReflectionInstanceMetadata) *
                kMaxHybridReflectionInstances;
        return static_cast<u64>(descriptorSets.size()) * perFrameBytes;
    }

    VkDevice deviceHandle = VK_NULL_HANDLE;
    VkExtent2D extent{};
    const VulkanFfxSssrClassifyTilesResources& classifyResources;
    const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<std::unique_ptr<VulkanImage>> resultImages;
    std::vector<std::unique_ptr<VulkanBuffer>> controlsBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> diagnosticsBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> instanceMetadataBuffers;
    std::vector<bool> submitted;
    std::vector<bool> frameEnabled;
    std::vector<bool> tlasDescriptorReady;
    std::unique_ptr<VulkanComputePipeline> pipeline;
};

VulkanHybridReflectionRayQuery::VulkanHybridReflectionRayQuery(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanCommandPool& commandPool,
    const VulkanFfxSssrConstantsDescriptorSetLayout& constantsLayout,
    const VulkanFfxSssrClassifyTilesResources& classifyResources,
    const VulkanFfxSssrPrepareIndirectArgsResources& prepareResources,
    const VulkanFfxSssrBlueNoiseResources& blueNoiseResources,
    const VulkanSceneRenderTargets& renderTargets,
    const VulkanDepthPyramid& depthPyramid,
    const std::string& computeShaderPath
) : m_Impl(std::make_unique<Impl>(
        device,
        physicalDevice,
        commandPool,
        constantsLayout,
        classifyResources,
        prepareResources,
        blueNoiseResources,
        renderTargets,
        depthPyramid,
        computeShaderPath
    )) {
}

VulkanHybridReflectionRayQuery::~VulkanHybridReflectionRayQuery() = default;

void VulkanHybridReflectionRayQuery::PrepareFrame(
    const VulkanDevice& device,
    u32 imageIndex,
    VkAccelerationStructureKHR topLevelAccelerationStructure,
    std::span<const HybridReflectionInstanceMetadata> instanceMetadata,
    u32 instanceMaterialCount,
    bool enabled,
    bool hitAttributesEnabled,
    const HybridReflectionRayQuerySettings& settings,
    RendererHybridReflectionStats& stats
) {
    m_Impl->PrepareFrame(
        device,
        imageIndex,
        topLevelAccelerationStructure,
        instanceMetadata,
        instanceMaterialCount,
        enabled,
        hitAttributesEnabled,
        settings,
        stats
    );
}

void VulkanHybridReflectionRayQuery::Record(
    VkCommandBuffer commandBuffer,
    u32 imageIndex,
    VkDescriptorSet ffxConstantsDescriptorSet,
    VkBuffer indirectArgsBuffer,
    RendererHybridReflectionStats& stats
) {
    m_Impl->Record(
        commandBuffer,
        imageIndex,
        ffxConstantsDescriptorSet,
        indirectArgsBuffer,
        stats
    );
}

HybridReflectionRayQueryDiagnostics
VulkanHybridReflectionRayQuery::ReadDiagnostics(u32 imageIndex) const {
    return m_Impl->ReadDiagnostics(imageIndex);
}

std::size_t VulkanHybridReflectionRayQuery::Count() const {
    return m_Impl->descriptorSets.size();
}

VkExtent2D VulkanHybridReflectionRayQuery::Extent() const {
    return m_Impl->extent;
}

VkFormat VulkanHybridReflectionRayQuery::ResultFormat() const {
    return kRayQueryResultFormat;
}

u64 VulkanHybridReflectionRayQuery::TotalMemoryBytes() const {
    return m_Impl->TotalMemoryBytes();
}

}
