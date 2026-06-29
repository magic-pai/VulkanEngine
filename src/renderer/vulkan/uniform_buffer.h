#pragma once

#include "renderer/vulkan/shadow_settings.h"
#include "renderer/vulkan/vulkan_common.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace se {

class VulkanBuffer;
class VulkanDevice;
class VulkanPhysicalDevice;

inline constexpr std::size_t kMaxFrameLocalLights = 64;
inline constexpr std::size_t kLightTileSize = 16;
inline constexpr std::size_t kMaxFrameLightTiles = 8192;
inline constexpr std::size_t kMaxFrameLightsPerTile = 16;
inline constexpr std::size_t kLightIndexGroupsPerTile =
    (kMaxFrameLightsPerTile + 3) / 4;
inline constexpr std::size_t kMaxFrameLightTileOverflowIndices = 65536;
inline constexpr std::size_t kMaxFrameMaterials = 256;
inline constexpr std::size_t kMaxFrameLocalShadowTiles = 64;

struct UniformBufferObject {
    alignas(16) glm::mat4 view{ 1.0f };
    alignas(16) glm::mat4 proj{ 1.0f };
    alignas(16) glm::mat4 invView{ 1.0f };
    alignas(16) glm::mat4 invProj{ 1.0f };
    alignas(16) glm::mat4 lightViewProj{ 1.0f };
    alignas(16) glm::vec4 directionalLight{ -0.45f, -0.82f, -0.35f, 0.78f };
    alignas(16) glm::vec4 ambientLight{ 0.22f, 0.24f, 0.0f, 0.0f };
    alignas(16) glm::vec4 shadowControls{ 1.0f, 0.95f, 0.00045f, 0.0012f };
    alignas(16) glm::vec4 shadowFiltering{ 1.0f, 0.42f, 0.0f, 0.0f };
    alignas(16) glm::vec4 contactShadowControls{ 0.35f, 0.18f, 4.0f, 0.0f };
};

struct GpuLocalLightRecord {
    alignas(16) glm::vec4 positionRadius{};
    alignas(16) glm::vec4 colorIntensity{};
    alignas(16) glm::vec4 directionType{};
    alignas(16) glm::vec4 parameters{};
};

struct GpuLightTileRecord {
    alignas(16) glm::uvec4 offsetCount{};
    alignas(16) glm::uvec4 overflowOffsetCount{};
};

struct LightBufferObject {
    alignas(16) glm::vec4 directionalLight{ -0.45f, -0.82f, -0.35f, 0.78f };
    alignas(16) glm::vec4 ambientLight{ 0.22f, 0.24f, 0.0f, 0.0f };
    alignas(16) glm::vec4 lightCounts{ 1.0f, 1.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 tileInfo{
        static_cast<f32>(kLightTileSize),
        0.0f,
        0.0f,
        static_cast<f32>(kMaxFrameLightsPerTile)
    };
    alignas(16) std::array<GpuLocalLightRecord, kMaxFrameLocalLights> localLights{};
    alignas(16) GpuLightTileRecord lightTiles[kMaxFrameLightTiles]{};
    alignas(16) glm::uvec4 tileLightIndexGroups[
        kMaxFrameLightTiles * kLightIndexGroupsPerTile
    ]{};
    alignas(4) std::array<u32, kMaxFrameLightTileOverflowIndices>
        tileOverflowLightIndices{};
};

struct LightTileDiagnosticsBufferObject {
    alignas(16) glm::uvec4 counters{};
    alignas(16) glm::uvec4 overflowCounters{};
};

struct DirectionalShadowCascadeBufferObject {
    alignas(16) glm::vec4 cascadeInfo{ 0.0f };
    alignas(16) glm::vec4 splitDepths{ 0.0f };
    alignas(16) glm::vec4 texelWorldSizes{ 0.0f };
    alignas(16) glm::vec4 cascadeBlendControls{ 0.0f };
    alignas(16) glm::mat4 fallbackViewProjection{ 1.0f };
    alignas(16) std::array<glm::mat4, kMaxDirectionalShadowCascades> viewProjections{};
};

struct GpuLocalShadowTileRecord {
    alignas(16) glm::mat4 viewProjection{ 1.0f };
    alignas(16) glm::uvec4 tileInfo{ 0u };
    alignas(16) glm::vec4 lightInfo{ 0.0f };
};

struct LocalShadowBufferObject {
    alignas(16) glm::uvec4 atlasInfo{ 0u };
    alignas(16) glm::uvec4 atlasInfo2{ 0u };
    alignas(16) glm::vec4 filterControls{ 0.0009f, 0.0024f, 1.0f, 1.0f };
    alignas(16) glm::vec4 softShadowControls{ 0.0f };
    alignas(16) std::array<GpuLocalShadowTileRecord, kMaxFrameLocalShadowTiles> tiles{};
};

struct GpuMaterialRecord {
    alignas(16) glm::vec4 baseColorFactor{ 1.0f };
    alignas(16) glm::vec4 materialControls{ 1.0f, 0.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 materialCustom{ 0.0f };
    alignas(16) glm::vec4 cameraControls{ 15.0f, 1.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 materialFlags{ 0.0f, 0.0f, 0.0f, 1.0f };
    alignas(16) glm::vec4 pbrFactors{ 1.0f, 1.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 emissiveFactor{ 0.0f, 0.0f, 0.0f, 1.0f };
    alignas(16) glm::vec4 specularFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
    alignas(16) glm::vec4 uvTransform{ 0.0f, 0.0f, 1.0f, 1.0f };
    alignas(16) glm::vec4 uvControls{ 0.0f, 0.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 volumeFactor{ 0.0f, 0.0f, 0.0f, 0.0f };
};

struct MaterialBufferObject {
    alignas(16) glm::vec4 materialCounts{
        0.0f,
        static_cast<f32>(kMaxFrameMaterials),
        0.0f,
        0.0f
    };
    alignas(16) std::array<GpuMaterialRecord, kMaxFrameMaterials> materials{};
};

struct ObjectPushConstants {
    alignas(16) glm::mat4 model{ 1.0f };
    alignas(16) glm::vec4 tint{ 1.0f };
    alignas(16) glm::vec4 materialBaseColorFactor{ 1.0f };
    alignas(16) glm::vec4 materialControls{ 1.0f, 0.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 materialCustom{ 0.0f };
    alignas(16) glm::vec4 viewport{ 1.0f, 1.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 cameraControls{ 15.0f, 1.0f, 0.0f, 0.0f };
    alignas(16) glm::vec4 cameraPosition{ 0.0f, 0.0f, 15.0f, 0.0f };
    alignas(16) glm::vec4 cameraDirection{ 0.0f, 0.0f, -1.0f, 0.0f };
};

static_assert(
    sizeof(UniformBufferObject) == sizeof(glm::mat4) * 5 + sizeof(glm::vec4) * 5,
    "UniformBufferObject layout must match the shader uniform block"
);

static_assert(
    sizeof(GpuLocalLightRecord) == sizeof(glm::vec4) * 4,
    "GpuLocalLightRecord layout must match the shader storage buffer record"
);

static_assert(
    sizeof(GpuLightTileRecord) == sizeof(glm::uvec4) * 2,
    "GpuLightTileRecord layout must match the shader storage buffer record"
);

static_assert(
    sizeof(LightBufferObject) ==
        sizeof(glm::vec4) * 4 +
        sizeof(GpuLocalLightRecord) * kMaxFrameLocalLights +
        sizeof(GpuLightTileRecord) * kMaxFrameLightTiles +
        sizeof(glm::uvec4) * kMaxFrameLightTiles * kLightIndexGroupsPerTile +
        sizeof(u32) * kMaxFrameLightTileOverflowIndices,
    "LightBufferObject layout must match the shader uniform block"
);

static_assert(
    sizeof(LightTileDiagnosticsBufferObject) == sizeof(glm::uvec4) * 2,
    "LightTileDiagnosticsBufferObject layout must match the shader storage buffer"
);

static_assert(
    sizeof(DirectionalShadowCascadeBufferObject) ==
        sizeof(glm::vec4) * 4 +
        sizeof(glm::mat4) * (1 + kMaxDirectionalShadowCascades),
    "DirectionalShadowCascadeBufferObject layout must match the shader storage buffer"
);

static_assert(
    sizeof(GpuLocalShadowTileRecord) ==
        sizeof(glm::mat4) + sizeof(glm::uvec4) + sizeof(glm::vec4),
    "GpuLocalShadowTileRecord layout must match the shader storage buffer record"
);

static_assert(
    sizeof(LocalShadowBufferObject) ==
        sizeof(glm::uvec4) * 2 +
        sizeof(glm::vec4) * 2 +
        sizeof(GpuLocalShadowTileRecord) * kMaxFrameLocalShadowTiles,
    "LocalShadowBufferObject layout must match the shader storage buffer"
);

static_assert(
    sizeof(GpuMaterialRecord) == sizeof(glm::vec4) * 11,
    "GpuMaterialRecord layout must match the shader storage buffer record"
);

static_assert(
    sizeof(MaterialBufferObject) ==
        sizeof(glm::vec4) +
        sizeof(GpuMaterialRecord) * kMaxFrameMaterials,
    "MaterialBufferObject layout must match the shader storage buffer"
);

static_assert(
    sizeof(ObjectPushConstants) == sizeof(glm::mat4) + sizeof(glm::vec4) * 8,
    "ObjectPushConstants layout must match the shader push constant block"
);

class VulkanUniformBuffer {
public:
    VulkanUniformBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

    ~VulkanUniformBuffer();

    SE_DISABLE_COPY(VulkanUniformBuffer);
    SE_DISABLE_MOVE(VulkanUniformBuffer);

    std::size_t Count() const;
    VkDescriptorBufferInfo DescriptorInfo(std::size_t index) const;

    void Update(std::size_t index, const UniformBufferObject& uniformData) const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );
    void Release();

private:
    void CreateUniformBuffers(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

private:
    std::vector<std::unique_ptr<VulkanBuffer>> m_Buffers;
};

class VulkanLightBuffer {
public:
    VulkanLightBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

    ~VulkanLightBuffer();

    SE_DISABLE_COPY(VulkanLightBuffer);
    SE_DISABLE_MOVE(VulkanLightBuffer);

    std::size_t Count() const;
    VkDescriptorBufferInfo DescriptorInfo(std::size_t index) const;

    void Update(std::size_t index, const LightBufferObject& lightData) const;
    void Download(std::size_t index, LightBufferObject& lightData) const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );
    void Release();

private:
    void CreateLightBuffers(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

private:
    std::vector<std::unique_ptr<VulkanBuffer>> m_Buffers;
};

class VulkanLightTileDiagnosticsBuffer {
public:
    VulkanLightTileDiagnosticsBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

    ~VulkanLightTileDiagnosticsBuffer();

    SE_DISABLE_COPY(VulkanLightTileDiagnosticsBuffer);
    SE_DISABLE_MOVE(VulkanLightTileDiagnosticsBuffer);

    std::size_t Count() const;
    VkDescriptorBufferInfo DescriptorInfo(std::size_t index) const;

    void Update(std::size_t index, const LightTileDiagnosticsBufferObject& data) const;
    LightTileDiagnosticsBufferObject Download(std::size_t index) const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );
    void Release();

private:
    void CreateBuffers(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

private:
    std::vector<std::unique_ptr<VulkanBuffer>> m_Buffers;
};

class VulkanMaterialBuffer {
public:
    VulkanMaterialBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

    ~VulkanMaterialBuffer();

    SE_DISABLE_COPY(VulkanMaterialBuffer);
    SE_DISABLE_MOVE(VulkanMaterialBuffer);

    std::size_t Count() const;
    VkDescriptorBufferInfo DescriptorInfo(std::size_t index) const;

    void Update(std::size_t index, const MaterialBufferObject& materialData) const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );
    void Release();

private:
    void CreateMaterialBuffers(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

private:
    std::vector<std::unique_ptr<VulkanBuffer>> m_Buffers;
};

class VulkanDirectionalShadowCascadeBuffer {
public:
    VulkanDirectionalShadowCascadeBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

    ~VulkanDirectionalShadowCascadeBuffer();

    SE_DISABLE_COPY(VulkanDirectionalShadowCascadeBuffer);
    SE_DISABLE_MOVE(VulkanDirectionalShadowCascadeBuffer);

    std::size_t Count() const;
    VkDescriptorBufferInfo DescriptorInfo(std::size_t index) const;

    void Update(
        std::size_t index,
        const DirectionalShadowCascadeBufferObject& cascadeData
    ) const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );
    void Release();

private:
    void CreateBuffers(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

private:
    std::vector<std::unique_ptr<VulkanBuffer>> m_Buffers;
};

class VulkanLocalShadowBuffer {
public:
    VulkanLocalShadowBuffer(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

    ~VulkanLocalShadowBuffer();

    SE_DISABLE_COPY(VulkanLocalShadowBuffer);
    SE_DISABLE_MOVE(VulkanLocalShadowBuffer);

    std::size_t Count() const;
    VkDescriptorBufferInfo DescriptorInfo(std::size_t index) const;

    void Update(
        std::size_t index,
        const LocalShadowBufferObject& localShadowData
    ) const;
    void Recreate(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );
    void Release();

private:
    void CreateBuffers(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        std::size_t count
    );

private:
    std::vector<std::unique_ptr<VulkanBuffer>> m_Buffers;
};

}
