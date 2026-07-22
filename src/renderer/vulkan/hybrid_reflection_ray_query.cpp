#include "renderer/vulkan/hybrid_reflection_ray_query.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/compute_pipeline.h"
#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/fidelityfx_sssr_adapter.h"
#include "renderer/vulkan/hybrid_reflection_acceleration_structures.h"
#include "renderer/vulkan/image.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/pipeline_spec.h"
#include "renderer/vulkan/render_targets.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/sampler.h"
#include "renderer/vulkan/texture_2d.h"
#include "renderer/vulkan/uniform_buffer.h"
#include "renderer/vulkan/vertex.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <cmath>
#include <span>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace se {

namespace {

constexpr u32 kRayQueryContractVersion = 3u;
constexpr u32 kHitAttributeContractVersion = 1u;
constexpr u32 kMaterialTableContractVersion = 1u;
constexpr u32 kHitLightingContractVersion = 1u;
constexpr u32 kShadowVisibilityContractVersion = 1u;
constexpr u32 kDenoiserBridgeContractVersion = 1u;
constexpr u32 kHitLightingVisibilityModeUnshadowed = 1u;
constexpr u32 kHitLightingVisibilityModeRayQuery = 2u;
constexpr u32 kHitLightingVisibilityFallbackPendingRayQuery = 1u;
constexpr u32 kMaxShadowedLocalLights = 8u;
constexpr u32 kMaxRectangleShadowSamples = 4u;
constexpr u32 kDiagnosticValueCount = 86u;
constexpr u32 kRayQueryRuntimeForceAllRaysBit = 1u << 0u;
constexpr u32 kRayQueryRuntimeDisableBackFaceCullBit = 1u << 1u;
constexpr u32 kRayQueryRuntimeTargetAttributionBit = 1u << 2u;
constexpr u32 kRayQueryRuntimeFullAuditBit = 1u << 3u;
constexpr u32 kFullAuditHeaderWordCount = 128u;
constexpr u32 kFullAuditInstanceCounterCount = 8u;
constexpr u32 kFullAuditRayRecordWordCount = 24u;
constexpr u32 kFullAuditInstanceCounterBase = kFullAuditHeaderWordCount;
constexpr u32 kFullAuditRayRecordBase = kFullAuditInstanceCounterBase +
    kMaxHybridReflectionInstances * kFullAuditInstanceCounterCount;
constexpr u32 kFullAuditApplyRecordCountIndex = 86u;
constexpr u32 kFullAuditApplyRecordWordCount = 16u;
constexpr u32 kFullAuditApplyRecordBase = kFullAuditRayRecordBase +
    kHybridReflectionFullAuditMaxRayRecords * kFullAuditRayRecordWordCount;
constexpr u32 kFullAuditWordCount = kFullAuditApplyRecordBase +
    kHybridReflectionFullAuditMaxApplyRecords *
        kFullAuditApplyRecordWordCount;
constexpr u32 kHitDistanceMinDiagnosticIndex = 7u;
constexpr u32 kNormalLengthMinDiagnosticIndex = 20u;
constexpr u32 kBarycentricSumMinDiagnosticIndex = 25u;
constexpr u32 kSampleLodMinDiagnosticIndex = 33u;
constexpr u32 kHitSurfaceLuminanceMinDiagnosticIndex = 37u;
constexpr u32 kRadianceLuminanceMinDiagnosticIndex = 56u;
constexpr u32 kShadowHitDistanceMinDiagnosticIndex = 74u;
constexpr u32 kShadowVisibilityMinDiagnosticIndex = 76u;
constexpr VkFormat kRayQueryResultFormat = VK_FORMAT_R32G32_UINT;
constexpr VkFormat kHitSurfaceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

struct alignas(16) HybridReflectionMaterialRecord {
    glm::vec4 baseColorFactor{ 1.0f };
    glm::vec4 emissiveFactor{ 0.0f };
    // x: texture mix, y: metallic, z: roughness, w: alpha.
    glm::vec4 surfaceControls{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 uvTransform{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 uvControls{ 0.0f };
    // x: albedo texture index, y: sampler index, z: mip count, w: ready.
    glm::uvec4 textureInfo{ 0u };
    // x/y: texture dimensions, z/w reserved.
    glm::uvec4 textureExtent{ 1u, 1u, 0u, 0u };
};

static_assert(kMaxHybridReflectionMaterials == kMaxFrameMaterials);
static_assert(sizeof(HybridReflectionMaterialRecord) == 112u);
static_assert(offsetof(HybridReflectionMaterialRecord, baseColorFactor) == 0u);
static_assert(offsetof(HybridReflectionMaterialRecord, emissiveFactor) == 16u);
static_assert(offsetof(HybridReflectionMaterialRecord, surfaceControls) == 32u);
static_assert(offsetof(HybridReflectionMaterialRecord, uvTransform) == 48u);
static_assert(offsetof(HybridReflectionMaterialRecord, uvControls) == 64u);
static_assert(offsetof(HybridReflectionMaterialRecord, textureInfo) == 80u);
static_assert(offsetof(HybridReflectionMaterialRecord, textureExtent) == 96u);

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
    u32 materialTableCount = 0u;
    u32 materialTableCapacity = kMaxHybridReflectionMaterials;
    u32 materialTexturesEnabled = 0u;
    u32 materialTableContractVersion = kMaterialTableContractVersion;
    u32 hitLightingEnabled = 0u;
    u32 hitLightingContractVersion = kHitLightingContractVersion;
    u32 directionalLightCount = 0u;
    u32 localLightCount = 0u;
    u32 iblPrefilteredMipCount = 0u;
    u32 hitLightingVisibilityMode = 0u;
    u32 iblResourcesReady = 0u;
    u32 reserved = 0u;
    u32 shadowVisibilityEnabled = 0u;
    u32 shadowVisibilityContractVersion = kShadowVisibilityContractVersion;
    u32 maxShadowedLocalLights = kMaxShadowedLocalLights;
    u32 rectangleShadowSampleCount = kMaxRectangleShadowSamples;
    u32 denoiserInjectionEnabled = 0u;
    u32 denoiserBridgeContractVersion = kDenoiserBridgeContractVersion;
    u32 diagnosticTargetInstanceIndex = 0u;
    u32 runtimeFlags = 0u;
};

static_assert(sizeof(RayQueryControls) == 128u);
static_assert(offsetof(RayQueryControls, enabled) == 20u);
static_assert(offsetof(RayQueryControls, contractVersion) == 24u);
static_assert(offsetof(RayQueryControls, diagnosticsEnabled) == 28u);
static_assert(offsetof(RayQueryControls, instanceMetadataCount) == 32u);
static_assert(offsetof(RayQueryControls, instanceMaterialCount) == 36u);
static_assert(offsetof(RayQueryControls, expectedVertexStride) == 40u);
static_assert(offsetof(RayQueryControls, hitAttributesEnabled) == 44u);
static_assert(offsetof(RayQueryControls, materialTableCount) == 48u);
static_assert(offsetof(RayQueryControls, materialTexturesEnabled) == 56u);
static_assert(offsetof(RayQueryControls, materialTableContractVersion) == 60u);
static_assert(offsetof(RayQueryControls, hitLightingEnabled) == 64u);
static_assert(offsetof(RayQueryControls, hitLightingContractVersion) == 68u);
static_assert(offsetof(RayQueryControls, directionalLightCount) == 72u);
static_assert(offsetof(RayQueryControls, localLightCount) == 76u);
static_assert(offsetof(RayQueryControls, iblPrefilteredMipCount) == 80u);
static_assert(offsetof(RayQueryControls, hitLightingVisibilityMode) == 84u);
static_assert(offsetof(RayQueryControls, iblResourcesReady) == 88u);
static_assert(offsetof(RayQueryControls, shadowVisibilityEnabled) == 96u);
static_assert(offsetof(RayQueryControls, shadowVisibilityContractVersion) == 100u);
static_assert(offsetof(RayQueryControls, maxShadowedLocalLights) == 104u);
static_assert(offsetof(RayQueryControls, rectangleShadowSampleCount) == 108u);
static_assert(offsetof(RayQueryControls, denoiserInjectionEnabled) == 112u);
static_assert(offsetof(RayQueryControls, denoiserBridgeContractVersion) == 116u);
static_assert(offsetof(RayQueryControls, diagnosticTargetInstanceIndex) == 120u);
static_assert(offsetof(RayQueryControls, runtimeFlags) == 124u);
static_assert(offsetof(Vertex3D, position) == 0u);
static_assert(offsetof(Vertex3D, normal) == 12u);
static_assert(offsetof(Vertex3D, texCoord) == 36u);

bool ExtentsDiffer(VkExtent2D lhs, VkExtent2D rhs) {
    return lhs.width != rhs.width || lhs.height != rhs.height;
}

HybridReflectionMaterialRecord BuildMaterialRecord(
    const VulkanMaterial& material,
    u32 descriptorIndex
) {
    const MaterialProperties& properties = material.Properties();
    const VulkanTexture2D& texture = material.AlbedoTexture();
    const VkExtent2D textureExtent = texture.Extent();

    HybridReflectionMaterialRecord record{};
    record.baseColorFactor = glm::vec4(
        properties.baseColorFactor[0],
        properties.baseColorFactor[1],
        properties.baseColorFactor[2],
        properties.baseColorFactor[3]
    );
    record.emissiveFactor = glm::vec4(
        properties.emissiveFactor[0],
        properties.emissiveFactor[1],
        properties.emissiveFactor[2],
        0.0f
    );
    record.surfaceControls = glm::vec4(
        std::clamp(properties.textureMix, 0.0f, 1.0f),
        std::clamp(properties.cameraControls[0], 0.0f, 1.0f),
        std::clamp(properties.cameraControls[1], 0.04f, 1.0f),
        std::clamp(properties.baseColorFactor[3], 0.0f, 1.0f)
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
        properties.uvControls[2],
        properties.uvControls[3]
    );
    record.textureInfo = glm::uvec4(
        descriptorIndex,
        descriptorIndex,
        std::max(texture.MipLevels(), 1u),
        1u
    );
    record.textureExtent = glm::uvec4(
        std::max(textureExtent.width, 1u),
        std::max(textureExtent.height, 1u),
        0u,
        0u
    );
    return record;
}

u32 VulkanEnvironmentU32OrDefault(const char* name, u32 fallback) {
    const std::string value = ReadVulkanEnvironmentString(name);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return fallback;
    }
    return static_cast<u32>(std::min<unsigned long>(
        parsed,
        std::numeric_limits<u32>::max()
    ));
}

std::vector<u32> BuildDiagnosticClearValues(u32 wordCount) {
    std::vector<u32> diagnostics(wordCount, 0u);
    diagnostics[kHitDistanceMinDiagnosticIndex] =
        std::numeric_limits<u32>::max();
    diagnostics[kNormalLengthMinDiagnosticIndex] =
        std::numeric_limits<u32>::max();
    diagnostics[kBarycentricSumMinDiagnosticIndex] =
        std::numeric_limits<u32>::max();
    diagnostics[kSampleLodMinDiagnosticIndex] =
        std::numeric_limits<u32>::max();
    diagnostics[kHitSurfaceLuminanceMinDiagnosticIndex] =
        std::numeric_limits<u32>::max();
    diagnostics[kRadianceLuminanceMinDiagnosticIndex] =
        std::numeric_limits<u32>::max();
    diagnostics[kShadowHitDistanceMinDiagnosticIndex] =
        std::numeric_limits<u32>::max();
    diagnostics[kShadowVisibilityMinDiagnosticIndex] =
        std::numeric_limits<u32>::max();
    return diagnostics;
}

std::string CsvEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 2u);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

const char* FullAuditImageStageName(
    HybridReflectionFullAuditImageStage stage
) {
    switch (stage) {
    case HybridReflectionFullAuditImageStage::IntersectRadiance:
        return "dnsr_intersect_radiance";
    case HybridReflectionFullAuditImageStage::IntersectConfidence:
        return "dnsr_intersect_confidence";
    case HybridReflectionFullAuditImageStage::ReprojectRadiance:
        return "dnsr_reproject_radiance";
    case HybridReflectionFullAuditImageStage::ReprojectConfidence:
        return "dnsr_reproject_confidence";
    case HybridReflectionFullAuditImageStage::PrefilterRadiance:
        return "dnsr_prefilter_radiance";
    case HybridReflectionFullAuditImageStage::PrefilterVariance:
        return "dnsr_prefilter_variance";
    case HybridReflectionFullAuditImageStage::PrefilterSampleCount:
        return "dnsr_prefilter_sample_count";
    case HybridReflectionFullAuditImageStage::ResolveRadiance:
        return "dnsr_resolve_radiance";
    case HybridReflectionFullAuditImageStage::ResolveConfidence:
        return "dnsr_resolve_confidence";
    case HybridReflectionFullAuditImageStage::DeferredHdrBeforeApply:
        return "deferred_hdr_before_apply";
    case HybridReflectionFullAuditImageStage::HdrAfterApply:
        return "hdr_after_apply";
    case HybridReflectionFullAuditImageStage::TemporalInput:
        return "taa_or_dlss_input";
    case HybridReflectionFullAuditImageStage::TemporalUpscaleOutput:
        return "temporal_upscale_output";
    case HybridReflectionFullAuditImageStage::FinalOutputBeforePresent:
        return "final_output_before_present";
    }
    return "unknown";
}

VkDeviceSize FullAuditFormatPixelBytes(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16u;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8u;
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        return 4u;
    default:
        return 0u;
    }
}

f32 HalfToFloat(u16 value) {
    const u32 sign = (static_cast<u32>(value) & 0x8000u) << 16u;
    const u32 exponent = (static_cast<u32>(value) >> 10u) & 0x1fu;
    const u32 mantissa = static_cast<u32>(value) & 0x3ffu;
    u32 bits = sign;
    if (exponent == 0u) {
        if (mantissa != 0u) {
            u32 normalizedMantissa = mantissa;
            u32 normalizedExponent = 0u;
            while ((normalizedMantissa & 0x400u) == 0u) {
                normalizedMantissa <<= 1u;
                ++normalizedExponent;
            }
            bits |= (127u - 14u - normalizedExponent) << 23u;
            bits |= (normalizedMantissa & 0x3ffu) << 13u;
        }
    } else if (exponent == 0x1fu) {
        bits |= 0x7f800000u | (mantissa << 13u);
    } else {
        bits |= (exponent + (127u - 15u)) << 23u;
        bits |= mantissa << 13u;
    }
    return std::bit_cast<f32>(bits);
}

void RecordFullAuditImageBarrier(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0u;
    barrier.subresourceRange.levelCount = 1u;
    barrier.subresourceRange.baseArrayLayer = 0u;
    barrier.subresourceRange.layerCount = 1u;
    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0u,
        0u,
        nullptr,
        0u,
        nullptr,
        1u,
        &barrier
    );
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
        const VulkanLightBuffer& lightBuffer,
        VkImageView iblBrdfView,
        VkImageView iblIrradianceView,
        VkImageView iblPrefilteredView,
        VkSampler iblSampler,
        u32 iblPrefilteredMipCount,
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
            lightBuffer.Count() != count ||
            ExtentsDiffer(classifyResources.Extent(), extent) ||
            ExtentsDiffer(depthPyramid.Extent(), extent)) {
            throw std::runtime_error(
                "Hybrid reflection Ray Query producer resources do not match"
            );
        }
        if (iblBrdfView == VK_NULL_HANDLE ||
            iblIrradianceView == VK_NULL_HANDLE ||
            iblPrefilteredView == VK_NULL_HANDLE ||
            iblSampler == VK_NULL_HANDLE ||
            iblPrefilteredMipCount == 0u) {
            throw std::runtime_error(
                "Hybrid reflection Ray Query IBL resources are incomplete"
            );
        }
        this->iblPrefilteredMipCount = iblPrefilteredMipCount;
#if !defined(NDEBUG)
        fullAuditResourcesAllocated = VulkanEnvironmentFlagEnabled(
            "SE_HYBRID_REFLECTIONS_FULL_AUDIT"
        );
        fullAuditVerbosePixelCsv = VulkanEnvironmentFlagEnabled(
            "SE_HYBRID_REFLECTIONS_FULL_AUDIT_VERBOSE_PIXEL_CSV"
        );
        fullAuditRawEvidence = fullAuditVerbosePixelCsv ||
            VulkanEnvironmentFlagEnabled(
                "SE_HYBRID_REFLECTIONS_FULL_AUDIT_RAW_EVIDENCE"
            );
#else
        fullAuditResourcesAllocated = false;
        fullAuditVerbosePixelCsv = false;
        fullAuditRawEvidence = false;
#endif
        diagnosticWordCount = fullAuditResourcesAllocated
            ? kFullAuditWordCount
            : kDiagnosticValueCount;
        fullAuditMaxCapturedFrames = std::clamp(
            VulkanEnvironmentU32OrDefault(
                "SE_HYBRID_REFLECTIONS_FULL_AUDIT_CAPTURE_FRAMES",
                4u
            ),
            1u,
            120u
        );
        fullAuditOutputDirectory = ReadVulkanEnvironmentString(
            "SE_HYBRID_REFLECTIONS_FULL_AUDIT_DIR"
        );
        if (fullAuditOutputDirectory.empty()) {
            fullAuditOutputDirectory =
                "tmp/hybrid_reflection_full_audit";
        }

        CreateDescriptorSetLayout(device);
        CreateResources(
            device,
            physicalDevice,
            commandPool,
            classifyResources,
            prepareResources,
            blueNoiseResources,
            renderTargets,
            lightBuffer,
            iblBrdfView,
            iblIrradianceView,
            iblPrefilteredView,
            iblSampler
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
        fullAuditImageReadbackBuffers = {};
        fullAuditObjectIdReadbackBuffers.clear();
        fullAuditMetadata.clear();
        instanceMetadataBuffers.clear();
        materialBuffers.clear();
        controlsBuffers.clear();
        hitSurfaceImages.clear();
        resultImages.clear();
        fallbackSampler.reset();
        fallbackTexture.reset();
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
        std::array<VkDescriptorSetLayoutBinding, 24> bindings{};
        auto setBinding = [&](u32 binding, VkDescriptorType type, u32 count = 1u) {
            bindings[binding].binding = binding;
            bindings[binding].descriptorType = type;
            bindings[binding].descriptorCount = count;
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
        setBinding(12u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        setBinding(
            13u,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            kMaxHybridReflectionMaterials
        );
        setBinding(
            14u,
            VK_DESCRIPTOR_TYPE_SAMPLER,
            kMaxHybridReflectionMaterials
        );
        setBinding(15u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        setBinding(16u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        for (u32 binding = 17u; binding <= 19u; ++binding) {
            setBinding(binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        }
        setBinding(20u, VK_DESCRIPTOR_TYPE_SAMPLER);
        setBinding(21u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        setBinding(22u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        setBinding(23u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

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
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanLightBuffer& lightBuffer,
        VkImageView iblBrdfView,
        VkImageView iblIrradianceView,
        VkImageView iblPrefilteredView,
        VkSampler iblSampler
    ) {
        const std::size_t count = renderTargets.Count();
        constexpr VkMemoryPropertyFlags hostMemory =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        constexpr std::array<u8, 4> fallbackPixels{ 255u, 255u, 255u, 255u };
        fallbackTexture = std::make_unique<VulkanTexture2D>(
            device,
            physicalDevice,
            commandPool,
            VulkanTexturePixels{
                std::span<const u8>(fallbackPixels),
                1u,
                1u
            },
            false,
            false
        );
        fallbackSampler = std::make_unique<VulkanSampler>(
            device,
            physicalDevice,
            1u
        );
        resultImages.reserve(count);
        hitSurfaceImages.reserve(count);
        controlsBuffers.reserve(count);
        diagnosticsBuffers.reserve(count);
        instanceMetadataBuffers.reserve(count);
        materialBuffers.reserve(count);
        submitted.assign(count, false);
        frameEnabled.assign(count, false);
        denoiserInjectionActive.assign(count, false);
        diagnosticsActive.assign(count, false);
        fullAuditActive.assign(count, false);
        fullAuditImageRecorded = {};
        tlasDescriptorReady.assign(count, false);
        fullAuditObjectIdRecorded.assign(count, false);
        fullAuditMetadata.resize(count);
        for (auto& recorded : fullAuditImageRecorded) {
            recorded.assign(count, false);
        }
        boundMaterialTextureViews.resize(count);
        boundMaterialSamplers.resize(count);

        const std::vector<u32> diagnostics =
            BuildDiagnosticClearValues(diagnosticWordCount);
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
            auto hitSurface = std::make_unique<VulkanImage>(
                device,
                physicalDevice,
                extent,
                kHitSurfaceFormat,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT
            );
            hitSurface->TransitionLayout(
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
                static_cast<VkDeviceSize>(diagnostics.size()) * sizeof(u32),
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
            auto materialBuffer = std::make_unique<VulkanBuffer>(
                device,
                physicalDevice,
                sizeof(HybridReflectionMaterialRecord) *
                    kMaxHybridReflectionMaterials,
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
            hitSurfaceImages.push_back(std::move(hitSurface));
            controlsBuffers.push_back(std::move(controls));
            diagnosticsBuffers.push_back(std::move(diagnosticBuffer));
            instanceMetadataBuffers.push_back(
                std::move(instanceMetadataBuffer)
            );
            materialBuffers.push_back(std::move(materialBuffer));
        }

        if (fullAuditResourcesAllocated) {
            const VkExtent2D displayExtent = renderTargets.DisplayExtent();
            const std::array<VkExtent2D, kHybridReflectionFullAuditImageStageCount>
                snapshotExtents{
                    extent,
                    extent,
                    extent,
                    extent,
                    extent,
                    extent,
                    extent,
                    extent,
                    extent,
                    extent,
                    extent,
                    extent,
                    displayExtent,
                    displayExtent
                };
            const std::array<VkFormat, kHybridReflectionFullAuditImageStageCount>
                snapshotFormats{
                    VK_FORMAT_R32G32B32A32_SFLOAT,
                    VK_FORMAT_R32_SFLOAT,
                    VK_FORMAT_R32G32B32A32_SFLOAT,
                    VK_FORMAT_R32_SFLOAT,
                    VK_FORMAT_R32G32B32A32_SFLOAT,
                    VK_FORMAT_R32_SFLOAT,
                    VK_FORMAT_R32_SFLOAT,
                    VK_FORMAT_R32G32B32A32_SFLOAT,
                    VK_FORMAT_R32_SFLOAT,
                    renderTargets.HdrSceneColorFormat(),
                    renderTargets.HdrSceneColorFormat(),
                    renderTargets.HdrSceneColorFormat(),
                    renderTargets.TemporalUpscaleOutputFormat(),
                    VK_FORMAT_B8G8R8A8_SRGB
                };
            for (u32 stageIndex = 0u;
                 stageIndex < kHybridReflectionFullAuditImageStageCount;
                 ++stageIndex) {
                fullAuditImageReadbackBuffers[stageIndex].reserve(count);
                for (std::size_t imageIndex = 0u;
                     imageIndex < count;
                     ++imageIndex) {
                    const VkDeviceSize pixelBytes =
                        FullAuditFormatPixelBytes(snapshotFormats[stageIndex]);
                    if (pixelBytes == 0u) {
                        throw std::runtime_error(
                            "Unsupported hybrid reflection full-audit snapshot format"
                        );
                    }
                    const VkDeviceSize bytes = static_cast<VkDeviceSize>(
                        snapshotExtents[stageIndex].width
                    ) * static_cast<VkDeviceSize>(
                        snapshotExtents[stageIndex].height
                    ) * pixelBytes;
                    fullAuditImageReadbackBuffers[stageIndex].push_back(
                        std::make_unique<VulkanBuffer>(
                            device,
                            physicalDevice,
                            std::max<VkDeviceSize>(bytes, 4u),
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            hostMemory
                        )
                    );
                }
                fullAuditImageExtents[stageIndex] = snapshotExtents[stageIndex];
                fullAuditImageFormats[stageIndex] = snapshotFormats[stageIndex];
            }
            fullAuditObjectIdReadbackBuffers.reserve(count);
            for (std::size_t imageIndex = 0u;
                 imageIndex < count;
                 ++imageIndex) {
                const VkDeviceSize bytes = static_cast<VkDeviceSize>(
                    extent.width
                ) * static_cast<VkDeviceSize>(extent.height) * sizeof(u32);
                fullAuditObjectIdReadbackBuffers.push_back(
                    std::make_unique<VulkanBuffer>(
                        device,
                        physicalDevice,
                        std::max<VkDeviceSize>(bytes, 4u),
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        hostMemory
                    )
                );
            }
        }

        std::array<VkDescriptorPoolSize, 8> poolSizes{};
        poolSizes[0] = {
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            static_cast<u32>(count)
        };
        poolSizes[1] = {
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            static_cast<u32>(
                count * (9u + kMaxHybridReflectionMaterials)
            )
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
            static_cast<u32>(count * 4u)
        };
        poolSizes[5] = {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            static_cast<u32>(count)
        };
        poolSizes[6] = {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            static_cast<u32>(count * 4u)
        };
        poolSizes[7] = {
            VK_DESCRIPTOR_TYPE_SAMPLER,
            static_cast<u32>(count * (kMaxHybridReflectionMaterials + 1u))
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
            VkDescriptorImageInfo hitSurfaceImage{
                VK_NULL_HANDLE,
                hitSurfaceImages[imageIndex]->View(),
                VK_IMAGE_LAYOUT_GENERAL
            };
            VkDescriptorImageInfo denoiserRadianceImage{
                VK_NULL_HANDLE,
                classify.IntersectionOutputView(imageIndex),
                VK_IMAGE_LAYOUT_GENERAL
            };
            VkDescriptorImageInfo denoiserConfidenceImage{
                VK_NULL_HANDLE,
                classify.HitConfidenceView(imageIndex),
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
            VkDescriptorBufferInfo materialInfo{
                materialBuffers[imageIndex]->Handle(),
                0u,
                VK_WHOLE_SIZE
            };
            const VkDescriptorBufferInfo lightInfo =
                lightBuffer.DescriptorInfo(imageIndex);
            const std::array<VkDescriptorImageInfo, 3> iblImageInfos{
                VkDescriptorImageInfo{
                    VK_NULL_HANDLE,
                    iblBrdfView,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                },
                VkDescriptorImageInfo{
                    VK_NULL_HANDLE,
                    iblIrradianceView,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                },
                VkDescriptorImageInfo{
                    VK_NULL_HANDLE,
                    iblPrefilteredView,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            };
            const VkDescriptorImageInfo iblSamplerInfo{
                iblSampler,
                VK_NULL_HANDLE,
                VK_IMAGE_LAYOUT_UNDEFINED
            };
            std::array<
                VkDescriptorImageInfo,
                kMaxHybridReflectionMaterials
            > materialTextureInfos{};
            std::array<
                VkDescriptorImageInfo,
                kMaxHybridReflectionMaterials
            > materialSamplerInfos{};
            for (u32 slot = 0u; slot < kMaxHybridReflectionMaterials; ++slot) {
                materialTextureInfos[slot] = {
                    VK_NULL_HANDLE,
                    fallbackTexture->View(),
                    fallbackTexture->Layout()
                };
                materialSamplerInfos[slot] = {
                    fallbackSampler->Handle(),
                    VK_NULL_HANDLE,
                    VK_IMAGE_LAYOUT_UNDEFINED
                };
                boundMaterialTextureViews[imageIndex][slot] =
                    fallbackTexture->View();
                boundMaterialSamplers[imageIndex][slot] =
                    fallbackSampler->Handle();
            }

            std::array<VkWriteDescriptorSet, 23> writes{};
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
            writes[11] = writes[10];
            writes[11].dstBinding = 12u;
            writes[11].pBufferInfo = &materialInfo;
            writes[12].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[12].dstSet = descriptorSets[imageIndex];
            writes[12].dstBinding = 13u;
            writes[12].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[12].descriptorCount = kMaxHybridReflectionMaterials;
            writes[12].pImageInfo = materialTextureInfos.data();
            writes[13] = writes[12];
            writes[13].dstBinding = 14u;
            writes[13].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writes[13].pImageInfo = materialSamplerInfos.data();
            writes[14].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[14].dstSet = descriptorSets[imageIndex];
            writes[14].dstBinding = 15u;
            writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[14].descriptorCount = 1u;
            writes[14].pImageInfo = &hitSurfaceImage;
            writes[15] = writes[11];
            writes[15].dstBinding = 16u;
            writes[15].pBufferInfo = &lightInfo;
            for (u32 sourceIndex = 0u; sourceIndex < iblImageInfos.size();
                ++sourceIndex) {
                writes[16u + sourceIndex].sType =
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[16u + sourceIndex].dstSet = descriptorSets[imageIndex];
                writes[16u + sourceIndex].dstBinding = 17u + sourceIndex;
                writes[16u + sourceIndex].descriptorType =
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                writes[16u + sourceIndex].descriptorCount = 1u;
                writes[16u + sourceIndex].pImageInfo =
                    &iblImageInfos[sourceIndex];
            }
            writes[19].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[19].dstSet = descriptorSets[imageIndex];
            writes[19].dstBinding = 20u;
            writes[19].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writes[19].descriptorCount = 1u;
            writes[19].pImageInfo = &iblSamplerInfo;
            writes[20] = writes[14];
            writes[20].dstBinding = 21u;
            writes[20].pImageInfo = &denoiserRadianceImage;
            writes[21] = writes[14];
            writes[21].dstBinding = 22u;
            writes[21].pImageInfo = &denoiserConfidenceImage;
            VkDescriptorImageInfo objectIdInfo{};
            if (fullAuditResourcesAllocated) {
                if (!renderTargets.ReflectionAuditObjectIdEnabled()) {
                    throw std::runtime_error(
                        "Full reflection audit requires the GBuffer object ID target"
                    );
                }
                objectIdInfo.imageView =
                    renderTargets.ReflectionAuditObjectIdView(imageIndex);
                objectIdInfo.imageLayout =
                    VK_IMAGE_LAYOUT_GENERAL;
                writes[22].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[22].dstSet = descriptorSets[imageIndex];
                writes[22].dstBinding = 23u;
                writes[22].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                writes[22].descriptorCount = 1u;
                writes[22].pImageInfo = &objectIdInfo;
            }
            vkUpdateDescriptorSets(
                device.Handle(),
                fullAuditResourcesAllocated
                    ? static_cast<u32>(writes.size())
                    : static_cast<u32>(writes.size() - 1u),
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
        std::span<const VulkanMaterial* const> instanceMaterials,
        bool enabled,
        bool hitAttributesEnabled,
        bool materialTexturesEnabled,
        bool hitLightingEnabled,
        bool shadowVisibilityEnabled,
        bool denoiserInjectionEnabled,
        bool diagnosticsEnabled,
        u32 directionalLightCount,
        u32 localLightCount,
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

        const u32 sourceMaterialCount = static_cast<u32>(
            instanceMaterials.size()
        );
        const u32 materialCount = std::min(
            sourceMaterialCount,
            kMaxHybridReflectionMaterials
        );
        std::array<
            HybridReflectionMaterialRecord,
            kMaxHybridReflectionMaterials
        > materialRecords{};
        std::array<
            VkDescriptorImageInfo,
            kMaxHybridReflectionMaterials
        > materialTextureInfos{};
        std::array<
            VkDescriptorImageInfo,
            kMaxHybridReflectionMaterials
        > materialSamplerInfos{};
        std::unordered_set<VkImageView> distinctTextureViews;
        std::unordered_set<VkSampler> distinctSamplers;
        u32 invalidMaterialCount = 0u;
        bool descriptorsChanged = false;
        for (u32 slot = 0u; slot < kMaxHybridReflectionMaterials; ++slot) {
            const VulkanMaterial* material = slot < materialCount
                ? instanceMaterials[slot]
                : nullptr;
            VkImageView textureView = fallbackTexture->View();
            VkImageLayout textureLayout = fallbackTexture->Layout();
            VkSampler sampler = fallbackSampler->Handle();
            if (material != nullptr) {
                materialRecords[slot] = BuildMaterialRecord(*material, slot);
                textureView = material->AlbedoTexture().View();
                textureLayout = material->AlbedoTexture().Layout();
                sampler = material->Sampler().Handle();
                distinctTextureViews.insert(textureView);
                distinctSamplers.insert(sampler);
            } else if (slot < materialCount) {
                ++invalidMaterialCount;
            }

            materialTextureInfos[slot] = {
                VK_NULL_HANDLE,
                textureView,
                textureLayout
            };
            materialSamplerInfos[slot] = {
                sampler,
                VK_NULL_HANDLE,
                VK_IMAGE_LAYOUT_UNDEFINED
            };
            descriptorsChanged = descriptorsChanged ||
                boundMaterialTextureViews[imageIndex][slot] != textureView ||
                boundMaterialSamplers[imageIndex][slot] != sampler;
        }
        if (descriptorsChanged) {
            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = descriptorSets[imageIndex];
            descriptorWrites[0].dstBinding = 13u;
            descriptorWrites[0].descriptorType =
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            descriptorWrites[0].descriptorCount =
                kMaxHybridReflectionMaterials;
            descriptorWrites[0].pImageInfo = materialTextureInfos.data();
            descriptorWrites[1] = descriptorWrites[0];
            descriptorWrites[1].dstBinding = 14u;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            descriptorWrites[1].pImageInfo = materialSamplerInfos.data();
            vkUpdateDescriptorSets(
                device.Handle(),
                static_cast<u32>(descriptorWrites.size()),
                descriptorWrites.data(),
                0u,
                nullptr
            );
            for (u32 slot = 0u; slot < kMaxHybridReflectionMaterials; ++slot) {
                boundMaterialTextureViews[imageIndex][slot] =
                    materialTextureInfos[slot].imageView;
                boundMaterialSamplers[imageIndex][slot] =
                    materialSamplerInfos[slot].sampler;
            }
        }
        if (materialCount > 0u) {
            materialBuffers[imageIndex]->Upload(std::as_bytes(std::span(
                materialRecords.data(),
                materialCount
            )));
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
        controls.instanceMaterialCount = sourceMaterialCount;
        controls.hitAttributesEnabled =
            enabled && hitAttributesEnabled ? 1u : 0u;
        controls.materialTableCount = materialCount;
        controls.materialTexturesEnabled = enabled && hitAttributesEnabled &&
            materialTexturesEnabled ? 1u : 0u;
        controls.hitLightingEnabled = controls.materialTexturesEnabled != 0u &&
            hitLightingEnabled ? 1u : 0u;
        controls.shadowVisibilityEnabled = controls.hitLightingEnabled != 0u &&
            shadowVisibilityEnabled ? 1u : 0u;
        controls.denoiserInjectionEnabled =
            controls.shadowVisibilityEnabled != 0u &&
            denoiserInjectionEnabled ? 1u : 0u;
        controls.directionalLightCount = std::min(directionalLightCount, 1u);
        controls.localLightCount = std::min(
            localLightCount,
            static_cast<u32>(kMaxFrameLocalLights)
        );
        controls.iblPrefilteredMipCount = iblPrefilteredMipCount;
        controls.hitLightingVisibilityMode = controls.hitLightingEnabled == 0u
            ? 0u
            : controls.shadowVisibilityEnabled != 0u
                ? kHitLightingVisibilityModeRayQuery
                : kHitLightingVisibilityModeUnshadowed;
        controls.iblResourcesReady = 1u;
        controls.maxShadowedLocalLights = std::clamp(
            settings.maxShadowedLocalLights,
            1u,
            kMaxShadowedLocalLights
        );
        controls.rectangleShadowSampleCount = std::clamp(
            settings.rectangleShadowSampleCount,
            1u,
            kMaxRectangleShadowSamples
        );
        controls.diagnosticTargetInstanceIndex =
            settings.diagnosticTargetInstanceIndex;
        controls.runtimeFlags =
            (settings.forceAllRayQueries
                ? kRayQueryRuntimeForceAllRaysBit
                : 0u) |
            (!settings.cullBackFacingTriangles
                ? kRayQueryRuntimeDisableBackFaceCullBit
                : 0u) |
            (settings.diagnosticTargetInstanceIndex !=
                    std::numeric_limits<u32>::max()
                ? kRayQueryRuntimeTargetAttributionBit
                : 0u) |
            (settings.fullAuditEnabled && fullAuditResourcesAllocated
                ? kRayQueryRuntimeFullAuditBit
                : 0u);
        controls.diagnosticsEnabled = enabled && diagnosticsEnabled ? 1u : 0u;
        controlsBuffers[imageIndex]->Upload(
            std::as_bytes(std::span{ &controls, 1u })
        );

        const std::vector<u32> diagnostics =
            BuildDiagnosticClearValues(diagnosticWordCount);
        diagnosticsBuffers[imageIndex]->Upload(
            std::as_bytes(std::span<const u32>(diagnostics))
        );
        submitted[imageIndex] = false;
        denoiserInjectionActive[imageIndex] =
            controls.denoiserInjectionEnabled != 0u;
        diagnosticsActive[imageIndex] = controls.diagnosticsEnabled != 0u;
        fullAuditActive[imageIndex] =
            (controls.runtimeFlags & kRayQueryRuntimeFullAuditBit) != 0u;

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
        stats.rayQueryMaterialTableContractVersion =
            kMaterialTableContractVersion;
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
        stats.rayQueryDiagnosticTargetSubmissionIndex =
            settings.diagnosticTargetSubmissionIndex;
        stats.rayQueryDiagnosticTargetMatchCount =
            settings.diagnosticTargetInstanceIndex < instanceMetadata.size()
                ? 1u
                : 0u;
        stats.rayQueryDiagnosticTargetTlasIndex =
            settings.diagnosticTargetInstanceIndex;
        stats.rayQueryDiagnosticTargetMaterialIndex = 0u;
        if (stats.rayQueryDiagnosticTargetMatchCount != 0u) {
            stats.rayQueryDiagnosticTargetMaterialIndex =
                instanceMetadata[settings.diagnosticTargetInstanceIndex]
                    .materialIndex;
        }
        stats.rayQueryForceAllRayQueries =
            settings.forceAllRayQueries ? 1u : 0u;
        stats.rayQueryCullBackFacingTriangles =
            settings.cullBackFacingTriangles ? 1u : 0u;
        stats.rayQueryFullAuditRequested = settings.fullAuditEnabled ? 1u : 0u;
        stats.rayQueryFullAuditResourcesReady =
            fullAuditResourcesAllocated ? 1u : 0u;
        stats.rayQueryFullAuditActive =
            settings.fullAuditEnabled && fullAuditResourcesAllocated && enabled
                ? 1u
                : 0u;
        stats.rayQueryFullAuditMaxRayRecords =
            fullAuditResourcesAllocated
                ? kHybridReflectionFullAuditMaxRayRecords
                : 0u;
        stats.rayQueryFullAuditCapturedFrameCount = fullAuditFramesWritten;
        stats.rayQueryFullAuditBufferBytes = fullAuditResourcesAllocated
            ? static_cast<u64>(diagnosticWordCount) * sizeof(u32)
            : 0u;
        stats.rayQueryMaterialTableResourcesReady = 1u;
        stats.rayQueryMaterialTableCount = materialCount;
        stats.rayQueryMaterialTableCapacity = kMaxHybridReflectionMaterials;
        stats.rayQueryMaterialTableOverflowCount =
            sourceMaterialCount - materialCount;
        stats.rayQueryMaterialBufferReady = 1u;
        stats.rayQueryMaterialBufferUploadCount = materialCount > 0u ? 1u : 0u;
        stats.rayQueryMaterialBufferBytes =
            static_cast<u64>(materialCount) *
            sizeof(HybridReflectionMaterialRecord);
        stats.rayQueryTextureDescriptorCount = materialCount;
        stats.rayQueryTextureDescriptorCapacity = kMaxHybridReflectionMaterials;
        stats.rayQuerySamplerDescriptorCount = materialCount;
        stats.rayQuerySamplerDescriptorCapacity = kMaxHybridReflectionMaterials;
        stats.rayQueryDistinctTextureCount =
            static_cast<u32>(distinctTextureViews.size());
        stats.rayQueryDistinctSamplerCount =
            static_cast<u32>(distinctSamplers.size());
        stats.rayQueryDuplicateTextureCount = materialCount -
            invalidMaterialCount - stats.rayQueryDistinctTextureCount;
        stats.rayQueryDuplicateSamplerCount = materialCount -
            invalidMaterialCount - stats.rayQueryDistinctSamplerCount;
        stats.rayQueryFallbackDescriptorCount =
            (kMaxHybridReflectionMaterials - materialCount +
                invalidMaterialCount) * 2u;
        stats.rayQueryHitSurfaceWidth = extent.width;
        stats.rayQueryHitSurfaceHeight = extent.height;
        stats.rayQueryHitSurfaceFormat = static_cast<u32>(kHitSurfaceFormat);
        stats.rayQueryHitLightingContractVersion =
            kHitLightingContractVersion;
        stats.rayQueryShadowVisibilityContractVersion =
            kShadowVisibilityContractVersion;
        stats.rayQueryDenoiserBridgeContractVersion =
            kDenoiserBridgeContractVersion;
        stats.rayQueryHitLightingResourcesReady = 1u;
        stats.rayQueryLightBufferDescriptorReady = 1u;
        stats.rayQueryIblBrdfDescriptorReady = 1u;
        stats.rayQueryIblIrradianceDescriptorReady = 1u;
        stats.rayQueryIblPrefilteredDescriptorReady = 1u;
        stats.rayQueryIblSamplerDescriptorReady = 1u;
        stats.rayQueryIblPrefilteredMipCount = iblPrefilteredMipCount;
        stats.rayQueryDirectionalLightCount = controls.directionalLightCount;
        stats.rayQueryLocalLightCount = controls.localLightCount;
        stats.rayQueryHitLightingVisibilityMode =
            controls.hitLightingVisibilityMode;
        stats.rayQueryHitLightingVisibilityFallbackReason =
            controls.hitLightingEnabled != 0u &&
                controls.shadowVisibilityEnabled == 0u
                ? kHitLightingVisibilityFallbackPendingRayQuery
                : 0u;
        stats.rayQueryShadowVisibilityResourcesReady = 1u;
        stats.rayQueryShadowMaxLocalLightCount =
            controls.maxShadowedLocalLights;
        stats.rayQueryShadowRectangleSampleCount =
            controls.rectangleShadowSampleCount;
        stats.rayQueryShadowMaxRaysPerHit =
            controls.directionalLightCount +
            controls.maxShadowedLocalLights *
                controls.rectangleShadowSampleCount;
        stats.rayQueryDenoiserResourcesReady = 1u;
        stats.rayQueryDenoiserRadianceDescriptorReady = 1u;
        stats.rayQueryDenoiserConfidenceDescriptorReady = 1u;
        stats.rayQueryDenoiserInjectionEnabled =
            controls.denoiserInjectionEnabled;
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
        VkImageMemoryBarrier hitSurfaceForClear = resultForClear;
        hitSurfaceForClear.image = hitSurfaceImages[imageIndex]->Handle();
        std::array<VkImageMemoryBarrier, 2> imagesForClear{
            resultForClear,
            hitSurfaceForClear
        };
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u,
            0u,
            nullptr,
            0u,
            nullptr,
            static_cast<u32>(imagesForClear.size()),
            imagesForClear.data()
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
        vkCmdClearColorImage(
            commandBuffer,
            hitSurfaceImages[imageIndex]->Handle(),
            VK_IMAGE_LAYOUT_GENERAL,
            &clearValue,
            1u,
            &hitSurfaceForClear.subresourceRange
        );

        VkImageMemoryBarrier resultForWrite = resultForClear;
        resultForWrite.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        resultForWrite.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        VkImageMemoryBarrier hitSurfaceForWrite = hitSurfaceForClear;
        hitSurfaceForWrite.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hitSurfaceForWrite.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        VkImageMemoryBarrier denoiserRadianceForWrite = resultForClear;
        denoiserRadianceForWrite.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        denoiserRadianceForWrite.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        denoiserRadianceForWrite.image =
            classifyResources.IntersectionOutputImage(imageIndex);
        VkImageMemoryBarrier confidenceForReadWrite = resultForClear;
        confidenceForReadWrite.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        confidenceForReadWrite.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        confidenceForReadWrite.image =
            classifyResources.HitConfidenceImage(imageIndex);
        std::array<VkImageMemoryBarrier, 4> imageBarriers{
            resultForWrite,
            hitSurfaceForWrite,
            denoiserRadianceForWrite,
            confidenceForReadWrite
        };

        std::array<VkBufferMemoryBarrier, 5> bufferBarriers{};
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
        setBufferBarrier(
            bufferBarriers[4],
            materialBuffers[imageIndex]->Handle(),
            materialBuffers[imageIndex]->Size(),
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
        stats.active = denoiserInjectionActive[imageIndex] ? 1u : 0u;
        stats.fallbackReason = stats.active != 0u ? 0u : 8u;
    }

    HybridReflectionRayQueryDiagnostics ReadDiagnostics(u32 imageIndex) const {
        HybridReflectionRayQueryDiagnostics result{};
        if (imageIndex >= diagnosticsBuffers.size() ||
            !submitted[imageIndex] ||
            !diagnosticsActive[imageIndex]) {
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
        result.materialRecordResolvedCount = values[27];
        result.materialRecordFallbackCount = values[28];
        result.textureSampleResolvedCount = values[29];
        result.textureSampleFallbackCount = values[30];
        result.textureSampleInvalidCount = values[31];
        result.finiteSampledColorCount = values[32];
        result.sampleLodMinMillilevels = result.textureSampleResolvedCount > 0u
            ? values[33]
            : 0u;
        result.sampleLodMaxMillilevels = values[34];
        result.hitSurfacePayloadWriteCount = values[35];
        result.hitSurfacePayloadChecksum = values[36];
        result.hitSurfaceLuminanceMinMilliunits =
            result.hitSurfacePayloadWriteCount > 0u ? values[37] : 0u;
        result.hitSurfaceLuminanceMaxMilliunits = values[38];
        result.hitLightingResolvedCount = values[39];
        result.hitLightingInvalidCount = values[40];
        result.directionalLightEvaluationCount = values[41];
        result.directionalLightContributionCount = values[42];
        result.pointLightEvaluationCount = values[43];
        result.pointLightContributionCount = values[44];
        result.spotLightEvaluationCount = values[45];
        result.spotLightContributionCount = values[46];
        result.rectLightEvaluationCount = values[47];
        result.rectLightContributionCount = values[48];
        result.finiteDirectRadianceCount = values[49];
        result.finiteIblRadianceCount = values[50];
        result.finiteEmissiveRadianceCount = values[51];
        result.finiteRadianceCount = values[52];
        result.directLuminanceSumMilliunits = values[53];
        result.iblLuminanceSumMilliunits = values[54];
        result.emissiveLuminanceSumMilliunits = values[55];
        result.radianceLuminanceMinMilliunits =
            result.finiteRadianceCount > 0u ? values[56] : 0u;
        result.radianceLuminanceMaxMilliunits = values[57];
        result.radianceChecksum = values[58];
        result.shadowVisibilityResolvedCount = values[59];
        result.shadowRayCount = values[60];
        result.shadowVisibleCount = values[61];
        result.shadowOccludedCount = values[62];
        result.shadowInvalidCount = values[63];
        result.directionalShadowRayCount = values[64];
        result.pointShadowRayCount = values[65];
        result.spotShadowRayCount = values[66];
        result.rectShadowRayCount = values[67];
        result.localShadowCandidateCount = values[68];
        result.localShadowSelectedCount = values[69];
        result.localShadowDroppedCount = values[70];
        result.unshadowedDirectLuminanceSumMilliunits = values[71];
        result.visibleDirectLuminanceSumMilliunits = values[72];
        result.shadowSelfIntersectionCandidateCount = values[73];
        result.shadowHitDistanceMinMillimeters =
            result.shadowOccludedCount > 0u ? values[74] : 0u;
        result.shadowHitDistanceMaxMillimeters = values[75];
        result.shadowVisibilityMinPermille =
            result.shadowVisibilityResolvedCount > 0u ? values[76] : 0u;
        result.shadowVisibilityMaxPermille = values[77];
        result.localShadowDroppedLuminanceSumMilliunits = values[78];
        result.denoiserInjectionResolvedCount = values[79];
        result.denoiserRadiancePixelWriteCount = values[80];
        result.denoiserConfidencePixelWriteCount = values[81];
        result.denoiserConfidenceSumPermille = values[82];
        result.diagnosticTargetCommittedHitCount = values[83];
        result.diagnosticTargetAttributeResolvedCount = values[84];
        result.diagnosticTargetDenoiserWriteCount = values[85];
        return result;
    }

    VkDescriptorBufferInfo FullAuditDescriptorInfo(u32 imageIndex) const {
        if (!fullAuditResourcesAllocated ||
            imageIndex >= diagnosticsBuffers.size()) {
            return {};
        }
        return VkDescriptorBufferInfo{
            diagnosticsBuffers[imageIndex]->Handle(),
            0u,
            diagnosticsBuffers[imageIndex]->Size()
        };
    }

    void RecordFullAuditApplyBarrier(
        VkCommandBuffer commandBuffer,
        u32 imageIndex,
        bool forHostRead
    ) const {
        if (!fullAuditResourcesAllocated ||
            imageIndex >= diagnosticsBuffers.size() ||
            imageIndex >= fullAuditActive.size() ||
            !submitted[imageIndex] || !fullAuditActive[imageIndex]) {
            return;
        }
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = forHostRead
            ? VK_ACCESS_HOST_READ_BIT
            : VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = diagnosticsBuffers[imageIndex]->Handle();
        barrier.offset = 0u;
        barrier.size = diagnosticsBuffers[imageIndex]->Size();
        vkCmdPipelineBarrier(
            commandBuffer,
            forHostRead
                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            forHostRead
                ? VK_PIPELINE_STAGE_HOST_BIT
                : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0u,
            0u,
            nullptr,
            1u,
            &barrier,
            0u,
            nullptr
        );
    }

    void RecordFullAuditImageSnapshot(
        VkCommandBuffer commandBuffer,
        u32 imageIndex,
        HybridReflectionFullAuditImageStage stage,
        VkImage image,
        VkFormat format,
        VkExtent2D imageExtent,
        VkImageLayout currentLayout,
        VkImage objectIdImage,
        VkImageLayout objectIdLayout
    ) {
        if (!fullAuditResourcesAllocated ||
            image == VK_NULL_HANDLE ||
            imageIndex >= fullAuditActive.size() ||
            !fullAuditActive[imageIndex]) {
            return;
        }
        const u32 stageIndex = static_cast<u32>(stage);
        if (stageIndex >= kHybridReflectionFullAuditImageStageCount ||
            imageIndex >= fullAuditImageReadbackBuffers[stageIndex].size() ||
            fullAuditImageReadbackBuffers[stageIndex][imageIndex] == nullptr ||
            fullAuditImageRecorded[stageIndex][imageIndex] ||
            fullAuditImageFormats[stageIndex] != format ||
            fullAuditImageExtents[stageIndex].width != imageExtent.width ||
            fullAuditImageExtents[stageIndex].height != imageExtent.height) {
            return;
        }
        const VkDeviceSize pixelBytes = FullAuditFormatPixelBytes(format);
        const VkDeviceSize requiredBytes = static_cast<VkDeviceSize>(
            imageExtent.width
        ) * static_cast<VkDeviceSize>(imageExtent.height) * pixelBytes;
        if (requiredBytes == 0u ||
            requiredBytes > fullAuditImageReadbackBuffers[stageIndex][imageIndex]->Size()) {
            return;
        }

        RecordFullAuditImageBarrier(
            commandBuffer,
            image,
            currentLayout,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            currentLayout == VK_IMAGE_LAYOUT_GENERAL
                ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                : VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT
        );
        VkBufferImageCopy imageCopy{};
        imageCopy.bufferOffset = 0u;
        imageCopy.bufferRowLength = 0u;
        imageCopy.bufferImageHeight = 0u;
        imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopy.imageSubresource.mipLevel = 0u;
        imageCopy.imageSubresource.baseArrayLayer = 0u;
        imageCopy.imageSubresource.layerCount = 1u;
        imageCopy.imageExtent = { imageExtent.width, imageExtent.height, 1u };
        vkCmdCopyImageToBuffer(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            fullAuditImageReadbackBuffers[stageIndex][imageIndex]->Handle(),
            1u,
            &imageCopy
        );
        RecordFullAuditImageBarrier(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            currentLayout,
            VK_ACCESS_TRANSFER_READ_BIT,
            currentLayout == VK_IMAGE_LAYOUT_GENERAL
                ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                : VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        );

        fullAuditImageExtents[stageIndex] = imageExtent;
        fullAuditImageFormats[stageIndex] = format;
        fullAuditImageRecorded[stageIndex][imageIndex] = true;

        if (objectIdImage == VK_NULL_HANDLE ||
            imageIndex >= fullAuditObjectIdReadbackBuffers.size() ||
            fullAuditObjectIdReadbackBuffers[imageIndex] == nullptr ||
            fullAuditObjectIdRecorded[imageIndex]) {
            return;
        }
        const VkExtent2D objectIdExtent = extent;
        const VkDeviceSize objectIdBytes = static_cast<VkDeviceSize>(
            objectIdExtent.width
        ) * static_cast<VkDeviceSize>(objectIdExtent.height) * sizeof(u32);
        if (objectIdBytes > fullAuditObjectIdReadbackBuffers[imageIndex]->Size()) {
            return;
        }
        RecordFullAuditImageBarrier(
            commandBuffer,
            objectIdImage,
            objectIdLayout,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT
        );
        VkBufferImageCopy objectIdCopy = imageCopy;
        objectIdCopy.imageExtent = { objectIdExtent.width, objectIdExtent.height, 1u };
        vkCmdCopyImageToBuffer(
            commandBuffer,
            objectIdImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            fullAuditObjectIdReadbackBuffers[imageIndex]->Handle(),
            1u,
            &objectIdCopy
        );
        RecordFullAuditImageBarrier(
            commandBuffer,
            objectIdImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            objectIdLayout,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        );
        fullAuditObjectIdRecorded[imageIndex] = true;
    }

    void CaptureFullAuditMetadata(
        u32 imageIndex,
        u64 frameNumber,
        u32 sceneObjectCount,
        std::span<const HybridReflectionFullAuditLightRecord> lights,
        std::span<const HybridReflectionFullAuditProbeRecord> probes,
        std::span<const HybridReflectionFullAuditQueueCommandRecord> commands
    ) {
        if (!fullAuditResourcesAllocated || imageIndex >= fullAuditMetadata.size()) {
            return;
        }
        FullAuditMetadata& metadata = fullAuditMetadata[imageIndex];
        metadata.valid = true;
        metadata.frameNumber = frameNumber;
        metadata.sceneObjectCount = sceneObjectCount;
        metadata.lights.assign(lights.begin(), lights.end());
        metadata.probes.assign(probes.begin(), probes.end());
        metadata.commands.assign(commands.begin(), commands.end());
        fullAuditObjectIdRecorded[imageIndex] = false;
        for (auto& recorded : fullAuditImageRecorded) {
            if (imageIndex < recorded.size()) {
                recorded[imageIndex] = false;
            }
        }
    }

    void WriteFullAuditFrame(
        u32 imageIndex,
        u64 frameNumber,
        std::span<const HybridReflectionInstanceAuditRecord> instances,
        RendererHybridReflectionStats& stats
    ) {
        if (!fullAuditResourcesAllocated ||
            imageIndex >= diagnosticsBuffers.size() ||
            imageIndex >= fullAuditActive.size() ||
            !submitted[imageIndex] ||
            !fullAuditActive[imageIndex] ||
            fullAuditFramesWritten >= fullAuditMaxCapturedFrames) {
            return;
        }

        std::vector<u32> values(diagnosticWordCount, 0u);
        diagnosticsBuffers[imageIndex]->Download(
            std::as_writable_bytes(std::span<u32>(values))
        );
        const u32 recordCount = std::min(
            values[0],
            kHybridReflectionFullAuditMaxRayRecords
        );
        const u32 applyRecordCount = std::min(
            values[kFullAuditApplyRecordCountIndex],
            kHybridReflectionFullAuditMaxApplyRecords
        );
        const u32 applyRecordOverflow =
            values[kFullAuditApplyRecordCountIndex] >
                kHybridReflectionFullAuditMaxApplyRecords
            ? values[kFullAuditApplyRecordCountIndex] -
                kHybridReflectionFullAuditMaxApplyRecords
            : 0u;
        std::filesystem::create_directories(fullAuditOutputDirectory);
        const bool firstFrame = fullAuditFramesWritten == 0u;
        const std::ios::openmode mode = std::ios::out |
            (firstFrame ? std::ios::trunc : std::ios::app);

        std::ofstream frameFile(
            fullAuditOutputDirectory / "frames.csv",
            mode
        );
        std::ofstream instanceFile(
            fullAuditOutputDirectory / "instances.csv",
            mode
        );
        std::ofstream objectFile(
            fullAuditOutputDirectory / "objects.csv",
            mode
        );
        std::ofstream counterFile(
            fullAuditOutputDirectory / "instance_counters.csv",
            mode
        );
        std::ofstream rayFile(
            fullAuditOutputDirectory / "rays.csv",
            mode
        );
        std::ofstream applyFile(
            fullAuditOutputDirectory / "apply.csv",
            mode
        );
        std::ofstream gBufferSamplesFile(
            fullAuditOutputDirectory / "gbuffer_samples.csv",
            mode
        );
        std::ofstream compositionFile(
            fullAuditOutputDirectory / "reflection_composition.csv",
            mode
        );
        std::ofstream compositionSummaryFile(
            fullAuditOutputDirectory / "reflection_composition_summary.csv",
            mode
        );
        std::ofstream lightsFile(
            fullAuditOutputDirectory / "lights.csv",
            mode
        );
        std::ofstream probesFile(
            fullAuditOutputDirectory / "probes.csv",
            mode
        );
        std::ofstream queueFile(
            fullAuditOutputDirectory / "queue_commands.csv",
            mode
        );
        std::ofstream auditIndexFile(
            fullAuditOutputDirectory / "audit_index.csv",
            mode
        );
        std::ofstream runtimeObjectFile(
            fullAuditOutputDirectory / "runtime_object_summary.csv",
            mode
        );
        std::ofstream runtimePairFile(
            fullAuditOutputDirectory / "runtime_receiver_hit_matrix.csv",
            mode
        );
        std::ofstream runtimeQualityFile(
            fullAuditOutputDirectory / "runtime_receiver_quality.csv",
            mode
        );
        std::ofstream runtimeApplyDiscontinuityFile(
            fullAuditOutputDirectory / "runtime_apply_discontinuities.csv",
            mode
        );
        std::ofstream runtimeDnsrConfidenceQualityFile(
            fullAuditOutputDirectory / "runtime_dnsr_confidence_quality.csv",
            mode
        );
        std::ofstream runtimeDnsrConfidenceTransitionFile(
            fullAuditOutputDirectory /
                "runtime_dnsr_confidence_transitions.csv",
            mode
        );
        std::ofstream imageSnapshotManifestFile(
            fullAuditOutputDirectory / "image_snapshot_manifest.csv",
            mode
        );
        if (!frameFile || !instanceFile || !objectFile || !counterFile ||
            !rayFile || !applyFile || !gBufferSamplesFile || !compositionFile ||
            !compositionSummaryFile || !lightsFile || !probesFile ||
            !queueFile || !auditIndexFile || !runtimeObjectFile ||
            !runtimePairFile || !runtimeQualityFile ||
            !runtimeApplyDiscontinuityFile ||
            !runtimeDnsrConfidenceQualityFile ||
            !runtimeDnsrConfidenceTransitionFile ||
            !imageSnapshotManifestFile) {
            throw std::runtime_error(
                "Failed to open hybrid reflection full-audit output files"
            );
        }
        frameFile << std::setprecision(9);
        instanceFile << std::setprecision(9);
        objectFile << std::setprecision(9);
        counterFile << std::setprecision(9);
        rayFile << std::setprecision(9);
        applyFile << std::setprecision(9);
        gBufferSamplesFile << std::setprecision(9);
        compositionFile << std::setprecision(9);
        compositionSummaryFile << std::setprecision(9);
        lightsFile << std::setprecision(9);
        probesFile << std::setprecision(9);
        queueFile << std::setprecision(9);
        auditIndexFile << std::setprecision(17);
        runtimeObjectFile << std::setprecision(9);
        runtimePairFile << std::setprecision(9);
        runtimeQualityFile << std::setprecision(9);
        runtimeApplyDiscontinuityFile << std::setprecision(9);
        runtimeDnsrConfidenceQualityFile << std::setprecision(17);
        runtimeDnsrConfidenceTransitionFile << std::setprecision(17);
        imageSnapshotManifestFile << std::setprecision(9);
        const FullAuditMetadata* metadata =
            imageIndex < fullAuditMetadata.size() &&
                fullAuditMetadata[imageIndex].valid
            ? &fullAuditMetadata[imageIndex]
            : nullptr;
        const u64 capturedFrameNumber = metadata != nullptr
            ? metadata->frameNumber
            : frameNumber;
        if (firstFrame) {
            frameFile <<
                "capture_index,frame_number,image_index,width,height,"
                "scene_object_count,instance_count,candidate_records,"
                "screen_accepted,"
                "production_traces,committed_hits,misses,invalid_rays,"
                "apply_records,apply_record_overflow,cpu_frame_number,"
                "metadata_valid,metadata_light_count,metadata_probe_count,"
                "metadata_queue_command_count,image_stage_mask,"
                "object_id_readback_valid,raw_evidence\n";
            objectFile <<
                "capture_index,frame_number,tlas_index,submission_index,"
                "reflection_audit_object_id,"
                "in_tlas,exclusion_reason,render_identity,renderable_name,"
                "mesh_identity,material_identity,material_index,"
                "instance_flags,vertex_count,index_count,vertex_stride,"
                "alpha_mode,render_class,double_sided,cast_shadow,"
                "reflection_capture_visible,metallic,roughness,texture_mix,"
                "model_determinant,bounds_min_x,bounds_min_y,bounds_min_z,"
                "bounds_max_x,bounds_max_y,bounds_max_z,"
                "base_r,base_g,base_b,base_a,emissive_r,emissive_g,"
                "emissive_b,emissive_a";
            for (u32 element = 0u; element < 16u; ++element) {
                objectFile << ",model_" << element;
            }
            objectFile << '\n';
            instanceFile <<
                "capture_index,frame_number,tlas_index,submission_index,"
                "reflection_audit_object_id,"
                "render_identity,renderable_name,mesh_identity,"
                "material_identity,material_index,instance_flags,"
                "vertex_count,index_count,vertex_stride,alpha_mode,"
                "render_class,double_sided,cast_shadow,"
                "reflection_capture_visible,metallic,roughness,texture_mix,"
                "model_determinant,bounds_min_x,bounds_min_y,bounds_min_z,"
                "bounds_max_x,bounds_max_y,bounds_max_z,"
                "base_r,base_g,base_b,base_a,emissive_r,emissive_g,"
                "emissive_b,emissive_a";
            for (u32 element = 0u; element < 16u; ++element) {
                instanceFile << ",model_" << element;
            }
            instanceFile << '\n';
            counterFile <<
                "capture_index,frame_number,tlas_index,submission_index,"
                "reflection_audit_object_id,"
                "renderable_name,receiver_samples,receiver_screen_accepted,"
                "receiver_production_traces,receiver_production_hits,"
                "incoming_theoretical_hits,incoming_attribute_hits,"
                "incoming_denoiser_writes,receiver_production_misses\n";
            rayFile <<
                "capture_index,frame_number,ray_index,pixel_x,pixel_y,"
                "copy_horizontal,copy_vertical,copy_diagonal,source,flags,"
                "receiver_tlas_index,screen_confidence,hit_tlas_index,"
                "hit_primitive_index,hit_distance,hit_material_index,"
                "receiver_roughness,origin_x,origin_y,origin_z,"
                "direction_x,direction_y,direction_z,final_luminance,"
                "normal_x,normal_y,normal_z,normal_dot_reflection,"
                "receiver_object_id,origin_bias,origin_side,"
                "self_hit_distance\n";
            applyFile <<
                "capture_index,frame_number,apply_index,pixel_x,pixel_y,"
                "receiver_object_id,resolved_r,resolved_g,resolved_b,"
                "provenance_confidence,probe_r,probe_g,probe_b,"
                "blend_weight,contribution_r,contribution_g,"
                "contribution_b,roughness,metallic,flags\n";
            gBufferSamplesFile <<
                "capture_index,frame_number,sample_index,pixel_x,pixel_y,"
                "receiver_object_id,roughness,metallic,confidence,"
                "resolved_r,resolved_g,resolved_b,probe_r,probe_g,probe_b,"
                "blend_weight,contribution_r,contribution_g,contribution_b,"
                "finite,source_negative,contribution_negative\n";
            compositionFile <<
                "capture_index,frame_number,stage,stage_index,image_index,"
                "width,height,format,object_id,pixel_x,pixel_y,r,g,b,a,"
                "finite,negative\n";
            compositionSummaryFile <<
                "capture_index,frame_number,stage,stage_index,image_index,"
                "width,height,format,pixel_count,finite_count,"
                "non_finite_count,negative_count,luminance_sum,"
                "luminance_min,luminance_max,alpha_sum,alpha_min,alpha_max,"
                "zero_alpha_count,one_alpha_count\n";
            lightsFile <<
                "capture_index,frame_number,light_index,kind,enabled,"
                "casts_shadow,position_x,position_y,position_z,radius,"
                "color_r,color_g,color_b,intensity,direction_x,direction_y,"
                "direction_z,inner_cone_cos,outer_cone_cos,width,height,"
                "source_radius,specular,requested_shadow_tiles,"
                "assigned_shadow_tiles,first_shadow_tile,"
                "shadow_tile_range_valid\n";
            probesFile <<
                "capture_index,frame_number,probe_index,scene_index,enabled,"
                "scene_owned,capture_source,refresh_policy,"
                "capture_resource_ready,capture_descriptor_bound,"
                "fallback_reason,capture_slot,capture_mip_count,"
                "center_x,center_y,center_z,radius,box_x,box_y,box_z,"
                "color_r,color_g,color_b,intensity,blend_weight,falloff,"
                "box_projection_enabled,normalized_blend_weight\n";
            queueFile <<
                "capture_index,frame_number,queue_kind,command_index,"
                "submission_index,reflection_audit_object_id,render_identity,"
                "mesh_identity,material_identity,material_index,draw_order,"
                "lod_level,cast_shadow,reflection_capture_visible,"
                "skinned_bounds_conservative,bone_palette_ready,"
                "bone_palette_descriptor_ready,world_bounds_valid,"
                "bounds_min_x,bounds_min_y,bounds_min_z,bounds_max_x,"
                "bounds_max_y,bounds_max_z,renderable_name\n";
            auditIndexFile <<
                "contract_version,capture_index,frame_number,ray_rows,"
                "apply_rows,gbuffer_rows,unknown_receiver_count,"
                "unknown_hit_count,unresolved_receiver_count,"
                "invalid_stage_chain_count,missing_object_id_count,"
                "unknown_object_id_count,receiver_truth_mismatch_count,"
                "receiver_origin_outside_bounds_count,self_hit_count,"
                "negative_hemisphere_count,"
                "negative_hemisphere_rejected_count,"
                "scene_receiver_identity_resolved_count,"
                "tlas_eligible_receiver_ray_count,"
                "tlas_eligible_receiver_resolved_count,"
                "apply_missing_object_id_count,"
                "apply_unknown_object_id_count,apply_non_finite_count,"
                "gbuffer_non_finite_count,gbuffer_source_negative_count,"
                "apply_negative_contribution_count,"
                "receiver_counter_mismatch_count,"
                "incoming_counter_mismatch_count,"
                "apply_nonzero_contribution_count,"
                "apply_contribution_luminance_sum,apply_blend_mode,"
                "apply_confidence_source,"
                "apply_blend_expected_luminance_sum,"
                "apply_alpha_lookup_missing_count,hdr_before_luminance_sum,"
                "hdr_after_luminance_sum,hdr_actual_luminance_delta,"
                "hdr_before_alpha_sum,hdr_before_alpha_min,"
                "hdr_before_alpha_max,hdr_before_zero_alpha_count,"
                "hdr_before_one_alpha_count\n";
            runtimeObjectFile <<
                "capture_index,frame_number,tlas_index,submission_index,"
                "reflection_audit_object_id,renderable_name,material_index,"
                "metallic,roughness,receiver_samples,screen_accepted,"
                "production_traces,production_hits,production_misses,"
                "incoming_theoretical_hits,incoming_attribute_hits,"
                "incoming_denoiser_writes,gpu_receiver_samples,"
                "gpu_screen_accepted,gpu_production_traces,"
                "gpu_production_hits,gpu_production_misses,"
                "gpu_incoming_theoretical_hits,gpu_incoming_attribute_hits,"
                "gpu_incoming_denoiser_writes\n";
            runtimePairFile <<
                "capture_index,frame_number,receiver_tlas_index,"
                "receiver_object_id,receiver_name,hit_tlas_index,"
                "hit_object_id,hit_name,total_hits,screen_accepted_hits,"
                "production_rt_hits,attribute_hits,denoiser_writes\n";
            runtimeQualityFile <<
                "capture_index,frame_number,receiver_tlas_index,"
                "receiver_object_id,receiver_name,sample_count,"
                "screen_accepted,ray_query_hits,ray_query_misses,self_hits,"
                "negative_hemisphere_rays,dark_ray_query_hits,"
                "adjacent_pair_count,source_transition_count,"
                "hit_identity_transition_count,large_luminance_jump_count,"
                "apply_sample_count,apply_adjacent_pair_count,"
                "apply_blend_discontinuity_count,"
                "apply_confidence_discontinuity_count,"
                "apply_unexplained_blend_discontinuity_count,"
                "apply_blend_contribution_jump_count,"
                "apply_contribution_jump_count,"
                "apply_large_source_disagreement_count\n";
            runtimeApplyDiscontinuityFile <<
                "capture_index,frame_number,receiver_tlas_index,"
                "receiver_object_id,receiver_name,pixel_x,pixel_y,"
                "neighbor_x,neighbor_y,axis,roughness,neighbor_roughness,"
                "confidence,neighbor_confidence,blend_weight,"
                "neighbor_blend_weight,resolved_luminance,"
                "neighbor_resolved_luminance,probe_luminance,"
                "neighbor_probe_luminance,contribution_luminance,"
                "neighbor_contribution_luminance,roughness_delta,"
                "confidence_delta,blend_delta,contribution_delta,flags,"
                "neighbor_flags\n";
            runtimeDnsrConfidenceQualityFile <<
                "capture_index,frame_number,stage,stage_index,"
                "receiver_tlas_index,receiver_object_id,receiver_name,"
                "sample_count,confidence_mean,confidence_min,confidence_max,"
                "adjacent_pair_count,spatial_discontinuity_count,"
                "spatial_discontinuity_ratio\n";
            runtimeDnsrConfidenceTransitionFile <<
                "capture_index,frame_number,receiver_tlas_index,"
                "receiver_object_id,receiver_name,source_stage,"
                "destination_stage,sample_count,large_transition_count,"
                "increase_count,decrease_count,mean_absolute_delta,"
                "max_absolute_delta\n";
            imageSnapshotManifestFile <<
                "capture_index,frame_number,stage,stage_index,image_index,"
                "width,height,format,pixel_bytes,file_name,file_bytes,"
                "object_id_file_name,object_id_file_bytes,verbose_csv,"
                "raw_binary\n";
        }

        const u32 tlasInstanceCount = static_cast<u32>(std::count_if(
            instances.begin(),
            instances.end(),
            [](const HybridReflectionInstanceAuditRecord& instance) {
                return instance.inTlas != 0u;
            }
        ));
        u32 imageStageMask = 0u;
        for (u32 stageIndex = 0u;
             stageIndex < kHybridReflectionFullAuditImageStageCount;
             ++stageIndex) {
            if (imageIndex < fullAuditImageRecorded[stageIndex].size() &&
                fullAuditImageRecorded[stageIndex][imageIndex]) {
                imageStageMask |= 1u << stageIndex;
            }
        }
        frameFile << fullAuditFramesWritten << ',' << capturedFrameNumber << ','
            << imageIndex << ',' << extent.width << ',' << extent.height << ','
            << instances.size() << ',' << tlasInstanceCount << ','
            << recordCount << ',' << values[1] << ',' << values[2] << ','
            << values[3] << ',' << values[4] << ',' << values[5] << ','
            << applyRecordCount << ',' << applyRecordOverflow << ','
            << frameNumber << ',' << (metadata != nullptr ? 1u : 0u) << ','
            << (metadata != nullptr ? metadata->lights.size() : 0u) << ','
            << (metadata != nullptr ? metadata->probes.size() : 0u) << ','
            << (metadata != nullptr ? metadata->commands.size() : 0u) << ','
            << imageStageMask << ','
            << (fullAuditObjectIdRecorded[imageIndex] ? 1u : 0u) << ','
            << (fullAuditRawEvidence ? 1u : 0u) << '\n';

        auto writeObjectRecord = [&](std::ostream& file,
                                     const HybridReflectionInstanceAuditRecord& instance,
                                     bool includeEligibility) {
            file << fullAuditFramesWritten << ',' << capturedFrameNumber << ','
                << instance.tlasIndex << ',' << instance.submissionIndex << ','
                << instance.reflectionAuditObjectId;
            if (includeEligibility) {
                file << ',' << instance.inTlas << ','
                    << instance.exclusionReason;
            }
            file << ',' << instance.renderIdentity << ','
                << CsvEscape(instance.renderableName) << ','
                << instance.meshIdentity << ',' << instance.materialIdentity
                << ',' << instance.materialIndex << ',' << instance.instanceFlags
                << ',' << instance.vertexCount << ',' << instance.indexCount
                << ',' << instance.vertexStride << ',' << instance.alphaMode
                << ',' << instance.renderClass << ',' << instance.doubleSided
                << ',' << instance.castShadow << ','
                << instance.reflectionCaptureVisible << ',' << instance.metallic
                << ',' << instance.roughness << ',' << instance.textureMix
                << ',' << instance.modelDeterminant;
            for (const f32 value : instance.boundsMin) {
                file << ',' << value;
            }
            for (const f32 value : instance.boundsMax) {
                file << ',' << value;
            }
            for (const f32 value : instance.baseColor) {
                file << ',' << value;
            }
            for (const f32 value : instance.emissive) {
                file << ',' << value;
            }
            for (const f32 value : instance.model) {
                file << ',' << value;
            }
            file << '\n';
        };
        for (const HybridReflectionInstanceAuditRecord& instance : instances) {
            writeObjectRecord(objectFile, instance, true);
            if (instance.inTlas == 0u) {
                continue;
            }
            writeObjectRecord(instanceFile, instance, false);

            const u32 counterBase = kFullAuditInstanceCounterBase +
                instance.tlasIndex * kFullAuditInstanceCounterCount;
            counterFile << fullAuditFramesWritten << ',' << capturedFrameNumber << ','
                << instance.tlasIndex << ',' << instance.submissionIndex << ','
                << instance.reflectionAuditObjectId << ','
                << CsvEscape(instance.renderableName);
            for (u32 counter = 0u;
                 counter < kFullAuditInstanceCounterCount;
                 ++counter) {
                counterFile << ',' << values[counterBase + counter];
            }
            counterFile << '\n';
        }

        struct DerivedObjectCounters {
            u64 receiverSamples = 0u;
            u64 screenAccepted = 0u;
            u64 productionTraces = 0u;
            u64 productionHits = 0u;
            u64 productionMisses = 0u;
            u64 incomingHits = 0u;
            u64 incomingAttributes = 0u;
            u64 incomingDenoiserWrites = 0u;
            u64 selfHits = 0u;
            u64 negativeHemisphereRays = 0u;
            u64 darkRayQueryHits = 0u;
            u64 adjacentPairs = 0u;
            u64 sourceTransitions = 0u;
            u64 hitIdentityTransitions = 0u;
            u64 largeLuminanceJumps = 0u;
            u64 applySamples = 0u;
            u64 applyAdjacentPairs = 0u;
            u64 applyBlendDiscontinuities = 0u;
            u64 applyConfidenceDiscontinuities = 0u;
            u64 applyUnexplainedBlendDiscontinuities = 0u;
            u64 applyBlendContributionJumps = 0u;
            u64 applyContributionJumps = 0u;
            u64 applyLargeSourceDisagreements = 0u;
        };
        struct PairCounters {
            u64 total = 0u;
            u64 screen = 0u;
            u64 production = 0u;
            u64 attributes = 0u;
            u64 denoiser = 0u;
        };
        struct FrameAuditCounters {
            u64 unknownReceivers = 0u;
            u64 unknownHits = 0u;
            u64 unresolvedReceivers = 0u;
            u64 invalidStageChains = 0u;
            u64 missingObjectIds = 0u;
            u64 unknownObjectIds = 0u;
            u64 receiverTruthMismatches = 0u;
            u64 receiverOriginsOutsideBounds = 0u;
            u64 selfHits = 0u;
            u64 negativeHemisphere = 0u;
            u64 negativeHemisphereRejected = 0u;
            u64 sceneReceiverIdentityResolved = 0u;
            u64 tlasEligibleReceiverRays = 0u;
            u64 tlasEligibleReceiverResolved = 0u;
            u64 applyMissingObjectIds = 0u;
            u64 applyUnknownObjectIds = 0u;
            u64 applyNonFinite = 0u;
            u64 gBufferNonFinite = 0u;
            u64 gBufferSourceNegative = 0u;
            u64 negativeApplyContributions = 0u;
            u64 receiverCounterMismatches = 0u;
            u64 incomingCounterMismatches = 0u;
            u64 nonzeroApplyContributions = 0u;
            u64 applyAlphaLookupMissing = 0u;
            double applyContributionLuminance = 0.0;
        } frameAudit;

        constexpr u32 kInvalidAuditIndex = std::numeric_limits<u32>::max();
        std::vector<const HybridReflectionInstanceAuditRecord*> instanceByTlas(
            kMaxHybridReflectionInstances,
            nullptr
        );
        std::unordered_map<u32, const HybridReflectionInstanceAuditRecord*>
            objectByAuditId;
        objectByAuditId.reserve(instances.size());
        for (const HybridReflectionInstanceAuditRecord& instance : instances) {
            if (instance.reflectionAuditObjectId != 0u) {
                objectByAuditId[instance.reflectionAuditObjectId] = &instance;
            }
            if (instance.inTlas != 0u &&
                instance.tlasIndex < instanceByTlas.size()) {
                instanceByTlas[instance.tlasIndex] = &instance;
            }
        }
        std::vector<DerivedObjectCounters> derivedByTlas(
            kMaxHybridReflectionInstances
        );
        std::unordered_map<u64, PairCounters> pairCounters;
        pairCounters.reserve(recordCount / 8u + 1u);
        std::vector<u32> rayAtPixel(
            static_cast<std::size_t>(extent.width) * extent.height,
            kInvalidAuditIndex
        );
        std::vector<u32> applyAtPixel(rayAtPixel.size(), kInvalidAuditIndex);
        std::vector<double> applyContributionLuminance(
            applyRecordCount,
            0.0
        );

        auto raySource = [&](u32 flags) {
            const bool screenAccepted = (flags & (1u << 2u)) != 0u;
            const bool productionTrace = (flags & (1u << 3u)) != 0u;
            const bool committedHit = (flags & (1u << 4u)) != 0u;
            return screenAccepted ? 1u : productionTrace
                ? committedHit ? 2u : 3u
                : 0u;
        };
        auto computeLuminance = [](f32 red, f32 green, f32 blue) {
            return 0.2126 * static_cast<double>(red) +
                0.7152 * static_cast<double>(green) +
                0.0722 * static_cast<double>(blue);
        };

        for (u32 rayIndex = 0u; rayIndex < recordCount; ++rayIndex) {
            const u32 base = kFullAuditRayRecordBase +
                rayIndex * kFullAuditRayRecordWordCount;
            const u32 packedCoordinates = values[base + 0u];
            const u32 flags = values[base + 1u];
            const bool screenAccepted = (flags & (1u << 2u)) != 0u;
            const bool productionTrace = (flags & (1u << 3u)) != 0u;
            const bool committedHit = (flags & (1u << 4u)) != 0u;
            const bool attributesResolved = (flags & (1u << 5u)) != 0u;
            const bool materialResolved = (flags & (1u << 6u)) != 0u;
            const bool lightingResolved = (flags & (1u << 7u)) != 0u;
            const bool denoiserWrite = (flags & (1u << 8u)) != 0u;
            const bool receiverResolved = (flags & (1u << 9u)) != 0u;
            const bool auditOnly = (flags & (1u << 10u)) != 0u;
            const bool objectIdValid = (flags & (1u << 11u)) != 0u;
            const bool objectIdMapped = (flags & (1u << 12u)) != 0u;
            const bool selfHit = (flags & (1u << 13u)) != 0u;
            const bool negativeHemisphere = (flags & (1u << 14u)) != 0u;
            const u32 receiverTlasIndex = values[base + 2u];
            const u32 hitTlasIndex = values[base + 4u];
            const u32 receiverObjectId = values[base + 20u];
            const u32 pixelX = packedCoordinates & 0xffffu;
            const u32 pixelY = packedCoordinates >> 16u;
            const bool stageChainValid =
                (!attributesResolved || committedHit) &&
                (!materialResolved || attributesResolved) &&
                (!lightingResolved || materialResolved) &&
                (!denoiserWrite || lightingResolved) &&
                (!screenAccepted || auditOnly) &&
                (!productionTrace || !auditOnly);
            if (!stageChainValid) {
                ++frameAudit.invalidStageChains;
            }
            if (selfHit) {
                ++frameAudit.selfHits;
            }
            if (negativeHemisphere) {
                if (raySource(flags) == 0u) {
                    ++frameAudit.negativeHemisphereRejected;
                } else {
                    ++frameAudit.negativeHemisphere;
                }
            }

            const HybridReflectionInstanceAuditRecord* receiverObject = nullptr;
            const auto objectIt = objectByAuditId.find(receiverObjectId);
            if (!objectIdValid || receiverObjectId == 0u) {
                ++frameAudit.missingObjectIds;
            } else if (objectIt == objectByAuditId.end()) {
                ++frameAudit.unknownObjectIds;
            } else {
                receiverObject = objectIt->second;
                ++frameAudit.sceneReceiverIdentityResolved;
                const bool expectedInTlas = receiverObject->inTlas != 0u;
                if (expectedInTlas) {
                    ++frameAudit.tlasEligibleReceiverRays;
                }
                const bool mappingMatches = expectedInTlas
                    ? objectIdMapped && receiverResolved &&
                        receiverTlasIndex == receiverObject->tlasIndex &&
                        receiverTlasIndex < instanceByTlas.size() &&
                        instanceByTlas[receiverTlasIndex] != nullptr &&
                        instanceByTlas[receiverTlasIndex]
                            ->reflectionAuditObjectId == receiverObjectId
                    : !objectIdMapped && !receiverResolved;
                if (mappingMatches && expectedInTlas) {
                    ++frameAudit.tlasEligibleReceiverResolved;
                }
                if (!mappingMatches) {
                    ++frameAudit.receiverTruthMismatches;
                }
                if (expectedInTlas && (flags & (1u << 1u)) != 0u) {
                    const f32 originBias = std::bit_cast<f32>(values[base + 21u]);
                    const f32 padding = std::max(0.05f, std::abs(originBias) * 4.0f);
                    const glm::vec3 origin(
                        std::bit_cast<f32>(values[base + 9u]),
                        std::bit_cast<f32>(values[base + 10u]),
                        std::bit_cast<f32>(values[base + 11u])
                    );
                    const bool inside =
                        origin.x >= receiverObject->boundsMin[0] - padding &&
                        origin.x <= receiverObject->boundsMax[0] + padding &&
                        origin.y >= receiverObject->boundsMin[1] - padding &&
                        origin.y <= receiverObject->boundsMax[1] + padding &&
                        origin.z >= receiverObject->boundsMin[2] - padding &&
                        origin.z <= receiverObject->boundsMax[2] + padding;
                    if (!inside) {
                        ++frameAudit.receiverOriginsOutsideBounds;
                    }
                }
            }

            if (committedHit) {
                if (hitTlasIndex >= instanceByTlas.size() ||
                    instanceByTlas[hitTlasIndex] == nullptr) {
                    ++frameAudit.unknownHits;
                } else {
                    DerivedObjectCounters& incoming = derivedByTlas[hitTlasIndex];
                    ++incoming.incomingHits;
                    if (attributesResolved) {
                        ++incoming.incomingAttributes;
                    }
                    if (denoiserWrite) {
                        ++incoming.incomingDenoiserWrites;
                    }
                }
            }

            const bool resolvedReceiverValid = receiverResolved &&
                receiverTlasIndex != kInvalidAuditIndex;
            if (!resolvedReceiverValid) {
                ++frameAudit.unresolvedReceivers;
            } else if (receiverTlasIndex >= instanceByTlas.size() ||
                instanceByTlas[receiverTlasIndex] == nullptr) {
                ++frameAudit.unknownReceivers;
            } else {
                DerivedObjectCounters& derived = derivedByTlas[receiverTlasIndex];
                ++derived.receiverSamples;
                if (screenAccepted) {
                    ++derived.screenAccepted;
                }
                if (productionTrace) {
                    ++derived.productionTraces;
                    if (committedHit) {
                        ++derived.productionHits;
                    } else {
                        ++derived.productionMisses;
                    }
                }
                if (selfHit) {
                    ++derived.selfHits;
                }
                const f32 normalDotReflection =
                    std::bit_cast<f32>(values[base + 19u]);
                if (raySource(flags) != 0u &&
                    (negativeHemisphere || normalDotReflection < -0.000001f)) {
                    ++derived.negativeHemisphereRays;
                }
                const f32 finalLuminance =
                    std::bit_cast<f32>(values[base + 15u]);
                if (productionTrace && committedHit && finalLuminance < 0.01f) {
                    ++derived.darkRayQueryHits;
                }
                if (committedHit && hitTlasIndex < instanceByTlas.size() &&
                    instanceByTlas[hitTlasIndex] != nullptr) {
                    const u64 pairKey =
                        (static_cast<u64>(receiverTlasIndex) << 32u) |
                        hitTlasIndex;
                    PairCounters& pair = pairCounters[pairKey];
                    ++pair.total;
                    if (screenAccepted) {
                        ++pair.screen;
                    }
                    if (productionTrace) {
                        ++pair.production;
                    }
                    if (attributesResolved) {
                        ++pair.attributes;
                    }
                    if (denoiserWrite) {
                        ++pair.denoiser;
                    }
                }
            }
            if (pixelX < extent.width && pixelY < extent.height) {
                rayAtPixel[static_cast<std::size_t>(pixelY) * extent.width + pixelX] =
                    rayIndex;
            }
            if (fullAuditRawEvidence) {
                const char* source = screenAccepted
                    ? "screen_accepted"
                    : productionTrace
                        ? committedHit ? "ray_query_hit" : "ray_query_miss"
                        : "invalid";
                rayFile << fullAuditFramesWritten << ','
                    << capturedFrameNumber << ',' << rayIndex << ','
                    << (packedCoordinates & 0xffffu) << ','
                    << (packedCoordinates >> 16u) << ','
                    << ((flags >> 16u) & 1u) << ','
                    << ((flags >> 17u) & 1u) << ','
                    << ((flags >> 18u) & 1u) << ',' << source << ',' << flags
                    << ',' << values[base + 2u] << ','
                    << std::bit_cast<f32>(values[base + 3u]) << ','
                    << values[base + 4u] << ',' << values[base + 5u] << ','
                    << std::bit_cast<f32>(values[base + 6u]) << ','
                    << values[base + 7u] << ','
                    << std::bit_cast<f32>(values[base + 8u]);
                for (u32 word = 9u; word <= 19u; ++word) {
                    rayFile << ',' << std::bit_cast<f32>(values[base + word]);
                }
                rayFile << ',' << values[base + 20u];
                for (u32 word = 21u; word <= 23u; ++word) {
                    rayFile << ',' << std::bit_cast<f32>(values[base + word]);
                }
                rayFile << '\n';
            }
        }

        constexpr std::array<std::array<u32, 2>, 4> kRayNeighborOffsets{{
            {{1u, 0u}}, {{2u, 0u}}, {{0u, 1u}}, {{0u, 2u}}
        }};
        for (u32 rayIndex = 0u; rayIndex < recordCount; ++rayIndex) {
            const u32 base = kFullAuditRayRecordBase +
                rayIndex * kFullAuditRayRecordWordCount;
            const u32 coordinates = values[base + 0u];
            const u32 x = coordinates & 0xffffu;
            const u32 y = coordinates >> 16u;
            const u32 receiver = values[base + 2u];
            if (receiver >= instanceByTlas.size() ||
                instanceByTlas[receiver] == nullptr) {
                continue;
            }
            DerivedObjectCounters& quality = derivedByTlas[receiver];
            for (const auto& offset : kRayNeighborOffsets) {
                const u32 neighborX = x + offset[0];
                const u32 neighborY = y + offset[1];
                if (neighborX >= extent.width || neighborY >= extent.height) {
                    continue;
                }
                const u32 neighborIndex = rayAtPixel[
                    static_cast<std::size_t>(neighborY) * extent.width + neighborX
                ];
                if (neighborIndex == kInvalidAuditIndex) {
                    continue;
                }
                const u32 neighborBase = kFullAuditRayRecordBase +
                    neighborIndex * kFullAuditRayRecordWordCount;
                if (values[neighborBase + 2u] != receiver) {
                    continue;
                }
                ++quality.adjacentPairs;
                if (raySource(values[base + 1u]) !=
                    raySource(values[neighborBase + 1u])) {
                    ++quality.sourceTransitions;
                }
                if (values[base + 4u] != values[neighborBase + 4u]) {
                    ++quality.hitIdentityTransitions;
                }
                const f32 luminanceDelta = std::abs(
                    std::bit_cast<f32>(values[base + 15u]) -
                    std::bit_cast<f32>(values[neighborBase + 15u])
                );
                if (luminanceDelta > 0.25f) {
                    ++quality.largeLuminanceJumps;
                }
            }
        }

        for (u32 applyIndex = 0u; applyIndex < applyRecordCount;
            ++applyIndex) {
            const u32 base = kFullAuditApplyRecordBase +
                applyIndex * kFullAuditApplyRecordWordCount;
            const u32 packedCoordinates = values[base + 0u];
            if (fullAuditRawEvidence) {
                applyFile << fullAuditFramesWritten << ','
                    << capturedFrameNumber << ',' << applyIndex << ','
                    << (packedCoordinates & 0xffffu) << ','
                    << (packedCoordinates >> 16u) << ',' << values[base + 1u];
                for (u32 word = 2u; word <= 14u; ++word) {
                    applyFile << ',' << std::bit_cast<f32>(values[base + word]);
                }
                applyFile << ',' << values[base + 15u] << '\n';
            }
            const f32 resolvedR = std::bit_cast<f32>(values[base + 2u]);
            const f32 resolvedG = std::bit_cast<f32>(values[base + 3u]);
            const f32 resolvedB = std::bit_cast<f32>(values[base + 4u]);
            const f32 confidence = std::bit_cast<f32>(values[base + 5u]);
            const f32 probeR = std::bit_cast<f32>(values[base + 6u]);
            const f32 probeG = std::bit_cast<f32>(values[base + 7u]);
            const f32 probeB = std::bit_cast<f32>(values[base + 8u]);
            const f32 blendWeight = std::bit_cast<f32>(values[base + 9u]);
            const f32 contributionR = std::bit_cast<f32>(values[base + 10u]);
            const f32 contributionG = std::bit_cast<f32>(values[base + 11u]);
            const f32 contributionB = std::bit_cast<f32>(values[base + 12u]);
            const bool finite = std::isfinite(resolvedR) &&
                std::isfinite(resolvedG) && std::isfinite(resolvedB) &&
                std::isfinite(confidence) && std::isfinite(probeR) &&
                std::isfinite(probeG) && std::isfinite(probeB) &&
                std::isfinite(blendWeight) && std::isfinite(contributionR) &&
                std::isfinite(contributionG) && std::isfinite(contributionB);
            const bool sourceNegative = resolvedR < 0.0f || resolvedG < 0.0f ||
                resolvedB < 0.0f || probeR < 0.0f || probeG < 0.0f ||
                probeB < 0.0f;
            const bool contributionNegative = contributionR < 0.0f ||
                contributionG < 0.0f || contributionB < 0.0f;
            const u32 objectId = values[base + 1u];
            const u32 pixelX = packedCoordinates & 0xffffu;
            const u32 pixelY = packedCoordinates >> 16u;
            const u32 applyFlags = values[base + 15u];
            const bool allAuditValuesFinite =
                (applyFlags & (1u << 3u)) != 0u &&
                (applyFlags & (1u << 4u)) != 0u &&
                (applyFlags & (1u << 5u)) != 0u;
            if (objectId == 0u) {
                ++frameAudit.applyMissingObjectIds;
            }
            const auto applyObjectIt = objectByAuditId.find(objectId);
            if (objectId != 0u && applyObjectIt == objectByAuditId.end()) {
                ++frameAudit.applyUnknownObjectIds;
            }
            if (!allAuditValuesFinite || !finite) {
                ++frameAudit.applyNonFinite;
            }
            if (!finite) {
                ++frameAudit.gBufferNonFinite;
            }
            if (sourceNegative) {
                ++frameAudit.gBufferSourceNegative;
            }
            if (contributionNegative) {
                ++frameAudit.negativeApplyContributions;
            }
            const double contributionLuminance = computeLuminance(
                contributionR,
                contributionG,
                contributionB
            );
            applyContributionLuminance[applyIndex] = contributionLuminance;
            frameAudit.applyContributionLuminance += contributionLuminance;
            if (contributionLuminance > 0.000001) {
                ++frameAudit.nonzeroApplyContributions;
            }
            if (applyObjectIt != objectByAuditId.end() &&
                applyObjectIt->second->inTlas != 0u &&
                applyObjectIt->second->tlasIndex < derivedByTlas.size()) {
                DerivedObjectCounters& quality =
                    derivedByTlas[applyObjectIt->second->tlasIndex];
                ++quality.applySamples;
                const double resolvedLuminance = computeLuminance(
                    resolvedR,
                    resolvedG,
                    resolvedB
                );
                const double probeLuminance = computeLuminance(
                    probeR,
                    probeG,
                    probeB
                );
                if (std::abs(resolvedLuminance - probeLuminance) > 0.5) {
                    ++quality.applyLargeSourceDisagreements;
                }
            }
            if (pixelX < extent.width && pixelY < extent.height) {
                applyAtPixel[
                    static_cast<std::size_t>(pixelY) * extent.width + pixelX
                ] = applyIndex;
            }
            if (fullAuditRawEvidence) {
                gBufferSamplesFile << fullAuditFramesWritten << ','
                    << capturedFrameNumber << ',' << applyIndex << ','
                    << (packedCoordinates & 0xffffu) << ','
                    << (packedCoordinates >> 16u) << ',' << values[base + 1u]
                    << ',' << std::bit_cast<f32>(values[base + 13u]) << ','
                    << std::bit_cast<f32>(values[base + 14u]) << ','
                    << confidence << ',' << resolvedR << ',' << resolvedG << ','
                    << resolvedB << ',' << probeR << ',' << probeG << ','
                    << probeB << ',' << blendWeight << ',' << contributionR
                    << ',' << contributionG << ',' << contributionB << ','
                    << (finite ? 1u : 0u) << ','
                    << (sourceNegative ? 1u : 0u) << ','
                    << (contributionNegative ? 1u : 0u) << '\n';
            }
        }

        constexpr std::array<std::array<u32, 2>, 2> kApplyNeighborOffsets{{
            {{1u, 0u}}, {{0u, 1u}}
        }};
        for (u32 applyIndex = 0u; applyIndex < applyRecordCount; ++applyIndex) {
            const u32 base = kFullAuditApplyRecordBase +
                applyIndex * kFullAuditApplyRecordWordCount;
            const u32 coordinates = values[base + 0u];
            const u32 x = coordinates & 0xffffu;
            const u32 y = coordinates >> 16u;
            const u32 objectId = values[base + 1u];
            const auto objectIt = objectByAuditId.find(objectId);
            if (objectIt == objectByAuditId.end() ||
                objectIt->second->inTlas == 0u ||
                objectIt->second->tlasIndex >= derivedByTlas.size()) {
                continue;
            }
            DerivedObjectCounters& quality =
                derivedByTlas[objectIt->second->tlasIndex];
            for (const auto& offset : kApplyNeighborOffsets) {
                const u32 neighborX = x + offset[0];
                const u32 neighborY = y + offset[1];
                if (neighborX >= extent.width || neighborY >= extent.height) {
                    continue;
                }
                const u32 neighborIndex = applyAtPixel[
                    static_cast<std::size_t>(neighborY) * extent.width + neighborX
                ];
                if (neighborIndex == kInvalidAuditIndex) {
                    continue;
                }
                const u32 neighborBase = kFullAuditApplyRecordBase +
                    neighborIndex * kFullAuditApplyRecordWordCount;
                if (values[neighborBase + 1u] != objectId) {
                    continue;
                }
                ++quality.applyAdjacentPairs;
                const f32 roughness =
                    std::bit_cast<f32>(values[base + 13u]);
                const f32 neighborRoughness =
                    std::bit_cast<f32>(values[neighborBase + 13u]);
                const f32 confidence =
                    std::bit_cast<f32>(values[base + 5u]);
                const f32 neighborConfidence =
                    std::bit_cast<f32>(values[neighborBase + 5u]);
                const f32 blendWeight =
                    std::bit_cast<f32>(values[base + 9u]);
                const f32 neighborBlendWeight =
                    std::bit_cast<f32>(values[neighborBase + 9u]);
                const f32 roughnessDelta =
                    std::abs(roughness - neighborRoughness);
                const f32 confidenceDelta =
                    std::abs(confidence - neighborConfidence);
                const f32 blendDelta =
                    std::abs(blendWeight - neighborBlendWeight);
                const double contributionDelta = std::abs(
                    applyContributionLuminance[applyIndex] -
                    applyContributionLuminance[neighborIndex]
                );
                if (roughnessDelta < 0.05f && blendDelta > 0.35f) {
                    ++quality.applyBlendDiscontinuities;
                    if (confidenceDelta > 0.35f) {
                        ++quality.applyConfidenceDiscontinuities;
                    } else {
                        ++quality.applyUnexplainedBlendDiscontinuities;
                    }
                    if (contributionDelta > 0.25) {
                        ++quality.applyBlendContributionJumps;
                    }
                    if (fullAuditRawEvidence) {
                        const auto luminanceAt = [&](u32 recordBase, u32 offset) {
                            return computeLuminance(
                                std::bit_cast<f32>(values[recordBase + offset]),
                                std::bit_cast<f32>(values[recordBase + offset + 1u]),
                                std::bit_cast<f32>(values[recordBase + offset + 2u])
                            );
                        };
                        runtimeApplyDiscontinuityFile << fullAuditFramesWritten
                            << ',' << capturedFrameNumber << ','
                            << objectIt->second->tlasIndex << ',' << objectId
                            << ',' << CsvEscape(objectIt->second->renderableName)
                            << ',' << x << ',' << y << ',' << neighborX << ','
                            << neighborY << ','
                            << (offset[0] != 0u ? "x" : "y") << ','
                            << roughness << ',' << neighborRoughness << ','
                            << confidence << ',' << neighborConfidence << ','
                            << blendWeight << ',' << neighborBlendWeight << ','
                            << luminanceAt(base, 2u) << ','
                            << luminanceAt(neighborBase, 2u) << ','
                            << luminanceAt(base, 6u) << ','
                            << luminanceAt(neighborBase, 6u) << ','
                            << applyContributionLuminance[applyIndex] << ','
                            << applyContributionLuminance[neighborIndex] << ','
                            << roughnessDelta << ',' << confidenceDelta << ','
                            << blendDelta << ',' << contributionDelta << ','
                            << values[base + 15u] << ','
                            << values[neighborBase + 15u] << '\n';
                    }
                }
                if (contributionDelta > 0.25) {
                    ++quality.applyContributionJumps;
                }
            }
        }

        if (metadata != nullptr) {
            for (const HybridReflectionFullAuditLightRecord& light : metadata->lights) {
                lightsFile << fullAuditFramesWritten << ',' << capturedFrameNumber
                    << ',' << light.index << ',' << light.kind << ','
                    << light.enabled << ',' << light.castsShadow << ','
                    << light.positionRadius.x << ',' << light.positionRadius.y
                    << ',' << light.positionRadius.z << ','
                    << light.positionRadius.w << ',' << light.colorIntensity.x
                    << ',' << light.colorIntensity.y << ','
                    << light.colorIntensity.z << ',' << light.colorIntensity.w
                    << ',' << light.directionAndCones.x << ','
                    << light.directionAndCones.y << ','
                    << light.directionAndCones.z << ','
                    << light.directionAndCones.w << ',' << light.outerConeCos
                    << ','
                    << light.shapeAndSpecular.x << ','
                    << light.shapeAndSpecular.y << ','
                    << light.shapeAndSpecular.z << ','
                    << light.shapeAndSpecular.w << ','
                    << light.requestedShadowTiles << ','
                    << light.assignedShadowTiles << ','
                    << light.firstShadowTile << ','
                    << light.shadowTileRangeValid << '\n';
            }
            for (const HybridReflectionFullAuditProbeRecord& probe : metadata->probes) {
                probesFile << fullAuditFramesWritten << ',' << capturedFrameNumber
                    << ',' << probe.index << ',' << probe.sceneIndex << ','
                    << probe.enabled << ',' << probe.sceneOwned << ','
                    << probe.captureSource << ',' << probe.refreshPolicy << ','
                    << probe.captureResourceReady << ','
                    << probe.captureDescriptorBound << ','
                    << probe.fallbackReason << ',' << probe.captureSlot << ','
                    << probe.captureMipCount << ',' << probe.centerRadius.x
                    << ',' << probe.centerRadius.y << ',' << probe.centerRadius.z
                    << ',' << probe.centerRadius.w << ',' << probe.boxExtents.x
                    << ',' << probe.boxExtents.y << ',' << probe.boxExtents.z
                    << ',' << probe.colorIntensity.x << ','
                    << probe.colorIntensity.y << ',' << probe.colorIntensity.z
                    << ',' << probe.colorIntensity.w << ','
                    << probe.blendFalloffProjection.x << ','
                    << probe.blendFalloffProjection.y << ','
                    << probe.blendFalloffProjection.z << ','
                    << probe.blendFalloffProjection.w << '\n';
            }
            for (const HybridReflectionFullAuditQueueCommandRecord& command : metadata->commands) {
                queueFile << fullAuditFramesWritten << ',' << capturedFrameNumber
                    << ',' << command.queueKind << ',' << command.commandIndex
                    << ',' << command.submissionIndex << ','
                    << command.reflectionAuditObjectId << ','
                    << command.renderIdentity << ',' << command.meshIdentity
                    << ',' << command.materialIdentity << ','
                    << command.materialIndex << ',' << command.drawOrder << ','
                    << command.lodLevel << ',' << command.castShadow << ','
                    << command.reflectionCaptureVisible << ','
                    << command.skinnedBoundsConservative << ','
                    << command.bonePaletteReady << ','
                    << command.bonePaletteDescriptorReady << ','
                    << command.worldBoundsValid << ',' << command.boundsMin.x
                    << ',' << command.boundsMin.y << ',' << command.boundsMin.z
                    << ',' << command.boundsMax.x << ',' << command.boundsMax.y
                    << ',' << command.boundsMax.z << ','
                    << CsvEscape(command.renderableName) << '\n';
            }
        }

        for (const HybridReflectionInstanceAuditRecord& instance : instances) {
            if (instance.inTlas == 0u ||
                instance.tlasIndex >= derivedByTlas.size()) {
                continue;
            }
            const DerivedObjectCounters& derived =
                derivedByTlas[instance.tlasIndex];
            const u32 counterBase = kFullAuditInstanceCounterBase +
                instance.tlasIndex * kFullAuditInstanceCounterCount;
            const std::array<u64, 5> observedReceiver{{
                derived.receiverSamples,
                derived.screenAccepted,
                derived.productionTraces,
                derived.productionHits,
                derived.productionMisses
            }};
            const std::array<u64, 5> gpuReceiver{{
                values[counterBase + 0u],
                values[counterBase + 1u],
                values[counterBase + 2u],
                values[counterBase + 3u],
                values[counterBase + 7u]
            }};
            const std::array<u64, 3> observedIncoming{{
                derived.incomingHits,
                derived.incomingAttributes,
                derived.incomingDenoiserWrites
            }};
            const std::array<u64, 3> gpuIncoming{{
                values[counterBase + 4u],
                values[counterBase + 5u],
                values[counterBase + 6u]
            }};
            if (observedReceiver != gpuReceiver) {
                ++frameAudit.receiverCounterMismatches;
            }
            if (observedIncoming != gpuIncoming) {
                ++frameAudit.incomingCounterMismatches;
            }
            runtimeObjectFile << fullAuditFramesWritten << ','
                << capturedFrameNumber << ',' << instance.tlasIndex << ','
                << instance.submissionIndex << ','
                << instance.reflectionAuditObjectId << ','
                << CsvEscape(instance.renderableName) << ','
                << instance.materialIndex << ',' << instance.metallic << ','
                << instance.roughness << ',' << observedReceiver[0] << ','
                << observedReceiver[1] << ',' << observedReceiver[2] << ','
                << observedReceiver[3] << ',' << observedReceiver[4] << ','
                << observedIncoming[0] << ',' << observedIncoming[1] << ','
                << observedIncoming[2] << ',' << gpuReceiver[0] << ','
                << gpuReceiver[1] << ',' << gpuReceiver[2] << ','
                << gpuReceiver[3] << ',' << gpuReceiver[4] << ','
                << gpuIncoming[0] << ',' << gpuIncoming[1] << ','
                << gpuIncoming[2] << '\n';
            runtimeQualityFile << fullAuditFramesWritten << ','
                << capturedFrameNumber << ',' << instance.tlasIndex << ','
                << instance.reflectionAuditObjectId << ','
                << CsvEscape(instance.renderableName) << ','
                << derived.receiverSamples << ',' << derived.screenAccepted
                << ',' << derived.productionHits << ','
                << derived.productionMisses << ',' << derived.selfHits << ','
                << derived.negativeHemisphereRays << ','
                << derived.darkRayQueryHits << ',' << derived.adjacentPairs
                << ',' << derived.sourceTransitions << ','
                << derived.hitIdentityTransitions << ','
                << derived.largeLuminanceJumps << ',' << derived.applySamples
                << ',' << derived.applyAdjacentPairs << ','
                << derived.applyBlendDiscontinuities << ','
                << derived.applyConfidenceDiscontinuities << ','
                << derived.applyUnexplainedBlendDiscontinuities << ','
                << derived.applyBlendContributionJumps << ','
                << derived.applyContributionJumps << ','
                << derived.applyLargeSourceDisagreements << '\n';
        }
        std::vector<u64> sortedPairKeys;
        sortedPairKeys.reserve(pairCounters.size());
        for (const auto& [pairKey, pair] : pairCounters) {
            static_cast<void>(pair);
            sortedPairKeys.push_back(pairKey);
        }
        std::sort(sortedPairKeys.begin(), sortedPairKeys.end());
        for (const u64 pairKey : sortedPairKeys) {
            const PairCounters& pair = pairCounters.at(pairKey);
            const u32 receiver = static_cast<u32>(pairKey >> 32u);
            const u32 hit = static_cast<u32>(pairKey & 0xffffffffu);
            if (receiver >= instanceByTlas.size() ||
                hit >= instanceByTlas.size() ||
                instanceByTlas[receiver] == nullptr ||
                instanceByTlas[hit] == nullptr) {
                continue;
            }
            const HybridReflectionInstanceAuditRecord& receiverInstance =
                *instanceByTlas[receiver];
            const HybridReflectionInstanceAuditRecord& hitInstance =
                *instanceByTlas[hit];
            runtimePairFile << fullAuditFramesWritten << ','
                << capturedFrameNumber << ',' << receiver << ','
                << receiverInstance.reflectionAuditObjectId << ','
                << CsvEscape(receiverInstance.renderableName) << ',' << hit
                << ',' << hitInstance.reflectionAuditObjectId << ','
                << CsvEscape(hitInstance.renderableName) << ',' << pair.total
                << ',' << pair.screen << ',' << pair.production << ','
                << pair.attributes << ',' << pair.denoiser << '\n';
        }

        std::vector<u32> objectIds;
        std::string objectIdSnapshotName;
        u64 objectIdSnapshotBytes = 0u;
        if (fullAuditObjectIdRecorded[imageIndex]) {
            objectIds.resize(static_cast<std::size_t>(extent.width) * extent.height);
            fullAuditObjectIdReadbackBuffers[imageIndex]->Download(
                std::as_writable_bytes(std::span<u32>(objectIds))
            );
            objectIdSnapshotName = "object_ids_capture_" +
                std::to_string(fullAuditFramesWritten) + ".bin";
            const std::filesystem::path objectIdSnapshotPath =
                fullAuditOutputDirectory / objectIdSnapshotName;
            std::ofstream objectIdSnapshotFile(
                objectIdSnapshotPath,
                std::ios::out | std::ios::binary | std::ios::trunc
            );
            if (!objectIdSnapshotFile) {
                throw std::runtime_error(
                    "Failed to open full-audit object-ID snapshot"
                );
            }
            objectIdSnapshotBytes = static_cast<u64>(objectIds.size()) *
                sizeof(u32);
            objectIdSnapshotFile.write(
                reinterpret_cast<const char*>(objectIds.data()),
                static_cast<std::streamsize>(objectIdSnapshotBytes)
            );
            if (!objectIdSnapshotFile) {
                throw std::runtime_error(
                    "Failed to write full-audit object-ID snapshot"
                );
            }
        }
        std::array<double, kHybridReflectionFullAuditImageStageCount>
            stageLuminanceSums{};
        stageLuminanceSums.fill(std::numeric_limits<double>::quiet_NaN());
        std::array<std::vector<f32>, 3> dnsrConfidenceStages;
        const auto dnsrConfidenceSlot = [](u32 stageIndex) {
            if (stageIndex == static_cast<u32>(
                    HybridReflectionFullAuditImageStage::IntersectConfidence
                )) {
                return 0;
            }
            if (stageIndex == static_cast<u32>(
                    HybridReflectionFullAuditImageStage::ReprojectConfidence
                )) {
                return 1;
            }
            if (stageIndex == static_cast<u32>(
                    HybridReflectionFullAuditImageStage::ResolveConfidence
                )) {
                return 2;
            }
            return -1;
        };
        std::vector<f32> beforeApplyAlpha;
        double beforeApplyAlphaSum = 0.0;
        double beforeApplyAlphaMin = 0.0;
        double beforeApplyAlphaMax = 0.0;
        u64 beforeApplyZeroAlphaCount = 0u;
        u64 beforeApplyOneAlphaCount = 0u;
        for (u32 stageIndex = 0u;
             stageIndex < kHybridReflectionFullAuditImageStageCount;
             ++stageIndex) {
            if (!fullAuditImageRecorded[stageIndex][imageIndex] ||
                fullAuditImageReadbackBuffers[stageIndex].size() <= imageIndex) {
                continue;
            }
            const VkExtent2D snapshotExtent = fullAuditImageExtents[stageIndex];
            const VkFormat snapshotFormat = fullAuditImageFormats[stageIndex];
            const bool float32Rgba =
                snapshotFormat == VK_FORMAT_R32G32B32A32_SFLOAT;
            const bool halfFloat =
                snapshotFormat == VK_FORMAT_R16G16B16A16_SFLOAT;
            const bool float32Scalar =
                snapshotFormat == VK_FORMAT_R32_SFLOAT;
            const VkDeviceSize pixelBytes =
                FullAuditFormatPixelBytes(snapshotFormat);
            const std::size_t pixelCount = static_cast<std::size_t>(
                snapshotExtent.width
            ) * snapshotExtent.height;
            const int confidenceSlot = dnsrConfidenceSlot(stageIndex);
            if (confidenceSlot >= 0) {
                dnsrConfidenceStages[static_cast<std::size_t>(confidenceSlot)]
                    .resize(pixelCount, 0.0f);
            }
            const VkDeviceSize byteCount = static_cast<VkDeviceSize>(pixelCount) *
                pixelBytes;
            std::vector<std::byte> pixels(static_cast<std::size_t>(byteCount));
            fullAuditImageReadbackBuffers[stageIndex][imageIndex]->Download(
                pixels
            );
            const std::string stageName = FullAuditImageStageName(
                static_cast<HybridReflectionFullAuditImageStage>(stageIndex)
            );
            std::string snapshotName;
            u64 snapshotBytes = 0u;
            if (fullAuditRawEvidence) {
                snapshotName = "image_capture_" +
                    std::to_string(fullAuditFramesWritten) + "_" + stageName +
                    ".bin";
                std::ofstream snapshotFile(
                    fullAuditOutputDirectory / snapshotName,
                    std::ios::out | std::ios::binary | std::ios::trunc
                );
                if (!snapshotFile) {
                    throw std::runtime_error(
                        "Failed to open full-audit image snapshot"
                    );
                }
                snapshotFile.write(
                    reinterpret_cast<const char*>(pixels.data()),
                    static_cast<std::streamsize>(pixels.size())
                );
                if (!snapshotFile) {
                    throw std::runtime_error(
                        "Failed to write full-audit image snapshot"
                    );
                }
                snapshotBytes = static_cast<u64>(pixels.size());
            }
            imageSnapshotManifestFile << fullAuditFramesWritten << ','
                << capturedFrameNumber << ',' << stageName << ','
                << stageIndex << ',' << imageIndex << ','
                << snapshotExtent.width << ',' << snapshotExtent.height << ','
                << static_cast<u32>(snapshotFormat) << ',' << pixelBytes << ','
                << CsvEscape(snapshotName) << ',' << snapshotBytes << ','
                << CsvEscape(objectIdSnapshotName) << ','
                << objectIdSnapshotBytes << ','
                << (fullAuditVerbosePixelCsv ? 1u : 0u) << ','
                << (fullAuditRawEvidence ? 1u : 0u) << '\n';
            u64 finiteCount = 0u;
            u64 nonFiniteCount = 0u;
            u64 negativeCount = 0u;
            double luminanceSum = 0.0;
            double luminanceMin = std::numeric_limits<double>::infinity();
            double luminanceMax = -std::numeric_limits<double>::infinity();
            double alphaSum = 0.0;
            double alphaMin = std::numeric_limits<double>::infinity();
            double alphaMax = -std::numeric_limits<double>::infinity();
            u64 zeroAlphaCount = 0u;
            u64 oneAlphaCount = 0u;
            if (stageIndex == static_cast<u32>(
                    HybridReflectionFullAuditImageStage::DeferredHdrBeforeApply
                )) {
                beforeApplyAlpha.resize(pixelCount, 0.0f);
            }
            for (u32 y = 0u; y < snapshotExtent.height; ++y) {
                for (u32 x = 0u; x < snapshotExtent.width; ++x) {
                    const std::size_t pixelIndex = static_cast<std::size_t>(y) *
                        snapshotExtent.width + x;
                    const u8* raw = reinterpret_cast<const u8*>(
                        pixels.data() + pixelIndex * pixelBytes
                    );
                    f32 channels[4]{};
                    if (float32Rgba) {
                        std::memcpy(channels, raw, sizeof(channels));
                    } else if (halfFloat) {
                        for (u32 channel = 0u; channel < 4u; ++channel) {
                            u16 half = 0u;
                            std::memcpy(
                                &half,
                                raw + channel * sizeof(u16),
                                sizeof(u16)
                            );
                            channels[channel] = HalfToFloat(half);
                        }
                    } else if (float32Scalar) {
                        f32 value = 0.0f;
                        std::memcpy(&value, raw, sizeof(value));
                        channels[0] = value;
                        channels[1] = value;
                        channels[2] = value;
                        channels[3] = 1.0f;
                    } else {
                        const bool bgra =
                            snapshotFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                            snapshotFormat == VK_FORMAT_B8G8R8A8_UNORM;
                        channels[0] = static_cast<f32>(raw[bgra ? 2u : 0u]) / 255.0f;
                        channels[1] = static_cast<f32>(raw[1u]) / 255.0f;
                        channels[2] = static_cast<f32>(raw[bgra ? 0u : 2u]) / 255.0f;
                        channels[3] = static_cast<f32>(raw[3u]) / 255.0f;
                    }
                    if (confidenceSlot >= 0) {
                        dnsrConfidenceStages[
                            static_cast<std::size_t>(confidenceSlot)
                        ][pixelIndex] = channels[0];
                    }
                    const bool finite = std::isfinite(channels[0]) &&
                        std::isfinite(channels[1]) &&
                        std::isfinite(channels[2]) &&
                        std::isfinite(channels[3]);
                    const bool negative = channels[0] < 0.0f ||
                        channels[1] < 0.0f || channels[2] < 0.0f;
                    const double luminance =
                        0.2126 * channels[0] + 0.7152 * channels[1] +
                        0.0722 * channels[2];
                    if (finite) {
                        ++finiteCount;
                        luminanceSum += luminance;
                        luminanceMin = std::min(luminanceMin, luminance);
                        luminanceMax = std::max(luminanceMax, luminance);
                        alphaSum += channels[3];
                        alphaMin = std::min(
                            alphaMin,
                            static_cast<double>(channels[3])
                        );
                        alphaMax = std::max(
                            alphaMax,
                            static_cast<double>(channels[3])
                        );
                        if (std::abs(channels[3]) <= 0.000001f) {
                            ++zeroAlphaCount;
                        }
                        if (std::abs(channels[3] - 1.0f) <= 0.000001f) {
                            ++oneAlphaCount;
                        }
                    } else {
                        ++nonFiniteCount;
                    }
                    if (negative) {
                        ++negativeCount;
                    }
                    if (!beforeApplyAlpha.empty() &&
                        stageIndex == static_cast<u32>(
                            HybridReflectionFullAuditImageStage::DeferredHdrBeforeApply
                        )) {
                        beforeApplyAlpha[pixelIndex] = channels[3];
                    }
                    u32 objectId = 0u;
                    if (pixelIndex < objectIds.size() &&
                        stageIndex <= static_cast<u32>(
                            HybridReflectionFullAuditImageStage::TemporalInput
                        )) {
                        objectId = objectIds[pixelIndex];
                    }
                    if (fullAuditVerbosePixelCsv) {
                        compositionFile << fullAuditFramesWritten << ','
                            << capturedFrameNumber << ',' << stageName << ','
                            << stageIndex << ',' << imageIndex << ','
                            << snapshotExtent.width << ',' << snapshotExtent.height
                            << ',' << static_cast<u32>(snapshotFormat) << ','
                            << objectId << ',' << x << ',' << y << ','
                            << channels[0] << ',' << channels[1] << ','
                            << channels[2] << ',' << channels[3] << ','
                            << (finite ? 1u : 0u) << ',' << (negative ? 1u : 0u)
                            << '\n';
                    }
                }
            }
            if (finiteCount == 0u) {
                luminanceMin = 0.0;
                luminanceMax = 0.0;
                alphaMin = 0.0;
                alphaMax = 0.0;
            }
            stageLuminanceSums[stageIndex] = luminanceSum;
            if (stageIndex == static_cast<u32>(
                    HybridReflectionFullAuditImageStage::DeferredHdrBeforeApply
                )) {
                beforeApplyAlphaSum = alphaSum;
                beforeApplyAlphaMin = alphaMin;
                beforeApplyAlphaMax = alphaMax;
                beforeApplyZeroAlphaCount = zeroAlphaCount;
                beforeApplyOneAlphaCount = oneAlphaCount;
            }
            compositionSummaryFile << fullAuditFramesWritten << ','
                << capturedFrameNumber << ','
                << FullAuditImageStageName(
                    static_cast<HybridReflectionFullAuditImageStage>(stageIndex)
                ) << ',' << stageIndex << ',' << imageIndex << ','
                << snapshotExtent.width << ',' << snapshotExtent.height << ','
                << static_cast<u32>(snapshotFormat) << ',' << pixelCount << ','
                << finiteCount << ',' << nonFiniteCount << ','
                << negativeCount << ',' << luminanceSum << ','
                << luminanceMin << ',' << luminanceMax << ',' << alphaSum
                << ',' << alphaMin << ',' << alphaMax << ','
                << zeroAlphaCount << ',' << oneAlphaCount << '\n';
        }

        const bool dnsrConfidenceReady = !objectIds.empty() &&
            std::all_of(
                dnsrConfidenceStages.begin(),
                dnsrConfidenceStages.end(),
                [&](const std::vector<f32>& values) {
                    return values.size() == objectIds.size();
                }
            );
        if (dnsrConfidenceReady) {
            u32 maxObjectId = 0u;
            for (const auto& [objectId, instance] : objectByAuditId) {
                static_cast<void>(instance);
                maxObjectId = std::max(maxObjectId, objectId);
            }
            struct ConfidenceQualityCounters {
                u64 samples = 0u;
                double sum = 0.0;
                f32 minimum = std::numeric_limits<f32>::infinity();
                f32 maximum = -std::numeric_limits<f32>::infinity();
                u64 adjacentPairs = 0u;
                u64 spatialDiscontinuities = 0u;
            };
            const std::array<const char*, 3> confidenceStageNames{
                "dnsr_intersect_confidence",
                "dnsr_reproject_confidence",
                "dnsr_resolve_confidence"
            };
            const std::array<u32, 3> confidenceStageIndices{
                static_cast<u32>(
                    HybridReflectionFullAuditImageStage::IntersectConfidence
                ),
                static_cast<u32>(
                    HybridReflectionFullAuditImageStage::ReprojectConfidence
                ),
                static_cast<u32>(
                    HybridReflectionFullAuditImageStage::ResolveConfidence
                )
            };
            for (std::size_t stage = 0u;
                 stage < dnsrConfidenceStages.size();
                 ++stage) {
                const std::vector<f32>& confidence =
                    dnsrConfidenceStages[stage];
                std::vector<ConfidenceQualityCounters> quality(
                    static_cast<std::size_t>(maxObjectId) + 1u
                );
                for (std::size_t pixelIndex = 0u;
                     pixelIndex < confidence.size();
                     ++pixelIndex) {
                    const u32 objectId = objectIds[pixelIndex];
                    if (objectId == 0u || objectId > maxObjectId ||
                        objectByAuditId.find(objectId) == objectByAuditId.end()) {
                        continue;
                    }
                    ConfidenceQualityCounters& counters = quality[objectId];
                    const f32 value = confidence[pixelIndex];
                    ++counters.samples;
                    counters.sum += value;
                    counters.minimum = std::min(counters.minimum, value);
                    counters.maximum = std::max(counters.maximum, value);
                    const u32 x = static_cast<u32>(pixelIndex % extent.width);
                    const u32 y = static_cast<u32>(pixelIndex / extent.width);
                    const auto countNeighbor = [&](std::size_t neighborIndex) {
                        if (objectIds[neighborIndex] != objectId) {
                            return;
                        }
                        ++counters.adjacentPairs;
                        if (std::abs(
                                value - confidence[neighborIndex]
                            ) > 0.35f) {
                            ++counters.spatialDiscontinuities;
                        }
                    };
                    if (x + 1u < extent.width) {
                        countNeighbor(pixelIndex + 1u);
                    }
                    if (y + 1u < extent.height) {
                        countNeighbor(pixelIndex + extent.width);
                    }
                }
                for (u32 objectId = 1u; objectId <= maxObjectId; ++objectId) {
                    const ConfidenceQualityCounters& counters = quality[objectId];
                    const auto objectIt = objectByAuditId.find(objectId);
                    if (counters.samples == 0u ||
                        objectIt == objectByAuditId.end()) {
                        continue;
                    }
                    const HybridReflectionInstanceAuditRecord& instance =
                        *objectIt->second;
                    runtimeDnsrConfidenceQualityFile << fullAuditFramesWritten
                        << ',' << capturedFrameNumber << ','
                        << confidenceStageNames[stage] << ','
                        << confidenceStageIndices[stage] << ','
                        << instance.tlasIndex << ',' << objectId << ','
                        << CsvEscape(instance.renderableName) << ','
                        << counters.samples << ','
                        << counters.sum / static_cast<double>(counters.samples)
                        << ',' << counters.minimum << ',' << counters.maximum
                        << ',' << counters.adjacentPairs << ','
                        << counters.spatialDiscontinuities << ','
                        << (counters.adjacentPairs > 0u
                            ? static_cast<double>(
                                counters.spatialDiscontinuities
                            ) / static_cast<double>(counters.adjacentPairs)
                            : 0.0)
                        << '\n';
                }
            }

            struct ConfidenceTransitionCounters {
                u64 samples = 0u;
                u64 largeTransitions = 0u;
                u64 increases = 0u;
                u64 decreases = 0u;
                double absoluteDeltaSum = 0.0;
                f32 absoluteDeltaMaximum = 0.0f;
            };
            for (std::size_t transition = 0u; transition < 2u; ++transition) {
                const std::vector<f32>& source =
                    dnsrConfidenceStages[transition];
                const std::vector<f32>& destination =
                    dnsrConfidenceStages[transition + 1u];
                std::vector<ConfidenceTransitionCounters> transitions(
                    static_cast<std::size_t>(maxObjectId) + 1u
                );
                for (std::size_t pixelIndex = 0u;
                     pixelIndex < source.size();
                     ++pixelIndex) {
                    const u32 objectId = objectIds[pixelIndex];
                    if (objectId == 0u || objectId > maxObjectId ||
                        objectByAuditId.find(objectId) == objectByAuditId.end()) {
                        continue;
                    }
                    ConfidenceTransitionCounters& counters =
                        transitions[objectId];
                    const f32 delta =
                        destination[pixelIndex] - source[pixelIndex];
                    const f32 absoluteDelta = std::abs(delta);
                    ++counters.samples;
                    counters.absoluteDeltaSum += absoluteDelta;
                    counters.absoluteDeltaMaximum = std::max(
                        counters.absoluteDeltaMaximum,
                        absoluteDelta
                    );
                    if (absoluteDelta > 0.35f) {
                        ++counters.largeTransitions;
                        if (delta > 0.0f) {
                            ++counters.increases;
                        } else {
                            ++counters.decreases;
                        }
                    }
                }
                for (u32 objectId = 1u; objectId <= maxObjectId; ++objectId) {
                    const ConfidenceTransitionCounters& counters =
                        transitions[objectId];
                    const auto objectIt = objectByAuditId.find(objectId);
                    if (counters.samples == 0u ||
                        objectIt == objectByAuditId.end()) {
                        continue;
                    }
                    const HybridReflectionInstanceAuditRecord& instance =
                        *objectIt->second;
                    runtimeDnsrConfidenceTransitionFile << fullAuditFramesWritten
                        << ',' << capturedFrameNumber << ','
                        << instance.tlasIndex << ',' << objectId << ','
                        << CsvEscape(instance.renderableName) << ','
                        << confidenceStageNames[transition] << ','
                        << confidenceStageNames[transition + 1u] << ','
                        << counters.samples << ','
                        << counters.largeTransitions << ','
                        << counters.increases << ',' << counters.decreases << ','
                        << counters.absoluteDeltaSum /
                            static_cast<double>(counters.samples)
                        << ',' << counters.absoluteDeltaMaximum << '\n';
                }
            }
        }

        const ColorBlendMode applyBlendMode = FidelityFxSssrApplyBlendMode();
        double blendExpectedLuminance = 0.0;
        for (u32 applyIndex = 0u; applyIndex < applyRecordCount; ++applyIndex) {
            double blendFactor = 1.0;
            if (applyBlendMode == ColorBlendMode::DestinationAlphaAdditive) {
                const u32 base = kFullAuditApplyRecordBase +
                    applyIndex * kFullAuditApplyRecordWordCount;
                const u32 coordinates = values[base + 0u];
                const u32 x = coordinates & 0xffffu;
                const u32 y = coordinates >> 16u;
                const std::size_t pixelIndex =
                    static_cast<std::size_t>(y) * extent.width + x;
                if (x >= extent.width || y >= extent.height ||
                    pixelIndex >= beforeApplyAlpha.size()) {
                    ++frameAudit.applyAlphaLookupMissing;
                    blendFactor = 0.0;
                } else {
                    blendFactor = beforeApplyAlpha[pixelIndex];
                }
            }
            blendExpectedLuminance +=
                applyContributionLuminance[applyIndex] * blendFactor;
        }
        const double hdrBeforeLuminance = stageLuminanceSums[
            static_cast<u32>(
                HybridReflectionFullAuditImageStage::DeferredHdrBeforeApply
            )
        ];
        const double hdrAfterLuminance = stageLuminanceSums[
            static_cast<u32>(HybridReflectionFullAuditImageStage::HdrAfterApply)
        ];
        const double hdrActualDelta = hdrAfterLuminance - hdrBeforeLuminance;
        auditIndexFile << 3u << ',' << fullAuditFramesWritten << ','
            << capturedFrameNumber << ',' << recordCount << ','
            << applyRecordCount << ',' << applyRecordCount << ','
            << frameAudit.unknownReceivers << ',' << frameAudit.unknownHits
            << ',' << frameAudit.unresolvedReceivers << ','
            << frameAudit.invalidStageChains << ','
            << frameAudit.missingObjectIds << ','
            << frameAudit.unknownObjectIds << ','
            << frameAudit.receiverTruthMismatches << ','
            << frameAudit.receiverOriginsOutsideBounds << ','
            << frameAudit.selfHits << ',' << frameAudit.negativeHemisphere
            << ',' << frameAudit.negativeHemisphereRejected << ','
            << frameAudit.sceneReceiverIdentityResolved << ','
            << frameAudit.tlasEligibleReceiverRays << ','
            << frameAudit.tlasEligibleReceiverResolved << ','
            << frameAudit.applyMissingObjectIds << ','
            << frameAudit.applyUnknownObjectIds << ','
            << frameAudit.applyNonFinite << ','
            << frameAudit.gBufferNonFinite << ','
            << frameAudit.gBufferSourceNegative << ','
            << frameAudit.negativeApplyContributions << ','
            << frameAudit.receiverCounterMismatches << ','
            << frameAudit.incomingCounterMismatches << ','
            << frameAudit.nonzeroApplyContributions << ','
            << frameAudit.applyContributionLuminance << ','
            << ColorBlendModeName(applyBlendMode) << ','
            << "ffx_resolve_temporal_history" << ','
            << blendExpectedLuminance << ','
            << frameAudit.applyAlphaLookupMissing << ','
            << hdrBeforeLuminance << ',' << hdrAfterLuminance << ','
            << hdrActualDelta << ',' << beforeApplyAlphaSum << ','
            << beforeApplyAlphaMin << ',' << beforeApplyAlphaMax << ','
            << beforeApplyZeroAlphaCount << ',' << beforeApplyOneAlphaCount
            << '\n';

        ++fullAuditFramesWritten;
        stats.rayQueryFullAuditRecordedRayCount = recordCount;
        stats.rayQueryFullAuditCapturedFrameCount = fullAuditFramesWritten;
    }

    u64 TotalMemoryBytes() const {
        const u64 resultBytes = static_cast<u64>(extent.width) *
            static_cast<u64>(extent.height) * sizeof(u32) * 2u;
        const u64 hitSurfaceBytes = static_cast<u64>(extent.width) *
            static_cast<u64>(extent.height) * sizeof(u16) * 4u;
        const u64 perFrameBytes = resultBytes + hitSurfaceBytes +
            sizeof(RayQueryControls) +
            sizeof(u32) * static_cast<u64>(diagnosticWordCount) +
            sizeof(HybridReflectionInstanceMetadata) *
                kMaxHybridReflectionInstances +
            sizeof(HybridReflectionMaterialRecord) *
                kMaxHybridReflectionMaterials;
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
    std::vector<std::unique_ptr<VulkanImage>> hitSurfaceImages;
    std::vector<std::unique_ptr<VulkanBuffer>> controlsBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> diagnosticsBuffers;
    std::array<std::vector<std::unique_ptr<VulkanBuffer>>,
        kHybridReflectionFullAuditImageStageCount>
        fullAuditImageReadbackBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> fullAuditObjectIdReadbackBuffers;
    std::array<std::vector<bool>, kHybridReflectionFullAuditImageStageCount>
        fullAuditImageRecorded;
    std::vector<bool> fullAuditObjectIdRecorded;
    std::array<VkExtent2D, kHybridReflectionFullAuditImageStageCount>
        fullAuditImageExtents{};
    std::array<VkFormat, kHybridReflectionFullAuditImageStageCount>
        fullAuditImageFormats{};
    struct FullAuditMetadata {
        bool valid = false;
        u64 frameNumber = 0u;
        u32 sceneObjectCount = 0u;
        std::vector<HybridReflectionFullAuditLightRecord> lights;
        std::vector<HybridReflectionFullAuditProbeRecord> probes;
        std::vector<HybridReflectionFullAuditQueueCommandRecord> commands;
    };
    std::vector<FullAuditMetadata> fullAuditMetadata;
    std::vector<std::unique_ptr<VulkanBuffer>> instanceMetadataBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> materialBuffers;
    std::unique_ptr<VulkanTexture2D> fallbackTexture;
    std::unique_ptr<VulkanSampler> fallbackSampler;
    std::vector<std::array<VkImageView, kMaxHybridReflectionMaterials>>
        boundMaterialTextureViews;
    std::vector<std::array<VkSampler, kMaxHybridReflectionMaterials>>
        boundMaterialSamplers;
    std::vector<bool> submitted;
    std::vector<bool> frameEnabled;
    std::vector<bool> denoiserInjectionActive;
    std::vector<bool> diagnosticsActive;
    std::vector<bool> fullAuditActive;
    std::vector<bool> tlasDescriptorReady;
    bool fullAuditResourcesAllocated = false;
    bool fullAuditVerbosePixelCsv = false;
    bool fullAuditRawEvidence = false;
    u32 diagnosticWordCount = kDiagnosticValueCount;
    u32 fullAuditMaxCapturedFrames = 0u;
    u32 fullAuditFramesWritten = 0u;
    std::filesystem::path fullAuditOutputDirectory;
    u32 iblPrefilteredMipCount = 0u;
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
    const VulkanLightBuffer& lightBuffer,
    VkImageView iblBrdfView,
    VkImageView iblIrradianceView,
    VkImageView iblPrefilteredView,
    VkSampler iblSampler,
    u32 iblPrefilteredMipCount,
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
        lightBuffer,
        iblBrdfView,
        iblIrradianceView,
        iblPrefilteredView,
        iblSampler,
        iblPrefilteredMipCount,
        computeShaderPath
    )) {
}

VulkanHybridReflectionRayQuery::~VulkanHybridReflectionRayQuery() = default;

void VulkanHybridReflectionRayQuery::PrepareFrame(
    const VulkanDevice& device,
    u32 imageIndex,
    VkAccelerationStructureKHR topLevelAccelerationStructure,
    std::span<const HybridReflectionInstanceMetadata> instanceMetadata,
    std::span<const VulkanMaterial* const> instanceMaterials,
    bool enabled,
    bool hitAttributesEnabled,
    bool materialTexturesEnabled,
    bool hitLightingEnabled,
    bool shadowVisibilityEnabled,
    bool denoiserInjectionEnabled,
    bool diagnosticsEnabled,
    u32 directionalLightCount,
    u32 localLightCount,
    const HybridReflectionRayQuerySettings& settings,
    RendererHybridReflectionStats& stats
) {
    m_Impl->PrepareFrame(
        device,
        imageIndex,
        topLevelAccelerationStructure,
        instanceMetadata,
        instanceMaterials,
        enabled,
        hitAttributesEnabled,
        materialTexturesEnabled,
        hitLightingEnabled,
        shadowVisibilityEnabled,
        denoiserInjectionEnabled,
        diagnosticsEnabled,
        directionalLightCount,
        localLightCount,
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

bool VulkanHybridReflectionRayQuery::FullAuditResourcesAllocated() const {
    return m_Impl->fullAuditResourcesAllocated;
}

VkDescriptorBufferInfo VulkanHybridReflectionRayQuery::FullAuditDescriptorInfo(
    u32 imageIndex
) const {
    return m_Impl->FullAuditDescriptorInfo(imageIndex);
}

void VulkanHybridReflectionRayQuery::RecordFullAuditApplyBeginBarrier(
    VkCommandBuffer commandBuffer,
    u32 imageIndex
) const {
    m_Impl->RecordFullAuditApplyBarrier(commandBuffer, imageIndex, false);
}

void VulkanHybridReflectionRayQuery::RecordFullAuditApplyEndBarrier(
    VkCommandBuffer commandBuffer,
    u32 imageIndex
) const {
    m_Impl->RecordFullAuditApplyBarrier(commandBuffer, imageIndex, true);
}

void VulkanHybridReflectionRayQuery::RecordFullAuditImageSnapshot(
    VkCommandBuffer commandBuffer,
    u32 imageIndex,
    HybridReflectionFullAuditImageStage stage,
    VkImage image,
    VkFormat format,
    VkExtent2D imageExtent,
    VkImageLayout currentLayout,
    VkImage objectIdImage,
    VkImageLayout objectIdLayout
) {
    m_Impl->RecordFullAuditImageSnapshot(
        commandBuffer,
        imageIndex,
        stage,
        image,
        format,
        imageExtent,
        currentLayout,
        objectIdImage,
        objectIdLayout
    );
}

void VulkanHybridReflectionRayQuery::CaptureFullAuditMetadata(
    u32 imageIndex,
    u64 frameNumber,
    u32 sceneObjectCount,
    std::span<const HybridReflectionFullAuditLightRecord> lights,
    std::span<const HybridReflectionFullAuditProbeRecord> probes,
    std::span<const HybridReflectionFullAuditQueueCommandRecord> commands
) {
    m_Impl->CaptureFullAuditMetadata(
        imageIndex,
        frameNumber,
        sceneObjectCount,
        lights,
        probes,
        commands
    );
}

void VulkanHybridReflectionRayQuery::WriteFullAuditFrame(
    u32 imageIndex,
    u64 frameNumber,
    std::span<const HybridReflectionInstanceAuditRecord> instances,
    RendererHybridReflectionStats& stats
) {
    m_Impl->WriteFullAuditFrame(
        imageIndex,
        frameNumber,
        instances,
        stats
    );
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

VkImage VulkanHybridReflectionRayQuery::HitSurfaceImage(u32 imageIndex) const {
    if (imageIndex >= m_Impl->hitSurfaceImages.size()) {
        return VK_NULL_HANDLE;
    }
    return m_Impl->hitSurfaceImages[imageIndex]->Handle();
}

VkImageView VulkanHybridReflectionRayQuery::HitSurfaceView(u32 imageIndex) const {
    if (imageIndex >= m_Impl->hitSurfaceImages.size()) {
        return VK_NULL_HANDLE;
    }
    return m_Impl->hitSurfaceImages[imageIndex]->View();
}

VkFormat VulkanHybridReflectionRayQuery::HitSurfaceFormat() const {
    return kHitSurfaceFormat;
}

u64 VulkanHybridReflectionRayQuery::TotalMemoryBytes() const {
    return m_Impl->TotalMemoryBytes();
}

}
