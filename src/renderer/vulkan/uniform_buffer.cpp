#include "renderer/vulkan/uniform_buffer.h"

#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/physical_device.h"

namespace se {

namespace {

std::span<const std::byte> AsBytes(const UniformBufferObject& uniformData) {
    return std::as_bytes(std::span<const UniformBufferObject>(&uniformData, 1));
}

std::span<const std::byte> AsBytes(const LightBufferObject& lightData) {
    return std::as_bytes(std::span<const LightBufferObject>(&lightData, 1));
}

std::span<const std::byte> AsBytes(const LightTileDiagnosticsBufferObject& diagnosticsData) {
    return std::as_bytes(std::span<const LightTileDiagnosticsBufferObject>(&diagnosticsData, 1));
}

std::span<const std::byte> AsBytes(const AutoExposureBufferObject& exposureData) {
    return std::as_bytes(std::span<const AutoExposureBufferObject>(&exposureData, 1));
}

std::span<const std::byte> AsBytes(
    const DirectionalShadowCascadeBufferObject& cascadeData
) {
    return std::as_bytes(std::span<const DirectionalShadowCascadeBufferObject>(&cascadeData, 1));
}

std::span<const std::byte> AsBytes(const LocalShadowBufferObject& localShadowData) {
    return std::as_bytes(std::span<const LocalShadowBufferObject>(&localShadowData, 1));
}

std::span<const std::byte> AsBytes(const MaterialBufferObject& materialData) {
    return std::as_bytes(std::span<const MaterialBufferObject>(&materialData, 1));
}

std::span<const std::byte> AsBytes(const ProbeGridBufferObject& probeGridData) {
    return std::as_bytes(std::span<const ProbeGridBufferObject>(&probeGridData, 1));
}

}

VulkanUniformBuffer::VulkanUniformBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    CreateUniformBuffers(device, physicalDevice, count);
}

VulkanUniformBuffer::~VulkanUniformBuffer() {
    Release();
}

std::size_t VulkanUniformBuffer::Count() const {
    return m_Buffers.size();
}

VkDescriptorBufferInfo VulkanUniformBuffer::DescriptorInfo(std::size_t index) const {
    SE_ASSERT(index < m_Buffers.size(), "Uniform buffer index is out of range");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_Buffers[index]->Handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    return bufferInfo;
}

void VulkanUniformBuffer::Update(
    std::size_t index,
    const UniformBufferObject& uniformData
) const {
    SE_ASSERT(index < m_Buffers.size(), "Uniform buffer index is out of range");
    m_Buffers[index]->Upload(AsBytes(uniformData));
}

void VulkanUniformBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    Release();
    CreateUniformBuffers(device, physicalDevice, count);
}

void VulkanUniformBuffer::Release() {
    m_Buffers.clear();
}

void VulkanUniformBuffer::CreateUniformBuffers(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Uniform buffer count must be greater than zero");

    m_Buffers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        m_Buffers.push_back(std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(UniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));
    }
}

VulkanLightBuffer::VulkanLightBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    CreateLightBuffers(device, physicalDevice, count);
}

VulkanLightBuffer::~VulkanLightBuffer() {
    Release();
}

std::size_t VulkanLightBuffer::Count() const {
    return m_Buffers.size();
}

VkDescriptorBufferInfo VulkanLightBuffer::DescriptorInfo(std::size_t index) const {
    SE_ASSERT(index < m_Buffers.size(), "Light buffer index is out of range");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_Buffers[index]->Handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(LightBufferObject);

    return bufferInfo;
}

void VulkanLightBuffer::Update(
    std::size_t index,
    const LightBufferObject& lightData
) const {
    SE_ASSERT(index < m_Buffers.size(), "Light buffer index is out of range");
    m_Buffers[index]->Upload(AsBytes(lightData));
}

void VulkanLightBuffer::Download(
    std::size_t index,
    LightBufferObject& lightData
) const {
    SE_ASSERT(index < m_Buffers.size(), "Light buffer index is out of range");

    m_Buffers[index]->Download(
        std::as_writable_bytes(std::span<LightBufferObject>(&lightData, 1))
    );
}

void VulkanLightBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    Release();
    CreateLightBuffers(device, physicalDevice, count);
}

void VulkanLightBuffer::Release() {
    m_Buffers.clear();
}

void VulkanLightBuffer::CreateLightBuffers(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Light buffer count must be greater than zero");

    m_Buffers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        m_Buffers.push_back(std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(LightBufferObject),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));
    }
}

VulkanLightTileDiagnosticsBuffer::VulkanLightTileDiagnosticsBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    CreateBuffers(device, physicalDevice, count);
}

VulkanLightTileDiagnosticsBuffer::~VulkanLightTileDiagnosticsBuffer() {
    Release();
}

std::size_t VulkanLightTileDiagnosticsBuffer::Count() const {
    return m_Buffers.size();
}

VkDescriptorBufferInfo VulkanLightTileDiagnosticsBuffer::DescriptorInfo(std::size_t index) const {
    SE_ASSERT(index < m_Buffers.size(), "Light tile diagnostics buffer index is out of range");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_Buffers[index]->Handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(LightTileDiagnosticsBufferObject);

    return bufferInfo;
}

void VulkanLightTileDiagnosticsBuffer::Update(
    std::size_t index,
    const LightTileDiagnosticsBufferObject& data
) const {
    SE_ASSERT(index < m_Buffers.size(), "Light tile diagnostics buffer index is out of range");
    m_Buffers[index]->Upload(AsBytes(data));
}

LightTileDiagnosticsBufferObject VulkanLightTileDiagnosticsBuffer::Download(
    std::size_t index
) const {
    SE_ASSERT(index < m_Buffers.size(), "Light tile diagnostics buffer index is out of range");

    LightTileDiagnosticsBufferObject data{};
    m_Buffers[index]->Download(
        std::as_writable_bytes(std::span<LightTileDiagnosticsBufferObject>(&data, 1))
    );
    return data;
}

void VulkanLightTileDiagnosticsBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    Release();
    CreateBuffers(device, physicalDevice, count);
}

void VulkanLightTileDiagnosticsBuffer::Release() {
    m_Buffers.clear();
}

void VulkanLightTileDiagnosticsBuffer::CreateBuffers(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Light tile diagnostics buffer count must be greater than zero");

    m_Buffers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        m_Buffers.push_back(std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(LightTileDiagnosticsBufferObject),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));
    }
}

VulkanAutoExposureBuffer::VulkanAutoExposureBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    CreateBuffers(device, physicalDevice, count);
}

VulkanAutoExposureBuffer::~VulkanAutoExposureBuffer() {
    Release();
}

std::size_t VulkanAutoExposureBuffer::Count() const {
    return m_Buffers.size();
}

VkDescriptorBufferInfo VulkanAutoExposureBuffer::DescriptorInfo(std::size_t index) const {
    SE_ASSERT(index < m_Buffers.size(), "Auto exposure buffer index is out of range");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_Buffers[index]->Handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(AutoExposureBufferObject);

    return bufferInfo;
}

void VulkanAutoExposureBuffer::Update(
    std::size_t index,
    const AutoExposureBufferObject& data
) const {
    SE_ASSERT(index < m_Buffers.size(), "Auto exposure buffer index is out of range");
    m_Buffers[index]->Upload(AsBytes(data));
}

AutoExposureBufferObject VulkanAutoExposureBuffer::Download(
    std::size_t index
) const {
    SE_ASSERT(index < m_Buffers.size(), "Auto exposure buffer index is out of range");

    AutoExposureBufferObject data{};
    m_Buffers[index]->Download(
        std::as_writable_bytes(std::span<AutoExposureBufferObject>(&data, 1))
    );
    return data;
}

void VulkanAutoExposureBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    Release();
    CreateBuffers(device, physicalDevice, count);
}

void VulkanAutoExposureBuffer::Release() {
    m_Buffers.clear();
}

void VulkanAutoExposureBuffer::CreateBuffers(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Auto exposure buffer count must be greater than zero");

    m_Buffers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto buffer = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(AutoExposureBufferObject),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        AutoExposureBufferObject initialData{};
        buffer->Upload(AsBytes(initialData));
        m_Buffers.push_back(std::move(buffer));
    }
}

VulkanMaterialBuffer::VulkanMaterialBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    CreateMaterialBuffers(device, physicalDevice, count);
}

VulkanMaterialBuffer::~VulkanMaterialBuffer() {
    Release();
}

std::size_t VulkanMaterialBuffer::Count() const {
    return m_Buffers.size();
}

VkDescriptorBufferInfo VulkanMaterialBuffer::DescriptorInfo(std::size_t index) const {
    SE_ASSERT(index < m_Buffers.size(), "Material buffer index is out of range");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_Buffers[index]->Handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(MaterialBufferObject);

    return bufferInfo;
}

void VulkanMaterialBuffer::Update(
    std::size_t index,
    const MaterialBufferObject& materialData
) const {
    SE_ASSERT(index < m_Buffers.size(), "Material buffer index is out of range");
    m_Buffers[index]->Upload(AsBytes(materialData));
}

void VulkanMaterialBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    Release();
    CreateMaterialBuffers(device, physicalDevice, count);
}

void VulkanMaterialBuffer::Release() {
    m_Buffers.clear();
}

void VulkanMaterialBuffer::CreateMaterialBuffers(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Material buffer count must be greater than zero");

    m_Buffers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        m_Buffers.push_back(std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(MaterialBufferObject),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));
    }
}

VulkanProbeGridBuffer::VulkanProbeGridBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    CreateBuffers(device, physicalDevice, count);
}

VulkanProbeGridBuffer::~VulkanProbeGridBuffer() {
    Release();
}

std::size_t VulkanProbeGridBuffer::Count() const {
    return m_Buffers.size();
}

VkDescriptorBufferInfo VulkanProbeGridBuffer::DescriptorInfo(
    std::size_t index
) const {
    SE_ASSERT(index < m_Buffers.size(), "Probe grid buffer index is out of range");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_Buffers[index]->Handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(ProbeGridBufferObject);

    return bufferInfo;
}

void VulkanProbeGridBuffer::Update(
    std::size_t index,
    const ProbeGridBufferObject& probeGridData
) const {
    SE_ASSERT(index < m_Buffers.size(), "Probe grid buffer index is out of range");
    m_Buffers[index]->Upload(AsBytes(probeGridData));
}

void VulkanProbeGridBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    Release();
    CreateBuffers(device, physicalDevice, count);
}

void VulkanProbeGridBuffer::Release() {
    m_Buffers.clear();
}

void VulkanProbeGridBuffer::CreateBuffers(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Probe grid buffer count must be greater than zero");

    m_Buffers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto buffer = std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(ProbeGridBufferObject),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        ProbeGridBufferObject initialData{};
        buffer->Upload(AsBytes(initialData));
        m_Buffers.push_back(std::move(buffer));
    }
}

VulkanDirectionalShadowCascadeBuffer::VulkanDirectionalShadowCascadeBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    CreateBuffers(device, physicalDevice, count);
}

VulkanDirectionalShadowCascadeBuffer::~VulkanDirectionalShadowCascadeBuffer() {
    Release();
}

std::size_t VulkanDirectionalShadowCascadeBuffer::Count() const {
    return m_Buffers.size();
}

VkDescriptorBufferInfo VulkanDirectionalShadowCascadeBuffer::DescriptorInfo(
    std::size_t index
) const {
    SE_ASSERT(index < m_Buffers.size(), "Directional shadow cascade buffer index is out of range");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_Buffers[index]->Handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(DirectionalShadowCascadeBufferObject);

    return bufferInfo;
}

void VulkanDirectionalShadowCascadeBuffer::Update(
    std::size_t index,
    const DirectionalShadowCascadeBufferObject& cascadeData
) const {
    SE_ASSERT(index < m_Buffers.size(), "Directional shadow cascade buffer index is out of range");
    m_Buffers[index]->Upload(AsBytes(cascadeData));
}

void VulkanDirectionalShadowCascadeBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    Release();
    CreateBuffers(device, physicalDevice, count);
}

void VulkanDirectionalShadowCascadeBuffer::Release() {
    m_Buffers.clear();
}

void VulkanDirectionalShadowCascadeBuffer::CreateBuffers(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Directional shadow cascade buffer count must be greater than zero");

    m_Buffers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        m_Buffers.push_back(std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(DirectionalShadowCascadeBufferObject),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));
    }
}

VulkanLocalShadowBuffer::VulkanLocalShadowBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    CreateBuffers(device, physicalDevice, count);
}

VulkanLocalShadowBuffer::~VulkanLocalShadowBuffer() {
    Release();
}

std::size_t VulkanLocalShadowBuffer::Count() const {
    return m_Buffers.size();
}

VkDescriptorBufferInfo VulkanLocalShadowBuffer::DescriptorInfo(
    std::size_t index
) const {
    SE_ASSERT(index < m_Buffers.size(), "Local shadow buffer index is out of range");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_Buffers[index]->Handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(LocalShadowBufferObject);

    return bufferInfo;
}

void VulkanLocalShadowBuffer::Update(
    std::size_t index,
    const LocalShadowBufferObject& localShadowData
) const {
    SE_ASSERT(index < m_Buffers.size(), "Local shadow buffer index is out of range");
    m_Buffers[index]->Upload(AsBytes(localShadowData));
}

void VulkanLocalShadowBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    Release();
    CreateBuffers(device, physicalDevice, count);
}

void VulkanLocalShadowBuffer::Release() {
    m_Buffers.clear();
}

void VulkanLocalShadowBuffer::CreateBuffers(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Local shadow buffer count must be greater than zero");

    m_Buffers.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        m_Buffers.push_back(std::make_unique<VulkanBuffer>(
            device,
            physicalDevice,
            sizeof(LocalShadowBufferObject),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));
    }
}

}
