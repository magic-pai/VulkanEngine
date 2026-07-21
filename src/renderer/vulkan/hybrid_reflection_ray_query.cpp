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
#include "renderer/vulkan/render_targets.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/sampler.h"
#include "renderer/vulkan/texture_2d.h"
#include "renderer/vulkan/uniform_buffer.h"
#include "renderer/vulkan/vertex.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace se {

namespace {

constexpr u32 kRayQueryContractVersion = 2u;
constexpr u32 kHitAttributeContractVersion = 1u;
constexpr u32 kMaterialTableContractVersion = 1u;
constexpr u32 kHitLightingContractVersion = 1u;
constexpr u32 kHitLightingVisibilityModeUnshadowed = 1u;
constexpr u32 kHitLightingVisibilityFallbackPendingRayQuery = 1u;
constexpr u32 kDiagnosticValueCount = 59u;
constexpr u32 kHitDistanceMinDiagnosticIndex = 7u;
constexpr u32 kNormalLengthMinDiagnosticIndex = 20u;
constexpr u32 kBarycentricSumMinDiagnosticIndex = 25u;
constexpr u32 kSampleLodMinDiagnosticIndex = 33u;
constexpr u32 kHitSurfaceLuminanceMinDiagnosticIndex = 37u;
constexpr u32 kRadianceLuminanceMinDiagnosticIndex = 56u;
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
};

static_assert(sizeof(RayQueryControls) == 96u);
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
        std::array<VkDescriptorSetLayoutBinding, 21> bindings{};
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
        tlasDescriptorReady.assign(count, false);
        boundMaterialTextureViews.resize(count);
        boundMaterialSamplers.resize(count);

        std::array<u32, kDiagnosticValueCount> diagnostics{};
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

        std::array<VkDescriptorPoolSize, 8> poolSizes{};
        poolSizes[0] = {
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            static_cast<u32>(count)
        };
        poolSizes[1] = {
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            static_cast<u32>(
                count * (8u + kMaxHybridReflectionMaterials)
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
            static_cast<u32>(count * 2u)
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

            std::array<VkWriteDescriptorSet, 20> writes{};
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
        std::span<const VulkanMaterial* const> instanceMaterials,
        bool enabled,
        bool hitAttributesEnabled,
        bool materialTexturesEnabled,
        bool hitLightingEnabled,
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
        controls.directionalLightCount = std::min(directionalLightCount, 1u);
        controls.localLightCount = std::min(
            localLightCount,
            static_cast<u32>(kMaxFrameLocalLights)
        );
        controls.iblPrefilteredMipCount = iblPrefilteredMipCount;
        controls.hitLightingVisibilityMode = controls.hitLightingEnabled != 0u
            ? kHitLightingVisibilityModeUnshadowed
            : 0u;
        controls.iblResourcesReady = 1u;
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
        diagnostics[kSampleLodMinDiagnosticIndex] =
            std::numeric_limits<u32>::max();
        diagnostics[kHitSurfaceLuminanceMinDiagnosticIndex] =
            std::numeric_limits<u32>::max();
        diagnostics[kRadianceLuminanceMinDiagnosticIndex] =
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
            controls.hitLightingEnabled != 0u
                ? kHitLightingVisibilityFallbackPendingRayQuery
                : 0u;
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
        VkImageMemoryBarrier confidenceForRead = resultForClear;
        confidenceForRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        confidenceForRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        confidenceForRead.image =
            classifyResources.HitConfidenceImage(imageIndex);
        std::array<VkImageMemoryBarrier, 3> imageBarriers{
            resultForWrite,
            hitSurfaceForWrite,
            confidenceForRead
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
        return result;
#endif
    }

    u64 TotalMemoryBytes() const {
        const u64 resultBytes = static_cast<u64>(extent.width) *
            static_cast<u64>(extent.height) * sizeof(u32) * 2u;
        const u64 hitSurfaceBytes = static_cast<u64>(extent.width) *
            static_cast<u64>(extent.height) * sizeof(u16) * 4u;
        const u64 perFrameBytes = resultBytes + hitSurfaceBytes +
            sizeof(RayQueryControls) +
            sizeof(u32) * kDiagnosticValueCount +
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
    std::vector<bool> tlasDescriptorReady;
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
