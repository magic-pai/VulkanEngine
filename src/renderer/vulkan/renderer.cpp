#include "renderer/vulkan/renderer.h"

#include "renderer/vulkan/ibl_generator.h"
#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/compute_pipeline.h"
#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/descriptor_set_layout.h"
#include "renderer/vulkan/descriptor_sets.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/features/height_fog_feature.h"
#include "renderer/vulkan/features/post_process_feature.h"
#include "renderer/vulkan/features/reflection_probe_fallback_feature.h"
#include "renderer/vulkan/features/ssao_feature.h"
#include "renderer/vulkan/features/ssr_feature.h"
#include "renderer/vulkan/framebuffer.h"
#include "renderer/vulkan/frame_materials.h"
#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/gpu_timer.h"
#include "renderer/vulkan/graphics_pipeline.h"
#include "renderer/vulkan/imgui_layer.h"
#include "renderer/vulkan/instance_buffer.h"
#include "renderer/vulkan/local_shadow_atlas.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/reflection_probe_resources.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "renderer/vulkan/render_targets.h"
#include "renderer/vulkan/render_pass.h"
#include "renderer/vulkan/shadow_cascade_atlas.h"
#include "renderer/vulkan/shadow_framebuffer.h"
#include "renderer/vulkan/shadow_map.h"
#include "renderer/vulkan/shadow_render_pass.h"
#include "renderer/vulkan/shadow_settings.h"
#include "renderer/vulkan/sampler.h"
#include "renderer/vulkan/surface.h"
#include "renderer/vulkan/swapchain.h"
#include "renderer/vulkan/sync_objects.h"
#include "renderer/vulkan/texture_2d.h"
#include "renderer/vulkan/uniform_buffer.h"
#include "scene/camera_2d.h"
#include "scene/camera_3d.h"
#include "scene/renderable_2d.h"
#include "scene/scene_2d.h"
#include "scene/scene_3d.h"
#include "scene/transform.h"
#include "platform/window.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>
#include <limits>
#include <optional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <imgui.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#ifndef SE_ASSET_DIR
#define SE_ASSET_DIR "assets"
#endif

namespace se {

static_assert(
    kRendererMaxFrameLocalLights == kMaxFrameLocalLights,
    "Renderer and GPU light-buffer local-light capacities must match"
);

FrameLightConstants FrameLightSet::Constants() const {
    glm::vec3 direction = primaryDirectional.direction;
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = glm::vec3(-0.45f, -0.82f, -0.35f);
    }
    direction = glm::normalize(direction);

    FrameLightConstants constants{};
    constants.directionalLight = glm::vec4(
        direction,
        std::max(primaryDirectional.intensity, 0.0f)
    );
    constants.ambientLight = glm::vec4(
        std::max(primaryDirectional.ambient, 0.0f),
        std::max(primaryDirectional.specular, 0.0f),
        0.0f,
        0.0f
    );
    return constants;
}

class VulkanBonePaletteFallbackDescriptorSet {
public:
    VulkanBonePaletteFallbackDescriptorSet(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice
    ) : m_Device(device.Handle()) {
        Create(device, physicalDevice);
    }

    ~VulkanBonePaletteFallbackDescriptorSet() {
        Release();
    }

    SE_DISABLE_COPY(VulkanBonePaletteFallbackDescriptorSet);
    SE_DISABLE_MOVE(VulkanBonePaletteFallbackDescriptorSet);

    VkDescriptorSet Handle() const { return m_Set; }
    u32 Ready() const { return m_Ready; }

private:
    void Create(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice
    ) {
        const std::array<glm::mat4, 2> identityPalette{
            glm::mat4{ 1.0f },
            glm::mat4{ 1.0f }
        };
        const VkDeviceSize bufferSize =
            static_cast<VkDeviceSize>(sizeof(glm::mat4) * identityPalette.size());
        m_Buffer = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        m_Buffer->Upload(std::as_bytes(std::span<const glm::mat4>(identityPalette)));

        VkDescriptorSetLayoutBinding paletteBinding =
            BonePaletteDescriptorSetLayoutBinding();
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &paletteBinding;
        if (vkCreateDescriptorSetLayout(
                m_Device,
                &layoutInfo,
                nullptr,
                &m_Layout
            ) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create fallback bone palette descriptor layout");
        }

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;
        if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_Pool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create fallback bone palette descriptor pool");
        }

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_Pool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &m_Layout;
        if (vkAllocateDescriptorSets(m_Device, &allocateInfo, &m_Set) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate fallback bone palette descriptor set");
        }

        VkDescriptorBufferInfo descriptorInfo{};
        descriptorInfo.buffer = m_Buffer->Handle();
        descriptorInfo.offset = 0;
        descriptorInfo.range = m_Buffer->Size();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_Set;
        write.dstBinding = paletteBinding.binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &descriptorInfo;
        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);

        m_Ready =
            m_Set != VK_NULL_HANDLE &&
            m_Buffer->Handle() != VK_NULL_HANDLE &&
            m_Buffer->Size() >= bufferSize
                ? 1u
                : 0u;
    }

    void Release() {
        m_Set = VK_NULL_HANDLE;
        if (m_Pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
            m_Pool = VK_NULL_HANDLE;
        }
        if (m_Layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
            m_Layout = VK_NULL_HANDLE;
        }
        m_Buffer.reset();
        m_Ready = 0;
    }

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    std::unique_ptr<VulkanBuffer> m_Buffer;
    VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_Set = VK_NULL_HANDLE;
    u32 m_Ready = 0;
};

namespace {

constexpr f32 kShadowMinHalfExtent = 2.5f;
constexpr f32 kShadowPaddingRatio = 0.18f;
constexpr f32 kShadowCascadeReceiverGuardRatio = 0.25f;
constexpr f32 kShadowDepthPadding = 4.0f;
constexpr f32 kLocalShadowNearPlane = 0.05f;
const glm::vec3 kProbeGridOrigin{ -18.0f, -4.0f, -18.0f };
constexpr f32 kProbeGridSpacing = 12.0f;
constexpr f32 kDlssSrQualityScale = 2.0f / 3.0f;
constexpr f32 kDlssSrBalancedScale = 0.58f;
constexpr f32 kDlssSrPerformanceScale = 0.5f;
constexpr f32 kDlssSrQualityMipLodBias = -1.58496f;
constexpr f32 kDlssSrBalancedMipLodBias = -1.78588f;
constexpr f32 kDlssSrPerformanceMipLodBias = -2.0f;
constexpr std::array<u8, 4> kVisibleSkyboxFallbackPixel = {
    112u,
    126u,
    142u,
    255u
};

std::string ReadEnvironmentString(const char* name);
constexpr u32 kTemporalConsumerSsrBit = 1u << 0u;
constexpr u32 kTemporalConsumerGtaoBit = 1u << 1u;
constexpr u32 kTemporalConsumerMotionBlurBit = 1u << 2u;
constexpr u32 kTemporalConsumerDynamicResolutionBit = 1u << 3u;
constexpr u32 kTemporalConsumerUpscalerBit = 1u << 4u;
constexpr u32 kTemporalUpscaleInputHdrSceneColorBit = 1u << 0u;
constexpr u32 kTemporalUpscaleInputSceneDepthBit = 1u << 1u;
constexpr u32 kTemporalUpscaleInputVelocityBit = 1u << 2u;
constexpr u32 kTemporalUpscaleInputHistoryColorBit = 1u << 3u;
constexpr u32 kTemporalUpscaleInputFrameStateBit = 1u << 4u;
constexpr u32 kTemporalUpscaleRequiredInputMask =
    kTemporalUpscaleInputHdrSceneColorBit |
    kTemporalUpscaleInputSceneDepthBit |
    kTemporalUpscaleInputVelocityBit |
    kTemporalUpscaleInputHistoryColorBit |
    kTemporalUpscaleInputFrameStateBit;
constexpr u32 kDlssQualityEvaluateOutputBit = 1u << 0u;
constexpr u32 kDlssQualityCameraMotionBit = 1u << 1u;
constexpr u32 kDlssQualityObjectMotionBit = 1u << 2u;
constexpr u32 kDlssQualityReactiveMaskBit = 1u << 3u;
constexpr u32 kDlssQualityTransparencyMaskBit = 1u << 4u;
constexpr u32 kDlssQualityExposurePolicyBit = 1u << 5u;
constexpr u32 kDlssQualityPostOrderingBit = 1u << 6u;
constexpr u32 kDlssQualityReferenceBaselineBit = 1u << 7u;
constexpr u32 kDlssQualityRequiredMask =
    kDlssQualityEvaluateOutputBit |
    kDlssQualityCameraMotionBit |
    kDlssQualityObjectMotionBit |
    kDlssQualityReactiveMaskBit |
    kDlssQualityTransparencyMaskBit |
    kDlssQualityExposurePolicyBit |
    kDlssQualityPostOrderingBit |
    kDlssQualityReferenceBaselineBit;

using FrameClock = std::chrono::steady_clock;

f32 ElapsedMilliseconds(FrameClock::time_point start, FrameClock::time_point end) {
    return std::chrono::duration<f32, std::milli>(end - start).count();
}

RendererDlssQualityGateFallbackReason DlssQualityGateFallbackFromBlockers(
    u32 blockerMask
) {
    if ((blockerMask & kDlssQualityEvaluateOutputBit) != 0u) {
        return RendererDlssQualityGateFallbackReason::EvaluateOutputUnavailable;
    }
    if ((blockerMask & kDlssQualityCameraMotionBit) != 0u) {
        return RendererDlssQualityGateFallbackReason::CameraMotionVectorsUnavailable;
    }
    if ((blockerMask & kDlssQualityObjectMotionBit) != 0u) {
        return RendererDlssQualityGateFallbackReason::ObjectMotionVectorsUnavailable;
    }
    if ((blockerMask & kDlssQualityReactiveMaskBit) != 0u) {
        return RendererDlssQualityGateFallbackReason::ReactiveMaskUnavailable;
    }
    if ((blockerMask & kDlssQualityTransparencyMaskBit) != 0u) {
        return RendererDlssQualityGateFallbackReason::TransparencyMaskUnavailable;
    }
    if ((blockerMask & kDlssQualityExposurePolicyBit) != 0u) {
        return RendererDlssQualityGateFallbackReason::ExposurePolicyUnverified;
    }
    if ((blockerMask & kDlssQualityPostOrderingBit) != 0u) {
        return RendererDlssQualityGateFallbackReason::PostOrderingUnverified;
    }
    if ((blockerMask & kDlssQualityReferenceBaselineBit) != 0u) {
        return RendererDlssQualityGateFallbackReason::ReferenceBaselineMissing;
    }
    return RendererDlssQualityGateFallbackReason::None;
}

void FinalizeDlssQualityGateStats(RendererTemporalStats& stats) {
    stats.temporalUpscalerDlssQualityRequiredMask = kDlssQualityRequiredMask;
    if (stats.temporalUpscalerDlssQualityGateRequested == 0u) {
        stats.temporalUpscalerDlssQualityGateReady = 0u;
        stats.temporalUpscalerDlssQualityReadyMask = 0u;
        stats.temporalUpscalerDlssQualityBlockerMask = 0u;
        stats.temporalUpscalerDlssQualityGateFallbackReason =
            static_cast<u32>(RendererDlssQualityGateFallbackReason::NotRequested);
        return;
    }

    u32 readyMask = 0u;
    if (stats.temporalUpscalerDlssQualityEvaluateOutputReady != 0u) {
        readyMask |= kDlssQualityEvaluateOutputBit;
    }
    if (stats.temporalUpscalerDlssQualityCameraMotionReady != 0u) {
        readyMask |= kDlssQualityCameraMotionBit;
    }
    if (stats.temporalUpscalerDlssQualityObjectMotionReady != 0u) {
        readyMask |= kDlssQualityObjectMotionBit;
    }
    if (stats.temporalUpscalerDlssQualityReactiveMaskReady != 0u) {
        readyMask |= kDlssQualityReactiveMaskBit;
    }
    if (stats.temporalUpscalerDlssQualityTransparencyMaskReady != 0u) {
        readyMask |= kDlssQualityTransparencyMaskBit;
    }
    if (stats.temporalUpscalerDlssQualityExposurePolicyReady != 0u) {
        readyMask |= kDlssQualityExposurePolicyBit;
    }
    if (stats.temporalUpscalerDlssQualityPostOrderingReady != 0u) {
        readyMask |= kDlssQualityPostOrderingBit;
    }
    if (stats.temporalUpscalerDlssQualityReferenceBaselineReady != 0u) {
        readyMask |= kDlssQualityReferenceBaselineBit;
    }

    stats.temporalUpscalerDlssQualityReadyMask = readyMask;
    stats.temporalUpscalerDlssQualityBlockerMask =
        kDlssQualityRequiredMask & ~readyMask;
    stats.temporalUpscalerDlssQualityGateReady =
        stats.temporalUpscalerDlssQualityBlockerMask == 0u ? 1u : 0u;
    stats.temporalUpscalerDlssQualityGateFallbackReason =
        static_cast<u32>(DlssQualityGateFallbackFromBlockers(
            stats.temporalUpscalerDlssQualityBlockerMask
        ));
}

bool DlssObjectMotionVectorsReady(
    bool has3DMainPass,
    bool gBufferPipelineReady,
    bool velocityTargetAllocated,
    const FrameTemporalState& temporalState,
    std::span<const RenderCommand> gBufferCommands,
    std::span<const RenderCommand> weightedTranslucencyCommands,
    std::span<const RenderCommand> forwardResidualCommands,
    bool weightedTranslucencyVelocityCoverageReady,
    bool forwardResidualVelocityCoverageReady
) {
    return has3DMainPass &&
        gBufferPipelineReady &&
        velocityTargetAllocated &&
        temporalState.velocityCameraMotionReady &&
        !gBufferCommands.empty() &&
        (weightedTranslucencyCommands.empty() ||
            weightedTranslucencyVelocityCoverageReady) &&
        (forwardResidualCommands.empty() || forwardResidualVelocityCoverageReady);
}

std::size_t ProbeGridLinearIndex(
    std::size_t x,
    std::size_t y,
    std::size_t z
) {
    return z * kProbeGridSizeY * kProbeGridSizeX + y * kProbeGridSizeX + x;
}

bool ProbeGridLayoutValid() {
    return kProbeGridSpacing > 0.0f &&
        kProbeGridSizeX >= 2 &&
        kProbeGridSizeY >= 2 &&
        kProbeGridSizeZ >= 2 &&
        kProbeGridProbeCount == kProbeGridSizeX * kProbeGridSizeY * kProbeGridSizeZ &&
        kProbeGridVec4sPerProbe == 1 + kProbeGridDirectionalLobeCount &&
        kProbeGridDirectionalLobeCount == 6;
}

glm::vec3 ProbeGridBoundsMax() {
    return kProbeGridOrigin +
        glm::vec3(
            static_cast<f32>(kProbeGridSizeX - 1),
            static_cast<f32>(kProbeGridSizeY - 1),
            static_cast<f32>(kProbeGridSizeZ - 1)
        ) * kProbeGridSpacing;
}

u32 ProbeGridCellCount() {
    if constexpr (kProbeGridSizeX < 2 || kProbeGridSizeY < 2 || kProbeGridSizeZ < 2) {
        return 0;
    }

    return static_cast<u32>(
        (kProbeGridSizeX - 1) *
        (kProbeGridSizeY - 1) *
        (kProbeGridSizeZ - 1)
    );
}

ProbeGridBufferObject BuildDeterministicProbeGridData() {
    ProbeGridBufferObject data{};
    for (std::size_t z = 0; z < kProbeGridSizeZ; ++z) {
        for (std::size_t y = 0; y < kProbeGridSizeY; ++y) {
            for (std::size_t x = 0; x < kProbeGridSizeX; ++x) {
                const glm::vec3 normalized{
                    kProbeGridSizeX > 1
                        ? static_cast<f32>(x) / static_cast<f32>(kProbeGridSizeX - 1)
                        : 0.0f,
                    kProbeGridSizeY > 1
                        ? static_cast<f32>(y) / static_cast<f32>(kProbeGridSizeY - 1)
                        : 0.0f,
                    kProbeGridSizeZ > 1
                        ? static_cast<f32>(z) / static_cast<f32>(kProbeGridSizeZ - 1)
                        : 0.0f
                };
                const glm::vec3 base =
                    glm::vec3(0.16f, 0.18f, 0.21f) +
                    glm::vec3(0.08f, 0.05f, 0.02f) * normalized.x +
                    glm::vec3(0.08f, 0.11f, 0.16f) * normalized.y +
                    glm::vec3(0.02f, 0.04f, 0.08f) * normalized.z;
                GpuProbeGridRecord& record =
                    data.probes[ProbeGridLinearIndex(x, y, z)];
                record.irradiance = glm::vec4(base, 1.0f);
                record.directionalLobes[0] =
                    glm::vec4(glm::vec3(0.035f, 0.020f, 0.012f) * (0.4f + normalized.x), 0.0f);
                record.directionalLobes[1] =
                    glm::vec4(glm::vec3(0.010f, 0.018f, 0.032f) * (1.1f - normalized.x), 0.0f);
                record.directionalLobes[2] =
                    glm::vec4(glm::vec3(0.050f, 0.062f, 0.085f) * (0.45f + normalized.y), 0.0f);
                record.directionalLobes[3] =
                    glm::vec4(glm::vec3(0.035f, 0.028f, 0.018f) * (1.0f - normalized.y * 0.35f), 0.0f);
                record.directionalLobes[4] =
                    glm::vec4(glm::vec3(0.014f, 0.026f, 0.045f) * (0.5f + normalized.z), 0.0f);
                record.directionalLobes[5] =
                    glm::vec4(glm::vec3(0.042f, 0.030f, 0.018f) * (1.1f - normalized.z * 0.5f), 0.0f);
            }
        }
    }
    return data;
}

RendererDrawStats DrawStatsForQueues(
    std::span<const RenderCommand> mainCommands,
    std::span<const RenderCommand> overlayCommands,
    std::span<const RenderCommand> shadowCommands
) {
    auto triangleCount = [](std::span<const RenderCommand> commands) {
        u64 count = 0;
        for (const RenderCommand& command : commands) {
            if (command.mesh != nullptr) {
                count += static_cast<u64>(command.mesh->IndexCount() / 3);
            }
        }

        return count;
    };

    RendererDrawStats stats{};
    stats.mainDraws = static_cast<u32>(mainCommands.size());
    stats.overlayDraws = static_cast<u32>(overlayCommands.size());
    stats.shadowDraws = static_cast<u32>(shadowCommands.size());
    stats.mainTriangles = triangleCount(mainCommands);
    stats.overlayTriangles = triangleCount(overlayCommands);
    stats.shadowTriangles = triangleCount(shadowCommands);

    return stats;
}

RendererBonePaletteDrawStats BonePaletteDrawStatsFor(
    std::span<const RenderCommand> mainCommands,
    bool fallbackDescriptorReady
) {
    RendererBonePaletteDrawStats stats{};
    std::unordered_set<std::string> seenResources;
    for (const RenderCommand& command : mainCommands) {
        if (command.bonePaletteResourceId.empty()) {
            continue;
        }

        ++stats.commandCount;
        if (command.bonePaletteReady != 0u) {
            ++stats.readyCommandCount;
        }
        if (command.bonePaletteDescriptorSet != VK_NULL_HANDLE) {
            ++stats.descriptorCommandCount;
        }
        if (command.bonePaletteDescriptorSetReady != 0u &&
            command.bonePaletteDescriptorSet != VK_NULL_HANDLE) {
            ++stats.descriptorReadyCommandCount;
        }

        ++stats.shaderSkinningCommandCount;
        const u32 paletteEntryCount =
            command.bonePalettePreviousEntryCount +
            command.bonePaletteCurrentEntryCount;
        const u32 requiredPaletteBytes =
            paletteEntryCount * static_cast<u32>(sizeof(glm::mat4));
        if (command.bonePaletteReady != 0u &&
            command.bonePaletteDescriptorSetReady != 0u &&
            command.bonePaletteDescriptorSet != VK_NULL_HANDLE &&
            command.bonePaletteCurrentEntryCount > 0u &&
            command.bonePalettePreviousEntryCount > 0u &&
            command.bonePaletteDescriptorRangeBytes >= requiredPaletteBytes) {
            ++stats.shaderSkinningReadyCommandCount;
        }
        ++stats.shaderVelocityCommandCount;
        if (command.bonePaletteReady != 0u &&
            command.bonePaletteDescriptorSetReady != 0u &&
            command.bonePaletteDescriptorSet != VK_NULL_HANDLE &&
            command.bonePaletteCurrentEntryCount > 0u &&
            command.bonePalettePreviousEntryCount > 0u &&
            command.bonePaletteDescriptorRangeBytes >= requiredPaletteBytes) {
            ++stats.shaderVelocityReadyCommandCount;
        }

        if (!seenResources.insert(command.bonePaletteResourceId).second) {
            continue;
        }

        ++stats.resourceCount;
        stats.currentEntryCount += command.bonePaletteCurrentEntryCount;
        stats.previousEntryCount += command.bonePalettePreviousEntryCount;
        stats.changedEntryCount += command.bonePaletteChangedEntryCount;
        stats.descriptorRangeBytes += command.bonePaletteDescriptorRangeBytes;
        if (command.bonePaletteReady != 0u) {
            ++stats.readyResourceCount;
        }
        if (command.bonePaletteDescriptorSet != VK_NULL_HANDLE) {
            ++stats.descriptorResourceCount;
            stats.descriptorSetIndex = command.bonePaletteDescriptorSetIndex;
            stats.descriptorBinding = command.bonePaletteDescriptorBinding;
        }
        if (command.bonePaletteDescriptorSetReady != 0u &&
            command.bonePaletteDescriptorSet != VK_NULL_HANDLE) {
            ++stats.descriptorReadyResourceCount;
        }
        if (stats.shaderSkinningCurrentPaletteOffset == 0u) {
            stats.shaderSkinningCurrentPaletteOffset =
                command.bonePalettePreviousEntryCount;
            stats.shaderSkinningCurrentEntryCount =
                command.bonePaletteCurrentEntryCount;
        }
        if (stats.shaderVelocityPreviousEntryCount == 0u) {
            stats.shaderVelocityPreviousPaletteOffset = 0u;
            stats.shaderVelocityPreviousEntryCount =
                command.bonePalettePreviousEntryCount;
        }
    }

    stats.drawPathReady =
        stats.commandCount > 0u &&
        stats.commandCount == stats.readyCommandCount &&
        stats.resourceCount > 0u &&
        stats.resourceCount == stats.readyResourceCount &&
        stats.currentEntryCount > 0u &&
        stats.currentEntryCount == stats.previousEntryCount
            ? 1u
            : 0u;
    stats.descriptorPathReady =
        stats.descriptorCommandCount > 0u &&
        stats.descriptorCommandCount == stats.descriptorReadyCommandCount &&
        stats.descriptorResourceCount > 0u &&
        stats.descriptorResourceCount == stats.descriptorReadyResourceCount &&
        stats.descriptorRangeBytes > 0u
            ? 1u
            : 0u;
    stats.shaderConsumerCommandCount = stats.descriptorCommandCount;
    stats.shaderConsumerReadyCommandCount = stats.descriptorReadyCommandCount;
    stats.shaderConsumerFallbackDescriptorReady =
        fallbackDescriptorReady ? 1u : 0u;
    stats.shaderConsumerPathReady =
        stats.descriptorPathReady != 0u && fallbackDescriptorReady ? 1u : 0u;
    stats.shaderSkinningPathReady =
        stats.shaderSkinningCommandCount > 0u &&
        stats.shaderSkinningCommandCount == stats.shaderSkinningReadyCommandCount &&
        stats.shaderSkinningCurrentPaletteOffset > 0u &&
        stats.shaderSkinningCurrentEntryCount > 0u
            ? 1u
            : 0u;
    stats.shaderVelocityPathReady =
        stats.shaderVelocityCommandCount > 0u &&
        stats.shaderVelocityCommandCount == stats.shaderVelocityReadyCommandCount &&
        stats.shaderVelocityPreviousEntryCount > 0u
            ? 1u
            : 0u;

    return stats;
}

u64 TriangleCountForCommand(const RenderCommand& command) {
    if (command.mesh == nullptr) {
        return 0;
    }

    return static_cast<u64>(command.mesh->IndexCount() / 3);
}

u32 CountSkinnedConservativeBounds(std::span<const RenderCommand> commands) {
    u32 count = 0;
    for (const RenderCommand& command : commands) {
        if (command.skinnedWorldBoundsConservative != 0u) {
            ++count;
        }
    }

    return count;
}

glm::vec3 CommandBoundsCenter(const RenderCommand& command) {
    if (command.worldBounds.valid) {
        return (command.worldBounds.min + command.worldBounds.max) * 0.5f;
    }

    return glm::vec3(command.model[3]);
}

glm::vec3 CameraPositionFromMatrices(const FrameMatrices* matrices) {
    if (matrices == nullptr) {
        return glm::vec3(0.0f);
    }

    const glm::mat4 invView = glm::inverse(matrices->view);
    return glm::vec3(invView[3]);
}

f32 DistanceSquaredToCamera(
    const RenderCommand& command,
    const glm::vec3& cameraPosition
) {
    const glm::vec3 delta = CommandBoundsCenter(command) - cameraPosition;
    return glm::dot(delta, delta);
}

MaterialRenderClass RenderClassForCommand(const RenderCommand& command) {
    const f32 alpha = std::clamp(
        command.materialPushConstants.materialBaseColorFactor.a,
        0.0f,
        1.0f
    );

    if (command.material == nullptr) {
        return alpha < 0.999f
            ? MaterialRenderClass::Transparent
            : MaterialRenderClass::DeferredOpaque;
    }

    const MaterialProperties& properties = command.material->Properties();
    if (properties.alphaMode == MaterialAlphaMode::Blend) {
        return MaterialRenderClass::Transparent;
    }
    if (properties.alphaMode != MaterialAlphaMode::Mask && alpha < 0.999f) {
        return MaterialRenderClass::Transparent;
    }

    return properties.renderClass;
}

f32 RenderClassValue(MaterialRenderClass renderClass) {
    return static_cast<f32>(static_cast<u32>(renderClass));
}

f32 AlphaModeValue(MaterialAlphaMode alphaMode) {
    return static_cast<f32>(static_cast<u32>(alphaMode));
}

void IncludePoint(glm::vec3 point, glm::vec3& boundsMin, glm::vec3& boundsMax) {
    boundsMin.x = std::min(boundsMin.x, point.x);
    boundsMin.y = std::min(boundsMin.y, point.y);
    boundsMin.z = std::min(boundsMin.z, point.z);

    boundsMax.x = std::max(boundsMax.x, point.x);
    boundsMax.y = std::max(boundsMax.y, point.y);
    boundsMax.z = std::max(boundsMax.z, point.z);
}

u32 FloatBits(f32 value) {
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

u32 CeilDivU32(u32 value, u32 divisor) {
    if (divisor == 0) {
        return 0;
    }

    return (value + divisor - 1) / divisor;
}

u64 HashCombine(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

u64 HashMatrix(u64 seed, const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            seed = HashCombine(seed, FloatBits(matrix[column][row]));
        }
    }

    return seed;
}

void IncludeCommandBounds(
    const RenderCommand& command,
    glm::vec3& boundsMin,
    glm::vec3& boundsMax,
    bool& hasBounds
) {
    if (!command.worldBounds.valid) {
        return;
    }

    IncludePoint(command.worldBounds.min, boundsMin, boundsMax);
    IncludePoint(command.worldBounds.max, boundsMin, boundsMax);
    hasBounds = true;
}

void ExpandRangeAroundCenter(f32& minValue, f32& maxValue, f32 minHalfExtent) {
    const f32 center = (minValue + maxValue) * 0.5f;
    const f32 halfExtent = std::max((maxValue - minValue) * 0.5f, minHalfExtent);
    minValue = center - halfExtent;
    maxValue = center + halfExtent;
}

void ExpandRangeByRatio(f32& minValue, f32& maxValue, f32 ratio) {
    const f32 clampedRatio = std::max(ratio, 0.0f);
    if (clampedRatio <= 0.0001f) {
        return;
    }

    const f32 center = (minValue + maxValue) * 0.5f;
    const f32 halfExtent = (maxValue - minValue) * 0.5f * (1.0f + clampedRatio);
    minValue = center - halfExtent;
    maxValue = center + halfExtent;
}

f32 EnvironmentFloatOrDefault(const char* name, f32 fallback) {
    const std::string value = ReadEnvironmentString(name);
    if (value.empty()) {
        return fallback;
    }

    char* end = nullptr;
    const f32 parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || !std::isfinite(parsed)) {
        return fallback;
    }

    return parsed;
}

f32 ShadowCascadeReceiverGuardRatio() {
    return std::clamp(
        EnvironmentFloatOrDefault(
            "SE_SHADOW_CASCADE_RECEIVER_GUARD",
            kShadowCascadeReceiverGuardRatio
        ),
        0.0f,
        1.5f
    );
}

glm::vec3 NormalizedDirectionalLightDirection(const FrameLightSet& lights) {
    glm::vec3 lightDirection = lights.primaryDirectional.direction;
    if (glm::dot(lightDirection, lightDirection) <= 0.0001f) {
        lightDirection = glm::vec3(-0.45f, -0.82f, -0.35f);
    }

    return glm::normalize(lightDirection);
}

bool CameraDepthRangeFromMatrices(
    const FrameMatrices* matrices,
    f32& nearDepth,
    f32& farDepth
) {
    if (matrices == nullptr) {
        return false;
    }

    const glm::mat4 inverseView = glm::inverse(matrices->view);
    const glm::mat4 inverseViewProjection =
        glm::inverse(matrices->proj * matrices->view);
    const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);

    auto unproject = [&](f32 x, f32 y, f32 z) {
        const glm::vec4 worldH = inverseViewProjection * glm::vec4(x, y, z, 1.0f);
        if (std::abs(worldH.w) <= 0.000001f) {
            return cameraPosition;
        }

        return glm::vec3(worldH) / worldH.w;
    };

    glm::vec3 nearCenter{ 0.0f };
    glm::vec3 farCenter{ 0.0f };
    for (const f32 x : { -1.0f, 1.0f }) {
        for (const f32 y : { -1.0f, 1.0f }) {
            nearCenter += unproject(x, y, 0.0f);
            farCenter += unproject(x, y, 1.0f);
        }
    }
    nearCenter *= 0.25f;
    farCenter *= 0.25f;

    glm::vec3 forward = farCenter - nearCenter;
    if (glm::dot(forward, forward) <= 0.000001f) {
        forward = -glm::vec3(inverseView[2]);
    }
    if (glm::dot(forward, forward) <= 0.000001f) {
        return false;
    }
    forward = glm::normalize(forward);

    nearDepth = glm::dot(nearCenter - cameraPosition, forward);
    farDepth = glm::dot(farCenter - cameraPosition, forward);
    if (!std::isfinite(nearDepth) || !std::isfinite(farDepth)) {
        return false;
    }
    if (nearDepth <= 0.0f || farDepth <= nearDepth + 0.001f) {
        return false;
    }

    return true;
}

bool CameraSliceCornersFromMatrices(
    const FrameMatrices& matrices,
    f32 nearDepth,
    f32 farDepth,
    std::array<glm::vec3, 8>& corners
) {
    const glm::mat4 inverseView = glm::inverse(matrices.view);
    const glm::mat4 inverseViewProjection =
        glm::inverse(matrices.proj * matrices.view);
    const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);

    auto unproject = [&](f32 x, f32 y, f32 z) {
        const glm::vec4 worldH = inverseViewProjection * glm::vec4(x, y, z, 1.0f);
        if (std::abs(worldH.w) <= 0.000001f) {
            return cameraPosition;
        }

        return glm::vec3(worldH) / worldH.w;
    };

    glm::vec3 farCenter{ 0.0f };
    std::array<glm::vec3, 4> farPlaneCorners{};
    u32 cornerIndex = 0;
    for (const f32 x : { -1.0f, 1.0f }) {
        for (const f32 y : { -1.0f, 1.0f }) {
            farPlaneCorners[cornerIndex] = unproject(x, y, 1.0f);
            farCenter += farPlaneCorners[cornerIndex];
            ++cornerIndex;
        }
    }
    farCenter *= 0.25f;

    glm::vec3 forward = farCenter - cameraPosition;
    if (glm::dot(forward, forward) <= 0.000001f) {
        forward = -glm::vec3(inverseView[2]);
    }
    if (glm::dot(forward, forward) <= 0.000001f) {
        return false;
    }
    forward = glm::normalize(forward);

    for (u32 index = 0; index < 4; ++index) {
        const glm::vec3 ray = farPlaneCorners[index] - cameraPosition;
        const f32 forwardDistance = glm::dot(ray, forward);
        if (std::abs(forwardDistance) <= 0.000001f) {
            return false;
        }

        corners[index] = cameraPosition + ray * (nearDepth / forwardDistance);
        corners[index + 4] = cameraPosition + ray * (farDepth / forwardDistance);
    }

    return true;
}

bool StableCascadeSphereFromMatrices(
    const FrameMatrices& matrices,
    f32 nearDepth,
    f32 farDepth,
    glm::vec3& center,
    f32& radius
) {
    const f32 projectionX = std::abs(matrices.proj[0][0]);
    const f32 projectionY = std::abs(matrices.proj[1][1]);
    if (projectionX <= 0.000001f || projectionY <= 0.000001f ||
        nearDepth <= 0.0f || farDepth <= nearDepth) {
        return false;
    }

    const f32 tanHalfFovX = 1.0f / projectionX;
    const f32 tanHalfFovY = 1.0f / projectionY;
    const f32 tanHalfFovDiagonalSquared =
        tanHalfFovX * tanHalfFovX + tanHalfFovY * tanHalfFovY;
    f32 centerDepth = 0.5f * (nearDepth + farDepth) *
        (1.0f + tanHalfFovDiagonalSquared);
    centerDepth = std::min(centerDepth, farDepth);
    const f32 axialDistance = farDepth - centerDepth;
    radius = std::sqrt(
        farDepth * farDepth * tanHalfFovDiagonalSquared +
        axialDistance * axialDistance
    );

    const glm::mat4 inverseView = glm::inverse(matrices.view);
    const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
    glm::vec3 cameraForward = -glm::vec3(inverseView[2]);
    if (glm::dot(cameraForward, cameraForward) <= 0.000001f) {
        return false;
    }
    cameraForward = glm::normalize(cameraForward);
    center = cameraPosition + cameraForward * centerDepth;
    return std::isfinite(radius) && radius > 0.0f;
}

glm::mat4 LightViewProjectionForCascade(
    std::span<const RenderCommand> renderCommands,
    const FrameLightSet& lights,
    const std::array<glm::vec3, 8>& frustumCorners,
    bool stableSnappingEnabled,
    u32 mapSize,
    f32* texelWorldSize,
    f32* lightDepthWorldSpan,
    const FrameMatrices* cameraMatrices,
    f32 cascadeNearDepth,
    f32 cascadeFarDepth
) {
    const glm::vec3 lightDirection = NormalizedDirectionalLightDirection(lights);
    glm::vec3 center{ 0.0f };
    for (const glm::vec3& worldPoint : frustumCorners) {
        center += worldPoint;
    }
    center /= static_cast<f32>(frustumCorners.size());

    f32 cascadeRadius = kShadowMinHalfExtent;
    for (const glm::vec3& worldPoint : frustumCorners) {
        cascadeRadius = std::max(cascadeRadius, glm::length(worldPoint - center));
    }
    if (stableSnappingEnabled && cameraMatrices != nullptr) {
        glm::vec3 analyticCenter{ 0.0f };
        f32 analyticRadius = 0.0f;
        if (StableCascadeSphereFromMatrices(
                *cameraMatrices,
                cascadeNearDepth,
                cascadeFarDepth,
                analyticCenter,
                analyticRadius)) {
            center = analyticCenter;
            cascadeRadius = std::max(analyticRadius, kShadowMinHalfExtent);
        }
    }
    auto quantizeStableExtent = [](f32 extent) {
        constexpr f32 kQuantizationUnits = 16.0f;
        constexpr f32 kBoundaryTolerance = 0.001f;
        return std::ceil(
            extent * kQuantizationUnits - kBoundaryTolerance
        ) / kQuantizationUnits;
    };
    if (stableSnappingEnabled) {
        // Valient-style stable CSM: quantizing a rotation-invariant sphere fit
        // prevents the orthographic scale from breathing as the camera turns.
        cascadeRadius = quantizeStableExtent(cascadeRadius);
    }

    const glm::vec3 lightForward = glm::normalize(lightDirection);
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    if (std::abs(glm::dot(lightForward, up)) > 0.95f) {
        up = { 0.0f, 0.0f, 1.0f };
    }

    const f32 receiverGuardRatio = ShadowCascadeReceiverGuardRatio();
    f32 stableHalfExtent = 0.0f;
    f32 stableTexelSize = 0.0f;
    glm::vec3 viewCenter = center;
    if (stableSnappingEnabled) {
        stableHalfExtent = quantizeStableExtent(
            cascadeRadius * (1.0f + receiverGuardRatio)
        );
        if (mapSize > 0u) {
            stableTexelSize = (stableHalfExtent * 2.0f) / static_cast<f32>(mapSize);
        }

        if (stableTexelSize > 0.0f) {
            const glm::vec3 lightRight = glm::normalize(glm::cross(lightForward, up));
            const glm::vec3 lightUp = glm::normalize(glm::cross(lightRight, lightForward));
            const f32 centerRight = glm::dot(center, lightRight);
            const f32 centerUp = glm::dot(center, lightUp);
            const f32 snappedRight = std::round(centerRight / stableTexelSize) *
                stableTexelSize;
            const f32 snappedUp = std::round(centerUp / stableTexelSize) *
                stableTexelSize;
            viewCenter += lightRight * (snappedRight - centerRight);
            viewCenter += lightUp * (snappedUp - centerUp);
        }
    }

    const glm::vec3 eye =
        viewCenter - lightForward * (cascadeRadius + kShadowDepthPadding);
    const glm::mat4 view = glm::lookAt(eye, viewCenter, up);
    glm::vec3 lightBoundsMin{ std::numeric_limits<f32>::max() };
    glm::vec3 lightBoundsMax{ std::numeric_limits<f32>::lowest() };
    for (const glm::vec3& worldPoint : frustumCorners) {
        IncludePoint(
            glm::vec3(view * glm::vec4(worldPoint, 1.0f)),
            lightBoundsMin,
            lightBoundsMax
        );
    }

    for (const RenderCommand& command : renderCommands) {
        if (!command.worldBounds.valid) {
            continue;
        }

        for (const glm::vec3& worldPoint : command.worldBounds.corners) {
            const glm::vec3 lightPoint =
                glm::vec3(view * glm::vec4(worldPoint, 1.0f));
            lightBoundsMin.z = std::min(lightBoundsMin.z, lightPoint.z);
            lightBoundsMax.z = std::max(lightBoundsMax.z, lightPoint.z);
        }
    }

    lightBoundsMin.z -= kShadowDepthPadding;
    lightBoundsMax.z += kShadowDepthPadding;
    if (stableSnappingEnabled) {
        lightBoundsMin.x = -stableHalfExtent;
        lightBoundsMax.x = stableHalfExtent;
        lightBoundsMin.y = -stableHalfExtent;
        lightBoundsMax.y = stableHalfExtent;
    } else {
        const f32 xPadding = std::max(
            (lightBoundsMax.x - lightBoundsMin.x) * kShadowPaddingRatio,
            0.35f
        );
        const f32 yPadding = std::max(
            (lightBoundsMax.y - lightBoundsMin.y) * kShadowPaddingRatio,
            0.35f
        );
        lightBoundsMin.x -= xPadding;
        lightBoundsMax.x += xPadding;
        lightBoundsMin.y -= yPadding;
        lightBoundsMax.y += yPadding;
        ExpandRangeAroundCenter(lightBoundsMin.x, lightBoundsMax.x, kShadowMinHalfExtent);
        ExpandRangeAroundCenter(lightBoundsMin.y, lightBoundsMax.y, kShadowMinHalfExtent);
        ExpandRangeByRatio(lightBoundsMin.x, lightBoundsMax.x, receiverGuardRatio);
        ExpandRangeByRatio(lightBoundsMin.y, lightBoundsMax.y, receiverGuardRatio);
    }

    f32 texelSize = stableSnappingEnabled ? stableTexelSize : 0.0f;
    if (!stableSnappingEnabled && mapSize > 0u) {
        texelSize = (lightBoundsMax.x - lightBoundsMin.x) /
            static_cast<f32>(mapSize);
    }
    if (texelWorldSize != nullptr) {
        *texelWorldSize = texelSize;
    }

    const f32 nearPlane = std::max(0.01f, -lightBoundsMax.z);
    const f32 farPlane = std::max(nearPlane + 0.1f, -lightBoundsMin.z);
    if (lightDepthWorldSpan != nullptr) {
        *lightDepthWorldSpan = farPlane - nearPlane;
    }
    glm::mat4 projection = glm::ortho(
        lightBoundsMin.x,
        lightBoundsMax.x,
        lightBoundsMin.y,
        lightBoundsMax.y,
        nearPlane,
        farPlane
    );
    projection[1][1] *= -1.0f;
    return projection * view;
}

RendererShadowCascadeStats ShadowCascadeStatsFor(
    const DirectionalShadowCascadeSet& cascades
) {
    RendererShadowCascadeStats stats{};
    stats.configuredCount = cascades.configuredCount;
    stats.activeCount = cascades.activeCount;
    stats.stableSnappingEnabled = cascades.stableSnappingEnabled ? 1u : 0u;
    stats.splitLambda = cascades.splitLambda;
    stats.maxDistance = cascades.maxDistance;
    stats.nearDepth = cascades.nearDepth;
    stats.farDepth = cascades.farDepth;
    stats.pcssLightAngularRadiusRadians = cascades.lightAngularRadiusRadians;
    for (u32 index = 0; index < std::min<u32>(cascades.activeCount, kMaxDirectionalShadowCascades); ++index) {
        stats.splitDepths[index] = cascades.cascades[index].splitDepth;
        stats.texelWorldSizes[index] = cascades.cascades[index].texelWorldSize;
        stats.lightDepthWorldSpans[index] =
            cascades.cascades[index].lightDepthWorldSpan;
    }

    return stats;
}

struct LocalShadowTileBudget {
    u32 shadowableLightCount = 0;
    u32 pointLightCount = 0;
    u32 spotLightCount = 0;
    u32 rectLightCount = 0;
    u32 pointFaceTiles = 0;
    u32 spotTiles = 0;
    u32 rectTiles = 0;
    u32 requestedTiles = 0;
};

inline constexpr u32 kRectAreaShadowBaseSampleTileCount = 2u;
inline constexpr u32 kRectAreaShadowMaxSampleTileCount = 4u;
inline constexpr f32 kRectAreaShadowSampleOffset = 0.57735f;
inline constexpr u32 kRectAreaShadowPatternAxis = 0u;
inline constexpr u32 kRectAreaShadowPatternSurface2x2 = 1u;
inline constexpr u32 kShadowQualityBudgetContractVersion = 1u;
inline constexpr u32 kLocalShadowFilterContractVersion = 3u;

u32 DepthFormatLogicalBytesPerTexel(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM:
        return 2u;
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return 4u;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return 8u;
    default:
        return 0u;
    }
}

u64 ShadowDepthLogicalBytes(
    VkExtent2D extent,
    VkFormat format,
    std::size_t imageCount
) {
    return static_cast<u64>(extent.width) * static_cast<u64>(extent.height) *
        static_cast<u64>(DepthFormatLogicalBytesPerTexel(format)) *
        static_cast<u64>(imageCount);
}

u32 LocalShadowProjectionSampleBudget(
    const VulkanLocalShadowFilterSettings& filter,
    bool productionFilterEnabled
) {
    if (!productionFilterEnabled) {
        const u32 kernelRadius = std::clamp<u32>(filter.pcfKernelRadius, 0u, 2u);
        const u32 kernelWidth = kernelRadius * 2u + 1u;
        const u32 filterSamples = kernelWidth * kernelWidth;
        return filterSamples + (filter.pcssStrength > 0.0001f ? 9u : 0u);
    }

    const u32 filterSamples = std::clamp<u32>(filter.pcssFilterSampleCount, 1u, 16u);
    const u32 blockerSamples = filter.pcssStrength > 0.0001f
        ? std::clamp<u32>(filter.pcssBlockerSampleCount, 0u, 16u)
        : 0u;
    return filterSamples + blockerSamples;
}

u32 LocalShadowAtlasTileSizeFor(const VulkanShadowSettings& settings) {
    if (settings.quality == VulkanShadowQuality::Low) {
        return 512u;
    }
    if (settings.quality == VulkanShadowQuality::Off) {
        return 512u;
    }

    return std::max<u32>(std::min<u32>(settings.mapSize / 2u, 1024u), 512u);
}

u32 LocalShadowAtlasTileCapacityFor(const VulkanShadowSettings& settings) {
    switch (settings.quality) {
    case VulkanShadowQuality::Off:
        return 8u;
    case VulkanShadowQuality::Low:
        return 12u;
    case VulkanShadowQuality::Medium:
        return 24u;
    case VulkanShadowQuality::High:
    case VulkanShadowQuality::Ultra:
        return 32u;
    }

    return 24u;
}

u32 RectShadowMaxSampleTileCountFor(const VulkanShadowSettings& settings) {
    const u32 requested = std::clamp<u32>(
        settings.rectLightShadowSampleTiles,
        kRectAreaShadowBaseSampleTileCount,
        kRectAreaShadowMaxSampleTileCount
    );
    return requested >= kRectAreaShadowMaxSampleTileCount
        ? kRectAreaShadowMaxSampleTileCount
        : kRectAreaShadowBaseSampleTileCount;
}

u32 ExpectedLocalShadowTileCountFor(RendererLightKind kind) {
    switch (kind) {
    case RendererLightKind::Point:
        return 6u;
    case RendererLightKind::Spot:
        return 1u;
    case RendererLightKind::Rect:
        return kRectAreaShadowBaseSampleTileCount;
    case RendererLightKind::Directional:
        break;
    }

    return 0u;
}

bool LocalShadowKindEnabled(
    const VulkanShadowSettings& settings,
    RendererLightKind kind
) {
    switch (kind) {
    case RendererLightKind::Point:
        return settings.pointLightShadowEnabled;
    case RendererLightKind::Spot:
        return settings.spotLightShadowEnabled;
    case RendererLightKind::Rect:
        return settings.rectLightShadowEnabled;
    case RendererLightKind::Directional:
        break;
    }

    return false;
}

bool LocalLightSelectedForShadowGeneration(
    const VulkanShadowSettings& settings,
    u32 localLightIndex
) {
    return settings.debugLocalShadowLightIndex < 0 ||
        settings.debugLocalShadowLightIndex == static_cast<i32>(localLightIndex);
}

f32 RectShadowSampleImportance(const RendererLocalLight& light) {
    const f32 area = std::max(light.width * light.height, 0.001f);
    const f32 radius = std::clamp(light.radius, 0.1f, 10.0f);
    return std::max(light.intensity, 0.0f) * std::sqrt(area) * radius;
}

struct RectShadowSampleCandidate {
    u32 localLightIndex = 0;
    f32 importance = 0.0f;
};

struct RectShadowSampleBudget {
    std::array<u32, kRendererMaxFrameLocalLights> sampleCounts{};
    u32 baseSampleTiles = kRectAreaShadowBaseSampleTileCount;
    u32 maxSampleTiles = kRectAreaShadowBaseSampleTileCount;
    u32 baseReservedTiles = 0;
    u32 extraRequestedTiles = 0;
    u32 extraGrantedTiles = 0;
    u32 budgetLimitedExtraTiles = 0;
};

RectShadowSampleBudget PlanRectShadowSampleBudget(
    const FrameLightSet& lights,
    const VulkanShadowSettings& settings,
    u32 tileCapacity
) {
    RectShadowSampleBudget budget{};
    budget.maxSampleTiles = RectShadowMaxSampleTileCountFor(settings);

    u32 baseReservedTiles = 0;
    std::vector<RectShadowSampleCandidate> extraCandidates;
    const u32 localCount = std::min<u32>(
        lights.localCount,
        static_cast<u32>(lights.localLights.size())
    );
    for (u32 index = 0; index < localCount; ++index) {
        if (!LocalLightSelectedForShadowGeneration(settings, index)) {
            continue;
        }

        const RendererLocalLight& light = lights.localLights[index];
        if (light.kind == RendererLightKind::Point) {
            if (settings.pointLightShadowEnabled) {
                baseReservedTiles += 6u;
            }
            continue;
        }
        if (light.kind == RendererLightKind::Spot) {
            if (settings.spotLightShadowEnabled) {
                ++baseReservedTiles;
            }
            continue;
        }
        if (light.kind != RendererLightKind::Rect ||
            !settings.rectLightShadowEnabled) {
            continue;
        }

        budget.sampleCounts[index] = kRectAreaShadowBaseSampleTileCount;
        baseReservedTiles += kRectAreaShadowBaseSampleTileCount;
        if (budget.maxSampleTiles > kRectAreaShadowBaseSampleTileCount) {
            const u32 extraTiles =
                budget.maxSampleTiles - kRectAreaShadowBaseSampleTileCount;
            budget.extraRequestedTiles += extraTiles;
            extraCandidates.push_back(RectShadowSampleCandidate{
                index,
                RectShadowSampleImportance(light)
            });
        }
    }

    budget.baseReservedTiles = baseReservedTiles;
    if (budget.maxSampleTiles <= kRectAreaShadowBaseSampleTileCount ||
        extraCandidates.empty()) {
        budget.budgetLimitedExtraTiles = budget.extraRequestedTiles;
        return budget;
    }

    u32 remainingTiles =
        tileCapacity > baseReservedTiles ? tileCapacity - baseReservedTiles : 0u;
    std::sort(
        extraCandidates.begin(),
        extraCandidates.end(),
        [](const RectShadowSampleCandidate& lhs, const RectShadowSampleCandidate& rhs) {
            if (lhs.importance == rhs.importance) {
                return lhs.localLightIndex < rhs.localLightIndex;
            }
            return lhs.importance > rhs.importance;
        }
    );

    const u32 extraTilesPerLight =
        budget.maxSampleTiles - kRectAreaShadowBaseSampleTileCount;
    for (const RectShadowSampleCandidate& candidate : extraCandidates) {
        if (remainingTiles < extraTilesPerLight) {
            budget.budgetLimitedExtraTiles += extraTilesPerLight;
            continue;
        }

        budget.sampleCounts[candidate.localLightIndex] += extraTilesPerLight;
        remainingTiles -= extraTilesPerLight;
        budget.extraGrantedTiles += extraTilesPerLight;
    }

    return budget;
}

LocalShadowTileBudget LocalShadowTileBudgetFor(const FrameLightSet& lights) {
    LocalShadowTileBudget budget{};
    const u32 localCount = std::min<u32>(
        lights.localCount,
        static_cast<u32>(lights.localLights.size())
    );

    for (u32 index = 0; index < localCount; ++index) {
        const RendererLocalLight& light = lights.localLights[index];
        if (light.kind == RendererLightKind::Point) {
            ++budget.shadowableLightCount;
            ++budget.pointLightCount;
            budget.pointFaceTiles += 6u;
            budget.requestedTiles += 6u;
            continue;
        }

        if (light.kind == RendererLightKind::Spot) {
            ++budget.shadowableLightCount;
            ++budget.spotLightCount;
            ++budget.spotTiles;
            ++budget.requestedTiles;
            continue;
        }

        if (light.kind == RendererLightKind::Rect) {
            ++budget.shadowableLightCount;
            ++budget.rectLightCount;
            budget.rectTiles += kRectAreaShadowBaseSampleTileCount;
            budget.requestedTiles += kRectAreaShadowBaseSampleTileCount;
        }
    }

    return budget;
}

glm::mat4 PerspectiveProjection(f32 verticalFovRadians, f32 aspectRatio, f32 nearPlane, f32 farPlane) {
    glm::mat4 projection = glm::perspective(
        verticalFovRadians,
        aspectRatio,
        nearPlane,
        std::max(farPlane, nearPlane + 0.1f)
    );
    projection[1][1] *= -1.0f;
    return projection;
}

glm::mat4 LocalShadowViewProjection(
    glm::vec3 position,
    glm::vec3 direction,
    glm::vec3 up,
    f32 verticalFovRadians,
    f32 farPlane
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    }
    direction = glm::normalize(direction);
    if (glm::dot(up, up) <= 0.0001f ||
        std::abs(glm::dot(glm::normalize(up), direction)) > 0.98f) {
        up = std::abs(direction.y) > 0.95f
            ? glm::vec3{ 0.0f, 0.0f, 1.0f }
            : glm::vec3{ 0.0f, 1.0f, 0.0f };
    }
    up = glm::normalize(up);

    return PerspectiveProjection(
        verticalFovRadians,
        1.0f,
        kLocalShadowNearPlane,
        farPlane
    ) * glm::lookAt(position, position + direction, up);
}

glm::mat4 LocalRectShadowViewProjection(
    glm::vec3 position,
    glm::vec3 direction,
    f32 width,
    f32 height,
    f32 farPlane
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    }
    direction = glm::normalize(direction);

    glm::vec3 up = std::abs(direction.y) > 0.95f
        ? glm::vec3{ 0.0f, 0.0f, 1.0f }
        : glm::vec3{ 0.0f, 1.0f, 0.0f };
    const glm::mat4 view = glm::lookAt(position, position + direction, up);

    const f32 halfWidth = std::max(width * 0.5f, 0.25f);
    const f32 halfHeight = std::max(height * 0.5f, 0.25f);
    const f32 influencePadding =
        std::max(glm::length(glm::vec2(halfWidth, halfHeight)), 0.35f);
    const f32 extentX = std::max(halfWidth + influencePadding, 0.75f);
    const f32 extentY = std::max(halfHeight + influencePadding, 0.75f);

    glm::mat4 projection = glm::ortho(
        -extentX,
        extentX,
        -extentY,
        extentY,
        kLocalShadowNearPlane,
        std::max(farPlane, kLocalShadowNearPlane + 0.1f)
    );
    projection[1][1] *= -1.0f;
    return projection * view;
}

glm::mat4 LocalRectAreaShadowSampleViewProjection(
    glm::vec3 position,
    glm::vec3 direction,
    f32 width,
    f32 height,
    u32 sampleIndex,
    u32 sampleCount,
    f32 farPlane
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    }
    direction = glm::normalize(direction);

    glm::vec3 seedUp = std::abs(direction.y) > 0.95f
        ? glm::vec3{ 0.0f, 0.0f, 1.0f }
        : glm::vec3{ 0.0f, 1.0f, 0.0f };
    glm::vec3 tangent = glm::normalize(glm::cross(seedUp, direction));
    glm::vec3 bitangent = glm::normalize(glm::cross(direction, tangent));

    const bool surfacePattern =
        sampleCount >= kRectAreaShadowMaxSampleTileCount;
    glm::vec3 samplePosition = position;
    glm::vec3 up = bitangent;
    if (surfacePattern) {
        static constexpr std::array<glm::vec2, 4> kSurfaceSampleSigns{
            glm::vec2{ -1.0f, -1.0f },
            glm::vec2{ 1.0f, -1.0f },
            glm::vec2{ -1.0f, 1.0f },
            glm::vec2{ 1.0f, 1.0f }
        };
        const glm::vec2 signs =
            kSurfaceSampleSigns[std::min<u32>(sampleIndex, 3u)];
        const f32 halfWidth = std::max(width * 0.5f, 0.001f);
        const f32 halfHeight = std::max(height * 0.5f, 0.001f);
        samplePosition =
            position +
            tangent * halfWidth * kRectAreaShadowSampleOffset * signs.x +
            bitangent * halfHeight * kRectAreaShadowSampleOffset * signs.y;
    } else {
        const bool sampleAlongWidth = width >= height;
        const glm::vec3 longAxis = sampleAlongWidth ? tangent : bitangent;
        const glm::vec3 shortAxis = sampleAlongWidth ? bitangent : tangent;
        const f32 axisLength = sampleAlongWidth ? width : height;
        const f32 halfLength = std::max(axisLength * 0.5f, 0.001f);
        const f32 sampleSign = (sampleIndex % 2u) == 0u ? -1.0f : 1.0f;
        samplePosition =
            position +
            longAxis * halfLength * kRectAreaShadowSampleOffset * sampleSign;
        up = shortAxis;
    }

    return LocalShadowViewProjection(
        samplePosition,
        direction,
        up,
        glm::radians(128.0f),
        farPlane
    );
}

f32 RectShadowResidualSourceRadius(f32 width, f32 height, u32 sampleCount) {
    const f32 diagonal = glm::length(glm::vec2(
        std::max(width, 0.001f),
        std::max(height, 0.001f)
    ));
    return 0.25f * diagonal /
        std::sqrt(static_cast<f32>(std::max(sampleCount, 1u)));
}

void AddLocalShadowTile(
    LocalShadowTileSet& tileSet,
    const glm::mat4& viewProjection,
    u32 localLightIndex,
    u32 faceIndex,
    RendererLightKind lightKind,
    glm::vec4 filterGeometry,
    u64 cacheTileIdentity,
    u64 cacheLightSignature,
    u64 cacheCasterSignature,
    LocalShadowCacheDecision cacheDecision
) {
    ++tileSet.requestedCount;
    if (localLightIndex < tileSet.requestedTilesByLocalLight.size()) {
        ++tileSet.requestedTilesByLocalLight[localLightIndex];
    }
    if (tileSet.assignedCount >= tileSet.tileCapacity ||
        tileSet.assignedCount >= tileSet.tiles.size()) {
        ++tileSet.droppedCount;
        return;
    }

    if (localLightIndex < tileSet.assignedTilesByLocalLight.size() &&
        tileSet.assignedTilesByLocalLight[localLightIndex] == 0u) {
        tileSet.firstAssignedTileByLocalLight[localLightIndex] =
            tileSet.assignedCount;
    }

    LocalShadowTile& tile = tileSet.tiles[tileSet.assignedCount];
    tile.viewProjection = viewProjection;
    tile.tileIndex = tileSet.assignedCount;
    tile.localLightIndex = localLightIndex;
    tile.faceIndex = faceIndex;
    tile.lightKind = static_cast<u32>(lightKind);
    tile.filterGeometry = filterGeometry;
    tile.cacheTileIdentity = cacheTileIdentity;
    tile.cacheLightSignature = cacheLightSignature;
    tile.cacheCasterSignature = cacheCasterSignature;
    tile.cacheKey = HashCombine(
        HashCombine(cacheTileIdentity, cacheLightSignature),
        cacheCasterSignature
    );
    tile.cacheDecision = cacheDecision;
    tile.cacheReusable = cacheDecision == LocalShadowCacheDecision::Hit;
    if (tile.cacheReusable) {
        ++tileSet.cacheEligibleTiles;
        ++tileSet.cacheHitTiles;
    } else {
        ++tileSet.cacheMissTiles;
        switch (cacheDecision) {
        case LocalShadowCacheDecision::Cold:
            ++tileSet.cacheColdTiles;
            break;
        case LocalShadowCacheDecision::TileLayoutChanged:
            ++tileSet.cacheTileLayoutChangedTiles;
            break;
        case LocalShadowCacheDecision::LightChanged:
            ++tileSet.cacheLightChangedTiles;
            break;
        case LocalShadowCacheDecision::CasterChanged:
            ++tileSet.cacheCasterChangedTiles;
            break;
        case LocalShadowCacheDecision::DynamicSkinnedCaster:
            ++tileSet.cacheDynamicSkinnedCasterTiles;
            break;
        case LocalShadowCacheDecision::Hit:
            break;
        }
    }
#if !defined(NDEBUG)
    const char* decisionName = "cold";
    switch (cacheDecision) {
    case LocalShadowCacheDecision::Hit:
        decisionName = "hit";
        break;
    case LocalShadowCacheDecision::TileLayoutChanged:
        decisionName = "layout";
        break;
    case LocalShadowCacheDecision::LightChanged:
        decisionName = "light";
        break;
    case LocalShadowCacheDecision::CasterChanged:
        decisionName = "caster";
        break;
    case LocalShadowCacheDecision::DynamicSkinnedCaster:
        decisionName = "skinned";
        break;
    case LocalShadowCacheDecision::Cold:
        break;
    }
    if (!tileSet.cacheReasonSummary.empty()) {
        tileSet.cacheReasonSummary += ';';
    }
    tileSet.cacheReasonSummary += "t" + std::to_string(tile.tileIndex);
    tileSet.cacheReasonSummary += ":l" + std::to_string(tile.localLightIndex);
    tileSet.cacheReasonSummary += ":f" + std::to_string(tile.faceIndex);
    tileSet.cacheReasonSummary += '=';
    tileSet.cacheReasonSummary += decisionName;
#endif
    ++tileSet.assignedCount;
    if (localLightIndex < tileSet.assignedTilesByLocalLight.size()) {
        ++tileSet.assignedTilesByLocalLight[localLightIndex];
    }
}

u64 LocalShadowTileIdentitySignature(
    u32 localLightIndex,
    u32 faceIndex,
    RendererLightKind lightKind
) {
    u64 hash = 0x6f4d1f5bb9e6d9c5ull;
    hash = HashCombine(hash, static_cast<u64>(localLightIndex));
    hash = HashCombine(hash, static_cast<u64>(lightKind));
    hash = HashCombine(hash, static_cast<u64>(faceIndex));
    return hash;
}

u64 LocalShadowLightCacheSignature(const RendererLocalLight& light) {
    u64 hash = 0x92f7a28d631d5c7bull;
    hash = HashCombine(hash, FloatBits(light.position.x));
    hash = HashCombine(hash, FloatBits(light.position.y));
    hash = HashCombine(hash, FloatBits(light.position.z));
    hash = HashCombine(hash, FloatBits(light.radius));
    hash = HashCombine(hash, FloatBits(light.direction.x));
    hash = HashCombine(hash, FloatBits(light.direction.y));
    hash = HashCombine(hash, FloatBits(light.direction.z));
    hash = HashCombine(hash, FloatBits(light.innerConeCos));
    hash = HashCombine(hash, FloatBits(light.outerConeCos));
    hash = HashCombine(hash, FloatBits(light.width));
    hash = HashCombine(hash, FloatBits(light.height));
    hash = HashCombine(hash, FloatBits(light.sourceRadius));
    return hash;
}

LocalShadowCacheDecision DetermineLocalShadowCacheDecision(
    const LocalShadowCacheState* cacheState,
    u32 tileIndex,
    u64 tileIdentity,
    u64 lightSignature,
    u64 casterSignature,
    bool hasDynamicSkinnedCaster
) {
    if (hasDynamicSkinnedCaster) {
        return LocalShadowCacheDecision::DynamicSkinnedCaster;
    }
    if (cacheState == nullptr ||
        !cacheState->valid ||
        tileIndex >= cacheState->tileCount ||
        tileIndex >= cacheState->tiles.size()) {
        return LocalShadowCacheDecision::Cold;
    }

    const LocalShadowCacheEntry& previous = cacheState->tiles[tileIndex];
    if (previous.tileIdentity != tileIdentity) {
        return LocalShadowCacheDecision::TileLayoutChanged;
    }
    if (previous.lightSignature != lightSignature) {
        return LocalShadowCacheDecision::LightChanged;
    }
    if (previous.casterSignature != casterSignature) {
        return LocalShadowCacheDecision::CasterChanged;
    }
    return LocalShadowCacheDecision::Hit;
}

bool SphereIntersectsAabb(
    glm::vec3 center,
    f32 radius,
    const RenderBounds& bounds
) {
    if (!bounds.valid) {
        return true;
    }

    const glm::vec3 closest = glm::clamp(center, bounds.min, bounds.max);
    const glm::vec3 delta = closest - center;
    return glm::dot(delta, delta) <= radius * radius;
}

glm::vec3 BoundsCenter(const RenderBounds& bounds) {
    return (bounds.min + bounds.max) * 0.5f;
}

f32 BoundsRadius(const RenderBounds& bounds) {
    if (!bounds.valid) {
        return std::numeric_limits<f32>::max();
    }

    return glm::length(bounds.max - bounds.min) * 0.5f;
}

bool PointLightFaceMaySeeBounds(
    const RendererLocalLight& light,
    glm::vec3 faceDirection,
    const RenderBounds& bounds
) {
    if (!bounds.valid) {
        return true;
    }
    if (!SphereIntersectsAabb(light.position, std::max(light.radius, kLocalShadowNearPlane), bounds)) {
        return false;
    }

    const glm::vec3 toBounds = BoundsCenter(bounds) - light.position;
    return glm::dot(toBounds, faceDirection) + BoundsRadius(bounds) >= 0.0f;
}

u64 HashShadowCommand(u64 signature, const RenderCommand& command) {
    signature = HashCombine(
        signature,
        static_cast<u64>(reinterpret_cast<std::uintptr_t>(command.mesh))
    );
    signature = HashCombine(
        signature,
        static_cast<u64>(reinterpret_cast<std::uintptr_t>(command.material))
    );
    signature = HashCombine(signature, static_cast<u64>(command.meshSortKey));
    signature = HashCombine(signature, static_cast<u64>(command.materialSortKey));
    signature = HashCombine(signature, static_cast<u64>(command.drawOrder));
    signature = HashCombine(signature, command.castShadow ? 1ull : 0ull);
    signature = HashMatrix(signature, command.model);
    if (command.worldBounds.valid) {
        signature = HashCombine(signature, 1ull);
        signature = HashCombine(signature, FloatBits(command.worldBounds.min.x));
        signature = HashCombine(signature, FloatBits(command.worldBounds.min.y));
        signature = HashCombine(signature, FloatBits(command.worldBounds.min.z));
        signature = HashCombine(signature, FloatBits(command.worldBounds.max.x));
        signature = HashCombine(signature, FloatBits(command.worldBounds.max.y));
        signature = HashCombine(signature, FloatBits(command.worldBounds.max.z));
    } else {
        signature = HashCombine(signature, 0ull);
    }

    return signature;
}

struct LocalShadowCasterSignatureResult {
    u64 signature = 0;
    bool hasDynamicSkinnedCaster = false;
};

bool IsDynamicSkinnedShadowCaster(const RenderCommand& command) {
    return command.bonePaletteReady != 0u &&
        command.bonePaletteDescriptorSetReady != 0u &&
        command.bonePaletteDescriptorSet != VK_NULL_HANDLE &&
        command.bonePaletteCurrentEntryCount > 0u &&
        command.bonePalettePreviousEntryCount > 0u &&
        command.bonePaletteChangedEntryCount > 0u;
}

LocalShadowCasterSignatureResult LocalShadowCasterSignature(
    std::span<const RenderCommand> shadowCommands,
    const RendererLocalLight& light,
    const glm::vec3* pointFaceDirection = nullptr
) {
    LocalShadowCasterSignatureResult result{};
    result.signature = 0x35f0d5a8936a1c21ull;
    u32 relevantCount = 0;
    const f32 influenceRadius = std::max(light.radius, kLocalShadowNearPlane);
    for (const RenderCommand& command : shadowCommands) {
        if (!command.castShadow) {
            continue;
        }
        const bool relevant = pointFaceDirection != nullptr
            ? PointLightFaceMaySeeBounds(light, *pointFaceDirection, command.worldBounds)
            : SphereIntersectsAabb(light.position, influenceRadius, command.worldBounds);
        if (!relevant) {
            continue;
        }

        result.signature = HashShadowCommand(result.signature, command);
        result.hasDynamicSkinnedCaster =
            result.hasDynamicSkinnedCaster || IsDynamicSkinnedShadowCaster(command);
        ++relevantCount;
    }

    result.signature = HashCombine(result.signature, static_cast<u64>(relevantCount));
    return result;
}

bool ShadowCommandIntersectsClipVolume(
    const RenderCommand& command,
    const glm::mat4& viewProjection,
    f32 clipMargin
) {
    if (!command.castShadow) {
        return false;
    }
    if (!command.worldBounds.valid) {
        return true;
    }

    bool outsideLeft = true;
    bool outsideRight = true;
    bool outsideBottom = true;
    bool outsideTop = true;
    bool outsideNear = true;
    bool outsideFar = true;

    for (const glm::vec3& worldPoint : command.worldBounds.corners) {
        const glm::vec4 clip = viewProjection * glm::vec4(worldPoint, 1.0f);
        if (!std::isfinite(clip.x) ||
            !std::isfinite(clip.y) ||
            !std::isfinite(clip.z) ||
            !std::isfinite(clip.w) ||
            clip.w <= 0.000001f) {
            return true;
        }

        const f32 margin = std::abs(clip.w) * clipMargin;
        outsideLeft = outsideLeft && clip.x < -clip.w - margin;
        outsideRight = outsideRight && clip.x > clip.w + margin;
        outsideBottom = outsideBottom && clip.y < -clip.w - margin;
        outsideTop = outsideTop && clip.y > clip.w + margin;
        outsideNear = outsideNear && clip.z < -margin;
        outsideFar = outsideFar && clip.z > clip.w + margin;
    }

    return !(outsideLeft ||
        outsideRight ||
        outsideBottom ||
        outsideTop ||
        outsideNear ||
        outsideFar);
}

std::vector<RenderCommand> FilterShadowCommandsForClipVolume(
    std::span<const RenderCommand> shadowCommands,
    const glm::mat4& viewProjection,
    f32 clipMargin
) {
    std::vector<RenderCommand> filtered;
    filtered.reserve(shadowCommands.size());
    for (const RenderCommand& command : shadowCommands) {
        if (ShadowCommandIntersectsClipVolume(command, viewProjection, clipMargin)) {
            filtered.push_back(command);
        }
    }

    return filtered;
}

std::vector<std::vector<RenderCommand>> BuildDirectionalShadowCommandLists(
    std::span<const RenderCommand> shadowCommands,
    const DirectionalShadowCascadeSet& cascades,
    bool useFullCasterList
) {
    constexpr f32 kCascadeClipMargin = 0.035f;
    const u32 cascadeCount = std::min<u32>(
        cascades.activeCount,
        static_cast<u32>(kMaxDirectionalShadowCascades)
    );
    std::vector<std::vector<RenderCommand>> commandLists;
    commandLists.reserve(cascadeCount);
    for (u32 cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex) {
        if (useFullCasterList) {
            commandLists.emplace_back(shadowCommands.begin(), shadowCommands.end());
            continue;
        }

        commandLists.push_back(FilterShadowCommandsForClipVolume(
            shadowCommands,
            cascades.cascades[cascadeIndex].viewProjection,
            kCascadeClipMargin
        ));
    }

    return commandLists;
}

bool LocalShadowCommandMayAffectTile(
    const RenderCommand& command,
    const RendererLocalLight& light,
    const LocalShadowTile& tile
) {
    if (!command.castShadow) {
        return false;
    }
    if (!command.worldBounds.valid) {
        return true;
    }

    static constexpr std::array<glm::vec3, 6> kPointFaceDirections{
        glm::vec3{ 1.0f, 0.0f, 0.0f },
        glm::vec3{ -1.0f, 0.0f, 0.0f },
        glm::vec3{ 0.0f, 1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, 0.0f, 1.0f },
        glm::vec3{ 0.0f, 0.0f, -1.0f }
    };

    if (static_cast<RendererLightKind>(tile.lightKind) == RendererLightKind::Point) {
        if (tile.faceIndex >= kPointFaceDirections.size()) {
            return true;
        }

        return PointLightFaceMaySeeBounds(
            light,
            kPointFaceDirections[tile.faceIndex],
            command.worldBounds
        );
    }

    return SphereIntersectsAabb(
        light.position,
        std::max(light.radius, kLocalShadowNearPlane),
        command.worldBounds
    );
}

std::vector<std::vector<RenderCommand>> BuildLocalShadowTileCommandLists(
    std::span<const RenderCommand> shadowCommands,
    const FrameLightSet& lights,
    const LocalShadowTileSet& localShadowTiles
) {
    constexpr f32 kLocalShadowClipMargin = 0.04f;
    const u32 tileCount = std::min<u32>(
        localShadowTiles.assignedCount,
        static_cast<u32>(localShadowTiles.tiles.size())
    );
    const u32 localLightCount = std::min<u32>(
        lights.localCount,
        static_cast<u32>(lights.localLights.size())
    );

    std::vector<std::vector<RenderCommand>> commandLists;
    commandLists.reserve(tileCount);
    for (u32 tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
        const LocalShadowTile& tile = localShadowTiles.tiles[tileIndex];
        std::vector<RenderCommand> filtered;
        filtered.reserve(shadowCommands.size());
        if (tile.localLightIndex < localLightCount) {
            const RendererLocalLight& light = lights.localLights[tile.localLightIndex];
            for (const RenderCommand& command : shadowCommands) {
                if (!LocalShadowCommandMayAffectTile(command, light, tile)) {
                    continue;
                }
                if (!ShadowCommandIntersectsClipVolume(
                        command,
                        tile.viewProjection,
                        kLocalShadowClipMargin
                    )) {
                    continue;
                }

                filtered.push_back(command);
            }
        }

        commandLists.push_back(std::move(filtered));
    }

    return commandLists;
}

std::string ShortDebugCasterName(std::string_view name) {
    constexpr std::string_view kShowcasePrefix = "Showcase ";
    if (name.starts_with(kShowcasePrefix)) {
        name.remove_prefix(kShowcasePrefix.size());
    }

    constexpr std::size_t kMaxNameLength = 42;
    if (name.size() <= kMaxNameLength) {
        return std::string(name);
    }

    std::string shortened(name.substr(0, kMaxNameLength - 3));
    shortened += "...";
    return shortened;
}

std::string ClassifyDebugCasterName(std::string_view name) {
    const std::string lower = LowerAscii(std::string(name));
    if (lower.find("floor") != std::string::npos ||
        lower.find("ground") != std::string::npos) {
        return "floor";
    }
    if (lower.find("wall") != std::string::npos ||
        lower.find("ceiling") != std::string::npos) {
        return "room";
    }
    if (lower.find("sphere") != std::string::npos ||
        lower.find("ball") != std::string::npos) {
        return "sphere";
    }
    if (lower.find("cube") != std::string::npos ||
        lower.find("block") != std::string::npos) {
        return "block";
    }
    if (lower.find("pedestal") != std::string::npos ||
        lower.find("base") != std::string::npos) {
        return "base";
    }
    if (lower.find("light") != std::string::npos ||
        lower.find("diffuser") != std::string::npos ||
        lower.find("frame") != std::string::npos) {
        return "fixture";
    }

    return "other";
}

u64 DebugCasterIdentityFor(const RenderCommand& command) {
#if !defined(NDEBUG)
    if (command.debugRenderableIdentity != 0u) {
        return command.debugRenderableIdentity;
    }
#endif

    u64 identity = 0xb30f76518d8a723dull;
    identity = HashCombine(identity, static_cast<u64>(command.meshSortKey));
    identity = HashCombine(identity, static_cast<u64>(command.materialSortKey));
    identity = HashCombine(identity, static_cast<u64>(command.submissionIndex));
    return identity;
}

std::string DebugCasterNameFor(const RenderCommand& command) {
#if !defined(NDEBUG)
    if (!command.debugRenderableName.empty()) {
        return ShortDebugCasterName(command.debugRenderableName);
    }
#endif

    return "caster#" + std::to_string(command.submissionIndex);
}

void AccumulateLocalShadowCasterAttribution(
    RendererLocalShadowAtlasStats& stats,
    u32 tileOrdinal,
    std::span<const RenderCommand> commands,
    std::unordered_set<u64>& uniqueCasters,
    std::vector<std::string>& casterNames
) {
    if (!stats.attributionTileCandidateDraws.empty()) {
        stats.attributionTileCandidateDraws += ';';
    }
    stats.attributionTileCandidateDraws += std::to_string(tileOrdinal);
    stats.attributionTileCandidateDraws += ':';
    stats.attributionTileCandidateDraws += std::to_string(commands.size());

    stats.attributionCandidateDraws += static_cast<u32>(commands.size());
    stats.attributionCasterSignature = HashCombine(
        stats.attributionCasterSignature,
        static_cast<u64>(tileOrdinal)
    );
    stats.attributionCasterSignature = HashCombine(
        stats.attributionCasterSignature,
        static_cast<u64>(commands.size())
    );

    for (const RenderCommand& command : commands) {
        const u64 identity = DebugCasterIdentityFor(command);
        stats.attributionCasterSignature = HashCombine(
            stats.attributionCasterSignature,
            identity
        );
        if (!uniqueCasters.insert(identity).second) {
            continue;
        }

        if (casterNames.size() < 16u) {
            std::string name = DebugCasterNameFor(command);
            name += '[';
            name += ClassifyDebugCasterName(name);
            name += ']';
            casterNames.push_back(std::move(name));
        }
    }
}

void WriteLocalShadowAttributionStats(
    RendererLocalShadowAtlasStats& stats,
    const FrameLightSet& lights,
    const LocalShadowTileSet& localShadowTiles,
    const std::vector<std::vector<RenderCommand>>& localShadowTileCommandLists,
    const VulkanShadowSettings& shadowSettings,
    const VulkanRenderDebugSettings& debugSettings
) {
    const i32 selectedIndex = debugSettings.localShadowDebugLightIndex >= 0
        ? debugSettings.localShadowDebugLightIndex
        : shadowSettings.debugLocalShadowLightIndex;
    stats.attributionLightIndex = selectedIndex;

    const u32 localCount = std::min<u32>(
        lights.localCount,
        static_cast<u32>(lights.localLights.size())
    );
    if (selectedIndex < 0 || selectedIndex >= static_cast<i32>(localCount)) {
        return;
    }

    const u32 selectedLocalIndex = static_cast<u32>(selectedIndex);
    const RendererLocalLight& light = lights.localLights[selectedLocalIndex];
    const RendererLightKind kind = light.kind;
    const bool shadowEnabled = LocalShadowKindEnabled(shadowSettings, kind);
    const bool matchesGenerationFilter =
        shadowSettings.debugLocalShadowLightIndex < 0 ||
        shadowSettings.debugLocalShadowLightIndex == selectedIndex;
    const u32 requestedTiles =
        selectedLocalIndex < localShadowTiles.requestedTilesByLocalLight.size()
            ? localShadowTiles.requestedTilesByLocalLight[selectedLocalIndex]
            : 0u;
    const u32 expectedTiles = requestedTiles;
    const bool skipAllCachedLocalShadowTiles =
        localShadowTiles.cacheSkippedTiles == localShadowTiles.assignedCount &&
        localShadowTiles.assignedCount > 0;
    const bool reuseCachedLocalShadowTiles =
        localShadowTiles.cacheSkippedTiles > 0 &&
        localShadowTiles.cacheSkippedTiles < localShadowTiles.assignedCount;

    stats.attributionLightValid = 1u;
    stats.attributionLightKind = static_cast<u32>(kind);
    stats.attributionExpectedTiles = expectedTiles;
    stats.attributionRequestedTiles = requestedTiles;
    stats.attributionShadowEnabled = shadowEnabled ? 1u : 0u;
    stats.attributionMatchesGenerationFilter = matchesGenerationFilter ? 1u : 0u;
    stats.attributionCasterSignature = 0x93f458ad7c2b8d51ull;

    std::unordered_set<u64> uniqueCasters;
    std::vector<std::string> casterNames;
    u32 selectedTileOrdinal = 0;
    const u32 assignedCount = std::min<u32>(
        localShadowTiles.assignedCount,
        static_cast<u32>(localShadowTiles.tiles.size())
    );
    for (u32 tileIndex = 0; tileIndex < assignedCount; ++tileIndex) {
        const LocalShadowTile& tile = localShadowTiles.tiles[tileIndex];
        if (tile.localLightIndex != selectedLocalIndex) {
            continue;
        }

        if (tileIndex < localShadowTileCommandLists.size()) {
            AccumulateLocalShadowCasterAttribution(
                stats,
                selectedTileOrdinal,
                std::span<const RenderCommand>(
                    localShadowTileCommandLists[tileIndex].data(),
                    localShadowTileCommandLists[tileIndex].size()
                ),
                uniqueCasters,
                casterNames
            );
        }
        ++selectedTileOrdinal;

        ++stats.attributionAssignedTiles;
        if (tile.cacheReusable) {
            ++stats.attributionCacheHitTiles;
        } else {
            ++stats.attributionCacheMissTiles;
        }

        const bool tileRecorded =
            !skipAllCachedLocalShadowTiles &&
            !(reuseCachedLocalShadowTiles && tile.cacheReusable);
        if (!tileRecorded) {
            continue;
        }

        ++stats.attributionRecordedTilePasses;
        if (tileIndex < localShadowTileCommandLists.size()) {
            stats.attributionRecordedDraws +=
                static_cast<u32>(localShadowTileCommandLists[tileIndex].size());
        }
    }

    stats.attributionDroppedTiles =
        requestedTiles > stats.attributionAssignedTiles
            ? requestedTiles - stats.attributionAssignedTiles
            : 0u;
    stats.attributionUniqueCasters = static_cast<u32>(uniqueCasters.size());
    for (std::size_t index = 0; index < casterNames.size(); ++index) {
        if (index > 0u) {
            stats.attributionCasterSummary += '|';
        }
        stats.attributionCasterSummary += casterNames[index];
    }
    if (stats.attributionUniqueCasters > casterNames.size()) {
        if (!stats.attributionCasterSummary.empty()) {
            stats.attributionCasterSummary += '|';
        }
        stats.attributionCasterSummary += '+';
        stats.attributionCasterSummary += std::to_string(
            stats.attributionUniqueCasters - static_cast<u32>(casterNames.size())
        );
        stats.attributionCasterSummary += " more";
    }
}

std::string ReadEnvironmentString(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
        return {};
    }

    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

bool GpuTimestampsEnabledFromEnvironment() {
    const std::string value = ReadEnvironmentString("SE_ENABLE_GPU_TIMESTAMPS");
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

struct RenderFeatureFrameGraphAppendData {
    const VulkanRenderFeatureRegistry* registry = nullptr;
    const VulkanRenderFeatureContext* renderer = nullptr;
    const RendererStats* stats = nullptr;
};

void AppendRenderFeaturesToCurrentFrameGraph(
    RenderFrameGraphPlan& plan,
    RenderFramePassKind stage,
    const void* userData
) {
    const auto* data =
        static_cast<const RenderFeatureFrameGraphAppendData*>(userData);
    if (data == nullptr ||
        data->registry == nullptr ||
        data->renderer == nullptr ||
        data->stats == nullptr) {
        return;
    }

    data->registry->AppendFrameGraph(
        VulkanRenderFeatureFrameGraphContext{
            plan,
            *data->renderer,
            *data->stats,
            stage == RenderFramePassKind::PostProcess
                ? VulkanRenderFeatureFrameGraphStage::PostProcess
                : VulkanRenderFeatureFrameGraphStage::Lighting
        }
    );
}

bool EnvironmentFlagEnabled(const char* name) {
    const std::string value = ReadEnvironmentString(name);
    return value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES";
}

bool ShutdownTraceEnabled() {
    return EnvironmentFlagEnabled("SE_SHUTDOWN_TRACE") ||
        EnvironmentFlagEnabled("SE_RENDERER_SHUTDOWN_TRACE");
}

f64 ElapsedShutdownMilliseconds(
    std::chrono::steady_clock::time_point startTime
) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime
    ).count();
}

f32 Halton(u32 index, u32 base) {
    f32 result = 0.0f;
    f32 fraction = 1.0f / static_cast<f32>(base);
    while (index > 0u) {
        result += fraction * static_cast<f32>(index % base);
        index /= base;
        fraction /= static_cast<f32>(base);
    }
    return result;
}

bool TemporalJitterEnabledFromEnvironment() {
    return EnvironmentFlagEnabled("SE_TEMPORAL_JITTER") ||
        EnvironmentFlagEnabled("SE_CAMERA_JITTER");
}

bool TemporalHistoryForceResetFromEnvironment() {
    return EnvironmentFlagEnabled("SE_TEMPORAL_HISTORY_RESET") ||
        EnvironmentFlagEnabled("SE_TEMPORAL_FORCE_HISTORY_RESET");
}

bool TemporalJitterApplicationEnabledFromEnvironment() {
    return EnvironmentFlagEnabled("SE_TAA_APPLY_JITTER") ||
        EnvironmentFlagEnabled("SE_TEMPORAL_APPLY_JITTER") ||
        EnvironmentFlagEnabled("SE_CAMERA_JITTER_APPLY");
}

std::string LowercaseAscii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))
        );
    }
    return value;
}

std::optional<bool> EnvironmentFlagOverride(const char* name);

bool DlssMInputDefaultsDisabled() {
    return EnvironmentFlagEnabled("SE_DLSS_DISABLE_M_PRESET_INPUT_DEFAULTS") ||
        EnvironmentFlagEnabled("SE_DLSS_DISABLE_M_PRESET_MV_JITTERED_DEFAULT");
}

bool DlssRequestedForMInputDefaults() {
    std::string value = ReadEnvironmentString("SE_UPSCALER_PLUGIN");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_TEMPORAL_UPSCALER_PLUGIN");
    }
    value = LowercaseAscii(value);
    return value == "dlss" ||
        value == "nvidia-dlss" ||
        value == "nvidia_dlss" ||
        value == "ngx";
}

TemporalUpscalerDlssPreset DlssPresetForMInputDefaults() {
    std::string value = ReadEnvironmentString("SE_DLSS_PRESET");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_DLSS_RENDER_PRESET");
    }
    if (value.empty()) {
        value = ReadEnvironmentString("SE_DLSS_PRESET_OVERRIDE");
    }
    return TemporalUpscalerDlssPresetFromName(value);
}

TemporalUpscalerDlssQualityMode DlssQualityForMInputDefaults() {
    std::string value = ReadEnvironmentString("SE_DLSS_QUALITY");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_DLSS_MODE");
    }
    return TemporalUpscalerDlssQualityModeFromName(value);
}

bool DlssEffectivePresetMForInputDefaults() {
    if (!DlssRequestedForMInputDefaults() || DlssMInputDefaultsDisabled()) {
        return false;
    }

    const TemporalUpscalerDlssPreset preset = DlssPresetForMInputDefaults();
    if (preset == TemporalUpscalerDlssPreset::M) {
        return true;
    }
    if (preset == TemporalUpscalerDlssPreset::K ||
        preset == TemporalUpscalerDlssPreset::L) {
        return false;
    }

    const TemporalUpscalerDlssQualityMode quality =
        DlssQualityForMInputDefaults();
    return quality == TemporalUpscalerDlssQualityMode::Dlaa ||
        quality == TemporalUpscalerDlssQualityMode::Performance;
}

bool TemporalVelocityJitteredHistoryPolicyFromEnvironment() {
    const std::string policy =
        LowercaseAscii(ReadEnvironmentString("SE_TEMPORAL_VELOCITY_JITTER_POLICY"));
    if (!policy.empty()) {
        return policy == "jittered" ||
            policy == "mvjittered" ||
            policy == "mv-jittered" ||
            policy == "dlss-jittered";
    }

    std::optional<bool> overrideValue =
        EnvironmentFlagOverride("SE_TEMPORAL_VELOCITY_JITTERED_HISTORY");
    if (!overrideValue.has_value()) {
        overrideValue = EnvironmentFlagOverride("SE_VELOCITY_JITTERED_HISTORY");
    }
    if (overrideValue.has_value()) {
        return *overrideValue;
    }

    return DlssEffectivePresetMForInputDefaults();
}

void ApplyProjectionJitter(glm::mat4& projection, const glm::vec2& jitterUv) {
    projection[2][0] += jitterUv.x * 2.0f;
    projection[2][1] += jitterUv.y * 2.0f;
}

bool TaaResolveEnabledFromEnvironment() {
    return EnvironmentFlagEnabled("SE_TAA") ||
        EnvironmentFlagEnabled("SE_TAA_RESOLVE");
}

std::optional<bool> EnvironmentFlagOverride(const char* name) {
    const std::string value = ReadEnvironmentString(name);
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "1" ||
        value == "true" ||
        value == "TRUE" ||
        value == "on" ||
        value == "ON" ||
        value == "yes" ||
        value == "YES") {
        return true;
    }
    if (value == "0" ||
        value == "false" ||
        value == "FALSE" ||
        value == "off" ||
        value == "OFF" ||
        value == "no" ||
        value == "NO") {
        return false;
    }

    return std::nullopt;
}

VulkanIblQuality GlobalIblQualityFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_GLOBAL_IBL_QUALITY");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_IBL_QUALITY");
    }
    value = LowercaseAscii(value);
    if (value == "low" || value == "0") {
        return VulkanIblQuality::Low;
    }
    if (value == "high" || value == "2") {
        return VulkanIblQuality::High;
    }
    if (value == "ultra" || value == "3") {
        return VulkanIblQuality::Ultra;
    }

    return VulkanIblQuality::Medium;
}

VulkanIblSource GlobalIblSourceFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_GLOBAL_IBL_SOURCE");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_IBL_SOURCE");
    }
    value = LowercaseAscii(value);
    if (value == "skybox" || value == "visible_skybox" ||
        value == "visible-skybox" || value == "1") {
        return VulkanIblSource::VisibleSkybox;
    }
    if (value == "equirect" || value == "equirectangular" ||
        value == "authored_equirectangular" ||
        value == "authored-equirectangular" || value == "2") {
        return VulkanIblSource::AuthoredEquirectangular;
    }
    if (value == "cubemap" || value == "authored_cubemap" ||
        value == "authored-cubemap" || value == "3") {
        return VulkanIblSource::AuthoredCubemap;
    }

    return VulkanIblSource::Procedural;
}

VulkanIblCachePolicy GlobalIblCachePolicyFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_GLOBAL_IBL_CACHE_POLICY");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_GLOBAL_IBL_CACHE");
    }
    if (value.empty()) {
        value = ReadEnvironmentString("SE_IBL_CACHE_POLICY");
    }
    if (value.empty()) {
        value = ReadEnvironmentString("SE_IBL_CACHE");
    }
    value = LowercaseAscii(value);
    if (value == "offline" || value == "prefer_offline" ||
        value == "prefer-offline" || value == "cache" || value == "1") {
        return VulkanIblCachePolicy::PreferOffline;
    }

    return VulkanIblCachePolicy::RuntimeGenerated;
}

std::string GlobalIblSourceAssetPathFromEnvironment(VulkanIblSource source) {
    std::string value = ReadEnvironmentString("SE_GLOBAL_IBL_ASSET");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_GLOBAL_IBL_SOURCE_ASSET");
    }
    if (value.empty()) {
        value = ReadEnvironmentString("SE_GLOBAL_IBL_SOURCE_PATH");
    }
    if (value.empty()) {
        value = ReadEnvironmentString("SE_IBL_ASSET");
    }
    if (!value.empty()) {
        return value;
    }

    if (source == VulkanIblSource::AuthoredEquirectangular ||
        source == VulkanIblSource::VisibleSkybox) {
        return (std::filesystem::path(SE_ASSET_DIR) /
            "skybox" /
            "bk.jpg").string();
    }

    return {};
}

VulkanIblGenerationSettings GlobalIblGenerationSettingsFromEnvironment() {
    VulkanIblGenerationSettings settings{};
    settings.quality = GlobalIblQualityFromEnvironment();
    settings.source = GlobalIblSourceFromEnvironment();
    settings.cachePolicy = GlobalIblCachePolicyFromEnvironment();
    settings.sourceAssetPath = GlobalIblSourceAssetPathFromEnvironment(
        settings.source
    );
    return settings;
}

std::optional<RendererReflectionProbeCaptureSource>
ReflectionProbeCaptureSourceOverrideFromEnvironment() {
    const std::string value =
        ReadEnvironmentString("SE_REFLECTION_PROBE_CAPTURE_SOURCE");
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "none" || value == "None" || value == "NONE" ||
        value == "off" || value == "OFF" || value == "0") {
        return RendererReflectionProbeCaptureSource::None;
    }
    if (value == "builtin" || value == "built_in" ||
        value == "procedural" || value == "BuiltInProcedural" ||
        value == "1") {
        return RendererReflectionProbeCaptureSource::BuiltInProcedural;
    }
    if (value == "authored" || value == "authored_cubemap" ||
        value == "AuthoredCubemap" || value == "2") {
        return RendererReflectionProbeCaptureSource::AuthoredCubemap;
    }
    if (value == "captured" || value == "captured_scene" ||
        value == "CapturedScene" || value == "3") {
        return RendererReflectionProbeCaptureSource::CapturedScene;
    }

    return std::nullopt;
}

enum class ReflectionProbeCaptureBackendPreference {
    Gpu,
    AnalyticCpu
};

ReflectionProbeCaptureBackendPreference
ReflectionProbeCaptureBackendPreferenceFromEnvironment() {
    const std::string value = LowerAscii(
        ReadEnvironmentString("SE_REFLECTION_PROBE_CAPTURE_BACKEND")
    );
    if (value == "analytic" || value == "cpu" || value == "analytic_cpu") {
        return ReflectionProbeCaptureBackendPreference::AnalyticCpu;
    }
    return ReflectionProbeCaptureBackendPreference::Gpu;
}

RendererReflectionProbeRefreshPolicy DefaultReflectionProbeRefreshPolicy(
    RendererReflectionProbeCaptureSource source
) {
    switch (source) {
    case RendererReflectionProbeCaptureSource::AuthoredCubemap:
        return RendererReflectionProbeRefreshPolicy::FileSignature;
    case RendererReflectionProbeCaptureSource::CapturedScene:
        return RendererReflectionProbeRefreshPolicy::SceneDirty;
    case RendererReflectionProbeCaptureSource::None:
    case RendererReflectionProbeCaptureSource::BuiltInProcedural:
        return RendererReflectionProbeRefreshPolicy::Static;
    }

    return RendererReflectionProbeRefreshPolicy::Static;
}

std::optional<RendererReflectionProbeRefreshPolicy>
ReflectionProbeRefreshPolicyOverrideFromEnvironment() {
    const std::string value =
        ReadEnvironmentString("SE_REFLECTION_PROBE_REFRESH_POLICY");
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "static" || value == "Static" || value == "STATIC" ||
        value == "0") {
        return RendererReflectionProbeRefreshPolicy::Static;
    }
    if (value == "file-signature" || value == "file_signature" ||
        value == "file" || value == "FileSignature" || value == "1") {
        return RendererReflectionProbeRefreshPolicy::FileSignature;
    }
    if (value == "forced" || value == "force" ||
        value == "Forced" || value == "2") {
        return RendererReflectionProbeRefreshPolicy::Forced;
    }
    if (value == "scene-dirty" || value == "scene_dirty" ||
        value == "dirty" || value == "SceneDirty" || value == "3") {
        return RendererReflectionProbeRefreshPolicy::SceneDirty;
    }

    return std::nullopt;
}

AuthoredReflectionProbeFilterQuality
ReflectionProbeFilterQualityFromEnvironment() {
    const std::string value =
        ReadEnvironmentString("SE_REFLECTION_PROBE_FILTER_QUALITY");
    if (value == "low" || value == "Low" || value == "LOW" || value == "0") {
        return AuthoredReflectionProbeFilterQuality::Low;
    }
    if (value == "high" || value == "High" || value == "HIGH" ||
        value == "2") {
        return AuthoredReflectionProbeFilterQuality::High;
    }
    if (value == "ultra" || value == "Ultra" || value == "ULTRA" ||
        value == "3") {
        return AuthoredReflectionProbeFilterQuality::Ultra;
    }

    return AuthoredReflectionProbeFilterQuality::Medium;
}

AuthoredReflectionProbeFilteringSettings
ReflectionProbeFilteringSettingsFromEnvironment() {
    AuthoredReflectionProbeFilteringSettings settings{};
    settings.quality = ReflectionProbeFilterQualityFromEnvironment();
    if (const std::optional<bool> seamAware =
            EnvironmentFlagOverride("SE_REFLECTION_PROBE_SEAM_AWARE_FILTER")) {
        settings.seamAwareFiltering = *seamAware;
    }
    return settings;
}

CapturedReflectionProbeFilteringSettings
CapturedReflectionProbeFilteringSettingsFromEnvironment() {
    const std::string value = LowerAscii(
        ReadEnvironmentString("SE_REFLECTION_CAPTURE_FILTER_QUALITY")
    );
    CapturedReflectionProbeFilteringSettings settings{};
    if (value == "off" || value == "0" || value == "fallback") {
        settings.quality = CapturedReflectionProbeFilterQuality::Off;
    } else if (value == "low" || value == "1") {
        settings.quality = CapturedReflectionProbeFilterQuality::Low;
    } else if (value == "high" || value == "3") {
        settings.quality = CapturedReflectionProbeFilterQuality::High;
    } else if (value == "ultra" || value == "4") {
        settings.quality = CapturedReflectionProbeFilterQuality::Ultra;
    } else if (value == "medium" || value == "2" || value.empty()) {
        settings.quality = CapturedReflectionProbeFilterQuality::Medium;
    }
    return settings;
}

std::optional<f32> EnvironmentFloatOverride(const char* name) {
    const std::string value = ReadEnvironmentString(name);
    if (value.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    const f32 parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str()) {
        return std::nullopt;
    }

    return parsed;
}

f32 TemporalRenderScaleFromEnvironment() {
    std::optional<f32> overrideValue =
        EnvironmentFloatOverride("SE_RENDER_SCALE");
    if (!overrideValue) {
        overrideValue = EnvironmentFloatOverride("SE_TEMPORAL_RENDER_SCALE");
    }
    if (!overrideValue) {
        overrideValue = EnvironmentFloatOverride("SE_INTERNAL_RENDER_SCALE");
    }
    return glm::clamp(overrideValue.value_or(1.0f), 0.5f, 1.0f);
}

bool TemporalRenderScaleApplyEnabledFromEnvironment() {
    return EnvironmentFlagEnabled("SE_RENDER_SCALE_APPLY") ||
        EnvironmentFlagEnabled("SE_INTERNAL_RENDER_SCALE_APPLY");
}

VkExtent2D TemporalRequestedInternalExtent(
    const VkExtent2D& displayExtent,
    f32 renderScale
) {
    if (displayExtent.width == 0u || displayExtent.height == 0u) {
        return {};
    }

    return VkExtent2D{
        std::max(
            1u,
            static_cast<u32>(std::round(
                static_cast<f32>(displayExtent.width) * renderScale
            ))
        ),
        std::max(
            1u,
            static_cast<u32>(std::round(
                static_cast<f32>(displayExtent.height) * renderScale
            ))
        )
    };
}

VkExtent2D TemporalActiveInternalExtentForDisplay(
    const VkExtent2D& displayExtent,
    f32 renderScale,
    bool applyRenderScale
) {
    if (!applyRenderScale ||
        renderScale >= 0.999f ||
        displayExtent.width == 0u ||
        displayExtent.height == 0u) {
        return displayExtent;
    }

    return TemporalRequestedInternalExtent(displayExtent, renderScale);
}

f32 TemporalRenderScaleForExtents(
    const VkExtent2D& displayExtent,
    const VkExtent2D& internalExtent
) {
    if (displayExtent.width == 0u ||
        displayExtent.height == 0u ||
        internalExtent.width == 0u ||
        internalExtent.height == 0u) {
        return 1.0f;
    }

    const f32 scaleX =
        static_cast<f32>(internalExtent.width) /
        static_cast<f32>(displayExtent.width);
    const f32 scaleY =
        static_cast<f32>(internalExtent.height) /
        static_cast<f32>(displayExtent.height);
    return glm::clamp(std::min(scaleX, scaleY), 0.5f, 1.0f);
}

bool ExtentsDiffer(const VkExtent2D& left, const VkExtent2D& right) {
    return left.width != right.width || left.height != right.height;
}

bool DynamicResolutionRequestedFromEnvironment() {
    return EnvironmentFlagEnabled("SE_DYNAMIC_RESOLUTION") ||
        EnvironmentFlagEnabled("SE_DYNAMIC_RESOLUTION_ENABLED");
}

bool TemporalUpscaleRequestedFromEnvironment() {
    return EnvironmentFlagEnabled("SE_TAAU") ||
        EnvironmentFlagEnabled("SE_TAA_UPSCALE") ||
        EnvironmentFlagEnabled("SE_TEMPORAL_UPSCALE");
}

bool TemporalUpscalePostSourceRequestedFromEnvironment() {
    return EnvironmentFlagEnabled("SE_TEMPORAL_UPSCALE_PRESENT") ||
        EnvironmentFlagEnabled("SE_TEMPORAL_UPSCALE_OUTPUT_PRESENT") ||
        EnvironmentFlagEnabled("SE_UPSCALER_PRESENT") ||
        EnvironmentFlagEnabled("SE_DLSS_PRESENT");
}

std::string TemporalUpscalerProviderFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_UPSCALER_PLUGIN");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_TEMPORAL_UPSCALER_PLUGIN");
    }
    return value;
}

bool UpscalerProviderRequested(const std::string& value) {
    return !value.empty() &&
        value != "0" &&
        value != "off" &&
        value != "OFF" &&
        value != "none" &&
        value != "None" &&
        value != "NONE";
}

bool TemporalUpscalerPluginRequestedFromEnvironment() {
    return UpscalerProviderRequested(TemporalUpscalerProviderFromEnvironment());
}

std::filesystem::path TemporalUpscalerSdkRootFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_NVIDIA_DLSS_SDK_DIR");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_DLSS_SDK_DIR");
    }
    return value.empty() ? std::filesystem::path{} : std::filesystem::path(value);
}

std::filesystem::path DlssReferenceBaselinePathFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_DLSS_REFERENCE_BASELINE_PATH");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_DLSS_VISUAL_BASELINE_PATH");
    }
    return std::filesystem::path(value);
}

bool DlssReferenceBaselineReadyFromEnvironment() {
    const std::filesystem::path baselinePath =
        DlssReferenceBaselinePathFromEnvironment();
    if (baselinePath.empty()) {
        return false;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(baselinePath, error) || error) {
        return false;
    }

    const auto size = std::filesystem::file_size(baselinePath, error);
    return !error && size > 0u;
}

TemporalUpscalerDlssQualityMode TemporalUpscalerDlssQualityModeFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_DLSS_QUALITY");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_DLSS_MODE");
    }
    return TemporalUpscalerDlssQualityModeFromName(value);
}

TemporalUpscalerDlssPreset TemporalUpscalerDlssPresetFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_DLSS_PRESET");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_DLSS_RENDER_PRESET");
    }
    if (value.empty()) {
        value = ReadEnvironmentString("SE_DLSS_PRESET_OVERRIDE");
    }
    return TemporalUpscalerDlssPresetFromName(value);
}

std::optional<f32> DlssSharpnessOverrideFromEnvironment() {
    std::optional<f32> overrideValue =
        EnvironmentFloatOverride("SE_DLSS_SHARPNESS");
    if (!overrideValue.has_value()) {
        overrideValue =
            EnvironmentFloatOverride("SE_TEMPORAL_UPSCALER_SHARPNESS");
    }
    if (!overrideValue.has_value()) {
        return std::nullopt;
    }
    return glm::clamp(*overrideValue, 0.0f, 1.0f);
}

std::optional<f32> MaterialTextureMipLodBiasOverrideFromEnvironment() {
    std::optional<f32> overrideValue =
        EnvironmentFloatOverride("SE_TEXTURE_MIP_LOD_BIAS");
    if (!overrideValue.has_value()) {
        overrideValue =
            EnvironmentFloatOverride("SE_MATERIAL_TEXTURE_MIP_BIAS");
    }
    if (!overrideValue.has_value()) {
        overrideValue = EnvironmentFloatOverride("SE_TEXTURE_MIP_BIAS");
    }
    if (!overrideValue.has_value()) {
        return std::nullopt;
    }

    return glm::clamp(*overrideValue, -2.0f, 2.0f);
}

f32 MaterialTextureMipLodBiasFromEnvironment() {
    return MaterialTextureMipLodBiasOverrideFromEnvironment().value_or(0.0f);
}

f32 MaterialTextureMipLodBiasForTemporalMode(
    RendererTemporalAntialiasingMode mode
) {
    if (const std::optional<f32> overrideValue =
            MaterialTextureMipLodBiasOverrideFromEnvironment()) {
        return *overrideValue;
    }

    switch (mode) {
    case RendererTemporalAntialiasingMode::DlssSrQuality:
        return kDlssSrQualityMipLodBias;
    case RendererTemporalAntialiasingMode::DlssSrBalanced:
        return kDlssSrBalancedMipLodBias;
    case RendererTemporalAntialiasingMode::DlssSrPerformance:
        return kDlssSrPerformanceMipLodBias;
    case RendererTemporalAntialiasingMode::Environment:
    case RendererTemporalAntialiasingMode::NativeTaa:
    case RendererTemporalAntialiasingMode::DlssDlaa:
    case RendererTemporalAntialiasingMode::Off:
    default:
        return 0.0f;
    }
}

f32 TaaHistoryWeightFromEnvironment() {
    const std::optional<f32> overrideValue =
        EnvironmentFloatOverride("SE_TAA_HISTORY_WEIGHT");
    return glm::clamp(overrideValue.value_or(0.88f), 0.0f, 0.95f);
}

f32 NativeTaaJitterScaleFromEnvironment() {
    std::optional<f32> overrideValue =
        EnvironmentFloatOverride("SE_NATIVE_TAA_JITTER_SCALE");
    if (!overrideValue.has_value()) {
        overrideValue = EnvironmentFloatOverride("SE_TAA_JITTER_SCALE");
    }
    return glm::clamp(overrideValue.value_or(0.5f), 0.0f, 1.0f);
}

bool TaaRejectionEnabledFromEnvironment() {
    if (const std::optional<bool> overrideValue =
            EnvironmentFlagOverride("SE_TAA_REJECTION")) {
        return *overrideValue;
    }
    return true;
}

bool TaaNeighborhoodClampEnabledFromEnvironment() {
    if (const std::optional<bool> overrideValue =
            EnvironmentFlagOverride("SE_TAA_CLAMP")) {
        return *overrideValue;
    }
    return true;
}

f32 TaaVelocityRejectionThresholdFromEnvironment() {
    const std::optional<f32> overrideValue =
        EnvironmentFloatOverride("SE_TAA_VELOCITY_REJECTION_THRESHOLD");
    return glm::clamp(overrideValue.value_or(0.035f), 0.0f, 1.0f);
}

f32 TaaDepthRejectionThresholdFromEnvironment() {
    const std::optional<f32> overrideValue =
        EnvironmentFloatOverride("SE_TAA_DEPTH_REJECTION_THRESHOLD");
    return glm::clamp(overrideValue.value_or(0.02f), 0.0f, 1.0f);
}

bool WeightedTranslucencyAlphaReferenceEnabled() {
    return EnvironmentFlagEnabled("SE_WBOIT_REFERENCE_ALPHA") ||
        EnvironmentFlagEnabled("SE_WEIGHTED_TRANSLUCENCY_ALPHA_REFERENCE");
}

RendererLocalLight PointLocalLight(
    glm::vec3 position,
    f32 radius,
    glm::vec3 color,
    f32 intensity,
    f32 sourceRadius = 0.05f
) {
    RendererLocalLight light{};
    light.kind = RendererLightKind::Point;
    light.position = position;
    light.radius = radius;
    light.color = color;
    light.intensity = intensity;
    light.sourceRadius = std::max(sourceRadius, 0.0f);
    return light;
}

RendererLocalLight SpotLocalLight(
    glm::vec3 position,
    glm::vec3 direction,
    f32 radius,
    glm::vec3 color,
    f32 intensity,
    f32 innerConeDegrees,
    f32 outerConeDegrees,
    f32 sourceRadius = 0.05f
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    } else {
        direction = glm::normalize(direction);
    }

    outerConeDegrees = std::clamp(outerConeDegrees, 0.1f, 89.0f);
    innerConeDegrees = std::clamp(innerConeDegrees, 0.05f, outerConeDegrees);

    RendererLocalLight light{};
    light.kind = RendererLightKind::Spot;
    light.position = position;
    light.radius = radius;
    light.color = color;
    light.intensity = intensity;
    light.direction = direction;
    light.innerConeCos = std::cos(glm::radians(innerConeDegrees));
    light.outerConeCos = std::cos(glm::radians(outerConeDegrees));
    light.sourceRadius = std::max(sourceRadius, 0.0f);
    return light;
}

RendererLocalLight RectLocalLight(
    glm::vec3 position,
    glm::vec3 direction,
    f32 width,
    f32 height,
    f32 radius,
    glm::vec3 color,
    f32 intensity
) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, -1.0f, 0.0f };
    } else {
        direction = glm::normalize(direction);
    }

    RendererLocalLight light{};
    light.kind = RendererLightKind::Rect;
    light.position = position;
    light.radius = radius;
    light.color = color;
    light.intensity = intensity;
    light.direction = direction;
    light.width = std::max(width, 0.0f);
    light.height = std::max(height, 0.0f);
    light.sourceRadius = 0.0f;
    return light;
}

RendererReflectionProbe ClampReflectionProbe(RendererReflectionProbe probe) {
    probe.radius = std::clamp(probe.radius, 0.01f, 256.0f);
    probe.boxExtents = glm::max(probe.boxExtents, glm::vec3(0.01f));
    probe.color = glm::max(probe.color, glm::vec3(0.0f));
    probe.intensity = std::clamp(probe.intensity, 0.0f, 4.0f);
    probe.blendStrength = std::clamp(probe.blendStrength, 0.0f, 1.0f);
    probe.falloff = std::clamp(probe.falloff, 0.25f, 8.0f);
    return probe;
}

bool ReflectionProbeBoxProjectionEnabled(const RendererReflectionProbe& probe) {
    return probe.sceneOwned &&
        probe.boxExtents.x > 0.01f &&
        probe.boxExtents.y > 0.01f &&
        probe.boxExtents.z > 0.01f;
}

struct ReflectionProbeBoxProjectionRayResult {
    glm::vec3 direction{ 0.0f, 1.0f, 0.0f };
    f32 hitDistance = 0.0f;
    bool hit = false;
    bool directionChanged = false;
};

ReflectionProbeBoxProjectionRayResult ReflectionProbeBoxProjectDirection(
    const RendererReflectionProbe& probe,
    glm::vec3 direction,
    glm::vec3 worldPosition
) {
    ReflectionProbeBoxProjectionRayResult result{};
    if (glm::dot(direction, direction) <= 0.0001f) {
        direction = { 0.0f, 1.0f, 0.0f };
    } else {
        direction = glm::normalize(direction);
    }
    result.direction = direction;
    if (!ReflectionProbeBoxProjectionEnabled(probe)) {
        return result;
    }

    const glm::vec3 extents = glm::max(probe.boxExtents, glm::vec3(0.001f));
    const glm::vec3 localPosition = worldPosition - probe.center;
    glm::vec3 safeDirection = direction;
    for (i32 axis = 0; axis < 3; ++axis) {
        if (std::abs(safeDirection[axis]) < 0.0001f) {
            safeDirection[axis] = safeDirection[axis] < 0.0f
                ? -0.0001f
                : 0.0001f;
        }
    }
    const glm::vec3 tMin = (-extents - localPosition) / safeDirection;
    const glm::vec3 tMax = (extents - localPosition) / safeDirection;
    const glm::vec3 tFar = glm::max(tMin, tMax);
    const f32 hitDistance = std::min(tFar.x, std::min(tFar.y, tFar.z));
    if (!std::isfinite(hitDistance) || hitDistance <= 0.0001f ||
        hitDistance > 100000.0f) {
        return result;
    }

    const glm::vec3 hitLocal = localPosition + direction * hitDistance;
    if (glm::dot(hitLocal, hitLocal) <= 0.0001f) {
        return result;
    }
    result.direction = glm::normalize(hitLocal);
    result.hitDistance = hitDistance;
    result.hit = true;
    result.directionChanged = glm::dot(result.direction, direction) < 0.9999f;
    return result;
}

f32 ReflectionProbeBoxWeight(
    const RendererReflectionProbe& probe,
    glm::vec3 position
) {
    if (!ReflectionProbeBoxProjectionEnabled(probe)) {
        return 1.0f;
    }

    const glm::vec3 extents = glm::max(probe.boxExtents, glm::vec3(0.01f));
    const glm::vec3 normalized =
        glm::abs(position - probe.center) / extents;
    const f32 maxAxis =
        std::max(normalized.x, std::max(normalized.y, normalized.z));
    if (maxAxis <= 1.0f) {
        return 1.0f;
    }

    return 1.0f / (1.0f + (maxAxis - 1.0f) * 4.0f);
}

f32 SmoothStep(f32 edge0, f32 edge1, f32 value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    const f32 t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

f32 ReflectionProbeInfluenceWeight(
    const RendererReflectionProbe& probe,
    glm::vec3 position
) {
    if (!probe.enabled ||
        probe.radius <= 0.001f ||
        probe.intensity <= 0.0001f ||
        probe.blendStrength <= 0.0001f) {
        return 0.0f;
    }

    const f32 radius = std::max(probe.radius, 0.001f);
    const f32 normalizedDistance = glm::length(position - probe.center) / radius;
    const f32 falloff = std::clamp(probe.falloff, 0.25f, 8.0f);
    f32 influence =
        std::pow(std::clamp(1.0f - normalizedDistance, 0.0f, 1.0f), falloff);
    if (ReflectionProbeBoxProjectionEnabled(probe)) {
        const glm::vec3 extents = glm::max(probe.boxExtents, glm::vec3(0.001f));
        const glm::vec3 normalizedBox =
            glm::abs(position - probe.center) / extents;
        const f32 maxAxis =
            std::max(normalizedBox.x, std::max(normalizedBox.y, normalizedBox.z));
        influence *= 1.0f - SmoothStep(1.0f, 1.25f, maxAxis);
    }

    return influence * std::clamp(probe.blendStrength, 0.0f, 1.0f);
}

f32 ReflectionProbeShapeCoordinate(
    const RendererReflectionProbe& probe,
    glm::vec3 position
) {
    if (ReflectionProbeBoxProjectionEnabled(probe)) {
        const glm::vec3 extents = glm::max(probe.boxExtents, glm::vec3(0.001f));
        const glm::vec3 normalized = glm::abs(position - probe.center) / extents;
        return std::max(normalized.x, std::max(normalized.y, normalized.z));
    }

    return glm::length(position - probe.center) /
        std::max(probe.radius, 0.001f);
}

f32 ReflectionProbeProductionCoverage(
    const RendererReflectionProbe& probe,
    glm::vec3 position
) {
    if (!probe.enabled ||
        probe.radius <= 0.001f ||
        probe.intensity <= 0.0001f ||
        probe.blendStrength <= 0.0001f) {
        return 0.0f;
    }

    const f32 edgeWidth = std::lerp(
        0.08f,
        0.45f,
        std::clamp(probe.blendStrength, 0.0f, 1.0f)
    );
    const f32 edgeStart = std::clamp(1.0f - edgeWidth, 0.05f, 0.95f);
    return 1.0f - SmoothStep(
        edgeStart,
        1.0f,
        ReflectionProbeShapeCoordinate(probe, position)
    );
}

f32 ReflectionProbeVolumePriority(const RendererReflectionProbe& probe) {
    if (ReflectionProbeBoxProjectionEnabled(probe)) {
        const glm::vec3 extents = glm::max(probe.boxExtents, glm::vec3(0.01f));
        return 1.0f / std::max(extents.x * extents.y * extents.z, 0.000001f);
    }

    const f32 radius = std::max(probe.radius, 0.01f);
    return 1.0f / (radius * radius * radius);
}

struct ReflectionReceiverAuditRequest {
    bool requested = false;
    glm::vec3 position{ 0.0f };
    glm::vec3 direction{ 0.0f, 0.0f, 1.0f };
    f32 roughness = 0.24f;
};

ReflectionReceiverAuditRequest ReflectionReceiverAuditFromEnvironment() {
    ReflectionReceiverAuditRequest request{};
    request.requested = EnvironmentFlagEnabled("SE_REFLECTION_RECEIVER_AUDIT");
    if (!request.requested) {
        return request;
    }

    request.position = {
        EnvironmentFloatOrDefault("SE_REFLECTION_RECEIVER_AUDIT_X", 0.0f),
        EnvironmentFloatOrDefault("SE_REFLECTION_RECEIVER_AUDIT_Y", 0.0f),
        EnvironmentFloatOrDefault("SE_REFLECTION_RECEIVER_AUDIT_Z", 0.0f)
    };
    request.direction = {
        EnvironmentFloatOrDefault("SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_X", 0.0f),
        EnvironmentFloatOrDefault("SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_Y", 0.0f),
        EnvironmentFloatOrDefault("SE_REFLECTION_RECEIVER_AUDIT_DIRECTION_Z", 1.0f)
    };
    if (glm::dot(request.direction, request.direction) <= 0.0001f) {
        request.direction = { 0.0f, 0.0f, 1.0f };
    } else {
        request.direction = glm::normalize(request.direction);
    }
    request.roughness = std::clamp(
        EnvironmentFloatOrDefault("SE_REFLECTION_RECEIVER_AUDIT_ROUGHNESS", 0.24f),
        0.0f,
        1.0f
    );
    return request;
}

f32 ReflectionProbeSelectionScore(
    const RendererReflectionProbe& probe,
    glm::vec3 position
) {
    const f32 radius = std::max(probe.radius, 0.001f);
    const f32 distance = glm::length(position - probe.center);
    const f32 normalizedDistance = distance / radius;
    const f32 falloff = std::clamp(probe.falloff, 0.25f, 8.0f);
    const f32 sphereInfluence =
        std::pow(std::clamp(1.0f - normalizedDistance, 0.0f, 1.0f), falloff);
    const f32 proximity = 1.0f / (1.0f + normalizedDistance);
    const f32 boxWeight = ReflectionProbeBoxWeight(probe, position);
    return (sphereInfluence * 3.0f + proximity * 0.35f) *
        std::max(boxWeight, 0.05f) *
        std::max(probe.intensity, 0.0f) *
        std::max(probe.blendStrength, 0.0f);
}

RendererReflectionProbeCaptureSource RendererCaptureSource(
    ReflectionProbeCaptureSource source
) {
    switch (source) {
    case ReflectionProbeCaptureSource::None:
        return RendererReflectionProbeCaptureSource::None;
    case ReflectionProbeCaptureSource::BuiltInProcedural:
        return RendererReflectionProbeCaptureSource::BuiltInProcedural;
    case ReflectionProbeCaptureSource::AuthoredCubemap:
        return RendererReflectionProbeCaptureSource::AuthoredCubemap;
    case ReflectionProbeCaptureSource::CapturedScene:
        return RendererReflectionProbeCaptureSource::CapturedScene;
    }

    return RendererReflectionProbeCaptureSource::None;
}

RendererReflectionProbeRefreshPolicy RendererRefreshPolicy(
    ReflectionProbeRefreshPolicy policy
) {
    switch (policy) {
    case ReflectionProbeRefreshPolicy::Static:
        return RendererReflectionProbeRefreshPolicy::Static;
    case ReflectionProbeRefreshPolicy::FileSignature:
        return RendererReflectionProbeRefreshPolicy::FileSignature;
    case ReflectionProbeRefreshPolicy::Forced:
        return RendererReflectionProbeRefreshPolicy::Forced;
    case ReflectionProbeRefreshPolicy::SceneDirty:
        return RendererReflectionProbeRefreshPolicy::SceneDirty;
    }

    return RendererReflectionProbeRefreshPolicy::Static;
}

RendererReflectionProbeCaptureFallbackReason CaptureFallbackReasonFor(
    RendererReflectionProbeCaptureSource source,
    bool resourceReady
) {
    if (resourceReady) {
        return RendererReflectionProbeCaptureFallbackReason::None;
    }

    switch (source) {
    case RendererReflectionProbeCaptureSource::None:
        return RendererReflectionProbeCaptureFallbackReason::SourceDisabled;
    case RendererReflectionProbeCaptureSource::BuiltInProcedural:
        return RendererReflectionProbeCaptureFallbackReason::BuiltInResourceUnavailable;
    case RendererReflectionProbeCaptureSource::AuthoredCubemap:
        return RendererReflectionProbeCaptureFallbackReason::AuthoredCubemapNotLoaded;
    case RendererReflectionProbeCaptureSource::CapturedScene:
        return RendererReflectionProbeCaptureFallbackReason::CapturedSceneResourceUnavailable;
    }

    return RendererReflectionProbeCaptureFallbackReason::SourceDisabled;
}

u32 StableStringHash(std::string_view value) {
    u32 hash = 2166136261u;
    for (char character : value) {
        hash ^= static_cast<u8>(character);
        hash *= 16777619u;
    }
    return hash;
}

bool AuthoredReflectionProbeAssetExists(std::string_view assetId) {
    if (assetId.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::path path(assetId);
    if (path.is_relative()) {
        path = std::filesystem::current_path(error) / path;
        error.clear();
    }
    return std::filesystem::is_regular_file(path, error);
}

bool ReflectionProbeCaptureResourceReady(
    RendererReflectionProbeCaptureSource source,
    bool builtInCubemapReady,
    bool authoredCubemapReady,
    bool capturedSceneReady
) {
    if (source == RendererReflectionProbeCaptureSource::BuiltInProcedural) {
        return builtInCubemapReady;
    }
    if (source == RendererReflectionProbeCaptureSource::AuthoredCubemap) {
        return authoredCubemapReady;
    }
    if (source == RendererReflectionProbeCaptureSource::CapturedScene) {
        return capturedSceneReady;
    }
    return false;
}

u32 ReflectionProbeInfluenceBit(u64 key) {
    return 1u << static_cast<u32>(key & 31ull);
}

u32 ReflectionProbeRegionMaskForPoint(
    const RendererReflectionProbe& probe,
    const glm::vec3& point
) {
    const glm::vec3 delta = point - probe.center;
    const u32 region =
        (delta.x >= 0.0f ? 1u : 0u) |
        (delta.y >= 0.0f ? 2u : 0u) |
        (delta.z >= 0.0f ? 4u : 0u);
    return 1u << region;
}

u32 ReflectionProbeRenderableIdentityBit(const RenderCommand& command) {
#if !defined(NDEBUG)
    if (command.debugRenderableIdentity != 0u) {
        return ReflectionProbeInfluenceBit(command.debugRenderableIdentity);
    }
#endif
    u64 key = 0x9e3779b97f4a7c15ull;
    key = HashCombine(key, static_cast<u64>(command.submissionIndex + 1u));
    key = HashCombine(key, static_cast<u64>(command.meshSortKey));
    key = HashCombine(key, static_cast<u64>(command.materialSortKey));
    key = HashCombine(key, static_cast<u64>(command.drawOrder));
    return ReflectionProbeInfluenceBit(key);
}

CapturedReflectionProbeLightSample CapturedReflectionProbeLightSampleFor(
    const RendererLocalLight& light,
    const RendererReflectionProbe& probe,
    std::size_t lightIndex
) {
    CapturedReflectionProbeLightSample sample{};
    sample.position = { light.position.x, light.position.y, light.position.z };
    sample.direction = { light.direction.x, light.direction.y, light.direction.z };
    sample.color = { light.color.r, light.color.g, light.color.b };
    sample.intensity = light.intensity;
    sample.radius = light.radius;
    sample.width = light.width;
    sample.height = light.height;
    sample.kind = static_cast<u32>(light.kind);
    sample.identityMask = ReflectionProbeInfluenceBit(
        static_cast<u64>(lightIndex + 1u)
    );
    sample.regionMask = ReflectionProbeRegionMaskForPoint(
        probe,
        light.position
    );
    return sample;
}

f32 ReflectionProbeCaptureDistance(const RendererReflectionProbe& probe) {
    return std::max(
        24.0f,
        std::max(probe.radius * 2.5f, glm::length(probe.boxExtents) * 2.25f)
    );
}

bool LocalLightMayInfluenceReflectionProbe(
    const RendererLocalLight& light,
    const RendererReflectionProbe& probe
) {
    const f32 sourceRadius = std::max({
        light.radius,
        light.width * 0.5f,
        light.height * 0.5f,
        0.1f
    });
    const f32 reach = ReflectionProbeCaptureDistance(probe) + sourceRadius;
    const glm::vec3 delta = light.position - probe.center;
    return glm::dot(delta, delta) <= reach * reach;
}

std::vector<CapturedReflectionProbeLightSample> CapturedReflectionProbeLights(
    const FrameLightSet& lights,
    const RendererReflectionProbe& probe
) {
    std::vector<CapturedReflectionProbeLightSample> samples;
    samples.reserve(std::min<std::size_t>(
        lights.localCount,
        kRendererMaxFrameLocalLights
    ));
    const std::size_t count = std::min<std::size_t>(
        lights.localCount,
        kRendererMaxFrameLocalLights
    );
    for (std::size_t index = 0; index < count; ++index) {
        const RendererLocalLight& light = lights.localLights[index];
        if (light.intensity <= 0.0001f || light.radius <= 0.0001f) {
            continue;
        }
        if (!LocalLightMayInfluenceReflectionProbe(light, probe)) {
            continue;
        }
        samples.push_back(CapturedReflectionProbeLightSampleFor(
            light,
            probe,
            index
        ));
    }
    return samples;
}

u64 HashVec3(u64 seed, glm::vec3 value) {
    seed = HashCombine(seed, FloatBits(value.x));
    seed = HashCombine(seed, FloatBits(value.y));
    seed = HashCombine(seed, FloatBits(value.z));
    return seed;
}

u64 HashCapturedLightSample(
    u64 seed,
    const CapturedReflectionProbeLightSample& light
) {
    for (f32 value : light.position) {
        seed = HashCombine(seed, FloatBits(value));
    }
    for (f32 value : light.direction) {
        seed = HashCombine(seed, FloatBits(value));
    }
    for (f32 value : light.color) {
        seed = HashCombine(seed, FloatBits(value));
    }
    seed = HashCombine(seed, FloatBits(light.intensity));
    seed = HashCombine(seed, FloatBits(light.radius));
    seed = HashCombine(seed, FloatBits(light.width));
    seed = HashCombine(seed, FloatBits(light.height));
    seed = HashCombine(seed, light.kind);
    seed = HashCombine(seed, light.identityMask);
    seed = HashCombine(seed, light.regionMask);
    return seed;
}

struct CapturedReflectionProbeGeometrySample {
    u32 signature = 0;
    u32 affectedRenderableCount = 0;
    u32 affectedRenderableIdentityMask = 0;
    u32 affectedRenderableRegionMask = 0;
};

CapturedReflectionProbeGeometrySample CapturedReflectionProbeGeometrySampleFor(
    const RendererReflectionProbe& probe,
    std::span<const RenderCommand> commands
) {
    CapturedReflectionProbeGeometrySample sample{};
    u64 signature = 0x2fdb9a9f90ca7f23ull;
    const f32 captureDistance = ReflectionProbeCaptureDistance(probe);
    for (const RenderCommand& command : commands) {
        if (!SphereIntersectsAabb(
                probe.center,
                captureDistance,
                command.worldBounds
            )) {
            continue;
        }
        signature = HashShadowCommand(signature, command);
        sample.affectedRenderableIdentityMask |=
            ReflectionProbeRenderableIdentityBit(command);
        sample.affectedRenderableRegionMask |= ReflectionProbeRegionMaskForPoint(
            probe,
            CommandBoundsCenter(command)
        );
        ++sample.affectedRenderableCount;
    }
    signature = HashCombine(signature, sample.affectedRenderableIdentityMask);
    signature = HashCombine(signature, sample.affectedRenderableRegionMask);
    signature = HashCombine(signature, sample.affectedRenderableCount);
    sample.signature = static_cast<u32>(signature ^ (signature >> 32u));
    if (sample.signature == 0u) {
        sample.signature = 1u;
    }
    return sample;
}

u32 ReflectionCaptureRefreshPriority(
    const RendererReflectionProbe& probe,
    const CapturedReflectionProbeSceneSample& lightSample,
    const CapturedReflectionProbeGeometrySample& geometrySample,
    bool forceRefresh
) {
    const f32 influenceSize = std::max(
        probe.radius,
        glm::length(probe.boxExtents)
    );
    const f32 score =
        std::max(probe.intensity, 0.0f) * 1024.0f +
        influenceSize * 64.0f +
        static_cast<f32>(lightSample.affectedLocalLightCount) * 8.0f +
        static_cast<f32>(geometrySample.affectedRenderableCount);
    const u32 priority = static_cast<u32>(std::clamp(
        score,
        0.0f,
        static_cast<f32>(std::numeric_limits<u32>::max() - 1024u)
    ));
    return forceRefresh ? priority + 1024u : priority;
}

u32 ReflectionCaptureMinimumRefreshFramesFromEnvironment() {
    const std::string value = ReadEnvironmentString(
        "SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES"
    );
    if (value.empty()) {
        return 12u;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return 12u;
    }
    return std::min<u32>(static_cast<u32>(parsed), 3600u);
}

bool ReflectionCaptureSelectiveInvalidationEnabled() {
    return !EnvironmentFlagEnabled("SE_REFLECTION_CAPTURE_SELECTIVE_REFRESH_OFF");
}

CapturedReflectionProbeSceneSample CapturedReflectionProbeSceneSampleFor(
    const RendererReflectionProbe& probe,
    const FrameLightSet& lights,
    std::span<const CapturedReflectionProbeLightSample> lightSamples
) {
    CapturedReflectionProbeSceneSample sample{};
    sample.center = { probe.center.x, probe.center.y, probe.center.z };
    sample.boxExtents = {
        probe.boxExtents.x,
        probe.boxExtents.y,
        probe.boxExtents.z
    };
    sample.tint = { probe.color.r, probe.color.g, probe.color.b };
    sample.directionalDirection = {
        lights.primaryDirectional.direction.x,
        lights.primaryDirectional.direction.y,
        lights.primaryDirectional.direction.z
    };
    sample.ambientStrength = std::clamp(
        lights.primaryDirectional.ambient + 0.08f,
        0.0f,
        1.2f
    );
    sample.ambientColor = {
        sample.ambientStrength,
        sample.ambientStrength,
        sample.ambientStrength
    };
    sample.directionalIntensity = lights.primaryDirectional.intensity;
    sample.intensity = std::clamp(probe.intensity, 0.0f, 2.0f);

    u64 signature = 0xcbf29ce484222325ull;
    signature = HashVec3(signature, probe.center);
    signature = HashVec3(signature, probe.boxExtents);
    signature = HashVec3(signature, probe.color);
    signature = HashCombine(signature, FloatBits(probe.intensity));
    signature = HashVec3(signature, lights.primaryDirectional.direction);
    signature = HashCombine(signature, FloatBits(lights.primaryDirectional.intensity));
    signature = HashCombine(signature, FloatBits(lights.primaryDirectional.ambient));
    signature = HashCombine(
        signature,
        FloatBits(lights.primaryDirectional.angularRadiusRadians)
    );
    signature = HashCombine(signature, static_cast<u64>(lightSamples.size()));
    for (const CapturedReflectionProbeLightSample& light : lightSamples) {
        signature = HashCapturedLightSample(signature, light);
        sample.localLightIdentityMask |= light.identityMask;
        sample.localLightRegionMask |= light.regionMask;
    }
    signature = HashCombine(signature, sample.localLightIdentityMask);
    signature = HashCombine(signature, sample.localLightRegionMask);
    sample.localLightSignature = static_cast<u32>(
        signature ^ (signature >> 32u)
    );
    if (sample.localLightSignature == 0u) {
        sample.localLightSignature = 1u;
    }
    sample.affectedLocalLightCount = static_cast<u32>(lightSamples.size());
    signature = HashCombine(signature, sample.localLightSignature);
    sample.signature = static_cast<u32>(signature ^ (signature >> 32u));
    if (sample.signature == 0u) {
        sample.signature = 1u;
    }
    return sample;
}

CapturedSceneRefreshRequest CapturedSceneRefreshRequestFor(
    const Scene3D& scene,
    const RendererReflectionProbe& probe,
    const CapturedReflectionProbeSceneSample& sample,
    const CapturedReflectionProbeGeometrySample& geometrySample,
    RendererReflectionProbeRefreshPolicy refreshPolicy,
    CapturedReflectionProbeFilteringSettings capturedFilteringSettings,
    bool forceRefresh,
    bool sceneDirtyOverride,
    u64 schedulerFrame
) {
    CapturedSceneRefreshRequest request{};
    request.refreshPolicy = refreshPolicy;
    request.membershipRevision = scene.MembershipRevision();
    request.lightRevision = scene.LightRevision();
    request.renderRevision = scene.RenderRevision();
    request.localLightSignature = sample.localLightSignature;
    request.geometrySignature = geometrySample.signature;
    request.affectedLocalLightCount = sample.affectedLocalLightCount;
    request.affectedRenderableCount = geometrySample.affectedRenderableCount;
    request.localLightIdentityMask = sample.localLightIdentityMask;
    request.geometryIdentityMask =
        geometrySample.affectedRenderableIdentityMask;
    request.localLightRegionMask = sample.localLightRegionMask;
    request.geometryRegionMask = geometrySample.affectedRenderableRegionMask;
    request.refreshPriority = ReflectionCaptureRefreshPriority(
        probe,
        sample,
        geometrySample,
        forceRefresh
    );
    request.minimumRefreshIntervalFrames =
        ReflectionCaptureMinimumRefreshFramesFromEnvironment();
    request.schedulerFrame = schedulerFrame;
    request.forceRefresh = forceRefresh;
    request.sceneDirtyOverride = sceneDirtyOverride;
    request.selectiveInvalidationEnabled =
        ReflectionCaptureSelectiveInvalidationEnabled();

    u64 signature = 0x13d9e7f1b4a8c625ull;
    signature = HashCombine(signature, sample.signature);
    signature = HashCombine(signature, request.localLightSignature);
    signature = HashCombine(signature, request.geometrySignature);
    signature = HashCombine(
        signature,
        static_cast<u32>(capturedFilteringSettings.quality)
    );
    if (!request.selectiveInvalidationEnabled) {
        signature = HashCombine(signature, request.membershipRevision);
        signature = HashCombine(signature, request.lightRevision);
        signature = HashCombine(signature, request.renderRevision);
    }
    request.captureSignature = static_cast<u32>(signature ^ (signature >> 32u));
    if (request.captureSignature == 0u) {
        request.captureSignature = 1u;
    }
    return request;
}

void ResetFrameReflectionProbeCaptureDiagnostics(FrameReflectionProbeSet& probes) {
    probes.selectedCaptureSlots.fill(-1);
    probes.selectedCaptureResourceReady.fill(false);
    probes.selectedCaptureDescriptorBound.fill(false);
    probes.selectedCaptureFallbackReasons.fill(
        RendererReflectionProbeCaptureFallbackReason::NoActiveSceneProbe
    );
    probes.selectedRefreshPolicies.fill(
        RendererReflectionProbeRefreshPolicy::Static
    );
    probes.selectedCapturedScenePlaceholderReady.fill(false);
    probes.selectedCapturedSceneInvalidated.fill(false);
    probes.selectedCapturedSceneDiffuseIrradianceReady.fill(false);
    probes.selectedCaptureMipCounts.fill(0u);
    probes.selectedAuthoredAssetHashes.fill(0u);
    probes.selectedAuthoredAssetSpecified.fill(false);
    probes.selectedAuthoredAssetFound.fill(false);
    probes.selectedDiffuseIrradianceLobesReady.fill(false);
    probes.selectedBlendWeights.fill(0.0f);
    probes.selectedNormalizedBlendWeights.fill(0.0f);
}

void SetSelectedReflectionProbeCaptureDiagnostics(
    FrameReflectionProbeSet& probes,
    u32 selectedIndex,
    const RendererReflectionProbe& probe,
    bool cubemapSamplingEnabled,
    bool builtInCubemapReady,
    bool authoredCubemapReady,
    bool capturedSceneReady,
    bool authoredAssetFound,
    bool authoredLoadFailed,
    u32 descriptorSetsBound,
    const CapturedSceneCaptureAudit& capturedSceneAudit
) {
    if (selectedIndex >= probes.selectedCaptureSlots.size()) {
        return;
    }

    const bool resourceReady =
        ReflectionProbeCaptureResourceReady(
            probe.captureSource,
            builtInCubemapReady,
            authoredCubemapReady,
            capturedSceneReady
        );
    const bool descriptorBound =
        resourceReady && (cubemapSamplingEnabled || descriptorSetsBound > 0u);
    const bool capturedScene =
        probe.captureSource == RendererReflectionProbeCaptureSource::CapturedScene;
    const bool policyForced =
        probe.refreshPolicy == RendererReflectionProbeRefreshPolicy::Forced;
    const bool capturedSceneRefreshRequested =
        capturedScene &&
        capturedSceneAudit.refreshRequested;
    const bool authoredAssetSpecified =
        probe.captureSource == RendererReflectionProbeCaptureSource::AuthoredCubemap &&
        !probe.captureAssetId.empty();
    const RendererReflectionProbeCaptureFallbackReason fallbackReason =
        cubemapSamplingEnabled
            ? probe.captureSource == RendererReflectionProbeCaptureSource::AuthoredCubemap &&
                    authoredLoadFailed
                ? RendererReflectionProbeCaptureFallbackReason::
                    AuthoredCubemapLoadFailed
                : probe.captureSource == RendererReflectionProbeCaptureSource::AuthoredCubemap &&
                    !resourceReady &&
                    (!authoredAssetSpecified || !authoredAssetFound)
                ? RendererReflectionProbeCaptureFallbackReason::
                    AuthoredCubemapAssetMissing
                : CaptureFallbackReasonFor(probe.captureSource, resourceReady)
            : RendererReflectionProbeCaptureFallbackReason::CubemapSamplingDisabled;
    const u32 bit = 1u << selectedIndex;

    probes.selectedCaptureSlots[selectedIndex] =
        resourceReady ? static_cast<i32>(selectedIndex) : -1;
    probes.selectedCaptureResourceReady[selectedIndex] = resourceReady;
    probes.selectedCaptureDescriptorBound[selectedIndex] = descriptorBound;
    probes.selectedCaptureFallbackReasons[selectedIndex] = fallbackReason;
    probes.selectedRefreshPolicies[selectedIndex] = probe.refreshPolicy;
    probes.selectedCapturedScenePlaceholderReady[selectedIndex] =
        capturedScene && resourceReady;
    probes.selectedCapturedSceneInvalidated[selectedIndex] =
        capturedSceneRefreshRequested;
    probes.selectedAuthoredAssetHashes[selectedIndex] =
        authoredAssetSpecified ? StableStringHash(probe.captureAssetId) : 0u;
    probes.selectedAuthoredAssetSpecified[selectedIndex] = authoredAssetSpecified;
    probes.selectedAuthoredAssetFound[selectedIndex] = authoredAssetFound;
    if (authoredAssetSpecified) {
        ++probes.selectedAuthoredAssetSpecifiedCount;
        probes.selectedAuthoredAssetSpecifiedMask |= bit;
        if (authoredAssetFound) {
            ++probes.selectedAuthoredAssetFoundCount;
            probes.selectedAuthoredAssetFoundMask |= bit;
        } else {
            ++probes.selectedAuthoredAssetMissingCount;
            probes.selectedAuthoredAssetMissingMask |= bit;
        }
    }
    if (resourceReady) {
        ++probes.selectedCaptureSlotCount;
        ++probes.selectedCaptureResourceReadyCount;
        probes.selectedCaptureReadyMask |= bit;
    } else {
        ++probes.selectedCaptureFallbackCount;
        probes.selectedCaptureFallbackMask |= bit;
    }
    if (cubemapSamplingEnabled && descriptorBound) {
        ++probes.selectedCubemapSamplingCount;
        probes.selectedCubemapSamplingMask |= bit;
    }
    if (policyForced) {
        probes.forcedRefreshRequested = true;
    }
    if (capturedScene) {
        ++probes.capturedSceneRequestedCount;
        if (resourceReady) {
            ++probes.capturedScenePlaceholderAllocatedCount;
            ++probes.capturedScenePlaceholderReadyCount;
        }
        if (capturedSceneRefreshRequested) {
            ++probes.capturedSceneInvalidatedCount;
            ++probes.capturedSceneRefreshRequestedCount;
        }
    }
}

RendererReflectionProbe SceneReflectionProbe(
    const ReflectionProbe3D& source,
    i32 sceneIndex
) {
    RendererReflectionProbe probe{};
    probe.center = source.center;
    probe.radius = source.radius;
    probe.boxExtents = source.boxExtents;
    probe.color = source.color;
    probe.intensity = source.intensity;
    probe.blendStrength = source.blendStrength;
    probe.falloff = source.falloff;
    probe.enabled = source.enabled;
    probe.sceneOwned = true;
    probe.sceneIndex = sceneIndex;
    probe.captureSource = RendererCaptureSource(source.captureSource);
    probe.captureAssetId = source.captureAssetId;
    probe.refreshPolicy = RendererRefreshPolicy(source.refreshPolicy);
    return ClampReflectionProbe(probe);
}

RendererReflectionProbe SettingsReflectionProbe(
    const VulkanShadowSettings& settings
) {
    RendererReflectionProbe probe{};
    probe.center = {
        settings.localReflectionProbeCenterX,
        settings.localReflectionProbeCenterY,
        settings.localReflectionProbeCenterZ
    };
    probe.radius = settings.localReflectionProbeRadius;
    probe.boxExtents = glm::vec3(settings.localReflectionProbeRadius);
    probe.color = {
        settings.localReflectionProbeColorR,
        settings.localReflectionProbeColorG,
        settings.localReflectionProbeColorB
    };
    probe.intensity = settings.localReflectionProbeIntensity;
    probe.blendStrength = settings.localReflectionProbeBlendStrength;
    probe.falloff = settings.localReflectionProbeFalloff;
    probe.enabled = settings.localReflectionProbeEnabled;
    probe.sceneOwned = false;
    probe.captureSource = RendererReflectionProbeCaptureSource::None;
    probe.refreshPolicy = RendererReflectionProbeRefreshPolicy::Static;
    return ClampReflectionProbe(probe);
}

bool ReflectionProbeContributes(const RendererReflectionProbe& probe) {
    return probe.enabled &&
        probe.radius > 0.001f &&
        probe.intensity > 0.0001f &&
        probe.blendStrength > 0.0001f;
}

void WriteReflectionReceiverAuditStats(
    const FrameReflectionProbeSet& frameProbes,
    RendererReflectionProbeStats& stats
) {
    stats.receiverAuditRequested = 0u;
    stats.receiverAuditProductionBlend = 0u;
    stats.receiverAuditIndependentIblEnergy = 0u;
    stats.receiverAuditPositiveWeightMask = 0u;
    stats.receiverAuditReadyCubemapMask = 0u;
    stats.receiverAuditBoxProjectionHitMask = 0u;
    stats.receiverAuditDominantSlot = -1;
    stats.receiverAuditTotalWeight = 0.0f;
    stats.receiverAuditLocalCoverage = 0.0f;
    stats.receiverAuditDominantNormalizedWeight = 0.0f;
    stats.receiverAuditLocalCubemapWeight = 0.0f;
    stats.receiverAuditWeights.fill(0.0f);
    stats.receiverAuditNormalizedWeights.fill(0.0f);
    stats.receiverAuditResolvedLods.fill(0.0f);
    stats.capturedSceneNeutralTintMask = 0u;

    const u32 selectedProbeCount = std::min<u32>(
        frameProbes.selectedProbeCount,
        static_cast<u32>(kMaxFrameReflectionProbes)
    );
    for (u32 index = 0; index < selectedProbeCount; ++index) {
        if (frameProbes.selectedProbes[index].captureSource ==
                RendererReflectionProbeCaptureSource::CapturedScene &&
            frameProbes.selectedCaptureDescriptorBound[index]) {
            stats.capturedSceneNeutralTintMask |= 1u << index;
        }
    }

    const ReflectionReceiverAuditRequest request =
        ReflectionReceiverAuditFromEnvironment();
    if (!request.requested) {
        return;
    }

    stats.receiverAuditRequested = 1u;
    const bool productionBlend =
        !EnvironmentFlagEnabled("SE_REFLECTION_PROBE_LEGACY_BLEND");
    const bool independentIblEnergy =
        !EnvironmentFlagEnabled("SE_REFLECTION_PROBE_LEGACY_ENERGY_SCALE");
    stats.receiverAuditProductionBlend = productionBlend ? 1u : 0u;
    stats.receiverAuditIndependentIblEnergy = independentIblEnergy ? 1u : 0u;
    stats.receiverAuditPositionX = request.position.x;
    stats.receiverAuditPositionY = request.position.y;
    stats.receiverAuditPositionZ = request.position.z;
    stats.receiverAuditDirectionX = request.direction.x;
    stats.receiverAuditDirectionY = request.direction.y;
    stats.receiverAuditDirectionZ = request.direction.z;
    stats.receiverAuditRoughness = request.roughness;
    stats.receiverAuditLocalCubemapWeight =
        1.0f - SmoothStep(0.88f, 1.0f, request.roughness);

    std::array<f32, kMaxFrameReflectionProbes> coverages{};
    std::array<f32, kMaxFrameReflectionProbes> priorities{};
    f32 bestPriority = 0.0f;
    for (u32 index = 0; index < selectedProbeCount; ++index) {
        const RendererReflectionProbe& probe = frameProbes.selectedProbes[index];
        coverages[index] = ReflectionProbeProductionCoverage(
            probe,
            request.position
        );
        priorities[index] = ReflectionProbeVolumePriority(probe);
        if (coverages[index] > 0.0001f) {
            bestPriority = std::max(bestPriority, priorities[index]);
        }
    }

    for (u32 index = 0; index < selectedProbeCount; ++index) {
        const RendererReflectionProbe& probe = frameProbes.selectedProbes[index];
        const f32 weight = productionBlend
            ? coverages[index] * SmoothStep(
                0.35f,
                0.95f,
                priorities[index] / std::max(bestPriority, 0.000001f)
            )
            : ReflectionProbeInfluenceWeight(probe, request.position);
        stats.receiverAuditWeights[index] = weight;
        stats.receiverAuditTotalWeight += weight;
        if (weight > 0.0001f) {
            stats.receiverAuditPositiveWeightMask |= 1u << index;
        }
        if (frameProbes.selectedCaptureDescriptorBound[index] &&
            frameProbes.selectedCaptureMipCounts[index] > 0u) {
            stats.receiverAuditReadyCubemapMask |= 1u << index;
        }
        const ReflectionProbeBoxProjectionRayResult projection =
            ReflectionProbeBoxProjectDirection(
                probe,
                request.direction,
                request.position
            );
        if (projection.hit) {
            stats.receiverAuditBoxProjectionHitMask |= 1u << index;
        }
        stats.receiverAuditResolvedLods[index] = request.roughness *
            static_cast<f32>(
                frameProbes.selectedCaptureMipCounts[index] > 0u
                    ? frameProbes.selectedCaptureMipCounts[index] - 1u
                    : 0u
            );
        stats.receiverAuditLocalCoverage = productionBlend
            ? std::max(stats.receiverAuditLocalCoverage, coverages[index])
            : std::clamp(stats.receiverAuditTotalWeight, 0.0f, 1.0f);
    }

    if (stats.receiverAuditTotalWeight <= 0.0001f) {
        return;
    }
    for (u32 index = 0; index < selectedProbeCount; ++index) {
        const f32 normalized = stats.receiverAuditWeights[index] /
            stats.receiverAuditTotalWeight;
        stats.receiverAuditNormalizedWeights[index] = normalized;
        if (normalized > stats.receiverAuditDominantNormalizedWeight) {
            stats.receiverAuditDominantNormalizedWeight = normalized;
            stats.receiverAuditDominantSlot = static_cast<i32>(index);
        }
    }
}

void AddDebugLocalLights(FrameLightSet& lights) {
    if (!EnvironmentFlagEnabled("SE_DEBUG_LOCAL_LIGHTS")) {
        return;
    }

    const std::array<RendererLocalLight, 3> debugLights{
        PointLocalLight(
            glm::vec3(-2.2f, 1.1f, -1.8f),
            4.8f,
            glm::vec3(1.0f, 0.38f, 0.22f),
            4.2f
        ),
        PointLocalLight(
            glm::vec3(1.8f, 1.0f, 0.9f),
            4.5f,
            glm::vec3(0.24f, 0.48f, 1.0f),
            3.8f
        ),
        PointLocalLight(
            glm::vec3(0.1f, 1.6f, 2.8f),
            5.2f,
            glm::vec3(0.36f, 1.0f, 0.44f),
            3.2f
        )
    };

    if (lights.localCount >= lights.localLights.size()) {
        return;
    }

    const u32 availableSlots =
        static_cast<u32>(lights.localLights.size() - lights.localCount);
    const u32 copyCount = std::min<u32>(
        availableSlots,
        static_cast<u32>(debugLights.size())
    );
    for (u32 index = 0; index < copyCount; ++index) {
        lights.localLights[lights.localCount + index] = debugLights[index];
    }
    lights.localCount += copyCount;
}

void WriteFrameReflectionProbeStats(
    const FrameReflectionProbeSet& frameProbes,
    RendererReflectionProbeStats& stats
) {
    const RendererReflectionProbe& localProbe = frameProbes.localProbe;
    stats.sceneProbeCount = frameProbes.sceneProbeCount;
    stats.activeProbeCount = frameProbes.activeLocalProbeCount;
    stats.sceneEligibleProbeCount = frameProbes.eligibleSceneProbeCount;
    stats.selectedProbeCount = frameProbes.selectedProbeCount;
    stats.blendedProbeCount = frameProbes.blendedProbeCount;
    stats.selectedCaptureSlotCount = frameProbes.selectedCaptureSlotCount;
    stats.selectedCaptureResourceReadyCount =
        frameProbes.selectedCaptureResourceReadyCount;
    stats.selectedCaptureFallbackCount =
        frameProbes.selectedCaptureFallbackCount;
    stats.selectedCubemapSamplingCount =
        frameProbes.selectedCubemapSamplingCount;
    stats.selectedCaptureReadyMask = frameProbes.selectedCaptureReadyMask;
    stats.selectedCaptureFallbackMask = frameProbes.selectedCaptureFallbackMask;
    stats.selectedCubemapSamplingMask = frameProbes.selectedCubemapSamplingMask;
    stats.selectedAuthoredAssetSpecifiedCount =
        frameProbes.selectedAuthoredAssetSpecifiedCount;
    stats.selectedAuthoredAssetFoundCount =
        frameProbes.selectedAuthoredAssetFoundCount;
    stats.selectedAuthoredAssetMissingCount =
        frameProbes.selectedAuthoredAssetMissingCount;
    stats.selectedAuthoredAssetSpecifiedMask =
        frameProbes.selectedAuthoredAssetSpecifiedMask;
    stats.selectedAuthoredAssetFoundMask =
        frameProbes.selectedAuthoredAssetFoundMask;
    stats.selectedAuthoredAssetMissingMask =
        frameProbes.selectedAuthoredAssetMissingMask;
    stats.authoredCubemapDiffuseLobesApplied =
        frameProbes.selectedDiffuseIrradianceLobesReadyCount;
    stats.authoredCubemapDiffuseLobeCount =
        frameProbes.selectedDiffuseIrradianceLobeCount;
    stats.selectedDiffuseLobeReadyMask =
        frameProbes.selectedDiffuseIrradianceLobesReadyMask;
    stats.selectedCapturedSceneDiffuseIrradianceReadyMask =
        frameProbes.selectedCapturedSceneDiffuseIrradianceReadyMask;
    stats.authoredCubemapDiffuseLobeEnergy = 0.0f;
    stats.selectedProbeMask = frameProbes.selectedProbeMask;
    stats.selectedBoxProjectionMask = frameProbes.selectedBoxProjectionMask;
    stats.selectedCapturedSceneBoxProjectionMask =
        frameProbes.selectedCapturedSceneBoxProjectionMask;
    stats.selectedBoxProjectionRayHitMask =
        frameProbes.selectedBoxProjectionRayHitMask;
    stats.selectedBoxProjectionDirectionChangedMask =
        frameProbes.selectedBoxProjectionDirectionChangedMask;
    stats.selectedBoxProjectionOutsideFallbackMask =
        frameProbes.selectedBoxProjectionOutsideFallbackMask;
    stats.selectedSceneOwnedMask = frameProbes.selectedSceneOwnedMask;
    stats.selectedPositiveInfluenceMask =
        frameProbes.selectedPositiveInfluenceMask;
    stats.selectedProbeDuplicateIndexMask =
        frameProbes.selectedProbeDuplicateIndexMask;
    stats.selectedCaptureMipReadyMask =
        frameProbes.selectedCaptureMipReadyMask;
    stats.spatialContractFailureMask = frameProbes.spatialContractFailureMask;
    stats.spatialContractValid = frameProbes.spatialContractValid ? 1u : 0u;
    stats.blendWeightNormalizationFallbackCount =
        frameProbes.blendWeightNormalizationFallbackCount;
    stats.normalizedBlendWeightSum = frameProbes.normalizedBlendWeightSum;
    stats.normalizedBlendWeightError = frameProbes.normalizedBlendWeightError;
    stats.capturedSceneRequestedCount =
        frameProbes.capturedSceneRequestedCount;
    stats.capturedScenePlaceholderAllocatedCount =
        frameProbes.capturedScenePlaceholderAllocatedCount;
    stats.capturedScenePlaceholderReadyCount =
        frameProbes.capturedScenePlaceholderReadyCount;
    stats.capturedSceneInvalidatedCount =
        frameProbes.capturedSceneInvalidatedCount;
    stats.capturedSceneRefreshRequestedCount =
        frameProbes.capturedSceneRefreshRequestedCount;
    stats.forcedRefreshRequested =
        frameProbes.forcedRefreshRequested ? 1u : 0u;
    stats.sceneDirtyRequested =
        frameProbes.sceneDirtyRequested ? 1u : 0u;
    stats.droppedProbeCount = frameProbes.droppedSceneProbeCount;
    stats.selectedProbeIndex = frameProbes.selectedSceneProbeIndex;
    stats.selectedProbeIndices.fill(-1);
    stats.selectedCaptureSlots.fill(-1);
    stats.selectedCaptureSourceTypes.fill(0u);
    stats.selectedCaptureFallbackReasons.fill(
        static_cast<u32>(
            RendererReflectionProbeCaptureFallbackReason::NoActiveSceneProbe
        )
    );
    stats.selectedRefreshPolicies.fill(
        static_cast<u32>(RendererReflectionProbeRefreshPolicy::Static)
    );
    stats.selectedCapturedScenePlaceholderReady.fill(0u);
    stats.selectedCapturedSceneInvalidated.fill(0u);
    stats.selectedCaptureMipCounts.fill(0u);
    stats.selectedAuthoredAssetHashes.fill(0u);
    stats.selectedBlendWeights.fill(0.0f);
    stats.selectedNormalizedBlendWeights.fill(0.0f);
    const u32 selectedProbeCount = std::min<u32>(
        frameProbes.selectedProbeCount,
        static_cast<u32>(stats.selectedProbeIndices.size())
    );
    for (u32 index = 0; index < selectedProbeCount; ++index) {
        stats.selectedProbeIndices[index] =
            frameProbes.selectedProbes[index].sceneIndex;
        stats.selectedCaptureSlots[index] =
            frameProbes.selectedCaptureSlots[index];
        stats.selectedCaptureSourceTypes[index] =
            static_cast<u32>(frameProbes.selectedProbes[index].captureSource);
        stats.selectedCaptureFallbackReasons[index] =
            static_cast<u32>(frameProbes.selectedCaptureFallbackReasons[index]);
        stats.selectedRefreshPolicies[index] =
            static_cast<u32>(frameProbes.selectedRefreshPolicies[index]);
        stats.selectedCapturedScenePlaceholderReady[index] =
            frameProbes.selectedCapturedScenePlaceholderReady[index] ? 1u : 0u;
        stats.selectedCapturedSceneInvalidated[index] =
            frameProbes.selectedCapturedSceneInvalidated[index] ? 1u : 0u;
        stats.selectedCaptureMipCounts[index] =
            frameProbes.selectedCaptureMipCounts[index];
        stats.selectedAuthoredAssetHashes[index] =
            frameProbes.selectedAuthoredAssetHashes[index];
        stats.selectedBlendWeights[index] =
            frameProbes.selectedBlendWeights[index];
        stats.selectedNormalizedBlendWeights[index] =
            frameProbes.selectedNormalizedBlendWeights[index];
        if (frameProbes.selectedDiffuseIrradianceLobesReady[index]) {
            for (const glm::vec4& lobe :
                frameProbes.selectedDiffuseIrradianceLobes[index]) {
                stats.authoredCubemapDiffuseLobeEnergy +=
                    std::abs(lobe.x) + std::abs(lobe.y) + std::abs(lobe.z);
            }
        }
    }
    const u32 lobeValueCount =
        frameProbes.selectedDiffuseIrradianceLobesReadyCount *
        static_cast<u32>(kReflectionProbeDiffuseLobeCount) *
        3u;
    if (lobeValueCount > 0u) {
        stats.authoredCubemapDiffuseLobeEnergy /=
            static_cast<f32>(lobeValueCount);
    }
    stats.maxBlendWeight = frameProbes.maxBlendWeight;
    stats.totalBlendWeight = frameProbes.totalBlendWeight;
    stats.multiBlendEnabled = frameProbes.multiBlendEnabled ? 1u : 0u;
    stats.localEnabled =
        frameProbes.activeLocalProbeCount > 0 && ReflectionProbeContributes(localProbe)
            ? 1u
            : 0u;
    stats.localSceneOwned =
        stats.localEnabled > 0 && localProbe.sceneOwned ? 1u : 0u;
    stats.localRadius = localProbe.radius;
    stats.localBoxExtentX = localProbe.boxExtents.x;
    stats.localBoxExtentY = localProbe.boxExtents.y;
    stats.localBoxExtentZ = localProbe.boxExtents.z;
    stats.localIntensity = localProbe.intensity;
    stats.localBlendStrength = localProbe.blendStrength;
    stats.localFalloff = localProbe.falloff;
    stats.captureSourceType =
        static_cast<u32>(frameProbes.captureSource);
    stats.refreshPolicy =
        static_cast<u32>(frameProbes.refreshPolicy);
    stats.captureResourceReady =
        frameProbes.captureResourceReady ? 1u : 0u;
    stats.captureFallbackReason =
        static_cast<u32>(frameProbes.captureFallbackReason);
    stats.captureDescriptorBound =
        frameProbes.captureDescriptorBound ? 1u : 0u;
    stats.boxProjectionEnabled =
        frameProbes.boxProjectionEnabled ? 1u : 0u;
    stats.influenceMode = frameProbes.influenceMode;
    stats.parallaxCorrectionEnabled =
        frameProbes.parallaxCorrectionEnabled ? 1u : 0u;
    WriteReflectionReceiverAuditStats(frameProbes, stats);
}

void PopulateReflectionProbeUniforms(
    const FrameReflectionProbeSet& reflectionProbes,
    bool globalCubemapSamplingEnabled,
    bool cubemapSamplingEnabled,
    UniformBufferObject& uniformData
) {
    const RendererReflectionProbe& localProbe = reflectionProbes.localProbe;
    const bool localReflectionProbeApplied =
        reflectionProbes.fallbackEnabled &&
        reflectionProbes.activeLocalProbeCount > 0 &&
        ReflectionProbeContributes(localProbe);
    const bool localReflectionProbeCubemapApplied =
        localReflectionProbeApplied &&
        cubemapSamplingEnabled &&
        reflectionProbes.captureResourceReady;
    uniformData.localReflectionProbePositionRadius = glm::vec4(
        localProbe.center,
        localProbe.radius
    );
    uniformData.localReflectionProbeControls = glm::vec4(
        localReflectionProbeApplied ? 1.0f : 0.0f,
        localProbe.intensity,
        localProbe.blendStrength,
        localProbe.falloff
    );
    uniformData.localReflectionProbeColor = glm::vec4(
        glm::clamp(localProbe.color, glm::vec3(0.0f), glm::vec3(4.0f)),
        localReflectionProbeCubemapApplied ? 1.0f : 0.0f
    );
    uniformData.localReflectionProbeBoxExtentsProjection = glm::vec4(
        glm::max(localProbe.boxExtents, glm::vec3(0.01f)),
        ReflectionProbeBoxProjectionEnabled(localProbe) ? 1.0f : 0.0f
    );

    const u32 selectedProbeCount = std::min<u32>(
        reflectionProbes.selectedProbeCount,
        static_cast<u32>(kMaxFrameReflectionProbes)
    );
    for (u32 index = 0; index < selectedProbeCount; ++index) {
        const RendererReflectionProbe& probe =
            reflectionProbes.selectedProbes[index];
        const bool probeApplied =
            reflectionProbes.fallbackEnabled && ReflectionProbeContributes(probe);
        const bool probeCubemapApplied =
            probeApplied &&
            cubemapSamplingEnabled &&
            reflectionProbes.selectedCaptureDescriptorBound[index];
        uniformData.reflectionProbePositionRadius[index] = glm::vec4(
            probe.center,
            probe.radius
        );
        uniformData.reflectionProbeControlsArray[index] = glm::vec4(
            probeApplied ? 1.0f : 0.0f,
            probe.intensity,
            probe.blendStrength,
            probe.falloff
        );
        const glm::vec3 samplingTint =
            probe.captureSource == RendererReflectionProbeCaptureSource::CapturedScene
                ? glm::vec3(1.0f)
                : glm::clamp(probe.color, glm::vec3(0.0f), glm::vec3(4.0f));
        uniformData.reflectionProbeColorArray[index] = glm::vec4(
            samplingTint,
            probeCubemapApplied ? static_cast<f32>(index + 1u) : 0.0f
        );
        uniformData.reflectionProbeBoxExtentsProjectionArray[index] = glm::vec4(
            glm::max(probe.boxExtents, glm::vec3(0.01f)),
            ReflectionProbeBoxProjectionEnabled(probe) ? 1.0f : 0.0f
        );
        const u32 mipCount = reflectionProbes.selectedCaptureMipCounts[index];
        uniformData.reflectionProbeMipControls[index] = glm::vec4(
            mipCount > 0u ? static_cast<f32>(mipCount - 1u) : 0.0f,
            static_cast<f32>(mipCount),
            probeCubemapApplied ? 1.0f : 0.0f,
            probe.captureSource ==
                    RendererReflectionProbeCaptureSource::CapturedScene &&
                    reflectionProbes
                        .selectedCapturedSceneDiffuseIrradianceReady[index] &&
                    probeCubemapApplied
                ? 1.0f
                : 0.0f
        );
        if (reflectionProbes.selectedDiffuseIrradianceLobesReady[index]) {
            for (std::size_t lobe = 0; lobe < kReflectionProbeDiffuseLobeCount;
                 ++lobe) {
                const std::size_t lobeIndex =
                    static_cast<std::size_t>(index) *
                        kReflectionProbeDiffuseLobeCount +
                    lobe;
                uniformData.reflectionProbeDiffuseLobes[lobeIndex] =
                    reflectionProbes.selectedDiffuseIrradianceLobes[index][lobe];
            }
        }
    }
    uniformData.reflectionProbeBlendControls = glm::vec4(
        static_cast<f32>(selectedProbeCount),
        reflectionProbes.multiBlendEnabled ? 1.0f : 0.0f,
        globalCubemapSamplingEnabled ? 1.0f : 0.0f,
        (EnvironmentFlagEnabled("SE_REFLECTION_PROBE_LEGACY_BLEND") ? 0.0f : 1.0f) +
            (EnvironmentFlagEnabled("SE_REFLECTION_PROBE_LEGACY_ENERGY_SCALE")
                ? 0.0f
                : 2.0f)
    );
}

struct ScreenTileBounds {
    u32 minX = 0;
    u32 minY = 0;
    u32 maxX = 0;
    u32 maxY = 0;
    bool valid = false;
    bool conservative = false;
};

ScreenTileBounds FullScreenTileBounds(u32 tileCountX, u32 tileCountY) {
    ScreenTileBounds bounds{};
    if (tileCountX == 0 || tileCountY == 0) {
        return bounds;
    }

    bounds.maxX = tileCountX - 1;
    bounds.maxY = tileCountY - 1;
    bounds.valid = true;
    bounds.conservative = true;
    return bounds;
}

ScreenTileBounds ProjectLightSphereToTiles(
    const GpuLocalLightRecord& light,
    const FrameMatrices* matrices,
    const VkExtent2D& extent,
    u32 tileSize,
    u32 tileCountX,
    u32 tileCountY,
    u32 guardTiles
) {
    if (matrices == nullptr || extent.width == 0 || extent.height == 0) {
        return FullScreenTileBounds(tileCountX, tileCountY);
    }

    const glm::vec3 center = glm::vec3(light.positionRadius);
    const f32 radius = std::max(light.positionRadius.w, 0.001f);
    const glm::mat4 viewProjection = matrices->proj * matrices->view;

    glm::vec2 minPixel{ std::numeric_limits<f32>::max() };
    glm::vec2 maxPixel{ std::numeric_limits<f32>::lowest() };
    bool projectedAny = false;
    bool touchesClipBoundary = false;
    const auto includeProjectedPoint = [&](const glm::vec3& point) {
        const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
        if (std::abs(clip.w) <= 0.0001f) {
            touchesClipBoundary = true;
            return;
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (clip.w <= 0.0f || ndc.z < 0.0f || ndc.z > 1.0f) {
            touchesClipBoundary = true;
        }

        const f32 pixelX = (ndc.x * 0.5f + 0.5f) * static_cast<f32>(extent.width);
        const f32 pixelY = (ndc.y * 0.5f + 0.5f) * static_cast<f32>(extent.height);
        minPixel = glm::min(minPixel, glm::vec2(pixelX, pixelY));
        maxPixel = glm::max(maxPixel, glm::vec2(pixelX, pixelY));
        projectedAny = true;
    };

    includeProjectedPoint(center);
    for (f32 xSign : { -1.0f, 1.0f }) {
        for (f32 ySign : { -1.0f, 1.0f }) {
            for (f32 zSign : { -1.0f, 1.0f }) {
                includeProjectedPoint(
                    center + glm::vec3(xSign, ySign, zSign) * radius
                );
            }
        }
    }

    if (!projectedAny || touchesClipBoundary) {
        return FullScreenTileBounds(tileCountX, tileCountY);
    }

    if (maxPixel.x < 0.0f || maxPixel.y < 0.0f ||
        minPixel.x > static_cast<f32>(extent.width) ||
        minPixel.y > static_cast<f32>(extent.height)) {
        return {};
    }

    if (minPixel.x > maxPixel.x || minPixel.y > maxPixel.y) {
        return {};
    }

    minPixel = glm::clamp(
        minPixel,
        glm::vec2(0.0f),
        glm::vec2(
            static_cast<f32>(extent.width),
            static_cast<f32>(extent.height)
        )
    );
    maxPixel = glm::clamp(
        maxPixel,
        glm::vec2(0.0f),
        glm::vec2(
            static_cast<f32>(extent.width),
            static_cast<f32>(extent.height)
        )
    );

    ScreenTileBounds bounds{};
    bounds.minX = std::min<u32>(
        tileCountX - 1,
        static_cast<u32>(std::floor(minPixel.x / static_cast<f32>(tileSize)))
    );
    bounds.minY = std::min<u32>(
        tileCountY - 1,
        static_cast<u32>(std::floor(minPixel.y / static_cast<f32>(tileSize)))
    );
    bounds.maxX = std::min<u32>(
        tileCountX - 1,
        static_cast<u32>(std::floor(std::max(maxPixel.x - 1.0f, 0.0f) /
            static_cast<f32>(tileSize)))
    );
    bounds.maxY = std::min<u32>(
        tileCountY - 1,
        static_cast<u32>(std::floor(std::max(maxPixel.y - 1.0f, 0.0f) /
            static_cast<f32>(tileSize)))
    );
    if (guardTiles > 0u) {
        bounds.minX = bounds.minX > guardTiles ? bounds.minX - guardTiles : 0u;
        bounds.minY = bounds.minY > guardTiles ? bounds.minY - guardTiles : 0u;
        bounds.maxX = std::min(tileCountX - 1, bounds.maxX + guardTiles);
        bounds.maxY = std::min(tileCountY - 1, bounds.maxY + guardTiles);
    }
    bounds.valid = bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY;
    return bounds;
}

FrameLightTileStats PopulateLightTileAssignments(
    LightBufferObject& lightData,
    std::size_t localCount,
    const VkExtent2D& extent,
    const FrameMatrices* matrices
) {
    const u32 tileSize = static_cast<u32>(kLightTileSize);
    const u32 tileCountX = CeilDivU32(extent.width, tileSize);
    const u32 tileCountY = CeilDivU32(extent.height, tileSize);
    const u64 requestedTileCount =
        static_cast<u64>(tileCountX) * static_cast<u64>(tileCountY);
    const bool canUseTileAssignments =
        requestedTileCount > 0 &&
        requestedTileCount <= kMaxFrameLightTiles &&
        localCount <= kMaxFrameLocalLights;

    FrameLightTileStats stats{};
    stats.tileSize = tileSize;
    stats.tileCountX = tileCountX;
    stats.tileCountY = tileCountY;
    stats.tileCount = requestedTileCount > std::numeric_limits<u32>::max()
        ? std::numeric_limits<u32>::max()
        : static_cast<u32>(requestedTileCount);
    stats.assignmentCapacity = stats.tileCount > std::numeric_limits<u32>::max() /
        static_cast<u32>(kMaxFrameLightsPerTile)
        ? std::numeric_limits<u32>::max()
        : stats.tileCount * static_cast<u32>(kMaxFrameLightsPerTile);
    stats.overflowCapacity = static_cast<u32>(kMaxFrameLightTileOverflowIndices);

    if (!canUseTileAssignments) {
        stats.fallbackCount = localCount > 0 ? 1 : 0;
        lightData.tileInfo = glm::vec4(
            static_cast<f32>(tileSize),
            0.0f,
            0.0f,
            static_cast<f32>(kMaxFrameLightsPerTile)
        );
        return stats;
    }

    lightData.lightCounts.w = 1.0f;
    lightData.tileInfo = glm::vec4(
        static_cast<f32>(tileSize),
        static_cast<f32>(tileCountX),
        static_cast<f32>(tileCountY),
        static_cast<f32>(kMaxFrameLightsPerTile)
    );

    const u32 localLightCount = std::min<u32>(
        static_cast<u32>(localCount),
        static_cast<u32>(kMaxFrameLocalLights)
    );
    u32 tileGuardTiles = 1u;
    if (const std::optional<f32> overrideGuardTiles =
            EnvironmentFloatOverride("SE_LIGHT_TILE_GUARD_TILES")) {
        tileGuardTiles = std::clamp(
            static_cast<u32>(*overrideGuardTiles + 0.5f),
            0u,
            4u
        );
    }
    const u32 groupsPerTile = static_cast<u32>(kLightIndexGroupsPerTile);
    std::vector<std::array<u32, kMaxFrameLightsPerTile>> tileAssignments(stats.tileCount);
    std::vector<std::vector<u32>> tileOverflowAssignments(stats.tileCount);
    std::vector<u32> tileAssignmentCounts(stats.tileCount, 0u);
    std::vector<u32> tileRawCandidateCounts(stats.tileCount, 0u);
    for (u32 localLightIndex = 0; localLightIndex < localLightCount; ++localLightIndex) {
        const GpuLocalLightRecord& localLight = lightData.localLights[localLightIndex];
        const bool rectLight = localLight.directionType.w >= 1.5f;
        const ScreenTileBounds bounds = rectLight
            ? FullScreenTileBounds(tileCountX, tileCountY)
            : ProjectLightSphereToTiles(
                localLight,
                matrices,
                extent,
                tileSize,
                tileCountX,
                tileCountY,
                tileGuardTiles
            );
        if (!bounds.valid) {
            continue;
        }
        if (bounds.conservative) {
            ++stats.fallbackCount;
        }

        for (u32 tileY = bounds.minY; tileY <= bounds.maxY; ++tileY) {
            for (u32 tileX = bounds.minX; tileX <= bounds.maxX; ++tileX) {
                const u32 tileIndex = tileY * tileCountX + tileX;
                u32& assignmentCount = tileAssignmentCounts[tileIndex];
                ++tileRawCandidateCounts[tileIndex];
                if (assignmentCount >= kMaxFrameLightsPerTile) {
                    tileOverflowAssignments[tileIndex].push_back(localLightIndex);
                    continue;
                }

                tileAssignments[tileIndex][assignmentCount] = localLightIndex;
                ++assignmentCount;
                ++stats.assignments;
            }
        }
    }

    std::vector<u32> tileOverflowOffsets(stats.tileCount, 0u);
    std::vector<u32> tileOverflowCounts(stats.tileCount, 0u);
    u32 overflowCursor = 0;
    for (u32 tileIndex = 0; tileIndex < stats.tileCount; ++tileIndex) {
        const std::vector<u32>& overflowAssignments = tileOverflowAssignments[tileIndex];
        tileOverflowOffsets[tileIndex] = overflowCursor;
        const u32 remainingOverflowCapacity =
            overflowCursor >= kMaxFrameLightTileOverflowIndices
                ? 0u
                : static_cast<u32>(kMaxFrameLightTileOverflowIndices) - overflowCursor;
        const u32 overflowCount = std::min<u32>(
            static_cast<u32>(overflowAssignments.size()),
            remainingOverflowCapacity
        );
        tileOverflowCounts[tileIndex] = overflowCount;
        if (overflowCount > 0) {
            ++stats.overflowTileCount;
        }
        stats.overflowAssignments += overflowCount;
        const u32 droppedOverflowCount =
            static_cast<u32>(overflowAssignments.size()) - overflowCount;
        stats.overflowDropped += droppedOverflowCount;
        stats.fallbackCount += droppedOverflowCount;
        for (u32 overflowIndex = 0; overflowIndex < overflowCount; ++overflowIndex) {
            const u32 absoluteIndex = overflowCursor + overflowIndex;
            lightData.tileOverflowLightIndices[absoluteIndex] =
                overflowAssignments[overflowIndex];
        }
        overflowCursor += overflowCount;
    }

    for (u32 tileIndex = 0; tileIndex < stats.tileCount; ++tileIndex) {
        const u32 groupOffset = tileIndex * groupsPerTile;
        const u32 assignmentCount = tileAssignmentCounts[tileIndex];
        const u32 rawCandidateCount = tileRawCandidateCounts[tileIndex];
        const u32 overflowCount = tileOverflowCounts[tileIndex];
        const u32 droppedOverflowCount = rawCandidateCount -
            std::min(rawCandidateCount, assignmentCount + overflowCount);
        lightData.lightTiles[tileIndex].offsetCount =
            glm::uvec4(
                groupOffset,
                assignmentCount,
                rawCandidateCount,
                rawCandidateCount > kMaxFrameLightsPerTile ? 1u : 0u
            );
        lightData.lightTiles[tileIndex].overflowOffsetCount =
            glm::uvec4(
                tileOverflowOffsets[tileIndex],
                overflowCount,
                droppedOverflowCount,
                overflowCount > 0u ? 1u : 0u
            );

        for (u32 groupIndex = 0; groupIndex < groupsPerTile; ++groupIndex) {
            glm::uvec4 packedIndices{ 0u };
            for (u32 component = 0; component < 4; ++component) {
                const u32 assignmentIndex = groupIndex * 4u + component;
                if (assignmentIndex < assignmentCount) {
                    packedIndices[component] =
                        tileAssignments[tileIndex][assignmentIndex];
                }
            }
            lightData.tileLightIndexGroups[groupOffset + groupIndex] = packedIndices;
        }
    }

    return stats;
}

void AddScenePointLights(
    const Scene3D* scene,
    FrameLightSet& lights
) {
    if (scene == nullptr) {
        return;
    }

    for (const PointLight3D& pointLight : scene->PointLights()) {
        if (!pointLight.enabled ||
            lights.localCount >= lights.localLights.size() ||
            pointLight.radius <= 0.001f ||
            pointLight.intensity <= 0.0f) {
            continue;
        }

        lights.localLights[lights.localCount] = PointLocalLight(
            pointLight.position,
            pointLight.radius,
            glm::max(pointLight.color, glm::vec3(0.0f)),
            pointLight.intensity,
            pointLight.sourceRadius
        );
        ++lights.localCount;
    }
}

void AddSceneSpotLights(
    const Scene3D* scene,
    FrameLightSet& lights
) {
    if (scene == nullptr) {
        return;
    }

    for (const SpotLight3D& spotLight : scene->SpotLights()) {
        if (!spotLight.enabled ||
            lights.localCount >= lights.localLights.size() ||
            spotLight.radius <= 0.001f ||
            spotLight.intensity <= 0.0f) {
            continue;
        }

        lights.localLights[lights.localCount] = SpotLocalLight(
            spotLight.position,
            spotLight.direction,
            spotLight.radius,
            glm::max(spotLight.color, glm::vec3(0.0f)),
            spotLight.intensity,
            spotLight.innerConeDegrees,
            spotLight.outerConeDegrees,
            spotLight.sourceRadius
        );
        ++lights.localCount;
    }
}

void AddSceneRectLights(
    const Scene3D* scene,
    FrameLightSet& lights
) {
    if (scene == nullptr) {
        return;
    }

    for (const RectLight3D& rectLight : scene->RectLights()) {
        if (!rectLight.enabled ||
            lights.localCount >= lights.localLights.size() ||
            rectLight.radius <= 0.001f ||
            rectLight.intensity <= 0.0f ||
            rectLight.width <= 0.001f ||
            rectLight.height <= 0.001f) {
            continue;
        }

        lights.localLights[lights.localCount] = RectLocalLight(
            rectLight.position,
            rectLight.direction,
            rectLight.width,
            rectLight.height,
            rectLight.radius,
            glm::max(rectLight.color, glm::vec3(0.0f)),
            rectLight.intensity
        );
        ++lights.localCount;
        ++lights.rectCount;
    }
}

bool ApplySceneDirectionalLight(
    const Scene3D* scene,
    FrameLightSet& lights
) {
    if (scene == nullptr) {
        return false;
    }

    const DirectionalLight3D* directionalLight = scene->PrimaryDirectionalLight();
    if (directionalLight == nullptr || !directionalLight->enabled) {
        return false;
    }

    glm::vec3 direction = directionalLight->direction;
    if (glm::dot(direction, direction) <= 0.0001f) {
        return false;
    }

    lights.primaryDirectional.direction = glm::normalize(direction);
    lights.primaryDirectional.intensity = std::max(directionalLight->intensity, 0.0f);
    lights.primaryDirectional.ambient = std::max(directionalLight->ambient, 0.0f);
    lights.primaryDirectional.specular = std::max(directionalLight->specular, 0.0f);
    lights.primaryDirectional.angularRadiusRadians = std::clamp(
        directionalLight->angularRadiusRadians,
        0.0f,
        0.05f
    );
    lights.directionalCount = 1;
    return true;
}

bool ApplyMaterialDirectionalFallback(
    std::span<const RenderCommand> renderCommands,
    FrameLightSet& lights
) {
    for (const RenderCommand& command : renderCommands) {
        const glm::vec3 candidate{
            command.materialPushConstants.materialCustom.x,
            command.materialPushConstants.materialCustom.y,
            command.materialPushConstants.materialCustom.z
        };
        if (glm::dot(candidate, candidate) <= 0.0001f) {
            continue;
        }

        lights.primaryDirectional.direction = glm::normalize(candidate);
        lights.primaryDirectional.intensity =
            std::max(command.materialPushConstants.materialControls.y, 0.0f);
        lights.primaryDirectional.ambient =
            std::max(command.materialPushConstants.materialCustom.w, 0.0f);
        lights.primaryDirectional.specular =
            std::max(command.materialPushConstants.materialControls.z, 0.0f);
        lights.directionalCount = 1;
        return true;
    }

    return false;
}

int GBufferDebugViewIndex(ForwardDebugView view) {
    switch (view) {
    case ForwardDebugView::GBufferAlbedo:
        return 0;
    case ForwardDebugView::GBufferNormal:
        return 1;
    case ForwardDebugView::GBufferRoughness:
        return 2;
    case ForwardDebugView::GBufferMetallic:
        return 3;
    case ForwardDebugView::GBufferOcclusion:
        return 11;
    case ForwardDebugView::GBufferMaterialId:
        return 4;
    case ForwardDebugView::GBufferDepth:
        return 5;
    case ForwardDebugView::GBufferEmissive:
        return 6;
    case ForwardDebugView::GBufferVelocity:
        return 7;
    case ForwardDebugView::DeferredShadow:
        return 8;
    case ForwardDebugView::DirectionalPcssDelta:
        return 13;
    case ForwardDebugView::ShadowCascade:
        if (EnvironmentFlagEnabled("SE_SHADOW_CASCADE_DIRECT")) {
            return -1;
        }
        return 9;
    case ForwardDebugView::ShadowCascadeReceiver:
        return 12;
    case ForwardDebugView::ShadowCascadeAtlas:
        return 10;
    default:
        return -1;
    }
}

int DeferredPbrDebugViewIndex(ForwardDebugView view) {
    switch (view) {
    case ForwardDebugView::DeferredDirect:
        return 1;
    case ForwardDebugView::DeferredAmbient:
        return 2;
    case ForwardDebugView::DeferredSpecular:
        return 3;
    case ForwardDebugView::DeferredAmbientDiffuse:
        return 18;
    case ForwardDebugView::DeferredAmbientSpecular:
        return 19;
    case ForwardDebugView::DeferredAmbientProbe:
        return 20;
    case ForwardDebugView::DeferredEnergyBalance:
        return 22;
    case ForwardDebugView::DeferredLightComplexity:
        return 4;
    case ForwardDebugView::DeferredTileOccupancy:
        return 5;
    case ForwardDebugView::DeferredMaterialTable:
        return 6;
    case ForwardDebugView::LocalShadowAtlas:
        return 7;
    case ForwardDebugView::LocalShadowVisibility:
        return 8;
    case ForwardDebugView::LocalShadowSelected:
        return 21;
    case ForwardDebugView::ContactShadow:
        return 9;
    case ForwardDebugView::LocalShadowFace:
        return 10;
    case ForwardDebugView::Ssao:
        return 11;
    case ForwardDebugView::Ssr:
        return 12;
    case ForwardDebugView::ReflectionProbe:
        return 13;
    case ForwardDebugView::ReflectionProbeContrast:
        return 17;
    case ForwardDebugView::ReflectionProbeRadiance:
        return 23;
    case ForwardDebugView::HeightFog:
        return 14;
    case ForwardDebugView::ProbeGrid:
        return 15;
    case ForwardDebugView::ProbeGridCell:
        return 16;
    default:
        return 0;
    }
}

int WeightedTranslucencyDebugViewIndex(ForwardDebugView view) {
    switch (view) {
    case ForwardDebugView::WeightedTranslucencyAccum:
        return 1;
    case ForwardDebugView::WeightedTranslucencyRevealage:
        return 2;
    case ForwardDebugView::WeightedTranslucencyWeight:
        return 3;
    default:
        return 0;
    }
}

bool UsesDeferredHdrComposite(ForwardDebugView view) {
    return view == ForwardDebugView::DeferredHdr ||
        view == ForwardDebugView::DeferredDirect ||
        view == ForwardDebugView::DeferredAmbient ||
        view == ForwardDebugView::DeferredSpecular ||
        view == ForwardDebugView::DeferredAmbientDiffuse ||
        view == ForwardDebugView::DeferredAmbientSpecular ||
        view == ForwardDebugView::DeferredAmbientProbe ||
        view == ForwardDebugView::DeferredEnergyBalance ||
        view == ForwardDebugView::DeferredLightComplexity ||
        view == ForwardDebugView::DeferredTileOccupancy ||
        view == ForwardDebugView::DeferredMaterialTable ||
        view == ForwardDebugView::LocalShadowAtlas ||
        view == ForwardDebugView::LocalShadowVisibility ||
        view == ForwardDebugView::LocalShadowSelected ||
        view == ForwardDebugView::ContactShadow ||
        view == ForwardDebugView::LocalShadowFace ||
        view == ForwardDebugView::Ssao ||
        view == ForwardDebugView::Ssr ||
        view == ForwardDebugView::ReflectionProbe ||
        view == ForwardDebugView::ReflectionProbeContrast ||
        view == ForwardDebugView::ReflectionProbeRadiance ||
        view == ForwardDebugView::HeightFog ||
        view == ForwardDebugView::ProbeGrid ||
        view == ForwardDebugView::ProbeGridCell ||
        view == ForwardDebugView::Bloom ||
        view == ForwardDebugView::ColorGrading ||
        view == ForwardDebugView::ToneMapping ||
        view == ForwardDebugView::AutoExposure ||
        view == ForwardDebugView::Sharpening ||
        view == ForwardDebugView::Taa ||
        view == ForwardDebugView::TaaRejection ||
        view == ForwardDebugView::TaaHistory ||
        view == ForwardDebugView::TaaReprojection ||
        view == ForwardDebugView::WeightedTranslucencyAccum ||
        view == ForwardDebugView::WeightedTranslucencyRevealage ||
        view == ForwardDebugView::WeightedTranslucencyWeight;
}

bool DebugViewBypassesTemporalReconstruction(ForwardDebugView view) {
    return view == ForwardDebugView::LocalShadowSelected ||
        view == ForwardDebugView::ContactShadow ||
        view == ForwardDebugView::DeferredEnergyBalance ||
        view == ForwardDebugView::ShadowCascade ||
        view == ForwardDebugView::ShadowCascadeReceiver ||
        view == ForwardDebugView::ShadowCascadeAtlas ||
        view == ForwardDebugView::DirectionalPcssDelta;
}

std::optional<ForwardDebugView> ForwardDebugViewFromEnvironment() {
    const std::string value = ReadEnvironmentString("SE_RENDER_VIEW");
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "deferred-hdr" || value == "DeferredHDR" || value == "deferred_hdr") {
        return ForwardDebugView::DeferredHdr;
    }
    if (value == "deferred-shadow" || value == "DeferredShadow" || value == "deferred_shadow") {
        return ForwardDebugView::DeferredShadow;
    }
    if (value == "directional-pcss-delta" ||
        value == "directional_pcss_delta" ||
        value == "pcss-delta" ||
        value == "pcss_delta") {
        return ForwardDebugView::DirectionalPcssDelta;
    }
    if (value == "shadow-cascade" ||
        value == "ShadowCascade" ||
        value == "shadow_cascade" ||
        value == "csm-cascade" ||
        value == "csm_cascade" ||
        value == "cascade-debug" ||
        value == "cascade_debug") {
        return ForwardDebugView::ShadowCascade;
    }
    if (value == "shadow-cascade-atlas" ||
        value == "ShadowCascadeAtlas" ||
        value == "shadow_cascade_atlas" ||
        value == "csm-atlas" ||
        value == "csm_atlas" ||
        value == "cascade-atlas" ||
        value == "cascade_atlas") {
        return ForwardDebugView::ShadowCascadeAtlas;
    }
    if (value == "shadow-cascade-receiver" ||
        value == "ShadowCascadeReceiver" ||
        value == "shadow_cascade_receiver" ||
        value == "csm-receiver" ||
        value == "csm_receiver" ||
        value == "cascade-receiver" ||
        value == "cascade_receiver") {
        return ForwardDebugView::ShadowCascadeReceiver;
    }
    if (value == "deferred-direct" || value == "DeferredDirect" || value == "deferred_direct") {
        return ForwardDebugView::DeferredDirect;
    }
    if (value == "deferred-ambient" || value == "DeferredAmbient" || value == "deferred_ambient") {
        return ForwardDebugView::DeferredAmbient;
    }
    if (value == "deferred-ambient-diffuse" ||
        value == "deferred_ambient_diffuse" ||
        value == "DeferredAmbientDiffuse" ||
        value == "ambient-diffuse" ||
        value == "ambient_diffuse") {
        return ForwardDebugView::DeferredAmbientDiffuse;
    }
    if (value == "deferred-ambient-specular" ||
        value == "deferred_ambient_specular" ||
        value == "DeferredAmbientSpecular" ||
        value == "ambient-specular" ||
        value == "ambient_specular") {
        return ForwardDebugView::DeferredAmbientSpecular;
    }
    if (value == "deferred-ambient-probe" ||
        value == "deferred_ambient_probe" ||
        value == "DeferredAmbientProbe" ||
        value == "ambient-probe" ||
        value == "ambient_probe" ||
        value == "probe-ambient" ||
        value == "probe_ambient") {
        return ForwardDebugView::DeferredAmbientProbe;
    }
    if (value == "deferred-specular" || value == "DeferredSpecular" || value == "deferred_specular") {
        return ForwardDebugView::DeferredSpecular;
    }
    if (value == "lighting-energy" ||
        value == "lighting_energy" ||
        value == "energy-balance" ||
        value == "energy_balance" ||
        value == "deferred-energy" ||
        value == "deferred_energy" ||
        value == "deferred-energy-balance" ||
        value == "deferred_energy_balance" ||
        value == "DeferredEnergyBalance" ||
        value == "LightingEnergyBalance") {
        return ForwardDebugView::DeferredEnergyBalance;
    }
    if (value == "forward-light-complexity" ||
        value == "ForwardLightComplexity" ||
        value == "forward_light_complexity" ||
        value == "forward-plus-light-complexity" ||
        value == "forward_plus_light_complexity") {
        return ForwardDebugView::ForwardLightComplexity;
    }
    if (value == "deferred-light-complexity" ||
        value == "DeferredLightComplexity" ||
        value == "deferred_light_complexity" ||
        value == "light-complexity" ||
        value == "light_complexity") {
        return ForwardDebugView::DeferredLightComplexity;
    }
    if (value == "deferred-material-table" ||
        value == "DeferredMaterialTable" ||
        value == "deferred_material_table" ||
        value == "material-table" ||
        value == "material_table") {
        return ForwardDebugView::DeferredMaterialTable;
    }
    if (value == "deferred-tile-occupancy" ||
        value == "DeferredTileOccupancy" ||
        value == "deferred_tile_occupancy" ||
        value == "tile-occupancy" ||
        value == "tile_occupancy") {
        return ForwardDebugView::DeferredTileOccupancy;
    }
    if (value == "local-shadow-atlas" ||
        value == "LocalShadowAtlas" ||
        value == "local_shadow_atlas" ||
        value == "local-shadow" ||
        value == "local_shadow" ||
        value == "local-shadow-debug" ||
        value == "local_shadow_debug") {
        return ForwardDebugView::LocalShadowAtlas;
    }
    if (value == "local-shadow-visibility" ||
        value == "LocalShadowVisibility" ||
        value == "local_shadow_visibility" ||
        value == "local-shadow-resolve" ||
        value == "local_shadow_resolve" ||
        value == "local-shadow-visibility-debug" ||
        value == "local_shadow_visibility_debug") {
        return ForwardDebugView::LocalShadowVisibility;
    }
    if (value == "local-shadow-selected" ||
        value == "LocalShadowSelected" ||
        value == "local_shadow_selected" ||
        value == "local-shadow-one" ||
        value == "local_shadow_one" ||
        value == "selected-local-shadow" ||
        value == "selected_local_shadow") {
        return ForwardDebugView::LocalShadowSelected;
    }
    if (value == "contact-shadow" ||
        value == "ContactShadow" ||
        value == "contact_shadow" ||
        value == "deferred-contact-shadow" ||
        value == "deferred_contact_shadow" ||
        value == "contact-shadow-debug" ||
        value == "contact_shadow_debug") {
        return ForwardDebugView::ContactShadow;
    }
    if (value == "local-shadow-face" ||
        value == "LocalShadowFace" ||
        value == "local_shadow_face" ||
        value == "point-shadow-face" ||
        value == "point_shadow_face" ||
        value == "local-shadow-seam" ||
        value == "local_shadow_seam" ||
        value == "point-shadow-seam" ||
        value == "point_shadow_seam") {
        return ForwardDebugView::LocalShadowFace;
    }
    if (value == "ssao" ||
        value == "SSAO" ||
        value == "screen-space-ao" ||
        value == "screen_space_ao" ||
        value == "ambient-occlusion" ||
        value == "ambient_occlusion") {
        return ForwardDebugView::Ssao;
    }
    if (value == "ssr" ||
        value == "SSR" ||
        value == "screen-space-reflection" ||
        value == "screen_space_reflection" ||
        value == "screen-space-reflections" ||
        value == "screen_space_reflections" ||
        value == "reflection-debug" ||
        value == "reflection_debug") {
        return ForwardDebugView::Ssr;
    }
    if (value == "reflection-probe" ||
        value == "reflection_probe" ||
        value == "ReflectionProbe" ||
        value == "global-reflection-probe" ||
        value == "global_reflection_probe" ||
        value == "reflection-fallback" ||
        value == "reflection_fallback") {
        return ForwardDebugView::ReflectionProbe;
    }
    if (value == "reflection-probe-contrast" ||
        value == "reflection_probe_contrast" ||
        value == "ReflectionProbeContrast" ||
        value == "reflection-contrast" ||
        value == "reflection_contrast" ||
        value == "probe-contrast" ||
        value == "probe_contrast") {
        return ForwardDebugView::ReflectionProbeContrast;
    }
    if (value == "reflection-probe-radiance" ||
        value == "reflection_probe_radiance" ||
        value == "ReflectionProbeRadiance" ||
        value == "local-probe-radiance" ||
        value == "local_probe_radiance") {
        return ForwardDebugView::ReflectionProbeRadiance;
    }
    if (value == "probe-grid" ||
        value == "probe_grid" ||
        value == "ProbeGrid" ||
        value == "light-probe-grid" ||
        value == "light_probe_grid") {
        return ForwardDebugView::ProbeGrid;
    }
    if (value == "probe-grid-cell" ||
        value == "probe_grid_cell" ||
        value == "ProbeGridCell" ||
        value == "probe-cell" ||
        value == "probe_cell") {
        return ForwardDebugView::ProbeGridCell;
    }
    if (value == "height-fog" ||
        value == "height_fog" ||
        value == "HeightFog" ||
        value == "distance-fog" ||
        value == "distance_fog" ||
        value == "fog") {
        return ForwardDebugView::HeightFog;
    }
    if (value == "bloom" ||
        value == "Bloom" ||
        value == "bloom-debug" ||
        value == "bloom_debug") {
        return ForwardDebugView::Bloom;
    }
    if (value == "color-grading" ||
        value == "color_grading" ||
        value == "color-grade" ||
        value == "color_grade" ||
        value == "grading" ||
        value == "ColorGrading") {
        return ForwardDebugView::ColorGrading;
    }
    if (value == "tone-map" ||
        value == "tone_map" ||
        value == "tonemap" ||
        value == "tone-mapping" ||
        value == "tone_mapping" ||
        value == "ToneMapping") {
        return ForwardDebugView::ToneMapping;
    }
    if (value == "auto-exposure" ||
        value == "auto_exposure" ||
        value == "autoexposure" ||
        value == "AutoExposure") {
        return ForwardDebugView::AutoExposure;
    }
    if (value == "sharpening" ||
        value == "sharpen" ||
        value == "sharpness" ||
        value == "Sharpening") {
        return ForwardDebugView::Sharpening;
    }
    if (value == "taa" ||
        value == "TAA" ||
        value == "temporal-aa" ||
        value == "temporal_aa" ||
        value == "temporal-antialiasing" ||
        value == "temporal_antialiasing") {
        return ForwardDebugView::Taa;
    }
    if (value == "taa-rejection" ||
        value == "taa_rejection" ||
        value == "temporal-rejection" ||
        value == "temporal_rejection" ||
        value == "history-rejection" ||
        value == "history_rejection") {
        return ForwardDebugView::TaaRejection;
    }
    if (value == "taa-history" ||
        value == "taa_history" ||
        value == "temporal-history" ||
        value == "temporal_history" ||
        value == "history-color" ||
        value == "history_color") {
        return ForwardDebugView::TaaHistory;
    }
    if (value == "taa-reprojection" ||
        value == "taa_reprojection" ||
        value == "temporal-reprojection" ||
        value == "temporal_reprojection" ||
        value == "history-uv" ||
        value == "history_uv") {
        return ForwardDebugView::TaaReprojection;
    }
    if (value == "wboit-accum" ||
        value == "wboit_accum" ||
        value == "weighted-translucency-accum" ||
        value == "weighted_translucency_accum" ||
        value == "WeightedTranslucencyAccum") {
        return ForwardDebugView::WeightedTranslucencyAccum;
    }
    if (value == "wboit-revealage" ||
        value == "wboit_revealage" ||
        value == "weighted-translucency-revealage" ||
        value == "weighted_translucency_revealage" ||
        value == "WeightedTranslucencyRevealage") {
        return ForwardDebugView::WeightedTranslucencyRevealage;
    }
    if (value == "wboit-weight" ||
        value == "wboit_weight" ||
        value == "weighted-translucency-weight" ||
        value == "weighted_translucency_weight" ||
        value == "WeightedTranslucencyWeight") {
        return ForwardDebugView::WeightedTranslucencyWeight;
    }
    if (value == "gbuffer-albedo" || value == "GBufferAlbedo" || value == "gbuffer_albedo") {
        return ForwardDebugView::GBufferAlbedo;
    }
    if (value == "gbuffer-normal" || value == "GBufferNormal" || value == "gbuffer_normal") {
        return ForwardDebugView::GBufferNormal;
    }
    if (value == "gbuffer-roughness" || value == "GBufferRoughness" || value == "gbuffer_roughness") {
        return ForwardDebugView::GBufferRoughness;
    }
    if (value == "gbuffer-metallic" || value == "GBufferMetallic" || value == "gbuffer_metallic") {
        return ForwardDebugView::GBufferMetallic;
    }
    if (value == "gbuffer-occlusion" ||
        value == "GBufferOcclusion" ||
        value == "gbuffer_occlusion" ||
        value == "gbuffer-ao" ||
        value == "gbuffer_ao") {
        return ForwardDebugView::GBufferOcclusion;
    }
    if (value == "gbuffer-material-id" ||
        value == "GBufferMaterialId" ||
        value == "gbuffer_material_id" ||
        value == "material-id" ||
        value == "material_id") {
        return ForwardDebugView::GBufferMaterialId;
    }
    if (value == "gbuffer-depth" || value == "GBufferDepth" || value == "gbuffer_depth") {
        return ForwardDebugView::GBufferDepth;
    }
    if (value == "gbuffer-emissive" || value == "GBufferEmissive" || value == "gbuffer_emissive") {
        return ForwardDebugView::GBufferEmissive;
    }
    if (value == "gbuffer-velocity" || value == "GBufferVelocity" || value == "gbuffer_velocity") {
        return ForwardDebugView::GBufferVelocity;
    }
    if (value == "lit" || value == "Lit") {
        return ForwardDebugView::Lit;
    }
    if (value == "albedo" || value == "Albedo") {
        return ForwardDebugView::Albedo;
    }
    if (value == "normal" || value == "Normal") {
        return ForwardDebugView::Normal;
    }
    if (value == "roughness" || value == "Roughness") {
        return ForwardDebugView::Roughness;
    }
    if (value == "metallic" || value == "Metallic") {
        return ForwardDebugView::Metallic;
    }
    if (value == "occlusion" || value == "Occlusion") {
        return ForwardDebugView::Occlusion;
    }
    if (value == "shadow" || value == "Shadow") {
        return ForwardDebugView::Shadow;
    }
    if (value == "light-depth" || value == "LightSpaceDepth") {
        return ForwardDebugView::LightSpaceDepth;
    }

    return std::nullopt;
}

std::optional<u32> ToneMapModeFromEnvironment() {
    std::string value = ReadEnvironmentString("SE_TONEMAP_MODE");
    if (value.empty()) {
        value = ReadEnvironmentString("SE_TONE_MAP_MODE");
    }
    if (value.empty()) {
        value = ReadEnvironmentString("SE_TONEMAP");
    }
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "aces" || value == "ACES" || value == "0") {
        return 0u;
    }
    if (value == "reinhard" || value == "Reinhard" || value == "1") {
        return 1u;
    }
    if (value == "linear" ||
        value == "Linear" ||
        value == "linear-clamp" ||
        value == "linear_clamp" ||
        value == "off" ||
        value == "2") {
        return 2u;
    }

    return std::nullopt;
}

bool MaterialFlagEnabled(f32 flags, f32 bit) {
    return std::fmod(std::floor(flags / bit), 2.0f) > 0.5f;
}

std::optional<VulkanShadowQuality> ShadowQualityFromEnvironment() {
    const std::string value = ReadEnvironmentString("SE_SHADOW_QUALITY");
    if (value.empty()) {
        return std::nullopt;
    }

    if (value == "off" || value == "Off" || value == "OFF" || value == "0") {
        return VulkanShadowQuality::Off;
    }
    if (value == "low" || value == "Low" || value == "LOW" || value == "1") {
        return VulkanShadowQuality::Low;
    }
    if (value == "medium" || value == "Medium" || value == "MEDIUM" || value == "2") {
        return VulkanShadowQuality::Medium;
    }
    if (value == "high" || value == "High" || value == "HIGH" || value == "3") {
        return VulkanShadowQuality::High;
    }
    if (value == "ultra" || value == "Ultra" || value == "ULTRA" || value == "4") {
        return VulkanShadowQuality::Ultra;
    }

    return std::nullopt;
}

}

VulkanRenderer::VulkanRenderer(
    Window& window,
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const VulkanSurface& surface,
    VkInstance instance,
    const VulkanCommandPool& commandPool,
    Scene2D* scene,
    Camera2D* camera,
    const VulkanRenderResources2D& renderResources,
    PipelineSpec pipelineSpec
) : m_Window(window),
    m_Device(device),
    m_PhysicalDevice(physicalDevice),
    m_Surface(surface),
    m_Instance(instance),
    m_CommandPool(commandPool),
    m_Scene(scene),
    m_Camera(camera),
    m_RenderResources(renderResources),
    m_PipelineSpec(std::move(pipelineSpec)) {
    m_RenderFeatures.Add(std::make_unique<VulkanSsaoFeature>());
    m_RenderFeatures.Add(std::make_unique<VulkanSsrFeature>());
    m_RenderFeatures.Add(std::make_unique<VulkanReflectionProbeFallbackFeature>());
    m_RenderFeatures.Add(std::make_unique<VulkanHeightFogFeature>());
    m_RenderFeatures.Add(std::make_unique<VulkanPostProcessFeature>());
    ApplyEnvironmentRenderSettings();
    if (m_Scene != nullptr) {
        SE_ASSERT(!m_Scene->Empty(), "VulkanRenderer requires at least one renderable in the 2D scene");
    }
    ValidateSceneResources();
    CreateSwapchainResources();
}

VulkanRenderer::~VulkanRenderer() {
    const bool traceShutdown = ShutdownTraceEnabled();
    const auto shutdownStartTime = std::chrono::steady_clock::now();
    auto traceStep = [&](const char* label) {
        if (!traceShutdown) {
            return;
        }
        std::cout << "[shutdown] renderer " << label << " +"
            << ElapsedShutdownMilliseconds(shutdownStartTime) << "ms"
            << std::endl;
    };

    traceStep("begin");
    WaitIdle();
    ResetReflectionCaptureShadowSnapshot();
    ReleaseReflectionCapturePersistentShadowSnapshots();
    traceStep("wait_idle");
    ShutdownTemporalUpscalerRuntime(m_Device.Handle());
    traceStep("temporal_upscaler_shutdown");

    m_GpuTimer.reset();
    m_CommandBuffer.reset();
    m_InstanceBuffer.reset();
    m_Framebuffer.reset();
    m_BloomUpsamplePipeline.reset();
    m_BloomDownsamplePipeline.reset();
    m_OverlayGraphicsPipeline.reset();
    m_InstancedGraphicsPipeline.reset();
    m_DoubleSidedInstancedGraphicsPipeline.reset();
    m_GBufferDebugPipeline.reset();
    m_TaaResolvePipeline.reset();
    m_HdrCompositePipeline.reset();
    m_WeightedTranslucencyResolvePipeline.reset();
    m_DeferredLightingPipeline.reset();
    m_LightTileCullComputePipeline.reset();
    m_LightClusterCullComputePipeline.reset();
    m_AutoExposureComputePipeline.reset();
    m_HiZBuildComputePipeline.reset();
    m_SsrTraceComputePipeline.reset();
    m_SsrTemporalComputePipeline.reset();
    m_SsrSpatialComputePipeline.reset();
    m_SsrDiagnosticsComputePipeline.reset();
    m_GBufferGraphicsPipeline.reset();
    m_DoubleSidedGBufferGraphicsPipeline.reset();
    m_ForwardResidualVelocityGraphicsPipeline.reset();
    m_DoubleSidedForwardResidualVelocityGraphicsPipeline.reset();
    m_DlssMaskGraphicsPipeline.reset();
    m_DoubleSidedDlssMaskGraphicsPipeline.reset();
    m_DepthPrefillGraphicsPipeline.reset();
    m_DoubleSidedDepthPrefillGraphicsPipeline.reset();
    m_WeightedTranslucencyGraphicsPipeline.reset();
    m_DoubleSidedWeightedTranslucencyGraphicsPipeline.reset();
    m_ForwardResidualHdrGraphicsPipeline.reset();
    m_DoubleSidedForwardResidualHdrGraphicsPipeline.reset();
    m_ForwardResidualGraphicsPipeline.reset();
    m_DoubleSidedForwardResidualGraphicsPipeline.reset();
    m_ShadowGraphicsPipeline.reset();
    m_DoubleSidedShadowGraphicsPipeline.reset();
    m_ReflectionCaptureGraphicsPipeline.reset();
    m_DoubleSidedReflectionCaptureGraphicsPipeline.reset();
    m_ReflectionCapturePipelineRenderPass = VK_NULL_HANDLE;
    m_GraphicsPipeline.reset();
    m_DoubleSidedGraphicsPipeline.reset();
    traceStep("pipelines_reset");
    m_ImGuiLayer.reset();
    traceStep("imgui_reset");
    m_DepthLoadRenderPass.reset();
    m_RenderPass.reset();
    m_GBufferFramebuffer.reset();
    m_GBufferRenderPass.reset();
    m_ForwardResidualVelocityFramebuffer.reset();
    m_ForwardResidualVelocityRenderPass.reset();
    m_DlssMaskFramebuffer.reset();
    m_DlssMaskRenderPass.reset();
    m_WeightedTranslucencyFramebuffer.reset();
    m_WeightedTranslucencyRenderPass.reset();
    m_TaaResolveFramebuffer.reset();
    m_HdrFramebuffer.reset();
    m_HdrRenderPass.reset();
    m_DirectionalShadowCascadeFramebuffer.reset();
    m_LocalShadowFramebuffer.reset();
    m_ShadowFramebuffer.reset();
    m_ReflectionCaptureLocalShadowFramebuffer.reset();
    m_ReflectionCaptureShadowFramebuffer.reset();
    m_ReflectionCaptureLocalShadowAtlas.reset();
    m_ReflectionCaptureShadowMap.reset();
    m_ShadowRenderPass.reset();
    m_LocalShadowAtlas.reset();
    m_DirectionalShadowCascadeAtlas.reset();
    m_ShadowMap.reset();
    m_BloomUpsampleFramebuffer.reset();
    m_BloomDownsampleFramebuffer.reset();
    m_BloomUpsampleRenderPass.reset();
    m_BloomDownsampleRenderPass.reset();
    m_TemporalUpscaleHdrDescriptorSets.reset();
    m_HdrDescriptorSets.reset();
    m_TemporalUpscaleBloomDescriptorSets.reset();
    m_BloomDescriptorSets.reset();
    m_WeightedTranslucencyDescriptorSets.reset();
    m_SsrReconstructionDescriptorSets.reset();
    m_HiZDescriptorSets.reset();
    m_GBufferDescriptorSets.reset();
    m_SsrDepthPyramidSampler.reset();
    m_SceneTargetSampler.reset();
    m_VisibleSkyboxSampler.reset();
    m_VisibleSkyboxTexture.reset();
    m_VisibleSkyboxFallbackTexture.reset();
    m_ColorGradingLut.reset();
    m_BloomPyramid.reset();
    m_SsrDepthPyramid.reset();
    m_SceneRenderTargets.reset();
    m_SsrReconstructionImagesInitialized = false;
    traceStep("render_targets_reset");
    if (m_IblSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device.Handle(), m_IblSampler, nullptr);
        m_IblSampler = VK_NULL_HANDLE;
    }
    m_ReflectionProbeResources.Release();
    traceStep("reflection_probe_resources_released");
    m_IblPrefilteredImage.reset();
    m_IblIrradianceImage.reset();
    m_IblBrdfImage.reset();
    m_IblPrefilteredView = VK_NULL_HANDLE;
    m_IblIrradianceView = VK_NULL_HANDLE;
    m_DepthBuffer.reset();
    m_ReflectionCaptureMaterialDescriptorSets.reset();
    m_MaterialDescriptorSets.reset();
    m_OverlayDescriptorSets.reset();
    m_DescriptorSets.reset();
    m_OverlayUniformBuffer.reset();
    m_LightBuffer.reset();
    m_LightTileDiagnosticsBuffer.reset();
    m_AutoExposureBuffer.reset();
    m_MaterialBuffer.reset();
    m_ProbeGridBuffer.reset();
    m_DirectionalShadowCascadeBuffer.reset();
    m_LocalShadowBuffer.reset();
    m_BonePaletteFallbackDescriptorSet.reset();
    m_UniformBuffer.reset();
    m_SsrReconstructionDescriptorSetLayout.reset();
    m_HiZDescriptorSetLayout.reset();
    m_MaterialDescriptorSetLayout.reset();
    m_DescriptorSetLayout.reset();
    m_Swapchain.reset();
    traceStep("swapchain_reset");
    m_SyncObjects.reset();
    traceStep("sync_objects_reset");
    traceStep("end");
}

void VulkanRenderer::DrawFrame() {
    RendererStats frameStats{};
#if !defined(NDEBUG)
    const bool ssrHoleDiagnosticsRequested =
        EnvironmentFlagEnabled("SE_SSR_HOLE_DIAGNOSTICS");
#else
    constexpr bool ssrHoleDiagnosticsRequested = false;
#endif
    ResetTransformMatrixRecalculationCount();
    const FrameClock::time_point frameStart = FrameClock::now();
    FrameClock::time_point sectionStart = frameStart;

    if (m_TemporalRenderTargetsRecreateRequested) {
        m_TemporalRenderTargetsRecreateRequested = false;
        RecreateSwapchain();
    }

    const VkFence currentFrameFence = m_SyncObjects->InFlightFence(m_CurrentFrame);
    const VkSemaphore imageAvailableSemaphore = m_SyncObjects->ImageAvailableSemaphore(m_CurrentFrame);

    vkWaitForFences(
        m_Device.Handle(),
        1,
        &currentFrameFence,
        VK_TRUE,
        std::numeric_limits<u64>::max()
    );

    u32 imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        m_Device.Handle(),
        m_Swapchain->Handle(),
        std::numeric_limits<u64>::max(),
        imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire Vulkan swapchain image");
    }

    const VkFence imageInFlightFence = m_SyncObjects->ImageInFlightFence(imageIndex);
    if (imageInFlightFence != VK_NULL_HANDLE) {
        vkWaitForFences(
            m_Device.Handle(),
            1,
            &imageInFlightFence,
            VK_TRUE,
            std::numeric_limits<u64>::max()
        );
    }
    if (m_GpuTimer != nullptr) {
        frameStats.gpu = m_GpuTimer->ReadFrameStats(imageIndex);
    }
    const FrameLightTileGpuReadbackStats lightTileGpuStats =
        ReadPreviousLightTileGpuStats(imageIndex);
    const FrameSsrGpuDiagnosticsStats ssrGpuDiagnostics =
        ReadPreviousSsrGpuDiagnostics(imageIndex);
    const FrameAutoExposureReadbackStats autoExposureStats =
        ReadPreviousAutoExposureStats(imageIndex);

    FrameClock::time_point sectionEnd = FrameClock::now();
    frameStats.cpu.waitAcquireMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    const bool hideImGuiForVisualQa =
        EnvironmentFlagEnabled("SE_VISUAL_QA_HIDE_IMGUI") ||
        EnvironmentFlagEnabled("SE_HIDE_IMGUI");
    if (!hideImGuiForVisualQa) {
        m_ImGuiLayer->BeginFrame(
            m_Scene,
            m_Camera,
            m_ImGuiScene3D,
            m_ImGuiCamera3D,
            &m_RenderResources,
            &m_LastStats,
            &m_RenderDebugSettings,
            &m_ShadowSettings,
            static_cast<u32>(m_TemporalAntialiasingMode),
            [this](u32 mode) {
                SetTemporalAntialiasingMode(
                    static_cast<RendererTemporalAntialiasingMode>(mode)
                );
            }
        );

        sectionEnd = FrameClock::now();
        frameStats.cpu.imguiMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    } else {
        sectionEnd = FrameClock::now();
        frameStats.cpu.imguiMs = 0.0f;
    }
    sectionStart = sectionEnd;

    if (m_SceneRenderTargets != nullptr) {
        const VkExtent2D desiredSceneExtent =
            ActiveInternalExtentForDisplay(m_Swapchain->Extent());
        if (ExtentsDiffer(m_SceneRenderTargets->Extent(), desiredSceneExtent)) {
            m_TemporalRenderTargetsRecreateRequested = true;
        }
    }

    ApplyShadowMapSettings();

    HandleObjectPicking();

    sectionEnd = FrameClock::now();
    frameStats.cpu.pickingMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    const VkExtent2D extent = m_Swapchain->Extent();
    const VkExtent2D sceneExtent = m_SceneRenderTargets != nullptr
        ? m_SceneRenderTargets->Extent()
        : extent;
    const f32 aspectRatio = static_cast<f32>(extent.width) / static_cast<f32>(extent.height);
    std::optional<FrameMatrices> mainFrameMatrices;
    if (m_FrameMatricesProvider) {
        mainFrameMatrices = m_FrameMatricesProvider(aspectRatio);
    } else if (m_Camera != nullptr) {
        mainFrameMatrices = FrameMatrices{
            m_Camera->ViewMatrix(),
            m_Camera->ProjectionMatrix(aspectRatio)
        };
    }
    std::optional<FrameMatrices> overlayFrameMatrices;
    if (m_OverlayCamera3D != nullptr) {
        overlayFrameMatrices = FrameMatrices{
            m_OverlayCamera3D->ViewMatrix(),
            m_OverlayCamera3D->ProjectionMatrix(aspectRatio)
        };
    }

    std::optional<Frustum> mainFrustum;
    if (m_RenderQueueBuilder && mainFrameMatrices.has_value()) {
        mainFrustum = Frustum::FromViewProjection(
            mainFrameMatrices->proj * mainFrameMatrices->view
        );
    }
    std::optional<Frustum> overlayFrustum;
    if (overlayFrameMatrices.has_value()) {
        overlayFrustum = Frustum::FromViewProjection(
            overlayFrameMatrices->proj * overlayFrameMatrices->view
        );
    }

    RenderQueueCullingStats mainCullingStats{};
    RenderQueueCullingStats overlayCullingStats{};
    RenderQueueCullingStats shadowCullingStats{};
    RenderQueueCacheStats mainCacheStats{};
    RenderQueueCacheStats overlayCacheStats{};
    const bool shadowPassEnabled = m_ShadowSettings.enabled &&
        m_ShadowSettings.strength > 0.001f;
    m_ShadowRenderQueue.Clear();
    if (m_RenderQueueBuilder) {
        m_RenderQueueBuilder(
            m_RenderQueue,
            RenderQueueContext{
                mainFrustum.has_value() ? &*mainFrustum : nullptr,
                &mainCullingStats,
                &mainCacheStats,
                shadowPassEnabled ? &m_ShadowRenderQueue : nullptr,
                shadowPassEnabled ? &shadowCullingStats : nullptr
            }
        );
    } else {
        SE_ASSERT(m_Scene != nullptr, "VulkanRenderer needs a render queue builder when no 2D scene is attached");
        m_RenderQueue.BuildFromScene2D(
            m_RenderResources,
            m_Scene->Renderables(),
            m_Scene->SelectedRenderable()
        );
    }
    if (m_OverlayScene3D != nullptr) {
        RenderQueueBuildOptions overlayBuildOptions{};
        overlayBuildOptions.frustum = overlayFrustum.has_value() ? &*overlayFrustum : nullptr;
        overlayBuildOptions.cullingStats = &overlayCullingStats;
        overlayBuildOptions.cacheStats = &overlayCacheStats;
        overlayBuildOptions.sceneIdentity = m_OverlayScene3D;
        overlayBuildOptions.sceneMembershipRevision = m_OverlayScene3D->MembershipRevision();
        overlayBuildOptions.sceneRenderRevision = m_OverlayScene3D->RenderRevision();
        overlayBuildOptions.useSceneRevisions = true;
        m_OverlayRenderQueue.BuildFromScene3D(
            m_RenderResources,
            m_OverlayScene3D->Renderables(),
            m_OverlayScene3D->SelectedRenderable(),
            overlayBuildOptions
        );
    } else {
        m_OverlayRenderQueue.Clear();
    }
    sectionEnd = FrameClock::now();
    frameStats.cpu.queueBuildMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    const std::span<const RenderCommand> mainCommands = m_RenderQueue.Commands();
    const std::span<const RenderCommand> overlayCommands = m_OverlayRenderQueue.Commands();
    const std::span<const RenderCommand> shadowCommands = ShadowRenderCommands();
    const bool shadowSamplingEnabled = shadowPassEnabled && !shadowCommands.empty();
    const bool recordTransparentAlphaReference =
        WeightedTranslucencyAlphaReferenceEnabled();
    if (m_LocalShadowCacheStates.size() != m_Swapchain->Images().size()) {
        m_LocalShadowCacheStates.assign(m_Swapchain->Images().size(), LocalShadowCacheState{});
    }
    const LocalShadowCacheState* localShadowCacheState =
        imageIndex < m_LocalShadowCacheStates.size()
            ? &m_LocalShadowCacheStates[imageIndex]
            : nullptr;
    std::vector<RenderCommand> gBufferCommands;
    std::vector<RenderCommand> weightedTranslucencyCommands;
    std::vector<RenderCommand> weightedTranslucencyVelocityCommands;
    std::vector<RenderCommand> forwardResidualCommands;
    std::vector<RenderCommand> forwardResidualVelocityCommands;
    const bool has3DMainPass =
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D ||
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3DInstanced;
    const bool temporalReconstructionAllowed =
        !DebugViewBypassesTemporalReconstruction(m_RenderDebugSettings.forwardView);
    const bool dlssModeActive =
        temporalReconstructionAllowed && TemporalDlssModeActive();
    const bool nativeTaaModeActive =
        temporalReconstructionAllowed && TemporalNativeTaaModeActive();
    const bool temporalUpscalePostSourceRequested =
        dlssModeActive || (
            !nativeTaaModeActive &&
            TemporalUpscalePostSourceRequestedFromEnvironment()
        );
    const bool suppressNativeTaaResolveForUpscaler =
        dlssModeActive || (
            !nativeTaaModeActive &&
            temporalUpscalePostSourceRequested &&
            TemporalUpscalerPluginRequestedFromEnvironment()
        );
    const bool showDeferredHdr =
        UsesDeferredHdrComposite(m_RenderDebugSettings.forwardView) ||
        temporalUpscalePostSourceRequested ||
        nativeTaaModeActive;
    const bool hdrCompositeAvailable =
        showDeferredHdr &&
        m_HdrCompositePipeline != nullptr &&
        m_HdrDescriptorSets != nullptr;
    const bool velocityTargetAllocated = m_SceneRenderTargets != nullptr;
    const bool materialAuxTargetAllocated = m_SceneRenderTargets != nullptr;
    const bool historyColorTargetAllocated = m_SceneRenderTargets != nullptr;
    const bool taaResolveConfigured =
        temporalReconstructionAllowed &&
        (
            dlssModeActive ||
            nativeTaaModeActive ||
            TaaResolveEnabledFromEnvironment()
        );
    const f32 taaHistoryWeight = TaaHistoryWeightFromEnvironment();
    const bool taaRejectionEnabled = TaaRejectionEnabledFromEnvironment();
    const bool taaNeighborhoodClampEnabled =
        TaaNeighborhoodClampEnabledFromEnvironment();
    const f32 taaVelocityRejectionThreshold =
        TaaVelocityRejectionThresholdFromEnvironment();
    const f32 taaDepthRejectionThreshold =
        TaaDepthRejectionThresholdFromEnvironment();
    const bool temporalJitterApplyRequested =
        temporalReconstructionAllowed &&
        TemporalJitterApplyEnabledForCurrentMode();
    FrameTemporalState temporalState = BuildFrameTemporalState(
        mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
        sceneExtent,
        velocityTargetAllocated,
        materialAuxTargetAllocated,
        hdrCompositeAvailable,
        historyColorTargetAllocated,
        m_TemporalHistoryColorValid,
        taaResolveConfigured,
        taaHistoryWeight,
        taaRejectionEnabled,
        taaNeighborhoodClampEnabled,
        taaVelocityRejectionThreshold,
        taaDepthRejectionThreshold,
        temporalJitterApplyRequested,
        suppressNativeTaaResolveForUpscaler
    );
    const bool ssrSceneColorHistorySourceValid =
        m_PreviousTemporalHistoryImageIndex.has_value() &&
        *m_PreviousTemporalHistoryImageIndex <
            (m_SceneRenderTargets != nullptr ? m_SceneRenderTargets->Count() : 0u);
    const u32 ssrSceneColorHistorySourceImageIndex =
        ssrSceneColorHistorySourceValid
            ? *m_PreviousTemporalHistoryImageIndex
            : imageIndex;
    const bool gBufferSceneColorHistoryDescriptorUpdated =
        m_GBufferDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SceneTargetSampler != nullptr &&
        m_GBufferDescriptorSets->UpdateSsrSceneColorHistory(
            m_Device,
            *m_SceneRenderTargets,
            *m_SceneTargetSampler,
            imageIndex,
            ssrSceneColorHistorySourceImageIndex
        );
    // SSR Temporal binding 7 is created against the current frame's HDR
    // scene-color image and must remain that way until the post-lighting
    // compute pass consumes it. Only the Deferred consumer binding (16) is
    // rebound to the previous submitted temporal history below.
    const bool ssrReconstructionCurrentHdrDescriptorBound =
        m_SsrReconstructionDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SceneTargetSampler != nullptr &&
        imageIndex < m_SsrReconstructionDescriptorSets->Count() &&
        m_SsrReconstructionDescriptorSets->Count() ==
            m_SceneRenderTargets->Count();
    const bool ssrSceneColorHistoryDescriptorUpdated =
        gBufferSceneColorHistoryDescriptorUpdated &&
        ssrReconstructionCurrentHdrDescriptorBound;
    const FrameLightSet frameLightSet = BuildFrameLightSet(mainCommands);
    PrepareReflectionProbeCaptureResources(
        imageIndex,
        frameLightSet,
        mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr
    );
    const FrameReflectionProbeSet frameReflectionProbes =
        BuildFrameReflectionProbeSet(
            mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr
        );
    const u32 selectedReflectionProbeDescriptorWrites =
        UpdateEnvironmentDescriptorSets(
            m_DescriptorSets.get(),
            &frameReflectionProbes,
            imageIndex
        ) +
        UpdateEnvironmentDescriptorSets(
            m_OverlayDescriptorSets.get(),
            &frameReflectionProbes,
            imageIndex
        );
    if (selectedReflectionProbeDescriptorWrites > 0u) {
        m_ReflectionProbeResources.SetDescriptorSetsBound(
            selectedReflectionProbeDescriptorWrites
        );
    }
    const VulkanRenderFeatureContext renderFeatureContext{
        m_ShadowSettings,
        m_RenderDebugSettings,
        has3DMainPass,
        m_DeferredLightingPipeline != nullptr && m_GBufferDescriptorSets != nullptr,
        hdrCompositeAvailable,
        frameReflectionProbes.sceneProbeCount,
        frameReflectionProbes.activeLocalProbeCount,
        frameReflectionProbes.localProbe.sceneOwned,
        m_ShadowSettings.reflectionProbeCubemapEnabled &&
            frameReflectionProbes.fallbackEnabled &&
            frameReflectionProbes.localProbe.sceneOwned &&
            frameReflectionProbes.selectedCubemapSamplingCount > 0,
        static_cast<u32>(frameReflectionProbes.captureSource),
        static_cast<u32>(frameReflectionProbes.captureFallbackReason),
        m_SsrDepthPyramid != nullptr,
        m_HiZDescriptorSets != nullptr &&
            m_SsrDepthPyramid != nullptr &&
            m_HiZDescriptorSets->Count() == m_SsrDepthPyramid->Count() &&
            m_HiZDescriptorSets->MipCount() == m_SsrDepthPyramid->MipCount(),
        m_HiZBuildComputePipeline != nullptr,
        ssrSceneColorHistoryDescriptorUpdated,
        m_SsrDepthPyramid != nullptr ? m_SsrDepthPyramid->Extent().width : 0u,
        m_SsrDepthPyramid != nullptr ? m_SsrDepthPyramid->Extent().height : 0u,
        m_SsrDepthPyramid != nullptr ? m_SsrDepthPyramid->MipCount() : 0u,
        m_SsrDepthPyramid != nullptr
            ? static_cast<u32>(m_SsrDepthPyramid->Count())
            : 0u,
        m_SsrDepthPyramid != nullptr
            ? m_SsrDepthPyramid->Format()
            : VK_FORMAT_UNDEFINED,
        m_SceneRenderTargets != nullptr &&
            m_SceneRenderTargets->Count() > 1,
        m_SsrReconstructionDescriptorSets != nullptr &&
            m_SsrReconstructionDescriptorSets->Count() ==
                (m_SceneRenderTargets != nullptr
                    ? m_SceneRenderTargets->Count()
                    : 0u),
        m_SsrTraceComputePipeline != nullptr &&
            m_SsrTemporalComputePipeline != nullptr &&
            m_SsrSpatialComputePipeline != nullptr,
        m_SceneRenderTargets != nullptr
            ? static_cast<u32>(m_SceneRenderTargets->Count())
            : 0u
    };
    const FrameMaterialSet frameMaterialSet = has3DMainPass
        ? BuildFrameMaterialSet(mainCommands)
        : FrameMaterialSet{};
    const FrameLightConstants frameLights = frameLightSet.Constants();
    const glm::mat4 lightViewProjection = LightViewProjection(shadowCommands, frameLightSet);
    const DirectionalShadowCascadeSet directionalShadowCascades =
        BuildDirectionalShadowCascades(
            shadowCommands,
            frameLightSet,
            mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
            shadowSamplingEnabled
        );
    const LocalShadowTileSet localShadowTiles = BuildLocalShadowTiles(
        frameLightSet,
        shadowCommands,
        m_LocalShadowAtlas != nullptr ? m_LocalShadowAtlas->TileCapacity() : 0u,
        localShadowCacheState
    );
    const bool useFullDirectionalShadowCasterList =
        EnvironmentFlagEnabled("SE_SHADOW_CASCADE_FULL_COMMANDS");
    const std::vector<std::vector<RenderCommand>> directionalShadowCommandLists =
        BuildDirectionalShadowCommandLists(
            shadowCommands,
            directionalShadowCascades,
            useFullDirectionalShadowCasterList
        );
    std::array<std::span<const RenderCommand>, kMaxDirectionalShadowCascades>
        directionalShadowCommandSpans{};
    const u32 directionalShadowCommandSpanCount = std::min<u32>(
        static_cast<u32>(directionalShadowCommandLists.size()),
        static_cast<u32>(directionalShadowCommandSpans.size())
    );
    for (u32 index = 0; index < directionalShadowCommandSpanCount; ++index) {
        directionalShadowCommandSpans[index] = std::span<const RenderCommand>(
            directionalShadowCommandLists[index].data(),
            directionalShadowCommandLists[index].size()
        );
    }
    const std::vector<std::vector<RenderCommand>> localShadowTileCommandLists =
        BuildLocalShadowTileCommandLists(shadowCommands, frameLightSet, localShadowTiles);
    std::array<std::span<const RenderCommand>, kMaxLocalShadowTiles>
        localShadowTileCommandSpans{};
    const u32 localShadowTileCommandSpanCount = std::min<u32>(
        static_cast<u32>(localShadowTileCommandLists.size()),
        static_cast<u32>(localShadowTileCommandSpans.size())
    );
    for (u32 index = 0; index < localShadowTileCommandSpanCount; ++index) {
        localShadowTileCommandSpans[index] = std::span<const RenderCommand>(
            localShadowTileCommandLists[index].data(),
            localShadowTileCommandLists[index].size()
        );
    }
    UpdateUniformBuffer(
        imageIndex,
        mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
        lightViewProjection,
        frameLights,
        frameReflectionProbes,
        shadowSamplingEnabled,
        &temporalState
    );
    UpdateOverlayUniformBuffer(
        imageIndex,
        overlayFrameMatrices.has_value() ? &*overlayFrameMatrices : nullptr,
        lightViewProjection,
        frameLights,
        frameReflectionProbes,
        shadowSamplingEnabled
    );
    FrameLightTileStats lightTileStats{};
    UpdateLightBuffer(
        imageIndex,
        frameLightSet,
        sceneExtent,
        mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
        &lightTileStats
    );
    UpdateMaterialBuffer(imageIndex, frameMaterialSet);
    UpdateProbeGridBuffer(imageIndex, frameStats.probeGrid);
    UpdateDirectionalShadowCascadeBuffer(
        imageIndex,
        directionalShadowCascades,
        lightViewProjection
    );
    UpdateLocalShadowBuffer(imageIndex, localShadowTiles);

    sectionEnd = FrameClock::now();
    frameStats.cpu.uniformUpdateMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    const VkSemaphore renderFinishedSemaphore = m_SyncObjects->RenderFinishedSemaphore(imageIndex);
    m_SyncObjects->MarkImageInFlight(imageIndex, currentFrameFence);
    vkResetFences(m_Device.Handle(), 1, &currentFrameFence);

    const int deferredPbrDebugView =
        DeferredPbrDebugViewIndex(m_RenderDebugSettings.forwardView);
    const int weightedTranslucencyDebugView =
        WeightedTranslucencyDebugViewIndex(m_RenderDebugSettings.forwardView);
    const int gBufferDebugView = GBufferDebugViewIndex(m_RenderDebugSettings.forwardView);
    frameStats.renderDebug.forwardView =
        static_cast<u32>(static_cast<int>(m_RenderDebugSettings.forwardView));
    frameStats.renderDebug.deferredPbrDebugView =
        static_cast<u32>(std::max(deferredPbrDebugView, 0));
    frameStats.renderDebug.usesDeferredHdrComposite =
        UsesDeferredHdrComposite(m_RenderDebugSettings.forwardView) ? 1u : 0u;
    frameStats.renderDebug.temporalReconstructionBypassed =
        temporalReconstructionAllowed ? 0u : 1u;
    frameStats.renderDebug.lightingEnergyViewEnabled =
        m_RenderDebugSettings.forwardView == ForwardDebugView::DeferredEnergyBalance
            ? 1u
            : 0u;
    const bool allowInstanceBatchCacheReuse =
        mainCacheStats.queueCacheHits > 0 &&
        mainCacheStats.queueCacheMisses == 0;
    const bool reusedInstanceBatches = BuildMainInstanceBatches(
        mainCommands,
        allowInstanceBatchCacheReuse
    );
    bool uploadedMainInstances = false;
    bool skippedMainInstanceUpload = false;
    if (m_InstanceBuffer != nullptr) {
        uploadedMainInstances = UploadMainInstancesIfNeeded(imageIndex);
        skippedMainInstanceUpload = !uploadedMainInstances;
    }
    frameStats.draw = DrawStatsForQueues(
        mainCommands,
        overlayCommands,
        shadowCommands
    );
    frameStats.bonePaletteDraw = BonePaletteDrawStatsFor(
        mainCommands,
        m_BonePaletteFallbackDescriptorSet != nullptr &&
            m_BonePaletteFallbackDescriptorSet->Ready() != 0u
    );
    frameStats.shadowCascades = ShadowCascadeStatsFor(directionalShadowCascades);
    frameStats.shadowCascades.quality = static_cast<u32>(m_ShadowSettings.quality);
    frameStats.shadowCascades.directionalReceiveEnabled =
        m_ShadowSettings.directionalShadowReceiveEnabled ? 1u : 0u;
    frameStats.shadowCascades.pcfKernelRadius =
        std::clamp<u32>(m_ShadowSettings.pcfKernelRadius, 0u, 2u);
    frameStats.shadowCascades.filterMode = static_cast<u32>(
        m_ShadowSettings.directionalFilterMode
    );
    frameStats.shadowCascades.filterSampleCount = std::clamp<u32>(
        m_ShadowSettings.directionalFilterSampleCount,
        0u,
        16u
    );
    frameStats.shadowCascades.filterKernelWidth = std::clamp<u32>(
        m_ShadowSettings.directionalFilterKernelWidth,
        0u,
        5u
    );
    frameStats.shadowCascades.filterHardwareCompareEnabled = 1u;
    frameStats.shadowCascades.filterFallbackReason =
        m_ShadowSettings.directionalFilterMode ==
                VulkanDirectionalShadowFilterMode::HardwareBoxPcf
            ? 1u
            : 0u;
    frameStats.shadowCascades.pcssStrength =
        std::clamp(m_ShadowSettings.pcssStrength, 0.0f, 1.0f);
    frameStats.shadowCascades.pcssBlockerSampleCount = std::clamp<u32>(
        m_ShadowSettings.directionalPcssBlockerSampleCount,
        0u,
        16u
    );
    frameStats.shadowCascades.pcssFilterSampleCount = std::clamp<u32>(
        m_ShadowSettings.directionalPcssFilterSampleCount,
        0u,
        16u
    );
    frameStats.shadowCascades.pcssSearchRadiusTexels = std::clamp(
        m_ShadowSettings.directionalPcssSearchRadiusTexels,
        0.0f,
        16.0f
    );
    frameStats.shadowCascades.pcssMaxPenumbraTexels = std::clamp(
        m_ShadowSettings.directionalPcssMaxPenumbraTexels,
        0.0f,
        16.0f
    );
    const bool pcssRawDepthSamplerReady =
        (m_DirectionalShadowCascadeAtlas != nullptr &&
            m_DirectionalShadowCascadeAtlas->RawDepthSampler() != VK_NULL_HANDLE) ||
        (m_ShadowMap != nullptr && m_ShadowMap->RawDepthSampler() != VK_NULL_HANDLE);
    frameStats.shadowCascades.pcssRawDepthSamplerReady =
        pcssRawDepthSamplerReady ? 1u : 0u;
    const bool pcssControlsValid =
        frameStats.shadowCascades.pcssBlockerSampleCount > 0u &&
        frameStats.shadowCascades.pcssFilterSampleCount > 0u &&
        frameStats.shadowCascades.pcssSearchRadiusTexels > 0.0f &&
        frameStats.shadowCascades.pcssMaxPenumbraTexels > 0.0f &&
        frameStats.shadowCascades.pcssLightAngularRadiusRadians > 0.0f;
    frameStats.shadowCascades.pcssEnabled =
        frameStats.shadowCascades.pcssStrength > 0.0001f &&
        pcssRawDepthSamplerReady && pcssControlsValid
            ? 1u
            : 0u;
    frameStats.shadowCascades.pcssFallbackReason =
        frameStats.shadowCascades.pcssEnabled != 0u
            ? 0u
            : (frameStats.shadowCascades.pcssStrength <= 0.0001f
                ? 1u
                : (!pcssRawDepthSamplerReady ? 2u : 3u));
    frameStats.shadowCascades.pcssGrazingFadeEnabled =
        m_ShadowSettings.directionalPcssGrazingFadeEnabled ? 1u : 0u;
    frameStats.shadowCascades.pcssGrazingFadeStart = std::clamp(
        m_ShadowSettings.directionalPcssGrazingFadeStart,
        0.0f,
        0.95f
    );
    frameStats.shadowCascades.pcssGrazingFadeEnd = std::clamp(
        m_ShadowSettings.directionalPcssGrazingFadeEnd,
        frameStats.shadowCascades.pcssGrazingFadeStart + 0.01f,
        1.0f
    );
    frameStats.shadowCascades.filterMaxDepthSamples =
        frameStats.shadowCascades.pcssEnabled != 0u
            ? frameStats.shadowCascades.pcssBlockerSampleCount +
                frameStats.shadowCascades.pcssFilterSampleCount
            : frameStats.shadowCascades.filterSampleCount;
    frameStats.shadowCascades.budgetContractVersion =
        kShadowQualityBudgetContractVersion;
    frameStats.shadowCascades.budgetDirectionalReceiverSamples =
        frameStats.shadowCascades.filterMaxDepthSamples;
    frameStats.shadowCascades.budgetPointProjectionSamples =
        LocalShadowProjectionSampleBudget(
            m_ShadowSettings.pointLocalShadowFilter,
            m_ShadowSettings.localProductionFilterEnabled
        );
    frameStats.shadowCascades.budgetSpotProjectionSamples =
        LocalShadowProjectionSampleBudget(
            m_ShadowSettings.spotLocalShadowFilter,
            m_ShadowSettings.localProductionFilterEnabled
        );
    frameStats.shadowCascades.budgetRectProjectionSamples =
        LocalShadowProjectionSampleBudget(
            m_ShadowSettings.rectLocalShadowFilter,
            m_ShadowSettings.localProductionFilterEnabled
        );
    frameStats.shadowCascades.budgetRectProjectionCount =
        RectShadowMaxSampleTileCountFor(m_ShadowSettings);
    frameStats.shadowCascades.budgetContactSamples =
        std::clamp<u32>(m_ShadowSettings.contactShadowSteps, 0u, 32u);
    frameStats.shadowCascades.budgetGenerationMaxPasses =
        (m_ShadowSettings.enabled ? 1u : 0u) +
        frameStats.shadowCascades.configuredCount +
        LocalShadowAtlasTileCapacityFor(m_ShadowSettings);
    // gpu_shadow_ms covers legacy, CSM, and local depth generation after the
    // command-buffer timestamp boundary fix. Receiver filtering stays in the
    // lighting/main timing and is represented by the sample budget above.
    frameStats.shadowCascades.budgetGpuGenerationScope = 1u;
    frameStats.shadowCascades.filterReceiverBiasExtentTexels =
        std::clamp(m_ShadowSettings.directionalFilterReceiverBiasExtentTexels, 0.0f, 4.0f);
    frameStats.shadowCascades.receiverPlaneBiasScale =
        std::clamp(m_ShadowSettings.directionalReceiverPlaneBiasScale, 0.0f, 4.0f);
    frameStats.shadowCascades.receiverPlaneBiasEnabled =
        frameStats.shadowCascades.receiverPlaneBiasScale > 0.0001f ? 1u : 0u;
    frameStats.shadowCascades.normalOffsetBiasTexels =
        std::clamp(m_ShadowSettings.directionalNormalOffsetBiasTexels, 0.0f, 4.0f);
    frameStats.shadowCascades.normalOffsetBiasEnabled =
        frameStats.shadowCascades.normalOffsetBiasTexels > 0.0001f ? 1u : 0u;
    frameStats.shadowCascades.slopeOffsetBiasTexels =
        std::clamp(m_ShadowSettings.directionalSlopeOffsetBiasTexels, 0.0f, 2.0f);
    frameStats.shadowCascades.slopeOffsetBiasEnabled =
        frameStats.shadowCascades.slopeOffsetBiasTexels > 0.0001f ? 1u : 0u;
    frameStats.shadowCascades.casterDepthBiasEnabled =
        m_ShadowSettings.casterDepthBiasEnabled ? 1u : 0u;
    frameStats.shadowCascades.casterDepthBiasConstant =
        std::clamp(m_ShadowSettings.casterDepthBiasConstant, 0.0f, 262144.0f);
    frameStats.shadowCascades.casterDepthBiasClamp =
        m_PhysicalDevice.Features().depthBiasClamp
            ? std::clamp(m_ShadowSettings.casterDepthBiasClamp, 0.0f, 0.05f)
            : 0.0f;
    frameStats.shadowCascades.casterDepthBiasSlope =
        std::clamp(m_ShadowSettings.casterDepthBiasSlope, 0.0f, 16.0f);
    frameStats.shadowCascades.blendRatio =
        std::clamp(m_ShadowSettings.cascadeBlendRatio, 0.0f, 0.25f);
    frameStats.shadowCascades.fadeRatio =
        std::clamp(m_ShadowSettings.cascadeFadeRatio, 0.0f, 0.35f);
    frameStats.shadowCascades.receiverGuardRatio =
        ShadowCascadeReceiverGuardRatio();
    frameStats.shadowCascades.contactShadowStrength =
        std::clamp(m_ShadowSettings.contactShadowStrength, 0.0f, 1.0f);
    frameStats.shadowCascades.contactShadowLength =
        std::clamp(m_ShadowSettings.contactShadowLength, 0.0f, 1.0f);
    frameStats.shadowCascades.contactShadowThickness =
        std::clamp(m_ShadowSettings.contactShadowThickness, 0.0f, 0.5f);
    frameStats.shadowCascades.contactShadowSteps =
        std::clamp<u32>(m_ShadowSettings.contactShadowSteps, 0u, 12u);
    frameStats.shadowCascades.contactShadowJitterStrength =
        std::clamp(m_ShadowSettings.contactShadowJitterStrength, 0.0f, 1.0f);
    frameStats.shadowCascades.contactShadowEdgeFadePixels =
        std::clamp(m_ShadowSettings.contactShadowEdgeFadePixels, 0.0f, 96.0f);
    m_RenderFeatures.WriteStats(
        VulkanRenderFeatureStatsContext{
            frameStats,
            renderFeatureContext
        }
    );
    frameStats.ssr.holeDiagnosticsRequested =
        ssrHoleDiagnosticsRequested ? 1u : 0u;
    frameStats.ssr.holeDiagnosticsReadbackValid =
        ssrGpuDiagnostics.valid ? 1u : 0u;
    frameStats.ssr.holeDiagnosticsPixelCount = ssrGpuDiagnostics.pixelCount;
    frameStats.ssr.holeDiagnosticsRawHitPixels = ssrGpuDiagnostics.rawHitPixels;
    frameStats.ssr.holeDiagnosticsRawHighConfidencePixels =
        ssrGpuDiagnostics.rawHighConfidencePixels;
    frameStats.ssr.holeDiagnosticsTemporalValidPixels =
        ssrGpuDiagnostics.temporalValidPixels;
    frameStats.ssr.holeDiagnosticsResolvedValidPixels =
        ssrGpuDiagnostics.resolvedValidPixels;
    frameStats.ssr.holeDiagnosticsIsolatedRawHitPixels =
        ssrGpuDiagnostics.isolatedRawHitPixels;
    frameStats.ssr.holeDiagnosticsCenterMissNeighborHitPixels =
        ssrGpuDiagnostics.centerMissNeighborHitPixels;
    frameStats.ssr.holeDiagnosticsResolvedHolePixels =
        ssrGpuDiagnostics.resolvedHolePixels;
    frameStats.ssr.holeDiagnosticsRawHitTemporalRejectedPixels =
        ssrGpuDiagnostics.rawHitTemporalRejectedPixels;
    frameStats.ssr.holeDiagnosticsRawHitSpatialRejectedPixels =
        ssrGpuDiagnostics.rawHitSpatialRejectedPixels;
    frameStats.ssr.holeDiagnosticsTemporalMissCarriedPixels =
        ssrGpuDiagnostics.temporalMissCarriedPixels;
    frameStats.ssr.fallbackBlendResolvedPixels =
        ssrGpuDiagnostics.fallbackBlendResolvedPixels;
    frameStats.ssr.fallbackBlendPartialPixels =
        ssrGpuDiagnostics.fallbackBlendPartialPixels;
    frameStats.ssr.fallbackBlendHighTrustPixels =
        ssrGpuDiagnostics.fallbackBlendHighTrustPixels;
    if (ssrGpuDiagnostics.fallbackBlendResolvedPixels > 0u) {
        frameStats.ssr.fallbackBlendAveragePermille = static_cast<u32>(
            (
                static_cast<u64>(ssrGpuDiagnostics.fallbackBlendWeightSum64) *
                    1000ull +
                static_cast<u64>(ssrGpuDiagnostics.fallbackBlendResolvedPixels) *
                    32ull
            ) /
            (
                static_cast<u64>(ssrGpuDiagnostics.fallbackBlendResolvedPixels) *
                64ull
            )
        );
    }
    frameStats.ssr.holeDiagnosticsActive =
        ssrHoleDiagnosticsRequested &&
        frameStats.ssr.reconstructionActive > 0u &&
        m_SsrDiagnosticsComputePipeline != nullptr
            ? 1u
            : 0u;
    frameStats.ssr.holeDiagnosticsContractVersion =
        frameStats.ssr.holeDiagnosticsActive > 0u
            ? ssrGpuDiagnostics.contractVersion
            : 0u;
    WriteFrameReflectionProbeStats(
        frameReflectionProbes,
        frameStats.reflectionProbe
    );
    const bool recordAutoExposureCompute =
        frameStats.postProcess.autoExposureEnabled > 0 &&
        hdrCompositeAvailable &&
        m_AutoExposureComputePipeline != nullptr &&
        m_AutoExposureBuffer != nullptr &&
        m_HdrDescriptorSets != nullptr;
    frameStats.postProcess.autoExposureHistogramEnabled =
        recordAutoExposureCompute ? 1u : 0u;
    frameStats.postProcess.autoExposureHistoryValid =
        autoExposureStats.valid ? 1u : 0u;
    frameStats.postProcess.autoExposureGpuExposure =
        autoExposureStats.exposure;
    frameStats.postProcess.autoExposureGpuTargetExposure =
        autoExposureStats.targetExposure;
    frameStats.postProcess.autoExposureGpuAverageLuminance =
        autoExposureStats.averageLuminance;
    frameStats.postProcess.autoExposureFallbacks =
        frameStats.postProcess.autoExposureEnabled > 0 &&
            !recordAutoExposureCompute
            ? 1u
            : 0u;
    const bool recordBloomPyramid =
        frameStats.postProcess.bloomEnabled > 0 &&
        hdrCompositeAvailable &&
        m_BloomPyramid != nullptr &&
        m_BloomDescriptorSets != nullptr &&
        m_BloomDownsampleRenderPass != nullptr &&
        m_BloomUpsampleRenderPass != nullptr &&
        m_BloomDownsampleFramebuffer != nullptr &&
        m_BloomUpsampleFramebuffer != nullptr &&
        m_BloomDownsamplePipeline != nullptr &&
        m_BloomUpsamplePipeline != nullptr;
    frameStats.postProcess.bloomPyramidEnabled =
        recordBloomPyramid ? 1u : 0u;
    frameStats.postProcess.bloomPyramidMipCount =
        m_BloomPyramid != nullptr ? m_BloomPyramid->MipCount() : 0u;
    frameStats.postProcess.bloomPyramidFallbacks =
        frameStats.postProcess.bloomEnabled > 0 && !recordBloomPyramid ? 1u : 0u;
    const bool recordTemporalHistoryColorCopy =
        hdrCompositeAvailable && m_SceneRenderTargets != nullptr;
    const bool colorGradingLutReady =
        m_ColorGradingLut != nullptr && m_ColorGradingLut->Uploaded();
    frameStats.postProcess.colorGradingLutEnabled =
        frameStats.postProcess.colorGradingEnabled > 0 &&
            colorGradingLutReady &&
            frameStats.postProcess.colorGradingLutStrength > 0.0001f
            ? 1u
            : 0u;
    frameStats.postProcess.colorGradingLutSize =
        colorGradingLutReady ? m_ColorGradingLut->LutSize() : 0u;
    frameStats.postProcess.colorGradingLutFallbacks =
        frameStats.postProcess.colorGradingEnabled > 0 && !colorGradingLutReady ? 1u : 0u;
    const bool iblBrdfReady =
        m_IblBrdfImage != nullptr &&
        m_IblBrdfImage->View() != VK_NULL_HANDLE &&
        m_IblSampler != VK_NULL_HANDLE;
    const bool iblIrradianceReady =
        m_IblIrradianceImage != nullptr &&
        m_IblIrradianceView != VK_NULL_HANDLE &&
        m_IblSampler != VK_NULL_HANDLE;
    const bool iblPrefilteredReady =
        m_IblPrefilteredImage != nullptr &&
        m_IblPrefilteredView != VK_NULL_HANDLE &&
        m_IblSampler != VK_NULL_HANDLE;
    const bool iblReady =
        iblBrdfReady && iblIrradianceReady && iblPrefilteredReady;
    frameStats.ibl.quality =
        static_cast<u32>(m_IblGenerationInfo.quality);
    frameStats.ibl.requestedSource =
        static_cast<u32>(m_IblGenerationInfo.requestedSource);
    frameStats.ibl.actualSource =
        static_cast<u32>(m_IblGenerationInfo.actualSource);
    frameStats.ibl.sourceFallbackReason =
        static_cast<u32>(m_IblGenerationInfo.sourceFallbackReason);
    frameStats.ibl.cachePolicy =
        static_cast<u32>(m_IblGenerationInfo.cachePolicy);
    frameStats.ibl.cacheFallbackReason =
        static_cast<u32>(m_IblGenerationInfo.cacheFallbackReason);
    frameStats.ibl.cacheHit = m_IblGenerationInfo.cacheHit;
    frameStats.ibl.runtimeGenerated = m_IblGenerationInfo.runtimeGenerated;
    frameStats.ibl.sourceAssetSpecified =
        m_IblGenerationInfo.sourceAssetSpecified;
    frameStats.ibl.sourceAssetFound = m_IblGenerationInfo.sourceAssetFound;
    frameStats.ibl.sourceSignature = m_IblGenerationInfo.sourceSignature;
    frameStats.ibl.brdfLutAllocated = iblBrdfReady ? 1u : 0u;
    frameStats.ibl.brdfLutSize =
        iblBrdfReady ? m_IblBrdfImage->Extent().width : 0u;
    frameStats.ibl.brdfLutFormat =
        iblBrdfReady ? m_IblBrdfImage->Format() : VK_FORMAT_UNDEFINED;
    frameStats.ibl.irradianceMapAllocated = iblIrradianceReady ? 1u : 0u;
    frameStats.ibl.irradianceFaceSize =
        iblIrradianceReady ? m_IblIrradianceImage->Extent().width : 0u;
    frameStats.ibl.irradianceFormat =
        iblIrradianceReady ? m_IblIrradianceImage->Format() : VK_FORMAT_UNDEFINED;
    frameStats.ibl.prefilteredMapAllocated = iblPrefilteredReady ? 1u : 0u;
    frameStats.ibl.prefilteredFaceSize =
        iblPrefilteredReady ? m_IblPrefilteredImage->Extent().width : 0u;
    frameStats.ibl.prefilteredMipCount =
        iblPrefilteredReady ? m_IblPrefilteredImage->MipLevels() : 0u;
    frameStats.ibl.prefilteredFormat =
        iblPrefilteredReady ? m_IblPrefilteredImage->Format() : VK_FORMAT_UNDEFINED;
    frameStats.ibl.descriptorSetsBound =
        iblReady && m_DescriptorSets != nullptr
            ? static_cast<u32>(m_DescriptorSets->Count())
            : 0u;
    frameStats.ibl.shaderIntegrationEnabled =
        iblReady && has3DMainPass ? 1u : 0u;
    i32 selectedCapturedSceneProbeSceneIndex = -1;
    for (u32 index = 0; index < frameReflectionProbes.selectedProbeCount; ++index) {
        const RendererReflectionProbe& probe =
            frameReflectionProbes.selectedProbes[index];
        if (probe.captureSource ==
            RendererReflectionProbeCaptureSource::CapturedScene) {
            selectedCapturedSceneProbeSceneIndex = probe.sceneIndex;
            break;
        }
    }
    const bool selectedCapturedSceneProbe =
        selectedCapturedSceneProbeSceneIndex >= 0;
    const bool capturedSceneCubemapReady = selectedCapturedSceneProbe &&
        m_ReflectionProbeResources.CapturedSceneReady(
            selectedCapturedSceneProbeSceneIndex,
            m_IblSampler
        );
    const CapturedSceneCaptureAudit& capturedSceneAudit =
        m_ReflectionProbeResources.CapturedSceneAudit(
            selectedCapturedSceneProbeSceneIndex
        );
    frameStats.reflectionProbe.capturedSceneCaptureBackend =
        static_cast<u32>(capturedSceneAudit.backend);
    frameStats.reflectionProbe.capturedSceneFaceCount =
        capturedSceneAudit.faceCount;
    frameStats.reflectionProbe.capturedSceneFacesRendered =
        capturedSceneAudit.facesRendered;
    frameStats.reflectionProbe.capturedSceneFacesPending =
        capturedSceneAudit.facesPending;
    frameStats.reflectionProbe.capturedSceneCapturePassCount =
        capturedSceneAudit.capturePassCount;
    frameStats.reflectionProbe.capturedSceneCaptureDrawCount =
        capturedSceneAudit.captureDrawCount;
    frameStats.reflectionProbe.capturedSceneCaptureVisibleCount =
        capturedSceneAudit.captureVisibleCount;
    frameStats.reflectionProbe.capturedSceneCaptureCulledCount =
        capturedSceneAudit.captureCulledCount;
    frameStats.reflectionProbe.capturedSceneCaptureFaceOrientationMask =
        capturedSceneAudit.captureFaceOrientationMask;
    frameStats.reflectionProbe.capturedSceneMipGenerationCount =
        capturedSceneAudit.mipGenerationCount;
    frameStats.reflectionProbe.capturedSceneSourceMipGenerationCount =
        capturedSceneAudit.sourceMipGenerationCount;
    frameStats.reflectionProbe.capturedSceneSourceMipCount =
        capturedSceneAudit.sourceMipCount;
    frameStats.reflectionProbe.capturedSceneSourceMipMemoryBytes =
        capturedSceneAudit.sourceMipMemoryBytes;
    frameStats.reflectionProbe.capturedSceneSourceMipChainReady =
        capturedSceneAudit.sourceMipChainReady ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneGgxPrefilterSourceImageSeparated =
        capturedSceneAudit.ggxPrefilterSourceImageSeparated ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneGgxPrefilterPdfLodEnabled =
        capturedSceneAudit.ggxPrefilterPdfLodEnabled ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneGgxPrefilterDispatchCount =
        capturedSceneAudit.ggxPrefilterDispatchCount;
    frameStats.reflectionProbe.capturedSceneGgxPrefilterSampleCount =
        capturedSceneAudit.ggxPrefilterSampleCount;
    frameStats.reflectionProbe.capturedSceneGgxPrefilterQuality =
        capturedSceneAudit.ggxPrefilterQuality;
    frameStats.reflectionProbe.capturedSceneDiffuseIrradianceDispatchCount =
        capturedSceneAudit.diffuseIrradianceDispatchCount;
    frameStats.reflectionProbe.capturedSceneDiffuseIrradianceSampleCount =
        capturedSceneAudit.diffuseIrradianceSampleCount;
    frameStats.reflectionProbe.capturedSceneDiffuseIrradianceFaceSize =
        capturedSceneAudit.diffuseIrradianceFaceSize;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowRequested =
        capturedSceneAudit.directionalShadowRequested ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowReady =
        capturedSceneAudit.directionalShadowReady ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowPassCount =
        capturedSceneAudit.directionalShadowPassCount;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowDrawCount =
        capturedSceneAudit.directionalShadowDrawCount;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowCasterCount =
        capturedSceneAudit.directionalShadowCasterCount;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowMapSize =
        capturedSceneAudit.directionalShadowMapSize;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowFaceMask =
        capturedSceneAudit.directionalShadowFaceMask;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowCameraIndependent =
        capturedSceneAudit.directionalShadowCameraIndependent ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowLocalTilesSuppressed =
        capturedSceneAudit.directionalShadowLocalTilesSuppressed ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneDirectionalShadowProbeSceneIndex =
        capturedSceneAudit.directionalShadowProbeSceneIndex;
    frameStats.reflectionProbe.capturedSceneLocalShadowRequested =
        capturedSceneAudit.localShadowRequested ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneLocalShadowReady =
        capturedSceneAudit.localShadowReady ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneLocalShadowPassCount =
        capturedSceneAudit.localShadowPassCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowDrawCount =
        capturedSceneAudit.localShadowDrawCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowCasterCount =
        capturedSceneAudit.localShadowCasterCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowTileCount =
        capturedSceneAudit.localShadowTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowPointFaceTileCount =
        capturedSceneAudit.localShadowPointFaceTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowSpotTileCount =
        capturedSceneAudit.localShadowSpotTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowRectTileCount =
        capturedSceneAudit.localShadowRectTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowRequestedTileCount =
        capturedSceneAudit.localShadowRequestedTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowDroppedTileCount =
        capturedSceneAudit.localShadowDroppedTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowRectRequestedTileCount =
        capturedSceneAudit.localShadowRectRequestedTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowRectMaximumTileCount =
        capturedSceneAudit.localShadowRectMaximumTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowRectExtraSampleTileCount =
        capturedSceneAudit.localShadowRectExtraSampleTileCount;
    frameStats.reflectionProbe
        .capturedSceneLocalShadowRectBudgetLimitedSampleTileCount =
        capturedSceneAudit.localShadowRectBudgetLimitedSampleTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowRectDroppedTileCount =
        capturedSceneAudit.localShadowRectDroppedTileCount;
    frameStats.reflectionProbe.capturedSceneLocalShadowMapTileSize =
        capturedSceneAudit.localShadowMapTileSize;
    frameStats.reflectionProbe.capturedSceneLocalShadowFaceMask =
        capturedSceneAudit.localShadowFaceMask;
    frameStats.reflectionProbe.capturedSceneLocalShadowSupportedKindMask =
        capturedSceneAudit.localShadowSupportedKindMask;
    frameStats.reflectionProbe.capturedSceneLocalShadowSuppressedKindMask =
        capturedSceneAudit.localShadowSuppressedKindMask;
    frameStats.reflectionProbe.capturedSceneLocalShadowCameraIndependent =
        capturedSceneAudit.localShadowCameraIndependent ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneLocalShadowProbeSceneIndex =
        capturedSceneAudit.localShadowProbeSceneIndex;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotBuildCount =
        capturedSceneAudit.shadowSnapshotBuildCount;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotReuseFaceCount =
        capturedSceneAudit.shadowSnapshotReuseFaceCount;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotSavedDirectionalPassCount =
        capturedSceneAudit.shadowSnapshotSavedDirectionalPassCount;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotSavedLocalTilePassCount =
        capturedSceneAudit.shadowSnapshotSavedLocalTilePassCount;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotSavedLocalDrawCount =
        capturedSceneAudit.shadowSnapshotSavedLocalDrawCount;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotBuildFaceMask =
        capturedSceneAudit.shadowSnapshotBuildFaceMask;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotReuseFaceMask =
        capturedSceneAudit.shadowSnapshotReuseFaceMask;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotProbeSceneIndex =
        capturedSceneAudit.shadowSnapshotProbeSceneIndex;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotPersistentCacheSlot =
        capturedSceneAudit.shadowSnapshotPersistentCacheSlot;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotPersistentHitCount =
        capturedSceneAudit.shadowSnapshotPersistentHitCount;
    frameStats.reflectionProbe
        .capturedSceneShadowSnapshotPersistentCacheResourceCount =
        capturedSceneAudit.shadowSnapshotPersistentCacheResourceCount;
    frameStats.reflectionProbe
        .capturedSceneShadowSnapshotPersistentCacheEvictionCount =
        capturedSceneAudit.shadowSnapshotPersistentCacheEvictionCount;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotInputSignature =
        capturedSceneAudit.shadowSnapshotInputSignature;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotReady =
        capturedSceneAudit.shadowSnapshotReady ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotCameraIndependent =
        capturedSceneAudit.shadowSnapshotCameraIndependent ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotEnabled =
        capturedSceneAudit.shadowSnapshotEnabled ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotFallbackActive =
        capturedSceneAudit.shadowSnapshotFallbackActive ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotPersistentEnabled =
        capturedSceneAudit.shadowSnapshotPersistentEnabled ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneShadowSnapshotPersistentHit =
        capturedSceneAudit.shadowSnapshotPersistentHit ? 1u : 0u;
    frameStats.reflectionProbe.capturedScenePersistentShadowCacheCapacity =
        static_cast<u32>(m_ReflectionCapturePersistentShadowSnapshots.size());
    frameStats.reflectionProbe.capturedScenePersistentShadowCacheResourceCount =
        ReflectionCapturePersistentShadowSnapshotCount();
    frameStats.reflectionProbe.capturedScenePersistentShadowCacheEvictionCount =
        m_ReflectionCapturePersistentShadowSnapshotEvictionCount;
    for (std::size_t index = 0;
         index < m_ReflectionCapturePersistentShadowSnapshots.size();
         ++index) {
        const ReflectionCapturePersistentShadowSnapshot& snapshot =
            m_ReflectionCapturePersistentShadowSnapshots[index];
        const bool resourceReady = snapshot.directionalShadowMap != nullptr &&
            snapshot.localShadowAtlas != nullptr &&
            snapshot.materialDescriptorSets != nullptr;
        frameStats.reflectionProbe
            .capturedScenePersistentShadowCacheProbeSceneIndices[index] =
            resourceReady ? snapshot.snapshot.probeSceneIndex : -1;
        frameStats.reflectionProbe
            .capturedScenePersistentShadowCacheInputSignatures[index] =
            resourceReady ? snapshot.snapshot.inputSignature : 0u;
    }
    frameStats.reflectionProbe.capturedSceneLastCapturedFace =
        capturedSceneAudit.lastCapturedFace;
    frameStats.reflectionProbe.capturedSceneRasterizedGeometry =
        capturedSceneAudit.rasterizedGeometry ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneGpuResourcesAllocated =
        capturedSceneAudit.gpuResourcesAllocated ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneGpuCaptureInProgress =
        capturedSceneAudit.gpuCaptureInProgress ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneCaptureFaceOrientationValid =
        capturedSceneAudit.captureFaceOrientationValid ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneMipChainReady =
        capturedSceneAudit.mipChainReady ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneGgxPrefilterReady =
        capturedSceneAudit.ggxPrefilterReady ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneGgxPrefilterFallbackActive =
        capturedSceneAudit.ggxPrefilterFallbackActive ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneDiffuseIrradianceReady =
        capturedSceneAudit.diffuseIrradianceReady ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneProbeSceneIndex =
        capturedSceneAudit.probeSceneIndex;
    frameStats.reflectionProbe.capturedSceneProbeResourceCount =
        m_ReflectionProbeResources.CapturedSceneProbeResourceCount();
    frameStats.reflectionProbe.capturedSceneReadyProbeCount =
        m_ReflectionProbeResources.CapturedSceneReadyProbeCount(m_IblSampler);
    frameStats.reflectionProbe.capturedSceneInFlightProbeCount =
        m_ReflectionProbeResources.CapturedSceneInFlightProbeCount();
    frameStats.reflectionProbe.capturedSceneDistinctActiveViewCount =
        m_ReflectionProbeResources.CapturedSceneDistinctActiveViewCount(m_IblSampler);
    frameStats.reflectionProbe.capturedSceneDiffuseIrradianceReadyProbeCount =
        m_ReflectionProbeResources.CapturedSceneDiffuseIrradianceReadyProbeCount(
            m_IblSampler
        );
    frameStats.reflectionProbe
        .capturedSceneDistinctActiveDiffuseIrradianceViewCount =
        m_ReflectionProbeResources
            .CapturedSceneDistinctActiveDiffuseIrradianceViewCount(m_IblSampler);
    std::array<VkImageView, kMaxFrameReflectionProbes>
        selectedCapturedSceneDescriptorViews{};
    std::array<VkImageView, kMaxFrameReflectionProbes>
        selectedCapturedSceneDiffuseIrradianceDescriptorViews{};
    for (u32 index = 0; index < frameReflectionProbes.selectedProbeCount; ++index) {
        const RendererReflectionProbe& selectedProbe =
            frameReflectionProbes.selectedProbes[index];
        if (selectedProbe.captureSource ==
            RendererReflectionProbeCaptureSource::CapturedScene) {
            selectedCapturedSceneDescriptorViews[index] =
                m_ReflectionProbeResources.CapturedSceneDescriptorViewFor(
                    selectedProbe.sceneIndex,
                    m_IblPrefilteredView,
                    m_IblSampler
                );
            selectedCapturedSceneDiffuseIrradianceDescriptorViews[index] =
                m_ReflectionProbeResources
                    .CapturedSceneDiffuseIrradianceDescriptorViewFor(
                        selectedProbe.sceneIndex,
                        m_IblIrradianceView,
                        m_IblSampler
                    );
        }
        if (selectedProbe.captureSource ==
                RendererReflectionProbeCaptureSource::CapturedScene &&
            frameReflectionProbes.selectedCaptureResourceReady[index] &&
            m_ReflectionProbeResources.CapturedSceneDescriptorMatchesProbe(
                selectedProbe.sceneIndex,
                selectedCapturedSceneDescriptorViews[index],
                m_IblSampler
            )) {
            frameStats.reflectionProbe.selectedCapturedSceneMapMatchesActiveMask |=
                1u << index;
        }
        if (selectedProbe.captureSource ==
                RendererReflectionProbeCaptureSource::CapturedScene &&
            frameReflectionProbes
                .selectedCapturedSceneDiffuseIrradianceReady[index] &&
            m_ReflectionProbeResources
                .CapturedSceneDiffuseIrradianceDescriptorMatchesProbe(
                    selectedProbe.sceneIndex,
                    selectedCapturedSceneDiffuseIrradianceDescriptorViews[index],
                    m_IblSampler
                )) {
            frameStats.reflectionProbe
                .selectedCapturedSceneDiffuseIrradianceMapMatchesActiveMask |=
                1u << index;
        }
    }
    for (u32 left = 0; left < frameReflectionProbes.selectedProbeCount; ++left) {
        const RendererReflectionProbe& leftProbe =
            frameReflectionProbes.selectedProbes[left];
        if (leftProbe.captureSource !=
                RendererReflectionProbeCaptureSource::CapturedScene ||
            !frameReflectionProbes.selectedCaptureResourceReady[left]) {
            continue;
        }
        for (u32 right = 0; right < left; ++right) {
            const RendererReflectionProbe& rightProbe =
                frameReflectionProbes.selectedProbes[right];
            if (rightProbe.captureSource ==
                    RendererReflectionProbeCaptureSource::CapturedScene &&
                frameReflectionProbes.selectedCaptureResourceReady[right] &&
                leftProbe.sceneIndex != rightProbe.sceneIndex &&
                selectedCapturedSceneDescriptorViews[left] ==
                    selectedCapturedSceneDescriptorViews[right]) {
                frameStats.reflectionProbe.selectedCapturedSceneDuplicateActiveViewMask |=
                    (1u << left) | (1u << right);
            }
            if (rightProbe.captureSource ==
                    RendererReflectionProbeCaptureSource::CapturedScene &&
                frameReflectionProbes
                    .selectedCapturedSceneDiffuseIrradianceReady[left] &&
                frameReflectionProbes
                    .selectedCapturedSceneDiffuseIrradianceReady[right] &&
                leftProbe.sceneIndex != rightProbe.sceneIndex &&
                selectedCapturedSceneDiffuseIrradianceDescriptorViews[left] ==
                    selectedCapturedSceneDiffuseIrradianceDescriptorViews[right]) {
                frameStats.reflectionProbe
                    .selectedCapturedSceneDiffuseIrradianceDuplicateActiveViewMask |=
                    (1u << left) | (1u << right);
            }
        }
    }
    frameStats.reflectionProbe.capturedSceneUploadCount =
        m_ReflectionProbeResources.CapturedSceneUploadCount();
    frameStats.reflectionProbe.capturedSceneRefreshCheckCount =
        m_ReflectionProbeResources.CapturedSceneRefreshCheckCount();
    frameStats.reflectionProbe.capturedSceneRefreshPerformed =
        capturedSceneAudit.refreshPerformed ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneRefreshReason =
        static_cast<u32>(capturedSceneAudit.refreshReason);
    frameStats.reflectionProbe.capturedSceneLastRefreshReason =
        static_cast<u32>(capturedSceneAudit.lastRefreshReason);
    frameStats.reflectionProbe.capturedSceneDirtyMask =
        capturedSceneAudit.dirtyMask;
    frameStats.reflectionProbe.capturedSceneActiveSignature =
        m_ReflectionProbeResources.CapturedSceneSignature();
    frameStats.reflectionProbe.capturedSceneRequestedSignature =
        capturedSceneAudit.captureSignature;
    frameStats.reflectionProbe.capturedSceneRadianceSignature =
        capturedSceneAudit.radianceSignature;
    frameStats.reflectionProbe.capturedSceneMembershipRevision =
        capturedSceneAudit.membershipRevision;
    frameStats.reflectionProbe.capturedSceneLightRevision =
        capturedSceneAudit.lightRevision;
    frameStats.reflectionProbe.capturedSceneRenderRevision =
        capturedSceneAudit.renderRevision;
    frameStats.reflectionProbe.capturedSceneSchedulerFrame =
        capturedSceneAudit.schedulerFrame;
    frameStats.reflectionProbe.capturedSceneLastRefreshCompletedFrame =
        capturedSceneAudit.lastRefreshCompletedFrame;
    frameStats.reflectionProbe.capturedSceneLocalLightSignature =
        capturedSceneAudit.localLightSignature;
    frameStats.reflectionProbe.capturedSceneGeometrySignature =
        capturedSceneAudit.geometrySignature;
    frameStats.reflectionProbe.capturedSceneAffectedLocalLightCount =
        capturedSceneAudit.affectedLocalLightCount;
    frameStats.reflectionProbe.capturedSceneAffectedRenderableCount =
        capturedSceneAudit.affectedRenderableCount;
    frameStats.reflectionProbe.capturedSceneLocalLightIdentityMask =
        capturedSceneAudit.localLightIdentityMask;
    frameStats.reflectionProbe.capturedSceneGeometryIdentityMask =
        capturedSceneAudit.geometryIdentityMask;
    frameStats.reflectionProbe.capturedSceneLocalLightRegionMask =
        capturedSceneAudit.localLightRegionMask;
    frameStats.reflectionProbe.capturedSceneGeometryRegionMask =
        capturedSceneAudit.geometryRegionMask;
    frameStats.reflectionProbe.capturedSceneDirtyLocalLightCount =
        capturedSceneAudit.dirtyLocalLightCount;
    frameStats.reflectionProbe.capturedSceneDirtyRenderableCount =
        capturedSceneAudit.dirtyRenderableCount;
    frameStats.reflectionProbe.capturedSceneRefreshPriority =
        capturedSceneAudit.refreshPriority;
    frameStats.reflectionProbe.capturedSceneMinimumRefreshIntervalFrames =
        capturedSceneAudit.minimumRefreshIntervalFrames;
    frameStats.reflectionProbe.capturedSceneRefreshDeferredCount =
        capturedSceneAudit.refreshDeferredCount;
    frameStats.reflectionProbe.capturedSceneSelectiveInvalidationEnabled =
        capturedSceneAudit.selectiveInvalidationEnabled ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneRefreshDeferredByBudget =
        capturedSceneAudit.refreshDeferredByBudget ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneLocalLightDirty =
        capturedSceneAudit.localLightDirty ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneGeometryDirty =
        capturedSceneAudit.geometryDirty ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneLocalityIgnoredLightRevision =
        capturedSceneAudit.localityIgnoredLightRevision ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneLocalityIgnoredGeometryRevision =
        capturedSceneAudit.localityIgnoredGeometryRevision ? 1u : 0u;
    frameStats.reflectionProbe.capturedSceneLocalityIgnoredLightRevisionCount =
        m_ReflectionProbeResources.CapturedSceneLocalityIgnoredLightRevisionCount();
    frameStats.reflectionProbe.capturedSceneLocalityIgnoredGeometryRevisionCount =
        m_ReflectionProbeResources.CapturedSceneLocalityIgnoredGeometryRevisionCount();
    frameStats.reflectionProbe.capturedSceneDirtyLocalLightProbeCount =
        m_ReflectionProbeResources.CapturedSceneDirtyLocalLightProbeCount();
    frameStats.reflectionProbe.capturedSceneDirtyGeometryProbeCount =
        m_ReflectionProbeResources.CapturedSceneDirtyGeometryProbeCount();
    const bool reflectionProbeCubemapReady = selectedCapturedSceneProbe
        ? capturedSceneCubemapReady
        : LocalReflectionProbeCubemapReady();
    frameStats.reflectionProbe.localCubemapAllocated =
        reflectionProbeCubemapReady ? 1u : 0u;
    frameStats.reflectionProbe.localCubemapFaceSize =
        selectedCapturedSceneProbe && capturedSceneCubemapReady
            ? m_ReflectionProbeResources.CapturedSceneFaceSize(
                selectedCapturedSceneProbeSceneIndex
            )
        : reflectionProbeCubemapReady
            ? m_ReflectionProbeResources.FaceSize()
            : 0u;
    frameStats.reflectionProbe.localCubemapMipCount =
        selectedCapturedSceneProbe && capturedSceneCubemapReady
            ? m_ReflectionProbeResources.CapturedSceneMipCount(
                selectedCapturedSceneProbeSceneIndex
            )
        : reflectionProbeCubemapReady
            ? m_ReflectionProbeResources.MipCount()
            : 0u;
    frameStats.reflectionProbe.localCubemapFormat =
        selectedCapturedSceneProbe && capturedSceneCubemapReady
            ? m_ReflectionProbeResources.CapturedSceneFormat(
                selectedCapturedSceneProbeSceneIndex
            )
        : reflectionProbeCubemapReady
            ? m_ReflectionProbeResources.Format()
            : VK_FORMAT_UNDEFINED;
    frameStats.reflectionProbe.localCubemapDescriptorSetsBound =
        reflectionProbeCubemapReady
            ? m_ReflectionProbeResources.DescriptorSetsBound()
            : 0u;
    frameStats.reflectionProbe.localCubemapShaderSamplingEnabled =
        m_ShadowSettings.reflectionProbeCubemapEnabled &&
            frameStats.reflectionProbe.selectedCubemapSamplingCount > 0 &&
            frameStats.reflectionProbe.localEnabled > 0 &&
            frameStats.reflectionProbe.localSceneOwned > 0 &&
            has3DMainPass
            ? 1u
            : 0u;
    frameStats.reflectionProbe.localCubemapSourceType =
        frameStats.reflectionProbe.captureSourceType;
    frameStats.reflectionProbe.authoredCubemapLoadedCount =
        m_ReflectionProbeResources.AuthoredCubemapLoadedCount();
    frameStats.reflectionProbe.authoredCubemapMissingCount =
        m_ReflectionProbeResources.AuthoredCubemapMissingCount();
    frameStats.reflectionProbe.authoredCubemapLoadFailedCount =
        m_ReflectionProbeResources.AuthoredCubemapLoadFailedCount();
    frameStats.reflectionProbe.authoredCubemapUploadCount =
        m_ReflectionProbeResources.AuthoredCubemapUploadCount();
    frameStats.reflectionProbe.authoredCubemapSixFaceLoadedCount =
        m_ReflectionProbeResources.AuthoredCubemapSixFaceLoadedCount();
    frameStats.reflectionProbe.authoredCubemapEquirectangularLoadedCount =
        m_ReflectionProbeResources.AuthoredCubemapEquirectangularLoadedCount();
    frameStats.reflectionProbe.authoredCubemapEquirectangularConversionCount =
        m_ReflectionProbeResources.AuthoredCubemapEquirectangularConversionCount();
    frameStats.reflectionProbe.authoredCubemapHdrLoadedCount =
        m_ReflectionProbeResources.AuthoredCubemapHdrLoadedCount();
    frameStats.reflectionProbe.authoredCubemapPrefilteredLoadedCount =
        m_ReflectionProbeResources.AuthoredCubemapPrefilteredLoadedCount();
    frameStats.reflectionProbe.authoredCubemapPrefilteredUploadCount =
        m_ReflectionProbeResources.AuthoredCubemapPrefilteredUploadCount();
    frameStats.reflectionProbe.authoredCubemapCacheHitCount =
        m_ReflectionProbeResources.AuthoredCubemapCacheHitCount();
    frameStats.reflectionProbe.authoredCubemapReloadCount =
        m_ReflectionProbeResources.AuthoredCubemapReloadCount();
    frameStats.reflectionProbe.authoredCubemapRefreshCheckCount =
        m_ReflectionProbeResources.AuthoredCubemapRefreshCheckCount();
    frameStats.reflectionProbe.authoredCubemapIrradianceReadyCount =
        m_ReflectionProbeResources.AuthoredCubemapIrradianceReadyCount();
    frameStats.reflectionProbe.authoredCubemapDiffuseLobesReadyCount =
        m_ReflectionProbeResources.AuthoredCubemapDiffuseLobesReadyCount();
    if (frameReflectionProbes.selectedProbeCount > 0 &&
        frameReflectionProbes.selectedProbes[0].captureSource ==
            RendererReflectionProbeCaptureSource::AuthoredCubemap) {
        frameStats.reflectionProbe.authoredCubemapFaceSize =
            m_ReflectionProbeResources.AuthoredCubemapFaceSize(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            );
        frameStats.reflectionProbe.authoredCubemapMipCount =
            m_ReflectionProbeResources.AuthoredCubemapMipCount(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            );
        frameStats.reflectionProbe.authoredCubemapFormat =
            m_ReflectionProbeResources.AuthoredCubemapFormat(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            );
        frameStats.reflectionProbe.authoredCubemapSourceType =
            static_cast<u32>(m_ReflectionProbeResources.AuthoredCubemapSourceType(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            ));
        frameStats.reflectionProbe.authoredCubemapHdr =
            m_ReflectionProbeResources.AuthoredCubemapHdr(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            )
                ? 1u
                : 0u;
        frameStats.reflectionProbe.authoredCubemapPrefiltered =
            m_ReflectionProbeResources.AuthoredCubemapPrefiltered(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            )
                ? 1u
                : 0u;
        frameStats.reflectionProbe.authoredCubemapGeneratedMipCount =
            m_ReflectionProbeResources.AuthoredCubemapGeneratedMipCount(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            );
        frameStats.reflectionProbe.authoredCubemapPrefilterSampleCount =
            m_ReflectionProbeResources.AuthoredCubemapPrefilterSampleCount(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            );
        frameStats.reflectionProbe.authoredCubemapPrefilterMode =
            frameStats.reflectionProbe.authoredCubemapPrefiltered > 0
                ? 1u
                : 0u;
        frameStats.reflectionProbe.authoredCubemapFilterQuality =
            static_cast<u32>(
                m_ReflectionProbeResources.AuthoredCubemapFilterQuality(
                    frameReflectionProbes.selectedProbes[0].captureAssetId
                )
            );
        frameStats.reflectionProbe.authoredCubemapSeamAwareFiltering =
            m_ReflectionProbeResources.AuthoredCubemapSeamAwareFiltering(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            )
                ? 1u
                : 0u;
        if (m_ReflectionProbeResources.AuthoredCubemapIrradianceReady(
                frameReflectionProbes.selectedProbes[0].captureAssetId
            )) {
            const std::array<f32, 3> irradiance =
                m_ReflectionProbeResources.AuthoredCubemapIrradianceColor(
                    frameReflectionProbes.selectedProbes[0].captureAssetId
                );
            frameStats.reflectionProbe.authoredCubemapIrradianceApplied = 1u;
            frameStats.reflectionProbe.authoredCubemapIrradianceR = irradiance[0];
            frameStats.reflectionProbe.authoredCubemapIrradianceG = irradiance[1];
            frameStats.reflectionProbe.authoredCubemapIrradianceB = irradiance[2];
        }
    }
    if (m_DirectionalShadowCascadeAtlas != nullptr) {
        const VkExtent2D cascadeAtlasExtent = m_DirectionalShadowCascadeAtlas->Extent();
        frameStats.shadowCascades.atlasAllocated = cascadeAtlasExtent.width > 0 ? 1u : 0u;
        frameStats.shadowCascades.atlasTileSize =
            m_DirectionalShadowCascadeAtlas->TileSize();
        frameStats.shadowCascades.atlasWidth = cascadeAtlasExtent.width;
        frameStats.shadowCascades.atlasHeight = cascadeAtlasExtent.height;
        frameStats.shadowCascades.atlasTileColumns =
            m_DirectionalShadowCascadeAtlas->TileColumns();
        frameStats.shadowCascades.atlasTileRows =
            m_DirectionalShadowCascadeAtlas->TileRows();
        frameStats.shadowCascades.atlasCascadeCapacity =
            m_DirectionalShadowCascadeAtlas->CascadeCapacity();
        frameStats.shadowCascades.budgetDirectionalDepthBytes =
            ShadowDepthLogicalBytes(
                cascadeAtlasExtent,
                m_DirectionalShadowCascadeAtlas->Format(),
                m_DirectionalShadowCascadeAtlas->Count()
            );
    }
    if (m_LocalShadowAtlas != nullptr) {
        const VkExtent2D localAtlasExtent = m_LocalShadowAtlas->Extent();
        frameStats.localShadowAtlas.allocated = localAtlasExtent.width > 0 ? 1u : 0u;
        frameStats.localShadowAtlas.tileSize = m_LocalShadowAtlas->TileSize();
        frameStats.localShadowAtlas.atlasWidth = localAtlasExtent.width;
        frameStats.localShadowAtlas.atlasHeight = localAtlasExtent.height;
        frameStats.localShadowAtlas.tileColumns = m_LocalShadowAtlas->TileColumns();
        frameStats.localShadowAtlas.tileRows = m_LocalShadowAtlas->TileRows();
        frameStats.localShadowAtlas.tileCapacity = m_LocalShadowAtlas->TileCapacity();
        frameStats.shadowCascades.budgetLocalDepthBytes =
            ShadowDepthLogicalBytes(
                localAtlasExtent,
                m_LocalShadowAtlas->Format(),
                m_LocalShadowAtlas->Count()
            );
        frameStats.localShadowAtlas.shadowableLocalLights =
            localShadowTiles.pointLightCount +
            localShadowTiles.spotLightCount +
            localShadowTiles.rectLightCount;
        frameStats.localShadowAtlas.pointLightCount = localShadowTiles.pointLightCount;
        frameStats.localShadowAtlas.spotLightCount = localShadowTiles.spotLightCount;
        frameStats.localShadowAtlas.rectLightCount = localShadowTiles.rectLightCount;
        frameStats.localShadowAtlas.pointFaceTiles = localShadowTiles.pointFaceTiles;
        frameStats.localShadowAtlas.spotTiles = localShadowTiles.spotTiles;
        frameStats.localShadowAtlas.rectTiles = localShadowTiles.rectTiles;
        frameStats.localShadowAtlas.requestedTiles = localShadowTiles.requestedCount;
        frameStats.localShadowAtlas.assignedTiles = localShadowTiles.assignedCount;
        frameStats.localShadowAtlas.droppedTiles = localShadowTiles.droppedCount;
        frameStats.localShadowAtlas.cacheEligibleTiles = localShadowTiles.cacheEligibleTiles;
        frameStats.localShadowAtlas.cacheHitTiles = localShadowTiles.cacheHitTiles;
        frameStats.localShadowAtlas.cacheMissTiles = localShadowTiles.cacheMissTiles;
        frameStats.localShadowAtlas.cacheSkippedTiles =
            localShadowTiles.cacheSkippedTiles;
        frameStats.localShadowAtlas.cacheColdTiles =
            localShadowTiles.cacheColdTiles;
        frameStats.localShadowAtlas.cacheTileLayoutChangedTiles =
            localShadowTiles.cacheTileLayoutChangedTiles;
        frameStats.localShadowAtlas.cacheLightChangedTiles =
            localShadowTiles.cacheLightChangedTiles;
        frameStats.localShadowAtlas.cacheCasterChangedTiles =
            localShadowTiles.cacheCasterChangedTiles;
        frameStats.localShadowAtlas.cacheDynamicSkinnedCasterTiles =
            localShadowTiles.cacheDynamicSkinnedCasterTiles;
        frameStats.localShadowAtlas.cacheReasonSummary =
            localShadowTiles.cacheReasonSummary;
        frameStats.localShadowAtlas.biasMin =
            std::clamp(m_ShadowSettings.localBiasMin, 0.0f, 0.02f);
        frameStats.localShadowAtlas.biasSlope =
            std::clamp(m_ShadowSettings.localBiasSlope, 0.0f, 0.05f);
        frameStats.localShadowAtlas.pcfRadius =
            std::clamp(m_ShadowSettings.localPcfRadius, 0.0f, 4.0f);
        frameStats.localShadowAtlas.pcfKernelRadius =
            std::clamp<u32>(m_ShadowSettings.localPcfKernelRadius, 0u, 2u);
        frameStats.localShadowAtlas.pcssStrength =
            std::clamp(m_ShadowSettings.localPcssStrength, 0.0f, 1.0f);
        frameStats.localShadowAtlas.filterContractVersion =
            kLocalShadowFilterContractVersion;
        frameStats.localShadowAtlas.productionFilterEnabled =
            m_ShadowSettings.localProductionFilterEnabled ? 1u : 0u;
        frameStats.localShadowAtlas.comparisonSamplerReady =
            m_LocalShadowAtlas->ComparisonSampler() != VK_NULL_HANDLE ? 1u : 0u;
        frameStats.localShadowAtlas.rawDepthSamplerReady =
            m_LocalShadowAtlas->Sampler() != VK_NULL_HANDLE ? 1u : 0u;

        u32 invalidTileRangeLights = 0u;
        u32 maxTilesPerLight = 0u;
        for (u32 lightIndex = 0u;
             lightIndex < localShadowTiles.assignedTilesByLocalLight.size();
             ++lightIndex) {
            const u32 lightTileCount =
                localShadowTiles.assignedTilesByLocalLight[lightIndex];
            if (lightTileCount == 0u) {
                continue;
            }
            maxTilesPerLight = std::max(maxTilesPerLight, lightTileCount);
            const u32 firstTile =
                localShadowTiles.firstAssignedTileByLocalLight[lightIndex];
            bool rangeValid = lightTileCount <= 6u &&
                firstTile < localShadowTiles.assignedCount &&
                firstTile + lightTileCount <= localShadowTiles.assignedCount;
            if (rangeValid) {
                for (u32 rangeOffset = 0u;
                     rangeOffset < lightTileCount;
                     ++rangeOffset) {
                    const LocalShadowTile& tile =
                        localShadowTiles.tiles[firstTile + rangeOffset];
                    if (tile.localLightIndex != lightIndex ||
                        tile.tileIndex != firstTile + rangeOffset) {
                        rangeValid = false;
                        break;
                    }
                }
            }
            if (!rangeValid) {
                ++invalidTileRangeLights;
            }
        }
        frameStats.localShadowAtlas.tileRangeInvalidLights =
            invalidTileRangeLights;
        frameStats.localShadowAtlas.tileRangeContractValid =
            invalidTileRangeLights == 0u ? 1u : 0u;
        frameStats.localShadowAtlas.tileRangeMaxTilesPerLight =
            maxTilesPerLight;

        const u32 assignedLocalShadowTiles = std::min<u32>(
            localShadowTiles.assignedCount,
            static_cast<u32>(localShadowTiles.tiles.size())
        );
        for (u32 tileIndex = 0u;
             tileIndex < assignedLocalShadowTiles;
             ++tileIndex) {
            const glm::vec4 geometry =
                localShadowTiles.tiles[tileIndex].filterGeometry;
            const bool geometryValid =
                std::isfinite(geometry.x) && std::isfinite(geometry.y) &&
                std::isfinite(geometry.z) && std::isfinite(geometry.w) &&
                geometry.x > 0.0f && geometry.y > geometry.x &&
                geometry.z >= 0.0f && geometry.w > 0.0f;
            if (geometryValid) {
                ++frameStats.localShadowAtlas.filterGeometryValidTiles;
            } else {
                ++frameStats.localShadowAtlas.filterGeometryInvalidTiles;
            }
        }

        const bool localFilterResourcesReady =
            frameStats.localShadowAtlas.allocated != 0u &&
            frameStats.localShadowAtlas.comparisonSamplerReady != 0u &&
            frameStats.localShadowAtlas.rawDepthSamplerReady != 0u;
        const bool localFilterContractReady =
            frameStats.localShadowAtlas.tileRangeContractValid != 0u &&
            frameStats.localShadowAtlas.filterGeometryInvalidTiles == 0u;
        frameStats.localShadowAtlas.productionFilterReady =
            localFilterResourcesReady && localFilterContractReady ? 1u : 0u;
        frameStats.localShadowAtlas.productionFilterActive =
            m_ShadowSettings.localProductionFilterEnabled &&
            frameStats.localShadowAtlas.productionFilterReady != 0u &&
            localShadowTiles.assignedCount > 0u ? 1u : 0u;
        if (!m_ShadowSettings.localProductionFilterEnabled) {
            frameStats.localShadowAtlas.productionFilterFallbackReason = 1u;
        } else if (frameStats.localShadowAtlas.allocated == 0u) {
            frameStats.localShadowAtlas.productionFilterFallbackReason = 2u;
        } else if (!localFilterResourcesReady) {
            frameStats.localShadowAtlas.productionFilterFallbackReason = 3u;
        } else if (frameStats.localShadowAtlas.tileRangeContractValid == 0u) {
            frameStats.localShadowAtlas.productionFilterFallbackReason = 4u;
        } else if (frameStats.localShadowAtlas.filterGeometryInvalidTiles != 0u) {
            frameStats.localShadowAtlas.productionFilterFallbackReason = 5u;
        }
        frameStats.localShadowAtlas.faceBlendStrength =
            std::clamp(m_ShadowSettings.localFaceBlendStrength, 0.0f, 1.0f);
        frameStats.localShadowAtlas.rectBiasScale =
            std::clamp(m_ShadowSettings.rectLightShadowBiasScale, 0.0f, 32.0f);
        const VulkanLocalShadowFilterSettings& pointFilter =
            m_ShadowSettings.pointLocalShadowFilter;
        frameStats.localShadowAtlas.pointBiasMin =
            std::clamp(pointFilter.biasMin, 0.0f, 0.02f);
        frameStats.localShadowAtlas.pointBiasSlope =
            std::clamp(pointFilter.biasSlope, 0.0f, 0.05f);
        frameStats.localShadowAtlas.pointPcfRadius =
            std::clamp(pointFilter.pcfRadius, 0.0f, 4.0f);
        frameStats.localShadowAtlas.pointPcfKernelRadius =
            std::clamp<u32>(pointFilter.pcfKernelRadius, 0u, 2u);
        frameStats.localShadowAtlas.pointPcssStrength =
            std::clamp(pointFilter.pcssStrength, 0.0f, 1.0f);
        frameStats.localShadowAtlas.pointPcssBlockerSamples =
            std::clamp<u32>(pointFilter.pcssBlockerSampleCount, 0u, 16u);
        frameStats.localShadowAtlas.pointPcssFilterSamples =
            std::clamp<u32>(pointFilter.pcssFilterSampleCount, 0u, 16u);
        frameStats.localShadowAtlas.pointPcssSearchRadiusTexels =
            std::clamp(pointFilter.pcssSearchRadiusTexels, 0.0f, 16.0f);
        frameStats.localShadowAtlas.pointPcssMaxPenumbraTexels =
            std::clamp(pointFilter.pcssMaxPenumbraTexels, 0.0f, 16.0f);
        const VulkanLocalShadowFilterSettings& spotFilter =
            m_ShadowSettings.spotLocalShadowFilter;
        frameStats.localShadowAtlas.spotBiasMin =
            std::clamp(spotFilter.biasMin, 0.0f, 0.02f);
        frameStats.localShadowAtlas.spotBiasSlope =
            std::clamp(spotFilter.biasSlope, 0.0f, 0.05f);
        frameStats.localShadowAtlas.spotPcfRadius =
            std::clamp(spotFilter.pcfRadius, 0.0f, 4.0f);
        frameStats.localShadowAtlas.spotPcfKernelRadius =
            std::clamp<u32>(spotFilter.pcfKernelRadius, 0u, 2u);
        frameStats.localShadowAtlas.spotPcssStrength =
            std::clamp(spotFilter.pcssStrength, 0.0f, 1.0f);
        frameStats.localShadowAtlas.spotPcssBlockerSamples =
            std::clamp<u32>(spotFilter.pcssBlockerSampleCount, 0u, 16u);
        frameStats.localShadowAtlas.spotPcssFilterSamples =
            std::clamp<u32>(spotFilter.pcssFilterSampleCount, 0u, 16u);
        frameStats.localShadowAtlas.spotPcssSearchRadiusTexels =
            std::clamp(spotFilter.pcssSearchRadiusTexels, 0.0f, 16.0f);
        frameStats.localShadowAtlas.spotPcssMaxPenumbraTexels =
            std::clamp(spotFilter.pcssMaxPenumbraTexels, 0.0f, 16.0f);
        const VulkanLocalShadowFilterSettings& rectFilter =
            m_ShadowSettings.rectLocalShadowFilter;
        frameStats.localShadowAtlas.rectBiasMin =
            std::clamp(rectFilter.biasMin, 0.0f, 0.02f);
        frameStats.localShadowAtlas.rectBiasSlope =
            std::clamp(rectFilter.biasSlope, 0.0f, 0.05f);
        frameStats.localShadowAtlas.rectPcfRadius =
            std::clamp(rectFilter.pcfRadius, 0.0f, 4.0f);
        frameStats.localShadowAtlas.rectPcfKernelRadius =
            std::clamp<u32>(rectFilter.pcfKernelRadius, 0u, 2u);
        frameStats.localShadowAtlas.rectPcssStrength =
            std::clamp(rectFilter.pcssStrength, 0.0f, 1.0f);
        frameStats.localShadowAtlas.rectPcssBlockerSamples =
            std::clamp<u32>(rectFilter.pcssBlockerSampleCount, 0u, 16u);
        frameStats.localShadowAtlas.rectPcssFilterSamples =
            std::clamp<u32>(rectFilter.pcssFilterSampleCount, 0u, 16u);
        frameStats.localShadowAtlas.rectPcssSearchRadiusTexels =
            std::clamp(rectFilter.pcssSearchRadiusTexels, 0.0f, 16.0f);
        frameStats.localShadowAtlas.rectPcssMaxPenumbraTexels =
            std::clamp(rectFilter.pcssMaxPenumbraTexels, 0.0f, 16.0f);
        frameStats.localShadowAtlas.rectShadowBaseSampleTiles =
            localShadowTiles.rectShadowBaseSampleTiles;
        frameStats.localShadowAtlas.rectShadowMaxSampleTiles =
            localShadowTiles.rectShadowMaxSampleTiles;
        frameStats.localShadowAtlas.rectShadowSamplePattern =
            localShadowTiles.rectShadowSamplePattern;
        frameStats.localShadowAtlas.rectShadowExtraSampleTiles =
            localShadowTiles.rectShadowExtraSampleTiles;
        frameStats.localShadowAtlas.rectShadowBudgetLimitedSampleTiles =
            localShadowTiles.rectShadowBudgetLimitedSampleTiles;
        frameStats.localShadowAtlas.pointShadowEnabled =
            m_ShadowSettings.pointLightShadowEnabled ? 1u : 0u;
        frameStats.localShadowAtlas.spotShadowEnabled =
            m_ShadowSettings.spotLightShadowEnabled ? 1u : 0u;
        frameStats.localShadowAtlas.rectShadowEnabled =
            m_ShadowSettings.rectLightShadowEnabled ? 1u : 0u;
        frameStats.localShadowAtlas.debugLightIndex =
            m_ShadowSettings.debugLocalShadowLightIndex;
    }
    const u32 swapchainImageCount = m_Swapchain != nullptr
        ? static_cast<u32>(m_Swapchain->Images().size())
        : 0u;
    frameStats.shadowCascades.budgetSwapchainImageCount = swapchainImageCount;
    if (m_ShadowMap != nullptr) {
        frameStats.shadowCascades.budgetLegacyDepthBytes =
            ShadowDepthLogicalBytes(
                m_ShadowMap->Extent(),
                m_ShadowMap->Format(),
                m_ShadowMap->Count()
            );
    }
    frameStats.shadowCascades.budgetMainDepthBytes =
        frameStats.shadowCascades.budgetLegacyDepthBytes +
        frameStats.shadowCascades.budgetDirectionalDepthBytes +
        frameStats.shadowCascades.budgetLocalDepthBytes;

    u32 shadowBudgetFallbackReason = 0u;
    if (m_ShadowMap == nullptr || m_DirectionalShadowCascadeAtlas == nullptr ||
        m_LocalShadowAtlas == nullptr || swapchainImageCount == 0u) {
        shadowBudgetFallbackReason = 1u;
    } else if (m_ShadowMap->Count() != swapchainImageCount ||
        m_DirectionalShadowCascadeAtlas->Count() != swapchainImageCount ||
        m_LocalShadowAtlas->Count() != swapchainImageCount) {
        shadowBudgetFallbackReason = 2u;
    } else if (m_ShadowMap->Extent().width != m_ShadowSettings.mapSize ||
        m_ShadowMap->Extent().height != m_ShadowSettings.mapSize) {
        shadowBudgetFallbackReason = 3u;
    } else if (m_DirectionalShadowCascadeAtlas->TileSize() !=
        m_ShadowSettings.mapSize) {
        shadowBudgetFallbackReason = 4u;
    } else if (m_LocalShadowAtlas->TileSize() !=
            LocalShadowAtlasTileSizeFor(m_ShadowSettings) ||
        m_LocalShadowAtlas->TileCapacity() !=
            LocalShadowAtlasTileCapacityFor(m_ShadowSettings)) {
        shadowBudgetFallbackReason = 5u;
    } else if (frameStats.shadowCascades.budgetLegacyDepthBytes == 0u ||
        frameStats.shadowCascades.budgetDirectionalDepthBytes == 0u ||
        frameStats.shadowCascades.budgetLocalDepthBytes == 0u) {
        shadowBudgetFallbackReason = 6u;
    }
    frameStats.shadowCascades.budgetFallbackReason = shadowBudgetFallbackReason;
    frameStats.shadowCascades.budgetResourceContractValid =
        shadowBudgetFallbackReason == 0u ? 1u : 0u;
    WriteLocalShadowAttributionStats(
        frameStats.localShadowAtlas,
        frameLightSet,
        localShadowTiles,
        localShadowTileCommandLists,
        m_ShadowSettings,
        m_RenderDebugSettings
    );
    if (m_SceneRenderTargets != nullptr &&
        m_WeightedTranslucencyRenderPass != nullptr &&
        m_WeightedTranslucencyFramebuffer != nullptr) {
        const VkExtent2D weightedExtent =
            m_WeightedTranslucencyFramebuffer->Extent();
        frameStats.weightedTranslucency.allocated =
            weightedExtent.width > 0 && weightedExtent.height > 0 ? 1u : 0u;
        frameStats.weightedTranslucency.accumWidth = weightedExtent.width;
        frameStats.weightedTranslucency.accumHeight = weightedExtent.height;
        frameStats.weightedTranslucency.revealageWidth = weightedExtent.width;
        frameStats.weightedTranslucency.revealageHeight = weightedExtent.height;
        frameStats.weightedTranslucency.accumFormat =
            m_SceneRenderTargets->WeightedTranslucencyAccumFormat();
        frameStats.weightedTranslucency.revealageFormat =
            m_SceneRenderTargets->WeightedTranslucencyRevealageFormat();
        frameStats.weightedTranslucency.renderPassAllocated =
            m_WeightedTranslucencyRenderPass->Handle() != VK_NULL_HANDLE ? 1u : 0u;
        frameStats.weightedTranslucency.framebufferCount =
            static_cast<u32>(m_WeightedTranslucencyFramebuffer->Count());
    }
    frameStats.temporal.taaDebugViewEnabled =
        m_RenderDebugSettings.forwardView == ForwardDebugView::Taa ? 1u : 0u;
    frameStats.temporal.taaRejectionDebugViewEnabled =
        m_RenderDebugSettings.forwardView == ForwardDebugView::TaaRejection ? 1u : 0u;
    frameStats.temporal.taaHistoryDebugViewEnabled =
        m_RenderDebugSettings.forwardView == ForwardDebugView::TaaHistory ? 1u : 0u;
    frameStats.temporal.taaReprojectionDebugViewEnabled =
        m_RenderDebugSettings.forwardView == ForwardDebugView::TaaReprojection ? 1u : 0u;
    frameStats.temporal.temporalConsumerGtaoReady = 0u;
    frameStats.temporal.temporalConsumerMotionBlurReady = 0u;
    if (has3DMainPass && m_GBufferGraphicsPipeline != nullptr) {
        BuildGBufferCommandList(
            mainCommands,
            gBufferCommands,
            weightedTranslucencyCommands,
            forwardResidualCommands,
            mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
            recordTransparentAlphaReference,
            frameStats.draw
        );
    }
    weightedTranslucencyVelocityCommands.clear();
    weightedTranslucencyVelocityCommands.reserve(weightedTranslucencyCommands.size());
    for (const RenderCommand& command : weightedTranslucencyCommands) {
        if (RenderClassForCommand(command) == MaterialRenderClass::Transparent) {
            weightedTranslucencyVelocityCommands.push_back(command);
        }
    }
    forwardResidualVelocityCommands.clear();
    forwardResidualVelocityCommands.reserve(forwardResidualCommands.size());
    for (const RenderCommand& command : forwardResidualCommands) {
        if (RenderClassForCommand(command) == MaterialRenderClass::ForwardSpecial) {
            forwardResidualVelocityCommands.push_back(command);
        }
    }
    const bool forwardResidualVelocityCoverageReady =
        !forwardResidualCommands.empty() &&
        forwardResidualVelocityCommands.size() == forwardResidualCommands.size() &&
        m_ForwardResidualVelocityRenderPass != nullptr &&
        m_ForwardResidualVelocityFramebuffer != nullptr &&
        m_ForwardResidualVelocityGraphicsPipeline != nullptr;
    const bool weightedTranslucencyVelocityCoverageReady =
        !weightedTranslucencyCommands.empty() &&
        weightedTranslucencyVelocityCommands.size() == weightedTranslucencyCommands.size() &&
        m_ForwardResidualVelocityRenderPass != nullptr &&
        m_ForwardResidualVelocityFramebuffer != nullptr &&
        m_ForwardResidualVelocityGraphicsPipeline != nullptr;
    const bool objectMotionVectorsReady = DlssObjectMotionVectorsReady(
        has3DMainPass,
        m_GBufferGraphicsPipeline != nullptr &&
            m_GBufferRenderPass != nullptr &&
            m_GBufferFramebuffer != nullptr,
        velocityTargetAllocated,
        temporalState,
        std::span<const RenderCommand>(gBufferCommands.data(), gBufferCommands.size()),
        std::span<const RenderCommand>(
            weightedTranslucencyCommands.data(),
            weightedTranslucencyCommands.size()
        ),
        std::span<const RenderCommand>(
            forwardResidualCommands.data(),
            forwardResidualCommands.size()
        ),
        weightedTranslucencyVelocityCoverageReady,
        forwardResidualVelocityCoverageReady
    );
    frameStats.temporal.velocityObjectMotionReady =
        objectMotionVectorsReady ? 1u : 0u;
    const bool dlssQualityObjectMotionReady =
        objectMotionVectorsReady &&
        m_DlssQualitySceneContentMotionSupported;
    frameStats.temporal.temporalUpscalerDlssQualitySceneContentMotionSupported =
        m_DlssQualitySceneContentMotionSupported ? 1u : 0u;
    frameStats.temporal.temporalUpscalerDlssQualityObjectMotionReady =
        dlssQualityObjectMotionReady ? 1u : 0u;
    FinalizeDlssQualityGateStats(frameStats.temporal);
    // Feed the final object-motion readiness back into the temporal contract
    // before building the upscale state and publishing CSV stats.
    temporalState.velocityObjectMotionReady = objectMotionVectorsReady;
    const FrameTemporalUpscaleState temporalUpscaleState =
        BuildFrameTemporalUpscaleState(
            extent,
            sceneExtent,
            hdrCompositeAvailable,
            m_SceneRenderTargets != nullptr,
            temporalState,
            temporalReconstructionAllowed
        );
    WriteTemporalStats(
        temporalState,
        temporalUpscaleState,
        velocityTargetAllocated,
        m_SceneRenderTargets != nullptr
            ? m_SceneRenderTargets->VelocityFormat()
            : VK_FORMAT_UNDEFINED,
        materialAuxTargetAllocated,
        m_SceneRenderTargets != nullptr
            ? m_SceneRenderTargets->GBufferMaterialAuxFormat()
            : VK_FORMAT_UNDEFINED,
        historyColorTargetAllocated,
        m_SceneRenderTargets != nullptr
            ? m_SceneRenderTargets->TemporalHistoryColorFormat()
            : VK_FORMAT_UNDEFINED,
        m_SceneRenderTargets != nullptr,
        m_SceneRenderTargets != nullptr
            ? m_SceneRenderTargets->TemporalUpscaleOutputFormat()
            : VK_FORMAT_UNDEFINED,
        m_SceneRenderTargets != nullptr
            ? m_SceneRenderTargets->DisplayExtent()
            : VkExtent2D{},
        recordTemporalHistoryColorCopy
            ? static_cast<u32>(m_SceneRenderTargets->Count())
            : 0u,
        frameStats.temporal
    );
    const bool temporalHistoryReadyForConsumers =
        frameStats.temporal.taaHistoryColorReady > 0 &&
        frameStats.temporal.velocityCameraMotionReady > 0 &&
        ssrSceneColorHistorySourceValid;
    frameStats.temporal.temporalConsumerSsrReady =
        temporalHistoryReadyForConsumers && frameStats.ssr.enabled > 0 ? 1u : 0u;
    frameStats.ssr.sceneColorHistoryReady =
        frameStats.temporal.temporalConsumerSsrReady;
    frameStats.ssr.sceneColorHistorySourceValid =
        ssrSceneColorHistorySourceValid ? 1u : 0u;
    frameStats.ssr.sceneColorHistoryCurrentImageIndex = imageIndex;
    frameStats.ssr.sceneColorHistorySourceImageIndex =
        ssrSceneColorHistorySourceImageIndex;
    frameStats.ssr.sceneColorHistoryFrameAge =
        ssrSceneColorHistorySourceValid ? 1u : 0u;
    frameStats.ssr.sceneColorHistoryActive =
        frameStats.ssr.sceneColorHistoryRequested > 0 &&
        frameStats.ssr.sceneColorHistoryDescriptorBound > 0 &&
        frameStats.ssr.sceneColorHistoryReady > 0
            ? 1u
            : 0u;
    if (frameStats.ssr.sceneColorHistoryActive > 0) {
        frameStats.ssr.sceneColorHistoryFallbackReason = 0u;
    } else if (frameStats.ssr.sceneColorHistoryRequested == 0u) {
        frameStats.ssr.sceneColorHistoryFallbackReason = 1u;
    } else if (frameStats.ssr.sceneColorHistoryDescriptorBound == 0u) {
        frameStats.ssr.sceneColorHistoryFallbackReason = 2u;
    } else if (frameStats.temporal.taaHistoryColorReady == 0u) {
        frameStats.ssr.sceneColorHistoryFallbackReason = 3u;
    } else {
        frameStats.ssr.sceneColorHistoryFallbackReason = 4u;
    }
    frameStats.temporal.temporalConsumerSsrActive =
        frameStats.ssr.sceneColorHistoryActive;
    frameStats.temporal.temporalConsumerDynamicResolutionReady =
        temporalUpscaleState.dynamicResolutionRequested &&
            temporalUpscaleState.temporalUpscaleContractReady
            ? 1u
            : 0u;
    frameStats.temporal.temporalConsumerUpscalerReady =
        temporalUpscaleState.temporalUpscaleRequested &&
            temporalUpscaleState.temporalUpscaleContractReady
            ? 1u
            : 0u;
    frameStats.temporal.temporalConsumerReadinessMask =
        (frameStats.temporal.temporalConsumerSsrReady > 0
            ? kTemporalConsumerSsrBit
            : 0u) |
        (frameStats.temporal.temporalConsumerDynamicResolutionReady > 0
            ? kTemporalConsumerDynamicResolutionBit
            : 0u) |
        (frameStats.temporal.temporalConsumerUpscalerReady > 0
            ? kTemporalConsumerUpscalerBit
            : 0u);
    frameStats.temporal.temporalConsumerActiveMask =
        frameStats.temporal.temporalConsumerSsrActive > 0
            ? kTemporalConsumerSsrBit
            : 0u;
    frameStats.temporal.temporalConsumerUnsupportedMask =
        kTemporalConsumerGtaoBit |
        kTemporalConsumerMotionBlurBit |
        kTemporalConsumerDynamicResolutionBit |
        kTemporalConsumerUpscalerBit;
    frameStats.binds.forwardResidualAlphaReferenceEnabled =
        recordTransparentAlphaReference ? 1u : 0u;
    if (recordTransparentAlphaReference) {
        const u32 weightedDraws =
            static_cast<u32>(weightedTranslucencyCommands.size());
        const u32 residualDraws =
            static_cast<u32>(forwardResidualCommands.size());
        frameStats.binds.weightedTranslucencyAlphaReferenceMismatchDraws =
            weightedDraws > residualDraws
                ? weightedDraws - residualDraws
                : residualDraws - weightedDraws;
    }
    frameStats.draw.mainVisible = mainCullingStats.visible;
    frameStats.draw.mainCulled = mainCullingStats.culled;
    frameStats.draw.overlayVisible = overlayCullingStats.visible;
    frameStats.draw.overlayCulled = overlayCullingStats.culled;
    frameStats.draw.shadowVisible = shadowCullingStats.visible;
    frameStats.draw.shadowCulled = shadowCullingStats.culled;
    frameStats.draw.mainSkinnedConservativeBounds =
        CountSkinnedConservativeBounds(mainCommands);
    frameStats.draw.shadowSkinnedConservativeBounds =
        CountSkinnedConservativeBounds(shadowCommands);
    frameStats.draw.mainBoundsCacheHits = mainCacheStats.boundsCacheHits;
    frameStats.draw.mainBoundsCacheMisses = mainCacheStats.boundsCacheMisses;
    frameStats.draw.mainCommandCacheHits = mainCacheStats.commandCacheHits;
    frameStats.draw.mainCommandCacheMisses = mainCacheStats.commandCacheMisses;
    frameStats.draw.mainVisibilityCacheHits = mainCacheStats.visibilityCacheHits;
    frameStats.draw.mainVisibilityCacheMisses = mainCacheStats.visibilityCacheMisses;
    frameStats.draw.mainQueueCacheHits = mainCacheStats.queueCacheHits;
    frameStats.draw.mainQueueCacheMisses = mainCacheStats.queueCacheMisses;
    if (reusedInstanceBatches) {
        ++frameStats.draw.mainInstanceBatchCacheHits;
    } else {
        ++frameStats.draw.mainInstanceBatchCacheMisses;
    }
    if (uploadedMainInstances) {
        ++frameStats.binds.mainInstanceBufferUploads;
    } else if (skippedMainInstanceUpload) {
        ++frameStats.binds.mainInstanceBufferUploadSkips;
    }
    ++frameStats.binds.frameLightConstantUpdates;
    ++frameStats.binds.frameLightBufferUpdates;
    frameStats.binds.frameLightTotalCount =
        frameLightSet.directionalCount + frameLightSet.localCount;
    frameStats.binds.frameDirectionalLightCount = frameLightSet.directionalCount;
    frameStats.binds.frameLocalLightCount = frameLightSet.localCount;
    frameStats.binds.frameRectLightCount = frameLightSet.rectCount;
    frameStats.binds.frameLightTileSize = lightTileStats.tileSize;
    frameStats.binds.frameLightTileCountX = lightTileStats.tileCountX;
    frameStats.binds.frameLightTileCountY = lightTileStats.tileCountY;
    frameStats.binds.frameLightTileCount = lightTileStats.tileCount;
    frameStats.binds.frameLightTileAssignments = lightTileStats.assignments;
    frameStats.binds.frameLightTileAssignmentCapacity =
        lightTileStats.assignmentCapacity;
    frameStats.binds.frameLightTileOverflowAssignments =
        lightTileStats.overflowAssignments;
    frameStats.binds.frameLightTileOverflowCapacity =
        lightTileStats.overflowCapacity;
    frameStats.binds.frameLightTileOverflowTiles =
        lightTileStats.overflowTileCount;
    frameStats.binds.frameLightTileOverflowDropped =
        lightTileStats.overflowDropped;
    frameStats.binds.frameLightTileAssignmentFallbacks =
        lightTileStats.fallbackCount;
    frameStats.binds.frameLightTileGpuReadbackValid =
        lightTileGpuStats.valid ? 1u : 0u;
    frameStats.binds.frameLightTileGpuSaturatedTiles =
        lightTileGpuStats.saturatedTileCount;
    frameStats.binds.frameLightTileGpuMaxCandidates =
        lightTileGpuStats.maxRawCandidateCount;
    frameStats.binds.frameLightTileGpuRawCandidates =
        lightTileGpuStats.rawCandidateCountSum;
    frameStats.binds.frameLightTileGpuOverflowTiles =
        lightTileGpuStats.overflowUsedTileCount;
    frameStats.binds.frameLightTileGpuOverflowDroppedTiles =
        lightTileGpuStats.overflowDroppedTileCount;
    frameStats.binds.frameLightTileGpuOverflowStored =
        lightTileGpuStats.overflowStoredCount;
    frameStats.binds.frameLightTileGpuOverflowDropped =
        lightTileGpuStats.overflowDroppedCount;
    if (m_MaterialBuffer != nullptr) {
        ++frameStats.binds.frameMaterialBufferUpdates;
    }
    if (m_DirectionalShadowCascadeBuffer != nullptr) {
        ++frameStats.binds.shadowCascadeBufferUpdates;
    }
    if (m_LocalShadowBuffer != nullptr) {
        ++frameStats.binds.localShadowBufferUpdates;
    }
    frameStats.binds.localShadowResolveEnabled =
        m_LocalShadowAtlas != nullptr &&
        m_LocalShadowBuffer != nullptr &&
        localShadowTiles.assignedCount > 0 &&
        shadowSamplingEnabled
            ? 1u
            : 0u;
    frameStats.binds.frameMaterialCount = frameMaterialSet.count;
    frameStats.binds.frameMaterialCapacity = static_cast<u32>(kMaxFrameMaterials);
    frameStats.binds.frameMaterialOverflowCount = frameMaterialSet.overflowCount;
    frameStats.binds.frameMaterialOpaqueCount = frameMaterialSet.opaqueCount;
    frameStats.binds.frameMaterialTransparentCount = frameMaterialSet.transparentCount;
    frameStats.binds.frameMaterialForwardSpecialCount = frameMaterialSet.forwardSpecialCount;
    frameStats.binds.frameMaterialEmissiveHintCount = frameMaterialSet.emissiveHintCount;
    frameStats.binds.frameMaterialSpecularHintCount = frameMaterialSet.specularHintCount;
    frameStats.binds.frameMaterialSpecularTextureCount =
        frameMaterialSet.specularTextureCount;
    frameStats.binds.frameMaterialAlphaMaskCount = frameMaterialSet.alphaMaskCount;
    frameStats.binds.frameMaterialAlphaBlendCount = frameMaterialSet.alphaBlendCount;
    frameStats.binds.frameMaterialUvTransformCount = frameMaterialSet.uvTransformCount;
    frameStats.binds.frameMaterialDoubleSidedCount = frameMaterialSet.doubleSidedCount;
    frameStats.binds.frameMaterialClearcoatCount = frameMaterialSet.clearcoatCount;
    frameStats.binds.frameMaterialClearcoatTextureCount =
        frameMaterialSet.clearcoatTextureCount;
    frameStats.binds.frameMaterialClearcoatRoughnessTextureCount =
        frameMaterialSet.clearcoatRoughnessTextureCount;
    frameStats.binds.frameMaterialTransmissionCount = frameMaterialSet.transmissionCount;
    frameStats.binds.frameMaterialTransmissionTextureCount =
        frameMaterialSet.transmissionTextureCount;
    frameStats.binds.frameMaterialVolumeCount = frameMaterialSet.volumeCount;
    frameStats.binds.frameMaterialOpacityTextureCount =
        frameMaterialSet.opacityTextureCount;
    frameStats.binds.frameMaterialTexturedCount = frameMaterialSet.texturedCount;
    frameStats.binds.frameMaterialTextureMipLodBias =
        ActiveMaterialTextureMipLodBias();
    frameStats.draw.overlayBoundsCacheHits = overlayCacheStats.boundsCacheHits;
    frameStats.draw.overlayBoundsCacheMisses = overlayCacheStats.boundsCacheMisses;
    frameStats.draw.overlayCommandCacheHits = overlayCacheStats.commandCacheHits;
    frameStats.draw.overlayCommandCacheMisses = overlayCacheStats.commandCacheMisses;
    frameStats.draw.overlayVisibilityCacheHits = overlayCacheStats.visibilityCacheHits;
    frameStats.draw.overlayVisibilityCacheMisses = overlayCacheStats.visibilityCacheMisses;
    frameStats.draw.overlayQueueCacheHits = overlayCacheStats.queueCacheHits;
    frameStats.draw.overlayQueueCacheMisses = overlayCacheStats.queueCacheMisses;
    frameStats.draw.mainInstancedDraws =
        static_cast<u32>(m_MainInstanceBatches.size());
    frameStats.draw.mainInstancedInstances =
        static_cast<u32>(m_MainInstances.size());
    frameStats.draw.matrixRecalculations = TransformMatrixRecalculationCount();
    const RenderFeatureFrameGraphAppendData renderFeatureFrameGraphAppendData{
        &m_RenderFeatures,
        &renderFeatureContext,
        &frameStats
    };
    // Debug views that bypass temporal reconstruction do not consume the
    // persistent history image this frame. Keep the graph aligned with the
    // executed passes rather than reporting that dormant cache as unused.
    const bool frameGraphTemporalHistoryActive =
        temporalReconstructionAllowed &&
        frameStats.temporal.taaHistoryColorTargetAllocated > 0;
    frameStats.frameGraph = BuildCurrentVulkanFrameGraphPlan(
        CurrentVulkanFrameGraphInputs{
            shadowPassEnabled && !m_ShadowRenderQueue.Empty(),
            m_OverlayScene3D != nullptr && !overlayCommands.empty(),
            m_ImGuiLayer != nullptr,
            m_FrameMatricesProvider || m_ImGuiScene3D != nullptr || m_OverlayScene3D != nullptr,
            has3DMainPass || m_OverlayScene3D != nullptr,
            m_Swapchain->ImageFormat(),
            m_DepthBuffer->Format(),
            m_Swapchain->Extent(),
            static_cast<u32>(m_Swapchain->Images().size()),
            m_ShadowMap != nullptr ? m_ShadowMap->Extent().width : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? m_DirectionalShadowCascadeAtlas->Extent().width
                : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? m_DirectionalShadowCascadeAtlas->Extent().height
                : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? m_DirectionalShadowCascadeAtlas->TileSize()
                : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? m_DirectionalShadowCascadeAtlas->CascadeCapacity()
                : 0,
            m_LocalShadowAtlas != nullptr
                ? m_LocalShadowAtlas->Extent().width
                : 0,
            m_LocalShadowAtlas != nullptr
                ? m_LocalShadowAtlas->Extent().height
                : 0,
            m_LocalShadowAtlas != nullptr
                ? m_LocalShadowAtlas->TileSize()
                : 0,
            m_LocalShadowAtlas != nullptr
                ? m_LocalShadowAtlas->TileCapacity()
                : 0,
            m_LocalShadowAtlas != nullptr
                ? frameStats.localShadowAtlas.assignedTiles
                : 0,
            m_DirectionalShadowCascadeAtlas != nullptr
                ? directionalShadowCascades.activeCount
                : 0,
            directionalShadowCascades.activeCount,
            directionalShadowCascades.activeCount > 0,
            m_SceneRenderTargets != nullptr,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->HdrSceneColorFormat()
                : VK_FORMAT_UNDEFINED,
            m_HdrRenderPass != nullptr && m_HdrFramebuffer != nullptr,
            recordBloomPyramid,
            m_BloomPyramid != nullptr
                ? m_BloomPyramid->BloomFormat()
                : VK_FORMAT_UNDEFINED,
            m_BloomPyramid != nullptr ? m_BloomPyramid->MipCount() : 0,
            frameStats.postProcess.colorGradingLutEnabled > 0,
            m_ColorGradingLut != nullptr
                ? m_ColorGradingLut->Format()
                : VK_FORMAT_UNDEFINED,
            colorGradingLutReady ? m_ColorGradingLut->LutSize() : 0,
            frameStats.ibl.brdfLutAllocated > 0,
            frameStats.ibl.brdfLutFormat,
            frameStats.ibl.brdfLutSize,
            frameStats.ibl.irradianceMapAllocated > 0,
            frameStats.ibl.irradianceFormat,
            frameStats.ibl.irradianceFaceSize,
            frameStats.ibl.prefilteredMapAllocated > 0,
            frameStats.ibl.prefilteredFormat,
            frameStats.ibl.prefilteredFaceSize,
            frameStats.ibl.prefilteredMipCount,
            frameReflectionProbes.activeLocalProbeCount > 0 &&
                frameReflectionProbes.localProbe.sceneOwned,
            frameReflectionProbes.sceneProbeCount,
            frameReflectionProbes.activeLocalProbeCount > 0 &&
                frameReflectionProbes.localProbe.sceneOwned,
            frameStats.reflectionProbe.selectedProbeMask,
            frameStats.reflectionProbe.selectedBoxProjectionMask,
            frameStats.reflectionProbe.selectedPositiveInfluenceMask,
            frameStats.reflectionProbe.normalizedBlendWeightSum,
            frameStats.reflectionProbe.blendWeightNormalizationFallbackCount,
            frameReflectionProbes.activeLocalProbeCount > 0,
            frameStats.reflectionProbe.captureSourceType,
            frameStats.reflectionProbe.captureFallbackReason,
            frameStats.reflectionProbe.selectedProbeCount > 0,
            frameStats.reflectionProbe.refreshPolicy,
            frameStats.reflectionProbe.forcedRefreshRequested > 0,
            frameStats.reflectionProbe.sceneDirtyRequested > 0,
            frameStats.reflectionProbe.capturedScenePlaceholderAllocatedCount > 0,
            frameStats.reflectionProbe.capturedScenePlaceholderReadyCount,
            frameStats.reflectionProbe.capturedSceneInvalidatedCount,
            frameStats.reflectionProbe.selectedCubemapSamplingCount > 0,
            frameStats.reflectionProbe.localCubemapFormat,
            frameStats.reflectionProbe.localCubemapFaceSize,
            frameStats.reflectionProbe.localCubemapMipCount,
            frameStats.reflectionProbe.selectedCubemapSamplingCount > 0,
            frameStats.reflectionProbe.authoredCubemapFormat,
            frameStats.reflectionProbe.authoredCubemapFaceSize,
            frameStats.reflectionProbe.authoredCubemapMipCount,
            frameStats.reflectionProbe.authoredCubemapLoadedCount,
            frameStats.reflectionProbe.authoredCubemapEquirectangularLoadedCount,
            frameStats.reflectionProbe.authoredCubemapEquirectangularConversionCount,
            frameStats.reflectionProbe.authoredCubemapHdrLoadedCount,
            frameStats.reflectionProbe.authoredCubemapPrefilteredLoadedCount,
            frameStats.reflectionProbe.authoredCubemapPrefilteredUploadCount,
            frameStats.reflectionProbe.authoredCubemapPrefilterMode,
            frameStats.reflectionProbe.authoredCubemapFilterQuality,
            frameStats.reflectionProbe.authoredCubemapSeamAwareFiltering > 0,
            frameStats.reflectionProbe.authoredCubemapIrradianceReadyCount,
            frameStats.reflectionProbe.authoredCubemapIrradianceApplied > 0,
            frameStats.reflectionProbe.authoredCubemapDiffuseLobesReadyCount,
            frameStats.reflectionProbe.authoredCubemapDiffuseLobesApplied > 0,
            frameStats.reflectionProbe.authoredCubemapDiffuseLobeCount,
            frameStats.reflectionProbe.authoredCubemapCacheHitCount,
            frameStats.reflectionProbe.authoredCubemapReloadCount,
            frameStats.reflectionProbe.authoredCubemapRefreshCheckCount,
            frameStats.postProcess.autoExposureHistogramEnabled > 0,
            frameStats.postProcess.autoExposureHistogramEnabled > 0 &&
                m_AutoExposureBuffer != nullptr,
            m_DeferredLightingPipeline != nullptr && m_GBufferDescriptorSets != nullptr,
            AppendRenderFeaturesToCurrentFrameGraph,
            &renderFeatureFrameGraphAppendData,
            has3DMainPass && m_LightTileCullComputePipeline != nullptr,
            gBufferDebugView >= 0 && m_GBufferDebugPipeline != nullptr && m_GBufferDescriptorSets != nullptr,
            frameStats.weightedTranslucency.allocated > 0,
            frameStats.weightedTranslucency.accumFormat,
            frameStats.weightedTranslucency.revealageFormat,
            frameStats.weightedTranslucency.renderPassAllocated > 0,
            frameStats.weightedTranslucency.framebufferCount,
            has3DMainPass &&
                !forwardResidualCommands.empty() &&
                m_ForwardResidualHdrGraphicsPipeline != nullptr &&
                m_HdrRenderPass != nullptr &&
                m_HdrFramebuffer != nullptr &&
                m_SceneRenderTargets != nullptr,
            m_SceneRenderTargets != nullptr,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->SceneDepthFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->VelocityFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->GBufferAlbedoFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->GBufferNormalRoughnessFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->GBufferMaterialFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->GBufferEmissiveFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->GBufferMaterialAuxFormat()
                : VK_FORMAT_UNDEFINED,
            m_GBufferRenderPass != nullptr && m_GBufferFramebuffer != nullptr,
            has3DMainPass && m_GBufferGraphicsPipeline != nullptr,
            has3DMainPass &&
                !forwardResidualVelocityCommands.empty() &&
                m_ForwardResidualVelocityRenderPass != nullptr &&
                m_ForwardResidualVelocityFramebuffer != nullptr &&
                m_ForwardResidualVelocityGraphicsPipeline != nullptr,
            has3DMainPass &&
                !weightedTranslucencyVelocityCommands.empty() &&
                m_ForwardResidualVelocityRenderPass != nullptr &&
                m_ForwardResidualVelocityFramebuffer != nullptr &&
                m_ForwardResidualVelocityGraphicsPipeline != nullptr,
            frameStats.temporal.velocityTargetAllocated > 0,
            frameStats.temporal.historyValid > 0,
            frameStats.temporal.historyReset > 0,
            frameStats.temporal.historyResetReason,
            frameStats.temporal.jitterEnabled > 0,
            frameStats.temporal.jitterApplied > 0,
            frameStats.temporal.velocityCameraMotionReady > 0,
            frameStats.temporal.velocityObjectMotionReady > 0,
            frameStats.temporal.velocityMaterialAuxMigrated > 0,
            frameGraphTemporalHistoryActive,
            frameGraphTemporalHistoryActive
                ? frameStats.temporal.taaHistoryColorFormat
                : VK_FORMAT_UNDEFINED,
            frameGraphTemporalHistoryActive &&
                frameStats.temporal.taaHistoryColorReady > 0,
            frameGraphTemporalHistoryActive &&
                frameStats.temporal.taaHistoryColorCopies > 0,
            frameStats.temporal.taaResolveConfigured > 0,
            frameStats.temporal.taaResolveEnabled > 0,
            frameStats.temporal.taaVelocityReprojectionEnabled > 0,
            frameStats.temporal.taaFallbackReason,
            frameStats.temporal.temporalConsumerReadinessMask,
            frameStats.temporal.temporalConsumerActiveMask,
            frameStats.temporal.temporalConsumerUnsupportedMask,
            frameStats.temporal.renderScaleRequested,
            frameStats.temporal.renderScaleActive,
            frameStats.temporal.renderScaleApplied > 0,
            frameStats.temporal.temporalUpscaleDisplayWidth,
            frameStats.temporal.temporalUpscaleDisplayHeight,
            frameStats.temporal.temporalUpscaleRequestedWidth,
            frameStats.temporal.temporalUpscaleRequestedHeight,
            frameStats.temporal.temporalUpscaleActiveWidth,
            frameStats.temporal.temporalUpscaleActiveHeight,
            frameStats.temporal.temporalUpscaleOutputAllocated > 0,
            frameStats.temporal.temporalUpscaleOutputFormat,
            frameStats.temporal.temporalUpscaleOutputWidth,
            frameStats.temporal.temporalUpscaleOutputHeight,
            m_SceneRenderTargets != nullptr &&
                frameStats.temporal.temporalUpscalerProviderKind ==
                    static_cast<u32>(TemporalUpscalerProviderKind::Dlss),
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->DlssBiasCurrentColorMaskFormat()
                : VK_FORMAT_UNDEFINED,
            m_SceneRenderTargets != nullptr
                ? m_SceneRenderTargets->DlssTransparencyMaskFormat()
                : VK_FORMAT_UNDEFINED,
            has3DMainPass &&
                frameStats.temporal.temporalUpscaleEnabled > 0 &&
                (!weightedTranslucencyCommands.empty() ||
                    !forwardResidualCommands.empty()) &&
                m_DlssMaskRenderPass != nullptr &&
                m_DlssMaskFramebuffer != nullptr &&
                m_DlssMaskGraphicsPipeline != nullptr,
            frameStats.temporal.dynamicResolutionRequested > 0,
            frameStats.temporal.dynamicResolutionEnabled > 0,
            frameStats.temporal.taauRequested > 0,
            frameStats.temporal.temporalUpscaleRequested > 0,
            frameStats.temporal.temporalUpscaleEnabled > 0,
            frameStats.temporal.temporalUpscaleFallbackReason,
            frameStats.temporal.temporalUpscaleInputReadinessMask,
            frameStats.temporal.temporalUpscaleRequiredInputMask,
            frameStats.temporal.temporalUpscaleContractReady > 0,
            frameStats.temporal.temporalUpscalerPluginRequested > 0,
            frameStats.temporal.temporalUpscalerPluginAvailable > 0,
            frameStats.temporal.temporalUpscalerProviderKind,
            frameStats.temporal.temporalUpscalerPackageFallbackReason,
            frameStats.temporal.temporalUpscalerPackageReady > 0,
            frameStats.temporal.temporalUpscalerDlssSuperResolutionSymbolsFound > 0,
            frameStats.temporal.temporalUpscalerEvaluateAdapterAvailable > 0,
            frameStats.temporal.temporalUpscalerRuntimeFallbackReason,
            frameStats.temporal.temporalUpscalerAdapterCompiled > 0,
            frameStats.temporal.temporalUpscalerInitializationAttempted > 0,
            frameStats.temporal.temporalUpscalerInitialized > 0,
            frameStats.temporal.temporalUpscalerCapabilityParametersReady > 0,
            frameStats.temporal.temporalUpscalerFeatureRequirementsQueried > 0,
            frameStats.temporal.temporalUpscalerFeatureRequirementsSupported > 0,
            frameStats.temporal.temporalUpscalerFeatureSupportedMask,
            frameStats.temporal.temporalUpscalerInstanceExtensionMissingAvailableCount,
            frameStats.temporal.temporalUpscalerInstanceExtensionMissingEnabledCount,
            frameStats.temporal.temporalUpscalerDeviceExtensionMissingAvailableCount,
            frameStats.temporal.temporalUpscalerDeviceExtensionMissingEnabledCount,
            frameStats.temporal.temporalUpscalerDlssSuperResolutionSupported > 0,
            frameStats.temporal.temporalUpscalerOptimalSettingsQueried > 0,
            frameStats.temporal.temporalUpscalerDlssQualityGateRequested > 0,
            frameStats.temporal.temporalUpscalerDlssQualityGateReady > 0,
            frameStats.temporal.temporalUpscalerDlssQualityRequiredMask,
            frameStats.temporal.temporalUpscalerDlssQualityReadyMask,
            frameStats.temporal.temporalUpscalerDlssQualityBlockerMask,
            frameReflectionProbes.activeLocalProbeCount > 0 &&
                frameReflectionProbes.localProbe.sceneOwned,
            frameStats.reflectionProbe.selectedCaptureSlotCount,
            frameStats.reflectionProbe.selectedCaptureResourceReadyCount,
            frameStats.reflectionProbe.selectedCaptureFallbackCount,
            frameStats.probeGrid.allocated > 0,
            frameStats.probeGrid.shaderIntegrationEnabled > 0,
            frameStats.probeGrid.probeCount,
            frameStats.probeGrid.sizeX,
            frameStats.probeGrid.sizeY,
            frameStats.probeGrid.sizeZ,
            frameStats.probeGrid.vec4sPerProbe,
            frameStats.probeGrid.directionalLobeCount,
            frameStats.probeGrid.cellCount,
            frameStats.probeGrid.fallbackReason,
            frameStats.probeGrid.debugViewEnabled > 0,
            frameStats.probeGrid.cellDebugViewEnabled > 0,
            frameStats.temporal.temporalUpscalePostSourceRequested > 0,
            frameStats.temporal.temporalUpscaleEnabled > 0,
            frameStats.temporal.temporalUpscalePostSourceFallbackReason,
            frameStats.ssr.hierarchicalActive > 0,
            frameStats.ssr.reconstructionActive > 0,
            frameStats.ssr.depthPyramidFormat,
            frameStats.ssr.depthPyramidWidth,
            frameStats.ssr.depthPyramidHeight,
            frameStats.ssr.depthPyramidMipCount
        }
    );

    constexpr u32 kLightTileCullComputeLocalSizeX = 8;
    constexpr u32 kLightTileCullComputeLocalSizeY = 8;
    const bool recordLightTileCullCompute =
        has3DMainPass &&
        m_LightTileCullComputePipeline != nullptr &&
        lightTileStats.tileCountX > 0 &&
        lightTileStats.tileCountY > 0;
    // Cluster cull writes a different light-tile layout than the current shaders consume.
    const bool recordClusterLightCullCompute = false;
    const u32 lightTileCullGroupCountX = recordLightTileCullCompute
        ? (lightTileStats.tileCountX + kLightTileCullComputeLocalSizeX - 1) /
            kLightTileCullComputeLocalSizeX
        : 0;
    const u32 lightTileCullGroupCountY = recordLightTileCullCompute
        ? (lightTileStats.tileCountY + kLightTileCullComputeLocalSizeY - 1) /
            kLightTileCullComputeLocalSizeY
        : 0;

    TemporalUpscalerEvaluateStatus temporalUpscalerEvaluateStatus{};
    TemporalUpscalePostSourceStatus temporalUpscalePostSourceStatus{};
    const bool temporalUpscaleOutputInitialized =
        imageIndex < m_TemporalUpscaleOutputInitialized.size()
            ? m_TemporalUpscaleOutputInitialized[imageIndex]
            : false;
    const bool dlssMaskInputsInitialized =
        imageIndex < m_DlssMaskInputsInitialized.size()
            ? m_DlssMaskInputsInitialized[imageIndex]
            : false;
    const ShadowDepthBiasControls shadowDepthBias{
        m_ShadowSettings.casterDepthBiasEnabled,
        std::clamp(m_ShadowSettings.casterDepthBiasConstant, 0.0f, 262144.0f),
        m_PhysicalDevice.Features().depthBiasClamp
            ? std::clamp(m_ShadowSettings.casterDepthBiasClamp, 0.0f, 0.05f)
            : 0.0f,
        std::clamp(m_ShadowSettings.casterDepthBiasSlope, 0.0f, 16.0f)
    };
    m_CommandBuffer->Record(
        imageIndex,
        *m_RenderPass,
        *m_GraphicsPipeline,
        m_DoubleSidedGraphicsPipeline.get(),
        *m_DescriptorSets,
        *m_MaterialDescriptorSets,
        m_RenderQueue.Commands(),
        *m_Framebuffer,
        m_DepthLoadRenderPass.get(),
        m_DepthLoadFramebuffer.get(),
        *m_Swapchain,
        m_PhysicalDevice,
        hideImGuiForVisualQa ? nullptr : m_ImGuiLayer.get(),
        m_ShadowRenderPass.get(),
        m_ShadowGraphicsPipeline.get(),
        m_DoubleSidedShadowGraphicsPipeline.get(),
        m_ShadowFramebuffer.get(),
        ShadowDescriptorSets(),
        shadowCommands,
        m_DirectionalShadowCascadeFramebuffer.get(),
        &directionalShadowCascades,
        m_LocalShadowFramebuffer.get(),
        &localShadowTiles,
        std::span<const std::span<const RenderCommand>>(
            directionalShadowCommandSpans.data(),
            directionalShadowCommandSpanCount
        ),
        std::span<const std::span<const RenderCommand>>(
            localShadowTileCommandSpans.data(),
            localShadowTileCommandSpanCount
        ),
        shadowDepthBias,
        localShadowTiles.cacheSkippedTiles == localShadowTiles.assignedCount &&
            localShadowTiles.assignedCount > 0,
        m_HdrRenderPass.get(),
        m_HdrFramebuffer.get(),
        m_DeferredLightingPipeline.get(),
        m_DescriptorSets.get(),
        m_GBufferDescriptorSets.get(),
        deferredPbrDebugView,
        m_HdrCompositePipeline.get(),
        m_HdrDescriptorSets.get(),
        m_TemporalUpscaleHdrDescriptorSets.get(),
        m_BloomDownsampleRenderPass.get(),
        m_BloomUpsampleRenderPass.get(),
        m_BloomDownsampleFramebuffer.get(),
        m_BloomUpsampleFramebuffer.get(),
        recordBloomPyramid ? m_BloomDownsamplePipeline.get() : nullptr,
        recordBloomPyramid ? m_BloomUpsamplePipeline.get() : nullptr,
        recordBloomPyramid ? m_BloomDescriptorSets.get() : nullptr,
        recordBloomPyramid ? m_TemporalUpscaleBloomDescriptorSets.get() : nullptr,
        recordBloomPyramid,
        showDeferredHdr,
        temporalUpscaleState.temporalUpscalePostSourceRequested,
        m_RenderDebugSettings.forwardView == ForwardDebugView::Bloom,
        m_RenderDebugSettings.forwardView == ForwardDebugView::ToneMapping,
        m_RenderDebugSettings.forwardView == ForwardDebugView::AutoExposure,
        m_RenderDebugSettings.forwardView == ForwardDebugView::ColorGrading,
        m_RenderDebugSettings.forwardView == ForwardDebugView::Sharpening,
        m_TemporalHistoryColorValid,
        recordTemporalHistoryColorCopy,
        temporalState.taaResolveEnabled
            ? TemporalHistoryColorCopySource::TaaResolvedColor
            : TemporalHistoryColorCopySource::HdrSceneColor,
        m_TaaResolveFramebuffer.get(),
        m_TaaResolvePipeline.get(),
        &temporalState,
        &temporalUpscaleState,
        temporalUpscaleOutputInitialized,
        dlssMaskInputsInitialized,
        &temporalUpscalerEvaluateStatus,
        &temporalUpscalePostSourceStatus,
        m_GBufferDebugPipeline.get(),
        m_GBufferDescriptorSets.get(),
        gBufferDebugView,
        has3DMainPass ? m_DepthPrefillGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedDepthPrefillGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_SceneRenderTargets.get() : nullptr,
        has3DMainPass ? m_DepthBuffer.get() : nullptr,
        m_GBufferRenderPass.get(),
        m_GBufferFramebuffer.get(),
        m_ForwardResidualVelocityRenderPass.get(),
        m_ForwardResidualVelocityFramebuffer.get(),
        has3DMainPass ? m_ForwardResidualVelocityGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedForwardResidualVelocityGraphicsPipeline.get() : nullptr,
        has3DMainPass
            ? std::span<const RenderCommand>(
                forwardResidualVelocityCommands.data(),
                forwardResidualVelocityCommands.size()
            )
            : std::span<const RenderCommand>{},
        has3DMainPass
            ? std::span<const RenderCommand>(
                weightedTranslucencyVelocityCommands.data(),
                weightedTranslucencyVelocityCommands.size()
            )
            : std::span<const RenderCommand>{},
        m_DlssMaskRenderPass.get(),
        m_DlssMaskFramebuffer.get(),
        has3DMainPass ? m_DlssMaskGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedDlssMaskGraphicsPipeline.get() : nullptr,
        has3DMainPass
            ? std::span<const RenderCommand>(
                weightedTranslucencyCommands.data(),
                weightedTranslucencyCommands.size()
            )
            : std::span<const RenderCommand>{},
        has3DMainPass
            ? std::span<const RenderCommand>(
                forwardResidualCommands.data(),
                forwardResidualCommands.size()
            )
            : std::span<const RenderCommand>{},
        m_WeightedTranslucencyRenderPass.get(),
        m_WeightedTranslucencyFramebuffer.get(),
        has3DMainPass ? m_WeightedTranslucencyGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedWeightedTranslucencyGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_WeightedTranslucencyResolvePipeline.get() : nullptr,
        m_WeightedTranslucencyDescriptorSets.get(),
        has3DMainPass
            ? std::span<const RenderCommand>(
                weightedTranslucencyCommands.data(),
                weightedTranslucencyCommands.size()
            )
            : std::span<const RenderCommand>{},
        weightedTranslucencyDebugView,
        has3DMainPass ? m_GBufferGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedGBufferGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DescriptorSets.get() : nullptr,
        has3DMainPass ? std::span<const RenderCommand>(gBufferCommands.data(), gBufferCommands.size()) : std::span<const RenderCommand>{},
        has3DMainPass && m_BonePaletteFallbackDescriptorSet != nullptr
            ? m_BonePaletteFallbackDescriptorSet->Handle()
            : VK_NULL_HANDLE,
        has3DMainPass && m_BonePaletteFallbackDescriptorSet != nullptr
            ? m_BonePaletteFallbackDescriptorSet->Ready()
            : 0u,
        recordLightTileCullCompute ? m_LightTileCullComputePipeline.get() : nullptr,
        recordLightTileCullCompute ? m_DescriptorSets.get() : nullptr,
        lightTileCullGroupCountX,
        lightTileCullGroupCountY,
        recordClusterLightCullCompute ? 4u : 1u,
        recordClusterLightCullCompute ? m_LightClusterCullComputePipeline.get() : nullptr,
        recordAutoExposureCompute ? m_AutoExposureComputePipeline.get() : nullptr,
        recordAutoExposureCompute ? m_DescriptorSets.get() : nullptr,
        recordAutoExposureCompute ? m_HdrDescriptorSets.get() : nullptr,
        recordAutoExposureCompute,
        has3DMainPass ? m_ForwardResidualHdrGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedForwardResidualHdrGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_ForwardResidualGraphicsPipeline.get() : nullptr,
        has3DMainPass ? m_DoubleSidedForwardResidualGraphicsPipeline.get() : nullptr,
        has3DMainPass ? std::span<const RenderCommand>(forwardResidualCommands.data(), forwardResidualCommands.size()) : std::span<const RenderCommand>{},
        &frameMaterialSet,
        m_OverlayGraphicsPipeline.get(),
        m_OverlayDescriptorSets.get(),
        overlayCommands,
        m_InstancedGraphicsPipeline.get(),
        m_DoubleSidedInstancedGraphicsPipeline.get(),
        m_InstanceBuffer.get(),
        m_MainInstanceBatches,
        m_GpuTimer.get(),
        &frameStats.binds,
        &frameStats.frameGraph,
        frameStats.ssr.hierarchicalActive > 0
            ? m_HiZBuildComputePipeline.get()
            : nullptr,
        frameStats.ssr.hierarchicalActive > 0
            ? m_HiZDescriptorSets.get()
            : nullptr,
        frameStats.ssr.hierarchicalActive > 0
            ? m_SsrDepthPyramid.get()
            : nullptr,
        frameStats.ssr.hierarchicalActive > 0
            ? m_SceneRenderTargets.get()
            : nullptr,
        frameStats.ssr.reconstructionActive > 0
            ? m_SsrTraceComputePipeline.get()
            : nullptr,
        frameStats.ssr.reconstructionActive > 0
            ? m_SsrTemporalComputePipeline.get()
            : nullptr,
        frameStats.ssr.reconstructionActive > 0
            ? m_SsrSpatialComputePipeline.get()
            : nullptr,
        frameStats.ssr.holeDiagnosticsActive > 0
            ? m_SsrDiagnosticsComputePipeline.get()
            : nullptr,
        frameStats.ssr.reconstructionActive > 0
            ? m_SsrReconstructionDescriptorSets.get()
            : nullptr,
        has3DMainPass ? m_SceneRenderTargets.get() : nullptr,
        frameStats.ssr.reconstructionActive > 0,
        m_SsrReconstructionImagesInitialized,
        frameStats.temporal.historyReset > 0
    );
    if (has3DMainPass && m_SceneRenderTargets != nullptr) {
        m_SsrReconstructionImagesInitialized = true;
    }
    frameStats.ssr.reconstructionTraceDispatches =
        frameStats.binds.ssrReconstructionTraceDispatches;
    frameStats.ssr.reconstructionTemporalDispatches =
        frameStats.binds.ssrReconstructionTemporalDispatches;
    frameStats.ssr.reconstructionSpatialDispatches =
        frameStats.binds.ssrReconstructionSpatialDispatches;
    frameStats.ssr.reconstructionHistoryCopies =
        frameStats.binds.ssrReconstructionHistoryCopies;
    frameStats.ssr.reconstructionHistoryReset =
        frameStats.temporal.historyReset;
    frameStats.ssr.reconstructionTemporalContractVersion =
        frameStats.ssr.reconstructionActive > 0 ? 13u : 0u;
    frameStats.ssr.reconstructionTemporalMissHistoryRejectEnabled =
        frameStats.ssr.reconstructionActive > 0 &&
            m_ShadowSettings.ssrTemporalMissHistoryRejectEnabled
            ? 1u
            : 0u;
    frameStats.ssr.reconstructionTemporalPreviousViewDepthEnabled =
        frameStats.ssr.reconstructionActive > 0 ? 1u : 0u;
    frameStats.ssr.reconstructionTemporalHistoryLockEnabled =
        frameStats.ssr.reconstructionActive > 0 &&
            m_ShadowSettings.ssrTemporalHistoryLockEnabled
            ? 1u
            : 0u;
    frameStats.ssr.reconstructionSpatialCenterHitGateEnabled =
        frameStats.ssr.reconstructionActive > 0 ? 1u : 0u;
    frameStats.ssr.reconstructionSpatialVarianceClampEnabled =
        frameStats.ssr.reconstructionActive > 0 &&
            m_ShadowSettings.ssrSpatialVarianceClampEnabled
            ? 1u
            : 0u;
    frameStats.ssr.reconstructionSpatialSupportTapCount =
        frameStats.ssr.reconstructionActive > 0 ? 13u : 0u;
    frameStats.ssr.reconstructionRawResolvedAliased =
        frameStats.ssr.reconstructionActive > 0 &&
            m_SceneRenderTargets != nullptr &&
            m_SceneRenderTargets->Count() > 0 &&
            m_SceneRenderTargets->SsrRawImage(0) ==
                m_SceneRenderTargets->SsrResolvedImage(0)
            ? 1u
            : 0u;
    frameStats.ssr.reconstructionCurrentHdrSourceEnabled =
        frameStats.ssr.reconstructionActive > 0 &&
            m_ShadowSettings.ssrCurrentHdrSourceEnabled &&
            ssrReconstructionCurrentHdrDescriptorBound
            ? 1u
            : 0u;
    frameStats.ssr.reconstructionCurrentHdrRadianceFilterEnabled =
        frameStats.ssr.reconstructionCurrentHdrSourceEnabled > 0 &&
            m_ShadowSettings.ssrCurrentHdrRadianceFilterEnabled
            ? 1u
            : 0u;
    frameStats.ssr.reconstructionCurrentHdrMipLevels =
        m_SceneRenderTargets != nullptr
            ? m_SceneRenderTargets->HdrSceneColorMipLevels()
            : 0u;
    frameStats.ssr.reconstructionCurrentHdrMipChainReady =
        frameStats.ssr.reconstructionCurrentHdrMipLevels > 1u ? 1u : 0u;
    frameStats.ssr.radianceSource =
        frameStats.ssr.reconstructionCurrentHdrSourceEnabled > 0 &&
        frameStats.ssr.sceneColorHistoryActive > 0 &&
        frameStats.ssr.reconstructionCurrentHdrMipChainReady > 0
            ? 3u
            : (frameStats.ssr.sceneColorHistoryActive > 0
                ? 2u
                : (frameStats.ssr.colorResolveEnabled > 0 ? 1u : 0u));
    frameStats.ssr.fallbackBlendRequested =
        m_ShadowSettings.ssrProbeFallbackBlendEnabled ? 1u : 0u;
    frameStats.ssr.fallbackBlendActive =
        frameStats.ssr.colorResolveEnabled > 0u &&
            frameStats.ssr.fallbackBlendRequested > 0u
            ? 1u
            : 0u;
    frameStats.ssr.fallbackBlendContractVersion =
        frameStats.ssr.fallbackBlendActive > 0u ? 1u : 0u;
    const bool deferredSsrReceiverContractActive =
        frameStats.ssr.reconstructionActive > 0 &&
        m_ShadowSettings.ssrDeferredReceiverReprojectionEnabled;
    frameStats.ssr.reconstructionDeferredConsumerContractVersion =
        frameStats.ssr.reconstructionActive > 0 ? 6u : 0u;
    frameStats.ssr.reconstructionDeferredReceiverReprojectionEnabled =
        deferredSsrReceiverContractActive ? 1u : 0u;
    frameStats.ssr.reconstructionDeferredDepthRejectEnabled =
        deferredSsrReceiverContractActive ? 1u : 0u;
    frameStats.ssr.reconstructionDeferredNormalRejectEnabled =
        deferredSsrReceiverContractActive ? 1u : 0u;
    frameStats.ssr.reconstructionDeferredRoughnessRejectEnabled =
        deferredSsrReceiverContractActive ? 1u : 0u;
    frameStats.ssr.reconstructionDeferredMetadataDescriptorBound =
        frameStats.ssr.reconstructionActive > 0 &&
            gBufferSceneColorHistoryDescriptorUpdated
            ? 1u
            : 0u;
    frameStats.ssr.reconstructionResolvedMetadataAliased =
        frameStats.ssr.reconstructionActive > 0 &&
            m_SceneRenderTargets != nullptr &&
            m_SceneRenderTargets->Count() > 0 &&
            m_SceneRenderTargets->SsrResolvedImage(0) ==
                m_SceneRenderTargets->SsrHistoryMetadataImage(0)
            ? 1u
            : 0u;
    if (imageIndex < m_TemporalUpscaleOutputInitialized.size() &&
        temporalUpscalerEvaluateStatus.outputReady > 0u) {
        m_TemporalUpscaleOutputInitialized[imageIndex] = true;
    }
    if (imageIndex < m_DlssMaskInputsInitialized.size() &&
        temporalUpscalerEvaluateStatus.attempted > 0u) {
        m_DlssMaskInputsInitialized[imageIndex] = true;
    }
    frameStats.temporal.temporalUpscalePostSourceRequested =
        temporalUpscalePostSourceStatus.requested;
    frameStats.temporal.temporalUpscalePostSourceActive =
        temporalUpscalePostSourceStatus.active;
    frameStats.temporal.temporalUpscalePostSourceFallbackReason =
        static_cast<u32>(temporalUpscalePostSourceStatus.fallbackReason);
    frameStats.temporal.temporalUpscalerEvaluateRequested =
        temporalUpscalerEvaluateStatus.requested;
    frameStats.temporal.temporalUpscalerEvaluateAttempted =
        temporalUpscalerEvaluateStatus.attempted;
    frameStats.temporal.temporalUpscalerEvaluateFallbackReason =
        static_cast<u32>(temporalUpscalerEvaluateStatus.fallbackReason);
    frameStats.temporal.temporalUpscalerEvaluateParametersAllocated =
        temporalUpscalerEvaluateStatus.parametersAllocated;
    frameStats.temporal.temporalUpscalerEvaluateParameterAllocationResult =
        temporalUpscalerEvaluateStatus.parameterAllocationResult;
    frameStats.temporal.temporalUpscalerFeatureCreateAttempted =
        temporalUpscalerEvaluateStatus.featureCreateAttempted;
    frameStats.temporal.temporalUpscalerFeatureCreated =
        temporalUpscalerEvaluateStatus.featureCreated;
    frameStats.temporal.temporalUpscalerFeatureCreateResult =
        temporalUpscalerEvaluateStatus.featureCreateResult;
    frameStats.temporal.temporalUpscalerFeatureRecreated =
        temporalUpscalerEvaluateStatus.featureRecreated;
    frameStats.temporal.temporalUpscalerFeatureRecreationReason =
        static_cast<u32>(temporalUpscalerEvaluateStatus.featureRecreationReason);
    frameStats.temporal.temporalUpscalerDlssEvaluateAttempted =
        temporalUpscalerEvaluateStatus.evaluateAttempted;
    frameStats.temporal.temporalUpscalerDlssEvaluateResult =
        temporalUpscalerEvaluateStatus.evaluateResult;
    frameStats.temporal.temporalUpscalerDlssOutputReady =
        temporalUpscalerEvaluateStatus.outputReady;
    frameStats.temporal.temporalUpscalerDlssRenderWidth =
        temporalUpscalerEvaluateStatus.renderWidth;
    frameStats.temporal.temporalUpscalerDlssRenderHeight =
        temporalUpscalerEvaluateStatus.renderHeight;
    frameStats.temporal.temporalUpscalerDlssOutputWidth =
        temporalUpscalerEvaluateStatus.outputWidth;
    frameStats.temporal.temporalUpscalerDlssOutputHeight =
        temporalUpscalerEvaluateStatus.outputHeight;
    frameStats.temporal.temporalUpscalerDlssCreateFlags =
        temporalUpscalerEvaluateStatus.createFlags;
    frameStats.temporal.temporalUpscalerDlssCreateFlagIsHdr =
        temporalUpscalerEvaluateStatus.createFlagIsHdr;
    frameStats.temporal.temporalUpscalerDlssCreateFlagMvLowRes =
        temporalUpscalerEvaluateStatus.createFlagMvLowRes;
    frameStats.temporal.temporalUpscalerDlssCreateFlagMvJittered =
        temporalUpscalerEvaluateStatus.createFlagMvJittered;
    frameStats.temporal.temporalUpscalerDlssCreateFlagDepthInverted =
        temporalUpscalerEvaluateStatus.createFlagDepthInverted;
    frameStats.temporal.temporalUpscalerDlssCreateFlagAutoExposure =
        temporalUpscalerEvaluateStatus.createFlagAutoExposure;
    frameStats.temporal.temporalUpscalerDlssInputColorFormat =
        temporalUpscalerEvaluateStatus.inputColorFormat;
    frameStats.temporal.temporalUpscalerDlssInputDepthFormat =
        temporalUpscalerEvaluateStatus.inputDepthFormat;
    frameStats.temporal.temporalUpscalerDlssInputMotionVectorFormat =
        temporalUpscalerEvaluateStatus.inputMotionVectorFormat;
    frameStats.temporal.temporalUpscalerDlssInputColorWidth =
        temporalUpscalerEvaluateStatus.inputColorWidth;
    frameStats.temporal.temporalUpscalerDlssInputColorHeight =
        temporalUpscalerEvaluateStatus.inputColorHeight;
    frameStats.temporal.temporalUpscalerDlssInputDepthWidth =
        temporalUpscalerEvaluateStatus.inputDepthWidth;
    frameStats.temporal.temporalUpscalerDlssInputDepthHeight =
        temporalUpscalerEvaluateStatus.inputDepthHeight;
    frameStats.temporal.temporalUpscalerDlssInputMotionVectorWidth =
        temporalUpscalerEvaluateStatus.inputMotionVectorWidth;
    frameStats.temporal.temporalUpscalerDlssInputMotionVectorHeight =
        temporalUpscalerEvaluateStatus.inputMotionVectorHeight;
    frameStats.temporal.temporalUpscalerDlssInputDepthAspectMask =
        temporalUpscalerEvaluateStatus.inputDepthAspectMask;
    frameStats.temporal.temporalUpscalerDlssInputMotionVectorAspectMask =
        temporalUpscalerEvaluateStatus.inputMotionVectorAspectMask;
    frameStats.temporal.temporalUpscalerDlssInputDepthMatchesRenderExtent =
        temporalUpscalerEvaluateStatus.inputDepthMatchesRenderExtent;
    frameStats.temporal.temporalUpscalerDlssInputMotionVectorMatchesRenderExtent =
        temporalUpscalerEvaluateStatus.inputMotionVectorMatchesRenderExtent;
    frameStats.temporal.temporalUpscalerDlssMotionVectorScalePixelSpace =
        temporalUpscalerEvaluateStatus.motionVectorScalePixelSpace;
    frameStats.temporal.temporalUpscalerDlssMotionVectorScaleUnitSpace =
        temporalUpscalerEvaluateStatus.motionVectorScaleUnitSpace;
    frameStats.temporal.temporalUpscalerDlssMotionVectorScaleMatchesRenderExtent =
        temporalUpscalerEvaluateStatus.motionVectorScaleMatchesRenderExtent;
    frameStats.temporal.temporalUpscalerDlssReset =
        temporalUpscalerEvaluateStatus.reset;
    frameStats.temporal.temporalUpscalerDlssJitterOffsetX =
        temporalUpscalerEvaluateStatus.jitterOffsetX;
    frameStats.temporal.temporalUpscalerDlssJitterOffsetY =
        temporalUpscalerEvaluateStatus.jitterOffsetY;
    frameStats.temporal.temporalUpscalerDlssMotionVectorScaleX =
        temporalUpscalerEvaluateStatus.motionVectorScaleX;
    frameStats.temporal.temporalUpscalerDlssMotionVectorScaleY =
        temporalUpscalerEvaluateStatus.motionVectorScaleY;
    frameStats.temporal.temporalUpscalerDlssEvaluateSharpness =
        temporalUpscalerEvaluateStatus.sharpness;
    frameStats.temporal.temporalUpscalerDlssQualityEvaluateOutputReady =
        temporalUpscalerEvaluateStatus.outputReady;
    frameStats.temporal.temporalUpscalerDlssQualityReactiveMaskReady =
        temporalUpscalerEvaluateStatus.biasCurrentColorMaskReady;
    frameStats.temporal.temporalUpscalerDlssQualityTransparencyMaskReady =
        temporalUpscalerEvaluateStatus.transparencyMaskReady;
    frameStats.temporal.temporalUpscalerDlssQualityPostOrderingReady =
        temporalUpscalePostSourceStatus.active;
    FinalizeDlssQualityGateStats(frameStats.temporal);
    frameStats.temporal.temporalUpscaleEnabled =
        temporalUpscalerEvaluateStatus.outputReady;
    if (temporalUpscalerEvaluateStatus.outputReady > 0u) {
        frameStats.temporal.temporalUpscaleFallbackReason =
            static_cast<u32>(RendererTemporalUpscaleFallbackReason::None);
        frameStats.temporal.temporalConsumerActiveMask |=
            kTemporalConsumerUpscalerBit;
        frameStats.temporal.temporalConsumerUnsupportedMask &=
            ~kTemporalConsumerUpscalerBit;
    } else if (temporalUpscalerEvaluateStatus.attempted > 0u) {
        frameStats.temporal.temporalUpscaleFallbackReason =
            static_cast<u32>(
                RendererTemporalUpscaleFallbackReason::UpscalerEvaluateFailed
            );
        frameStats.temporal.temporalUpscaleEnabled = 0u;
    }
    frameStats.localShadowAtlas.recordedTilePasses =
        frameStats.binds.localShadowAtlasPasses;
    frameStats.localShadowAtlas.recordedDraws =
        frameStats.binds.localShadowAtlasDraws;
    frameStats.localShadowAtlas.recordedMeshBinds =
        frameStats.binds.localShadowAtlasMeshBinds;
    frameStats.weightedTranslucency.clearPasses =
        frameStats.binds.weightedTranslucencyClearPasses;
    frameStats.weightedTranslucency.draws =
        frameStats.binds.weightedTranslucencyDraws;
    frameStats.weightedTranslucency.sharedLightListDraws =
        frameStats.binds.weightedTranslucencySharedLightListDraws;
    frameStats.weightedTranslucency.shadowReadyDraws =
        frameStats.binds.weightedTranslucencyShadowReadyDraws;
    frameStats.weightedTranslucency.resolveDraws =
        frameStats.binds.weightedTranslucencyResolveDraws;
    if (imageIndex < m_LocalShadowCacheStates.size()) {
        LocalShadowCacheState& cacheState = m_LocalShadowCacheStates[imageIndex];
        for (u32 tileIndex = 0;
             tileIndex < localShadowTiles.assignedCount &&
             tileIndex < localShadowTiles.tiles.size() &&
             tileIndex < cacheState.tiles.size();
             ++tileIndex) {
            const LocalShadowTile& tile = localShadowTiles.tiles[tileIndex];
            cacheState.tiles[tileIndex] = LocalShadowCacheEntry{
                tile.cacheTileIdentity,
                tile.cacheLightSignature,
                tile.cacheCasterSignature
            };
        }
        cacheState.tileCount = localShadowTiles.assignedCount;
        cacheState.valid = localShadowTiles.assignedCount > 0;
    }
    if (imageIndex < m_LightTileGpuReadbackReady.size()) {
        m_LightTileGpuReadbackReady[imageIndex] =
            recordLightTileCullCompute ||
            frameStats.ssr.holeDiagnosticsActive > 0u;
    }

    sectionEnd = FrameClock::now();
    frameStats.cpu.commandRecordMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    sectionStart = sectionEnd;

    const VkSemaphore waitSemaphores[] = {
        imageAvailableSemaphore
    };

    const VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    const VkCommandBuffer commandBuffers[] = {
        m_CommandBuffer->Handle(imageIndex)
    };

    const VkSemaphore signalSemaphores[] = {
        renderFinishedSemaphore
    };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandBuffers;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_Device.GraphicsQueue(), 1, &submitInfo, currentFrameFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit Vulkan draw command buffer");
    }
    if (m_GpuTimer != nullptr) {
        m_GpuTimer->MarkFrameSubmitted(imageIndex);
    }

    const VkSwapchainKHR swapchains[] = {
        m_Swapchain->Handle()
    };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(m_Device.PresentQueue(), &presentInfo);

    sectionEnd = FrameClock::now();
    frameStats.cpu.submitPresentMs = ElapsedMilliseconds(sectionStart, sectionEnd);
    frameStats.cpu.totalFrameMs = ElapsedMilliseconds(frameStart, sectionEnd);
    m_LastStats = frameStats;

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        m_Window.WasResized()) {
        m_Window.ResetResizedFlag();
        RecreateSwapchain();
        return;
    }

    if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present Vulkan swapchain image");
    }

    StoreTemporalHistory(
        mainFrameMatrices.has_value() ? &*mainFrameMatrices : nullptr,
        sceneExtent,
        &temporalState
    );
    if (hdrCompositeAvailable && m_SceneRenderTargets != nullptr) {
        m_TemporalHistoryColorValid = true;
        m_PreviousTemporalHistoryImageIndex = imageIndex;
    }
    m_CurrentFrame = (m_CurrentFrame + 1) % VulkanSyncObjects::kMaxFramesInFlight;
}

void VulkanRenderer::WaitIdle() const {
    vkDeviceWaitIdle(m_Device.Handle());
}

void VulkanRenderer::SetFrameMatricesProvider(FrameMatricesProvider provider) {
    m_FrameMatricesProvider = std::move(provider);
}

void VulkanRenderer::SetRenderQueueBuilder(RenderQueueBuilder builder) {
    m_RenderQueueBuilder = std::move(builder);
}

void VulkanRenderer::SetDlssQualitySceneContentMotionSupported(bool supported) {
    m_DlssQualitySceneContentMotionSupported = supported;
}

void VulkanRenderer::SetTemporalAntialiasingMode(
    RendererTemporalAntialiasingMode mode
) {
    if (m_TemporalAntialiasingMode == mode) {
        return;
    }

    WaitIdle();
    const f32 materialMipLodBias = MaterialTextureMipLodBiasForTemporalMode(mode);
    const bool materialSamplersRecreated =
        m_RenderResources.RecreateMaterialSamplers(
            m_Device,
            m_PhysicalDevice,
            materialMipLodBias
        );
    if (materialSamplersRecreated && m_MaterialDescriptorSets != nullptr) {
        std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
        m_MaterialDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            materials,
            m_ShadowMap.get(),
            m_DirectionalShadowCascadeAtlas.get(),
            m_LocalShadowAtlas.get()
        );
        if (m_ReflectionCaptureMaterialDescriptorSets != nullptr) {
            m_ReflectionCaptureMaterialDescriptorSets->Recreate(
                m_Device,
                *m_MaterialDescriptorSetLayout,
                materials,
                m_ReflectionCaptureShadowMap.get(),
                nullptr,
                m_ReflectionCaptureLocalShadowAtlas.get()
            );
        }
    }
    m_TemporalAntialiasingMode = mode;
    m_TemporalHistoryValid = false;
    m_TemporalHistoryColorValid = false;
    m_PreviousTemporalHistoryImageIndex.reset();
    m_PreviousTemporalJitterPixels = glm::vec2(0.0f);
    m_PreviousTemporalJitterUv = glm::vec2(0.0f);
    m_PreviousTemporalJitterApplied = false;
    std::fill(
        m_TemporalUpscaleOutputInitialized.begin(),
        m_TemporalUpscaleOutputInitialized.end(),
        false
    );
    m_TemporalRenderTargetsRecreateRequested = true;
    ResetTemporalUpscalerFeatureCache();
}

void VulkanRenderer::ToggleTemporalAntialiasingMode() {
    switch (m_TemporalAntialiasingMode) {
    case RendererTemporalAntialiasingMode::NativeTaa:
        SetTemporalAntialiasingMode(RendererTemporalAntialiasingMode::DlssDlaa);
        break;
    case RendererTemporalAntialiasingMode::DlssDlaa:
        SetTemporalAntialiasingMode(
            RendererTemporalAntialiasingMode::DlssSrQuality
        );
        break;
    case RendererTemporalAntialiasingMode::DlssSrQuality:
        SetTemporalAntialiasingMode(
            RendererTemporalAntialiasingMode::DlssSrBalanced
        );
        break;
    case RendererTemporalAntialiasingMode::DlssSrBalanced:
        SetTemporalAntialiasingMode(
            RendererTemporalAntialiasingMode::DlssSrPerformance
        );
        break;
    case RendererTemporalAntialiasingMode::DlssSrPerformance:
        SetTemporalAntialiasingMode(RendererTemporalAntialiasingMode::NativeTaa);
        break;
    case RendererTemporalAntialiasingMode::Off:
        SetTemporalAntialiasingMode(RendererTemporalAntialiasingMode::NativeTaa);
        break;
    case RendererTemporalAntialiasingMode::Environment:
    default:
        SetTemporalAntialiasingMode(RendererTemporalAntialiasingMode::NativeTaa);
        break;
    }
}

RendererTemporalAntialiasingMode VulkanRenderer::TemporalAntialiasingMode() const {
    return m_TemporalAntialiasingMode;
}

void VulkanRenderer::SetImGui3DContext(Scene3D* scene, Camera3D* camera) {
    m_MainScene3D = scene;
    m_ImGuiScene3D = scene;
    m_ImGuiCamera3D = camera;
}

void VulkanRenderer::SetOverlay3DContext(
    Scene3D* scene,
    Camera3D* camera,
    PipelineSpec pipelineSpec
) {
    m_OverlayScene3D = scene;
    m_OverlayCamera3D = camera;
    m_OverlayPipelineSpec = std::move(pipelineSpec);

    if (m_Swapchain == nullptr) {
        return;
    }

    WaitIdle();
    m_OverlayUniformBuffer = std::make_unique<VulkanUniformBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_OverlayDescriptorSets = std::make_unique<VulkanDescriptorSets>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_OverlayUniformBuffer,
        *m_LightBuffer,
        *m_LightTileDiagnosticsBuffer,
        *m_MaterialBuffer,
        *m_ProbeGridBuffer,
        *m_DirectionalShadowCascadeBuffer,
        *m_LocalShadowBuffer,
        *m_AutoExposureBuffer
    );
    m_ReflectionProbeResources.SetDescriptorSetsBound(
        UpdateEnvironmentDescriptorSets(m_DescriptorSets.get()) +
            UpdateEnvironmentDescriptorSets(m_OverlayDescriptorSets.get())
    );
    m_OverlayGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        *m_OverlayPipelineSpec
    );
}

void VulkanRenderer::RefreshMaterialDescriptors() {
    WaitIdle();
    ReleaseReflectionCapturePersistentShadowSnapshots();
    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    m_MaterialDescriptorSets->Recreate(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        materials,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get()
    );
    if (m_ReflectionCaptureMaterialDescriptorSets != nullptr) {
        m_ReflectionCaptureMaterialDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            materials,
            m_ReflectionCaptureShadowMap.get(),
            nullptr,
            m_ReflectionCaptureLocalShadowAtlas.get()
        );
    }
}

VulkanRenderDebugSettings& VulkanRenderer::RenderDebugSettings() {
    return m_RenderDebugSettings;
}

const VulkanRenderDebugSettings& VulkanRenderer::RenderDebugSettings() const {
    return m_RenderDebugSettings;
}

const RendererStats& VulkanRenderer::Stats() const {
    return m_LastStats;
}

VulkanShadowSettings& VulkanRenderer::ShadowSettings() {
    return m_ShadowSettings;
}

const VulkanShadowSettings& VulkanRenderer::ShadowSettings() const {
    return m_ShadowSettings;
}

bool VulkanRenderer::TemporalDlssDlaaModeActive() const {
    return m_TemporalAntialiasingMode ==
        RendererTemporalAntialiasingMode::DlssDlaa;
}

bool VulkanRenderer::TemporalDlssSrModeActive() const {
    return m_TemporalAntialiasingMode ==
            RendererTemporalAntialiasingMode::DlssSrQuality ||
        m_TemporalAntialiasingMode ==
            RendererTemporalAntialiasingMode::DlssSrBalanced ||
        m_TemporalAntialiasingMode ==
            RendererTemporalAntialiasingMode::DlssSrPerformance;
}

bool VulkanRenderer::TemporalDlssModeActive() const {
    return TemporalDlssDlaaModeActive() || TemporalDlssSrModeActive();
}

bool VulkanRenderer::TemporalNativeTaaModeActive() const {
    return m_TemporalAntialiasingMode ==
        RendererTemporalAntialiasingMode::NativeTaa;
}

f32 VulkanRenderer::TemporalRenderScaleForCurrentMode() const {
    switch (m_TemporalAntialiasingMode) {
    case RendererTemporalAntialiasingMode::DlssSrQuality:
        return kDlssSrQualityScale;
    case RendererTemporalAntialiasingMode::DlssSrBalanced:
        return kDlssSrBalancedScale;
    case RendererTemporalAntialiasingMode::DlssSrPerformance:
        return kDlssSrPerformanceScale;
    case RendererTemporalAntialiasingMode::Environment:
        return TemporalRenderScaleFromEnvironment();
    case RendererTemporalAntialiasingMode::NativeTaa:
    case RendererTemporalAntialiasingMode::DlssDlaa:
    case RendererTemporalAntialiasingMode::Off:
    default:
        return 1.0f;
    }
}

bool VulkanRenderer::TemporalRenderScaleApplyEnabledForCurrentMode() const {
    if (TemporalDlssSrModeActive()) {
        return true;
    }
    if (m_TemporalAntialiasingMode == RendererTemporalAntialiasingMode::Environment) {
        return TemporalRenderScaleApplyEnabledFromEnvironment();
    }
    return false;
}

VkExtent2D VulkanRenderer::ActiveInternalExtentForDisplay(
    const VkExtent2D& displayExtent
) const {
    const bool temporalReconstructionAllowed =
        !DebugViewBypassesTemporalReconstruction(m_RenderDebugSettings.forwardView);
    return TemporalActiveInternalExtentForDisplay(
        displayExtent,
        temporalReconstructionAllowed ? TemporalRenderScaleForCurrentMode() : 1.0f,
        temporalReconstructionAllowed &&
            TemporalRenderScaleApplyEnabledForCurrentMode()
    );
}

TemporalUpscalerDlssQualityMode
VulkanRenderer::TemporalDlssQualityModeForCurrentMode() const {
    switch (m_TemporalAntialiasingMode) {
    case RendererTemporalAntialiasingMode::DlssDlaa:
        return TemporalUpscalerDlssQualityMode::Dlaa;
    case RendererTemporalAntialiasingMode::DlssSrQuality:
        return TemporalUpscalerDlssQualityMode::Quality;
    case RendererTemporalAntialiasingMode::DlssSrBalanced:
        return TemporalUpscalerDlssQualityMode::Balanced;
    case RendererTemporalAntialiasingMode::DlssSrPerformance:
        return TemporalUpscalerDlssQualityMode::Performance;
    case RendererTemporalAntialiasingMode::Environment:
    case RendererTemporalAntialiasingMode::NativeTaa:
    case RendererTemporalAntialiasingMode::Off:
    default:
        return TemporalUpscalerDlssQualityModeFromEnvironment();
    }
}

TemporalUpscalerDlssPreset
VulkanRenderer::TemporalDlssPresetForCurrentMode() const {
    const TemporalUpscalerDlssPreset overridePreset =
        TemporalUpscalerDlssPresetFromEnvironment();
    if (overridePreset != TemporalUpscalerDlssPreset::Default) {
        return overridePreset;
    }

    if (TemporalDlssModeActive()) {
        return TemporalUpscalerDlssPreset::L;
    }

    return TemporalUpscalerDlssPreset::Default;
}

bool VulkanRenderer::TemporalJitterEnabledForCurrentMode() const {
    return TemporalDlssModeActive() ||
        TemporalNativeTaaModeActive() ||
        TemporalJitterEnabledFromEnvironment();
}

bool VulkanRenderer::TemporalJitterApplyEnabledForCurrentMode() const {
    return TemporalDlssModeActive() ||
        TemporalNativeTaaModeActive() ||
        TemporalJitterApplicationEnabledFromEnvironment();
}

bool VulkanRenderer::TemporalVelocityJitteredHistoryPolicyForCurrentMode() const {
    return TemporalDlssModeActive() ||
        TemporalVelocityJitteredHistoryPolicyFromEnvironment();
}

f32 VulkanRenderer::ActiveMaterialTextureMipLodBias() const {
    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    if (materials.empty() || materials.front() == nullptr) {
        return MaterialTextureMipLodBiasFromEnvironment();
    }

    return materials.front()->Sampler().MipLodBias();
}

bool VulkanRenderer::SsrHiZResourcesReady() const {
    return m_ShadowSettings.ssrHiZEnabled &&
        m_SsrDepthPyramid != nullptr &&
        m_SsrDepthPyramid->Count() > 0 &&
        m_SsrDepthPyramid->MipCount() > 1 &&
        m_HiZDescriptorSets != nullptr &&
        m_HiZDescriptorSets->Count() == m_SsrDepthPyramid->Count() &&
        m_HiZDescriptorSets->MipCount() == m_SsrDepthPyramid->MipCount() &&
        m_HiZBuildComputePipeline != nullptr;
}

bool VulkanRenderer::SsrReconstructionResourcesReady() const {
    return m_ShadowSettings.ssrHiZEnabled &&
        m_SceneRenderTargets != nullptr &&
        m_SceneRenderTargets->Count() > 1u &&
        m_SsrReconstructionDescriptorSets != nullptr &&
        m_SsrReconstructionDescriptorSets->Count() ==
            m_SceneRenderTargets->Count() &&
        m_SsrTraceComputePipeline != nullptr &&
        m_SsrTemporalComputePipeline != nullptr &&
        m_SsrSpatialComputePipeline != nullptr;
}

void VulkanRenderer::ValidateSceneResources() const {
    if (m_Scene == nullptr) {
        return;
    }

    for (const Renderable2D* renderable : m_Scene->Renderables()) {
        SE_ASSERT(renderable != nullptr, "Scene contains a null renderable");
        SE_ASSERT(
            m_RenderResources.ContainsMesh(renderable->MeshId()),
            "Renderable2D references a mesh id that is not registered"
        );
        SE_ASSERT(
            m_RenderResources.ContainsMaterial(renderable->MaterialId()),
            "Renderable2D references a material id that is not registered"
        );
    }
}

void VulkanRenderer::CreateSwapchainResources() {
    m_Swapchain = std::make_unique<VulkanSwapchain>(m_Window, m_PhysicalDevice, m_Device, m_Surface);
    m_DescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device);
    m_MaterialDescriptorSetLayout = std::make_unique<VulkanMaterialDescriptorSetLayout>(m_Device);
    m_HiZDescriptorSetLayout = std::make_unique<VulkanHiZDescriptorSetLayout>(m_Device);
    m_SsrReconstructionDescriptorSetLayout =
        std::make_unique<VulkanSsrReconstructionDescriptorSetLayout>(m_Device);
    m_UniformBuffer = std::make_unique<VulkanUniformBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_LightBuffer = std::make_unique<VulkanLightBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_LightTileDiagnosticsBuffer = std::make_unique<VulkanLightTileDiagnosticsBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_AutoExposureBuffer = std::make_unique<VulkanAutoExposureBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_LightTileGpuReadbackReady.assign(m_Swapchain->Images().size(), false);
    m_MaterialBuffer = std::make_unique<VulkanMaterialBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_ProbeGridBuffer = std::make_unique<VulkanProbeGridBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_DirectionalShadowCascadeBuffer =
        std::make_unique<VulkanDirectionalShadowCascadeBuffer>(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    m_LocalShadowBuffer = std::make_unique<VulkanLocalShadowBuffer>(
        m_Device,
        m_PhysicalDevice,
        m_Swapchain->Images().size()
    );
    m_BonePaletteFallbackDescriptorSet =
        std::make_unique<VulkanBonePaletteFallbackDescriptorSet>(
            m_Device,
            m_PhysicalDevice
        );
    m_IblGenerationSettings = GlobalIblGenerationSettingsFromEnvironment();
    GenerateIblTextures(m_Device, m_PhysicalDevice, m_CommandPool,
        m_IblBrdfImage, m_IblIrradianceImage, m_IblPrefilteredImage,
        m_IblIrradianceView, m_IblPrefilteredView, m_IblSampler,
        m_IblGenerationSettings, &m_IblGenerationInfo);
    m_ReflectionProbeResources.CreateBuiltInProcedural(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool
    );
    EnsureVisibleSkyboxResources();
    m_DescriptorSets = std::make_unique<VulkanDescriptorSets>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_UniformBuffer,
        *m_LightBuffer,
        *m_LightTileDiagnosticsBuffer,
        *m_MaterialBuffer,
        *m_ProbeGridBuffer,
        *m_DirectionalShadowCascadeBuffer,
        *m_LocalShadowBuffer,
        *m_AutoExposureBuffer
    );
    m_ReflectionProbeResources.SetDescriptorSetsBound(
        UpdateEnvironmentDescriptorSets(m_DescriptorSets.get()) +
            UpdateEnvironmentDescriptorSets(m_OverlayDescriptorSets.get())
    );
    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    m_DepthBuffer = std::make_unique<VulkanDepthBuffer>(m_Device, m_PhysicalDevice, *m_Swapchain);
    const VkExtent2D sceneExtent =
        ActiveInternalExtentForDisplay(m_Swapchain->Extent());
    m_SceneRenderTargets = std::make_unique<VulkanSceneRenderTargets>(
        m_Device,
        m_PhysicalDevice,
        *m_Swapchain,
        sceneExtent
    );
    m_SsrDepthPyramid = std::make_unique<VulkanDepthPyramid>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        *m_Swapchain,
        sceneExtent
    );
    m_TemporalUpscaleOutputInitialized.assign(
        m_Swapchain->Images().size(),
        false
    );
    m_DlssMaskInputsInitialized.assign(
        m_Swapchain->Images().size(),
        false
    );
    m_BloomPyramid = std::make_unique<VulkanBloomPyramid>(
        m_Device,
        m_PhysicalDevice,
        *m_Swapchain
    );
    m_ColorGradingLut = std::make_unique<VulkanColorGradingLut>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool
    );
    m_SceneTargetSampler = std::make_unique<VulkanSampler>(
        m_Device,
        m_PhysicalDevice,
        m_SceneRenderTargets != nullptr
            ? std::max(1u, m_SceneRenderTargets->HdrSceneColorMipLevels())
            : 1u
    );
    m_SsrDepthPyramidSampler = std::make_unique<VulkanSampler>(
        m_Device,
        m_PhysicalDevice,
        m_SsrDepthPyramid->MipCount()
    );
    m_HdrRenderPass = std::make_unique<VulkanHdrRenderPass>(
        m_Device,
        *m_SceneRenderTargets
    );
    m_BloomDownsampleRenderPass = std::make_unique<VulkanBloomRenderPass>(
        m_Device,
        m_BloomPyramid->BloomFormat(),
        false
    );
    m_BloomUpsampleRenderPass = std::make_unique<VulkanBloomRenderPass>(
        m_Device,
        m_BloomPyramid->BloomFormat(),
        true
    );
    m_WeightedTranslucencyRenderPass =
        std::make_unique<VulkanWeightedTranslucencyRenderPass>(
            m_Device,
            *m_SceneRenderTargets
        );
    m_GBufferRenderPass = std::make_unique<VulkanGBufferRenderPass>(
        m_Device,
        *m_SceneRenderTargets
    );
    m_ForwardResidualVelocityRenderPass =
        std::make_unique<VulkanForwardResidualVelocityRenderPass>(
            m_Device,
            *m_SceneRenderTargets
        );
    m_DlssMaskRenderPass =
        std::make_unique<VulkanDlssMaskRenderPass>(
            m_Device,
            *m_SceneRenderTargets
        );
    m_HdrFramebuffer = std::make_unique<VulkanHdrFramebuffer>(
        m_Device,
        *m_HdrRenderPass,
        *m_SceneRenderTargets
    );
    m_TaaResolveFramebuffer = std::make_unique<VulkanHdrFramebuffer>(
        m_Device,
        *m_HdrRenderPass,
        *m_SceneRenderTargets,
        true
    );
    m_BloomDownsampleFramebuffer = std::make_unique<VulkanBloomFramebuffer>(
        m_Device,
        *m_BloomDownsampleRenderPass,
        *m_BloomPyramid
    );
    m_BloomUpsampleFramebuffer = std::make_unique<VulkanBloomFramebuffer>(
        m_Device,
        *m_BloomUpsampleRenderPass,
        *m_BloomPyramid
    );
    m_WeightedTranslucencyFramebuffer =
        std::make_unique<VulkanWeightedTranslucencyFramebuffer>(
            m_Device,
            *m_WeightedTranslucencyRenderPass,
            *m_SceneRenderTargets
        );
    m_GBufferFramebuffer = std::make_unique<VulkanGBufferFramebuffer>(
        m_Device,
        *m_GBufferRenderPass,
        *m_SceneRenderTargets
    );
    m_ForwardResidualVelocityFramebuffer =
        std::make_unique<VulkanForwardResidualVelocityFramebuffer>(
            m_Device,
            *m_ForwardResidualVelocityRenderPass,
            *m_SceneRenderTargets
        );
    m_DlssMaskFramebuffer =
        std::make_unique<VulkanDlssMaskFramebuffer>(
            m_Device,
            *m_DlssMaskRenderPass,
            *m_SceneRenderTargets
        );
    m_ShadowMap = std::make_unique<VulkanShadowMap>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        m_Swapchain->Images().size(),
        m_ShadowSettings.mapSize
    );
    m_DirectionalShadowCascadeAtlas =
        std::make_unique<VulkanDirectionalShadowCascadeAtlas>(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            m_ShadowSettings.mapSize
        );
    m_LocalShadowAtlas = std::make_unique<VulkanLocalShadowAtlas>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        m_Swapchain->Images().size(),
        LocalShadowAtlasTileSizeFor(m_ShadowSettings),
        LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
    );
    ResetLocalShadowCacheStates();
    m_ShadowRenderPass = std::make_unique<VulkanShadowRenderPass>(
        m_Device,
        *m_ShadowMap
    );
    m_ReflectionCaptureShadowMap = std::make_unique<VulkanShadowMap>(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        1u,
        m_ShadowSettings.mapSize
    );
    m_ReflectionCaptureLocalShadowAtlas =
        std::make_unique<VulkanLocalShadowAtlas>(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            1u,
            LocalShadowAtlasTileSizeFor(m_ShadowSettings),
            LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
        );
    m_ShadowFramebuffer = std::make_unique<VulkanShadowFramebuffer>(
        m_Device,
        *m_ShadowRenderPass,
        *m_ShadowMap
    );
    m_DirectionalShadowCascadeFramebuffer =
        std::make_unique<VulkanShadowFramebuffer>(
            m_Device,
            *m_ShadowRenderPass,
            *m_DirectionalShadowCascadeAtlas
        );
    m_LocalShadowFramebuffer =
        std::make_unique<VulkanShadowFramebuffer>(
            m_Device,
            *m_ShadowRenderPass,
            *m_LocalShadowAtlas
        );
    m_ReflectionCaptureShadowFramebuffer =
        std::make_unique<VulkanShadowFramebuffer>(
            m_Device,
            *m_ShadowRenderPass,
            *m_ReflectionCaptureShadowMap
        );
    m_ReflectionCaptureLocalShadowFramebuffer =
        std::make_unique<VulkanShadowFramebuffer>(
            m_Device,
            *m_ShadowRenderPass,
            *m_ReflectionCaptureLocalShadowAtlas
        );
    m_MaterialDescriptorSets = std::make_unique<VulkanMaterialDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        materials,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get()
    );
    m_ReflectionCaptureMaterialDescriptorSets =
        std::make_unique<VulkanMaterialDescriptorSets>(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            materials,
            m_ReflectionCaptureShadowMap.get(),
            nullptr,
            m_ReflectionCaptureLocalShadowAtlas.get()
        );
    m_GBufferDescriptorSets = std::make_unique<VulkanGBufferDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        *m_SceneRenderTargets,
        *m_SceneTargetSampler,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get(),
        m_SsrDepthPyramid.get()
    );
    m_HiZDescriptorSets = std::make_unique<VulkanHiZDescriptorSets>(
        m_Device,
        *m_HiZDescriptorSetLayout,
        *m_SceneRenderTargets,
        *m_SsrDepthPyramid,
        *m_SsrDepthPyramidSampler
    );
    m_SsrReconstructionDescriptorSets =
        std::make_unique<VulkanSsrReconstructionDescriptorSets>(
            m_Device,
            *m_SsrReconstructionDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SsrDepthPyramid,
            *m_SceneTargetSampler
        );
    m_HdrDescriptorSets = std::make_unique<VulkanHdrDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        *m_SceneRenderTargets,
        m_BloomPyramid.get(),
        m_ColorGradingLut.get(),
        *m_SceneTargetSampler
    );
    m_TemporalUpscaleHdrDescriptorSets =
        std::make_unique<VulkanHdrDescriptorSets>(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            m_BloomPyramid.get(),
            m_ColorGradingLut.get(),
            *m_SceneTargetSampler,
            true
        );
    m_BloomDescriptorSets = std::make_unique<VulkanBloomDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        *m_SceneRenderTargets,
        *m_BloomPyramid,
        *m_SceneTargetSampler
    );
    m_TemporalUpscaleBloomDescriptorSets =
        std::make_unique<VulkanBloomDescriptorSets>(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_BloomPyramid,
            *m_SceneTargetSampler,
            true
        );
    m_WeightedTranslucencyDescriptorSets =
        std::make_unique<VulkanWeightedTranslucencyDescriptorSets>(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SceneTargetSampler
        );
    m_RenderPass = std::make_unique<VulkanRenderPass>(m_Device, *m_Swapchain, *m_DepthBuffer);
    m_DepthLoadRenderPass = std::make_unique<VulkanRenderPass>(
        m_Device,
        *m_Swapchain,
        *m_DepthBuffer,
        true
    );
    m_ImGuiLayer = std::make_unique<VulkanImGuiLayer>(
        m_Window,
        m_Instance,
        m_PhysicalDevice,
        m_Device,
        *m_RenderPass,
        *m_Swapchain
    );
    m_GraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        m_PipelineSpec
    );
    m_DoubleSidedGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        PipelineSpec::DoubleSided(m_PipelineSpec)
    );
    if (m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D ||
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3DInstanced) {
        const std::string depthPrefillShaderPath =
            std::string(SE_SHADER_DIR) + "/depth_prefill_3d.vert.spv";
        const PipelineSpec depthPrefillSpec =
            PipelineSpec::DepthPrefill3D(depthPrefillShaderPath);
        m_DepthPrefillGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            depthPrefillSpec
        );
        m_DoubleSidedDepthPrefillGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::DoubleSided(depthPrefillSpec)
        );
        const std::string weightedTranslucencyFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/weighted_translucency_3d.frag.spv";
        const PipelineSpec weightedTranslucencySpec =
            PipelineSpec::WeightedTranslucency3D(
                m_PipelineSpec.vertexShaderPath,
                weightedTranslucencyFragmentShaderPath
            );
        m_WeightedTranslucencyGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_WeightedTranslucencyRenderPass->Handle(),
                *m_Swapchain,
                weightedTranslucencySpec
            );
        m_DoubleSidedWeightedTranslucencyGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_WeightedTranslucencyRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(weightedTranslucencySpec)
            );
        const PipelineSpec forwardResidualSpec =
            PipelineSpec::ForwardResidual3D(
                m_PipelineSpec.vertexShaderPath,
                m_PipelineSpec.fragmentShaderPath
            );
        const std::string gBufferVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.vert.spv";
        const std::string forwardVelocityFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/forward_velocity_3d.frag.spv";
        const PipelineSpec forwardResidualVelocitySpec =
            PipelineSpec::ForwardResidualVelocity3D(
                gBufferVertexShaderPath,
                forwardVelocityFragmentShaderPath
            );
        const std::string dlssMaskFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/dlss_mask_3d.frag.spv";
        const PipelineSpec dlssMaskSpec =
            PipelineSpec::DlssMask3D(
                gBufferVertexShaderPath,
                dlssMaskFragmentShaderPath
            );
        m_ForwardResidualVelocityGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_ForwardResidualVelocityRenderPass->Handle(),
                *m_Swapchain,
                forwardResidualVelocitySpec
            );
        m_DoubleSidedForwardResidualVelocityGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_ForwardResidualVelocityRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(forwardResidualVelocitySpec)
            );
        m_DlssMaskGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_DlssMaskRenderPass->Handle(),
                *m_Swapchain,
                dlssMaskSpec
            );
        m_DoubleSidedDlssMaskGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_DlssMaskRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(dlssMaskSpec)
            );
        m_ForwardResidualHdrGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_HdrRenderPass->Handle(),
            *m_Swapchain,
            forwardResidualSpec
        );
        m_DoubleSidedForwardResidualHdrGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_HdrRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(forwardResidualSpec)
            );
        m_ForwardResidualGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            forwardResidualSpec
        );
        m_DoubleSidedForwardResidualGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::DoubleSided(forwardResidualSpec)
        );
    } else {
        m_DepthPrefillGraphicsPipeline.reset();
        m_DoubleSidedDepthPrefillGraphicsPipeline.reset();
        m_WeightedTranslucencyGraphicsPipeline.reset();
        m_DoubleSidedWeightedTranslucencyGraphicsPipeline.reset();
        m_ForwardResidualVelocityGraphicsPipeline.reset();
        m_DoubleSidedForwardResidualVelocityGraphicsPipeline.reset();
        m_DlssMaskGraphicsPipeline.reset();
        m_DoubleSidedDlssMaskGraphicsPipeline.reset();
        m_ForwardResidualHdrGraphicsPipeline.reset();
        m_DoubleSidedForwardResidualHdrGraphicsPipeline.reset();
        m_ForwardResidualGraphicsPipeline.reset();
        m_DoubleSidedForwardResidualGraphicsPipeline.reset();
    }
    const std::string hdrCompositeVertexShaderPath =
        std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
    const std::string hdrCompositeFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/hdr_composite.frag.spv";
    const std::string bloomDownsampleFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/bloom_downsample.frag.spv";
    const std::string bloomUpsampleFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/bloom_upsample.frag.spv";
    m_BloomDownsamplePipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_BloomDownsampleRenderPass->Handle(),
        *m_Swapchain,
        PipelineSpec::BloomPyramid(
            hdrCompositeVertexShaderPath,
            bloomDownsampleFragmentShaderPath
        )
    );
    m_BloomUpsamplePipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_BloomUpsampleRenderPass->Handle(),
        *m_Swapchain,
        PipelineSpec::BloomUpsample(
            hdrCompositeVertexShaderPath,
            bloomUpsampleFragmentShaderPath
        )
    );
    m_HdrCompositePipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        PipelineSpec::HdrComposite(
            hdrCompositeVertexShaderPath,
            hdrCompositeFragmentShaderPath
        )
    );
    const std::string taaResolveFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/taa_resolve.frag.spv";
    m_TaaResolvePipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_HdrRenderPass->Handle(),
        *m_Swapchain,
        PipelineSpec::HdrComposite(
            hdrCompositeVertexShaderPath,
            taaResolveFragmentShaderPath
        )
    );
    const std::string weightedTranslucencyResolveFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/weighted_translucency_resolve.frag.spv";
    m_WeightedTranslucencyResolvePipeline =
        std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_HdrRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::WeightedTranslucencyResolve(
                hdrCompositeVertexShaderPath,
                weightedTranslucencyResolveFragmentShaderPath
            )
        );
    const std::string gBufferDebugVertexShaderPath =
        std::string(SE_SHADER_DIR) + "/gbuffer_debug.vert.spv";
    const std::string gBufferDebugFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/gbuffer_debug.frag.spv";
    m_GBufferDebugPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        PipelineSpec::GBufferDebug(
            gBufferDebugVertexShaderPath,
            gBufferDebugFragmentShaderPath
        )
    );
    if (m_PipelineSpec.supportsInstancing && !m_PipelineSpec.instancedVertexShaderPath.empty()) {
        PipelineSpec instancedSpec = m_PipelineSpec;
        instancedSpec.vertexShaderPath = m_PipelineSpec.instancedVertexShaderPath;
        instancedSpec.vertexLayout = VertexLayout::Vertex3DInstanced;
        m_InstancedGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            instancedSpec
        );
        m_DoubleSidedInstancedGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::DoubleSided(instancedSpec)
        );
        m_InstanceBuffer = std::make_unique<VulkanInstanceBuffer>(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
        m_MainInstanceUploadSignatures.clear();
    } else {
        m_InstancedGraphicsPipeline.reset();
        m_DoubleSidedInstancedGraphicsPipeline.reset();
        m_InstanceBuffer.reset();
        m_MainInstanceUploadSignatures.clear();
    }
    if (m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D ||
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3DInstanced) {
        const std::string gBufferVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.vert.spv";
        const std::string gBufferFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.frag.spv";
        const PipelineSpec gBufferSpec =
            PipelineSpec::GBuffer3D(
                gBufferVertexShaderPath,
                gBufferFragmentShaderPath
            );
        m_GBufferGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_GBufferRenderPass->Handle(),
            *m_Swapchain,
            gBufferSpec
        );
        m_DoubleSidedGBufferGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_GBufferRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::DoubleSided(gBufferSpec)
        );
    } else {
        m_GBufferGraphicsPipeline.reset();
        m_DoubleSidedGBufferGraphicsPipeline.reset();
    }
    const std::string deferredLightingVertexShaderPath =
        std::string(SE_SHADER_DIR) + "/deferred_lighting.vert.spv";
    const std::string deferredLightingFragmentShaderPath =
        std::string(SE_SHADER_DIR) + "/deferred_lighting.frag.spv";
    m_DeferredLightingPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_HdrRenderPass->Handle(),
        *m_Swapchain,
        PipelineSpec::DeferredLighting(
            deferredLightingVertexShaderPath,
            deferredLightingFragmentShaderPath
        )
    );
    if (m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D ||
        m_PipelineSpec.vertexLayout == VertexLayout::Vertex3DInstanced) {
        const std::string lightTileCullShaderPath =
            std::string(SE_SHADER_DIR) + "/light_tile_cull.comp.spv";
        m_LightTileCullComputePipeline = std::make_unique<VulkanComputePipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            lightTileCullShaderPath
        );
        m_LightClusterCullComputePipeline = std::make_unique<VulkanComputePipeline>(
            m_Device, *m_DescriptorSetLayout,
            std::string(SE_SHADER_DIR) + "/light_cluster_cull.comp.spv");
    } else {
        m_LightTileCullComputePipeline.reset();
        m_LightClusterCullComputePipeline.reset();
    }
    m_AutoExposureComputePipeline = std::make_unique<VulkanComputePipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        std::string(SE_SHADER_DIR) + "/auto_exposure_histogram.comp.spv"
    );
    m_HiZBuildComputePipeline = std::make_unique<VulkanComputePipeline>(
        m_Device,
        *m_HiZDescriptorSetLayout,
        std::string(SE_SHADER_DIR) + "/ssr_depth_pyramid.comp.spv"
    );
    m_SsrTraceComputePipeline = std::make_unique<VulkanComputePipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_SsrReconstructionDescriptorSetLayout,
        std::string(SE_SHADER_DIR) + "/ssr_trace.comp.spv"
    );
    m_SsrTemporalComputePipeline = std::make_unique<VulkanComputePipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_SsrReconstructionDescriptorSetLayout,
        std::string(SE_SHADER_DIR) + "/ssr_temporal.comp.spv"
    );
    m_SsrSpatialComputePipeline = std::make_unique<VulkanComputePipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_SsrReconstructionDescriptorSetLayout,
        std::string(SE_SHADER_DIR) + "/ssr_spatial.comp.spv"
    );
#if !defined(NDEBUG)
    m_SsrDiagnosticsComputePipeline = std::make_unique<VulkanComputePipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_SsrReconstructionDescriptorSetLayout,
        std::string(SE_SHADER_DIR) + "/ssr_diagnostics.comp.spv"
    );
#endif
    const std::string shadowShaderPath = std::string(SE_SHADER_DIR) + "/shadow_depth.vert.spv";
    const PipelineSpec shadowSpec =
        PipelineSpec::ShadowDepth(shadowShaderPath, m_ShadowMap->Extent());
    m_ShadowGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_ShadowRenderPass->Handle(),
        *m_Swapchain,
        shadowSpec
    );
    m_DoubleSidedShadowGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_ShadowRenderPass->Handle(),
        *m_Swapchain,
        PipelineSpec::DoubleSided(shadowSpec)
    );
    m_Framebuffer = std::make_unique<VulkanFramebuffer>(
        m_Device,
        *m_RenderPass,
        *m_DepthBuffer,
        *m_Swapchain
    );
    m_DepthLoadFramebuffer = std::make_unique<VulkanFramebuffer>(
        m_Device,
        *m_DepthLoadRenderPass,
        *m_DepthBuffer,
        *m_Swapchain
    );
    m_CommandBuffer = std::make_unique<VulkanCommandBuffer>(
        m_Device,
        m_CommandPool,
        *m_Framebuffer
    );
    if (GpuTimestampsEnabledFromEnvironment()) {
        m_GpuTimer = std::make_unique<VulkanGpuTimer>(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    } else {
        m_GpuTimer.reset();
    }
    m_SyncObjects = std::make_unique<VulkanSyncObjects>(m_Device, m_Swapchain->Images().size());
}

void VulkanRenderer::RecreateSwapchain() {
    m_Window.WaitForValidFramebufferSize();

    if (m_Window.ShouldClose() || m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0) {
        return;
    }

    m_Window.ResetResizedFlag();
    vkDeviceWaitIdle(m_Device.Handle());
    ResetReflectionCaptureShadowSnapshot();
    ReleaseReflectionCapturePersistentShadowSnapshots();

    m_CommandBuffer->Release();
    if (m_GpuTimer != nullptr) {
        m_GpuTimer->Release();
    }
    m_Framebuffer->Release();
    if (m_DepthLoadFramebuffer != nullptr) {
        m_DepthLoadFramebuffer->Release();
    }
    if (m_OverlayGraphicsPipeline != nullptr) {
        m_OverlayGraphicsPipeline->Release();
    }
    if (m_InstancedGraphicsPipeline != nullptr) {
        m_InstancedGraphicsPipeline->Release();
    }
    if (m_DoubleSidedInstancedGraphicsPipeline != nullptr) {
        m_DoubleSidedInstancedGraphicsPipeline->Release();
    }
    if (m_GBufferDebugPipeline != nullptr) {
        m_GBufferDebugPipeline->Release();
    }
    if (m_BloomUpsamplePipeline != nullptr) {
        m_BloomUpsamplePipeline->Release();
    }
    if (m_BloomDownsamplePipeline != nullptr) {
        m_BloomDownsamplePipeline->Release();
    }
    if (m_WeightedTranslucencyResolvePipeline != nullptr) {
        m_WeightedTranslucencyResolvePipeline->Release();
    }
    if (m_TaaResolvePipeline != nullptr) {
        m_TaaResolvePipeline->Release();
    }
    if (m_DoubleSidedDepthPrefillGraphicsPipeline != nullptr) {
        m_DoubleSidedDepthPrefillGraphicsPipeline->Release();
    }
    if (m_DepthPrefillGraphicsPipeline != nullptr) {
        m_DepthPrefillGraphicsPipeline->Release();
    }
    if (m_DoubleSidedWeightedTranslucencyGraphicsPipeline != nullptr) {
        m_DoubleSidedWeightedTranslucencyGraphicsPipeline->Release();
    }
    if (m_WeightedTranslucencyGraphicsPipeline != nullptr) {
        m_WeightedTranslucencyGraphicsPipeline->Release();
    }
    if (m_DoubleSidedForwardResidualVelocityGraphicsPipeline != nullptr) {
        m_DoubleSidedForwardResidualVelocityGraphicsPipeline->Release();
    }
    if (m_ForwardResidualVelocityGraphicsPipeline != nullptr) {
        m_ForwardResidualVelocityGraphicsPipeline->Release();
    }
    if (m_DoubleSidedDlssMaskGraphicsPipeline != nullptr) {
        m_DoubleSidedDlssMaskGraphicsPipeline->Release();
    }
    if (m_DlssMaskGraphicsPipeline != nullptr) {
        m_DlssMaskGraphicsPipeline->Release();
    }
    if (m_DoubleSidedForwardResidualHdrGraphicsPipeline != nullptr) {
        m_DoubleSidedForwardResidualHdrGraphicsPipeline->Release();
    }
    if (m_ForwardResidualHdrGraphicsPipeline != nullptr) {
        m_ForwardResidualHdrGraphicsPipeline->Release();
    }
    if (m_DoubleSidedForwardResidualGraphicsPipeline != nullptr) {
        m_DoubleSidedForwardResidualGraphicsPipeline->Release();
    }
    if (m_ForwardResidualGraphicsPipeline != nullptr) {
        m_ForwardResidualGraphicsPipeline->Release();
    }
    if (m_HdrCompositePipeline != nullptr) {
        m_HdrCompositePipeline->Release();
    }
    if (m_DeferredLightingPipeline != nullptr) {
        m_DeferredLightingPipeline->Release();
    }
    if (m_LightTileCullComputePipeline != nullptr) {
        m_LightTileCullComputePipeline->Release();
    }
    if (m_AutoExposureComputePipeline != nullptr) {
        m_AutoExposureComputePipeline->Release();
    }
    if (m_GBufferGraphicsPipeline != nullptr) {
        m_GBufferGraphicsPipeline->Release();
    }
    if (m_DoubleSidedGBufferGraphicsPipeline != nullptr) {
        m_DoubleSidedGBufferGraphicsPipeline->Release();
    }
    if (m_ShadowGraphicsPipeline != nullptr) {
        m_ShadowGraphicsPipeline->Release();
    }
    if (m_DoubleSidedShadowGraphicsPipeline != nullptr) {
        m_DoubleSidedShadowGraphicsPipeline->Release();
    }
    m_GraphicsPipeline->Release();
    if (m_DoubleSidedGraphicsPipeline != nullptr) {
        m_DoubleSidedGraphicsPipeline->Release();
    }
    m_ImGuiLayer.reset();
    m_RenderPass->Release();
    if (m_DepthLoadRenderPass != nullptr) {
        m_DepthLoadRenderPass->Release();
    }
    if (m_GBufferFramebuffer != nullptr) {
        m_GBufferFramebuffer->Release();
    }
    if (m_GBufferRenderPass != nullptr) {
        m_GBufferRenderPass->Release();
    }
    if (m_ForwardResidualVelocityFramebuffer != nullptr) {
        m_ForwardResidualVelocityFramebuffer->Release();
    }
    if (m_ForwardResidualVelocityRenderPass != nullptr) {
        m_ForwardResidualVelocityRenderPass->Release();
    }
    if (m_DlssMaskFramebuffer != nullptr) {
        m_DlssMaskFramebuffer->Release();
    }
    if (m_DlssMaskRenderPass != nullptr) {
        m_DlssMaskRenderPass->Release();
    }
    if (m_HdrFramebuffer != nullptr) {
        m_HdrFramebuffer->Release();
    }
    if (m_TaaResolveFramebuffer != nullptr) {
        m_TaaResolveFramebuffer->Release();
    }
    if (m_HdrRenderPass != nullptr) {
        m_HdrRenderPass->Release();
    }
    if (m_BloomUpsampleFramebuffer != nullptr) {
        m_BloomUpsampleFramebuffer->Release();
    }
    if (m_BloomDownsampleFramebuffer != nullptr) {
        m_BloomDownsampleFramebuffer->Release();
    }
    if (m_BloomUpsampleRenderPass != nullptr) {
        m_BloomUpsampleRenderPass->Release();
    }
    if (m_BloomDownsampleRenderPass != nullptr) {
        m_BloomDownsampleRenderPass->Release();
    }
    if (m_WeightedTranslucencyFramebuffer != nullptr) {
        m_WeightedTranslucencyFramebuffer->Release();
    }
    if (m_WeightedTranslucencyRenderPass != nullptr) {
        m_WeightedTranslucencyRenderPass->Release();
    }
    if (m_ShadowFramebuffer != nullptr) {
        m_ShadowFramebuffer->Release();
    }
    if (m_DirectionalShadowCascadeFramebuffer != nullptr) {
        m_DirectionalShadowCascadeFramebuffer->Release();
    }
    if (m_LocalShadowFramebuffer != nullptr) {
        m_LocalShadowFramebuffer->Release();
    }
    if (m_ReflectionCaptureShadowFramebuffer != nullptr) {
        m_ReflectionCaptureShadowFramebuffer->Release();
    }
    if (m_ReflectionCaptureLocalShadowFramebuffer != nullptr) {
        m_ReflectionCaptureLocalShadowFramebuffer->Release();
    }
    if (m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeAtlas->Release();
    }
    if (m_LocalShadowAtlas != nullptr) {
        m_LocalShadowAtlas->Release();
    }
    m_DepthBuffer->Release();
    if (m_SceneRenderTargets != nullptr) {
        m_SceneRenderTargets->Release();
    }
    if (m_BloomPyramid != nullptr) {
        m_BloomPyramid->Release();
    }
    if (m_ColorGradingLut != nullptr) {
        m_ColorGradingLut->Release();
    }
    if (m_MaterialDescriptorSets != nullptr) {
        m_MaterialDescriptorSets->Release();
    }
    if (m_ReflectionCaptureMaterialDescriptorSets != nullptr) {
        m_ReflectionCaptureMaterialDescriptorSets->Release();
    }
    if (m_GBufferDescriptorSets != nullptr) {
        m_GBufferDescriptorSets->Release();
    }
    if (m_HdrDescriptorSets != nullptr) {
        m_HdrDescriptorSets->Release();
    }
    if (m_TemporalUpscaleHdrDescriptorSets != nullptr) {
        m_TemporalUpscaleHdrDescriptorSets->Release();
    }
    if (m_BloomDescriptorSets != nullptr) {
        m_BloomDescriptorSets->Release();
    }
    if (m_TemporalUpscaleBloomDescriptorSets != nullptr) {
        m_TemporalUpscaleBloomDescriptorSets->Release();
    }
    if (m_WeightedTranslucencyDescriptorSets != nullptr) {
        m_WeightedTranslucencyDescriptorSets->Release();
    }
    if (m_OverlayDescriptorSets != nullptr) {
        m_OverlayDescriptorSets->Release();
    }
    m_DescriptorSets->Release();
    if (m_OverlayUniformBuffer != nullptr) {
        m_OverlayUniformBuffer->Release();
    }
    m_UniformBuffer->Release();
    if (m_LightBuffer != nullptr) {
        m_LightBuffer->Release();
    }
    if (m_LightTileDiagnosticsBuffer != nullptr) {
        m_LightTileDiagnosticsBuffer->Release();
    }
    if (m_AutoExposureBuffer != nullptr) {
        m_AutoExposureBuffer->Release();
    }
    if (m_MaterialBuffer != nullptr) {
        m_MaterialBuffer->Release();
    }
    if (m_ProbeGridBuffer != nullptr) {
        m_ProbeGridBuffer->Release();
    }
    if (m_DirectionalShadowCascadeBuffer != nullptr) {
        m_DirectionalShadowCascadeBuffer->Release();
    }
    if (m_LocalShadowBuffer != nullptr) {
        m_LocalShadowBuffer->Release();
    }
    m_Swapchain->Release();

    m_Swapchain->Recreate(m_Window, m_PhysicalDevice, m_Device, m_Surface);
    m_UniformBuffer->Recreate(m_Device, m_PhysicalDevice, m_Swapchain->Images().size());
    if (m_LightBuffer != nullptr) {
        m_LightBuffer->Recreate(m_Device, m_PhysicalDevice, m_Swapchain->Images().size());
    }
    if (m_LightTileDiagnosticsBuffer != nullptr) {
        m_LightTileDiagnosticsBuffer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
    if (m_AutoExposureBuffer != nullptr) {
        m_AutoExposureBuffer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
    m_LightTileGpuReadbackReady.assign(m_Swapchain->Images().size(), false);
    if (m_MaterialBuffer != nullptr) {
        m_MaterialBuffer->Recreate(m_Device, m_PhysicalDevice, m_Swapchain->Images().size());
    }
    if (m_ProbeGridBuffer != nullptr) {
        m_ProbeGridBuffer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
    if (m_DirectionalShadowCascadeBuffer != nullptr) {
        m_DirectionalShadowCascadeBuffer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
    if (m_LocalShadowBuffer != nullptr) {
        m_LocalShadowBuffer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
    m_DescriptorSets->Recreate(
        m_Device,
        *m_DescriptorSetLayout,
        *m_UniformBuffer,
        *m_LightBuffer,
        *m_LightTileDiagnosticsBuffer,
        *m_MaterialBuffer,
        *m_ProbeGridBuffer,
        *m_DirectionalShadowCascadeBuffer,
        *m_LocalShadowBuffer,
        *m_AutoExposureBuffer
    );
    m_ReflectionProbeResources.SetDescriptorSetsBound(
        UpdateEnvironmentDescriptorSets(m_DescriptorSets.get())
    );
    m_SyncObjects->RecreateSwapchainSyncObjects(m_Swapchain->Images().size());
    m_DepthBuffer->Recreate(m_Device, m_PhysicalDevice, *m_Swapchain);
    if (m_SceneRenderTargets != nullptr) {
        const VkExtent2D sceneExtent =
            ActiveInternalExtentForDisplay(m_Swapchain->Extent());
        m_SceneRenderTargets->Recreate(
            m_Device,
            m_PhysicalDevice,
            *m_Swapchain,
            sceneExtent
        );
        m_TemporalHistoryColorValid = false;
        m_PreviousTemporalHistoryImageIndex.reset();
        m_SsrReconstructionImagesInitialized = false;
        m_TemporalUpscaleOutputInitialized.assign(
            m_Swapchain->Images().size(),
            false
        );
        m_DlssMaskInputsInitialized.assign(
            m_Swapchain->Images().size(),
            false
        );
    }
    if (m_SceneTargetSampler != nullptr && m_SceneRenderTargets != nullptr) {
        m_SceneTargetSampler->Recreate(
            m_Device,
            m_PhysicalDevice,
            std::max(1u, m_SceneRenderTargets->HdrSceneColorMipLevels())
        );
    }
    if (m_SsrDepthPyramid != nullptr) {
        m_SsrDepthPyramid->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            *m_Swapchain,
            ActiveInternalExtentForDisplay(m_Swapchain->Extent())
        );
    }
    if (m_SsrDepthPyramidSampler != nullptr && m_SsrDepthPyramid != nullptr) {
        m_SsrDepthPyramidSampler->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_SsrDepthPyramid->MipCount()
        );
    }
    if (m_BloomPyramid != nullptr) {
        m_BloomPyramid->Recreate(
            m_Device,
            m_PhysicalDevice,
            *m_Swapchain
        );
    }
    if (m_ColorGradingLut != nullptr) {
        m_ColorGradingLut->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool
        );
    }
    if (m_HdrRenderPass != nullptr && m_SceneRenderTargets != nullptr) {
        m_HdrRenderPass->Recreate(
            m_Device,
            *m_SceneRenderTargets
        );
    }
    if (m_BloomDownsampleRenderPass != nullptr && m_BloomPyramid != nullptr) {
        m_BloomDownsampleRenderPass->Recreate(
            m_Device,
            m_BloomPyramid->BloomFormat(),
            false
        );
    }
    if (m_BloomUpsampleRenderPass != nullptr && m_BloomPyramid != nullptr) {
        m_BloomUpsampleRenderPass->Recreate(
            m_Device,
            m_BloomPyramid->BloomFormat(),
            true
        );
    }
    if (m_WeightedTranslucencyRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_WeightedTranslucencyRenderPass->Recreate(
            m_Device,
            *m_SceneRenderTargets
        );
    }
    if (m_GBufferRenderPass != nullptr && m_SceneRenderTargets != nullptr) {
        m_GBufferRenderPass->Recreate(
            m_Device,
            *m_SceneRenderTargets
        );
    }
    if (m_ForwardResidualVelocityRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_ForwardResidualVelocityRenderPass->Recreate(
            m_Device,
            *m_SceneRenderTargets
        );
    }
    if (m_DlssMaskRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_DlssMaskRenderPass->Recreate(
            m_Device,
            *m_SceneRenderTargets
        );
    }
    if (m_HdrFramebuffer != nullptr &&
        m_HdrRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_HdrFramebuffer->Recreate(
            m_Device,
            *m_HdrRenderPass,
            *m_SceneRenderTargets
        );
    }
    if (m_TaaResolveFramebuffer != nullptr &&
        m_HdrRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_TaaResolveFramebuffer->Recreate(
            m_Device,
            *m_HdrRenderPass,
            *m_SceneRenderTargets,
            true
        );
    }
    if (m_BloomDownsampleFramebuffer != nullptr &&
        m_BloomDownsampleRenderPass != nullptr &&
        m_BloomPyramid != nullptr) {
        m_BloomDownsampleFramebuffer->Recreate(
            m_Device,
            *m_BloomDownsampleRenderPass,
            *m_BloomPyramid
        );
    }
    if (m_BloomUpsampleFramebuffer != nullptr &&
        m_BloomUpsampleRenderPass != nullptr &&
        m_BloomPyramid != nullptr) {
        m_BloomUpsampleFramebuffer->Recreate(
            m_Device,
            *m_BloomUpsampleRenderPass,
            *m_BloomPyramid
        );
    }
    if (m_WeightedTranslucencyFramebuffer != nullptr &&
        m_WeightedTranslucencyRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_WeightedTranslucencyFramebuffer->Recreate(
            m_Device,
            *m_WeightedTranslucencyRenderPass,
            *m_SceneRenderTargets
        );
    }
    if (m_GBufferFramebuffer != nullptr &&
        m_GBufferRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_GBufferFramebuffer->Recreate(
            m_Device,
            *m_GBufferRenderPass,
            *m_SceneRenderTargets
        );
    }
    if (m_ForwardResidualVelocityFramebuffer != nullptr &&
        m_ForwardResidualVelocityRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_ForwardResidualVelocityFramebuffer->Recreate(
            m_Device,
            *m_ForwardResidualVelocityRenderPass,
            *m_SceneRenderTargets
        );
    }
    if (m_DlssMaskFramebuffer != nullptr &&
        m_DlssMaskRenderPass != nullptr &&
        m_SceneRenderTargets != nullptr) {
        m_DlssMaskFramebuffer->Recreate(
            m_Device,
            *m_DlssMaskRenderPass,
            *m_SceneRenderTargets
        );
    }
    if (m_ShadowMap != nullptr) {
        m_ShadowMap->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            m_ShadowSettings.mapSize
        );
    }
    if (m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            m_ShadowSettings.mapSize
        );
    }
    if (m_LocalShadowAtlas != nullptr) {
        m_LocalShadowAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            LocalShadowAtlasTileSizeFor(m_ShadowSettings),
            LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
        );
    }
    if (m_ReflectionCaptureShadowMap != nullptr) {
        m_ReflectionCaptureShadowMap->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            1u,
            m_ShadowSettings.mapSize
        );
    }
    if (m_ReflectionCaptureLocalShadowAtlas != nullptr) {
        m_ReflectionCaptureLocalShadowAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            1u,
            LocalShadowAtlasTileSizeFor(m_ShadowSettings),
            LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
        );
    }
    ResetLocalShadowCacheStates();
    if (m_ShadowFramebuffer != nullptr && m_ShadowRenderPass != nullptr && m_ShadowMap != nullptr) {
        m_ShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_ShadowMap
        );
    }
    if (m_DirectionalShadowCascadeFramebuffer != nullptr &&
        m_ShadowRenderPass != nullptr &&
        m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_DirectionalShadowCascadeAtlas
        );
    }
    if (m_LocalShadowFramebuffer != nullptr &&
        m_ShadowRenderPass != nullptr &&
        m_LocalShadowAtlas != nullptr) {
        m_LocalShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_LocalShadowAtlas
        );
    }
    if (m_ReflectionCaptureShadowFramebuffer != nullptr &&
        m_ShadowRenderPass != nullptr &&
        m_ReflectionCaptureShadowMap != nullptr) {
        m_ReflectionCaptureShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_ReflectionCaptureShadowMap
        );
    }
    if (m_ReflectionCaptureLocalShadowFramebuffer != nullptr &&
        m_ShadowRenderPass != nullptr &&
        m_ReflectionCaptureLocalShadowAtlas != nullptr) {
        m_ReflectionCaptureLocalShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_ReflectionCaptureLocalShadowAtlas
        );
    }
    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    m_MaterialDescriptorSets = std::make_unique<VulkanMaterialDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        materials,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get()
    );
    m_ReflectionCaptureMaterialDescriptorSets =
        std::make_unique<VulkanMaterialDescriptorSets>(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            materials,
            m_ReflectionCaptureShadowMap.get(),
            nullptr,
            m_ReflectionCaptureLocalShadowAtlas.get()
        );
    if (m_HiZDescriptorSets != nullptr &&
        m_HiZDescriptorSetLayout != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SsrDepthPyramid != nullptr &&
        m_SsrDepthPyramidSampler != nullptr) {
        m_HiZDescriptorSets->Recreate(
            m_Device,
            *m_HiZDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SsrDepthPyramid,
            *m_SsrDepthPyramidSampler
        );
    }
    if (m_SsrReconstructionDescriptorSets != nullptr &&
        m_SsrReconstructionDescriptorSetLayout != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SsrDepthPyramid != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_SsrReconstructionDescriptorSets->Recreate(
            m_Device,
            *m_SsrReconstructionDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SsrDepthPyramid,
            *m_SceneTargetSampler
        );
    }
    if (m_GBufferDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_GBufferDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SceneTargetSampler,
            m_ShadowMap.get(),
            m_DirectionalShadowCascadeAtlas.get(),
            m_LocalShadowAtlas.get(),
            m_SsrDepthPyramid.get()
        );
    }
    if (m_HdrDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_BloomPyramid != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_HdrDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            m_BloomPyramid.get(),
            m_ColorGradingLut.get(),
            *m_SceneTargetSampler
        );
    }
    if (m_TemporalUpscaleHdrDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_BloomPyramid != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_TemporalUpscaleHdrDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            m_BloomPyramid.get(),
            m_ColorGradingLut.get(),
            *m_SceneTargetSampler,
            true
        );
    }
    if (m_BloomDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_BloomPyramid != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_BloomDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_BloomPyramid,
            *m_SceneTargetSampler
        );
    }
    if (m_TemporalUpscaleBloomDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_BloomPyramid != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_TemporalUpscaleBloomDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_BloomPyramid,
            *m_SceneTargetSampler,
            true
        );
    }
    if (m_WeightedTranslucencyDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_WeightedTranslucencyDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SceneTargetSampler
        );
    }
    m_RenderPass->Recreate(m_Device, *m_Swapchain, *m_DepthBuffer);
    if (m_DepthLoadRenderPass != nullptr) {
        m_DepthLoadRenderPass->Recreate(
            m_Device,
            *m_Swapchain,
            *m_DepthBuffer,
            true
        );
    }
    m_ImGuiLayer = std::make_unique<VulkanImGuiLayer>(
        m_Window,
        m_Instance,
        m_PhysicalDevice,
        m_Device,
        *m_RenderPass,
        *m_Swapchain
    );
    m_GraphicsPipeline->Recreate(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        *m_RenderPass,
        *m_Swapchain,
        m_PipelineSpec
    );
    if (m_DoubleSidedGraphicsPipeline != nullptr) {
        m_DoubleSidedGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::DoubleSided(m_PipelineSpec)
        );
    }
    if (m_ForwardResidualGraphicsPipeline != nullptr) {
        const PipelineSpec forwardResidualSpec =
            PipelineSpec::ForwardResidual3D(
                m_PipelineSpec.vertexShaderPath,
                m_PipelineSpec.fragmentShaderPath
            );
        const std::string gBufferVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.vert.spv";
        const std::string forwardVelocityFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/forward_velocity_3d.frag.spv";
        const PipelineSpec forwardResidualVelocitySpec =
            PipelineSpec::ForwardResidualVelocity3D(
                gBufferVertexShaderPath,
                forwardVelocityFragmentShaderPath
            );
        const std::string dlssMaskFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/dlss_mask_3d.frag.spv";
        const PipelineSpec dlssMaskSpec =
            PipelineSpec::DlssMask3D(
                gBufferVertexShaderPath,
                dlssMaskFragmentShaderPath
            );
        if (m_ForwardResidualVelocityGraphicsPipeline != nullptr &&
            m_ForwardResidualVelocityRenderPass != nullptr) {
            m_ForwardResidualVelocityGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_ForwardResidualVelocityRenderPass->Handle(),
                *m_Swapchain,
                forwardResidualVelocitySpec
            );
        }
        if (m_DoubleSidedForwardResidualVelocityGraphicsPipeline != nullptr &&
            m_ForwardResidualVelocityRenderPass != nullptr) {
            m_DoubleSidedForwardResidualVelocityGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_ForwardResidualVelocityRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(forwardResidualVelocitySpec)
            );
        }
        if (m_DlssMaskGraphicsPipeline != nullptr &&
            m_DlssMaskRenderPass != nullptr) {
            m_DlssMaskGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_DlssMaskRenderPass->Handle(),
                *m_Swapchain,
                dlssMaskSpec
            );
        }
        if (m_DoubleSidedDlssMaskGraphicsPipeline != nullptr &&
            m_DlssMaskRenderPass != nullptr) {
            m_DoubleSidedDlssMaskGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_DlssMaskRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(dlssMaskSpec)
            );
        }
        if (m_ForwardResidualHdrGraphicsPipeline != nullptr &&
            m_HdrRenderPass != nullptr) {
            m_ForwardResidualHdrGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_HdrRenderPass->Handle(),
                *m_Swapchain,
                forwardResidualSpec
            );
        }
        if (m_DoubleSidedForwardResidualHdrGraphicsPipeline != nullptr &&
            m_HdrRenderPass != nullptr) {
            m_DoubleSidedForwardResidualHdrGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_HdrRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(forwardResidualSpec)
            );
        }
        m_ForwardResidualGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            forwardResidualSpec
        );
        if (m_DoubleSidedForwardResidualGraphicsPipeline != nullptr) {
            m_DoubleSidedForwardResidualGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                *m_RenderPass,
                *m_Swapchain,
                PipelineSpec::DoubleSided(forwardResidualSpec)
            );
        }
    }
    if (m_WeightedTranslucencyGraphicsPipeline != nullptr &&
        m_WeightedTranslucencyRenderPass != nullptr) {
        const std::string weightedTranslucencyFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/weighted_translucency_3d.frag.spv";
        const PipelineSpec weightedTranslucencySpec =
            PipelineSpec::WeightedTranslucency3D(
                m_PipelineSpec.vertexShaderPath,
                weightedTranslucencyFragmentShaderPath
            );
        m_WeightedTranslucencyGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_WeightedTranslucencyRenderPass->Handle(),
            *m_Swapchain,
            weightedTranslucencySpec
        );
        if (m_DoubleSidedWeightedTranslucencyGraphicsPipeline != nullptr) {
            m_DoubleSidedWeightedTranslucencyGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_WeightedTranslucencyRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(weightedTranslucencySpec)
            );
        }
    }
    if (m_DepthPrefillGraphicsPipeline != nullptr) {
        const std::string depthPrefillShaderPath =
            std::string(SE_SHADER_DIR) + "/depth_prefill_3d.vert.spv";
        const PipelineSpec depthPrefillSpec =
            PipelineSpec::DepthPrefill3D(depthPrefillShaderPath);
        m_DepthPrefillGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            depthPrefillSpec
        );
        if (m_DoubleSidedDepthPrefillGraphicsPipeline != nullptr) {
            m_DoubleSidedDepthPrefillGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                *m_RenderPass,
                *m_Swapchain,
                PipelineSpec::DoubleSided(depthPrefillSpec)
            );
        }
    }
    if (m_BloomDownsamplePipeline != nullptr &&
        m_BloomDownsampleRenderPass != nullptr) {
        const std::string hdrCompositeVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
        const std::string bloomDownsampleFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/bloom_downsample.frag.spv";
        m_BloomDownsamplePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_BloomDownsampleRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::BloomPyramid(
                hdrCompositeVertexShaderPath,
                bloomDownsampleFragmentShaderPath
            )
        );
    }
    if (m_BloomUpsamplePipeline != nullptr &&
        m_BloomUpsampleRenderPass != nullptr) {
        const std::string hdrCompositeVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
        const std::string bloomUpsampleFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/bloom_upsample.frag.spv";
        m_BloomUpsamplePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_BloomUpsampleRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::BloomUpsample(
                hdrCompositeVertexShaderPath,
                bloomUpsampleFragmentShaderPath
            )
        );
    }
    if (m_HdrCompositePipeline != nullptr) {
        const std::string hdrCompositeVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
        const std::string hdrCompositeFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.frag.spv";
        m_HdrCompositePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::HdrComposite(
                hdrCompositeVertexShaderPath,
                hdrCompositeFragmentShaderPath
            )
        );
    }
    if (m_TaaResolvePipeline != nullptr &&
        m_HdrRenderPass != nullptr) {
        const std::string hdrCompositeVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
        const std::string taaResolveFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/taa_resolve.frag.spv";
        m_TaaResolvePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_HdrRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::HdrComposite(
                hdrCompositeVertexShaderPath,
                taaResolveFragmentShaderPath
            )
        );
    }
    if (m_WeightedTranslucencyResolvePipeline != nullptr &&
        m_HdrRenderPass != nullptr) {
        const std::string hdrCompositeVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/hdr_composite.vert.spv";
        const std::string weightedTranslucencyResolveFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/weighted_translucency_resolve.frag.spv";
        m_WeightedTranslucencyResolvePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_HdrRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::WeightedTranslucencyResolve(
                hdrCompositeVertexShaderPath,
                weightedTranslucencyResolveFragmentShaderPath
            )
        );
    }
    if (m_GBufferDebugPipeline != nullptr) {
        const std::string gBufferDebugVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_debug.vert.spv";
        const std::string gBufferDebugFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_debug.frag.spv";
        m_GBufferDebugPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            PipelineSpec::GBufferDebug(
                gBufferDebugVertexShaderPath,
                gBufferDebugFragmentShaderPath
            )
        );
    }
    if (m_InstancedGraphicsPipeline != nullptr) {
        PipelineSpec instancedSpec = m_PipelineSpec;
        instancedSpec.vertexShaderPath = m_PipelineSpec.instancedVertexShaderPath;
        instancedSpec.vertexLayout = VertexLayout::Vertex3DInstanced;
        m_InstancedGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            instancedSpec
        );
        if (m_DoubleSidedInstancedGraphicsPipeline != nullptr) {
            m_DoubleSidedInstancedGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                *m_RenderPass,
                *m_Swapchain,
                PipelineSpec::DoubleSided(instancedSpec)
            );
        }
        if (m_InstanceBuffer != nullptr) {
            m_InstanceBuffer->Recreate(
                m_Device,
                m_PhysicalDevice,
                m_Swapchain->Images().size()
            );
            m_MainInstanceUploadSignatures.clear();
        }
    }
    if (m_GBufferGraphicsPipeline != nullptr && m_GBufferRenderPass != nullptr) {
        const std::string gBufferVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.vert.spv";
        const std::string gBufferFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/gbuffer_3d.frag.spv";
        const PipelineSpec gBufferSpec =
            PipelineSpec::GBuffer3D(
                gBufferVertexShaderPath,
                gBufferFragmentShaderPath
            );
        m_GBufferGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_GBufferRenderPass->Handle(),
            *m_Swapchain,
            gBufferSpec
        );
        if (m_DoubleSidedGBufferGraphicsPipeline != nullptr) {
            m_DoubleSidedGBufferGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_GBufferRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(gBufferSpec)
            );
        }
    }
    if (m_DeferredLightingPipeline != nullptr && m_HdrRenderPass != nullptr) {
        const std::string deferredLightingVertexShaderPath =
            std::string(SE_SHADER_DIR) + "/deferred_lighting.vert.spv";
        const std::string deferredLightingFragmentShaderPath =
            std::string(SE_SHADER_DIR) + "/deferred_lighting.frag.spv";
        m_DeferredLightingPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_HdrRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::DeferredLighting(
                deferredLightingVertexShaderPath,
                deferredLightingFragmentShaderPath
            )
        );
    }
    if (m_LightTileCullComputePipeline != nullptr) {
        const std::string lightTileCullShaderPath =
            std::string(SE_SHADER_DIR) + "/light_tile_cull.comp.spv";
        m_LightTileCullComputePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            lightTileCullShaderPath
        );
    }
    if (m_AutoExposureComputePipeline != nullptr) {
        m_AutoExposureComputePipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            std::string(SE_SHADER_DIR) + "/auto_exposure_histogram.comp.spv"
        );
    }
    if (m_ShadowGraphicsPipeline != nullptr && m_ShadowRenderPass != nullptr && m_ShadowMap != nullptr) {
        const std::string shadowShaderPath = std::string(SE_SHADER_DIR) + "/shadow_depth.vert.spv";
        const PipelineSpec shadowSpec =
            PipelineSpec::ShadowDepth(shadowShaderPath, m_ShadowMap->Extent());
        m_ShadowGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_ShadowRenderPass->Handle(),
            *m_Swapchain,
            shadowSpec
        );
        if (m_DoubleSidedShadowGraphicsPipeline != nullptr) {
            m_DoubleSidedShadowGraphicsPipeline->Recreate(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                m_ShadowRenderPass->Handle(),
                *m_Swapchain,
                PipelineSpec::DoubleSided(shadowSpec)
            );
        }
    }
    if (m_OverlayPipelineSpec.has_value()) {
        m_OverlayUniformBuffer = std::make_unique<VulkanUniformBuffer>(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
        m_OverlayDescriptorSets = std::make_unique<VulkanDescriptorSets>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_OverlayUniformBuffer,
            *m_LightBuffer,
            *m_LightTileDiagnosticsBuffer,
            *m_MaterialBuffer,
            *m_ProbeGridBuffer,
            *m_DirectionalShadowCascadeBuffer,
            *m_LocalShadowBuffer,
            *m_AutoExposureBuffer
        );
        m_ReflectionProbeResources.SetDescriptorSetsBound(
            m_ReflectionProbeResources.DescriptorSetsBound() +
                UpdateEnvironmentDescriptorSets(m_OverlayDescriptorSets.get())
        );
        m_OverlayGraphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            *m_RenderPass,
            *m_Swapchain,
            *m_OverlayPipelineSpec
        );
    }
    m_Framebuffer->Recreate(m_Device, *m_RenderPass, *m_DepthBuffer, *m_Swapchain);
    if (m_DepthLoadFramebuffer != nullptr && m_DepthLoadRenderPass != nullptr) {
        m_DepthLoadFramebuffer->Recreate(
            m_Device,
            *m_DepthLoadRenderPass,
            *m_DepthBuffer,
            *m_Swapchain
        );
    }
    m_CommandBuffer->Recreate(
        m_Device,
        m_CommandPool,
        *m_Framebuffer
    );
    if (m_GpuTimer != nullptr) {
        m_GpuTimer->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_Swapchain->Images().size()
        );
    }
}

void VulkanRenderer::ApplyEnvironmentRenderSettings() {
    if (EnvironmentFlagEnabled("SE_FPS_FIRST")) {
        ApplyShadowQualityPreset(m_ShadowSettings, VulkanShadowQuality::Off);
    }

    const std::optional<VulkanShadowQuality> shadowQuality =
        ShadowQualityFromEnvironment();
    if (shadowQuality.has_value()) {
        ApplyShadowQualityPreset(m_ShadowSettings, *shadowQuality);
    }
    if (const std::optional<f32> shadowMapSize =
            EnvironmentFloatOverride("SE_SHADOW_MAP_SIZE")) {
        m_ShadowSettings.mapSize = std::clamp(
            static_cast<u32>(*shadowMapSize + 0.5f),
            512u,
            8192u
        );
    }
    if (const std::optional<f32> shadowPcfRadius =
            EnvironmentFloatOverride("SE_SHADOW_PCF_RADIUS")) {
        m_ShadowSettings.pcfRadius =
            std::clamp(*shadowPcfRadius, 0.0f, 6.0f);
    }
    if (const std::optional<f32> shadowPcfKernelRadius =
            EnvironmentFloatOverride("SE_SHADOW_PCF_KERNEL_RADIUS")) {
        m_ShadowSettings.pcfKernelRadius = std::clamp(
            static_cast<u32>(*shadowPcfKernelRadius + 0.5f),
            0u,
            2u
        );
    }
    if (const std::optional<f32> shadowPcssStrength =
            EnvironmentFloatOverride("SE_SHADOW_PCSS_STRENGTH")) {
        m_ShadowSettings.pcssStrength =
            std::clamp(*shadowPcssStrength, 0.0f, 1.0f);
    }
    if (EnvironmentFlagEnabled("SE_DIRECTIONAL_PCSS_OFF")) {
        m_ShadowSettings.pcssStrength = 0.0f;
    }
    if (const std::optional<f32> blockerSamples =
            EnvironmentFloatOverride("SE_DIRECTIONAL_PCSS_BLOCKER_SAMPLES")) {
        m_ShadowSettings.directionalPcssBlockerSampleCount = std::clamp(
            static_cast<u32>(*blockerSamples + 0.5f),
            0u,
            16u
        );
    }
    if (const std::optional<f32> filterSamples =
            EnvironmentFloatOverride("SE_DIRECTIONAL_PCSS_FILTER_SAMPLES")) {
        m_ShadowSettings.directionalPcssFilterSampleCount = std::clamp(
            static_cast<u32>(*filterSamples + 0.5f),
            0u,
            16u
        );
    }
    if (const std::optional<f32> searchRadius =
            EnvironmentFloatOverride("SE_DIRECTIONAL_PCSS_SEARCH_RADIUS_TEXELS")) {
        m_ShadowSettings.directionalPcssSearchRadiusTexels =
            std::clamp(*searchRadius, 0.0f, 16.0f);
    }
    if (const std::optional<f32> maxPenumbra =
            EnvironmentFloatOverride("SE_DIRECTIONAL_PCSS_MAX_PENUMBRA_TEXELS")) {
        m_ShadowSettings.directionalPcssMaxPenumbraTexels =
            std::clamp(*maxPenumbra, 0.0f, 16.0f);
    }
    if (const std::optional<f32> grazingFadeStart =
            EnvironmentFloatOverride("SE_DIRECTIONAL_PCSS_GRAZING_FADE_START")) {
        m_ShadowSettings.directionalPcssGrazingFadeStart =
            std::clamp(*grazingFadeStart, 0.0f, 0.95f);
    }
    if (const std::optional<f32> grazingFadeEnd =
            EnvironmentFloatOverride("SE_DIRECTIONAL_PCSS_GRAZING_FADE_END")) {
        m_ShadowSettings.directionalPcssGrazingFadeEnd =
            std::clamp(*grazingFadeEnd, 0.01f, 1.0f);
    }
    if (m_ShadowSettings.directionalPcssGrazingFadeEnd <=
        m_ShadowSettings.directionalPcssGrazingFadeStart) {
        m_ShadowSettings.directionalPcssGrazingFadeEnd = std::min(
            m_ShadowSettings.directionalPcssGrazingFadeStart + 0.01f,
            1.0f
        );
    }
    if (EnvironmentFlagEnabled("SE_DIRECTIONAL_PCSS_GRAZING_FADE_OFF")) {
        m_ShadowSettings.directionalPcssGrazingFadeEnabled = false;
    }
    if (const std::optional<f32> directionalFilterMode =
            EnvironmentFloatOverride("SE_DIRECTIONAL_SHADOW_FILTER_MODE")) {
        m_ShadowSettings.directionalFilterMode = static_cast<VulkanDirectionalShadowFilterMode>(
            std::clamp(static_cast<i32>(*directionalFilterMode + 0.5f), 0, 1)
        );
    }
    if (const std::optional<f32> directionalFilterKernelWidth =
            EnvironmentFloatOverride("SE_DIRECTIONAL_SHADOW_FILTER_KERNEL_WIDTH")) {
        m_ShadowSettings.directionalFilterKernelWidth = std::clamp(
            static_cast<u32>(*directionalFilterKernelWidth + 0.5f),
            3u,
            5u
        );
        m_ShadowSettings.directionalFilterKernelWidth =
            m_ShadowSettings.directionalFilterKernelWidth >= 5u ? 5u : 3u;
    }
    if (const std::optional<f32> directionalFilterReceiverBiasExtent =
            EnvironmentFloatOverride("SE_DIRECTIONAL_SHADOW_FILTER_RECEIVER_BIAS_EXTENT_TEXELS")) {
        m_ShadowSettings.directionalFilterReceiverBiasExtentTexels = std::clamp(
            *directionalFilterReceiverBiasExtent,
            0.0f,
            4.0f
        );
    }
    if (const std::optional<f32> shadowBiasMin =
            EnvironmentFloatOverride("SE_SHADOW_BIAS_MIN")) {
        m_ShadowSettings.biasMin = std::clamp(*shadowBiasMin, 0.0f, 0.02f);
    }
    if (const std::optional<f32> shadowBiasSlope =
            EnvironmentFloatOverride("SE_SHADOW_BIAS_SLOPE")) {
        m_ShadowSettings.biasSlope = std::clamp(*shadowBiasSlope, 0.0f, 0.05f);
    }
    if (const std::optional<bool> casterDepthBiasEnabled =
            EnvironmentFlagOverride("SE_SHADOW_CASTER_DEPTH_BIAS_ENABLED")) {
        m_ShadowSettings.casterDepthBiasEnabled = *casterDepthBiasEnabled;
    }
    if (const std::optional<f32> casterDepthBiasConstant =
            EnvironmentFloatOverride("SE_SHADOW_CASTER_DEPTH_BIAS_CONSTANT")) {
        m_ShadowSettings.casterDepthBiasConstant =
            std::clamp(*casterDepthBiasConstant, 0.0f, 262144.0f);
    }
    if (const std::optional<f32> casterDepthBiasClamp =
            EnvironmentFloatOverride("SE_SHADOW_CASTER_DEPTH_BIAS_CLAMP")) {
        m_ShadowSettings.casterDepthBiasClamp =
            std::clamp(*casterDepthBiasClamp, 0.0f, 0.05f);
    }
    if (const std::optional<f32> casterDepthBiasSlope =
            EnvironmentFloatOverride("SE_SHADOW_CASTER_DEPTH_BIAS_SLOPE")) {
        m_ShadowSettings.casterDepthBiasSlope =
            std::clamp(*casterDepthBiasSlope, 0.0f, 16.0f);
    }
    if (const std::optional<f32> receiverPlaneBiasScale =
            EnvironmentFloatOverride("SE_DIRECTIONAL_RECEIVER_PLANE_BIAS_SCALE")) {
        m_ShadowSettings.directionalReceiverPlaneBiasScale =
            std::clamp(*receiverPlaneBiasScale, 0.0f, 4.0f);
    }
    if (const std::optional<f32> normalOffsetBiasTexels =
            EnvironmentFloatOverride("SE_DIRECTIONAL_NORMAL_OFFSET_BIAS_TEXELS")) {
        m_ShadowSettings.directionalNormalOffsetBiasTexels =
            std::clamp(*normalOffsetBiasTexels, 0.0f, 4.0f);
    }
    if (const std::optional<f32> slopeOffsetBiasTexels =
            EnvironmentFloatOverride("SE_DIRECTIONAL_SLOPE_OFFSET_BIAS_TEXELS")) {
        m_ShadowSettings.directionalSlopeOffsetBiasTexels =
            std::clamp(*slopeOffsetBiasTexels, 0.0f, 2.0f);
    }
    std::optional<bool> directionalShadowReceive =
        EnvironmentFlagOverride("SE_DIRECTIONAL_SHADOW_RECEIVE");
    if (!directionalShadowReceive.has_value()) {
        directionalShadowReceive =
            EnvironmentFlagOverride("SE_SHADOW_CASCADE_RECEIVE");
    }
    if (!directionalShadowReceive.has_value()) {
        directionalShadowReceive = EnvironmentFlagOverride("SE_CSM_RECEIVE");
    }
    if (directionalShadowReceive.has_value()) {
        m_ShadowSettings.directionalShadowReceiveEnabled =
            *directionalShadowReceive;
    }
    bool sharedLocalShadowFilterOverridden = false;
    if (const std::optional<f32> localShadowBiasMin =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_BIAS_MIN")) {
        m_ShadowSettings.localBiasMin =
            std::clamp(*localShadowBiasMin, 0.0f, 0.02f);
        sharedLocalShadowFilterOverridden = true;
    }
    if (const std::optional<f32> localShadowBiasSlope =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_BIAS_SLOPE")) {
        m_ShadowSettings.localBiasSlope =
            std::clamp(*localShadowBiasSlope, 0.0f, 0.05f);
        sharedLocalShadowFilterOverridden = true;
    }
    if (const std::optional<f32> localShadowPcfRadius =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_PCF_RADIUS")) {
        m_ShadowSettings.localPcfRadius =
            std::clamp(*localShadowPcfRadius, 0.0f, 4.0f);
        sharedLocalShadowFilterOverridden = true;
    }
    if (const std::optional<f32> localShadowPcfKernelRadius =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_PCF_KERNEL_RADIUS")) {
        m_ShadowSettings.localPcfKernelRadius = std::clamp(
            static_cast<u32>(*localShadowPcfKernelRadius + 0.5f),
            0u,
            2u
        );
        sharedLocalShadowFilterOverridden = true;
    }
    if (const std::optional<f32> localShadowPcssStrength =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_PCSS_STRENGTH")) {
        m_ShadowSettings.localPcssStrength =
            std::clamp(*localShadowPcssStrength, 0.0f, 1.0f);
        sharedLocalShadowFilterOverridden = true;
    }
    if (const std::optional<f32> blockerSamples =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_PCSS_BLOCKER_SAMPLES")) {
        m_ShadowSettings.localPcssBlockerSampleCount = std::clamp(
            static_cast<u32>(*blockerSamples + 0.5f),
            0u,
            16u
        );
        sharedLocalShadowFilterOverridden = true;
    }
    if (const std::optional<f32> filterSamples =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_PCSS_FILTER_SAMPLES")) {
        m_ShadowSettings.localPcssFilterSampleCount = std::clamp(
            static_cast<u32>(*filterSamples + 0.5f),
            1u,
            16u
        );
        sharedLocalShadowFilterOverridden = true;
    }
    if (const std::optional<f32> searchRadius =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_PCSS_SEARCH_RADIUS_TEXELS")) {
        m_ShadowSettings.localPcssSearchRadiusTexels =
            std::clamp(*searchRadius, 0.0f, 16.0f);
        sharedLocalShadowFilterOverridden = true;
    }
    if (const std::optional<f32> maxPenumbra =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_PCSS_MAX_PENUMBRA_TEXELS")) {
        m_ShadowSettings.localPcssMaxPenumbraTexels =
            std::clamp(*maxPenumbra, 0.5f, 16.0f);
        sharedLocalShadowFilterOverridden = true;
    }
    if (sharedLocalShadowFilterOverridden) {
        SyncLocalShadowKindFiltersToShared(m_ShadowSettings);
    }
    if (const std::optional<bool> productionFilter =
            EnvironmentFlagOverride("SE_LOCAL_SHADOW_PRODUCTION_FILTER")) {
        m_ShadowSettings.localProductionFilterEnabled = *productionFilter;
    }
    const auto applyLocalShadowKindOverrides = [](
        VulkanLocalShadowFilterSettings& filter,
        const char* biasMinName,
        const char* biasSlopeName,
        const char* pcfRadiusName,
        const char* pcfKernelRadiusName,
        const char* pcssStrengthName,
        const char* blockerSamplesName,
        const char* filterSamplesName,
        const char* searchRadiusName,
        const char* maxPenumbraName
    ) {
        if (const std::optional<f32> biasMin =
                EnvironmentFloatOverride(biasMinName)) {
            filter.biasMin = std::clamp(*biasMin, 0.0f, 0.02f);
        }
        if (const std::optional<f32> biasSlope =
                EnvironmentFloatOverride(biasSlopeName)) {
            filter.biasSlope = std::clamp(*biasSlope, 0.0f, 0.05f);
        }
        if (const std::optional<f32> pcfRadius =
                EnvironmentFloatOverride(pcfRadiusName)) {
            filter.pcfRadius = std::clamp(*pcfRadius, 0.0f, 4.0f);
        }
        if (const std::optional<f32> pcfKernelRadius =
                EnvironmentFloatOverride(pcfKernelRadiusName)) {
            filter.pcfKernelRadius = std::clamp(
                static_cast<u32>(*pcfKernelRadius + 0.5f),
                0u,
                2u
            );
        }
        if (const std::optional<f32> pcssStrength =
                EnvironmentFloatOverride(pcssStrengthName)) {
            filter.pcssStrength = std::clamp(*pcssStrength, 0.0f, 1.0f);
        }
        if (const std::optional<f32> blockerSamples =
                EnvironmentFloatOverride(blockerSamplesName)) {
            filter.pcssBlockerSampleCount = std::clamp(
                static_cast<u32>(*blockerSamples + 0.5f),
                0u,
                16u
            );
        }
        if (const std::optional<f32> filterSamples =
                EnvironmentFloatOverride(filterSamplesName)) {
            filter.pcssFilterSampleCount = std::clamp(
                static_cast<u32>(*filterSamples + 0.5f),
                1u,
                16u
            );
        }
        if (const std::optional<f32> searchRadius =
                EnvironmentFloatOverride(searchRadiusName)) {
            filter.pcssSearchRadiusTexels =
                std::clamp(*searchRadius, 0.0f, 16.0f);
        }
        if (const std::optional<f32> maxPenumbra =
                EnvironmentFloatOverride(maxPenumbraName)) {
            filter.pcssMaxPenumbraTexels =
                std::clamp(*maxPenumbra, 0.5f, 16.0f);
        }
    };
    applyLocalShadowKindOverrides(
        m_ShadowSettings.pointLocalShadowFilter,
        "SE_LOCAL_SHADOW_POINT_BIAS_MIN",
        "SE_LOCAL_SHADOW_POINT_BIAS_SLOPE",
        "SE_LOCAL_SHADOW_POINT_PCF_RADIUS",
        "SE_LOCAL_SHADOW_POINT_PCF_KERNEL_RADIUS",
        "SE_LOCAL_SHADOW_POINT_PCSS_STRENGTH",
        "SE_LOCAL_SHADOW_POINT_PCSS_BLOCKER_SAMPLES",
        "SE_LOCAL_SHADOW_POINT_PCSS_FILTER_SAMPLES",
        "SE_LOCAL_SHADOW_POINT_PCSS_SEARCH_RADIUS_TEXELS",
        "SE_LOCAL_SHADOW_POINT_PCSS_MAX_PENUMBRA_TEXELS"
    );
    applyLocalShadowKindOverrides(
        m_ShadowSettings.spotLocalShadowFilter,
        "SE_LOCAL_SHADOW_SPOT_BIAS_MIN",
        "SE_LOCAL_SHADOW_SPOT_BIAS_SLOPE",
        "SE_LOCAL_SHADOW_SPOT_PCF_RADIUS",
        "SE_LOCAL_SHADOW_SPOT_PCF_KERNEL_RADIUS",
        "SE_LOCAL_SHADOW_SPOT_PCSS_STRENGTH",
        "SE_LOCAL_SHADOW_SPOT_PCSS_BLOCKER_SAMPLES",
        "SE_LOCAL_SHADOW_SPOT_PCSS_FILTER_SAMPLES",
        "SE_LOCAL_SHADOW_SPOT_PCSS_SEARCH_RADIUS_TEXELS",
        "SE_LOCAL_SHADOW_SPOT_PCSS_MAX_PENUMBRA_TEXELS"
    );
    applyLocalShadowKindOverrides(
        m_ShadowSettings.rectLocalShadowFilter,
        "SE_LOCAL_SHADOW_RECT_BIAS_MIN",
        "SE_LOCAL_SHADOW_RECT_BIAS_SLOPE",
        "SE_LOCAL_SHADOW_RECT_PCF_RADIUS",
        "SE_LOCAL_SHADOW_RECT_PCF_KERNEL_RADIUS",
        "SE_LOCAL_SHADOW_RECT_PCSS_STRENGTH",
        "SE_LOCAL_SHADOW_RECT_PCSS_BLOCKER_SAMPLES",
        "SE_LOCAL_SHADOW_RECT_PCSS_FILTER_SAMPLES",
        "SE_LOCAL_SHADOW_RECT_PCSS_SEARCH_RADIUS_TEXELS",
        "SE_LOCAL_SHADOW_RECT_PCSS_MAX_PENUMBRA_TEXELS"
    );
    if (const std::optional<f32> localShadowFaceBlend =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_FACE_BLEND")) {
        m_ShadowSettings.localFaceBlendStrength =
            std::clamp(*localShadowFaceBlend, 0.0f, 1.0f);
    }
    std::optional<f32> rectShadowBiasScale =
        EnvironmentFloatOverride("SE_RECT_SHADOW_BIAS_SCALE");
    if (!rectShadowBiasScale.has_value()) {
        rectShadowBiasScale =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_RECT_BIAS_SCALE");
    }
    if (rectShadowBiasScale.has_value()) {
        m_ShadowSettings.rectLightShadowBiasScale =
            std::clamp(*rectShadowBiasScale, 0.0f, 32.0f);
    }
    if (const std::optional<f32> rectShadowSampleTiles =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_RECT_SAMPLE_TILES")) {
        const u32 requestedSamples = std::clamp(
            static_cast<u32>(std::lround(*rectShadowSampleTiles)),
            kRectAreaShadowBaseSampleTileCount,
            kRectAreaShadowMaxSampleTileCount
        );
        m_ShadowSettings.rectLightShadowSampleTiles =
            requestedSamples >= kRectAreaShadowMaxSampleTileCount
                ? kRectAreaShadowMaxSampleTileCount
                : kRectAreaShadowBaseSampleTileCount;
    }
    if (EnvironmentFlagEnabled("SE_POINT_LIGHT_SHADOWS_OFF") ||
        EnvironmentFlagEnabled("SE_LOCAL_SHADOW_POINT_OFF")) {
        m_ShadowSettings.pointLightShadowEnabled = false;
    }
    if (EnvironmentFlagEnabled("SE_SPOT_LIGHT_SHADOWS_OFF") ||
        EnvironmentFlagEnabled("SE_LOCAL_SHADOW_SPOT_OFF")) {
        m_ShadowSettings.spotLightShadowEnabled = false;
    }
    if (EnvironmentFlagEnabled("SE_RECT_LIGHT_SHADOWS_OFF")) {
        m_ShadowSettings.rectLightShadowEnabled = false;
    }
    if (EnvironmentFlagEnabled("SE_LOCAL_SHADOW_RECT_OFF")) {
        m_ShadowSettings.rectLightShadowEnabled = false;
    }
    std::optional<f32> localShadowDebugLightIndex =
        EnvironmentFloatOverride("SE_LOCAL_SHADOW_DEBUG_LIGHT_INDEX");
    if (!localShadowDebugLightIndex.has_value()) {
        localShadowDebugLightIndex =
            EnvironmentFloatOverride("SE_LOCAL_SHADOW_ONLY_LIGHT_INDEX");
    }
    if (localShadowDebugLightIndex.has_value()) {
        m_ShadowSettings.debugLocalShadowLightIndex = static_cast<i32>(
            std::clamp(
                static_cast<int>(std::lround(*localShadowDebugLightIndex)),
                -1,
                static_cast<int>(kRendererMaxFrameLocalLights) - 1
            )
        );
    }
    if (const std::optional<f32> contactShadowStrength =
            EnvironmentFloatOverride("SE_CONTACT_SHADOW_STRENGTH")) {
        m_ShadowSettings.contactShadowStrength =
            std::clamp(*contactShadowStrength, 0.0f, 1.0f);
    }
    if (const std::optional<f32> contactShadowLength =
            EnvironmentFloatOverride("SE_CONTACT_SHADOW_LENGTH")) {
        m_ShadowSettings.contactShadowLength =
            std::clamp(*contactShadowLength, 0.0f, 1.0f);
    }
    if (const std::optional<f32> contactShadowThickness =
            EnvironmentFloatOverride("SE_CONTACT_SHADOW_THICKNESS")) {
        m_ShadowSettings.contactShadowThickness =
            std::clamp(*contactShadowThickness, 0.0f, 0.5f);
    }
    if (const std::optional<f32> contactShadowSteps =
            EnvironmentFloatOverride("SE_CONTACT_SHADOW_STEPS")) {
        m_ShadowSettings.contactShadowSteps = std::clamp(
            static_cast<u32>(std::lround(*contactShadowSteps)),
            0u,
            12u
        );
    }
    if (const std::optional<f32> contactShadowJitterStrength =
            EnvironmentFloatOverride("SE_CONTACT_SHADOW_JITTER_STRENGTH")) {
        m_ShadowSettings.contactShadowJitterStrength =
            std::clamp(*contactShadowJitterStrength, 0.0f, 1.0f);
    }
    if (const std::optional<f32> contactShadowEdgeFade =
            EnvironmentFloatOverride("SE_CONTACT_SHADOW_EDGE_FADE_PIXELS")) {
        m_ShadowSettings.contactShadowEdgeFadePixels =
            std::clamp(*contactShadowEdgeFade, 0.0f, 96.0f);
    }
    if (const std::optional<f32> ssaoStrength =
            EnvironmentFloatOverride("SE_SSAO_STRENGTH")) {
        m_ShadowSettings.ssaoStrength = std::clamp(*ssaoStrength, 0.0f, 1.0f);
    }
    if (const std::optional<f32> ssrStrength =
            EnvironmentFloatOverride("SE_SSR_STRENGTH")) {
        m_ShadowSettings.ssrStrength = std::clamp(*ssrStrength, 0.0f, 1.0f);
    }
    if (const std::optional<f32> ssrRayLength =
            EnvironmentFloatOverride("SE_SSR_RAY_LENGTH")) {
        m_ShadowSettings.ssrRayLength = std::clamp(*ssrRayLength, 0.0f, 64.0f);
    }
    if (const std::optional<f32> ssrThickness =
            EnvironmentFloatOverride("SE_SSR_THICKNESS")) {
        m_ShadowSettings.ssrThickness = std::clamp(*ssrThickness, 0.0f, 0.5f);
    }
    if (const std::optional<f32> ssrSteps =
            EnvironmentFloatOverride("SE_SSR_STEPS")) {
        m_ShadowSettings.ssrStepCount = static_cast<u32>(std::clamp(
            std::lround(*ssrSteps),
            0l,
            32l
        ));
    }
    if (const std::optional<bool> ssr = EnvironmentFlagOverride("SE_SSR")) {
        if (!*ssr) {
            m_ShadowSettings.ssrStrength = 0.0f;
            m_ShadowSettings.ssrRayLength = 0.0f;
            m_ShadowSettings.ssrStepCount = 0u;
        } else if (m_ShadowSettings.ssrStrength <= 0.0001f ||
            m_ShadowSettings.ssrRayLength <= 0.0001f ||
            m_ShadowSettings.ssrStepCount == 0u) {
            const VulkanShadowSettings defaults{};
            m_ShadowSettings.ssrStrength = defaults.ssrStrength;
            m_ShadowSettings.ssrRayLength = defaults.ssrRayLength;
            m_ShadowSettings.ssrThickness = defaults.ssrThickness;
            m_ShadowSettings.ssrStepCount = defaults.ssrStepCount;
        }
    }
    if (const std::optional<bool> ssrRefinement =
            EnvironmentFlagOverride("SE_SSR_REFINEMENT")) {
        m_ShadowSettings.ssrRefinementEnabled = *ssrRefinement;
    }
    if (const std::optional<bool> ssrHiZ =
            EnvironmentFlagOverride("SE_SSR_HIZ")) {
        m_ShadowSettings.ssrHiZEnabled = *ssrHiZ;
    }
    if (EnvironmentFlagEnabled("SE_SSR_HIZ_OFF")) {
        m_ShadowSettings.ssrHiZEnabled = false;
    }
    std::optional<bool> ssrSceneColorHistory =
        EnvironmentFlagOverride("SE_SSR_SCENE_COLOR_HISTORY");
    if (!ssrSceneColorHistory.has_value()) {
        ssrSceneColorHistory = EnvironmentFlagOverride("SE_SSR_SCENE_COLOR");
    }
    if (ssrSceneColorHistory.has_value()) {
        m_ShadowSettings.ssrSceneColorHistoryEnabled = *ssrSceneColorHistory;
    }
    if (EnvironmentFlagEnabled("SE_SSR_SCENE_COLOR_HISTORY_OFF") ||
        EnvironmentFlagEnabled("SE_SSR_SCENE_COLOR_OFF")) {
        m_ShadowSettings.ssrSceneColorHistoryEnabled = false;
    }
    if (const std::optional<bool> ssrCurrentHdrSource =
            EnvironmentFlagOverride("SE_SSR_CURRENT_HDR_SOURCE")) {
        m_ShadowSettings.ssrCurrentHdrSourceEnabled = *ssrCurrentHdrSource;
    }
    if (EnvironmentFlagEnabled("SE_SSR_CURRENT_HDR_SOURCE_OFF") ||
        EnvironmentFlagEnabled("SE_SSR_SCENE_COLOR_OFF")) {
        m_ShadowSettings.ssrCurrentHdrSourceEnabled = false;
    }
    if (const std::optional<bool> ssrCurrentHdrRadianceFilter =
            EnvironmentFlagOverride("SE_SSR_CURRENT_HDR_FILTER")) {
        m_ShadowSettings.ssrCurrentHdrRadianceFilterEnabled =
            *ssrCurrentHdrRadianceFilter;
    }
    if (EnvironmentFlagEnabled("SE_SSR_CURRENT_HDR_FILTER_OFF")) {
        m_ShadowSettings.ssrCurrentHdrRadianceFilterEnabled = false;
    }
    if (const std::optional<bool> ssrSpatialVarianceClamp =
            EnvironmentFlagOverride("SE_SSR_SPATIAL_VARIANCE_CLAMP")) {
        m_ShadowSettings.ssrSpatialVarianceClampEnabled =
            *ssrSpatialVarianceClamp;
    }
    if (EnvironmentFlagEnabled("SE_SSR_SPATIAL_VARIANCE_CLAMP_OFF")) {
        m_ShadowSettings.ssrSpatialVarianceClampEnabled = false;
    }
    if (const std::optional<bool> ssrProbeFallbackBlend =
            EnvironmentFlagOverride("SE_SSR_PROBE_FALLBACK_BLEND")) {
        m_ShadowSettings.ssrProbeFallbackBlendEnabled =
            *ssrProbeFallbackBlend;
    }
    if (EnvironmentFlagEnabled("SE_SSR_PROBE_FALLBACK_BLEND_OFF")) {
        m_ShadowSettings.ssrProbeFallbackBlendEnabled = false;
    }
    if (const std::optional<bool> ssrHitValidation =
            EnvironmentFlagOverride("SE_SSR_HIT_VALIDATION")) {
        m_ShadowSettings.ssrHitValidationEnabled = *ssrHitValidation;
    }
    if (EnvironmentFlagEnabled("SE_SSR_HIT_VALIDATION_OFF")) {
        m_ShadowSettings.ssrHitValidationEnabled = false;
    }
    if (const std::optional<bool> ssrDeferredReceiverReprojection =
            EnvironmentFlagOverride("SE_SSR_DEFERRED_REPROJECTION")) {
        m_ShadowSettings.ssrDeferredReceiverReprojectionEnabled =
            *ssrDeferredReceiverReprojection;
    }
    if (EnvironmentFlagEnabled("SE_SSR_DEFERRED_REPROJECTION_OFF")) {
        m_ShadowSettings.ssrDeferredReceiverReprojectionEnabled = false;
    }
    if (const std::optional<bool> ssrTemporalHistoryLock =
            EnvironmentFlagOverride("SE_SSR_TEMPORAL_HISTORY_LOCK")) {
        m_ShadowSettings.ssrTemporalHistoryLockEnabled =
            *ssrTemporalHistoryLock;
    }
    if (EnvironmentFlagEnabled("SE_SSR_TEMPORAL_HISTORY_LOCK_OFF")) {
        m_ShadowSettings.ssrTemporalHistoryLockEnabled = false;
    }
    if (const std::optional<bool> ssrTemporalMissHistoryReject =
            EnvironmentFlagOverride("SE_SSR_TEMPORAL_MISS_HISTORY_REJECT")) {
        m_ShadowSettings.ssrTemporalMissHistoryRejectEnabled =
            *ssrTemporalMissHistoryReject;
    }
    if (EnvironmentFlagEnabled("SE_SSR_TEMPORAL_MISS_HISTORY_REJECT_OFF")) {
        m_ShadowSettings.ssrTemporalMissHistoryRejectEnabled = false;
    }
    if (const std::optional<f32> ssrHiZSteps =
            EnvironmentFloatOverride("SE_SSR_HIZ_STEPS")) {
        m_ShadowSettings.ssrStepCount = static_cast<u32>(std::clamp(
            std::lround(*ssrHiZSteps),
            4l,
            32l
        ));
    }
    if (const std::optional<f32> cascadeCount =
            EnvironmentFloatOverride("SE_SHADOW_CASCADE_COUNT")) {
        m_ShadowSettings.cascadeCount = std::clamp(
            static_cast<u32>(*cascadeCount + 0.5f),
            1u,
            static_cast<u32>(kMaxDirectionalShadowCascades)
        );
        m_ShadowSettings.cascadesEnabled = m_ShadowSettings.cascadeCount > 1u;
    }
    if (const std::optional<f32> cascadeBlend =
            EnvironmentFloatOverride("SE_SHADOW_CASCADE_BLEND")) {
        m_ShadowSettings.cascadeBlendRatio = std::clamp(*cascadeBlend, 0.0f, 0.25f);
    }
    if (const std::optional<f32> cascadeFade =
            EnvironmentFloatOverride("SE_SHADOW_CASCADE_FADE")) {
        m_ShadowSettings.cascadeFadeRatio = std::clamp(*cascadeFade, 0.0f, 0.35f);
    }
    if (const std::optional<f32> cascadeSplitLambda =
            EnvironmentFloatOverride("SE_SHADOW_CASCADE_SPLIT_LAMBDA")) {
        m_ShadowSettings.cascadeSplitLambda =
            std::clamp(*cascadeSplitLambda, 0.0f, 1.0f);
    }
    if (const std::optional<f32> cascadeMaxDistance =
            EnvironmentFloatOverride("SE_SHADOW_CASCADE_MAX_DISTANCE")) {
        m_ShadowSettings.cascadeMaxDistance =
            std::clamp(*cascadeMaxDistance, 10.0f, 2000.0f);
    }

    const std::optional<bool> reflectionProbeFallback =
        EnvironmentFlagOverride("SE_REFLECTION_PROBE_FALLBACK");
    if (reflectionProbeFallback.has_value()) {
        m_ShadowSettings.reflectionProbeFallbackEnabled = *reflectionProbeFallback;
    }
    const std::optional<bool> reflectionProbeCubemap =
        EnvironmentFlagOverride("SE_REFLECTION_PROBE_CUBEMAP");
    if (reflectionProbeCubemap.has_value()) {
        m_ShadowSettings.reflectionProbeCubemapEnabled = *reflectionProbeCubemap;
    }
    const std::optional<bool> localReflectionProbeCubemap =
        EnvironmentFlagOverride("SE_LOCAL_REFLECTION_PROBE_CUBEMAP");
    if (localReflectionProbeCubemap.has_value()) {
        m_ShadowSettings.reflectionProbeCubemapEnabled =
            *localReflectionProbeCubemap;
    }
    const std::optional<bool> globalIblCubemap =
        EnvironmentFlagOverride("SE_GLOBAL_IBL_CUBEMAP");
    if (globalIblCubemap.has_value()) {
        m_ShadowSettings.globalIblCubemapEnabled = *globalIblCubemap;
    }
    const std::optional<bool> skybox = EnvironmentFlagOverride("SE_SKYBOX");
    if (skybox.has_value()) {
        m_ShadowSettings.skyboxEnabled = *skybox;
    }
    if (const std::optional<f32> skyboxIntensity =
        EnvironmentFloatOverride("SE_SKYBOX_INTENSITY")) {
        m_ShadowSettings.skyboxIntensity =
            std::clamp(*skyboxIntensity, 0.0f, 4.0f);
    }
    if (const std::optional<f32> skyboxBlur =
        EnvironmentFloatOverride("SE_SKYBOX_BLUR")) {
        m_ShadowSettings.skyboxBlur =
            std::clamp(*skyboxBlur, 0.0f, 8.0f);
    }
    if (const std::optional<f32> reflectionProbeDiffuseIntensity =
        EnvironmentFloatOverride("SE_REFLECTION_PROBE_DIFFUSE_INTENSITY")) {
        m_ShadowSettings.reflectionProbeDiffuseIntensity =
            std::clamp(*reflectionProbeDiffuseIntensity, 0.0f, 4.0f);
    }
    if (const std::optional<f32> reflectionProbeSpecularIntensity =
            EnvironmentFloatOverride("SE_REFLECTION_PROBE_SPECULAR_INTENSITY")) {
        m_ShadowSettings.reflectionProbeSpecularIntensity =
            std::clamp(*reflectionProbeSpecularIntensity, 0.0f, 4.0f);
    }
    const std::optional<bool> localReflectionProbe =
        EnvironmentFlagOverride("SE_LOCAL_REFLECTION_PROBE");
    if (localReflectionProbe.has_value()) {
        m_ShadowSettings.localReflectionProbeEnabled = *localReflectionProbe;
    }
    const std::optional<bool> probeGrid =
        EnvironmentFlagOverride("SE_PROBE_GRID");
    if (probeGrid.has_value()) {
        m_ShadowSettings.probeGridEnabled = *probeGrid;
    }
    const std::optional<f32> probeGridBlend =
        EnvironmentFloatOverride("SE_PROBE_GRID_BLEND");
    if (probeGridBlend.has_value()) {
        m_ShadowSettings.probeGridBlendStrength = *probeGridBlend;
    }
    const std::optional<bool> heightFog =
        EnvironmentFlagOverride("SE_HEIGHT_FOG");
    if (heightFog.has_value()) {
        m_ShadowSettings.heightFogEnabled = *heightFog;
    }
    const std::optional<bool> bloom = EnvironmentFlagOverride("SE_BLOOM");
    if (bloom.has_value()) {
        m_RenderDebugSettings.bloomEnabled = *bloom;
    }
    const std::optional<f32> bloomIntensity =
        EnvironmentFloatOverride("SE_BLOOM_INTENSITY");
    if (bloomIntensity.has_value()) {
        m_RenderDebugSettings.bloomIntensity = *bloomIntensity;
    }
    const std::optional<f32> bloomThreshold =
        EnvironmentFloatOverride("SE_BLOOM_THRESHOLD");
    if (bloomThreshold.has_value()) {
        m_RenderDebugSettings.bloomThreshold = *bloomThreshold;
    }
    const std::optional<f32> bloomRadius =
        EnvironmentFloatOverride("SE_BLOOM_RADIUS");
    if (bloomRadius.has_value()) {
        m_RenderDebugSettings.bloomRadiusPixels = *bloomRadius;
    }
    const std::optional<f32> exposure =
        EnvironmentFloatOverride("SE_EXPOSURE");
    if (exposure.has_value()) {
        m_RenderDebugSettings.exposure = *exposure;
    }
    const std::optional<u32> toneMapMode = ToneMapModeFromEnvironment();
    if (toneMapMode.has_value()) {
        m_RenderDebugSettings.toneMapMode = *toneMapMode;
    }
    const std::optional<f32> toneMapWhitePoint =
        EnvironmentFloatOverride("SE_TONEMAP_WHITE_POINT");
    if (toneMapWhitePoint.has_value()) {
        m_RenderDebugSettings.toneMapWhitePoint = *toneMapWhitePoint;
    }
    const std::optional<bool> autoExposure =
        EnvironmentFlagOverride("SE_AUTO_EXPOSURE");
    if (autoExposure.has_value()) {
        m_RenderDebugSettings.autoExposureEnabled = *autoExposure;
    }
    const std::optional<f32> autoExposureTarget =
        EnvironmentFloatOverride("SE_AUTO_EXPOSURE_TARGET");
    if (autoExposureTarget.has_value()) {
        m_RenderDebugSettings.autoExposureTargetLuminance = *autoExposureTarget;
    }
    const std::optional<f32> autoExposureMin =
        EnvironmentFloatOverride("SE_AUTO_EXPOSURE_MIN");
    if (autoExposureMin.has_value()) {
        m_RenderDebugSettings.autoExposureMin = *autoExposureMin;
    }
    const std::optional<f32> autoExposureMax =
        EnvironmentFloatOverride("SE_AUTO_EXPOSURE_MAX");
    if (autoExposureMax.has_value()) {
        m_RenderDebugSettings.autoExposureMax = *autoExposureMax;
    }
    const std::optional<f32> autoExposureAdaptation =
        EnvironmentFloatOverride("SE_AUTO_EXPOSURE_ADAPTATION");
    if (autoExposureAdaptation.has_value()) {
        m_RenderDebugSettings.autoExposureAdaptation = *autoExposureAdaptation;
    }
    const std::optional<bool> colorGrading =
        EnvironmentFlagOverride("SE_COLOR_GRADING");
    if (colorGrading.has_value()) {
        m_RenderDebugSettings.colorGradingEnabled = *colorGrading;
    }
    const std::optional<f32> colorGradingSaturation =
        EnvironmentFloatOverride("SE_COLOR_GRADING_SATURATION");
    if (colorGradingSaturation.has_value()) {
        m_RenderDebugSettings.colorGradingSaturation = *colorGradingSaturation;
    }
    const std::optional<f32> colorGradingContrast =
        EnvironmentFloatOverride("SE_COLOR_GRADING_CONTRAST");
    if (colorGradingContrast.has_value()) {
        m_RenderDebugSettings.colorGradingContrast = *colorGradingContrast;
    }
    const std::optional<f32> colorGradingGamma =
        EnvironmentFloatOverride("SE_COLOR_GRADING_GAMMA");
    if (colorGradingGamma.has_value()) {
        m_RenderDebugSettings.colorGradingGamma = *colorGradingGamma;
    }
    const std::optional<f32> colorGradingLutStrength =
        EnvironmentFloatOverride("SE_COLOR_GRADING_LUT_STRENGTH");
    if (colorGradingLutStrength.has_value()) {
        m_RenderDebugSettings.colorGradingLutStrength = *colorGradingLutStrength;
    }
    const std::optional<bool> sharpening =
        EnvironmentFlagOverride("SE_SHARPENING");
    if (sharpening.has_value()) {
        m_RenderDebugSettings.sharpeningEnabled = *sharpening;
    }
    const std::optional<f32> sharpeningStrength =
        EnvironmentFloatOverride("SE_SHARPENING_STRENGTH");
    if (sharpeningStrength.has_value()) {
        m_RenderDebugSettings.sharpeningStrength = *sharpeningStrength;
    }
    const std::optional<f32> sharpeningRadius =
        EnvironmentFloatOverride("SE_SHARPENING_RADIUS");
    if (sharpeningRadius.has_value()) {
        m_RenderDebugSettings.sharpeningRadiusPixels = *sharpeningRadius;
    }
    const std::optional<f32> selectedLocalShadowLight =
        EnvironmentFloatOverride("SE_LOCAL_SHADOW_VIEW_LIGHT_INDEX");
    if (selectedLocalShadowLight.has_value()) {
        m_RenderDebugSettings.localShadowDebugLightIndex = static_cast<i32>(
            std::clamp(
                static_cast<int>(std::lround(*selectedLocalShadowLight)),
                -1,
                static_cast<int>(kRendererMaxFrameLocalLights) - 1
            )
        );
    }

    const std::optional<ForwardDebugView> forwardDebugView =
        ForwardDebugViewFromEnvironment();
    if (forwardDebugView.has_value()) {
        m_RenderDebugSettings.forwardView = *forwardDebugView;
    }
}

void VulkanRenderer::ApplyShadowMapSettings() {
    if (m_ShadowMap == nullptr ||
        m_ShadowFramebuffer == nullptr ||
        m_ShadowRenderPass == nullptr ||
        m_ShadowGraphicsPipeline == nullptr ||
        m_MaterialDescriptorSetLayout == nullptr ||
        m_MaterialDescriptorSets == nullptr) {
        return;
    }

    const VkExtent2D currentExtent = m_ShadowMap->Extent();
    if (currentExtent.width == m_ShadowSettings.mapSize &&
        currentExtent.height == m_ShadowSettings.mapSize) {
        return;
    }

    WaitIdle();
    ResetReflectionCaptureShadowSnapshot();
    ReleaseReflectionCapturePersistentShadowSnapshots();
    m_ShadowGraphicsPipeline->Release();
    if (m_DoubleSidedShadowGraphicsPipeline != nullptr) {
        m_DoubleSidedShadowGraphicsPipeline->Release();
    }
    if (m_DirectionalShadowCascadeFramebuffer != nullptr) {
        m_DirectionalShadowCascadeFramebuffer->Release();
    }
    if (m_LocalShadowFramebuffer != nullptr) {
        m_LocalShadowFramebuffer->Release();
    }
    if (m_ReflectionCaptureShadowFramebuffer != nullptr) {
        m_ReflectionCaptureShadowFramebuffer->Release();
    }
    if (m_ReflectionCaptureLocalShadowFramebuffer != nullptr) {
        m_ReflectionCaptureLocalShadowFramebuffer->Release();
    }
    m_ShadowFramebuffer->Release();
    m_ShadowMap->Recreate(
        m_Device,
        m_PhysicalDevice,
        m_CommandPool,
        m_Swapchain->Images().size(),
        m_ShadowSettings.mapSize
    );
    if (m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            m_ShadowSettings.mapSize
        );
    }
    if (m_LocalShadowAtlas != nullptr) {
        m_LocalShadowAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            m_Swapchain->Images().size(),
            LocalShadowAtlasTileSizeFor(m_ShadowSettings),
            LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
        );
    }
    if (m_ReflectionCaptureShadowMap != nullptr) {
        m_ReflectionCaptureShadowMap->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            1u,
            m_ShadowSettings.mapSize
        );
    }
    if (m_ReflectionCaptureLocalShadowAtlas != nullptr) {
        m_ReflectionCaptureLocalShadowAtlas->Recreate(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            1u,
            LocalShadowAtlasTileSizeFor(m_ShadowSettings),
            LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
        );
    }
    ResetLocalShadowCacheStates();
    m_ShadowFramebuffer->Recreate(
        m_Device,
        *m_ShadowRenderPass,
        *m_ShadowMap
    );
    if (m_DirectionalShadowCascadeFramebuffer != nullptr &&
        m_DirectionalShadowCascadeAtlas != nullptr) {
        m_DirectionalShadowCascadeFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_DirectionalShadowCascadeAtlas
        );
    }
    if (m_LocalShadowFramebuffer != nullptr &&
        m_LocalShadowAtlas != nullptr) {
        m_LocalShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_LocalShadowAtlas
        );
    }
    if (m_ReflectionCaptureShadowFramebuffer != nullptr &&
        m_ReflectionCaptureShadowMap != nullptr) {
        m_ReflectionCaptureShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_ReflectionCaptureShadowMap
        );
    }
    if (m_ReflectionCaptureLocalShadowFramebuffer != nullptr &&
        m_ReflectionCaptureLocalShadowAtlas != nullptr) {
        m_ReflectionCaptureLocalShadowFramebuffer->Recreate(
            m_Device,
            *m_ShadowRenderPass,
            *m_ReflectionCaptureLocalShadowAtlas
        );
    }

    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    m_MaterialDescriptorSets = std::make_unique<VulkanMaterialDescriptorSets>(
        m_Device,
        *m_MaterialDescriptorSetLayout,
        materials,
        m_ShadowMap.get(),
        m_DirectionalShadowCascadeAtlas.get(),
        m_LocalShadowAtlas.get()
    );
    m_ReflectionCaptureMaterialDescriptorSets =
        std::make_unique<VulkanMaterialDescriptorSets>(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            materials,
            m_ReflectionCaptureShadowMap.get(),
            nullptr,
            m_ReflectionCaptureLocalShadowAtlas.get()
        );
    if (m_GBufferDescriptorSets != nullptr &&
        m_SceneRenderTargets != nullptr &&
        m_SceneTargetSampler != nullptr) {
        m_GBufferDescriptorSets->Recreate(
            m_Device,
            *m_MaterialDescriptorSetLayout,
            *m_SceneRenderTargets,
            *m_SceneTargetSampler,
            m_ShadowMap.get(),
            m_DirectionalShadowCascadeAtlas.get(),
            m_LocalShadowAtlas.get(),
            m_SsrDepthPyramid.get()
        );
    }

    const std::string shadowShaderPath = std::string(SE_SHADER_DIR) + "/shadow_depth.vert.spv";
    const PipelineSpec shadowSpec =
        PipelineSpec::ShadowDepth(shadowShaderPath, m_ShadowMap->Extent());
    m_ShadowGraphicsPipeline->Recreate(
        m_Device,
        *m_DescriptorSetLayout,
        *m_MaterialDescriptorSetLayout,
        m_ShadowRenderPass->Handle(),
        *m_Swapchain,
        shadowSpec
    );
    if (m_DoubleSidedShadowGraphicsPipeline != nullptr) {
        m_DoubleSidedShadowGraphicsPipeline->Recreate(
            m_Device,
            *m_DescriptorSetLayout,
            *m_MaterialDescriptorSetLayout,
            m_ShadowRenderPass->Handle(),
            *m_Swapchain,
            PipelineSpec::DoubleSided(shadowSpec)
        );
    }
}

void VulkanRenderer::ResetLocalShadowCacheStates() {
    if (m_Swapchain == nullptr) {
        m_LocalShadowCacheStates.clear();
        return;
    }

    m_LocalShadowCacheStates.assign(
        m_Swapchain->Images().size(),
        LocalShadowCacheState{}
    );
}

void VulkanRenderer::ResetReflectionCaptureShadowSnapshot() {
    m_ReflectionCaptureShadowSnapshot = {};
    m_ReflectionCaptureActiveSceneIndex = -1;
}

void VulkanRenderer::ReleaseReflectionCapturePersistentShadowSnapshots() {
    for (ReflectionCapturePersistentShadowSnapshot& snapshot :
        m_ReflectionCapturePersistentShadowSnapshots) {
        snapshot = {};
    }
    m_ReflectionCapturePersistentShadowSnapshotEvictionCount = 0u;
}

u32 VulkanRenderer::ReflectionCapturePersistentShadowSnapshotCount() const {
    u32 count = 0u;
    for (const ReflectionCapturePersistentShadowSnapshot& snapshot :
        m_ReflectionCapturePersistentShadowSnapshots) {
        if (snapshot.directionalShadowMap != nullptr &&
            snapshot.localShadowAtlas != nullptr &&
            snapshot.materialDescriptorSets != nullptr) {
            ++count;
        }
    }
    return count;
}

VulkanRenderer::ReflectionCaptureShadowSnapshot*
VulkanRenderer::AcquireReflectionCapturePersistentShadowSnapshot(
    i32 probeSceneIndex
) {
    if (probeSceneIndex < 0 ||
        m_ShadowRenderPass == nullptr ||
        m_MaterialDescriptorSetLayout == nullptr) {
        return nullptr;
    }

    ReflectionCapturePersistentShadowSnapshot* candidate = nullptr;
    for (ReflectionCapturePersistentShadowSnapshot& snapshot :
        m_ReflectionCapturePersistentShadowSnapshots) {
        if (snapshot.snapshot.probeSceneIndex == probeSceneIndex &&
            snapshot.directionalShadowMap != nullptr &&
            snapshot.localShadowAtlas != nullptr &&
            snapshot.materialDescriptorSets != nullptr) {
            snapshot.lastUsedSchedulerFrame = m_ReflectionCaptureSchedulerFrame;
            return &snapshot.snapshot;
        }
        if (candidate == nullptr && snapshot.snapshot.probeSceneIndex < 0) {
            candidate = &snapshot;
        }
    }

    if (candidate == nullptr) {
        candidate = &*std::min_element(
            m_ReflectionCapturePersistentShadowSnapshots.begin(),
            m_ReflectionCapturePersistentShadowSnapshots.end(),
            [](const ReflectionCapturePersistentShadowSnapshot& left,
               const ReflectionCapturePersistentShadowSnapshot& right) {
                return left.lastUsedSchedulerFrame < right.lastUsedSchedulerFrame;
            }
        );
        if (candidate->snapshot.probeSceneIndex >= 0) {
            ++m_ReflectionCapturePersistentShadowSnapshotEvictionCount;
        }
    }

    *candidate = {};
    std::vector<const VulkanMaterial*> materials = m_RenderResources.Materials();
    try {
        candidate->directionalShadowMap = std::make_unique<VulkanShadowMap>(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            1u,
            m_ShadowSettings.mapSize
        );
        candidate->localShadowAtlas = std::make_unique<VulkanLocalShadowAtlas>(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            1u,
            LocalShadowAtlasTileSizeFor(m_ShadowSettings),
            LocalShadowAtlasTileCapacityFor(m_ShadowSettings)
        );
        candidate->directionalShadowFramebuffer =
            std::make_unique<VulkanShadowFramebuffer>(
                m_Device,
                *m_ShadowRenderPass,
                *candidate->directionalShadowMap
            );
        candidate->localShadowFramebuffer = std::make_unique<VulkanShadowFramebuffer>(
            m_Device,
            *m_ShadowRenderPass,
            *candidate->localShadowAtlas
        );
        candidate->materialDescriptorSets =
            std::make_unique<VulkanMaterialDescriptorSets>(
                m_Device,
                *m_MaterialDescriptorSetLayout,
                materials,
                candidate->directionalShadowMap.get(),
                nullptr,
                candidate->localShadowAtlas.get()
            );
    } catch (...) {
        *candidate = {};
        return nullptr;
    }

    candidate->snapshot.probeSceneIndex = probeSceneIndex;
    candidate->lastUsedSchedulerFrame = m_ReflectionCaptureSchedulerFrame;
    return &candidate->snapshot;
}

i32 VulkanRenderer::ReflectionCapturePersistentShadowSnapshotSlot(
    const ReflectionCaptureShadowSnapshot* snapshot
) const {
    if (snapshot == nullptr) {
        return -1;
    }
    for (std::size_t index = 0;
         index < m_ReflectionCapturePersistentShadowSnapshots.size();
         ++index) {
        if (&m_ReflectionCapturePersistentShadowSnapshots[index].snapshot == snapshot) {
            return static_cast<i32>(index);
        }
    }
    return -1;
}

u32 VulkanRenderer::ReflectionCaptureShadowInputSignature(
    const RendererReflectionProbe& probe,
    const CapturedSceneCaptureAudit& audit,
    bool rectShadowEnabled
) const {
    u64 signature = 0x0d3a20f4bd7f1c59ull;
    signature = HashCombine(signature, audit.localLightSignature);
    signature = HashCombine(signature, audit.geometrySignature);
    signature = HashCombine(signature, FloatBits(probe.center.x));
    signature = HashCombine(signature, FloatBits(probe.center.y));
    signature = HashCombine(signature, FloatBits(probe.center.z));
    signature = HashCombine(signature, FloatBits(probe.radius));
    signature = HashCombine(signature, FloatBits(probe.boxExtents.x));
    signature = HashCombine(signature, FloatBits(probe.boxExtents.y));
    signature = HashCombine(signature, FloatBits(probe.boxExtents.z));
    signature = HashCombine(signature, m_ShadowSettings.mapSize);
    signature = HashCombine(signature, m_ShadowSettings.enabled ? 1u : 0u);
    signature = HashCombine(
        signature,
        m_ShadowSettings.directionalShadowReceiveEnabled ? 1u : 0u
    );
    signature = HashCombine(
        signature,
        m_ShadowSettings.pointLightShadowEnabled ? 1u : 0u
    );
    signature = HashCombine(
        signature,
        m_ShadowSettings.spotLightShadowEnabled ? 1u : 0u
    );
    signature = HashCombine(
        signature,
        m_ShadowSettings.rectLightShadowEnabled ? 1u : 0u
    );
    signature = HashCombine(signature, m_ShadowSettings.rectLightShadowSampleTiles);
    signature = HashCombine(signature, rectShadowEnabled ? 1u : 0u);
    const u32 folded = static_cast<u32>(signature ^ (signature >> 32u));
    return folded == 0u ? 1u : folded;
}

void VulkanRenderer::HandleObjectPicking() {
    if (m_Scene == nullptr || m_Camera == nullptr) {
        return;
    }

    if (!m_Window.WasLeftMousePressed()) {
        return;
    }

    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        return;
    }

    m_Scene->SelectAtWorldPosition(CursorToWorldPosition(m_Swapchain->Extent()));
}

glm::vec2 VulkanRenderer::CursorToWorldPosition(const VkExtent2D& extent) const {
    SE_ASSERT(m_Camera != nullptr, "Cursor picking needs a 2D camera");

    const std::array<f64, 2> cursorPosition = m_Window.CursorPosition();
    const std::array<int, 2> windowSize = m_Window.WindowSize();
    const f32 cursorX = static_cast<f32>(cursorPosition[0]);
    const f32 cursorY = static_cast<f32>(cursorPosition[1]);
    const f32 windowWidth = static_cast<f32>(windowSize[0]);
    const f32 windowHeight = static_cast<f32>(windowSize[1]);
    const f32 framebufferWidth = static_cast<f32>(extent.width);
    const f32 framebufferHeight = static_cast<f32>(extent.height);
    const f32 aspectRatio = framebufferWidth / framebufferHeight;

    const f32 ndcX = (cursorX / windowWidth) * 2.0f - 1.0f;
    const f32 ndcY = (cursorY / windowHeight) * 2.0f - 1.0f;

    const glm::mat4 inverseViewProjection =
        glm::inverse(m_Camera->ProjectionMatrix(aspectRatio) * m_Camera->ViewMatrix());
    const glm::vec4 nearPositionH = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farPositionH = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 nearPosition = glm::vec3(nearPositionH) / nearPositionH.w;
    const glm::vec3 farPosition = glm::vec3(farPositionH) / farPositionH.w;
    const glm::vec3 rayDirection = farPosition - nearPosition;

    if (std::abs(rayDirection.z) <= std::numeric_limits<f32>::epsilon()) {
        return glm::vec2(nearPosition);
    }

    const f32 planeHit = -nearPosition.z / rayDirection.z;
    const glm::vec3 worldPosition = nearPosition + rayDirection * planeHit;

    return glm::vec2(worldPosition);
}

FrameTemporalState VulkanRenderer::BuildFrameTemporalState(
    const FrameMatrices* matrices,
    const VkExtent2D& extent,
    bool velocityTargetAllocated,
    bool materialAuxTargetAllocated,
    bool hdrCompositeAvailable,
    bool historyColorTargetAllocated,
    bool historyColorReady,
    bool taaResolveConfigured,
    f32 taaHistoryWeight,
    bool taaRejectionEnabled,
    bool taaNeighborhoodClampEnabled,
    f32 taaVelocityRejectionThreshold,
    f32 taaDepthRejectionThreshold,
    bool temporalJitterApplyRequested,
    bool suppressNativeTaaResolveForUpscaler
) const {
    FrameTemporalState state{};
    state.previousMatrices = m_PreviousTemporalMatrices;
    state.previousJitterPixels = m_PreviousTemporalJitterPixels;
    state.previousJitterUv = m_PreviousTemporalJitterUv;
    state.jitterEnabled = TemporalJitterEnabledForCurrentMode();
    state.jitterApplied = false;
    state.velocityJitteredHistoryPolicy =
        TemporalVelocityJitteredHistoryPolicyForCurrentMode();
    state.jitterSequenceIndex = m_TemporalFrameCounter % 8u;
    if (state.jitterEnabled && extent.width > 0u && extent.height > 0u) {
        const u32 haltonIndex = state.jitterSequenceIndex + 1u;
        const f32 jitterScale = TemporalNativeTaaModeActive()
            ? NativeTaaJitterScaleFromEnvironment()
            : 1.0f;
        state.jitterPixels = glm::vec2(
            Halton(haltonIndex, 2u) - 0.5f,
            Halton(haltonIndex, 3u) - 0.5f
        ) * jitterScale;
        state.jitterUv = glm::vec2(
            state.jitterPixels.x / static_cast<f32>(extent.width),
            state.jitterPixels.y / static_cast<f32>(extent.height)
        );
    }

    state.velocityMaterialAuxMigrated = materialAuxTargetAllocated;
    const bool forcedReset = TemporalHistoryForceResetFromEnvironment();
    const bool matricesAvailable = matrices != nullptr;
    const bool extentChanged =
        m_TemporalHistoryValid &&
        (m_PreviousTemporalExtent.width != extent.width ||
            m_PreviousTemporalExtent.height != extent.height);

    if (!matricesAvailable) {
        state.resetReason =
            RendererTemporalHistoryResetReason::MatricesUnavailable;
    } else if (forcedReset) {
        state.resetReason = RendererTemporalHistoryResetReason::Forced;
    } else if (!m_TemporalHistoryValid) {
        state.resetReason = RendererTemporalHistoryResetReason::FirstFrame;
    } else if (extentChanged) {
        state.resetReason = RendererTemporalHistoryResetReason::ExtentChanged;
    } else {
        state.resetReason = RendererTemporalHistoryResetReason::None;
        state.historyValid = true;
    }

    state.historyReset = !state.historyValid;
    state.velocityCameraMotionReady =
        velocityTargetAllocated && matricesAvailable && state.historyValid;
    state.velocityObjectMotionReady = false;
    state.taaResolveConfigured = taaResolveConfigured;
    state.taaResolveSuppressedForUpscaler =
        suppressNativeTaaResolveForUpscaler;
    state.taaHistoryColorTargetAllocated = historyColorTargetAllocated;
    state.taaHistoryColorReady = historyColorReady;
    state.taaHistoryWeight = taaHistoryWeight;
    state.taaRejectionEnabled = taaRejectionEnabled;
    state.taaNeighborhoodClampEnabled = taaNeighborhoodClampEnabled;
    state.taaVelocityRejectionThreshold = taaVelocityRejectionThreshold;
    state.taaDepthRejectionThreshold = taaDepthRejectionThreshold;
    state.taaVelocityReprojectionEnabled =
        state.velocityCameraMotionReady && velocityTargetAllocated;
    if (!taaResolveConfigured) {
        state.taaFallbackReason = RendererTaaFallbackReason::Disabled;
    } else if (!hdrCompositeAvailable) {
        state.taaFallbackReason = RendererTaaFallbackReason::CompositeUnavailable;
    } else if (!state.historyValid) {
        state.taaFallbackReason = RendererTaaFallbackReason::HistoryInvalid;
    } else if (!historyColorReady) {
        state.taaFallbackReason = RendererTaaFallbackReason::HistoryColorCold;
    } else if (!state.velocityCameraMotionReady) {
        state.taaFallbackReason = RendererTaaFallbackReason::VelocityUnavailable;
    } else {
        state.temporalUpscaleInputReady = true;
        if (suppressNativeTaaResolveForUpscaler) {
            state.taaFallbackReason =
                RendererTaaFallbackReason::SuppressedForTemporalUpscaler;
        } else {
            state.taaFallbackReason = RendererTaaFallbackReason::None;
            state.taaResolveEnabled = true;
        }
    }
    if (state.taaResolveEnabled) {
        state.temporalUpscaleInputReady = true;
    }
    state.jitterApplied =
        state.jitterEnabled &&
        temporalJitterApplyRequested &&
        (state.taaResolveEnabled || state.temporalUpscaleInputReady);
    if (!state.historyValid && matricesAvailable) {
        state.previousMatrices = *matrices;
        state.previousJitterPixels = glm::vec2(0.0f);
        state.previousJitterUv = glm::vec2(0.0f);
    }
    state.velocityPreviousJitterApplied =
        state.velocityJitteredHistoryPolicy &&
        state.historyValid &&
        m_PreviousTemporalJitterApplied;
    if (state.velocityPreviousJitterApplied) {
        ApplyProjectionJitter(state.previousMatrices.proj, state.previousJitterUv);
    }

    return state;
}

FrameTemporalUpscaleState VulkanRenderer::BuildFrameTemporalUpscaleState(
    const VkExtent2D& displayExtent,
    const VkExtent2D& activeInternalExtent,
    bool hdrSceneColorReady,
    bool sceneDepthReady,
    const FrameTemporalState& temporalState,
    bool temporalReconstructionAllowed
) const {
    FrameTemporalUpscaleState state{};
    state.displayExtent = displayExtent;
    state.activeInternalExtent =
        activeInternalExtent.width > 0u && activeInternalExtent.height > 0u
            ? activeInternalExtent
            : displayExtent;
    state.requestedRenderScale = temporalReconstructionAllowed
        ? TemporalRenderScaleForCurrentMode()
        : 1.0f;
    state.activeRenderScale =
        TemporalRenderScaleForExtents(displayExtent, state.activeInternalExtent);
    state.requiredInputMask = kTemporalUpscaleRequiredInputMask;
    state.requestedInternalExtent =
        TemporalRequestedInternalExtent(displayExtent, state.requestedRenderScale);

    const bool dlssModeActive =
        temporalReconstructionAllowed && TemporalDlssModeActive();
    const bool nativeTaaModeActive =
        temporalReconstructionAllowed && TemporalNativeTaaModeActive();
    const std::string upscalerProviderName =
        dlssModeActive ? std::string("dlss") : TemporalUpscalerProviderFromEnvironment();
    const bool renderScaleReduced =
        temporalReconstructionAllowed &&
        (
            state.requestedInternalExtent.width < displayExtent.width ||
            state.requestedInternalExtent.height < displayExtent.height
        );
    state.dynamicResolutionRequested =
        temporalReconstructionAllowed &&
        !nativeTaaModeActive &&
        DynamicResolutionRequestedFromEnvironment();
    state.taauRequested =
        (temporalReconstructionAllowed && dlssModeActive) ||
        (
            temporalReconstructionAllowed &&
            !nativeTaaModeActive &&
            TemporalUpscaleRequestedFromEnvironment()
        );
    state.temporalUpscalePostSourceRequested =
        (temporalReconstructionAllowed && dlssModeActive) ||
        (
            temporalReconstructionAllowed &&
            !nativeTaaModeActive &&
            TemporalUpscalePostSourceRequestedFromEnvironment()
        );
    state.dlssQualityMode = dlssModeActive
        ? TemporalDlssQualityModeForCurrentMode()
        : TemporalUpscalerDlssQualityModeFromEnvironment();
    state.dlssPreset = dlssModeActive
        ? TemporalDlssPresetForCurrentMode()
        : TemporalUpscalerDlssPresetFromEnvironment();
    state.upscalerPackage = ProbeTemporalUpscalerPackage(
        TemporalUpscalerProbeRequest{
            nativeTaaModeActive ? std::string{} : upscalerProviderName,
            TemporalUpscalerSdkRootFromEnvironment()
        }
    );
    state.upscalerPluginRequested =
        temporalReconstructionAllowed &&
        !nativeTaaModeActive &&
        (
            dlssModeActive ||
            TemporalUpscalerPluginRequestedFromEnvironment() ||
            state.upscalerPackage.requested > 0u
        );
    state.upscalerRuntime = QueryTemporalUpscalerRuntime(
        TemporalUpscalerRuntimeRequest{
            state.upscalerPackage,
            m_Instance,
            m_PhysicalDevice.Handle(),
            m_Device.Handle(),
            vkGetInstanceProcAddr,
            vkGetDeviceProcAddr,
            displayExtent,
            state.dlssQualityMode,
            state.dlssPreset,
            {}
        }
    );
    const std::optional<f32> dlssSharpnessOverride =
        DlssSharpnessOverrideFromEnvironment();
    if (dlssSharpnessOverride.has_value()) {
        state.upscalerRuntime.sharpness = *dlssSharpnessOverride;
    } else if (dlssModeActive) {
        state.upscalerRuntime.sharpness = 0.0f;
    }
    state.upscalerPackage.evaluateAdapterAvailable =
        state.upscalerRuntime.evaluateAdapterAvailable;
    state.temporalUpscaleRequested =
        !nativeTaaModeActive &&
        (state.taauRequested ||
        renderScaleReduced ||
        state.dynamicResolutionRequested ||
        state.upscalerPluginRequested);

    if (hdrSceneColorReady) {
        state.inputReadinessMask |= kTemporalUpscaleInputHdrSceneColorBit;
    }
    if (sceneDepthReady) {
        state.inputReadinessMask |= kTemporalUpscaleInputSceneDepthBit;
    }
    if (temporalState.velocityCameraMotionReady) {
        state.inputReadinessMask |= kTemporalUpscaleInputVelocityBit;
    }
    if (temporalState.taaHistoryColorReady) {
        state.inputReadinessMask |= kTemporalUpscaleInputHistoryColorBit;
    }
    if (temporalState.historyValid) {
        state.inputReadinessMask |= kTemporalUpscaleInputFrameStateBit;
    }
    state.temporalUpscaleContractReady =
        temporalState.temporalUpscaleInputReady &&
        (state.inputReadinessMask & state.requiredInputMask) ==
            state.requiredInputMask;

    state.renderScaleApplied =
        temporalReconstructionAllowed &&
        TemporalRenderScaleApplyEnabledForCurrentMode() &&
        ExtentsDiffer(state.activeInternalExtent, displayExtent);
    state.dynamicResolutionEnabled = false;
    state.temporalUpscaleEnabled = false;
    state.upscalerPluginAvailable =
        state.upscalerRuntime.evaluateAdapterAvailable > 0u;
    state.dlssQualityGateRequested =
        state.upscalerPluginRequested &&
        state.upscalerPackage.providerKind == TemporalUpscalerProviderKind::Dlss;
    state.dlssQualityEvaluateOutputReady = false;
    state.dlssQualityCameraMotionReady = temporalState.velocityCameraMotionReady;
    state.dlssQualitySceneContentMotionSupported =
        m_DlssQualitySceneContentMotionSupported;
    state.dlssQualityObjectMotionReady =
        temporalState.velocityObjectMotionReady &&
        state.dlssQualitySceneContentMotionSupported;
    state.dlssQualityReactiveMaskReady = false;
    state.dlssQualityTransparencyMaskReady = false;
    state.dlssQualityExposurePolicyReady = true;
    state.dlssQualityPostOrderingReady = false;
    state.dlssQualityReferenceBaselineReady =
        DlssReferenceBaselineReadyFromEnvironment();
    state.dlssQualityRequiredMask = kDlssQualityRequiredMask;
    state.dlssQualityReadyMask =
        (state.dlssQualityEvaluateOutputReady ? kDlssQualityEvaluateOutputBit : 0u) |
        (state.dlssQualityCameraMotionReady ? kDlssQualityCameraMotionBit : 0u) |
        (state.dlssQualityObjectMotionReady ? kDlssQualityObjectMotionBit : 0u) |
        (state.dlssQualityReactiveMaskReady ? kDlssQualityReactiveMaskBit : 0u) |
        (state.dlssQualityTransparencyMaskReady ? kDlssQualityTransparencyMaskBit : 0u) |
        (state.dlssQualityExposurePolicyReady ? kDlssQualityExposurePolicyBit : 0u) |
        (state.dlssQualityPostOrderingReady ? kDlssQualityPostOrderingBit : 0u) |
        (state.dlssQualityReferenceBaselineReady ? kDlssQualityReferenceBaselineBit : 0u);
    state.dlssQualityBlockerMask =
        state.dlssQualityGateRequested
            ? kDlssQualityRequiredMask & ~state.dlssQualityReadyMask
            : 0u;
    state.dlssQualityGateReady =
        state.dlssQualityGateRequested && state.dlssQualityBlockerMask == 0u;
    state.dlssQualityGateFallbackReason =
        state.dlssQualityGateRequested
            ? DlssQualityGateFallbackFromBlockers(state.dlssQualityBlockerMask)
            : RendererDlssQualityGateFallbackReason::NotRequested;
    if (!state.temporalUpscaleRequested) {
        state.fallbackReason =
            RendererTemporalUpscaleFallbackReason::Disabled;
    } else if (state.dynamicResolutionRequested) {
        state.fallbackReason =
            RendererTemporalUpscaleFallbackReason::DynamicResolutionUnsupported;
    } else if (!renderScaleReduced && !state.upscalerPluginRequested) {
        state.fallbackReason =
            RendererTemporalUpscaleFallbackReason::FullResolution;
    } else if (!state.temporalUpscaleContractReady) {
        state.fallbackReason =
            RendererTemporalUpscaleFallbackReason::InputsUnavailable;
    } else if (state.upscalerPluginAvailable) {
        state.temporalUpscaleEnabled = true;
        state.fallbackReason =
            RendererTemporalUpscaleFallbackReason::None;
    } else {
        state.fallbackReason =
            RendererTemporalUpscaleFallbackReason::UpscalerUnavailable;
    }

    return state;
}

void VulkanRenderer::StoreTemporalHistory(
    const FrameMatrices* matrices,
    const VkExtent2D& extent,
    const FrameTemporalState* temporalState
) {
    if (matrices == nullptr) {
        m_TemporalHistoryValid = false;
        m_PreviousTemporalJitterPixels = glm::vec2(0.0f);
        m_PreviousTemporalJitterUv = glm::vec2(0.0f);
        m_PreviousTemporalJitterApplied = false;
        return;
    }

    m_PreviousTemporalMatrices = *matrices;
    m_PreviousTemporalExtent = extent;
    if (temporalState != nullptr && temporalState->jitterApplied) {
        m_PreviousTemporalJitterPixels = temporalState->jitterPixels;
        m_PreviousTemporalJitterUv = temporalState->jitterUv;
        m_PreviousTemporalJitterApplied = true;
    } else {
        m_PreviousTemporalJitterPixels = glm::vec2(0.0f);
        m_PreviousTemporalJitterUv = glm::vec2(0.0f);
        m_PreviousTemporalJitterApplied = false;
    }
    m_TemporalHistoryValid = true;
    ++m_TemporalFrameCounter;
}

void VulkanRenderer::PopulateTemporalUniforms(
    UniformBufferObject& uniformData,
    const FrameTemporalState* temporalState
) const {
    if (temporalState == nullptr) {
        uniformData.previousView = uniformData.view;
        uniformData.previousProj = uniformData.proj;
        uniformData.temporalJitter = glm::vec4(0.0f);
        uniformData.temporalControls = glm::vec4(0.0f);
        uniformData.temporalResolveControls = glm::vec4(0.0f);
        uniformData.temporalRejectionControls = glm::vec4(0.0f);
        return;
    }

    uniformData.previousView = temporalState->previousMatrices.view;
    uniformData.previousProj = temporalState->previousMatrices.proj;
    uniformData.temporalJitter = glm::vec4(
        temporalState->jitterPixels,
        temporalState->jitterUv
    );
    if (temporalState->jitterApplied) {
        ApplyProjectionJitter(uniformData.proj, temporalState->jitterUv);
        uniformData.invProj = glm::inverse(uniformData.proj);
    }
    uniformData.temporalControls = glm::vec4(
        temporalState->velocityCameraMotionReady ? 1.0f : 0.0f,
        temporalState->historyValid ? 1.0f : 0.0f,
        temporalState->jitterApplied ? 1.0f : 0.0f,
        static_cast<f32>(temporalState->jitterSequenceIndex)
    );
    uniformData.temporalResolveControls = glm::vec4(
        temporalState->taaResolveEnabled ? 1.0f : 0.0f,
        temporalState->taaHistoryWeight,
        temporalState->taaHistoryColorReady ? 1.0f : 0.0f,
        temporalState->taaVelocityReprojectionEnabled ? 1.0f : 0.0f
    );
    uniformData.temporalRejectionControls = glm::vec4(
        temporalState->taaRejectionEnabled ? 1.0f : 0.0f,
        temporalState->taaVelocityRejectionThreshold,
        temporalState->taaDepthRejectionThreshold,
        temporalState->taaNeighborhoodClampEnabled ? 1.0f : 0.0f
    );
}

void VulkanRenderer::WriteTemporalStats(
    const FrameTemporalState& temporalState,
    const FrameTemporalUpscaleState& temporalUpscaleState,
    bool velocityTargetAllocated,
    VkFormat velocityFormat,
    bool materialAuxTargetAllocated,
    VkFormat materialAuxFormat,
    bool historyColorTargetAllocated,
    VkFormat historyColorFormat,
    bool temporalUpscaleOutputAllocated,
    VkFormat temporalUpscaleOutputFormat,
    VkExtent2D temporalUpscaleOutputExtent,
    u32 historyColorCopyCount,
    RendererTemporalStats& stats
) const {
    stats.antialiasingMode = static_cast<u32>(m_TemporalAntialiasingMode);
    stats.velocityTargetAllocated = velocityTargetAllocated ? 1u : 0u;
    stats.velocityFormat =
        velocityTargetAllocated ? velocityFormat : VK_FORMAT_UNDEFINED;
    stats.velocityCameraMotionEnabled = 1u;
    stats.velocityCameraMotionReady =
        temporalState.velocityCameraMotionReady ? 1u : 0u;
    stats.velocityObjectMotionReady =
        temporalState.velocityObjectMotionReady ? 1u : 0u;
    stats.velocityMaterialAuxTargetAllocated =
        materialAuxTargetAllocated ? 1u : 0u;
    stats.velocityMaterialAuxFormat =
        materialAuxTargetAllocated ? materialAuxFormat : VK_FORMAT_UNDEFINED;
    stats.velocityMaterialAuxMigrated =
        temporalState.velocityMaterialAuxMigrated ? 1u : 0u;
    stats.historyValid = temporalState.historyValid ? 1u : 0u;
    stats.historyReset = temporalState.historyReset ? 1u : 0u;
    stats.historyResetReason = static_cast<u32>(temporalState.resetReason);
    stats.jitterEnabled = temporalState.jitterEnabled ? 1u : 0u;
    stats.jitterApplied = temporalState.jitterApplied ? 1u : 0u;
    stats.jitterSequenceIndex = temporalState.jitterSequenceIndex;
    stats.jitterPixelsX = temporalState.jitterPixels.x;
    stats.jitterPixelsY = temporalState.jitterPixels.y;
    stats.jitterUvX = temporalState.jitterUv.x;
    stats.jitterUvY = temporalState.jitterUv.y;
    stats.velocityJitteredHistoryPolicy =
        temporalState.velocityJitteredHistoryPolicy ? 1u : 0u;
    stats.velocityPreviousJitterApplied =
        temporalState.velocityPreviousJitterApplied ? 1u : 0u;
    stats.previousJitterPixelsX = temporalState.previousJitterPixels.x;
    stats.previousJitterPixelsY = temporalState.previousJitterPixels.y;
    stats.previousJitterUvX = temporalState.previousJitterUv.x;
    stats.previousJitterUvY = temporalState.previousJitterUv.y;
    stats.taaResolveConfigured =
        temporalState.taaResolveConfigured ? 1u : 0u;
    stats.taaResolveEnabled =
        temporalState.taaResolveEnabled ? 1u : 0u;
    stats.taaResolveSuppressedForUpscaler =
        temporalState.taaResolveSuppressedForUpscaler ? 1u : 0u;
    stats.taaHistoryColorTargetAllocated =
        historyColorTargetAllocated ? 1u : 0u;
    stats.taaHistoryColorFormat =
        historyColorTargetAllocated ? historyColorFormat : VK_FORMAT_UNDEFINED;
    stats.taaHistoryColorReady =
        temporalState.taaHistoryColorReady ? 1u : 0u;
    stats.taaHistoryColorCopies = historyColorCopyCount;
    stats.taaHistoryWeight = temporalState.taaHistoryWeight;
    stats.taaVelocityReprojectionEnabled =
        temporalState.taaVelocityReprojectionEnabled ? 1u : 0u;
    stats.taaFallbackReason =
        static_cast<u32>(temporalState.taaFallbackReason);
    stats.taaRejectionEnabled =
        temporalState.taaRejectionEnabled ? 1u : 0u;
    stats.taaNeighborhoodClampEnabled =
        temporalState.taaNeighborhoodClampEnabled ? 1u : 0u;
    stats.taaVelocityRejectionThreshold =
        temporalState.taaVelocityRejectionThreshold;
    stats.taaDepthRejectionThreshold =
        temporalState.taaDepthRejectionThreshold;
    stats.renderScaleRequested = temporalUpscaleState.requestedRenderScale;
    stats.renderScaleActive = temporalUpscaleState.activeRenderScale;
    stats.renderScaleApplied =
        temporalUpscaleState.renderScaleApplied ? 1u : 0u;
    stats.temporalUpscaleDisplayWidth =
        temporalUpscaleState.displayExtent.width;
    stats.temporalUpscaleDisplayHeight =
        temporalUpscaleState.displayExtent.height;
    stats.temporalUpscaleRequestedWidth =
        temporalUpscaleState.requestedInternalExtent.width;
    stats.temporalUpscaleRequestedHeight =
        temporalUpscaleState.requestedInternalExtent.height;
    stats.temporalUpscaleActiveWidth =
        temporalUpscaleState.activeInternalExtent.width;
    stats.temporalUpscaleActiveHeight =
        temporalUpscaleState.activeInternalExtent.height;
    stats.temporalUpscaleOutputAllocated =
        temporalUpscaleOutputAllocated ? 1u : 0u;
    stats.temporalUpscaleOutputFormat =
        temporalUpscaleOutputAllocated
            ? temporalUpscaleOutputFormat
            : VK_FORMAT_UNDEFINED;
    stats.temporalUpscaleOutputWidth =
        temporalUpscaleOutputAllocated
            ? temporalUpscaleOutputExtent.width
            : 0u;
    stats.temporalUpscaleOutputHeight =
        temporalUpscaleOutputAllocated
            ? temporalUpscaleOutputExtent.height
            : 0u;
    stats.temporalUpscalePostSourceRequested =
        temporalUpscaleState.temporalUpscalePostSourceRequested ? 1u : 0u;
    stats.temporalUpscalePostSourceActive = 0u;
    stats.temporalUpscalePostSourceFallbackReason =
        temporalUpscaleState.temporalUpscalePostSourceRequested
            ? static_cast<u32>(
                TemporalUpscalePostSourceFallbackReason::EvaluateOutputUnavailable
            )
            : static_cast<u32>(
                TemporalUpscalePostSourceFallbackReason::Disabled
            );
    stats.dynamicResolutionRequested =
        temporalUpscaleState.dynamicResolutionRequested ? 1u : 0u;
    stats.dynamicResolutionEnabled =
        temporalUpscaleState.dynamicResolutionEnabled ? 1u : 0u;
    stats.taauRequested =
        temporalUpscaleState.taauRequested ? 1u : 0u;
    stats.temporalUpscaleRequested =
        temporalUpscaleState.temporalUpscaleRequested ? 1u : 0u;
    stats.temporalUpscaleEnabled =
        temporalUpscaleState.temporalUpscaleEnabled ? 1u : 0u;
    stats.temporalUpscaleInputReady =
        temporalState.temporalUpscaleInputReady ? 1u : 0u;
    stats.temporalUpscaleFallbackReason =
        static_cast<u32>(temporalUpscaleState.fallbackReason);
    stats.temporalUpscaleInputReadinessMask =
        temporalUpscaleState.inputReadinessMask;
    stats.temporalUpscaleRequiredInputMask =
        temporalUpscaleState.requiredInputMask;
    stats.temporalUpscaleContractReady =
        temporalUpscaleState.temporalUpscaleContractReady ? 1u : 0u;
    stats.temporalUpscalerPluginRequested =
        temporalUpscaleState.upscalerPluginRequested ? 1u : 0u;
    stats.temporalUpscalerPluginAvailable =
        temporalUpscaleState.upscalerPluginAvailable ? 1u : 0u;
    stats.temporalUpscalerProviderKind =
        static_cast<u32>(temporalUpscaleState.upscalerPackage.providerKind);
    stats.temporalUpscalerPackageFallbackReason =
        static_cast<u32>(temporalUpscaleState.upscalerPackage.fallbackReason);
    stats.temporalUpscalerPackageDirectoryFound =
        temporalUpscaleState.upscalerPackage.packageDirectoryFound;
    stats.temporalUpscalerHeadersFound =
        temporalUpscaleState.upscalerPackage.headersFound;
    stats.temporalUpscalerImportLibraryFound =
        temporalUpscaleState.upscalerPackage.importLibraryFound;
    stats.temporalUpscalerRuntimeFound =
        temporalUpscaleState.upscalerPackage.runtimeFound;
    stats.temporalUpscalerDlssSuperResolutionSymbolsFound =
        temporalUpscaleState.upscalerPackage.superResolutionSymbolsFound;
    stats.temporalUpscalerDlssFrameGenerationSymbolsFound =
        temporalUpscaleState.upscalerPackage.frameGenerationSymbolsFound;
    stats.temporalUpscalerDlssRayReconstructionSymbolsFound =
        temporalUpscaleState.upscalerPackage.rayReconstructionSymbolsFound;
    stats.temporalUpscalerDlssTransformerPresetSymbolsFound =
        temporalUpscaleState.upscalerPackage.transformerPresetSymbolsFound;
    stats.temporalUpscalerSdkVersionMajor =
        temporalUpscaleState.upscalerPackage.sdkVersionMajor;
    stats.temporalUpscalerSdkVersionMinor =
        temporalUpscaleState.upscalerPackage.sdkVersionMinor;
    stats.temporalUpscalerSdkVersionPatch =
        temporalUpscaleState.upscalerPackage.sdkVersionPatch;
    stats.temporalUpscalerPackageReady =
        temporalUpscaleState.upscalerPackage.packageReady;
    stats.temporalUpscalerEvaluateAdapterAvailable =
        temporalUpscaleState.upscalerRuntime.evaluateAdapterAvailable;
    stats.temporalUpscalerRuntimeFallbackReason =
        static_cast<u32>(temporalUpscaleState.upscalerRuntime.fallbackReason);
    stats.temporalUpscalerAdapterCompiled =
        temporalUpscaleState.upscalerRuntime.adapterCompiled;
    stats.temporalUpscalerInitializationAttempted =
        temporalUpscaleState.upscalerRuntime.initializationAttempted;
    stats.temporalUpscalerInitialized =
        temporalUpscaleState.upscalerRuntime.initialized;
    stats.temporalUpscalerInitializationResult =
        temporalUpscaleState.upscalerRuntime.initializationResult;
    stats.temporalUpscalerCapabilityParametersReady =
        temporalUpscaleState.upscalerRuntime.capabilityParametersReady;
    stats.temporalUpscalerCapabilityQueryResult =
        temporalUpscaleState.upscalerRuntime.capabilityQueryResult;
    stats.temporalUpscalerFeatureRequirementsQueried =
        temporalUpscaleState.upscalerRuntime.featureRequirementsQueried;
    stats.temporalUpscalerFeatureRequirementsResult =
        temporalUpscaleState.upscalerRuntime.featureRequirementsResult;
    stats.temporalUpscalerFeatureSupportedMask =
        temporalUpscaleState.upscalerRuntime.featureSupportedMask;
    stats.temporalUpscalerFeatureRequirementsSupported =
        temporalUpscaleState.upscalerRuntime.featureRequirementsSupported;
    stats.temporalUpscalerMinHardwareArchitecture =
        temporalUpscaleState.upscalerRuntime.minHardwareArchitecture;
    stats.temporalUpscalerMinOsVersion =
        temporalUpscaleState.upscalerRuntime.minOsVersion;
    stats.temporalUpscalerInstanceExtensionRequirementsQueried =
        temporalUpscaleState.upscalerRuntime.instanceExtensionRequirementsQueried;
    stats.temporalUpscalerInstanceExtensionRequirementsResult =
        temporalUpscaleState.upscalerRuntime.instanceExtensionRequirementsResult;
    stats.temporalUpscalerInstanceExtensionRequirementCount =
        temporalUpscaleState.upscalerRuntime.instanceExtensionRequirementCount;
    stats.temporalUpscalerInstanceExtensionAvailableCount =
        temporalUpscaleState.upscalerRuntime.instanceExtensionAvailableCount;
    stats.temporalUpscalerInstanceExtensionMissingAvailableCount =
        temporalUpscaleState.upscalerRuntime.instanceExtensionMissingAvailableCount;
    stats.temporalUpscalerInstanceExtensionEnabledCount =
        temporalUpscaleState.upscalerRuntime.instanceExtensionEnabledCount;
    stats.temporalUpscalerInstanceExtensionMissingEnabledCount =
        temporalUpscaleState.upscalerRuntime.instanceExtensionMissingEnabledCount;
    stats.temporalUpscalerInstanceExtensionRequirements =
        temporalUpscaleState.upscalerRuntime.instanceExtensionRequirements;
    stats.temporalUpscalerInstanceExtensionMissingAvailable =
        temporalUpscaleState.upscalerRuntime.instanceExtensionMissingAvailable;
    stats.temporalUpscalerInstanceExtensionMissingEnabled =
        temporalUpscaleState.upscalerRuntime.instanceExtensionMissingEnabled;
    stats.temporalUpscalerDeviceExtensionRequirementsQueried =
        temporalUpscaleState.upscalerRuntime.deviceExtensionRequirementsQueried;
    stats.temporalUpscalerDeviceExtensionRequirementsResult =
        temporalUpscaleState.upscalerRuntime.deviceExtensionRequirementsResult;
    stats.temporalUpscalerDeviceExtensionRequirementCount =
        temporalUpscaleState.upscalerRuntime.deviceExtensionRequirementCount;
    stats.temporalUpscalerDeviceExtensionAvailableCount =
        temporalUpscaleState.upscalerRuntime.deviceExtensionAvailableCount;
    stats.temporalUpscalerDeviceExtensionMissingAvailableCount =
        temporalUpscaleState.upscalerRuntime.deviceExtensionMissingAvailableCount;
    stats.temporalUpscalerDeviceExtensionEnabledCount =
        temporalUpscaleState.upscalerRuntime.deviceExtensionEnabledCount;
    stats.temporalUpscalerDeviceExtensionMissingEnabledCount =
        temporalUpscaleState.upscalerRuntime.deviceExtensionMissingEnabledCount;
    stats.temporalUpscalerDeviceExtensionRequirements =
        temporalUpscaleState.upscalerRuntime.deviceExtensionRequirements;
    stats.temporalUpscalerDeviceExtensionMissingAvailable =
        temporalUpscaleState.upscalerRuntime.deviceExtensionMissingAvailable;
    stats.temporalUpscalerDeviceExtensionMissingEnabled =
        temporalUpscaleState.upscalerRuntime.deviceExtensionMissingEnabled;
    stats.temporalUpscalerRuntimeFlavor =
        temporalUpscaleState.upscalerRuntime.runtimeFlavor;
    stats.temporalUpscalerRuntimePathOverridden =
        temporalUpscaleState.upscalerRuntime.runtimePathOverridden;
    stats.temporalUpscalerRuntimePathFound =
        temporalUpscaleState.upscalerRuntime.runtimePathFound;
    stats.temporalUpscalerRuntimePath =
        temporalUpscaleState.upscalerRuntime.runtimePath;
    stats.temporalUpscalerRuntimeDllFound =
        temporalUpscaleState.upscalerRuntime.runtimeDllFound;
    stats.temporalUpscalerRuntimeDllSizeBytes =
        temporalUpscaleState.upscalerRuntime.runtimeDllSizeBytes;
    stats.temporalUpscalerRuntimeDllHash =
        temporalUpscaleState.upscalerRuntime.runtimeDllHash;
    stats.temporalUpscalerDlssSuperResolutionSupported =
        temporalUpscaleState.upscalerRuntime.superResolutionSupported;
    stats.temporalUpscalerNeedsUpdatedDriver =
        temporalUpscaleState.upscalerRuntime.needsUpdatedDriver;
    stats.temporalUpscalerMinDriverVersionMajor =
        temporalUpscaleState.upscalerRuntime.minDriverVersionMajor;
    stats.temporalUpscalerMinDriverVersionMinor =
        temporalUpscaleState.upscalerRuntime.minDriverVersionMinor;
    stats.temporalUpscalerFeatureInitResult =
        temporalUpscaleState.upscalerRuntime.featureInitResult;
    stats.temporalUpscalerDlssQualityMode =
        temporalUpscaleState.upscalerRuntime.dlssQualityMode;
    stats.temporalUpscalerDlssRecommendedPreset =
        temporalUpscaleState.upscalerRuntime.recommendedPreset;
    stats.temporalUpscalerOptimalSettingsQueried =
        temporalUpscaleState.upscalerRuntime.optimalSettingsQueried;
    stats.temporalUpscalerOptimalSettingsResult =
        temporalUpscaleState.upscalerRuntime.optimalSettingsResult;
    stats.temporalUpscalerOptimalRenderWidth =
        temporalUpscaleState.upscalerRuntime.optimalRenderWidth;
    stats.temporalUpscalerOptimalRenderHeight =
        temporalUpscaleState.upscalerRuntime.optimalRenderHeight;
    stats.temporalUpscalerMinRenderWidth =
        temporalUpscaleState.upscalerRuntime.minRenderWidth;
    stats.temporalUpscalerMinRenderHeight =
        temporalUpscaleState.upscalerRuntime.minRenderHeight;
    stats.temporalUpscalerMaxRenderWidth =
        temporalUpscaleState.upscalerRuntime.maxRenderWidth;
    stats.temporalUpscalerMaxRenderHeight =
        temporalUpscaleState.upscalerRuntime.maxRenderHeight;
    stats.temporalUpscalerSharpness =
        temporalUpscaleState.upscalerRuntime.sharpness;
    stats.temporalUpscalerDlssQualityGateRequested =
        temporalUpscaleState.dlssQualityGateRequested ? 1u : 0u;
    stats.temporalUpscalerDlssQualityGateReady =
        temporalUpscaleState.dlssQualityGateReady ? 1u : 0u;
    stats.temporalUpscalerDlssQualityGateFallbackReason =
        static_cast<u32>(temporalUpscaleState.dlssQualityGateFallbackReason);
    stats.temporalUpscalerDlssQualityRequiredMask =
        temporalUpscaleState.dlssQualityRequiredMask;
    stats.temporalUpscalerDlssQualityReadyMask =
        temporalUpscaleState.dlssQualityReadyMask;
    stats.temporalUpscalerDlssQualityBlockerMask =
        temporalUpscaleState.dlssQualityBlockerMask;
    stats.temporalUpscalerDlssQualityEvaluateOutputReady =
        temporalUpscaleState.dlssQualityEvaluateOutputReady ? 1u : 0u;
    stats.temporalUpscalerDlssQualityCameraMotionReady =
        temporalUpscaleState.dlssQualityCameraMotionReady ? 1u : 0u;
    stats.temporalUpscalerDlssQualityObjectMotionReady =
        temporalUpscaleState.dlssQualityObjectMotionReady ? 1u : 0u;
    stats.temporalUpscalerDlssQualitySceneContentMotionSupported =
        temporalUpscaleState.dlssQualitySceneContentMotionSupported ? 1u : 0u;
    stats.temporalUpscalerDlssQualityReactiveMaskReady =
        temporalUpscaleState.dlssQualityReactiveMaskReady ? 1u : 0u;
    stats.temporalUpscalerDlssQualityTransparencyMaskReady =
        temporalUpscaleState.dlssQualityTransparencyMaskReady ? 1u : 0u;
    stats.temporalUpscalerDlssQualityExposurePolicyReady =
        temporalUpscaleState.dlssQualityExposurePolicyReady ? 1u : 0u;
    stats.temporalUpscalerDlssQualityPostOrderingReady =
        temporalUpscaleState.dlssQualityPostOrderingReady ? 1u : 0u;
    stats.temporalUpscalerDlssQualityReferenceBaselineReady =
        temporalUpscaleState.dlssQualityReferenceBaselineReady ? 1u : 0u;
    FinalizeDlssQualityGateStats(stats);
}

void VulkanRenderer::UpdateUniformBuffer(
    std::size_t imageIndex,
    const FrameMatrices* matrices,
    const glm::mat4& lightViewProjection,
    const FrameLightConstants& lights,
    const FrameReflectionProbeSet& reflectionProbes,
    bool shadowSamplingEnabled,
    const FrameTemporalState* temporalState
) const {
    UniformBufferObject uniformData{};
    if (matrices != nullptr) {
        uniformData.view = matrices->view;
        uniformData.proj = matrices->proj;
        uniformData.invView = glm::inverse(matrices->view);
        uniformData.invProj = glm::inverse(matrices->proj);
    }
    PopulateTemporalUniforms(uniformData, temporalState);
    uniformData.lightViewProj = lightViewProjection;
    uniformData.directionalLight = lights.directionalLight;
    uniformData.ambientLight = lights.ambientLight;
    uniformData.shadowControls = glm::vec4(
        shadowSamplingEnabled ? 1.0f : 0.0f,
        m_ShadowSettings.strength,
        m_ShadowSettings.biasMin,
        m_ShadowSettings.biasSlope
    );
    uniformData.shadowFiltering = glm::vec4(
        m_ShadowSettings.pcfRadius,
        m_ShadowSettings.ambientStrength,
        static_cast<f32>(static_cast<int>(m_RenderDebugSettings.forwardView)),
        m_RenderDebugSettings.exposure
    );
    uniformData.contactShadowControls = glm::vec4(
        std::clamp(m_ShadowSettings.contactShadowStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.contactShadowLength, 0.0f, 1.0f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.contactShadowSteps,
            0u,
            12u
        )),
        std::clamp(m_ShadowSettings.contactShadowThickness, 0.0f, 0.5f)
    );
    uniformData.contactShadowStabilityControls = glm::vec4(
        std::clamp(m_ShadowSettings.contactShadowJitterStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.contactShadowEdgeFadePixels, 0.0f, 96.0f),
        0.0f,
        0.0f
    );
    const f32 ssaoRadiusScale = TemporalDlssSrModeActive()
        ? TemporalRenderScaleForCurrentMode()
        : 1.0f;
    uniformData.ssaoControls = glm::vec4(
        std::clamp(m_ShadowSettings.ssaoStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.ssaoRadius * ssaoRadiusScale, 0.0f, 8.0f),
        std::clamp(m_ShadowSettings.ssaoBias, 0.0f, 0.5f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.ssaoSampleCount,
            0u,
            16u
        ))
    );
    const f32 ssrTraceControl = static_cast<f32>(std::clamp<u32>(
        m_ShadowSettings.ssrStepCount,
        0u,
        32u
    )) +
        (SsrHiZResourcesReady() ? 64.0f : 0.0f) +
        (m_ShadowSettings.ssrSceneColorHistoryEnabled ? 128.0f : 0.0f) +
        (m_ShadowSettings.ssrHitValidationEnabled ? 256.0f : 0.0f) +
        (SsrReconstructionResourcesReady() &&
            m_ShadowSettings.ssrStrength > 0.0001f &&
            m_ShadowSettings.ssrRayLength > 0.0001f &&
            m_ShadowSettings.ssrStepCount > 0u
            ? 512.0f
            : 0.0f) +
        (m_ShadowSettings.ssrDeferredReceiverReprojectionEnabled ? 1024.0f : 0.0f) +
        (m_ShadowSettings.ssrCurrentHdrSourceEnabled ? 2048.0f : 0.0f) +
        (m_ShadowSettings.ssrCurrentHdrRadianceFilterEnabled ? 4096.0f : 0.0f) +
        (m_ShadowSettings.ssrSpatialVarianceClampEnabled ? 8192.0f : 0.0f) +
        (m_ShadowSettings.ssrProbeFallbackBlendEnabled ? 16384.0f : 0.0f) +
        (m_ShadowSettings.ssrTemporalHistoryLockEnabled ? 32768.0f : 0.0f) +
        (m_ShadowSettings.ssrTemporalMissHistoryRejectEnabled ? 65536.0f : 0.0f);
    uniformData.ssrControls = glm::vec4(
        std::clamp(m_ShadowSettings.ssrStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.ssrRayLength, 0.0f, 64.0f),
        std::clamp(m_ShadowSettings.ssrThickness, 0.0f, 0.5f),
        ssrTraceControl *
            (m_ShadowSettings.ssrRefinementEnabled ? 1.0f : -1.0f)
    );
    uniformData.reflectionProbeControls = glm::vec4(
        m_ShadowSettings.reflectionProbeFallbackEnabled ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.reflectionProbeDiffuseIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.reflectionProbeSpecularIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.reflectionProbeHorizonBlend, 0.0f, 1.0f)
    );
    PopulateReflectionProbeUniforms(
        reflectionProbes,
        m_ShadowSettings.globalIblCubemapEnabled,
        m_ShadowSettings.reflectionProbeCubemapEnabled,
        uniformData
    );
    const bool temporalSkyboxStabilization =
        TemporalDlssModeActive() ||
        TemporalNativeTaaModeActive() ||
        TaaResolveEnabledFromEnvironment();
    uniformData.environmentControls = glm::vec4(
        m_ShadowSettings.skyboxEnabled ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.skyboxIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.skyboxBlur, 0.0f, 8.0f),
        temporalSkyboxStabilization ? 2.0f : 1.0f
    );
    PopulateProbeGridUniforms(uniformData);
    const bool heightFogApplied =
        m_ShadowSettings.heightFogEnabled &&
        m_ShadowSettings.heightFogDensity > 0.0001f &&
        m_ShadowSettings.heightFogMaxOpacity > 0.0001f;
    uniformData.heightFogControls = glm::vec4(
        heightFogApplied ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.heightFogDensity, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.heightFogHeightFalloff, 0.0f, 2.0f),
        std::clamp(m_ShadowSettings.heightFogStartDistance, 0.0f, 1000.0f)
    );
    uniformData.heightFogColor = glm::vec4(
        std::clamp(m_ShadowSettings.heightFogColorR, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogColorG, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogColorB, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogMaxOpacity, 0.0f, 1.0f)
    );
    const bool bloomApplied =
        m_RenderDebugSettings.bloomEnabled &&
        m_RenderDebugSettings.bloomIntensity > 0.0001f &&
        m_RenderDebugSettings.bloomRadiusPixels > 0.0001f;
    uniformData.postProcessControls = glm::vec4(
        bloomApplied ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.bloomIntensity, 0.0f, 4.0f),
        std::clamp(m_RenderDebugSettings.bloomThreshold, 0.0f, 16.0f),
        std::clamp(m_RenderDebugSettings.bloomRadiusPixels, 0.0f, 24.0f)
    );
    uniformData.colorGradingControls = glm::vec4(
        m_RenderDebugSettings.colorGradingEnabled ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.colorGradingSaturation, 0.0f, 2.5f),
        std::clamp(m_RenderDebugSettings.colorGradingContrast, 0.0f, 2.5f),
        std::clamp(m_RenderDebugSettings.colorGradingGamma, 0.25f, 4.0f)
    );
    const bool colorGradingLutReady =
        m_ColorGradingLut != nullptr && m_ColorGradingLut->Uploaded();
    const bool colorGradingLutApplied =
        m_RenderDebugSettings.colorGradingEnabled &&
        colorGradingLutReady &&
        m_RenderDebugSettings.colorGradingLutStrength > 0.0001f;
    uniformData.colorGradingLutControls = glm::vec4(
        colorGradingLutApplied
            ? std::clamp(m_RenderDebugSettings.colorGradingLutStrength, 0.0f, 1.0f)
            : 0.0f,
        static_cast<f32>(kColorGradingLutSize),
        colorGradingLutReady ? 1.0f : 0.0f,
        m_RenderDebugSettings.colorGradingEnabled && !colorGradingLutReady ? 1.0f : 0.0f
    );
    uniformData.debugControls = glm::vec4(
        static_cast<f32>(std::clamp(
            m_RenderDebugSettings.localShadowDebugLightIndex,
            -1,
            static_cast<i32>(kRendererMaxFrameLocalLights) - 1
        )),
        0.0f,
        0.0f,
        0.0f
    );
    uniformData.toneMappingControls = glm::vec4(
        static_cast<f32>(std::clamp<u32>(m_RenderDebugSettings.toneMapMode, 0u, 2u)),
        std::clamp(m_RenderDebugSettings.exposure, 0.001f, 32.0f),
        std::clamp(m_RenderDebugSettings.toneMapWhitePoint, 0.1f, 64.0f),
        m_RenderDebugSettings.autoExposureEnabled ? 1.0f : 0.0f
    );
    const f32 autoExposureMin = std::clamp(m_RenderDebugSettings.autoExposureMin, 0.001f, 32.0f);
    uniformData.autoExposureControls = glm::vec4(
        std::clamp(m_RenderDebugSettings.autoExposureTargetLuminance, 0.001f, 4.0f),
        autoExposureMin,
        std::max(autoExposureMin, std::clamp(m_RenderDebugSettings.autoExposureMax, 0.001f, 32.0f)),
        std::clamp(m_RenderDebugSettings.autoExposureAdaptation, 0.0f, 1.0f)
    );
    const bool sharpeningApplied =
        m_RenderDebugSettings.sharpeningEnabled &&
        m_RenderDebugSettings.sharpeningStrength > 0.0001f &&
        m_RenderDebugSettings.sharpeningRadiusPixels > 0.0001f;
    uniformData.sharpeningControls = glm::vec4(
        sharpeningApplied ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.sharpeningStrength, 0.0f, 2.0f),
        std::clamp(m_RenderDebugSettings.sharpeningRadiusPixels, 0.0f, 4.0f),
        0.0f
    );

    m_UniformBuffer->Update(imageIndex, uniformData);
}

void VulkanRenderer::UpdateOverlayUniformBuffer(
    std::size_t imageIndex,
    const FrameMatrices* matrices,
    const glm::mat4& lightViewProjection,
    const FrameLightConstants& lights,
    const FrameReflectionProbeSet& reflectionProbes,
    bool shadowSamplingEnabled
) const {
    if (m_OverlayUniformBuffer == nullptr || matrices == nullptr) {
        return;
    }

    UniformBufferObject uniformData{};
    uniformData.view = matrices->view;
    uniformData.proj = matrices->proj;
    uniformData.invView = glm::inverse(matrices->view);
    uniformData.invProj = glm::inverse(matrices->proj);
    uniformData.lightViewProj = lightViewProjection;
    uniformData.directionalLight = lights.directionalLight;
    uniformData.ambientLight = lights.ambientLight;
    uniformData.shadowControls = glm::vec4(
        shadowSamplingEnabled ? 1.0f : 0.0f,
        m_ShadowSettings.strength,
        m_ShadowSettings.biasMin,
        m_ShadowSettings.biasSlope
    );
    uniformData.shadowFiltering = glm::vec4(
        m_ShadowSettings.pcfRadius,
        m_ShadowSettings.ambientStrength,
        static_cast<f32>(static_cast<int>(m_RenderDebugSettings.forwardView)),
        m_RenderDebugSettings.exposure
    );
    uniformData.contactShadowControls = glm::vec4(
        std::clamp(m_ShadowSettings.contactShadowStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.contactShadowLength, 0.0f, 1.0f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.contactShadowSteps,
            0u,
            12u
        )),
        std::clamp(m_ShadowSettings.contactShadowThickness, 0.0f, 0.5f)
    );
    uniformData.contactShadowStabilityControls = glm::vec4(
        std::clamp(m_ShadowSettings.contactShadowJitterStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.contactShadowEdgeFadePixels, 0.0f, 96.0f),
        0.0f,
        0.0f
    );
    const f32 ssaoRadiusScale = TemporalDlssSrModeActive()
        ? TemporalRenderScaleForCurrentMode()
        : 1.0f;
    uniformData.ssaoControls = glm::vec4(
        std::clamp(m_ShadowSettings.ssaoStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.ssaoRadius * ssaoRadiusScale, 0.0f, 8.0f),
        std::clamp(m_ShadowSettings.ssaoBias, 0.0f, 0.5f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.ssaoSampleCount,
            0u,
            16u
        ))
    );
    const f32 ssrTraceControl = static_cast<f32>(std::clamp<u32>(
        m_ShadowSettings.ssrStepCount,
        0u,
        32u
    )) +
        (SsrHiZResourcesReady() ? 64.0f : 0.0f) +
        (m_ShadowSettings.ssrSceneColorHistoryEnabled ? 128.0f : 0.0f) +
        (m_ShadowSettings.ssrHitValidationEnabled ? 256.0f : 0.0f) +
        (SsrReconstructionResourcesReady() &&
            m_ShadowSettings.ssrStrength > 0.0001f &&
            m_ShadowSettings.ssrRayLength > 0.0001f &&
            m_ShadowSettings.ssrStepCount > 0u
            ? 512.0f
            : 0.0f) +
        (m_ShadowSettings.ssrDeferredReceiverReprojectionEnabled ? 1024.0f : 0.0f) +
        (m_ShadowSettings.ssrCurrentHdrSourceEnabled ? 2048.0f : 0.0f) +
        (m_ShadowSettings.ssrCurrentHdrRadianceFilterEnabled ? 4096.0f : 0.0f) +
        (m_ShadowSettings.ssrSpatialVarianceClampEnabled ? 8192.0f : 0.0f) +
        (m_ShadowSettings.ssrProbeFallbackBlendEnabled ? 16384.0f : 0.0f) +
        (m_ShadowSettings.ssrTemporalHistoryLockEnabled ? 32768.0f : 0.0f) +
        (m_ShadowSettings.ssrTemporalMissHistoryRejectEnabled ? 65536.0f : 0.0f);
    uniformData.ssrControls = glm::vec4(
        std::clamp(m_ShadowSettings.ssrStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.ssrRayLength, 0.0f, 64.0f),
        std::clamp(m_ShadowSettings.ssrThickness, 0.0f, 0.5f),
        ssrTraceControl *
            (m_ShadowSettings.ssrRefinementEnabled ? 1.0f : -1.0f)
    );
    uniformData.reflectionProbeControls = glm::vec4(
        m_ShadowSettings.reflectionProbeFallbackEnabled ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.reflectionProbeDiffuseIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.reflectionProbeSpecularIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.reflectionProbeHorizonBlend, 0.0f, 1.0f)
    );
    PopulateReflectionProbeUniforms(
        reflectionProbes,
        m_ShadowSettings.globalIblCubemapEnabled,
        m_ShadowSettings.reflectionProbeCubemapEnabled,
        uniformData
    );
    const bool temporalSkyboxStabilization =
        TemporalDlssModeActive() ||
        TemporalNativeTaaModeActive() ||
        TaaResolveEnabledFromEnvironment();
    uniformData.environmentControls = glm::vec4(
        m_ShadowSettings.skyboxEnabled ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.skyboxIntensity, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.skyboxBlur, 0.0f, 8.0f),
        temporalSkyboxStabilization ? 2.0f : 1.0f
    );
    PopulateProbeGridUniforms(uniformData);
    const bool heightFogApplied =
        m_ShadowSettings.heightFogEnabled &&
        m_ShadowSettings.heightFogDensity > 0.0001f &&
        m_ShadowSettings.heightFogMaxOpacity > 0.0001f;
    uniformData.heightFogControls = glm::vec4(
        heightFogApplied ? 1.0f : 0.0f,
        std::clamp(m_ShadowSettings.heightFogDensity, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.heightFogHeightFalloff, 0.0f, 2.0f),
        std::clamp(m_ShadowSettings.heightFogStartDistance, 0.0f, 1000.0f)
    );
    uniformData.heightFogColor = glm::vec4(
        std::clamp(m_ShadowSettings.heightFogColorR, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogColorG, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogColorB, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.heightFogMaxOpacity, 0.0f, 1.0f)
    );
    const bool bloomApplied =
        m_RenderDebugSettings.bloomEnabled &&
        m_RenderDebugSettings.bloomIntensity > 0.0001f &&
        m_RenderDebugSettings.bloomRadiusPixels > 0.0001f;
    uniformData.postProcessControls = glm::vec4(
        bloomApplied ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.bloomIntensity, 0.0f, 4.0f),
        std::clamp(m_RenderDebugSettings.bloomThreshold, 0.0f, 16.0f),
        std::clamp(m_RenderDebugSettings.bloomRadiusPixels, 0.0f, 24.0f)
    );
    uniformData.colorGradingControls = glm::vec4(
        m_RenderDebugSettings.colorGradingEnabled ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.colorGradingSaturation, 0.0f, 2.5f),
        std::clamp(m_RenderDebugSettings.colorGradingContrast, 0.0f, 2.5f),
        std::clamp(m_RenderDebugSettings.colorGradingGamma, 0.25f, 4.0f)
    );
    const bool colorGradingLutReady =
        m_ColorGradingLut != nullptr && m_ColorGradingLut->Uploaded();
    const bool colorGradingLutApplied =
        m_RenderDebugSettings.colorGradingEnabled &&
        colorGradingLutReady &&
        m_RenderDebugSettings.colorGradingLutStrength > 0.0001f;
    uniformData.colorGradingLutControls = glm::vec4(
        colorGradingLutApplied
            ? std::clamp(m_RenderDebugSettings.colorGradingLutStrength, 0.0f, 1.0f)
            : 0.0f,
        static_cast<f32>(kColorGradingLutSize),
        colorGradingLutReady ? 1.0f : 0.0f,
        m_RenderDebugSettings.colorGradingEnabled && !colorGradingLutReady ? 1.0f : 0.0f
    );
    uniformData.debugControls = glm::vec4(
        static_cast<f32>(std::clamp(
            m_RenderDebugSettings.localShadowDebugLightIndex,
            -1,
            static_cast<i32>(kRendererMaxFrameLocalLights) - 1
        )),
        0.0f,
        0.0f,
        0.0f
    );
    uniformData.toneMappingControls = glm::vec4(
        static_cast<f32>(std::clamp<u32>(m_RenderDebugSettings.toneMapMode, 0u, 2u)),
        std::clamp(m_RenderDebugSettings.exposure, 0.001f, 32.0f),
        std::clamp(m_RenderDebugSettings.toneMapWhitePoint, 0.1f, 64.0f),
        m_RenderDebugSettings.autoExposureEnabled ? 1.0f : 0.0f
    );
    const f32 autoExposureMin = std::clamp(m_RenderDebugSettings.autoExposureMin, 0.001f, 32.0f);
    uniformData.autoExposureControls = glm::vec4(
        std::clamp(m_RenderDebugSettings.autoExposureTargetLuminance, 0.001f, 4.0f),
        autoExposureMin,
        std::max(autoExposureMin, std::clamp(m_RenderDebugSettings.autoExposureMax, 0.001f, 32.0f)),
        std::clamp(m_RenderDebugSettings.autoExposureAdaptation, 0.0f, 1.0f)
    );
    const bool sharpeningApplied =
        m_RenderDebugSettings.sharpeningEnabled &&
        m_RenderDebugSettings.sharpeningStrength > 0.0001f &&
        m_RenderDebugSettings.sharpeningRadiusPixels > 0.0001f;
    uniformData.sharpeningControls = glm::vec4(
        sharpeningApplied ? 1.0f : 0.0f,
        std::clamp(m_RenderDebugSettings.sharpeningStrength, 0.0f, 2.0f),
        std::clamp(m_RenderDebugSettings.sharpeningRadiusPixels, 0.0f, 4.0f),
        0.0f
    );
    m_OverlayUniformBuffer->Update(imageIndex, uniformData);
}

void VulkanRenderer::UpdateLightBuffer(
    std::size_t imageIndex,
    const FrameLightSet& lights,
    const VkExtent2D& extent,
    const FrameMatrices* matrices,
    FrameLightTileStats* tileStats
) const {
    if (m_LightBuffer == nullptr) {
        return;
    }

    const FrameLightConstants constants = lights.Constants();
    auto lightData = std::make_unique<LightBufferObject>();
    lightData->directionalLight = constants.directionalLight;
    lightData->ambientLight = constants.ambientLight;
    lightData->lightCounts = glm::vec4(
        static_cast<f32>(lights.directionalCount + lights.localCount),
        static_cast<f32>(lights.directionalCount),
        static_cast<f32>(lights.localCount),
        0.0f
    );
    const std::size_t localCount = std::min<std::size_t>(
        lights.localCount,
        lightData->localLights.size()
    );
    for (std::size_t index = 0; index < localCount; ++index) {
        const RendererLocalLight& light = lights.localLights[index];
        GpuLocalLightRecord& record = lightData->localLights[index];
        record.positionRadius = glm::vec4(light.position, std::max(light.radius, 0.0f));
        record.colorIntensity = glm::vec4(
            glm::max(light.color, glm::vec3(0.0f)),
            std::max(light.intensity, 0.0f)
        );
        glm::vec3 direction = light.direction;
        if (glm::dot(direction, direction) <= 0.0001f) {
            direction = { 0.0f, -1.0f, 0.0f };
        } else {
            direction = glm::normalize(direction);
        }
        f32 localLightType = 0.0f;
        if (light.kind == RendererLightKind::Spot) {
            localLightType = 1.0f;
        } else if (light.kind == RendererLightKind::Rect) {
            localLightType = 2.0f;
        }
        record.directionType = glm::vec4(direction, localLightType);
        record.parameters = glm::vec4(
            std::clamp(light.innerConeCos, -1.0f, 1.0f),
            std::clamp(light.outerConeCos, -1.0f, 1.0f),
            std::max(light.width, 0.0f),
            std::max(light.height, 0.0f)
        );
    }
    const FrameLightTileStats populatedTileStats =
        PopulateLightTileAssignments(*lightData, localCount, extent, matrices);
    if (tileStats != nullptr) {
        *tileStats = populatedTileStats;
    }
    m_LightBuffer->Update(imageIndex, *lightData);
    if (m_LightTileDiagnosticsBuffer != nullptr) {
        m_LightTileDiagnosticsBuffer->Update(
            imageIndex,
            LightTileDiagnosticsBufferObject{}
        );
    }
}

FrameLightTileGpuReadbackStats VulkanRenderer::ReadPreviousLightTileGpuStats(
    std::size_t imageIndex
) const {
    FrameLightTileGpuReadbackStats stats{};
    if (m_LightTileDiagnosticsBuffer == nullptr ||
        imageIndex >= m_LightTileGpuReadbackReady.size() ||
        !m_LightTileGpuReadbackReady[imageIndex]) {
        return stats;
    }

    const LightTileDiagnosticsBufferObject diagnostics =
        m_LightTileDiagnosticsBuffer->Download(imageIndex);
    if (diagnostics.counters.w == 0u) {
        return stats;
    }

    stats.valid = true;
    stats.saturatedTileCount = diagnostics.counters.x;
    stats.maxRawCandidateCount = diagnostics.counters.y;
    stats.rawCandidateCountSum = diagnostics.counters.z;
    stats.overflowUsedTileCount = diagnostics.overflowCounters.x;
    stats.overflowDroppedTileCount = diagnostics.overflowCounters.y;
    stats.overflowDroppedCount = diagnostics.overflowCounters.w;
    stats.overflowStoredCount =
        diagnostics.overflowCounters.z >= diagnostics.overflowCounters.w
            ? diagnostics.overflowCounters.z - diagnostics.overflowCounters.w
            : 0u;

    return stats;
}

FrameSsrGpuDiagnosticsStats VulkanRenderer::ReadPreviousSsrGpuDiagnostics(
    std::size_t imageIndex
) const {
    FrameSsrGpuDiagnosticsStats stats{};
#if !defined(NDEBUG)
    if (!EnvironmentFlagEnabled("SE_SSR_HOLE_DIAGNOSTICS") ||
        m_LightTileDiagnosticsBuffer == nullptr ||
        imageIndex >= m_LightTileGpuReadbackReady.size() ||
        !m_LightTileGpuReadbackReady[imageIndex]) {
        return stats;
    }

    const LightTileDiagnosticsBufferObject diagnostics =
        m_LightTileDiagnosticsBuffer->Download(imageIndex);
    if (diagnostics.ssrCounters.w != 0x53535244u) {
        return stats;
    }

    stats.valid = true;
    if (m_SceneRenderTargets != nullptr) {
        const VkExtent2D extent = m_SceneRenderTargets->Extent();
        stats.pixelCount = extent.width * extent.height;
    }
    stats.rawHitPixels = diagnostics.ssrCounters.x;
    stats.rawHighConfidencePixels = diagnostics.ssrCounters.y;
    stats.temporalValidPixels = diagnostics.ssrCounters.z;
    stats.resolvedValidPixels = diagnostics.ssrTopologyCounters.x;
    stats.isolatedRawHitPixels = diagnostics.ssrTopologyCounters.y;
    stats.centerMissNeighborHitPixels = diagnostics.ssrTopologyCounters.z;
    stats.resolvedHolePixels = diagnostics.ssrTopologyCounters.w;
    stats.fallbackBlendResolvedPixels = diagnostics.ssrFallbackCounters.x;
    stats.fallbackBlendPartialPixels = diagnostics.ssrFallbackCounters.y;
    stats.fallbackBlendHighTrustPixels = diagnostics.ssrFallbackCounters.z;
    stats.fallbackBlendWeightSum64 = diagnostics.ssrFallbackCounters.w;
    stats.rawHitTemporalRejectedPixels = diagnostics.ssrReliabilityCounters.x;
    stats.rawHitSpatialRejectedPixels = diagnostics.ssrReliabilityCounters.y;
    stats.temporalMissCarriedPixels = diagnostics.ssrReliabilityCounters.z;
    stats.contractVersion = diagnostics.ssrReliabilityCounters.w;
#else
    (void)imageIndex;
#endif
    return stats;
}

FrameAutoExposureReadbackStats VulkanRenderer::ReadPreviousAutoExposureStats(
    std::size_t imageIndex
) const {
    FrameAutoExposureReadbackStats stats{};
    if (m_AutoExposureBuffer == nullptr ||
        imageIndex >= m_AutoExposureBuffer->Count()) {
        return stats;
    }

    const AutoExposureBufferObject exposure =
        m_AutoExposureBuffer->Download(imageIndex);
    stats.valid = exposure.exposure.w > 0.5f;
    stats.exposure = std::max(exposure.exposure.x, 0.001f);
    stats.targetExposure = std::max(exposure.exposure.y, 0.001f);
    stats.averageLuminance = std::max(exposure.exposure.z, 0.001f);
    return stats;
}

void VulkanRenderer::UpdateMaterialBuffer(
    std::size_t imageIndex,
    const FrameMaterialSet& materials
) const {
    if (m_MaterialBuffer == nullptr) {
        return;
    }

    m_MaterialBuffer->Update(imageIndex, materials.materialData);
}

bool VulkanRenderer::ProbeGridEnabled() const {
    return m_ShadowSettings.probeGridEnabled &&
        m_ShadowSettings.probeGridBlendStrength > 0.0001f &&
        m_ProbeGridBuffer != nullptr &&
        ProbeGridLayoutValid();
}

void VulkanRenderer::PopulateProbeGridUniforms(
    UniformBufferObject& uniformData
) const {
    if (!ProbeGridEnabled()) {
        uniformData.probeGridOriginSpacing = glm::vec4(kProbeGridOrigin, kProbeGridSpacing);
        uniformData.probeGridSizeBlend = glm::vec4(0.0f);
        return;
    }

    uniformData.probeGridOriginSpacing = glm::vec4(kProbeGridOrigin, kProbeGridSpacing);
    uniformData.probeGridSizeBlend = glm::vec4(
        static_cast<f32>(kProbeGridSizeX),
        static_cast<f32>(kProbeGridSizeY),
        static_cast<f32>(kProbeGridSizeZ),
        std::clamp(m_ShadowSettings.probeGridBlendStrength, 0.0f, 2.0f)
    );
}

void VulkanRenderer::UpdateProbeGridBuffer(
    std::size_t imageIndex,
    RendererProbeGridStats& stats
) const {
    const bool configured = m_ShadowSettings.probeGridEnabled;
    const f32 blendStrength =
        std::clamp(m_ShadowSettings.probeGridBlendStrength, 0.0f, 2.0f);
    const bool layoutValid = ProbeGridLayoutValid();
    const glm::vec3 boundsMax = ProbeGridBoundsMax();

    stats.allocated = m_ProbeGridBuffer != nullptr ? 1u : 0u;
    stats.enabled = configured ? 1u : 0u;
    stats.probeCount = static_cast<u32>(kProbeGridProbeCount);
    stats.sizeX = static_cast<u32>(kProbeGridSizeX);
    stats.sizeY = static_cast<u32>(kProbeGridSizeY);
    stats.sizeZ = static_cast<u32>(kProbeGridSizeZ);
    stats.vec4sPerProbe = static_cast<u32>(kProbeGridVec4sPerProbe);
    stats.directionalLobeCount = static_cast<u32>(kProbeGridDirectionalLobeCount);
    stats.cellCount = ProbeGridCellCount();
    stats.originX = kProbeGridOrigin.x;
    stats.originY = kProbeGridOrigin.y;
    stats.originZ = kProbeGridOrigin.z;
    stats.boundsMinX = kProbeGridOrigin.x;
    stats.boundsMinY = kProbeGridOrigin.y;
    stats.boundsMinZ = kProbeGridOrigin.z;
    stats.boundsMaxX = boundsMax.x;
    stats.boundsMaxY = boundsMax.y;
    stats.boundsMaxZ = boundsMax.z;
    stats.spacing = kProbeGridSpacing;
    stats.blendStrength = configured ? blendStrength : 0.0f;
    stats.debugViewEnabled =
        m_RenderDebugSettings.forwardView == ForwardDebugView::ProbeGrid ? 1u : 0u;
    stats.cellDebugViewEnabled =
        m_RenderDebugSettings.forwardView == ForwardDebugView::ProbeGridCell ? 1u : 0u;

    RendererProbeGridFallbackReason fallbackReason =
        RendererProbeGridFallbackReason::None;
    if (!configured) {
        fallbackReason = RendererProbeGridFallbackReason::Disabled;
    } else if (blendStrength <= 0.0001f) {
        fallbackReason = RendererProbeGridFallbackReason::BlendZero;
    } else if (m_ProbeGridBuffer == nullptr) {
        fallbackReason = RendererProbeGridFallbackReason::BufferUnavailable;
    } else if (!layoutValid) {
        fallbackReason = RendererProbeGridFallbackReason::InvalidLayout;
    } else if (imageIndex >= m_ProbeGridBuffer->Count()) {
        fallbackReason = RendererProbeGridFallbackReason::FrameIndexOutOfRange;
    }

    stats.fallbackReason = static_cast<u32>(fallbackReason);
    stats.fallbackCount =
        fallbackReason == RendererProbeGridFallbackReason::None ? 0u : 1u;
    stats.shaderIntegrationEnabled =
        fallbackReason == RendererProbeGridFallbackReason::None ? 1u : 0u;

    if (fallbackReason != RendererProbeGridFallbackReason::None) {
        return;
    }

    m_ProbeGridBuffer->Update(imageIndex, BuildDeterministicProbeGridData());
    stats.bufferUpdates = 1u;
}

void VulkanRenderer::UpdateDirectionalShadowCascadeBuffer(
    std::size_t imageIndex,
    const DirectionalShadowCascadeSet& cascades,
    const glm::mat4& fallbackLightViewProjection
) const {
    if (m_DirectionalShadowCascadeBuffer == nullptr) {
        return;
    }

    DirectionalShadowCascadeBufferObject cascadeData{};
    cascadeData.cascadeInfo = glm::vec4(
        static_cast<f32>(cascades.activeCount),
        m_ShadowSettings.directionalShadowReceiveEnabled ? 1.0f : 0.0f,
        cascades.stableSnappingEnabled ? 1.0f : 0.0f,
        cascades.singleMapSampling ? -1.0f : cascades.splitLambda
    );
    cascadeData.cascadeBlendControls = glm::vec4(
        cascades.singleMapSampling
            ? 0.0f
            : std::clamp(m_ShadowSettings.cascadeBlendRatio, 0.0f, 0.25f),
        cascades.singleMapSampling
            ? 0.0f
            : std::clamp(m_ShadowSettings.cascadeFadeRatio, 0.0f, 0.35f),
        static_cast<f32>(std::clamp<u32>(m_ShadowSettings.pcfKernelRadius, 0u, 2u)),
        std::clamp(m_ShadowSettings.pcssStrength, 0.0f, 1.0f)
    );
    cascadeData.receiverPlaneBiasControls = glm::vec4(
        std::clamp(m_ShadowSettings.directionalReceiverPlaneBiasScale, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.directionalNormalOffsetBiasTexels, 0.0f, 4.0f),
        std::clamp(m_ShadowSettings.directionalSlopeOffsetBiasTexels, 0.0f, 2.0f),
        0.0f
    );
    cascadeData.directionalFilterControls = glm::vec4(
        static_cast<f32>(m_ShadowSettings.directionalFilterMode),
        static_cast<f32>(std::clamp<u32>(m_ShadowSettings.directionalFilterSampleCount, 0u, 16u)),
        static_cast<f32>(std::clamp<u32>(m_ShadowSettings.directionalFilterKernelWidth, 0u, 5u)),
        std::clamp(m_ShadowSettings.directionalFilterReceiverBiasExtentTexels, 0.0f, 4.0f)
    );
    const f32 pcssStrength = std::clamp(m_ShadowSettings.pcssStrength, 0.0f, 1.0f);
    const u32 pcssBlockerSamples = std::clamp<u32>(
        m_ShadowSettings.directionalPcssBlockerSampleCount,
        0u,
        16u
    );
    const u32 pcssFilterSamples = std::clamp<u32>(
        m_ShadowSettings.directionalPcssFilterSampleCount,
        0u,
        16u
    );
    const f32 pcssSearchRadius = std::clamp(
        m_ShadowSettings.directionalPcssSearchRadiusTexels,
        0.0f,
        16.0f
    );
    const f32 pcssMaxPenumbra = std::clamp(
        m_ShadowSettings.directionalPcssMaxPenumbraTexels,
        0.0f,
        16.0f
    );
    const bool pcssRawDepthReady = cascades.activeCount > 0u;
    const bool pcssControlsValid = pcssBlockerSamples > 0u &&
        pcssFilterSamples > 0u && pcssSearchRadius > 0.0f &&
        pcssMaxPenumbra > 0.0f && cascades.lightAngularRadiusRadians > 0.0f;
    const u32 pcssFallbackReason =
        pcssStrength <= 0.0001f
            ? 1u
            : (!pcssRawDepthReady ? 2u : (!pcssControlsValid ? 3u : 0u));
    cascadeData.directionalPcssControls = glm::vec4(
        pcssStrength,
        static_cast<f32>(pcssBlockerSamples),
        static_cast<f32>(pcssFilterSamples),
        pcssMaxPenumbra
    );
    cascadeData.directionalPcssGeometry = glm::vec4(
        pcssSearchRadius,
        std::clamp(cascades.lightAngularRadiusRadians, 0.0f, 0.05f),
        pcssRawDepthReady ? 1.0f : 0.0f,
        static_cast<f32>(pcssFallbackReason)
    );
    const f32 pcssGrazingFadeStart = std::clamp(
        m_ShadowSettings.directionalPcssGrazingFadeStart,
        0.0f,
        0.95f
    );
    cascadeData.directionalPcssReceiverControls = glm::vec4(
        pcssGrazingFadeStart,
        std::clamp(
            m_ShadowSettings.directionalPcssGrazingFadeEnd,
            pcssGrazingFadeStart + 0.01f,
            1.0f
        ),
        m_ShadowSettings.directionalPcssGrazingFadeEnabled ? 1.0f : 0.0f,
        0.0f
    );
    cascadeData.fallbackViewProjection = fallbackLightViewProjection;
    for (u32 index = 0; index < kMaxDirectionalShadowCascades; ++index) {
        cascadeData.viewProjections[index] = cascades.cascades[index].viewProjection;
        cascadeData.splitDepths[index] = cascades.cascades[index].splitDepth;
        cascadeData.texelWorldSizes[index] = cascades.cascades[index].texelWorldSize;
        cascadeData.lightDepthWorldSpans[index] =
            cascades.cascades[index].lightDepthWorldSpan;
    }

    m_DirectionalShadowCascadeBuffer->Update(imageIndex, cascadeData);
}

void VulkanRenderer::UpdateLocalShadowBuffer(
    std::size_t imageIndex,
    const LocalShadowTileSet& localShadowTiles
) const {
    if (m_LocalShadowBuffer == nullptr) {
        return;
    }

    LocalShadowBufferObject localShadowData{};
    localShadowData.atlasInfo = glm::uvec4(
        localShadowTiles.assignedCount,
        localShadowTiles.tileSize,
        localShadowTiles.tileColumns,
        localShadowTiles.tileRows
    );
    localShadowData.atlasInfo2 = glm::uvec4(
        localShadowTiles.tileCapacity,
        localShadowTiles.requestedCount,
        localShadowTiles.droppedCount,
        kLocalShadowFilterContractVersion
    );
    localShadowData.filterControls = glm::vec4(
        std::clamp(m_ShadowSettings.localBiasMin, 0.0f, 0.02f),
        std::clamp(m_ShadowSettings.localBiasSlope, 0.0f, 0.05f),
        std::clamp(m_ShadowSettings.localPcfRadius, 0.0f, 4.0f),
        static_cast<f32>(std::clamp<u32>(
            m_ShadowSettings.localPcfKernelRadius,
            0u,
            2u
        ))
    );
    localShadowData.softShadowControls = glm::vec4(
        std::clamp(m_ShadowSettings.localPcssStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.localFaceBlendStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.rectLightShadowBiasScale, 0.0f, 32.0f),
        m_ShadowSettings.localProductionFilterEnabled ? 1.0f : 0.0f
    );
    const auto filterControlsFor =
        [](const VulkanLocalShadowFilterSettings& filter) {
            return glm::vec4(
                std::clamp(filter.biasMin, 0.0f, 0.02f),
                std::clamp(filter.biasSlope, 0.0f, 0.05f),
                std::clamp(filter.pcfRadius, 0.0f, 4.0f),
                static_cast<f32>(std::clamp<u32>(
                    filter.pcfKernelRadius,
                    0u,
                    2u
                ))
            );
        };
    localShadowData.pointFilterControls =
        filterControlsFor(m_ShadowSettings.pointLocalShadowFilter);
    localShadowData.spotFilterControls =
        filterControlsFor(m_ShadowSettings.spotLocalShadowFilter);
    localShadowData.rectFilterControls =
        filterControlsFor(m_ShadowSettings.rectLocalShadowFilter);
    localShadowData.kindSoftShadowControls = glm::vec4(
        std::clamp(m_ShadowSettings.pointLocalShadowFilter.pcssStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.spotLocalShadowFilter.pcssStrength, 0.0f, 1.0f),
        std::clamp(m_ShadowSettings.rectLocalShadowFilter.pcssStrength, 0.0f, 1.0f),
        0.0f
    );
    const auto pcssControlsFor = [](const VulkanLocalShadowFilterSettings& filter) {
        return glm::vec4(
            static_cast<f32>(std::clamp<u32>(
                filter.pcssBlockerSampleCount,
                0u,
                16u
            )),
            static_cast<f32>(std::clamp<u32>(
                filter.pcssFilterSampleCount,
                1u,
                16u
            )),
            std::clamp(filter.pcssSearchRadiusTexels, 0.0f, 16.0f),
            std::clamp(filter.pcssMaxPenumbraTexels, 0.0f, 16.0f)
        );
    };
    localShadowData.pointPcssControls =
        pcssControlsFor(m_ShadowSettings.pointLocalShadowFilter);
    localShadowData.spotPcssControls =
        pcssControlsFor(m_ShadowSettings.spotLocalShadowFilter);
    localShadowData.rectPcssControls =
        pcssControlsFor(m_ShadowSettings.rectLocalShadowFilter);
    for (u32 lightIndex = 0u;
         lightIndex < localShadowData.tileRanges.size();
         ++lightIndex) {
        const u32 assignedTiles =
            localShadowTiles.assignedTilesByLocalLight[lightIndex];
        localShadowData.tileRanges[lightIndex] = glm::uvec4(
            localShadowTiles.firstAssignedTileByLocalLight[lightIndex],
            assignedTiles,
            localShadowTiles.requestedTilesByLocalLight[lightIndex],
            assignedTiles > 0u ? 1u : 0u
        );
    }
    const u32 assignedCount = std::min<u32>(
        localShadowTiles.assignedCount,
        static_cast<u32>(localShadowData.tiles.size())
    );
    for (u32 index = 0; index < assignedCount; ++index) {
        const LocalShadowTile& tile = localShadowTiles.tiles[index];
        GpuLocalShadowTileRecord& record = localShadowData.tiles[index];
        record.viewProjection = tile.viewProjection;
        record.tileInfo = glm::uvec4(
            tile.tileIndex,
            tile.localLightIndex,
            tile.faceIndex,
            tile.lightKind
        );
        record.lightInfo = tile.filterGeometry;
    }

    m_LocalShadowBuffer->Update(imageIndex, localShadowData);
}

FrameLightSet VulkanRenderer::BuildFrameLightSet(std::span<const RenderCommand> renderCommands) const {
    FrameLightSet lights{};
    if (!ApplySceneDirectionalLight(m_MainScene3D, lights)) {
        (void)ApplyMaterialDirectionalFallback(renderCommands, lights);
    }

    AddScenePointLights(m_MainScene3D, lights);
    AddSceneSpotLights(m_MainScene3D, lights);
    AddSceneRectLights(m_MainScene3D, lights);
    AddDebugLocalLights(lights);
    return lights;
}

FrameReflectionProbeSet VulkanRenderer::BuildFrameReflectionProbeSet(
    const FrameMatrices* matrices
) const {
    FrameReflectionProbeSet probes{};
    ResetFrameReflectionProbeCaptureDiagnostics(probes);
    probes.fallbackEnabled = m_ShadowSettings.reflectionProbeFallbackEnabled;
    probes.influenceMode = 1u;
    const bool builtInCubemapReady = LocalReflectionProbeCubemapReady();
    const u32 reflectionProbeDescriptorSetsBound =
        m_ReflectionProbeResources.DescriptorSetsBound();
    std::span<const ReflectionProbe3D> sceneProbes{};
    if (m_MainScene3D != nullptr) {
        sceneProbes = m_MainScene3D->ReflectionProbes();
        probes.sceneProbeCount = static_cast<u32>(std::min<std::size_t>(
            sceneProbes.size(),
            std::numeric_limits<u32>::max()
        ));
    }
    if (!probes.fallbackEnabled) {
        probes.captureFallbackReason =
            RendererReflectionProbeCaptureFallbackReason::FallbackDisabled;
        return probes;
    }

    const std::optional<RendererReflectionProbeCaptureSource>
        captureSourceOverride = ReflectionProbeCaptureSourceOverrideFromEnvironment();
    const std::optional<RendererReflectionProbeRefreshPolicy>
        refreshPolicyOverride = ReflectionProbeRefreshPolicyOverrideFromEnvironment();
    probes.forcedRefreshRequested =
        EnvironmentFlagEnabled("SE_REFLECTION_PROBE_FORCE_REFRESH");
    probes.sceneDirtyRequested =
        EnvironmentFlagEnabled("SE_REFLECTION_PROBE_SCENE_DIRTY");

    if (!sceneProbes.empty()) {
        struct ReflectionProbeCandidate {
            RendererReflectionProbe probe{};
            f32 selectionScore = 0.0f;
            f32 blendWeight = 0.0f;
        };

        const glm::vec3 selectionPosition = CameraPositionFromMatrices(matrices);
        std::vector<ReflectionProbeCandidate> candidates;
        candidates.reserve(sceneProbes.size());

        for (std::size_t index = 0; index < sceneProbes.size(); ++index) {
            const ReflectionProbe3D& sceneProbe = sceneProbes[index];
            RendererReflectionProbe candidate = SceneReflectionProbe(
                sceneProbe,
                index <= static_cast<std::size_t>(std::numeric_limits<i32>::max())
                    ? static_cast<i32>(index)
                    : -1
            );
            if (!ReflectionProbeContributes(candidate)) {
                continue;
            }
            if (captureSourceOverride.has_value()) {
                candidate.captureSource = *captureSourceOverride;
                candidate.refreshPolicy =
                    DefaultReflectionProbeRefreshPolicy(candidate.captureSource);
            }
            if (refreshPolicyOverride.has_value()) {
                candidate.refreshPolicy = *refreshPolicyOverride;
            }
            ++probes.eligibleSceneProbeCount;
            candidates.push_back(ReflectionProbeCandidate{
                candidate,
                ReflectionProbeSelectionScore(candidate, selectionPosition),
                ReflectionProbeInfluenceWeight(candidate, selectionPosition)
            });
        }

        if (!candidates.empty()) {
            std::sort(
                candidates.begin(),
                candidates.end(),
                [](const ReflectionProbeCandidate& left,
                   const ReflectionProbeCandidate& right) {
                    if (left.selectionScore == right.selectionScore) {
                        return left.probe.sceneIndex < right.probe.sceneIndex;
                    }
                    return left.selectionScore > right.selectionScore;
                }
            );

            probes.selectedProbeCount = std::min<u32>(
                static_cast<u32>(candidates.size()),
                static_cast<u32>(kMaxFrameReflectionProbes)
            );
            probes.activeLocalProbeCount = probes.selectedProbeCount;
            probes.blendedProbeCount = probes.selectedProbeCount;
            probes.multiBlendEnabled = probes.selectedProbeCount > 0u;
            probes.droppedSceneProbeCount =
                probes.eligibleSceneProbeCount > probes.selectedProbeCount
                    ? probes.eligibleSceneProbeCount - probes.selectedProbeCount
                    : 0u;

            for (u32 index = 0; index < probes.selectedProbeCount; ++index) {
                const ReflectionProbeCandidate& selected = candidates[index];
                probes.selectedProbes[index] = selected.probe;
                probes.selectedBlendWeights[index] = selected.blendWeight;
                probes.selectedProbeMask |= 1u << index;
                if (selected.probe.sceneOwned) {
                    probes.selectedSceneOwnedMask |= 1u << index;
                }
                if (ReflectionProbeBoxProjectionEnabled(selected.probe)) {
                    probes.selectedBoxProjectionMask |= 1u << index;
                    if (selected.probe.captureSource ==
                        RendererReflectionProbeCaptureSource::CapturedScene) {
                        probes.selectedCapturedSceneBoxProjectionMask |= 1u << index;
                    }
                }
                if (selected.blendWeight > 0.0001f) {
                    probes.selectedPositiveInfluenceMask |= 1u << index;
                }
                probes.totalBlendWeight += selected.blendWeight;
                probes.maxBlendWeight =
                    std::max(probes.maxBlendWeight, selected.blendWeight);
                probes.boxProjectionEnabled =
                    probes.boxProjectionEnabled ||
                    ReflectionProbeBoxProjectionEnabled(selected.probe);
                const bool authoredCubemapReady =
                    selected.probe.captureSource ==
                            RendererReflectionProbeCaptureSource::AuthoredCubemap
                        ? m_ReflectionProbeResources.AuthoredCubemapReady(
                            selected.probe.captureAssetId,
                            m_IblSampler
                        )
                        : false;
                const bool authoredAssetFound =
                    selected.probe.captureSource ==
                            RendererReflectionProbeCaptureSource::AuthoredCubemap
                        ? m_ReflectionProbeResources.AuthoredCubemapAssetFound(
                            selected.probe.captureAssetId
                        )
                        : false;
                const bool authoredLoadFailed =
                    selected.probe.captureSource ==
                            RendererReflectionProbeCaptureSource::AuthoredCubemap
                        ? m_ReflectionProbeResources.AuthoredCubemapLoadFailed(
                            selected.probe.captureAssetId
                        )
                        : false;
                if (authoredCubemapReady &&
                    m_ReflectionProbeResources.AuthoredCubemapIrradianceReady(
                        selected.probe.captureAssetId
                    )) {
                    const std::array<f32, 3> irradiance =
                        m_ReflectionProbeResources.AuthoredCubemapIrradianceColor(
                            selected.probe.captureAssetId
                        );
                    probes.selectedProbes[index].color = glm::vec3{
                        irradiance[0],
                        irradiance[1],
                        irradiance[2]
                    };
                }
                if (authoredCubemapReady &&
                    m_ReflectionProbeResources.AuthoredCubemapDiffuseLobesReady(
                        selected.probe.captureAssetId
                    )) {
                    const AuthoredReflectionProbeDiffuseLobes diffuseLobes =
                        m_ReflectionProbeResources.AuthoredCubemapDiffuseLobes(
                            selected.probe.captureAssetId
                        );
                    probes.selectedDiffuseIrradianceLobesReady[index] = true;
                    ++probes.selectedDiffuseIrradianceLobesReadyCount;
                    probes.selectedDiffuseIrradianceLobesReadyMask |=
                        1u << index;
                    probes.selectedDiffuseIrradianceLobeCount =
                        static_cast<u32>(kReflectionProbeDiffuseLobeCount);
                    for (std::size_t lobe = 0;
                         lobe < kReflectionProbeDiffuseLobeCount;
                         ++lobe) {
                        probes.selectedDiffuseIrradianceLobes[index][lobe] =
                            glm::vec4(
                                diffuseLobes[lobe][0],
                                diffuseLobes[lobe][1],
                                diffuseLobes[lobe][2],
                                1.0f
                            );
                    }
                }
                const bool selectedCapturedSceneReady =
                    selected.probe.captureSource ==
                            RendererReflectionProbeCaptureSource::CapturedScene
                        ? m_ReflectionProbeResources.CapturedSceneReady(
                            selected.probe.sceneIndex,
                            m_IblSampler
                        )
                        : false;
                const bool selectedCapturedSceneDiffuseIrradianceReady =
                    selected.probe.captureSource ==
                            RendererReflectionProbeCaptureSource::CapturedScene &&
                    m_ReflectionProbeResources.CapturedSceneDiffuseIrradianceReady(
                        selected.probe.sceneIndex,
                        m_IblSampler
                    );
                if (selectedCapturedSceneDiffuseIrradianceReady) {
                    probes.selectedCapturedSceneDiffuseIrradianceReady[index] =
                        true;
                    ++probes.selectedCapturedSceneDiffuseIrradianceReadyCount;
                    probes.selectedCapturedSceneDiffuseIrradianceReadyMask |=
                        1u << index;
                }
                const CapturedSceneCaptureAudit& selectedCapturedSceneAudit =
                    m_ReflectionProbeResources.CapturedSceneAudit(
                        selected.probe.sceneIndex
                    );
                SetSelectedReflectionProbeCaptureDiagnostics(
                    probes,
                    index,
                    probes.selectedProbes[index],
                    m_ShadowSettings.reflectionProbeCubemapEnabled,
                    builtInCubemapReady,
                    authoredCubemapReady,
                    selectedCapturedSceneReady,
                    authoredAssetFound,
                    authoredLoadFailed,
                    reflectionProbeDescriptorSetsBound,
                    selectedCapturedSceneAudit
                );
                if (!probes.selectedCaptureResourceReady[index]) {
                    continue;
                }
                switch (selected.probe.captureSource) {
                case RendererReflectionProbeCaptureSource::BuiltInProcedural:
                    probes.selectedCaptureMipCounts[index] =
                        m_ReflectionProbeResources.MipCount();
                    break;
                case RendererReflectionProbeCaptureSource::AuthoredCubemap:
                    probes.selectedCaptureMipCounts[index] =
                        m_ReflectionProbeResources.AuthoredCubemapMipCount(
                            selected.probe.captureAssetId
                        );
                    break;
                case RendererReflectionProbeCaptureSource::CapturedScene:
                    probes.selectedCaptureMipCounts[index] =
                        m_ReflectionProbeResources.CapturedSceneMipCount(
                            selected.probe.sceneIndex
                        );
                    break;
                case RendererReflectionProbeCaptureSource::None:
                    break;
                }
            }

            if (probes.selectedProbeCount > 0u) {
                if (probes.totalBlendWeight > 0.0001f) {
                    for (u32 index = 0; index < probes.selectedProbeCount;
                         ++index) {
                        probes.selectedNormalizedBlendWeights[index] =
                            probes.selectedBlendWeights[index] /
                            probes.totalBlendWeight;
                        probes.normalizedBlendWeightSum +=
                            probes.selectedNormalizedBlendWeights[index];
                    }
                } else {
                    ++probes.blendWeightNormalizationFallbackCount;
                    const f32 equalWeight =
                        1.0f / static_cast<f32>(probes.selectedProbeCount);
                    for (u32 index = 0; index < probes.selectedProbeCount;
                         ++index) {
                        probes.selectedNormalizedBlendWeights[index] =
                            equalWeight;
                        probes.normalizedBlendWeightSum += equalWeight;
                    }
                }
                probes.normalizedBlendWeightError = std::abs(
                    probes.normalizedBlendWeightSum - 1.0f
                );
            }

            for (u32 index = 0; index < probes.selectedProbeCount; ++index) {
                if (probes.selectedCaptureMipCounts[index] >= 2u) {
                    probes.selectedCaptureMipReadyMask |= 1u << index;
                }
                if (probes.selectedCaptureDescriptorBound[index] &&
                    probes.selectedCaptureMipCounts[index] == 0u) {
                    probes.spatialContractFailureMask |= 1u << 3u;
                }
                const i32 sceneIndex = probes.selectedProbes[index].sceneIndex;
                if (sceneIndex < 0) {
                    continue;
                }
                for (u32 previous = 0; previous < index; ++previous) {
                    if (probes.selectedProbes[previous].sceneIndex == sceneIndex) {
                        probes.selectedProbeDuplicateIndexMask |=
                            (1u << previous) | (1u << index);
                    }
                }
            }
            if (probes.selectedProbeDuplicateIndexMask != 0u) {
                probes.spatialContractFailureMask |= 1u << 0u;
            }
            if (probes.normalizedBlendWeightError > 0.001f) {
                probes.spatialContractFailureMask |= 1u << 1u;
            }
            if ((probes.selectedBoxProjectionMask & ~probes.selectedProbeMask) != 0u) {
                probes.spatialContractFailureMask |= 1u << 2u;
            }
#if !defined(NDEBUG)
            constexpr glm::vec3 kBoxProjectionInsideOffset{
                0.21f,
                -0.13f,
                0.17f
            };
            constexpr glm::vec3 kBoxProjectionInsideDirection{
                0.72f,
                0.31f,
                0.62f
            };
            constexpr glm::vec3 kBoxProjectionOutsideDirection{
                0.61f,
                0.39f,
                0.69f
            };
            for (u32 index = 0; index < probes.selectedProbeCount; ++index) {
                const RendererReflectionProbe& probe = probes.selectedProbes[index];
                if (!ReflectionProbeBoxProjectionEnabled(probe)) {
                    continue;
                }
                const glm::vec3 extents = glm::max(
                    probe.boxExtents,
                    glm::vec3(0.001f)
                );
                const ReflectionProbeBoxProjectionRayResult insideResult =
                    ReflectionProbeBoxProjectDirection(
                        probe,
                        kBoxProjectionInsideDirection,
                        probe.center + extents * kBoxProjectionInsideOffset
                    );
                const ReflectionProbeBoxProjectionRayResult outsideResult =
                    ReflectionProbeBoxProjectDirection(
                        probe,
                        kBoxProjectionOutsideDirection,
                        probe.center + extents * 1.5f
                    );
                if (insideResult.hit) {
                    probes.selectedBoxProjectionRayHitMask |= 1u << index;
                }
                if (insideResult.directionChanged) {
                    probes.selectedBoxProjectionDirectionChangedMask |= 1u << index;
                }
                if (!outsideResult.hit) {
                    probes.selectedBoxProjectionOutsideFallbackMask |= 1u << index;
                }
            }
            const u32 expectedBoxProjectionMask = probes.selectedBoxProjectionMask;
            if ((probes.selectedBoxProjectionRayHitMask & expectedBoxProjectionMask) !=
                    expectedBoxProjectionMask ||
                (probes.selectedBoxProjectionDirectionChangedMask &
                    expectedBoxProjectionMask) != expectedBoxProjectionMask ||
                (probes.selectedBoxProjectionOutsideFallbackMask &
                    expectedBoxProjectionMask) != expectedBoxProjectionMask) {
                probes.spatialContractFailureMask |= 1u << 4u;
            }
#endif
            probes.spatialContractValid =
                probes.spatialContractFailureMask == 0u;

            probes.localProbe = probes.selectedProbes[0];
            probes.selectedSceneProbeIndex = probes.localProbe.sceneIndex;
            probes.parallaxCorrectionEnabled = probes.boxProjectionEnabled;
            probes.captureSource = probes.localProbe.captureSource;
            probes.refreshPolicy = probes.localProbe.refreshPolicy;
            probes.captureResourceReady = probes.selectedCaptureResourceReady[0];
            probes.captureDescriptorBound = probes.selectedCaptureDescriptorBound[0];
            probes.captureFallbackReason =
                probes.selectedCaptureFallbackReasons[0];
            return probes;
        }
    }

    RendererReflectionProbe settingsProbe =
        SettingsReflectionProbe(m_ShadowSettings);
    if (captureSourceOverride.has_value()) {
        settingsProbe.captureSource = *captureSourceOverride;
        settingsProbe.refreshPolicy =
            DefaultReflectionProbeRefreshPolicy(settingsProbe.captureSource);
    }
    if (refreshPolicyOverride.has_value()) {
        settingsProbe.refreshPolicy = *refreshPolicyOverride;
    }
    if (ReflectionProbeContributes(settingsProbe)) {
        probes.localProbe = settingsProbe;
        probes.selectedProbes[0] = settingsProbe;
        probes.selectedProbeCount = 1;
        probes.activeLocalProbeCount = 1;
        probes.blendedProbeCount = 1;
        probes.multiBlendEnabled = true;
        probes.maxBlendWeight = settingsProbe.blendStrength;
        probes.totalBlendWeight = settingsProbe.blendStrength;
        probes.normalizedBlendWeightSum = 1.0f;
        probes.selectedProbeMask = 1u;
        probes.selectedBlendWeights[0] = settingsProbe.blendStrength;
        probes.selectedNormalizedBlendWeights[0] = 1.0f;
        probes.normalizedBlendWeightError = 0.0f;
        if (settingsProbe.blendStrength > 0.0001f) {
            probes.selectedPositiveInfluenceMask = 1u;
        }
        if (ReflectionProbeBoxProjectionEnabled(settingsProbe)) {
            probes.selectedBoxProjectionMask = 1u;
        }
        SetSelectedReflectionProbeCaptureDiagnostics(
            probes,
            0u,
            settingsProbe,
            m_ShadowSettings.reflectionProbeCubemapEnabled,
            builtInCubemapReady,
            false,
            false,
            false,
            false,
            reflectionProbeDescriptorSetsBound,
            m_ReflectionProbeResources.CapturedSceneAudit(
                settingsProbe.sceneIndex
            )
        );
        if (probes.selectedCaptureResourceReady[0]) {
            switch (settingsProbe.captureSource) {
            case RendererReflectionProbeCaptureSource::BuiltInProcedural:
                probes.selectedCaptureMipCounts[0] =
                    m_ReflectionProbeResources.MipCount();
                break;
            case RendererReflectionProbeCaptureSource::AuthoredCubemap:
                probes.selectedCaptureMipCounts[0] =
                    m_ReflectionProbeResources.AuthoredCubemapMipCount(
                        settingsProbe.captureAssetId
                    );
                break;
            case RendererReflectionProbeCaptureSource::CapturedScene:
                probes.selectedCaptureMipCounts[0] =
                    m_ReflectionProbeResources.CapturedSceneMipCount(
                        settingsProbe.sceneIndex
                    );
                break;
            case RendererReflectionProbeCaptureSource::None:
                break;
            }
        }
        if (probes.selectedCaptureMipCounts[0] >= 2u) {
            probes.selectedCaptureMipReadyMask = 1u;
        }
        if (probes.selectedCaptureDescriptorBound[0] &&
            probes.selectedCaptureMipCounts[0] == 0u) {
            probes.spatialContractFailureMask |= 1u << 3u;
        }
        probes.spatialContractValid =
            probes.spatialContractFailureMask == 0u;
        probes.captureSource = settingsProbe.captureSource;
        probes.refreshPolicy = settingsProbe.refreshPolicy;
        probes.captureResourceReady = probes.selectedCaptureResourceReady[0];
        probes.captureDescriptorBound = probes.selectedCaptureDescriptorBound[0];
        probes.captureFallbackReason =
            probes.selectedCaptureFallbackReasons[0];
    }
    return probes;
}

FrameMaterialSet VulkanRenderer::BuildFrameMaterialSet(
    std::span<const RenderCommand> renderCommands
) const {
    FrameMaterialSet materials{};
    materials.materialData.materialCounts = glm::vec4(
        0.0f,
        static_cast<f32>(kMaxFrameMaterials),
        0.0f,
        0.0f
    );

    for (const RenderCommand& command : renderCommands) {
        if (command.material == nullptr) {
            continue;
        }
        if (materials.materialIds.find(command.material) != materials.materialIds.end()) {
            continue;
        }
        if (materials.count >= kMaxFrameMaterials) {
            ++materials.overflowCount;
            continue;
        }

        const u32 materialId = materials.count + 1;
        materials.materialIds.emplace(command.material, materialId);

        GpuMaterialRecord& record = materials.materialData.materials[materials.count];
        record.baseColorFactor = command.materialPushConstants.materialBaseColorFactor;
        record.materialControls = command.materialPushConstants.materialControls;
        record.materialControls.w = static_cast<f32>(materialId);
        const MaterialProperties& properties = command.material->Properties();
        record.materialCustom = glm::vec4(
            properties.volumeAttenuationColor[0],
            properties.volumeAttenuationColor[1],
            properties.volumeAttenuationColor[2],
            std::clamp(properties.transmissionFactor, 0.0f, 1.0f)
        );
        record.cameraControls = command.materialPushConstants.cameraControls;
        record.pbrFactors = glm::vec4(
            properties.pbrFactors[0],
            properties.pbrFactors[1],
            AlphaModeValue(properties.alphaMode),
            std::clamp(properties.alphaCutoff, 0.0f, 1.0f)
        );
        record.emissiveFactor = glm::vec4(
            properties.emissiveFactor[0],
            properties.emissiveFactor[1],
            properties.emissiveFactor[2],
            std::clamp(properties.clearcoatRoughness, 0.0f, 1.0f)
        );
        record.specularFactor = glm::vec4(
            properties.specularFactor[0],
            properties.specularFactor[1],
            properties.specularFactor[2],
            properties.specularFactor[3]
        );
        record.uvTransform = glm::vec4(
            properties.uvTransform[0],
            properties.uvTransform[1],
            properties.uvTransform[2],
            properties.uvTransform[3]
        );
        record.uvControls = glm::vec4(
            properties.uvControls[0],
            properties.uvControls[1],
            properties.doubleSided ? 1.0f : properties.uvControls[2],
            std::clamp(properties.clearcoatFactor, 0.0f, 1.0f)
        );
        record.volumeFactor = glm::vec4(
            std::clamp(properties.volumeThicknessFactor, 0.0f, 64.0f),
            std::clamp(properties.volumeAttenuationDistance, 0.0f, 1000000.0f),
            0.0f,
            properties.volumeThicknessFactor > 0.001f ? 1.0f : 0.0f
        );

        const f32 alpha = std::clamp(record.baseColorFactor.a, 0.0f, 1.0f);
        const f32 textureFlags = std::max(record.cameraControls.w, 0.0f);
        const bool hasAlbedoTexture = record.materialControls.x > 0.001f;
        const bool hasAuxTexture = record.cameraControls.z > 0.001f;
        const bool hasNormalTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagNormal);
        const bool hasOcclusionTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagOcclusion);
        const bool hasEmissiveTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagEmissive);
        const bool hasOpacityTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagOpacity);
        const bool hasSpecularTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagSpecular);
        const bool hasClearcoatTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagClearcoat);
        const bool hasTransmissionTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagTransmission);
        const bool hasClearcoatRoughnessTexture =
            MaterialFlagEnabled(textureFlags, kMaterialTextureFlagClearcoatRoughness);
        const f32 maxEmissiveFactor = std::max(
            record.emissiveFactor.r,
            std::max(record.emissiveFactor.g, record.emissiveFactor.b)
        );
        const bool hasEmissiveFactor = maxEmissiveFactor > 0.001f;
        const bool hasSpecularFactor =
            std::abs(record.specularFactor.r - 1.0f) > 0.001f ||
            std::abs(record.specularFactor.g - 1.0f) > 0.001f ||
            std::abs(record.specularFactor.b - 1.0f) > 0.001f ||
            std::abs(record.specularFactor.a - 1.0f) > 0.001f;
        const bool hasAnyTexture = hasAlbedoTexture ||
            hasAuxTexture ||
            hasNormalTexture ||
            hasOcclusionTexture ||
            hasEmissiveTexture ||
            hasOpacityTexture ||
            hasSpecularTexture ||
            hasClearcoatTexture ||
            hasTransmissionTexture ||
            hasClearcoatRoughnessTexture;
        const bool alphaMask = properties.alphaMode == MaterialAlphaMode::Mask;
        const bool alphaBlend = properties.alphaMode == MaterialAlphaMode::Blend;
        const bool hasUvTransform =
            record.uvControls.y > 0.5f ||
            std::abs(record.uvTransform.x) > 0.0001f ||
            std::abs(record.uvTransform.y) > 0.0001f ||
            std::abs(record.uvTransform.z - 1.0f) > 0.0001f ||
            std::abs(record.uvTransform.w - 1.0f) > 0.0001f ||
            std::abs(record.uvControls.x) > 0.0001f;
        const bool doubleSided = properties.doubleSided;
        const bool hasClearcoat = properties.clearcoatFactor > 0.001f;
        const bool hasTransmission = properties.transmissionFactor > 0.001f;
        const bool hasVolume = properties.volumeThicknessFactor > 0.001f;
        MaterialRenderClass materialRenderClass = command.material != nullptr
            ? properties.renderClass
            : MaterialRenderClass::DeferredOpaque;
        if (alphaBlend || (!alphaMask && alpha < 0.999f)) {
            materialRenderClass = MaterialRenderClass::Transparent;
        }
        const bool transparentCandidate =
            materialRenderClass == MaterialRenderClass::Transparent;
        const bool forwardSpecial =
            materialRenderClass == MaterialRenderClass::ForwardSpecial;
        const bool emissiveHint = hasEmissiveTexture || hasEmissiveFactor;
        const bool specularHint = hasSpecularTexture || hasSpecularFactor;
        record.materialFlags = glm::vec4(
            RenderClassValue(materialRenderClass),
            textureFlags,
            emissiveHint ? 1.0f : 0.0f,
            alpha
        );

        if (transparentCandidate) {
            ++materials.transparentCount;
        } else if (forwardSpecial) {
            ++materials.forwardSpecialCount;
        } else {
            ++materials.opaqueCount;
        }
        if (emissiveHint) {
            ++materials.emissiveHintCount;
        }
        if (specularHint) {
            ++materials.specularHintCount;
        }
        if (hasSpecularTexture) {
            ++materials.specularTextureCount;
        }
        if (alphaMask) {
            ++materials.alphaMaskCount;
        }
        if (alphaBlend) {
            ++materials.alphaBlendCount;
        }
        if (hasUvTransform) {
            ++materials.uvTransformCount;
        }
        if (doubleSided) {
            ++materials.doubleSidedCount;
        }
        if (hasClearcoat) {
            ++materials.clearcoatCount;
        }
        if (hasClearcoatTexture) {
            ++materials.clearcoatTextureCount;
        }
        if (hasClearcoatRoughnessTexture) {
            ++materials.clearcoatRoughnessTextureCount;
        }
        if (hasTransmission) {
            ++materials.transmissionCount;
        }
        if (hasTransmissionTexture) {
            ++materials.transmissionTextureCount;
        }
        if (hasVolume) {
            ++materials.volumeCount;
        }
        if (hasOpacityTexture) {
            ++materials.opacityTextureCount;
        }
        if (hasAnyTexture) {
            ++materials.texturedCount;
        }

        ++materials.count;
    }

    materials.materialData.materialCounts = glm::vec4(
        static_cast<f32>(materials.count),
        static_cast<f32>(kMaxFrameMaterials),
        static_cast<f32>(materials.overflowCount),
        static_cast<f32>(materials.transparentCount)
    );
    return materials;
}

void VulkanRenderer::BuildGBufferCommandList(
    std::span<const RenderCommand> renderCommands,
    std::vector<RenderCommand>& gBufferCommands,
    std::vector<RenderCommand>& weightedTranslucencyCommands,
    std::vector<RenderCommand>& forwardResidualCommands,
    const FrameMatrices* matrices,
    bool recordTransparentAlphaReference,
    RendererDrawStats& drawStats
) const {
    gBufferCommands.clear();
    gBufferCommands.reserve(renderCommands.size());
    weightedTranslucencyCommands.clear();
    weightedTranslucencyCommands.reserve(renderCommands.size());
    forwardResidualCommands.clear();
    forwardResidualCommands.reserve(renderCommands.size());

    for (const RenderCommand& command : renderCommands) {
        const u64 triangles = TriangleCountForCommand(command);
        switch (RenderClassForCommand(command)) {
        case MaterialRenderClass::Transparent:
            weightedTranslucencyCommands.push_back(command);
            ++drawStats.hybridForwardTransparentDraws;
            ++drawStats.hybridWeightedTranslucencyDraws;
            drawStats.hybridWeightedTranslucencyTriangles += triangles;
            if (recordTransparentAlphaReference) {
                ++drawStats.hybridForwardResidualDraws;
                drawStats.hybridForwardResidualTriangles += triangles;
            }
            break;
        case MaterialRenderClass::ForwardSpecial:
            forwardResidualCommands.push_back(command);
            ++drawStats.hybridForwardSpecialDraws;
            ++drawStats.hybridForwardResidualDraws;
            drawStats.hybridForwardResidualTriangles += triangles;
            break;
        case MaterialRenderClass::DeferredOpaque:
        default:
            gBufferCommands.push_back(command);
            ++drawStats.hybridDeferredOpaqueDraws;
            drawStats.hybridDeferredOpaqueTriangles += triangles;
            break;
        }
    }

    const glm::vec3 cameraPosition = CameraPositionFromMatrices(matrices);
    const bool hasCameraMatrices = matrices != nullptr;
    auto sortResidualCommands = [&](std::vector<RenderCommand>& commands) {
        std::vector<std::size_t> sortedIndices(commands.size());
        for (std::size_t index = 0; index < sortedIndices.size(); ++index) {
            sortedIndices[index] = index;
        }

        std::sort(
            sortedIndices.begin(),
            sortedIndices.end(),
            [&](std::size_t lhsIndex, std::size_t rhsIndex) {
                const RenderCommand& lhs = commands[lhsIndex];
                const RenderCommand& rhs = commands[rhsIndex];
                if (lhs.drawOrder != rhs.drawOrder) {
                    return lhs.drawOrder < rhs.drawOrder;
                }

                const MaterialRenderClass lhsClass = RenderClassForCommand(lhs);
                const MaterialRenderClass rhsClass = RenderClassForCommand(rhs);
                const bool lhsTransparent = lhsClass == MaterialRenderClass::Transparent;
                const bool rhsTransparent = rhsClass == MaterialRenderClass::Transparent;
                if (lhsTransparent != rhsTransparent) {
                    return lhsTransparent;
                }
                if (hasCameraMatrices && lhsTransparent && rhsTransparent) {
                    const f32 lhsDistance = DistanceSquaredToCamera(lhs, cameraPosition);
                    const f32 rhsDistance = DistanceSquaredToCamera(rhs, cameraPosition);
                    if (std::abs(lhsDistance - rhsDistance) > 0.0001f) {
                        return lhsDistance > rhsDistance;
                    }
                }

                return lhsIndex < rhsIndex;
            }
        );

        std::vector<RenderCommand> sortedCommands;
        sortedCommands.reserve(commands.size());
        for (std::size_t sortedIndex : sortedIndices) {
            sortedCommands.push_back(commands[sortedIndex]);
        }
        commands = std::move(sortedCommands);
    };

    if (!weightedTranslucencyCommands.empty()) {
        sortResidualCommands(weightedTranslucencyCommands);
        ++drawStats.hybridWeightedTranslucencySortOps;
        drawStats.hybridWeightedTranslucencySortedTransparentDraws =
            drawStats.hybridForwardTransparentDraws;
    }

    if (recordTransparentAlphaReference && !weightedTranslucencyCommands.empty()) {
        forwardResidualCommands.insert(
            forwardResidualCommands.begin(),
            weightedTranslucencyCommands.begin(),
            weightedTranslucencyCommands.end()
        );
    }

    if (!forwardResidualCommands.empty()) {
        sortResidualCommands(forwardResidualCommands);
        ++drawStats.hybridForwardResidualSortOps;
        drawStats.hybridForwardResidualSortedTransparentDraws =
            recordTransparentAlphaReference ? drawStats.hybridForwardTransparentDraws : 0u;
        drawStats.hybridForwardResidualStableSpecialDraws =
            drawStats.hybridForwardSpecialDraws;
    }

    drawStats.gBufferDraws = static_cast<u32>(gBufferCommands.size());
    drawStats.gBufferTriangles = drawStats.hybridDeferredOpaqueTriangles;
}

glm::mat4 VulkanRenderer::LightViewProjection(
    std::span<const RenderCommand> renderCommands,
    const FrameLightSet& lights
) const {
    const glm::vec3 lightDirection = NormalizedDirectionalLightDirection(lights);

    glm::vec3 worldBoundsMin{ std::numeric_limits<f32>::max() };
    glm::vec3 worldBoundsMax{ std::numeric_limits<f32>::lowest() };
    bool hasBounds = false;
    for (const RenderCommand& command : renderCommands) {
        IncludeCommandBounds(command, worldBoundsMin, worldBoundsMax, hasBounds);
    }

    if (!hasBounds) {
        worldBoundsMin = glm::vec3(-kShadowMinHalfExtent);
        worldBoundsMax = glm::vec3(kShadowMinHalfExtent);
    }

    const glm::vec3 center = (worldBoundsMin + worldBoundsMax) * 0.5f;
    const glm::vec3 sceneExtent = worldBoundsMax - worldBoundsMin;
    const f32 sceneRadius = std::max(glm::length(sceneExtent) * 0.5f, kShadowMinHalfExtent);

    const glm::vec3 lightForward = glm::normalize(lightDirection);
    const glm::vec3 eye = center - lightForward * (sceneRadius + kShadowDepthPadding);
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    if (std::abs(glm::dot(lightForward, up)) > 0.95f) {
        up = { 0.0f, 0.0f, 1.0f };
    }

    const glm::mat4 view = glm::lookAt(eye, center, up);
    glm::vec3 lightBoundsMin{ std::numeric_limits<f32>::max() };
    glm::vec3 lightBoundsMax{ std::numeric_limits<f32>::lowest() };
    for (const RenderCommand& command : renderCommands) {
        if (!command.worldBounds.valid) {
            continue;
        }

        for (const glm::vec3& worldPoint : command.worldBounds.corners) {
            IncludePoint(
                glm::vec3(view * glm::vec4(worldPoint, 1.0f)),
                lightBoundsMin,
                lightBoundsMax
            );
        }
    }

    if (!hasBounds) {
        lightBoundsMin = glm::vec3(-kShadowMinHalfExtent);
        lightBoundsMax = glm::vec3(kShadowMinHalfExtent);
    }

    const f32 xPadding = std::max(
        (lightBoundsMax.x - lightBoundsMin.x) * kShadowPaddingRatio,
        0.35f
    );
    const f32 yPadding = std::max(
        (lightBoundsMax.y - lightBoundsMin.y) * kShadowPaddingRatio,
        0.35f
    );
    lightBoundsMin.x -= xPadding;
    lightBoundsMax.x += xPadding;
    lightBoundsMin.y -= yPadding;
    lightBoundsMax.y += yPadding;
    lightBoundsMin.z -= kShadowDepthPadding;
    lightBoundsMax.z += kShadowDepthPadding;
    ExpandRangeAroundCenter(lightBoundsMin.x, lightBoundsMax.x, kShadowMinHalfExtent);
    ExpandRangeAroundCenter(lightBoundsMin.y, lightBoundsMax.y, kShadowMinHalfExtent);

    const f32 nearPlane = std::max(0.01f, -lightBoundsMax.z);
    const f32 farPlane = std::max(nearPlane + 0.1f, -lightBoundsMin.z);
    glm::mat4 projection = glm::ortho(
        lightBoundsMin.x,
        lightBoundsMax.x,
        lightBoundsMin.y,
        lightBoundsMax.y,
        nearPlane,
        farPlane
    );
    projection[1][1] *= -1.0f;
    return projection * view;
}

DirectionalShadowCascadeSet VulkanRenderer::BuildDirectionalShadowCascades(
    std::span<const RenderCommand> renderCommands,
    const FrameLightSet& lights,
    const FrameMatrices* matrices,
    bool shadowSamplingEnabled
) const {
    DirectionalShadowCascadeSet cascadeSet{};
    if (!m_ShadowSettings.enabled || !shadowSamplingEnabled) {
        return cascadeSet;
    }
    if (matrices == nullptr) {
        return cascadeSet;
    }

    const u32 requestedCount = m_ShadowSettings.cascadesEnabled
        ? m_ShadowSettings.cascadeCount
        : 1u;
    const u32 configuredCount = std::clamp<u32>(
        requestedCount,
        1u,
        static_cast<u32>(kMaxDirectionalShadowCascades)
    );
    cascadeSet.configuredCount = configuredCount;
    cascadeSet.stableSnappingEnabled = m_ShadowSettings.stableCascades;
    cascadeSet.splitLambda = std::clamp(m_ShadowSettings.cascadeSplitLambda, 0.0f, 1.0f);
    cascadeSet.maxDistance = std::max(m_ShadowSettings.cascadeMaxDistance, 0.0f);
    cascadeSet.lightAngularRadiusRadians = std::clamp(
        lights.primaryDirectional.angularRadiusRadians,
        0.0f,
        0.05f
    );

    f32 nearDepth = 0.0f;
    f32 farDepth = 0.0f;
    if (!CameraDepthRangeFromMatrices(matrices, nearDepth, farDepth)) {
        return cascadeSet;
    }

    if (cascadeSet.maxDistance > nearDepth + 0.1f) {
        farDepth = std::min(farDepth, cascadeSet.maxDistance);
    }
    farDepth = std::max(farDepth, nearDepth + 0.1f);
    cascadeSet.nearDepth = nearDepth;
    cascadeSet.farDepth = farDepth;

    f32 previousSplit = nearDepth;
    for (u32 cascadeIndex = 0; cascadeIndex < configuredCount; ++cascadeIndex) {
        const f32 ratio =
            static_cast<f32>(cascadeIndex + 1) / static_cast<f32>(configuredCount);
        const f32 logSplit = nearDepth *
            std::pow(std::max(farDepth / nearDepth, 1.0f), ratio);
        const f32 uniformSplit = nearDepth + (farDepth - nearDepth) * ratio;
        const f32 splitDepth = cascadeIndex + 1 == configuredCount
            ? farDepth
            : cascadeSet.splitLambda * logSplit +
                (1.0f - cascadeSet.splitLambda) * uniformSplit;

        std::array<glm::vec3, 8> corners{};
        if (!CameraSliceCornersFromMatrices(*matrices, previousSplit, splitDepth, corners)) {
            break;
        }

        DirectionalShadowCascade& cascade = cascadeSet.cascades[cascadeIndex];
        cascade.nearDepth = previousSplit;
        cascade.farDepth = splitDepth;
        cascade.splitDepth = splitDepth;
        cascade.viewProjection = LightViewProjectionForCascade(
            renderCommands,
            lights,
            corners,
            cascadeSet.stableSnappingEnabled,
            std::max(m_ShadowSettings.mapSize, 1u),
            &cascade.texelWorldSize,
            &cascade.lightDepthWorldSpan,
            matrices,
            previousSplit,
            splitDepth
        );
        ++cascadeSet.activeCount;
        previousSplit = splitDepth;
    }

    return cascadeSet;
}

DirectionalShadowCascadeSet
VulkanRenderer::BuildReflectionCaptureDirectionalShadow(
    std::span<const RenderCommand> shadowCommands,
    const FrameLightSet& lights,
    const RendererReflectionProbe& probe,
    u32 mapSize
) const {
    DirectionalShadowCascadeSet cascadeSet{};
    if (!m_ShadowSettings.enabled ||
        !m_ShadowSettings.directionalShadowReceiveEnabled ||
        shadowCommands.empty()) {
        return cascadeSet;
    }

    const f32 captureDistance = std::max(
        24.0f,
        std::max(
            probe.radius * 2.5f,
            glm::length(probe.boxExtents) * 2.25f
        )
    );
    // The capture frustum can look in any cubemap direction. A fixed probe-
    // volume bound keeps the directional projection independent of the player
    // camera while covering all six face receivers.
    const glm::vec3 halfExtent{ captureDistance * 1.05f };
    std::array<glm::vec3, 8> corners{};
    u32 cornerIndex = 0u;
    for (const f32 x : { -1.0f, 1.0f }) {
        for (const f32 y : { -1.0f, 1.0f }) {
            for (const f32 z : { -1.0f, 1.0f }) {
                corners[cornerIndex++] = probe.center +
                    glm::vec3(x, y, z) * halfExtent;
            }
        }
    }

    cascadeSet.configuredCount = 1u;
    cascadeSet.activeCount = 1u;
    cascadeSet.stableSnappingEnabled = m_ShadowSettings.stableCascades;
    cascadeSet.singleMapSampling = true;
    cascadeSet.maxDistance = captureDistance;
    cascadeSet.nearDepth = 0.0f;
    cascadeSet.farDepth = captureDistance;
    cascadeSet.lightAngularRadiusRadians = std::clamp(
        lights.primaryDirectional.angularRadiusRadians,
        0.0f,
        0.05f
    );
    DirectionalShadowCascade& cascade = cascadeSet.cascades[0];
    cascade.nearDepth = 0.0f;
    cascade.farDepth = captureDistance;
    cascade.splitDepth = captureDistance;
    cascade.viewProjection = LightViewProjectionForCascade(
        shadowCommands,
        lights,
        corners,
        cascadeSet.stableSnappingEnabled,
        std::max(mapSize, 1u),
        &cascade.texelWorldSize,
        &cascade.lightDepthWorldSpan,
        nullptr,
        0.0f,
        captureDistance
    );
    return cascadeSet;
}

LocalShadowTileSet VulkanRenderer::BuildLocalShadowTiles(
    const FrameLightSet& lights,
    std::span<const RenderCommand> shadowCommands,
    u32 atlasTileCapacity,
    const LocalShadowCacheState* cacheState,
    bool includeRectLights
) const {
    LocalShadowTileSet tileSet{};
    tileSet.tileCapacity = std::min<u32>(
        atlasTileCapacity,
        static_cast<u32>(tileSet.tiles.size())
    );
    if (m_LocalShadowAtlas != nullptr) {
        tileSet.tileSize = m_LocalShadowAtlas->TileSize();
        tileSet.tileColumns = m_LocalShadowAtlas->TileColumns();
        tileSet.tileRows = m_LocalShadowAtlas->TileRows();
    }

    static constexpr std::array<glm::vec3, 6> kPointFaceDirections{
        glm::vec3{ 1.0f, 0.0f, 0.0f },
        glm::vec3{ -1.0f, 0.0f, 0.0f },
        glm::vec3{ 0.0f, 1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, 0.0f, 1.0f },
        glm::vec3{ 0.0f, 0.0f, -1.0f }
    };
    static constexpr std::array<glm::vec3, 6> kPointFaceUps{
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, 0.0f, 1.0f },
        glm::vec3{ 0.0f, 0.0f, -1.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f }
    };

    const u32 localCount = std::min<u32>(
        lights.localCount,
        static_cast<u32>(lights.localLights.size())
    );
    const RectShadowSampleBudget rectSampleBudget = includeRectLights
        ? PlanRectShadowSampleBudget(lights, m_ShadowSettings, tileSet.tileCapacity)
        : RectShadowSampleBudget{};
    tileSet.rectShadowBaseSampleTiles = rectSampleBudget.baseSampleTiles;
    tileSet.rectShadowMaxSampleTiles = rectSampleBudget.maxSampleTiles;
    tileSet.rectShadowSamplePattern =
        rectSampleBudget.maxSampleTiles >= kRectAreaShadowMaxSampleTileCount
            ? kRectAreaShadowPatternSurface2x2
            : kRectAreaShadowPatternAxis;
    tileSet.rectShadowExtraSampleTiles = rectSampleBudget.extraGrantedTiles;
    tileSet.rectShadowBudgetLimitedSampleTiles =
        rectSampleBudget.budgetLimitedExtraTiles;
    for (u32 index = 0; index < localCount; ++index) {
        const RendererLocalLight& light = lights.localLights[index];
        const f32 farPlane = std::max(light.radius, kLocalShadowNearPlane + 0.1f);
        const u64 lightSignature = LocalShadowLightCacheSignature(light);
        const bool selectedByDebugIndex =
            LocalLightSelectedForShadowGeneration(m_ShadowSettings, index);
        if (light.kind == RendererLightKind::Point) {
            ++tileSet.pointLightCount;
            tileSet.pointFaceTiles += 6u;
            if (!m_ShadowSettings.pointLightShadowEnabled || !selectedByDebugIndex) {
                continue;
            }
            const f32 pointSourceRadius = std::clamp(
                light.sourceRadius,
                0.0f,
                farPlane * 0.25f
            );
            for (std::size_t faceIndex = 0; faceIndex < kPointFaceDirections.size(); ++faceIndex) {
                const LocalShadowCasterSignatureResult casterSignature =
                    LocalShadowCasterSignature(
                    shadowCommands,
                    light,
                    &kPointFaceDirections[faceIndex]
                );
                const u64 tileIdentity = LocalShadowTileIdentitySignature(
                    index,
                    static_cast<u32>(faceIndex),
                    light.kind
                );
                const LocalShadowCacheDecision cacheDecision =
                    DetermineLocalShadowCacheDecision(
                        cacheState,
                        tileSet.assignedCount,
                        tileIdentity,
                        lightSignature,
                        casterSignature.signature,
                        casterSignature.hasDynamicSkinnedCaster
                    );
                AddLocalShadowTile(
                    tileSet,
                    LocalShadowViewProjection(
                        light.position,
                        kPointFaceDirections[faceIndex],
                        kPointFaceUps[faceIndex],
                        glm::radians(90.0f),
                        farPlane
                    ),
                    index,
                    static_cast<u32>(faceIndex),
                    light.kind,
                    glm::vec4(
                        kLocalShadowNearPlane,
                        farPlane,
                        pointSourceRadius,
                        1.0f
                    ),
                    tileIdentity,
                    lightSignature,
                    casterSignature.signature,
                    cacheDecision
                );
            }
        } else if (light.kind == RendererLightKind::Spot) {
            ++tileSet.spotLightCount;
            ++tileSet.spotTiles;
            if (!m_ShadowSettings.spotLightShadowEnabled || !selectedByDebugIndex) {
                continue;
            }
            const LocalShadowCasterSignatureResult casterSignature =
                LocalShadowCasterSignature(shadowCommands, light);
            const f32 outerConeCos = std::clamp(light.outerConeCos, 0.0f, 0.999f);
            const f32 outerConeRadians = std::acos(outerConeCos);
            const f32 spotFov = std::clamp(
                outerConeRadians * 2.0f,
                glm::radians(5.0f),
                glm::radians(175.0f)
            );
            const f32 spotSourceRadius = std::clamp(
                light.sourceRadius,
                0.0f,
                farPlane * 0.25f
            );
            const u64 tileIdentity = LocalShadowTileIdentitySignature(
                index,
                0u,
                light.kind
            );
            const LocalShadowCacheDecision cacheDecision =
                DetermineLocalShadowCacheDecision(
                    cacheState,
                    tileSet.assignedCount,
                    tileIdentity,
                    lightSignature,
                    casterSignature.signature,
                    casterSignature.hasDynamicSkinnedCaster
                );
            AddLocalShadowTile(
                tileSet,
                LocalShadowViewProjection(
                    light.position,
                    light.direction,
                    { 0.0f, 1.0f, 0.0f },
                    spotFov,
                    farPlane
                ),
                index,
                0u,
                light.kind,
                glm::vec4(
                    kLocalShadowNearPlane,
                    farPlane,
                    spotSourceRadius,
                    std::tan(spotFov * 0.5f)
                ),
                tileIdentity,
                lightSignature,
                casterSignature.signature,
                cacheDecision
            );
        } else if (light.kind == RendererLightKind::Rect) {
            if (!includeRectLights) {
                continue;
            }
            ++tileSet.rectLightCount;
            tileSet.rectTiles += rectSampleBudget.maxSampleTiles;
            const u32 rectSampleTileCount = rectSampleBudget.sampleCounts[index];
            if (!m_ShadowSettings.rectLightShadowEnabled ||
                !selectedByDebugIndex ||
                rectSampleTileCount == 0u) {
                continue;
            }
            const LocalShadowCasterSignatureResult casterSignature =
                LocalShadowCasterSignature(shadowCommands, light);
            const f32 rectSourceRadius = RectShadowResidualSourceRadius(
                light.width,
                light.height,
                rectSampleTileCount
            );
            const f32 rectTanHalfFov = std::tan(glm::radians(64.0f));
            for (u32 sampleIndex = 0u;
                 sampleIndex < rectSampleTileCount;
                 ++sampleIndex) {
                const u64 tileIdentity = LocalShadowTileIdentitySignature(
                    index,
                    sampleIndex,
                    light.kind
                );
                const LocalShadowCacheDecision cacheDecision =
                    DetermineLocalShadowCacheDecision(
                        cacheState,
                        tileSet.assignedCount,
                        tileIdentity,
                        lightSignature,
                        casterSignature.signature,
                        casterSignature.hasDynamicSkinnedCaster
                    );
                AddLocalShadowTile(
                    tileSet,
                    LocalRectAreaShadowSampleViewProjection(
                        light.position,
                        light.direction,
                        light.width,
                        light.height,
                        sampleIndex,
                        rectSampleTileCount,
                        farPlane
                    ),
                    index,
                    sampleIndex,
                    light.kind,
                    glm::vec4(
                        kLocalShadowNearPlane,
                        farPlane,
                        rectSourceRadius,
                        rectTanHalfFov
                    ),
                    tileIdentity,
                    lightSignature,
                    casterSignature.signature,
                    cacheDecision
                );
            }
        }
    }

    if (tileSet.assignedCount > 0 && tileSet.cacheHitTiles > 0) {
        tileSet.cacheSkippedTiles = tileSet.cacheHitTiles;
    }

    return tileSet;
}

bool VulkanRenderer::LocalReflectionProbeCubemapReady() const {
    return m_ReflectionProbeResources.BuiltInProceduralReady(m_IblSampler);
}

void VulkanRenderer::EnsureVisibleSkyboxResources() {
    if (m_VisibleSkyboxFallbackTexture == nullptr) {
        m_VisibleSkyboxFallbackTexture = std::make_unique<VulkanTexture2D>(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            VulkanTexturePixels{
                std::span<const u8>(
                    kVisibleSkyboxFallbackPixel.data(),
                    kVisibleSkyboxFallbackPixel.size()
                ),
                1u,
                1u
            },
            true,
            false,
            false
        );
    }

    if (m_VisibleSkyboxTexture == nullptr) {
        const std::filesystem::path skyboxPath =
            std::filesystem::path(SE_ASSET_DIR) / "skybox" / "bk.jpg";
        try {
            m_VisibleSkyboxTexture = std::make_unique<VulkanTexture2D>(
                m_Device,
                m_PhysicalDevice,
                m_CommandPool,
                skyboxPath.string(),
                true,
                true,
                false
            );
        } catch (...) {
            m_VisibleSkyboxTexture.reset();
        }
    }

    const VulkanTexture2D* samplerTexture =
        m_VisibleSkyboxTexture != nullptr
            ? m_VisibleSkyboxTexture.get()
            : m_VisibleSkyboxFallbackTexture.get();
    if (m_VisibleSkyboxSampler == nullptr && samplerTexture != nullptr) {
        m_VisibleSkyboxSampler = std::make_unique<VulkanSampler>(
            m_Device,
            m_PhysicalDevice,
            std::max(1u, samplerTexture->MipLevels())
        );
    }
}

bool VulkanRenderer::EnsureReflectionProbeCapturePipelines() {
    const VkRenderPass renderPass =
        m_ReflectionProbeResources.GpuCapturedSceneRenderPass();
    const VkExtent2D extent = m_ReflectionProbeResources.GpuCapturedSceneExtent();
    if (renderPass == VK_NULL_HANDLE || extent.width == 0u || extent.height == 0u ||
        m_DescriptorSetLayout == nullptr || m_MaterialDescriptorSetLayout == nullptr ||
        m_Swapchain == nullptr) {
        return false;
    }
    if (m_ReflectionCaptureGraphicsPipeline != nullptr &&
        m_DoubleSidedReflectionCaptureGraphicsPipeline != nullptr &&
        m_ReflectionCapturePipelineRenderPass == renderPass) {
        return true;
    }

    m_ReflectionCaptureGraphicsPipeline.reset();
    m_DoubleSidedReflectionCaptureGraphicsPipeline.reset();
    m_ReflectionCapturePipelineRenderPass = VK_NULL_HANDLE;
    try {
        PipelineSpec spec = PipelineSpec::DefaultForward3D(
            m_PipelineSpec.vertexShaderPath,
            m_PipelineSpec.fragmentShaderPath
        );
        spec.fixedExtent = extent;
        m_ReflectionCaptureGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                renderPass,
                *m_Swapchain,
                spec
            );
        m_DoubleSidedReflectionCaptureGraphicsPipeline =
            std::make_unique<VulkanGraphicsPipeline>(
                m_Device,
                *m_DescriptorSetLayout,
                *m_MaterialDescriptorSetLayout,
                renderPass,
                *m_Swapchain,
                PipelineSpec::DoubleSided(spec)
            );
        m_ReflectionCapturePipelineRenderPass = renderPass;
    } catch (...) {
        m_ReflectionCaptureGraphicsPipeline.reset();
        m_DoubleSidedReflectionCaptureGraphicsPipeline.reset();
        return false;
    }
    return true;
}

struct ReflectionProbeCaptureFaceAxes {
    glm::vec3 direction{ 0.0f, 0.0f, 1.0f };
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };
};

ReflectionProbeCaptureFaceAxes ReflectionProbeCaptureFaceAxesFor(u32 face) {
    static const std::array<glm::vec3, 6> directions = {
        glm::vec3{ 1.0f, 0.0f, 0.0f },
        glm::vec3{ -1.0f, 0.0f, 0.0f },
        glm::vec3{ 0.0f, 1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, 0.0f, 1.0f },
        glm::vec3{ 0.0f, 0.0f, -1.0f }
    };
    static const std::array<glm::vec3, 6> ups = {
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, 0.0f, 1.0f },
        glm::vec3{ 0.0f, 0.0f, -1.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f },
        glm::vec3{ 0.0f, -1.0f, 0.0f }
    };
    const u32 index = std::min<u32>(face, 5u);
    return ReflectionProbeCaptureFaceAxes{ directions[index], ups[index] };
}

bool ReflectionProbeCaptureFaceOrientationValid(
    const FrameMatrices& matrices,
    const RendererReflectionProbe& probe,
    u32 face
) {
    const ReflectionProbeCaptureFaceAxes axes =
        ReflectionProbeCaptureFaceAxesFor(face);
    const glm::mat4 cameraWorld = glm::inverse(matrices.view);
    const glm::vec3 position = glm::vec3(cameraWorld[3]);
    const glm::vec3 forward = -glm::normalize(glm::vec3(cameraWorld[2]));
    const glm::vec3 up = glm::normalize(glm::vec3(cameraWorld[1]));
    return glm::distance(position, probe.center) <= 0.001f &&
        glm::dot(forward, axes.direction) >= 0.999f &&
        glm::dot(up, axes.up) >= 0.999f &&
        std::abs(glm::dot(forward, up)) <= 0.001f;
}

FrameMatrices VulkanRenderer::ReflectionProbeCaptureMatrices(
    const RendererReflectionProbe& probe,
    u32 face
) const {
    const ReflectionProbeCaptureFaceAxes axes =
        ReflectionProbeCaptureFaceAxesFor(face);
    const f32 farPlane = std::max(
        24.0f,
        std::max(probe.radius * 2.5f, glm::length(probe.boxExtents) * 2.25f)
    );
    // Cubemap layer orientation follows samplerCube's standard convention.
    // Unlike the swapchain camera, its per-face projection must not apply the
    // Vulkan presentation-space Y flip.
    const glm::mat4 cubemapProjection = glm::perspective(
        1.57079632679f,
        1.0f,
        0.05f,
        farPlane
    );
    return FrameMatrices{
        glm::lookAt(probe.center, probe.center + axes.direction, axes.up),
        cubemapProjection
    };
}

bool VulkanRenderer::CaptureNextReflectionProbeFace(
    std::size_t imageIndex,
    const FrameLightSet& lights,
    const RendererReflectionProbe& probe
) {
    constexpr std::size_t kReflectionCaptureDescriptorIndex = 0u;
    const CapturedReflectionProbeFilteringSettings captureFilteringSettings =
        CapturedReflectionProbeFilteringSettingsFromEnvironment();
    (void)imageIndex;
    if (!m_ReflectionProbeResources.GpuCapturedSceneRefreshPending(
            probe.sceneIndex
        ) ||
        m_ReflectionCaptureGraphicsPipeline == nullptr ||
        m_DoubleSidedReflectionCaptureGraphicsPipeline == nullptr ||
        m_DescriptorSets == nullptr ||
        m_ReflectionCaptureMaterialDescriptorSets == nullptr) {
        return false;
    }

    const u32 face = m_ReflectionProbeResources.GpuCapturedSceneNextFace(
        probe.sceneIndex
    );
    const VkExtent2D extent = m_ReflectionProbeResources.GpuCapturedSceneExtent(
        probe.sceneIndex
    );
    const VkFramebuffer framebuffer =
        m_ReflectionProbeResources.GpuCapturedSceneFramebuffer(
            probe.sceneIndex,
            face
        );
    if (extent.width == 0u || extent.height == 0u || framebuffer == VK_NULL_HANDLE) {
        return false;
    }

    const FrameMatrices matrices = ReflectionProbeCaptureMatrices(probe, face);
    m_ReflectionProbeResources.RecordGpuCapturedSceneFaceOrientation(
        probe.sceneIndex,
        face,
        ReflectionProbeCaptureFaceOrientationValid(matrices, probe, face)
    );
    const Frustum frustum = Frustum::FromViewProjection(matrices.proj * matrices.view);
    RenderQueueCullingStats cullingStats{};
    RenderQueueCacheStats cacheStats{};
    m_ReflectionCaptureRenderQueue.Clear();
    if (m_RenderQueueBuilder) {
        m_RenderQueueBuilder(
            m_ReflectionCaptureRenderQueue,
            RenderQueueContext{ &frustum, &cullingStats, &cacheStats, nullptr, nullptr }
        );
    } else if (m_MainScene3D != nullptr) {
        RenderQueueBuildOptions options{};
        options.frustum = &frustum;
        options.cullingStats = &cullingStats;
        options.cacheStats = &cacheStats;
        options.sceneIdentity = m_MainScene3D;
        options.sceneMembershipRevision = m_MainScene3D->MembershipRevision();
        options.sceneRenderRevision = m_MainScene3D->RenderRevision();
        options.useSceneRevisions = true;
        m_ReflectionCaptureRenderQueue.BuildFromScene3D(
            m_RenderResources,
            m_MainScene3D->Renderables(),
            m_MainScene3D->SelectedRenderable(),
            options
        );
    }
    const std::span<const RenderCommand> captureCommands =
        m_ReflectionCaptureRenderQueue.Commands();
    const std::span<const RenderCommand> captureShadowCommands =
        ShadowRenderCommands();
    const bool snapshotEnabled =
        !EnvironmentFlagEnabled("SE_REFLECTION_CAPTURE_SHADOW_SNAPSHOT_OFF");
    const bool persistentShadowCacheEnabled = snapshotEnabled &&
        !EnvironmentFlagEnabled(
            "SE_REFLECTION_CAPTURE_PERSISTENT_SHADOW_CACHE_OFF"
        );
    const bool captureRectShadowEnabled =
        !EnvironmentFlagEnabled("SE_REFLECTION_CAPTURE_RECT_SHADOWS_OFF");
    ReflectionCaptureShadowSnapshot* persistentShadowSnapshot =
        persistentShadowCacheEnabled
            ? AcquireReflectionCapturePersistentShadowSnapshot(probe.sceneIndex)
            : nullptr;
    const i32 persistentShadowCacheSlot =
        ReflectionCapturePersistentShadowSnapshotSlot(persistentShadowSnapshot);
    ReflectionCapturePersistentShadowSnapshot* persistentShadowResource =
        persistentShadowCacheSlot >= 0
            ? &m_ReflectionCapturePersistentShadowSnapshots[
                static_cast<std::size_t>(persistentShadowCacheSlot)]
            : nullptr;
    const bool persistentShadowCacheActive = persistentShadowResource != nullptr;
    const CapturedSceneCaptureAudit& captureAudit =
        m_ReflectionProbeResources.CapturedSceneAudit(probe.sceneIndex);
    const u32 shadowInputSignature = ReflectionCaptureShadowInputSignature(
        probe,
        captureAudit,
        captureRectShadowEnabled
    );
    const bool snapshotInvalid =
        !(persistentShadowCacheActive
            ? persistentShadowResource->snapshot.built
            : m_ReflectionCaptureShadowSnapshot.built) ||
        (persistentShadowCacheActive
            ? persistentShadowResource->snapshot.probeSceneIndex
            : m_ReflectionCaptureShadowSnapshot.probeSceneIndex) != probe.sceneIndex ||
        (persistentShadowCacheActive
            ? persistentShadowResource->snapshot.inputSignature
            : m_ReflectionCaptureShadowSnapshot.inputSignature) != shadowInputSignature;
    if (snapshotEnabled && face != 0u && snapshotInvalid) {
        return false;
    }
    const bool buildShadowSnapshot = snapshotEnabled && snapshotInvalid;
    const bool recordShadowDepthMaps = !snapshotEnabled || buildShadowSnapshot;
    ReflectionCaptureShadowSnapshot transientShadowSnapshot{};
    ReflectionCaptureShadowSnapshot* captureShadowSnapshot = snapshotEnabled
        ? persistentShadowCacheActive
            ? &persistentShadowResource->snapshot
            : &m_ReflectionCaptureShadowSnapshot
        : &transientShadowSnapshot;
    const VulkanShadowMap* captureShadowMap = persistentShadowCacheActive
        ? persistentShadowResource->directionalShadowMap.get()
        : m_ReflectionCaptureShadowMap.get();
    const VulkanLocalShadowAtlas* captureLocalShadowAtlas =
        persistentShadowCacheActive
            ? persistentShadowResource->localShadowAtlas.get()
            : m_ReflectionCaptureLocalShadowAtlas.get();
    const VulkanShadowFramebuffer* captureDirectionalShadowFramebuffer =
        persistentShadowCacheActive
            ? persistentShadowResource->directionalShadowFramebuffer.get()
            : m_ReflectionCaptureShadowFramebuffer.get();
    const VulkanShadowFramebuffer* captureLocalShadowFramebuffer =
        persistentShadowCacheActive
            ? persistentShadowResource->localShadowFramebuffer.get()
            : m_ReflectionCaptureLocalShadowFramebuffer.get();
    const VulkanMaterialDescriptorSets* captureMaterialDescriptorSets =
        persistentShadowCacheActive
            ? persistentShadowResource->materialDescriptorSets.get()
            : m_ReflectionCaptureMaterialDescriptorSets.get();
    const bool persistentShadowCacheHit = persistentShadowCacheActive &&
        !snapshotInvalid && face == 0u;

    if (recordShadowDepthMaps) {
        *captureShadowSnapshot = {};
        captureShadowSnapshot->probeSceneIndex = probe.sceneIndex;
        captureShadowSnapshot->inputSignature = shadowInputSignature;
        captureShadowSnapshot->directionalRequested =
            m_ShadowSettings.enabled &&
            m_ShadowSettings.strength > 0.001f &&
            m_ShadowSettings.directionalShadowReceiveEnabled &&
            !captureShadowCommands.empty();
        captureShadowSnapshot->directionalShadows =
            captureShadowSnapshot->directionalRequested
                ? BuildReflectionCaptureDirectionalShadow(
                    captureShadowCommands,
                    lights,
                    probe,
                    captureShadowMap != nullptr
                        ? captureShadowMap->Extent().width
                        : m_ShadowSettings.mapSize
                )
                : DirectionalShadowCascadeSet{};
        captureShadowSnapshot->directionalAvailable =
            captureShadowSnapshot->directionalRequested &&
            captureShadowSnapshot->directionalShadows.activeCount == 1u &&
            captureShadowMap != nullptr &&
            captureDirectionalShadowFramebuffer != nullptr &&
            m_ShadowRenderPass != nullptr &&
            m_ShadowGraphicsPipeline != nullptr;
        captureShadowSnapshot->rectShadowEnabled = captureRectShadowEnabled;
        captureShadowSnapshot->localShadowTiles = BuildLocalShadowTiles(
            lights,
            captureShadowCommands,
            captureLocalShadowAtlas != nullptr
                ? captureLocalShadowAtlas->TileCapacity()
                : 0u,
            nullptr,
            captureShadowSnapshot->rectShadowEnabled
        );
        captureShadowSnapshot->localRequested =
            m_ShadowSettings.enabled &&
            !captureShadowCommands.empty() &&
            captureShadowSnapshot->localShadowTiles.assignedCount > 0u;
        captureShadowSnapshot->localAvailable =
            captureShadowSnapshot->localRequested &&
            captureLocalShadowAtlas != nullptr &&
            captureLocalShadowFramebuffer != nullptr &&
            m_ShadowRenderPass != nullptr &&
            m_ShadowGraphicsPipeline != nullptr;
        captureShadowSnapshot->localShadowTileCommandLists =
            BuildLocalShadowTileCommandLists(
                captureShadowCommands,
                lights,
                captureShadowSnapshot->localShadowTiles
            );

        const u32 localTileCount = std::min<u32>(
            captureShadowSnapshot->localShadowTiles.assignedCount,
            static_cast<u32>(captureShadowSnapshot->localShadowTiles.tiles.size())
        );
        for (u32 index = 0u; index < localTileCount; ++index) {
            switch (static_cast<RendererLightKind>(
                captureShadowSnapshot->localShadowTiles.tiles[index].lightKind
            )) {
            case RendererLightKind::Point:
                ++captureShadowSnapshot->localPointFaceTileCount;
                break;
            case RendererLightKind::Spot:
                ++captureShadowSnapshot->localSpotTileCount;
                break;
            case RendererLightKind::Rect:
                ++captureShadowSnapshot->localRectTileCount;
                break;
            case RendererLightKind::Directional:
                break;
            }
        }
        const u32 localLightCount = std::min<u32>(
            lights.localCount,
            static_cast<u32>(lights.localLights.size())
        );
        for (u32 index = 0u; index < localLightCount; ++index) {
            if (lights.localLights[index].kind == RendererLightKind::Rect) {
                captureShadowSnapshot->localRectRequestedTileCount +=
                    captureShadowSnapshot->localShadowTiles
                        .requestedTilesByLocalLight[index];
            }
        }
        captureShadowSnapshot->localRectDroppedTileCount =
            captureShadowSnapshot->localRectRequestedTileCount >
                    captureShadowSnapshot->localRectTileCount
                ? captureShadowSnapshot->localRectRequestedTileCount -
                      captureShadowSnapshot->localRectTileCount
                : 0u;
    }

    const DirectionalShadowCascadeSet& captureDirectionalShadows =
        captureShadowSnapshot->directionalShadows;
    const LocalShadowTileSet& captureLocalShadowTiles =
        captureShadowSnapshot->localShadowTiles;
    const bool directionalShadowRequested =
        captureShadowSnapshot->directionalRequested;
    const bool directionalShadowAvailable =
        captureShadowSnapshot->directionalAvailable;
    const bool captureLocalShadowRequested = captureShadowSnapshot->localRequested;
    const bool captureLocalShadowAvailable = captureShadowSnapshot->localAvailable;
    const bool captureRectShadowsApplied =
        captureShadowSnapshot->rectShadowEnabled;
    if (captureMaterialDescriptorSets == nullptr) {
        return false;
    }
    const FrameMaterialSet captureMaterials = BuildFrameMaterialSet(captureCommands);
    FrameReflectionProbeSet captureProbes{};
    captureProbes.fallbackEnabled = m_ShadowSettings.reflectionProbeFallbackEnabled;
    // Slot zero is intentionally reserved for capture-only data. Wait for the
    // previous main submission before rewriting it, because main rendering can
    // use that same swapchain-indexed slot on a prior frame.
    if (vkQueueWaitIdle(m_Device.GraphicsQueue()) != VK_SUCCESS) {
        return false;
    }
    // Capture intentionally disables local probe sampling to avoid feedback. The
    // descriptor still needs valid cube views before its first offscreen draw.
    (void)UpdateEnvironmentDescriptorSets(
        m_DescriptorSets.get(),
        nullptr,
        kReflectionCaptureDescriptorIndex
    );
    const FrameLightConstants lightConstants = lights.Constants();
    // Capture writes a probe-centered snapshot once, then all six faces sample
    // that immutable depth data through the reserved descriptor slot.
    UpdateDirectionalShadowCascadeBuffer(
        kReflectionCaptureDescriptorIndex,
        captureDirectionalShadows,
        captureDirectionalShadows.activeCount > 0u
            ? captureDirectionalShadows.cascades[0].viewProjection
            : glm::mat4{ 1.0f }
    );
    UpdateLocalShadowBuffer(
        kReflectionCaptureDescriptorIndex,
        captureLocalShadowTiles
    );
    UpdateUniformBuffer(
        kReflectionCaptureDescriptorIndex,
        &matrices,
        captureDirectionalShadows.activeCount > 0u
            ? captureDirectionalShadows.cascades[0].viewProjection
            : glm::mat4{ 1.0f },
        lightConstants,
        captureProbes,
        directionalShadowAvailable || captureLocalShadowAvailable,
        nullptr
    );
    FrameLightTileStats ignoredTileStats{};
    UpdateLightBuffer(
        kReflectionCaptureDescriptorIndex,
        lights,
        extent,
        &matrices,
        &ignoredTileStats
    );
    UpdateMaterialBuffer(kReflectionCaptureDescriptorIndex, captureMaterials);

    std::array<std::span<const RenderCommand>, kMaxLocalShadowTiles>
        captureLocalShadowTileCommandSpans{};
    const u32 captureLocalShadowTileCommandSpanCount = std::min<u32>(
        static_cast<u32>(captureShadowSnapshot->localShadowTileCommandLists.size()),
        static_cast<u32>(captureLocalShadowTileCommandSpans.size())
    );
    for (u32 index = 0u;
         index < captureLocalShadowTileCommandSpanCount;
         ++index) {
        captureLocalShadowTileCommandSpans[index] =
            std::span<const RenderCommand>(
                captureShadowSnapshot->localShadowTileCommandLists[index].data(),
                captureShadowSnapshot->localShadowTileCommandLists[index].size()
            );
    }
    const u32 captureLocalShadowTileCount = std::min<u32>(
        captureLocalShadowTiles.assignedCount,
        static_cast<u32>(captureLocalShadowTiles.tiles.size())
    );

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = m_CommandPool.Handle();
    allocateInfo.commandBufferCount = 1u;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_Device.Handle(), &allocateInfo, &commandBuffer) != VK_SUCCESS) {
        return false;
    }

    bool submitted = false;
    const bool captureComplete = face == 5u;
    ReflectionCaptureDrawStats drawStats{};
    ReflectionCaptureDirectionalShadowDrawStats directionalShadowDrawStats{};
    ReflectionCaptureLocalShadowDrawStats localShadowDrawStats{};
    const ShadowDepthBiasControls shadowDepthBias{
        m_ShadowSettings.casterDepthBiasEnabled,
        std::clamp(m_ShadowSettings.casterDepthBiasConstant, 0.0f, 262144.0f),
        m_PhysicalDevice.Features().depthBiasClamp
            ? std::clamp(m_ShadowSettings.casterDepthBiasClamp, 0.0f, 0.05f)
            : 0.0f,
        std::clamp(m_ShadowSettings.casterDepthBiasSlope, 0.0f, 16.0f)
    };
    try {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("Failed to begin GPU reflection capture command buffer");
        }

        if (recordShadowDepthMaps && directionalShadowAvailable) {
            directionalShadowDrawStats = RecordReflectionCaptureDirectionalShadow(
                commandBuffer,
                *m_ShadowRenderPass,
                *m_ShadowGraphicsPipeline,
                m_DoubleSidedShadowGraphicsPipeline.get(),
                *captureDirectionalShadowFramebuffer,
                *m_DescriptorSets,
                captureShadowCommands,
                kReflectionCaptureDescriptorIndex,
                captureDirectionalShadows.cascades[0].viewProjection,
                shadowDepthBias,
                m_BonePaletteFallbackDescriptorSet != nullptr
                    ? m_BonePaletteFallbackDescriptorSet->Handle()
                    : VK_NULL_HANDLE,
                m_BonePaletteFallbackDescriptorSet != nullptr
                    ? m_BonePaletteFallbackDescriptorSet->Ready()
                    : 0u
            );
        }

        if (recordShadowDepthMaps && captureLocalShadowAvailable) {
            localShadowDrawStats = RecordReflectionCaptureLocalShadows(
                commandBuffer,
                *m_ShadowRenderPass,
                *m_ShadowGraphicsPipeline,
                m_DoubleSidedShadowGraphicsPipeline.get(),
                *captureLocalShadowFramebuffer,
                *m_DescriptorSets,
                captureLocalShadowTiles,
                std::span<const std::span<const RenderCommand>>(
                    captureLocalShadowTileCommandSpans.data(),
                    captureLocalShadowTileCommandSpanCount
                ),
                kReflectionCaptureDescriptorIndex,
                shadowDepthBias,
                m_BonePaletteFallbackDescriptorSet != nullptr
                    ? m_BonePaletteFallbackDescriptorSet->Handle()
                    : VK_NULL_HANDLE,
                m_BonePaletteFallbackDescriptorSet != nullptr
                    ? m_BonePaletteFallbackDescriptorSet->Ready()
                    : 0u
            );
        }

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0u };
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_ReflectionProbeResources.GpuCapturedSceneRenderPass();
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea.extent = extent;
        renderPassInfo.clearValueCount = static_cast<u32>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(
            commandBuffer,
            &renderPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );
        drawStats = RecordReflectionCaptureCommands(
            commandBuffer,
            *m_ReflectionCaptureGraphicsPipeline,
            m_DoubleSidedReflectionCaptureGraphicsPipeline.get(),
            *m_DescriptorSets,
            *captureMaterialDescriptorSets,
            captureMaterials,
            captureCommands,
            extent,
            kReflectionCaptureDescriptorIndex
        );
        vkCmdEndRenderPass(commandBuffer);
        if (captureComplete) {
            m_ReflectionProbeResources.RecordGpuCapturedSceneMipGeneration(
                probe.sceneIndex,
                commandBuffer,
                captureFilteringSettings
            );
            m_ReflectionProbeResources.RecordGpuCapturedSceneDiffuseIrradiance(
                probe.sceneIndex,
                commandBuffer
            );
        }
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to record GPU reflection capture command buffer");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1u;
        submitInfo.pCommandBuffers = &commandBuffer;
        if (vkQueueSubmit(m_Device.GraphicsQueue(), 1u, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit GPU reflection capture command buffer");
        }
        vkQueueWaitIdle(m_Device.GraphicsQueue());
        submitted = true;
    } catch (...) {
        if (!submitted) {
            (void)vkEndCommandBuffer(commandBuffer);
        }
    }
    vkFreeCommandBuffers(m_Device.Handle(), m_CommandPool.Handle(), 1u, &commandBuffer);
    if (!submitted) {
        return false;
    }

    if (recordShadowDepthMaps) {
        captureShadowSnapshot->directionalDrawCount =
            directionalShadowDrawStats.drawCount;
        captureShadowSnapshot->directionalCasterCount =
            directionalShadowAvailable
                ? static_cast<u32>(captureShadowCommands.size())
                : 0u;
        captureShadowSnapshot->localTilePassCount =
            localShadowDrawStats.tilePassCount;
        captureShadowSnapshot->localDrawCount = localShadowDrawStats.drawCount;
    }
    if (snapshotEnabled && buildShadowSnapshot) {
        captureShadowSnapshot->built = true;
    }

    const bool directionalShadowReady = directionalShadowAvailable &&
        (recordShadowDepthMaps
            ? directionalShadowDrawStats.passCount == 1u &&
                directionalShadowDrawStats.drawCount > 0u
            : captureShadowSnapshot->directionalDrawCount > 0u);
    m_ReflectionProbeResources.RecordGpuCapturedSceneDirectionalShadow(
        probe.sceneIndex,
        face,
        directionalShadowAvailable && captureShadowMap != nullptr
            ? captureShadowMap->Extent().width
            : 0u,
        recordShadowDepthMaps ? directionalShadowDrawStats.passCount : 0u,
        recordShadowDepthMaps ? directionalShadowDrawStats.drawCount : 0u,
        recordShadowDepthMaps && directionalShadowReady
            ? captureShadowSnapshot->directionalCasterCount
            : 0u,
        directionalShadowRequested,
        directionalShadowReady,
        directionalShadowAvailable &&
            captureDirectionalShadows.singleMapSampling,
        m_LocalShadowBuffer != nullptr
    );
    const bool localShadowFaceReady = captureLocalShadowAvailable &&
        (recordShadowDepthMaps
            ? localShadowDrawStats.tilePassCount == captureLocalShadowTileCount
            : captureShadowSnapshot->localTilePassCount ==
                captureLocalShadowTileCount &&
                captureShadowSnapshot->localDrawCount > 0u);
    m_ReflectionProbeResources.RecordGpuCapturedSceneLocalShadow(
        probe.sceneIndex,
        face,
        captureLocalShadowAtlas != nullptr
            ? captureLocalShadowAtlas->TileSize()
            : 0u,
        recordShadowDepthMaps ? localShadowDrawStats.tilePassCount : 0u,
        recordShadowDepthMaps ? localShadowDrawStats.drawCount : 0u,
        recordShadowDepthMaps ? localShadowDrawStats.drawCount : 0u,
        recordShadowDepthMaps ? captureLocalShadowTileCount : 0u,
        recordShadowDepthMaps
            ? captureShadowSnapshot->localPointFaceTileCount
            : 0u,
        recordShadowDepthMaps ? captureShadowSnapshot->localSpotTileCount : 0u,
        recordShadowDepthMaps ? captureShadowSnapshot->localRectTileCount : 0u,
        recordShadowDepthMaps ? captureLocalShadowTiles.requestedCount : 0u,
        recordShadowDepthMaps ? captureLocalShadowTiles.droppedCount : 0u,
        recordShadowDepthMaps
            ? captureShadowSnapshot->localRectRequestedTileCount
            : 0u,
        recordShadowDepthMaps ? captureLocalShadowTiles.rectTiles : 0u,
        recordShadowDepthMaps
            ? captureLocalShadowTiles.rectShadowExtraSampleTiles
            : 0u,
        recordShadowDepthMaps
            ? captureLocalShadowTiles.rectShadowBudgetLimitedSampleTiles
            : 0u,
        recordShadowDepthMaps
            ? captureShadowSnapshot->localRectDroppedTileCount
            : 0u,
        captureLocalShadowRequested,
        localShadowFaceReady,
        true,
        captureRectShadowsApplied ? 0x7u : 0x3u,
        captureRectShadowsApplied ? 0x0u : 0x4u
    );
    m_ReflectionProbeResources.RecordGpuCapturedSceneShadowSnapshot(
        probe.sceneIndex,
        face,
        buildShadowSnapshot,
        snapshotEnabled && !buildShadowSnapshot && directionalShadowRequested
            ? 1u
            : 0u,
        snapshotEnabled && !buildShadowSnapshot
            ? captureShadowSnapshot->localTilePassCount
            : 0u,
        snapshotEnabled && !buildShadowSnapshot
            ? captureShadowSnapshot->localDrawCount
            : 0u,
        directionalShadowReady || localShadowFaceReady,
        true,
        snapshotEnabled,
        persistentShadowCacheActive,
        persistentShadowCacheHit,
        persistentShadowCacheSlot,
        ReflectionCapturePersistentShadowSnapshotCount(),
        m_ReflectionCapturePersistentShadowSnapshotEvictionCount,
        shadowInputSignature
    );

    m_ReflectionProbeResources.CompleteGpuCapturedSceneFace(
        probe.sceneIndex,
        face,
        drawStats.drawCount,
        cullingStats.visible,
        cullingStats.culled,
        captureComplete,
        m_ReflectionCaptureSchedulerFrame
    );
    if (captureComplete) {
        ResetReflectionCaptureShadowSnapshot();
    }
    return true;
}

std::span<const RenderCommand> VulkanRenderer::ReflectionCaptureInfluenceCommands() {
    m_ReflectionCaptureInfluenceRenderQueue.Clear();
    if (m_RenderQueueBuilder) {
        RenderQueueCacheStats cacheStats{};
        m_RenderQueueBuilder(
            m_ReflectionCaptureInfluenceRenderQueue,
            RenderQueueContext{ nullptr, nullptr, &cacheStats, nullptr, nullptr }
        );
    } else if (m_MainScene3D != nullptr) {
        RenderQueueBuildOptions options{};
        options.sceneIdentity = m_MainScene3D;
        options.sceneMembershipRevision = m_MainScene3D->MembershipRevision();
        options.sceneRenderRevision = m_MainScene3D->RenderRevision();
        options.useSceneRevisions = true;
        m_ReflectionCaptureInfluenceRenderQueue.BuildFromScene3D(
            m_RenderResources,
            m_MainScene3D->Renderables(),
            m_MainScene3D->SelectedRenderable(),
            options
        );
    }
    return m_ReflectionCaptureInfluenceRenderQueue.Commands();
}

void VulkanRenderer::PrepareReflectionProbeCaptureResources(
    std::size_t imageIndex,
    const FrameLightSet& lights,
    const FrameMatrices* matrices
) {
    (void)matrices;
    if (m_MainScene3D == nullptr ||
        m_IblSampler == VK_NULL_HANDLE) {
        return;
    }

    const std::optional<RendererReflectionProbeCaptureSource>
        captureSourceOverride = ReflectionProbeCaptureSourceOverrideFromEnvironment();
    const std::optional<RendererReflectionProbeRefreshPolicy>
        refreshPolicyOverride = ReflectionProbeRefreshPolicyOverrideFromEnvironment();
    const AuthoredReflectionProbeFilteringSettings filteringSettings =
        ReflectionProbeFilteringSettingsFromEnvironment();
    const CapturedReflectionProbeFilteringSettings capturedFilteringSettings =
        CapturedReflectionProbeFilteringSettingsFromEnvironment();
    const bool forceRefresh =
        EnvironmentFlagEnabled("SE_REFLECTION_PROBE_FORCE_REFRESH");
    const bool sceneDirtyOverride =
        EnvironmentFlagEnabled("SE_REFLECTION_PROBE_SCENE_DIRTY");
    ++m_ReflectionCaptureSchedulerFrame;
    std::vector<RendererReflectionProbe> sceneCapturedProbes;

    std::span<const ReflectionProbe3D> sceneProbes =
        m_MainScene3D->ReflectionProbes();
    for (std::size_t index = 0; index < sceneProbes.size(); ++index) {
        const ReflectionProbe3D& probe = sceneProbes[index];
        RendererReflectionProbeCaptureSource captureSource =
            RendererCaptureSource(probe.captureSource);
        RendererReflectionProbeRefreshPolicy refreshPolicy =
            RendererRefreshPolicy(probe.refreshPolicy);
        if (captureSourceOverride.has_value()) {
            captureSource = *captureSourceOverride;
            refreshPolicy = DefaultReflectionProbeRefreshPolicy(captureSource);
        }
        if (refreshPolicyOverride.has_value()) {
            refreshPolicy = *refreshPolicyOverride;
        }
        RendererReflectionProbe rendererProbe = SceneReflectionProbe(
            probe,
            index <= static_cast<std::size_t>(std::numeric_limits<i32>::max())
                ? static_cast<i32>(index)
                : -1
        );
        rendererProbe.captureSource = captureSource;
        rendererProbe.refreshPolicy = refreshPolicy;
        if (captureSource == RendererReflectionProbeCaptureSource::CapturedScene &&
            ReflectionProbeContributes(rendererProbe)) {
            sceneCapturedProbes.push_back(rendererProbe);
        }
        if (captureSource != RendererReflectionProbeCaptureSource::AuthoredCubemap ||
            probe.captureAssetId.empty()) {
            continue;
        }
        if (refreshPolicy == RendererReflectionProbeRefreshPolicy::Static &&
            m_ReflectionProbeResources.AuthoredCubemapReady(
                probe.captureAssetId,
                m_IblSampler
            )) {
            continue;
        }
        m_ReflectionProbeResources.EnsureAuthoredCubemap(
            m_Device,
            m_PhysicalDevice,
            m_CommandPool,
            probe.captureAssetId,
            filteringSettings
        );
    }

    if (sceneCapturedProbes.empty()) {
        return;
    }

    const std::span<const RenderCommand> influenceCommands =
        ReflectionCaptureInfluenceCommands();

    std::sort(
        sceneCapturedProbes.begin(),
        sceneCapturedProbes.end(),
        [](const RendererReflectionProbe& left, const RendererReflectionProbe& right) {
            return left.sceneIndex < right.sceneIndex;
        }
    );
    const bool analyticBackendRequested =
        ReflectionProbeCaptureBackendPreferenceFromEnvironment() ==
        ReflectionProbeCaptureBackendPreference::AnalyticCpu;
    std::vector<RendererReflectionProbe> pendingGpuCaptures;
    pendingGpuCaptures.reserve(sceneCapturedProbes.size());

    for (const RendererReflectionProbe& probe : sceneCapturedProbes) {
        const std::vector<CapturedReflectionProbeLightSample> capturedLightSamples =
            CapturedReflectionProbeLights(lights, probe);
        const CapturedReflectionProbeSceneSample sceneSample =
            CapturedReflectionProbeSceneSampleFor(
                probe,
                lights,
                capturedLightSamples
            );
        const CapturedReflectionProbeGeometrySample geometrySample =
            CapturedReflectionProbeGeometrySampleFor(probe, influenceCommands);
        const CapturedSceneRefreshRequest refreshRequest =
            CapturedSceneRefreshRequestFor(
                *m_MainScene3D,
                probe,
                sceneSample,
                geometrySample,
                probe.refreshPolicy,
                capturedFilteringSettings,
                forceRefresh,
                sceneDirtyOverride,
                m_ReflectionCaptureSchedulerFrame
            );
        if (analyticBackendRequested ||
            !m_ReflectionProbeResources.EnsureGpuCapturedSceneResources(
                m_Device,
                m_PhysicalDevice,
                probe.sceneIndex
            )) {
            m_ReflectionProbeResources.EnsureCapturedSceneCubemap(
                m_Device,
                m_PhysicalDevice,
                m_CommandPool,
                probe.sceneIndex,
                sceneSample,
                capturedLightSamples,
                refreshRequest,
                filteringSettings
            );
            continue;
        }
        if (m_ReflectionProbeResources.RequestGpuCapturedSceneRefresh(
                refreshRequest,
                probe.sceneIndex
            )) {
            pendingGpuCaptures.push_back(probe);
        }
    }

    if (pendingGpuCaptures.empty()) {
        ResetReflectionCaptureShadowSnapshot();
        return;
    }
    if (!EnsureReflectionProbeCapturePipelines()) {
        for (const RendererReflectionProbe& probe : pendingGpuCaptures) {
            m_ReflectionProbeResources.FailGpuCapturedSceneRefresh(probe.sceneIndex);
        }
        ResetReflectionCaptureShadowSnapshot();
        return;
    }

    std::stable_sort(
        pendingGpuCaptures.begin(),
        pendingGpuCaptures.end(),
        [this](const RendererReflectionProbe& left,
               const RendererReflectionProbe& right) {
            const CapturedSceneCaptureAudit& leftAudit =
                m_ReflectionProbeResources.CapturedSceneAudit(left.sceneIndex);
            const CapturedSceneCaptureAudit& rightAudit =
                m_ReflectionProbeResources.CapturedSceneAudit(right.sceneIndex);
            if (leftAudit.refreshPriority == rightAudit.refreshPriority) {
                return left.sceneIndex < right.sceneIndex;
            }
            return leftAudit.refreshPriority > rightAudit.refreshPriority;
        }
    );
    const auto activeCapture = std::find_if(
        pendingGpuCaptures.begin(),
        pendingGpuCaptures.end(),
        [this](const RendererReflectionProbe& probe) {
            return probe.sceneIndex == m_ReflectionCaptureActiveSceneIndex;
        }
    );
    if (activeCapture == pendingGpuCaptures.end()) {
        ResetReflectionCaptureShadowSnapshot();
    }
    const auto nextCapture = activeCapture != pendingGpuCaptures.end()
        ? activeCapture
        : std::find_if(
            pendingGpuCaptures.begin(),
            pendingGpuCaptures.end(),
        [this](const RendererReflectionProbe& probe) {
            return probe.sceneIndex > m_ReflectionCaptureRoundRobinSceneIndex;
        }
    );
    const RendererReflectionProbe& scheduledProbe =
        nextCapture != pendingGpuCaptures.end()
            ? *nextCapture
            : pendingGpuCaptures.front();
    m_ReflectionCaptureRoundRobinSceneIndex = scheduledProbe.sceneIndex;
    m_ReflectionCaptureActiveSceneIndex = scheduledProbe.sceneIndex;
    if (!CaptureNextReflectionProbeFace(imageIndex, lights, scheduledProbe)) {
        m_ReflectionProbeResources.FailGpuCapturedSceneRefresh(
            scheduledProbe.sceneIndex
        );
        ResetReflectionCaptureShadowSnapshot();
        return;
    }
    if (!m_ReflectionProbeResources.GpuCapturedSceneRefreshPending(
            scheduledProbe.sceneIndex
        )) {
        ResetReflectionCaptureShadowSnapshot();
    }
}

u32 VulkanRenderer::UpdateEnvironmentDescriptorSets(
    VulkanDescriptorSets* descriptorSets,
    const FrameReflectionProbeSet* reflectionProbes,
    std::optional<std::size_t> descriptorSetIndex
) const {
    if (descriptorSets == nullptr ||
        m_IblSampler == VK_NULL_HANDLE ||
        m_IblBrdfImage == nullptr ||
        m_IblBrdfImage->View() == VK_NULL_HANDLE ||
        m_IblIrradianceView == VK_NULL_HANDLE ||
        m_IblPrefilteredView == VK_NULL_HANDLE ||
        m_VisibleSkyboxSampler == nullptr) {
        return 0;
    }
    const VulkanTexture2D* visibleSkyboxTexture =
        m_VisibleSkyboxTexture != nullptr
            ? m_VisibleSkyboxTexture.get()
            : m_VisibleSkyboxFallbackTexture.get();
    if (visibleSkyboxTexture == nullptr ||
        visibleSkyboxTexture->View() == VK_NULL_HANDLE) {
        return 0;
    }

    std::array<VkDescriptorImageInfo, kMaxFrameReflectionProbes>
        localReflectionProbeInfos{};
    std::array<VkDescriptorImageInfo, kMaxFrameReflectionProbes>
        localReflectionProbeDiffuseIrradianceInfos{};
    for (std::size_t slot = 0; slot < localReflectionProbeInfos.size(); ++slot) {
        VkImageView slotView = m_IblPrefilteredView;
        VkImageView diffuseIrradianceView = m_IblIrradianceView;
        if (reflectionProbes != nullptr &&
            slot < reflectionProbes->selectedProbeCount) {
            const RendererReflectionProbe& probe =
                reflectionProbes->selectedProbes[slot];
            if (probe.captureSource ==
                RendererReflectionProbeCaptureSource::BuiltInProcedural) {
                slotView = m_ReflectionProbeResources.DescriptorViewFor(
                    m_IblPrefilteredView,
                    m_IblSampler
                );
            } else if (
                probe.captureSource ==
                RendererReflectionProbeCaptureSource::AuthoredCubemap) {
                slotView = m_ReflectionProbeResources.AuthoredDescriptorViewFor(
                    probe.captureAssetId,
                    m_IblPrefilteredView,
                    m_IblSampler
                );
            } else if (
                probe.captureSource ==
                RendererReflectionProbeCaptureSource::CapturedScene) {
                slotView =
                    m_ReflectionProbeResources.CapturedSceneDescriptorViewFor(
                        probe.sceneIndex,
                        m_IblPrefilteredView,
                        m_IblSampler
                    );
                diffuseIrradianceView =
                    m_ReflectionProbeResources
                        .CapturedSceneDiffuseIrradianceDescriptorViewFor(
                            probe.sceneIndex,
                            m_IblIrradianceView,
                            m_IblSampler
                        );
            }
        } else {
            slotView = m_ReflectionProbeResources.DescriptorViewFor(
                m_IblPrefilteredView,
                m_IblSampler
            );
        }

        localReflectionProbeInfos[slot].imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        localReflectionProbeInfos[slot].imageView = slotView;
        localReflectionProbeInfos[slot].sampler = m_IblSampler;
        localReflectionProbeDiffuseIrradianceInfos[slot].imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        localReflectionProbeDiffuseIrradianceInfos[slot].imageView =
            diffuseIrradianceView;
        localReflectionProbeDiffuseIrradianceInfos[slot].sampler = m_IblSampler;
    }
    const std::size_t descriptorSetCount = descriptorSets->Count();
    if (descriptorSetIndex.has_value() &&
        *descriptorSetIndex >= descriptorSetCount) {
        return 0;
    }

    const std::size_t firstDescriptorSet =
        descriptorSetIndex.value_or(0u);
    const std::size_t endDescriptorSet =
        descriptorSetIndex.has_value()
            ? firstDescriptorSet + 1u
            : descriptorSetCount;

    u32 localProbeDescriptorWrites = 0;
    for (std::size_t index = firstDescriptorSet; index < endDescriptorSet; ++index) {
        VkDescriptorImageInfo brdfInfo{};
        brdfInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        brdfInfo.imageView = m_IblBrdfImage->View();
        brdfInfo.sampler = m_IblSampler;

        VkDescriptorImageInfo irradianceInfo{};
        irradianceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irradianceInfo.imageView = m_IblIrradianceView;
        irradianceInfo.sampler = m_IblSampler;

        VkDescriptorImageInfo prefilteredInfo{};
        prefilteredInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        prefilteredInfo.imageView = m_IblPrefilteredView;
        prefilteredInfo.sampler = m_IblSampler;

        VkDescriptorImageInfo visibleSkyboxInfo{};
        visibleSkyboxInfo.imageLayout = visibleSkyboxTexture->Layout();
        visibleSkyboxInfo.imageView = visibleSkyboxTexture->View();
        visibleSkyboxInfo.sampler = m_VisibleSkyboxSampler->Handle();

        std::array<VkWriteDescriptorSet, 6> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets->Handle(index);
        descriptorWrites[0].dstBinding = 6;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &brdfInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets->Handle(index);
        descriptorWrites[1].dstBinding = 7;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &irradianceInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSets->Handle(index);
        descriptorWrites[2].dstBinding = 8;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &prefilteredInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSets->Handle(index);
        descriptorWrites[3].dstBinding = 11;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[3].descriptorCount =
            static_cast<u32>(localReflectionProbeInfos.size());
        descriptorWrites[3].pImageInfo = localReflectionProbeInfos.data();

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = descriptorSets->Handle(index);
        descriptorWrites[4].dstBinding = 12;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pImageInfo = &visibleSkyboxInfo;

        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = descriptorSets->Handle(index);
        descriptorWrites[5].dstBinding = 13;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5].descriptorCount = static_cast<u32>(
            localReflectionProbeDiffuseIrradianceInfos.size()
        );
        descriptorWrites[5].pImageInfo =
            localReflectionProbeDiffuseIrradianceInfos.data();

        vkUpdateDescriptorSets(
            m_Device.Handle(),
            static_cast<u32>(descriptorWrites.size()),
            descriptorWrites.data(),
            0,
            nullptr
        );
        if (reflectionProbes != nullptr) {
            if (reflectionProbes->selectedCubemapSamplingCount > 0u) {
                ++localProbeDescriptorWrites;
            }
        } else if (LocalReflectionProbeCubemapReady()) {
            ++localProbeDescriptorWrites;
        }
    }

    return localProbeDescriptorWrites;
}

std::span<const RenderCommand> VulkanRenderer::ShadowRenderCommands() const {
    return m_ShadowRenderQueue.Commands();
}

const VulkanDescriptorSets* VulkanRenderer::ShadowDescriptorSets() const {
    if (m_PipelineSpec.vertexLayout == VertexLayout::Vertex3D) {
        return m_DescriptorSets.get();
    }

    return nullptr;
}

bool VulkanRenderer::BuildMainInstanceBatches(
    std::span<const RenderCommand> commands,
    bool allowCacheReuse
) {
    if (allowCacheReuse && m_MainInstanceBatchesCacheValid) {
        return true;
    }

    m_MainInstances.clear();
    m_MainInstanceBatches.clear();
    m_MainInstanceSignature = 0x2fcd2adf1379a1b9ull;
    if (m_InstancedGraphicsPipeline == nullptr || m_InstanceBuffer == nullptr) {
        m_MainInstanceBatchesCacheValid = true;
        return false;
    }

    std::size_t commandIndex = 0;
    while (commandIndex < commands.size()) {
        const RenderCommand& firstCommand = commands[commandIndex];
        std::size_t endIndex = commandIndex + 1;
        while (endIndex < commands.size()) {
            const RenderCommand& candidate = commands[endIndex];
            if (candidate.mesh != firstCommand.mesh ||
                candidate.material != firstCommand.material ||
                candidate.drawOrder != firstCommand.drawOrder ||
                candidate.tint != firstCommand.tint) {
                break;
            }
            ++endIndex;
        }

        const std::size_t commandCount = endIndex - commandIndex;
        if (commandCount > 1) {
            RenderInstanceBatch batch{};
            batch.firstCommandIndex = commandIndex;
            batch.commandCount = static_cast<u32>(commandCount);
            batch.firstInstance = static_cast<u32>(m_MainInstances.size());
            m_MainInstanceBatches.push_back(batch);

            for (std::size_t index = commandIndex; index < endIndex; ++index) {
                m_MainInstances.push_back(Instance3D{ commands[index].model });
                m_MainInstanceSignature = HashMatrix(
                    m_MainInstanceSignature,
                    commands[index].model
                );
            }
        }

        commandIndex = endIndex;
    }

    m_MainInstanceSignature = HashCombine(
        m_MainInstanceSignature,
        static_cast<u64>(m_MainInstances.size())
    );
    m_MainInstanceSignature = HashCombine(
        m_MainInstanceSignature,
        static_cast<u64>(m_MainInstanceBatches.size())
    );
    m_MainInstanceBatchesCacheValid = true;
    return false;
}

bool VulkanRenderer::UploadMainInstancesIfNeeded(std::size_t imageIndex) {
    if (m_InstanceBuffer == nullptr) {
        return false;
    }

    if (m_MainInstanceUploadSignatures.size() <= imageIndex) {
        m_MainInstanceUploadSignatures.resize(imageIndex + 1, 0);
    }

    if (m_MainInstanceUploadSignatures[imageIndex] == m_MainInstanceSignature) {
        return false;
    }

    m_InstanceBuffer->Update(
        m_Device,
        m_PhysicalDevice,
        imageIndex,
        m_MainInstances
    );
    m_MainInstanceUploadSignatures[imageIndex] = m_MainInstanceSignature;
    return true;
}

}
