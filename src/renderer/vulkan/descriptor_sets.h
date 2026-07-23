#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

class VulkanDescriptorSetLayout;
class VulkanHiZDescriptorSetLayout;
class VulkanSsrReconstructionDescriptorSetLayout;
class VulkanMaterialDescriptorSetLayout;
class VulkanDevice;
class VulkanMaterial;
class VulkanSampler;
class VulkanBloomPyramid;
class VulkanDepthPyramid;
class VulkanColorGradingLut;
class VulkanSceneRenderTargets;
class VulkanDirectionalShadowCascadeAtlas;
class VulkanLocalShadowAtlas;
class VulkanShadowMap;
class VulkanAutoExposureBuffer;
class VulkanLightBuffer;
class VulkanLightTileDiagnosticsBuffer;
class VulkanMaterialBuffer;
class VulkanProbeGridBuffer;
class VulkanDirectionalShadowCascadeBuffer;
class VulkanLocalShadowBuffer;
class VulkanUniformBuffer;

class VulkanDescriptorSets {
public:
    VulkanDescriptorSets(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& descriptorSetLayout,
        const VulkanUniformBuffer& uniformBuffer,
        const VulkanLightBuffer& lightBuffer,
        const VulkanLightTileDiagnosticsBuffer& lightTileDiagnosticsBuffer,
        const VulkanMaterialBuffer& materialBuffer,
        const VulkanProbeGridBuffer& probeGridBuffer,
        const VulkanDirectionalShadowCascadeBuffer& directionalShadowCascadeBuffer,
        const VulkanLocalShadowBuffer& localShadowBuffer,
        const VulkanAutoExposureBuffer& autoExposureBuffer
    );

    ~VulkanDescriptorSets();

    SE_DISABLE_COPY(VulkanDescriptorSets);
    SE_DISABLE_MOVE(VulkanDescriptorSets);

    VkDescriptorSet Handle(std::size_t index) const;
    std::size_t Count() const;
    void UpdateReflectionFullAuditBuffer(
        const VulkanDevice& device,
        std::size_t index,
        const VkDescriptorBufferInfo& bufferInfo
    );
    void Recreate(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& descriptorSetLayout,
        const VulkanUniformBuffer& uniformBuffer,
        const VulkanLightBuffer& lightBuffer,
        const VulkanLightTileDiagnosticsBuffer& lightTileDiagnosticsBuffer,
        const VulkanMaterialBuffer& materialBuffer,
        const VulkanProbeGridBuffer& probeGridBuffer,
        const VulkanDirectionalShadowCascadeBuffer& directionalShadowCascadeBuffer,
        const VulkanLocalShadowBuffer& localShadowBuffer,
        const VulkanAutoExposureBuffer& autoExposureBuffer
    );
    void Release();

private:
    void CreateDescriptorPool(const VulkanDevice& device, std::size_t count);
    void CreateDescriptorSets(
        const VulkanDevice& device,
        const VulkanDescriptorSetLayout& descriptorSetLayout,
        const VulkanUniformBuffer& uniformBuffer,
        const VulkanLightBuffer& lightBuffer,
        const VulkanLightTileDiagnosticsBuffer& lightTileDiagnosticsBuffer,
        const VulkanMaterialBuffer& materialBuffer,
        const VulkanProbeGridBuffer& probeGridBuffer,
        const VulkanDirectionalShadowCascadeBuffer& directionalShadowCascadeBuffer,
        const VulkanLocalShadowBuffer& localShadowBuffer,
        const VulkanAutoExposureBuffer& autoExposureBuffer
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};

class VulkanMaterialDescriptorSets {
public:
    VulkanMaterialDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        std::span<const VulkanMaterial*> materials,
        const VulkanShadowMap* shadowMap = nullptr,
        const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas = nullptr,
        const VulkanLocalShadowAtlas* localShadowAtlas = nullptr
    );

    ~VulkanMaterialDescriptorSets();

    SE_DISABLE_COPY(VulkanMaterialDescriptorSets);
    SE_DISABLE_MOVE(VulkanMaterialDescriptorSets);

    VkDescriptorSet Handle(const VulkanMaterial& material) const;
    VkDescriptorSet Handle(const VulkanMaterial& material, std::size_t imageIndex) const;
    std::size_t Count() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        std::span<const VulkanMaterial*> materials,
        const VulkanShadowMap* shadowMap = nullptr,
        const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas = nullptr,
        const VulkanLocalShadowAtlas* localShadowAtlas = nullptr
    );
    void Release();

private:
    void CreateDescriptorPool(const VulkanDevice& device, std::size_t count);
    void CreateDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        std::span<const VulkanMaterial*> materials,
        const VulkanShadowMap* shadowMap,
        const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas,
        const VulkanLocalShadowAtlas* localShadowAtlas
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<const VulkanMaterial*> m_Materials;
    std::vector<VkDescriptorSet> m_DescriptorSets;
    std::size_t m_SetsPerMaterial = 1;
};

class VulkanGBufferDescriptorSets {
public:
    VulkanGBufferDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanSampler& sampler,
        const VulkanShadowMap* shadowMap = nullptr,
        const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas = nullptr,
        const VulkanLocalShadowAtlas* localShadowAtlas = nullptr,
        const VulkanDepthPyramid* depthPyramid = nullptr
    );

    ~VulkanGBufferDescriptorSets();

    SE_DISABLE_COPY(VulkanGBufferDescriptorSets);
    SE_DISABLE_MOVE(VulkanGBufferDescriptorSets);

    VkDescriptorSet Handle(std::size_t index) const;
    std::size_t Count() const;
    bool UpdateSsrSceneColorHistory(
        const VulkanDevice& device,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanSampler& sampler,
        std::size_t descriptorIndex,
        std::size_t historyImageIndex,
        VkImageView resolvedReflectionOverride = VK_NULL_HANDLE,
        VkImageLayout resolvedReflectionLayout = VK_IMAGE_LAYOUT_GENERAL,
        VkImageView hitConfidenceOverride = VK_NULL_HANDLE,
        VkImageLayout hitConfidenceLayout = VK_IMAGE_LAYOUT_GENERAL,
        VkImageView currentReflectionOverride = VK_NULL_HANDLE,
        VkImageLayout currentReflectionLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    void Recreate(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanSampler& sampler,
        const VulkanShadowMap* shadowMap = nullptr,
        const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas = nullptr,
        const VulkanLocalShadowAtlas* localShadowAtlas = nullptr,
        const VulkanDepthPyramid* depthPyramid = nullptr
    );
    void Release();

private:
    void CreateDescriptorPool(const VulkanDevice& device, std::size_t count);
    void CreateDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanSampler& sampler,
        const VulkanShadowMap* shadowMap,
        const VulkanDirectionalShadowCascadeAtlas* cascadeAtlas,
        const VulkanLocalShadowAtlas* localShadowAtlas,
        const VulkanDepthPyramid* depthPyramid
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};

class VulkanHiZDescriptorSets {
public:
    VulkanHiZDescriptorSets(
        const VulkanDevice& device,
        const VulkanHiZDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanSampler& sampler
    );
    ~VulkanHiZDescriptorSets();

    SE_DISABLE_COPY(VulkanHiZDescriptorSets);
    SE_DISABLE_MOVE(VulkanHiZDescriptorSets);

    VkDescriptorSet Handle(std::size_t imageIndex, u32 mipIndex) const;
    std::size_t Count() const;
    u32 MipCount() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanHiZDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanSampler& sampler
    );
    void Release();

private:
    void CreateDescriptorSets(
        const VulkanDevice& device,
        const VulkanHiZDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanSampler& sampler
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DescriptorSets;
    std::size_t m_Count = 0;
    u32 m_MipCount = 0;
};

class VulkanSsrReconstructionDescriptorSets {
public:
    VulkanSsrReconstructionDescriptorSets(
        const VulkanDevice& device,
        const VulkanSsrReconstructionDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanSampler& sampler
    );
    ~VulkanSsrReconstructionDescriptorSets();

    SE_DISABLE_COPY(VulkanSsrReconstructionDescriptorSets);
    SE_DISABLE_MOVE(VulkanSsrReconstructionDescriptorSets);

    VkDescriptorSet Handle(std::size_t imageIndex) const;
    std::size_t Count() const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanSsrReconstructionDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanSampler& sampler
    );
    void Release();

private:
    void CreateDescriptorSets(
        const VulkanDevice& device,
        const VulkanSsrReconstructionDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanDepthPyramid& depthPyramid,
        const VulkanSampler& sampler
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};

class VulkanHdrDescriptorSets {
public:
    VulkanHdrDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanBloomPyramid* bloomPyramid,
        const VulkanColorGradingLut* colorGradingLut,
        const VulkanSampler& sampler,
        bool useTemporalUpscaleOutputSource = false
    );

    ~VulkanHdrDescriptorSets();

    SE_DISABLE_COPY(VulkanHdrDescriptorSets);
    SE_DISABLE_MOVE(VulkanHdrDescriptorSets);

    VkDescriptorSet Handle(std::size_t index) const;
    std::size_t Count() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanBloomPyramid* bloomPyramid,
        const VulkanColorGradingLut* colorGradingLut,
        const VulkanSampler& sampler,
        bool useTemporalUpscaleOutputSource = false
    );
    void Release();

private:
    void CreateDescriptorPool(const VulkanDevice& device, std::size_t count);
    void CreateDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanBloomPyramid* bloomPyramid,
        const VulkanColorGradingLut* colorGradingLut,
        const VulkanSampler& sampler,
        bool useTemporalUpscaleOutputSource
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};

class VulkanBloomDescriptorSets {
public:
    VulkanBloomDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanBloomPyramid& bloomPyramid,
        const VulkanSampler& sampler,
        bool useTemporalUpscaleOutputSource = false
    );

    ~VulkanBloomDescriptorSets();

    SE_DISABLE_COPY(VulkanBloomDescriptorSets);
    SE_DISABLE_MOVE(VulkanBloomDescriptorSets);

    VkDescriptorSet DownsampleHandle(std::size_t imageIndex, u32 mipIndex) const;
    VkDescriptorSet UpsampleHandle(std::size_t imageIndex, u32 mipIndex) const;
    std::size_t Count() const;
    u32 MipCount() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanBloomPyramid& bloomPyramid,
        const VulkanSampler& sampler,
        bool useTemporalUpscaleOutputSource = false
    );
    void Release();

private:
    void CreateDescriptorPool(
        const VulkanDevice& device,
        std::size_t swapchainCount,
        u32 mipCount
    );
    void CreateDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanBloomPyramid& bloomPyramid,
        const VulkanSampler& sampler,
        bool useTemporalUpscaleOutputSource
    );
    std::size_t DescriptorIndex(std::size_t imageIndex, u32 mipIndex) const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DownsampleDescriptorSets;
    std::vector<VkDescriptorSet> m_UpsampleDescriptorSets;
    std::size_t m_Count = 0;
    u32 m_MipCount = 0;
};

class VulkanWeightedTranslucencyDescriptorSets {
public:
    VulkanWeightedTranslucencyDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanSampler& sampler
    );

    ~VulkanWeightedTranslucencyDescriptorSets();

    SE_DISABLE_COPY(VulkanWeightedTranslucencyDescriptorSets);
    SE_DISABLE_MOVE(VulkanWeightedTranslucencyDescriptorSets);

    VkDescriptorSet Handle(std::size_t index) const;
    std::size_t Count() const;

    void Recreate(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanSampler& sampler
    );
    void Release();

private:
    void CreateDescriptorPool(const VulkanDevice& device, std::size_t count);
    void CreateDescriptorSets(
        const VulkanDevice& device,
        const VulkanMaterialDescriptorSetLayout& descriptorSetLayout,
        const VulkanSceneRenderTargets& renderTargets,
        const VulkanSampler& sampler
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_DescriptorSets;
};

}
