#include "scene/runtime_model_loader.h"

#include "renderer/vulkan/material.h"
#include "renderer/vulkan/material_library.h"
#include "renderer/vulkan/mesh_lod.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "renderer/vulkan/upload_batch.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

namespace se {

class RuntimeBonePaletteDescriptorSet {
public:
    explicit RuntimeBonePaletteDescriptorSet(VkDevice device)
        : m_Device(device) {
    }

    ~RuntimeBonePaletteDescriptorSet() {
        Release();
    }

    SE_DISABLE_COPY(RuntimeBonePaletteDescriptorSet);
    SE_DISABLE_MOVE(RuntimeBonePaletteDescriptorSet);

    void Create(const VulkanBuffer& buffer) {
        Release();

        m_Binding = 0;
        m_RangeBytes = buffer.Size() > std::numeric_limits<u32>::max()
            ? std::numeric_limits<u32>::max()
            : static_cast<u32>(buffer.Size());
        if (buffer.Handle() == VK_NULL_HANDLE || buffer.Size() == 0) {
            return;
        }

        VkDescriptorSetLayoutBinding paletteBinding{};
        paletteBinding.binding = m_Binding;
        paletteBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        paletteBinding.descriptorCount = 1;
        paletteBinding.stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        paletteBinding.pImmutableSamplers = nullptr;

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
            throw std::runtime_error("Failed to create imported bone palette descriptor layout");
        }

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        if (vkCreateDescriptorPool(
                m_Device,
                &poolInfo,
                nullptr,
                &m_Pool
            ) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create imported bone palette descriptor pool");
        }

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_Pool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &m_Layout;

        if (vkAllocateDescriptorSets(m_Device, &allocateInfo, &m_Set) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate imported bone palette descriptor set");
        }
        m_Allocated = m_Set != VK_NULL_HANDLE ? 1u : 0u;

        VkDescriptorBufferInfo descriptorInfo{};
        descriptorInfo.buffer = buffer.Handle();
        descriptorInfo.offset = 0;
        descriptorInfo.range = buffer.Size();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_Set;
        write.dstBinding = m_Binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &descriptorInfo;

        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
        m_Written = 1u;
        m_Ready = m_Allocated != 0u && m_Written != 0u && m_RangeBytes > 0
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
        m_Allocated = 0;
        m_Written = 0;
        m_Ready = 0;
    }

    u32 Allocated() const { return m_Allocated; }
    u32 Written() const { return m_Written; }
    u32 Ready() const { return m_Ready; }
    u32 Binding() const { return m_Binding; }
    u32 RangeBytes() const { return m_RangeBytes; }

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_Set = VK_NULL_HANDLE;
    u32 m_Allocated = 0;
    u32 m_Written = 0;
    u32 m_Ready = 0;
    u32 m_Binding = 0;
    u32 m_RangeBytes = 0;
};

namespace {

constexpr bool kUploadAuxiliaryTexturesOnImport = true;

struct ResolvedTextureSource {
    const ImportedTexture3D* texture = nullptr;
    std::optional<std::string> externalPath;

    bool UsesFallback() const {
        return texture == nullptr;
    }
};

bool MatrixChanged(
    const glm::mat4& previous,
    const glm::mat4& current,
    f32 epsilon = 0.0001f
) {
    for (u32 column = 0; column < 4; ++column) {
        for (u32 row = 0; row < 4; ++row) {
            if (std::abs(previous[column][row] - current[column][row]) > epsilon) {
                return true;
            }
        }
    }

    return false;
}

u32 CountChangedMatrices(
    const std::vector<glm::mat4>& previous,
    const std::vector<glm::mat4>& current
) {
    const std::size_t count = std::min(previous.size(), current.size());
    u32 changed = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (MatrixChanged(previous[index], current[index])) {
            ++changed;
        }
    }

    return changed;
}

struct RendererBonePaletteRegistration {
    u32 registered = 0;
    u32 currentEntryCount = 0;
    u32 previousEntryCount = 0;
    u32 changedEntryCount = 0;
    u32 ready = 0;
};

struct GpuBonePaletteUpload {
    std::unique_ptr<VulkanBuffer> buffer;
    u32 allocated = 0;
    u32 uploaded = 0;
    u32 descriptorInfoReady = 0;
    u32 byteSize = 0;
    u32 currentEntryCount = 0;
    u32 previousEntryCount = 0;
};

struct GpuBonePaletteDescriptorWrite {
    std::unique_ptr<RuntimeBonePaletteDescriptorSet> descriptorSet;
    u32 allocated = 0;
    u32 written = 0;
    u32 ready = 0;
    u32 binding = 0;
    u32 rangeBytes = 0;
};

RendererBonePaletteRegistration RegisterRendererBonePaletteResource(
    VulkanRenderResources2D& renderResources,
    const std::string& resourceId,
    const std::vector<glm::mat4>& previousPalette,
    const std::vector<glm::mat4>& currentPalette,
    u32 changedEntryCount,
    u32 ready
) {
    RendererBonePaletteRegistration result{};
    result.currentEntryCount = static_cast<u32>(currentPalette.size());
    result.previousEntryCount = static_cast<u32>(previousPalette.size());
    result.changedEntryCount = changedEntryCount;
    result.ready = ready;

    if (resourceId.empty() || currentPalette.empty() || previousPalette.empty()) {
        return result;
    }

    renderResources.RegisterBonePalette(
        resourceId,
        VulkanRenderResources2D::BonePaletteResource{
            previousPalette,
            currentPalette,
            changedEntryCount,
            ready
        }
    );
    result.registered = renderResources.ContainsBonePalette(resourceId) ? 1u : 0u;
    if (result.registered != 0u) {
        const VulkanRenderResources2D::BonePaletteResource& resource =
            renderResources.BonePalette(resourceId);
        result.currentEntryCount =
            static_cast<u32>(resource.currentPalette.size());
        result.previousEntryCount =
            static_cast<u32>(resource.previousPalette.size());
        result.changedEntryCount = resource.changedEntryCount;
        result.ready = resource.ready;
    }

    return result;
}

GpuBonePaletteUpload CreateGpuBonePaletteBuffer(
    const VulkanDevice& device,
    const VulkanPhysicalDevice& physicalDevice,
    const std::vector<glm::mat4>& previousPalette,
    const std::vector<glm::mat4>& currentPalette
) {
    GpuBonePaletteUpload result{};
    result.currentEntryCount = static_cast<u32>(currentPalette.size());
    result.previousEntryCount = static_cast<u32>(previousPalette.size());
    if (previousPalette.empty() || currentPalette.empty()) {
        return result;
    }

    std::vector<glm::mat4> paletteData;
    paletteData.reserve(previousPalette.size() + currentPalette.size());
    paletteData.insert(
        paletteData.end(),
        previousPalette.begin(),
        previousPalette.end()
    );
    paletteData.insert(
        paletteData.end(),
        currentPalette.begin(),
        currentPalette.end()
    );

    const VkDeviceSize bufferSize =
        static_cast<VkDeviceSize>(paletteData.size() * sizeof(glm::mat4));
    result.byteSize = static_cast<u32>(bufferSize);
    result.buffer = std::make_unique<VulkanBuffer>(
        device,
        physicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    result.allocated = result.buffer->Handle() != VK_NULL_HANDLE ? 1u : 0u;
    if (result.allocated == 0u) {
        return result;
    }

    result.buffer->Upload(std::as_bytes(std::span<const glm::mat4>(paletteData)));
    result.uploaded = 1u;

    VkDescriptorBufferInfo descriptorInfo{};
    descriptorInfo.buffer = result.buffer->Handle();
    descriptorInfo.offset = 0;
    descriptorInfo.range = result.buffer->Size();
    result.descriptorInfoReady =
        descriptorInfo.buffer != VK_NULL_HANDLE &&
        descriptorInfo.range >= bufferSize
            ? 1u
            : 0u;

    return result;
}

GpuBonePaletteDescriptorWrite CreateGpuBonePaletteDescriptorWrite(
    const VulkanDevice& device,
    const VulkanBuffer* buffer
) {
    GpuBonePaletteDescriptorWrite result{};
    if (buffer == nullptr || buffer->Handle() == VK_NULL_HANDLE || buffer->Size() == 0) {
        return result;
    }

    result.descriptorSet =
        std::make_unique<RuntimeBonePaletteDescriptorSet>(device.Handle());
    result.descriptorSet->Create(*buffer);
    result.allocated = result.descriptorSet->Allocated();
    result.written = result.descriptorSet->Written();
    result.ready = result.descriptorSet->Ready();
    result.binding = result.descriptorSet->Binding();
    result.rangeBytes = result.descriptorSet->RangeBytes();

    return result;
}

std::optional<ResolvedTextureSource> ResolveImportedTextureSource(
    const ImportedTexture3D& texture
) {
    std::error_code error;
    if (!texture.externalPath.empty() &&
        std::filesystem::is_regular_file(texture.externalPath, error) &&
        !error) {
        return ResolvedTextureSource{
            &texture,
            texture.externalPath.string()
        };
    }

    if (texture.HasEmbeddedTexture()) {
        return ResolvedTextureSource{
            &texture,
            std::nullopt
        };
    }

    return std::nullopt;
}

std::optional<ResolvedTextureSource> ResolvePrimaryTextureSource(
    const ImportedMaterial3D& material
) {
    const ImportedTexture3D* texture = material.FindPrimaryTexture();
    if (texture == nullptr) {
        return std::nullopt;
    }

    return ResolveImportedTextureSource(*texture);
}

ResolvedTextureSource PrimaryTextureSourceOrFallback(
    const ImportedMaterial3D& material,
    const std::string& fallbackTexturePath
) {
    if (std::optional<ResolvedTextureSource> textureSource =
        ResolvePrimaryTextureSource(material)) {
        return *textureSource;
    }

    return ResolvedTextureSource{
        nullptr,
        fallbackTexturePath
    };
}

struct ImportedAuxTexture {
    std::optional<ResolvedTextureSource> source;
    f32 kind = 0.0f;
};

ImportedAuxTexture ResolveAuxTexture(const ImportedMaterial3D& material) {
    struct Candidate {
        ImportedTextureKind kind;
        f32 shaderKind;
    };

    static constexpr Candidate kCandidates[] = {
        { ImportedTextureKind::MetallicRoughness, 1.0f },
        { ImportedTextureKind::Roughness, 2.0f },
        { ImportedTextureKind::Metallic, 3.0f }
    };

    for (const Candidate& candidate : kCandidates) {
        const ImportedTexture3D* texture = material.FindTexture(candidate.kind);
        if (texture == nullptr) {
            continue;
        }

        if (std::optional<ResolvedTextureSource> textureSource =
            ResolveImportedTextureSource(*texture)) {
            return ImportedAuxTexture{
                std::move(textureSource),
                candidate.shaderKind
            };
        }
    }

    return ImportedAuxTexture{};
}

std::optional<ResolvedTextureSource> ResolveTextureKind(
    const ImportedMaterial3D& material,
    ImportedTextureKind kind
) {
    const ImportedTexture3D* texture = material.FindTexture(kind);
    if (texture == nullptr) {
        return std::nullopt;
    }

    return ResolveImportedTextureSource(*texture);
}

VulkanTexturePixels EmbeddedTexturePixels(const EmbeddedTextureData& texture) {
    return VulkanTexturePixels{
        std::span<const u8>(texture.bytes.data(), texture.bytes.size()),
        texture.width,
        texture.height
    };
}

VulkanEncodedTextureBytes EmbeddedTextureBytes(const EmbeddedTextureData& texture) {
    return VulkanEncodedTextureBytes{
        std::span<const u8>(texture.bytes.data(), texture.bytes.size())
    };
}

f32 ImportedTextureFlags(
    f32 auxTextureKind,
    bool hasNormalTexture,
    bool hasOcclusionTexture,
    bool hasEmissiveTexture,
    bool hasOpacityTexture,
    bool hasSpecularTexture,
    bool hasClearcoatTexture,
    bool hasTransmissionTexture,
    bool hasClearcoatRoughnessTexture
) {
    f32 flags = auxTextureKind;
    if (hasNormalTexture) {
        flags += kMaterialTextureFlagNormal;
    }
    if (hasOcclusionTexture) {
        flags += kMaterialTextureFlagOcclusion;
    }
    if (hasEmissiveTexture) {
        flags += kMaterialTextureFlagEmissive;
    }
    if (hasOpacityTexture) {
        flags += kMaterialTextureFlagOpacity;
    }
    if (hasSpecularTexture) {
        flags += kMaterialTextureFlagSpecular;
    }
    if (hasClearcoatTexture) {
        flags += kMaterialTextureFlagClearcoat;
    }
    if (hasTransmissionTexture) {
        flags += kMaterialTextureFlagTransmission;
    }
    if (hasClearcoatRoughnessTexture) {
        flags += kMaterialTextureFlagClearcoatRoughness;
    }

    return flags;
}

MaterialProperties ForwardMaterial(
    std::array<f32, 4> baseColor,
    f32 textureMix,
    f32 ambient,
    f32 diffuse,
    f32 specular,
    f32 shininess
) {
    MaterialProperties properties{};
    properties.baseColorFactor = baseColor;
    properties.textureMix = textureMix;
    properties.custom = { -0.45f, -0.82f, -0.35f, ambient };
    properties.viewControls = { diffuse, specular, shininess, 0.0f };
    properties.cameraControls = { 0.0f, 0.65f, 0.0f, 0.0f };

    return properties;
}

f32 ShininessFromRoughness(f32 roughness) {
    const f32 clampedRoughness = std::clamp(roughness, 0.0f, 1.0f);
    return 4.0f + (1.0f - clampedRoughness) * 92.0f;
}

MaterialAlphaMode ImportedAlphaModeToMaterialAlphaMode(ImportedAlphaMode alphaMode) {
    switch (alphaMode) {
    case ImportedAlphaMode::Mask:
        return MaterialAlphaMode::Mask;
    case ImportedAlphaMode::Blend:
        return MaterialAlphaMode::Blend;
    case ImportedAlphaMode::Opaque:
    default:
        return MaterialAlphaMode::Opaque;
    }
}

MaterialProperties ImportedForwardMaterial(
    const ImportedMaterial3D& material,
    bool usePrimaryTexture,
    f32 textureFlags,
    bool hasAuxTexture
) {
    const f32 opacity = std::clamp(material.opacity, 0.0f, 1.0f);
    const f32 specular =
        std::clamp(material.specularFactor, 0.0f, 2.0f) * 0.22f;

    MaterialProperties properties = ForwardMaterial(
        {
            std::clamp(material.baseColor.r, 0.0f, 1.0f),
            std::clamp(material.baseColor.g, 0.0f, 1.0f),
            std::clamp(material.baseColor.b, 0.0f, 1.0f),
            opacity
        },
        usePrimaryTexture ? 1.0f : 0.0f,
        0.08f,
        3.1f,
        std::max(specular, 0.35f),
        ShininessFromRoughness(material.roughnessFactor)
    );
    properties.cameraControls = {
        std::clamp(material.metallicFactor, 0.0f, 1.0f),
        std::clamp(material.roughnessFactor, 0.04f, 1.0f),
        hasAuxTexture ? 1.0f : 0.0f,
        textureFlags
    };
    properties.pbrFactors = {
        std::clamp(material.normalScale, 0.0f, 4.0f),
        std::clamp(material.occlusionStrength, 0.0f, 1.0f),
        0.0f,
        0.0f
    };
    properties.emissiveFactor = {
        std::max(material.emissiveColor.r * material.emissiveIntensity, 0.0f),
        std::max(material.emissiveColor.g * material.emissiveIntensity, 0.0f),
        std::max(material.emissiveColor.b * material.emissiveIntensity, 0.0f),
        1.0f
    };
    properties.specularFactor = {
        std::clamp(material.specularColor.r, 0.0f, 2.0f),
        std::clamp(material.specularColor.g, 0.0f, 2.0f),
        std::clamp(material.specularColor.b, 0.0f, 2.0f),
        std::clamp(material.specularFactor, 0.0f, 2.0f)
    };
    properties.clearcoatFactor = std::clamp(material.clearcoatFactor, 0.0f, 1.0f);
    properties.clearcoatRoughness = std::clamp(material.clearcoatRoughness, 0.0f, 1.0f);
    properties.transmissionFactor = std::clamp(material.transmissionFactor, 0.0f, 1.0f);
    properties.volumeThicknessFactor = std::clamp(material.volumeThicknessFactor, 0.0f, 64.0f);
    properties.volumeAttenuationDistance =
        std::clamp(material.volumeAttenuationDistance, 0.0f, 1000000.0f);
    properties.volumeAttenuationColor = {
        std::clamp(material.volumeAttenuationColor.r, 0.0f, 1.0f),
        std::clamp(material.volumeAttenuationColor.g, 0.0f, 1.0f),
        std::clamp(material.volumeAttenuationColor.b, 0.0f, 1.0f),
        1.0f
    };
    properties.alphaMode = ImportedAlphaModeToMaterialAlphaMode(material.alphaMode);
    if (properties.alphaMode == MaterialAlphaMode::Opaque &&
        material.FindTexture(ImportedTextureKind::Opacity) != nullptr) {
        properties.alphaMode = MaterialAlphaMode::Blend;
    }
    properties.alphaCutoff = std::clamp(material.alphaCutoff, 0.0f, 1.0f);
    properties.doubleSided = material.doubleSided;
    if (properties.alphaMode == MaterialAlphaMode::Blend) {
        properties.renderClass = MaterialRenderClass::Transparent;
    }
    if (const ImportedTexture3D* primaryTexture = material.FindPrimaryTexture()) {
        if (primaryTexture->hasUvTransform) {
            properties.uvTransform = {
                primaryTexture->uvOffset.x,
                primaryTexture->uvOffset.y,
                primaryTexture->uvScale.x,
                primaryTexture->uvScale.y
            };
            properties.uvControls = {
                primaryTexture->uvRotation,
                1.0f,
                0.0f,
                0.0f
            };
        }
    }

    return properties;
}

void NormalizeImportedModel(ImportedModel3D& model, f32 targetMaxExtent) {
    if (targetMaxExtent <= 0.0f || !std::isfinite(targetMaxExtent)) {
        return;
    }

    const glm::vec3 extent = model.boundsMax - model.boundsMin;
    const f32 maxExtent = std::max({ extent.x, extent.y, extent.z });
    if (maxExtent <= 0.00001f || !std::isfinite(maxExtent)) {
        return;
    }

    const glm::vec3 center = (model.boundsMin + model.boundsMax) * 0.5f;
    const f32 scale = targetMaxExtent / maxExtent;
    model.boundsMin = glm::vec3(std::numeric_limits<f32>::max());
    model.boundsMax = glm::vec3(std::numeric_limits<f32>::lowest());

    for (ImportedMesh3D& mesh : model.meshes) {
        mesh.boundsMin = glm::vec3(std::numeric_limits<f32>::max());
        mesh.boundsMax = glm::vec3(std::numeric_limits<f32>::lowest());

        for (Vertex3D& vertex : mesh.mesh.vertices) {
            const glm::vec3 position{
                vertex.position[0],
                vertex.position[1],
                vertex.position[2]
            };
            const glm::vec3 normalized = (position - center) * scale;
            vertex.position = { normalized.x, normalized.y, normalized.z };
            mesh.boundsMin = glm::min(mesh.boundsMin, normalized);
            mesh.boundsMax = glm::max(mesh.boundsMax, normalized);
        }

        model.boundsMin = glm::min(model.boundsMin, mesh.boundsMin);
        model.boundsMax = glm::max(model.boundsMax, mesh.boundsMax);
    }
}

std::string PathLabel(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    return filename.empty() ? path.string() : filename;
}

}

RuntimeModelLoader::RuntimeModelLoader(
    VulkanDevice& device,
    VulkanPhysicalDevice& physicalDevice,
    VulkanCommandPool& commandPool,
    VulkanMaterialLibrary& materialLibrary,
    VulkanRenderResources2D& renderResources,
    Scene3D& scene,
    std::string fallbackTexturePath
) : m_Device(device),
    m_PhysicalDevice(physicalDevice),
    m_CommandPool(commandPool),
    m_MaterialLibrary(materialLibrary),
    m_RenderResources(renderResources),
    m_Scene(scene),
    m_FallbackTexturePath(std::move(fallbackTexturePath)) {
}

RuntimeModelLoader::LoadedRuntimeModel::~LoadedRuntimeModel() = default;

RuntimeModelLoadResult RuntimeModelLoader::LoadIntoScene(
    const std::filesystem::path& modelPath,
    glm::vec3 position,
    glm::vec3 rotationDegrees,
    f32 targetMaxExtent,
    glm::vec3 scale
) {
    try {
        std::error_code ec;
        const std::string cacheKey = std::filesystem::canonical(modelPath, ec).string();
        const std::string lookupKey = ec ? modelPath.string() : cacheKey;

        auto cacheIt = m_ModelCache.find(lookupKey);
        if (cacheIt != m_ModelCache.end()) {
            LoadedRuntimeModel& cached = *m_LoadedModels[cacheIt->second];
            const u32 modelId = m_NextModelId++;
            const std::string idPrefix = "RuntimeModel" + std::to_string(modelId);

            for (std::size_t mi = 0; mi < cached.meshes.size(); ++mi) {
                m_RenderResources.RegisterMesh(idPrefix + "_Mesh" + std::to_string(mi),
                    *cached.meshes[mi]);
            }
            for (std::size_t mi = 0; mi < cached.materials.size(); ++mi) {
                m_RenderResources.RegisterMaterial(idPrefix + "_Material" + std::to_string(mi),
                    *cached.materials[mi]);
            }
            const RendererBonePaletteRegistration rendererBonePalette =
                RegisterRendererBonePaletteResource(
                    m_RenderResources,
                    idPrefix + "_BonePalette",
                    cached.runtimePreviousBonePalette,
                    cached.runtimeCurrentBonePalette,
                    cached.runtimePoseCarrierChangedBonePaletteEntryCount,
                    cached.runtimePoseCarrierReady
                );

            u32 partIdx = 0;
            for (std::size_t mi = 0; mi < cached.meshes.size(); ++mi) {
                const std::size_t matIdx = std::min(mi,
                    cached.materialIds.empty() ? size_t(0) : cached.materialIds.size() - 1);
                Renderable3D& part = m_Scene.CreateRenderable(
                    "Cached " + PathLabel(modelPath) + " part" + std::to_string(partIdx++),
                    idPrefix + "_Mesh" + std::to_string(mi),
                    idPrefix + "_Material" + std::to_string(matIdx));
                part.Transform().SetPosition(position);
                part.Transform().SetRotationDegrees(rotationDegrees);
                part.Transform().SetScale(scale);
                part.Transform().SetAnimateRotation(false);
            }

            return RuntimeModelLoadResult{true,
                "Loaded (cached) " + PathLabel(modelPath) + " (" +
                    std::to_string(cached.meshes.size()) + " meshes, " +
                    std::to_string(cached.materials.size()) + " materials)",
                true,
                static_cast<u32>(cached.meshes.size()),
                static_cast<u32>(cached.materials.size()),
                cached.sourceNodeCount,
                cached.sourceBoneNodeCount,
                cached.sourceAnimationChannelBoundCount,
                cached.sourceAnimationChannelUnboundCount,
                cached.sourceBoneNameMatchedNodeCount,
                cached.sourceBoneNameUnmatchedCount,
                cached.sourceAnimationCount,
                cached.sourceAnimationChannelCount,
                cached.sourceAnimationPositionKeyCount,
                cached.sourceAnimationRotationKeyCount,
                cached.sourceAnimationScaleKeyCount,
                cached.sourceAnimationKeyCount,
                cached.sourceMaxAnimationKeysPerChannel,
                cached.sourcePoseSampledClipCount,
                cached.sourcePoseSampledChannelCount,
                cached.sourcePoseSampledNodeCount,
                cached.sourcePoseAnimatedNodeCount,
                cached.sourcePoseBonePaletteEntryCount,
                cached.sourcePosePreviousBonePaletteEntryCount,
                cached.sourcePoseChangedBonePaletteEntryCount,
                cached.sourcePoseBonePaletteReady,
                static_cast<u32>(cached.runtimeCurrentBonePalette.size()),
                static_cast<u32>(cached.runtimePreviousBonePalette.size()),
                cached.runtimePoseCarrierChangedBonePaletteEntryCount,
                cached.runtimePoseCarrierReady,
                rendererBonePalette.registered,
                rendererBonePalette.currentEntryCount,
                rendererBonePalette.previousEntryCount,
                rendererBonePalette.changedEntryCount,
                rendererBonePalette.ready,
                cached.gpuBonePaletteBuffer != nullptr ? 1u : 0u,
                cached.gpuPosePaletteBufferUploaded,
                cached.gpuPosePaletteDescriptorInfoReady,
                cached.gpuPosePaletteDescriptorSetAllocated,
                cached.gpuPosePaletteDescriptorSetWritten,
                cached.gpuPosePaletteDescriptorSetReady,
                cached.gpuPosePaletteDescriptorBinding,
                cached.gpuPosePaletteDescriptorRangeBytes,
                cached.gpuBonePaletteBuffer != nullptr
                    ? static_cast<u32>(cached.gpuBonePaletteBuffer->Size())
                    : 0u,
                cached.gpuPosePaletteCurrentEntryCount,
                cached.gpuPosePalettePreviousEntryCount,
                cached.sourceMeshWithBonesCount,
                cached.sourceBoneCount,
                cached.sourceSkinnedVertexCount,
                cached.sourceBoneInfluenceCount,
                cached.sourceMaxBoneInfluencesPerVertex,
                cached.skinnedAnimationUnsupported};
        }

        ModelImportOptions importOptions{};
        importOptions.fastImport = true;
        importOptions.readSkinningMetadata = true;
        importOptions.validateScene = false;
        importOptions.optimizeMeshes = false;
        ImportedModel3D importedModelData =
            ModelImporter::LoadModel3D(modelPath, importOptions);
        NormalizeImportedModel(importedModelData, targetMaxExtent);

        auto loadedModel = std::make_unique<LoadedRuntimeModel>();
        const u32 modelId = m_NextModelId++;
        const std::string idPrefix = "RuntimeModel" + std::to_string(modelId);
        VulkanUploadBatch uploadBatch(m_Device, m_CommandPool);

        loadedModel->meshes.reserve(importedModelData.meshes.size());
        loadedModel->meshIds.reserve(importedModelData.meshes.size());
        loadedModel->lodChains.reserve(importedModelData.meshes.size());
        for (ImportedMesh3D& importedMeshData : importedModelData.meshes) {
            // Save a copy of vertex/index data for LOD generation before move
            std::vector<Vertex3D> vCopy = importedMeshData.mesh.vertices;
            std::vector<u32> iCopy = importedMeshData.mesh.indices;
            loadedModel->meshes.push_back(std::make_unique<VulkanMesh>(
                m_Device,
                m_PhysicalDevice,
                m_CommandPool,
                std::move(importedMeshData.mesh.vertices),
                std::move(importedMeshData.mesh.indices),
                &uploadBatch
            ));
            // Generate LOD chain
            loadedModel->lodChains.push_back(MeshLodGenerator::Generate(vCopy, iCopy));
        }

        loadedModel->materialIds.reserve(importedModelData.materials.size());
        loadedModel->materials.reserve(importedModelData.materials.size());
        for (std::size_t materialIndex = 0;
             materialIndex < importedModelData.materials.size();
             ++materialIndex) {
            const ImportedMaterial3D& material = importedModelData.materials[materialIndex];
            const std::string materialId =
                idPrefix + "_Material" + std::to_string(materialIndex);
            const ResolvedTextureSource primaryTexture =
                PrimaryTextureSourceOrFallback(material, m_FallbackTexturePath);
            const ImportedAuxTexture auxTexture = ResolveAuxTexture(material);
            const ImportedAuxTexture uploadedAuxTexture = kUploadAuxiliaryTexturesOnImport
                ? auxTexture
                : ImportedAuxTexture{};
            const std::optional<ResolvedTextureSource> normalTexture =
                kUploadAuxiliaryTexturesOnImport
                    ? ResolveTextureKind(material, ImportedTextureKind::Normal)
                    : std::nullopt;
            const std::optional<ResolvedTextureSource> occlusionTexture =
                kUploadAuxiliaryTexturesOnImport
                    ? ResolveTextureKind(material, ImportedTextureKind::AmbientOcclusion)
                    : std::nullopt;
            const std::optional<ResolvedTextureSource> emissiveTexture =
                kUploadAuxiliaryTexturesOnImport
                    ? ResolveTextureKind(material, ImportedTextureKind::Emissive)
                    : std::nullopt;
            const std::optional<ResolvedTextureSource> opacityTexture =
                kUploadAuxiliaryTexturesOnImport
                    ? ResolveTextureKind(material, ImportedTextureKind::Opacity)
                    : std::nullopt;
            std::optional<ResolvedTextureSource> specularTexture =
                kUploadAuxiliaryTexturesOnImport
                    ? ResolveTextureKind(material, ImportedTextureKind::SpecularColor)
                    : std::nullopt;
            if (!specularTexture.has_value() && kUploadAuxiliaryTexturesOnImport) {
                specularTexture =
                    ResolveTextureKind(material, ImportedTextureKind::Specular);
            }
            const std::optional<ResolvedTextureSource> clearcoatTexture =
                kUploadAuxiliaryTexturesOnImport
                    ? ResolveTextureKind(material, ImportedTextureKind::Clearcoat)
                    : std::nullopt;
            const std::optional<ResolvedTextureSource> transmissionTexture =
                kUploadAuxiliaryTexturesOnImport
                    ? ResolveTextureKind(material, ImportedTextureKind::Transmission)
                    : std::nullopt;
            const std::optional<ResolvedTextureSource> clearcoatRoughnessTexture =
                kUploadAuxiliaryTexturesOnImport
                    ? ResolveTextureKind(material, ImportedTextureKind::ClearcoatRoughness)
                    : std::nullopt;
            const f32 textureFlags = ImportedTextureFlags(
                uploadedAuxTexture.kind,
                normalTexture.has_value(),
                occlusionTexture.has_value(),
                emissiveTexture.has_value(),
                opacityTexture.has_value(),
                specularTexture.has_value(),
                clearcoatTexture.has_value(),
                transmissionTexture.has_value(),
                clearcoatRoughnessTexture.has_value()
            );

            const MaterialProperties importedProperties = ImportedForwardMaterial(
                material,
                !primaryTexture.UsesFallback(),
                textureFlags,
                uploadedAuxTexture.source.has_value()
            );

            auto applyTextureSlot = [&](auto&& pathSetter, auto&& encodedSetter, auto&& pixelsSetter,
                                        const ResolvedTextureSource& texture,
                                        bool srgb) {
                if (texture.externalPath.has_value()) {
                    pathSetter(*texture.externalPath, srgb);
                } else if (texture.texture != nullptr &&
                    texture.texture->HasEmbeddedTexture()) {
                    const EmbeddedTextureData& embedded = texture.texture->embedded;
                    if (embedded.compressed) {
                        encodedSetter(EmbeddedTextureBytes(embedded), srgb);
                    } else {
                        pixelsSetter(EmbeddedTexturePixels(embedded), srgb);
                    }
                }
            };

            VulkanMaterial* importedMaterial = nullptr;
            if (primaryTexture.externalPath.has_value()) {
                importedMaterial = &m_MaterialLibrary.Create(
                    materialId,
                    *primaryTexture.externalPath,
                    importedProperties,
                    false,
                    true,
                    &uploadBatch
                );
            } else if (primaryTexture.texture != nullptr &&
                primaryTexture.texture->HasEmbeddedTexture()) {
                const EmbeddedTextureData& embedded = primaryTexture.texture->embedded;
                if (embedded.compressed) {
                    importedMaterial = &m_MaterialLibrary.Create(
                        materialId,
                        EmbeddedTextureBytes(embedded),
                        importedProperties,
                        false,
                        true,
                        &uploadBatch
                    );
                } else {
                    importedMaterial = &m_MaterialLibrary.Create(
                        materialId,
                        EmbeddedTexturePixels(embedded),
                        importedProperties,
                        false,
                        true,
                        &uploadBatch
                    );
                }
            }

            SE_ASSERT(importedMaterial != nullptr, "Runtime model material was not created");

            auto setColorMapPath = [&](const std::string& path, bool srgb) {
                importedMaterial->SetColorMap(
                    m_Device,
                    m_PhysicalDevice,
                    m_CommandPool,
                    path,
                    srgb,
                    false,
                    true,
                    &uploadBatch
                );
            };
            auto setColorMapEncoded = [&](VulkanEncodedTextureBytes bytes, bool srgb) {
                importedMaterial->SetColorMap(
                    m_Device,
                    m_PhysicalDevice,
                    m_CommandPool,
                    bytes,
                    srgb,
                    false,
                    true,
                    &uploadBatch
                );
            };
            auto setColorMapPixels = [&](VulkanTexturePixels pixels, bool srgb) {
                importedMaterial->SetColorMap(
                    m_Device,
                    m_PhysicalDevice,
                    m_CommandPool,
                    pixels,
                    srgb,
                    false,
                    true,
                    &uploadBatch
                );
            };

            if (uploadedAuxTexture.source.has_value()) {
                applyTextureSlot(
                    setColorMapPath,
                    setColorMapEncoded,
                    setColorMapPixels,
                    *uploadedAuxTexture.source,
                    false
                );
            }
            if (normalTexture.has_value()) {
                applyTextureSlot(
                    [&](const std::string& path, bool) {
                        importedMaterial->SetNormalMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            path,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanEncodedTextureBytes bytes, bool) {
                        importedMaterial->SetNormalMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            bytes,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanTexturePixels pixels, bool) {
                        importedMaterial->SetNormalMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            pixels,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    *normalTexture,
                    false
                );
            }
            if (occlusionTexture.has_value()) {
                applyTextureSlot(
                    [&](const std::string& path, bool) {
                        importedMaterial->SetOcclusionMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            path,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanEncodedTextureBytes bytes, bool) {
                        importedMaterial->SetOcclusionMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            bytes,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanTexturePixels pixels, bool) {
                        importedMaterial->SetOcclusionMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            pixels,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    *occlusionTexture,
                    false
                );
            }
            if (emissiveTexture.has_value()) {
                applyTextureSlot(
                    [&](const std::string& path, bool) {
                        importedMaterial->SetEmissiveMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            path,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanEncodedTextureBytes bytes, bool) {
                        importedMaterial->SetEmissiveMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            bytes,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanTexturePixels pixels, bool) {
                        importedMaterial->SetEmissiveMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            pixels,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    *emissiveTexture,
                    true
                );
            }
            if (opacityTexture.has_value()) {
                applyTextureSlot(
                    [&](const std::string& path, bool) {
                        importedMaterial->SetOpacityMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            path,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanEncodedTextureBytes bytes, bool) {
                        importedMaterial->SetOpacityMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            bytes,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanTexturePixels pixels, bool) {
                        importedMaterial->SetOpacityMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            pixels,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    *opacityTexture,
                    false
                );
            }
            if (specularTexture.has_value()) {
                applyTextureSlot(
                    [&](const std::string& path, bool srgb) {
                        importedMaterial->SetSpecularMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            path,
                            srgb,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanEncodedTextureBytes bytes, bool srgb) {
                        importedMaterial->SetSpecularMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            bytes,
                            srgb,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanTexturePixels pixels, bool srgb) {
                        importedMaterial->SetSpecularMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            pixels,
                            srgb,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    *specularTexture,
                    true
                );
            }
            if (clearcoatTexture.has_value()) {
                applyTextureSlot(
                    [&](const std::string& path, bool) {
                        importedMaterial->SetClearcoatMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            path,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanEncodedTextureBytes bytes, bool) {
                        importedMaterial->SetClearcoatMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            bytes,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanTexturePixels pixels, bool) {
                        importedMaterial->SetClearcoatMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            pixels,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    *clearcoatTexture,
                    false
                );
            }
            if (transmissionTexture.has_value()) {
                applyTextureSlot(
                    [&](const std::string& path, bool) {
                        importedMaterial->SetTransmissionMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            path,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanEncodedTextureBytes bytes, bool) {
                        importedMaterial->SetTransmissionMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            bytes,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanTexturePixels pixels, bool) {
                        importedMaterial->SetTransmissionMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            pixels,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    *transmissionTexture,
                    false
                );
            }
            if (clearcoatRoughnessTexture.has_value()) {
                applyTextureSlot(
                    [&](const std::string& path, bool) {
                        importedMaterial->SetClearcoatRoughnessMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            path,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanEncodedTextureBytes bytes, bool) {
                        importedMaterial->SetClearcoatRoughnessMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            bytes,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    [&](VulkanTexturePixels pixels, bool) {
                        importedMaterial->SetClearcoatRoughnessMap(
                            m_Device,
                            m_PhysicalDevice,
                            m_CommandPool,
                            pixels,
                            false,
                            true,
                            &uploadBatch
                        );
                    },
                    *clearcoatRoughnessTexture,
                    false
                );
            }

            loadedModel->materialIds.push_back(materialId);
            loadedModel->materials.push_back(importedMaterial);
            m_RenderResources.RegisterMaterial(materialId, *importedMaterial);
        }

        uploadBatch.Submit();

        for (std::size_t meshIndex = 0; meshIndex < loadedModel->meshes.size(); ++meshIndex) {
            const std::string meshId = idPrefix + "_Mesh" + std::to_string(meshIndex);
            loadedModel->meshIds.push_back(meshId);
            m_RenderResources.RegisterMesh(meshId, *loadedModel->meshes[meshIndex]);
            // Register LOD chain if generated
            if (meshIndex < loadedModel->lodChains.size() && !loadedModel->lodChains[meshIndex].Empty()) {
                m_RenderResources.RegisterMeshLodChain(meshId, loadedModel->lodChains[meshIndex]);
            }
        }

        for (std::size_t meshIndex = 0; meshIndex < importedModelData.meshes.size(); ++meshIndex) {
            const ImportedMesh3D& mesh = importedModelData.meshes[meshIndex];
            const std::size_t materialIndex = std::min<std::size_t>(
                mesh.materialIndex,
                loadedModel->materialIds.empty() ? 0 : loadedModel->materialIds.size() - 1
            );
            const std::string renderableName = mesh.name.empty()
                ? "Imported Model"
                : PathLabel(modelPath) + " / " + mesh.name;
            Renderable3D& importedPart = m_Scene.CreateRenderable(
                renderableName,
                loadedModel->meshIds[meshIndex],
                loadedModel->materialIds[materialIndex]
            );
            importedPart.Transform().SetPosition(position);
            importedPart.Transform().SetRotationDegrees(rotationDegrees);
            importedPart.Transform().SetScale(scale);
            importedPart.Transform().SetAnimateRotation(false);
            importedPart.Transform().SetRotationSpeedDegreesPerSecond({ 0.0f, 0.0f, 0.0f });
        }

        const std::size_t meshCount = importedModelData.meshes.size();
        const std::size_t materialCount = importedModelData.materials.size();
        loadedModel->sourceNodeCount = importedModelData.sourceNodeCount;
        loadedModel->sourceBoneNodeCount = importedModelData.sourceBoneNodeCount;
        loadedModel->sourceAnimationChannelBoundCount =
            importedModelData.sourceAnimationChannelBoundCount;
        loadedModel->sourceAnimationChannelUnboundCount =
            importedModelData.sourceAnimationChannelUnboundCount;
        loadedModel->sourceBoneNameMatchedNodeCount =
            importedModelData.sourceBoneNameMatchedNodeCount;
        loadedModel->sourceBoneNameUnmatchedCount =
            importedModelData.sourceBoneNameUnmatchedCount;
        loadedModel->sourceAnimationCount = importedModelData.sourceAnimationCount;
        loadedModel->sourceAnimationChannelCount =
            importedModelData.sourceAnimationChannelCount;
        loadedModel->sourceAnimationPositionKeyCount =
            importedModelData.sourceAnimationPositionKeyCount;
        loadedModel->sourceAnimationRotationKeyCount =
            importedModelData.sourceAnimationRotationKeyCount;
        loadedModel->sourceAnimationScaleKeyCount =
            importedModelData.sourceAnimationScaleKeyCount;
        loadedModel->sourceAnimationKeyCount =
            importedModelData.sourceAnimationKeyCount;
        loadedModel->sourceMaxAnimationKeysPerChannel =
            importedModelData.sourceMaxAnimationKeysPerChannel;
        loadedModel->sourcePoseSampledClipCount =
            importedModelData.sourcePoseSampledClipCount;
        loadedModel->sourcePoseSampledChannelCount =
            importedModelData.sourcePoseSampledChannelCount;
        loadedModel->sourcePoseSampledNodeCount =
            importedModelData.sourcePoseSampledNodeCount;
        loadedModel->sourcePoseAnimatedNodeCount =
            importedModelData.sourcePoseAnimatedNodeCount;
        loadedModel->sourcePoseBonePaletteEntryCount =
            importedModelData.sourcePoseBonePaletteEntryCount;
        loadedModel->sourcePosePreviousBonePaletteEntryCount =
            importedModelData.sourcePosePreviousBonePaletteEntryCount;
        loadedModel->sourcePoseChangedBonePaletteEntryCount =
            importedModelData.sourcePoseChangedBonePaletteEntryCount;
        loadedModel->sourcePoseBonePaletteReady =
            importedModelData.sourcePoseBonePaletteReady;
        if (!importedModelData.diagnosticPoseSamples.empty()) {
            const ImportedPoseSample3D& poseSample =
                importedModelData.diagnosticPoseSamples.front();
            loadedModel->runtimePreviousBonePalette =
                poseSample.previousBonePalette;
            loadedModel->runtimeCurrentBonePalette =
                poseSample.currentBonePalette;
            loadedModel->runtimePoseCarrierChangedBonePaletteEntryCount =
                CountChangedMatrices(
                    loadedModel->runtimePreviousBonePalette,
                    loadedModel->runtimeCurrentBonePalette
                );
            loadedModel->runtimePoseCarrierReady =
                importedModelData.sourcePoseBonePaletteReady != 0u &&
                !loadedModel->runtimeCurrentBonePalette.empty() &&
                loadedModel->runtimeCurrentBonePalette.size() ==
                    loadedModel->runtimePreviousBonePalette.size()
                    ? 1u
                    : 0u;
        }
        loadedModel->sourceMeshWithBonesCount = importedModelData.sourceMeshWithBonesCount;
        loadedModel->sourceBoneCount = importedModelData.sourceBoneCount;
        loadedModel->sourceSkinnedVertexCount = importedModelData.sourceSkinnedVertexCount;
        loadedModel->sourceBoneInfluenceCount = importedModelData.sourceBoneInfluenceCount;
        loadedModel->sourceMaxBoneInfluencesPerVertex =
            importedModelData.sourceMaxBoneInfluencesPerVertex;
        loadedModel->skinnedAnimationUnsupported =
            importedModelData.skinnedAnimationUnsupported ? 1u : 0u;
        const u32 runtimePoseCarrierBonePaletteEntryCount =
            static_cast<u32>(loadedModel->runtimeCurrentBonePalette.size());
        const u32 runtimePoseCarrierPreviousBonePaletteEntryCount =
            static_cast<u32>(loadedModel->runtimePreviousBonePalette.size());
        const u32 runtimePoseCarrierChangedBonePaletteEntryCount =
            loadedModel->runtimePoseCarrierChangedBonePaletteEntryCount;
        const u32 runtimePoseCarrierReady =
            loadedModel->runtimePoseCarrierReady;
        loadedModel->bonePaletteResourceId = idPrefix + "_BonePalette";
        const RendererBonePaletteRegistration rendererBonePalette =
            RegisterRendererBonePaletteResource(
                m_RenderResources,
                loadedModel->bonePaletteResourceId,
                loadedModel->runtimePreviousBonePalette,
                loadedModel->runtimeCurrentBonePalette,
                loadedModel->runtimePoseCarrierChangedBonePaletteEntryCount,
                loadedModel->runtimePoseCarrierReady
            );
        GpuBonePaletteUpload gpuBonePalette = CreateGpuBonePaletteBuffer(
            m_Device,
            m_PhysicalDevice,
            loadedModel->runtimePreviousBonePalette,
            loadedModel->runtimeCurrentBonePalette
        );
        const u32 gpuPosePaletteBufferAllocated = gpuBonePalette.allocated;
        const u32 gpuPosePaletteBufferUploaded = gpuBonePalette.uploaded;
        const u32 gpuPosePaletteDescriptorInfoReady =
            gpuBonePalette.descriptorInfoReady;
        const u32 gpuPosePaletteBufferBytes = gpuBonePalette.byteSize;
        const u32 gpuPosePaletteCurrentEntryCount =
            gpuBonePalette.currentEntryCount;
        const u32 gpuPosePalettePreviousEntryCount =
            gpuBonePalette.previousEntryCount;
        loadedModel->gpuPosePaletteBufferUploaded =
            gpuBonePalette.uploaded;
        loadedModel->gpuPosePaletteDescriptorInfoReady =
            gpuBonePalette.descriptorInfoReady;
        loadedModel->gpuPosePaletteCurrentEntryCount =
            gpuBonePalette.currentEntryCount;
        loadedModel->gpuPosePalettePreviousEntryCount =
            gpuBonePalette.previousEntryCount;
        loadedModel->gpuBonePaletteBuffer = std::move(gpuBonePalette.buffer);
        GpuBonePaletteDescriptorWrite gpuBonePaletteDescriptor =
            CreateGpuBonePaletteDescriptorWrite(
                m_Device,
                loadedModel->gpuBonePaletteBuffer.get()
            );
        const u32 gpuPosePaletteDescriptorSetAllocated =
            gpuBonePaletteDescriptor.allocated;
        const u32 gpuPosePaletteDescriptorSetWritten =
            gpuBonePaletteDescriptor.written;
        const u32 gpuPosePaletteDescriptorSetReady =
            gpuBonePaletteDescriptor.ready;
        const u32 gpuPosePaletteDescriptorBinding =
            gpuBonePaletteDescriptor.binding;
        const u32 gpuPosePaletteDescriptorRangeBytes =
            gpuBonePaletteDescriptor.rangeBytes;
        loadedModel->gpuPosePaletteDescriptorSetAllocated =
            gpuBonePaletteDescriptor.allocated;
        loadedModel->gpuPosePaletteDescriptorSetWritten =
            gpuBonePaletteDescriptor.written;
        loadedModel->gpuPosePaletteDescriptorSetReady =
            gpuBonePaletteDescriptor.ready;
        loadedModel->gpuPosePaletteDescriptorBinding =
            gpuBonePaletteDescriptor.binding;
        loadedModel->gpuPosePaletteDescriptorRangeBytes =
            gpuBonePaletteDescriptor.rangeBytes;
        loadedModel->gpuBonePaletteDescriptorSet =
            std::move(gpuBonePaletteDescriptor.descriptorSet);
        m_ModelCache[lookupKey] = m_LoadedModels.size();
        m_LoadedModels.push_back(std::move(loadedModel));

        return RuntimeModelLoadResult{
            true,
            "Loaded " + PathLabel(modelPath) + " (" +
                std::to_string(meshCount) + " mesh(es), " +
                std::to_string(materialCount) + " material(s))",
            false,
            static_cast<u32>(meshCount),
            static_cast<u32>(materialCount),
            importedModelData.sourceNodeCount,
            importedModelData.sourceBoneNodeCount,
            importedModelData.sourceAnimationChannelBoundCount,
            importedModelData.sourceAnimationChannelUnboundCount,
            importedModelData.sourceBoneNameMatchedNodeCount,
            importedModelData.sourceBoneNameUnmatchedCount,
            importedModelData.sourceAnimationCount,
            importedModelData.sourceAnimationChannelCount,
            importedModelData.sourceAnimationPositionKeyCount,
            importedModelData.sourceAnimationRotationKeyCount,
            importedModelData.sourceAnimationScaleKeyCount,
            importedModelData.sourceAnimationKeyCount,
            importedModelData.sourceMaxAnimationKeysPerChannel,
            importedModelData.sourcePoseSampledClipCount,
            importedModelData.sourcePoseSampledChannelCount,
            importedModelData.sourcePoseSampledNodeCount,
            importedModelData.sourcePoseAnimatedNodeCount,
            importedModelData.sourcePoseBonePaletteEntryCount,
            importedModelData.sourcePosePreviousBonePaletteEntryCount,
            importedModelData.sourcePoseChangedBonePaletteEntryCount,
            importedModelData.sourcePoseBonePaletteReady,
            runtimePoseCarrierBonePaletteEntryCount,
            runtimePoseCarrierPreviousBonePaletteEntryCount,
            runtimePoseCarrierChangedBonePaletteEntryCount,
            runtimePoseCarrierReady,
            rendererBonePalette.registered,
            rendererBonePalette.currentEntryCount,
            rendererBonePalette.previousEntryCount,
            rendererBonePalette.changedEntryCount,
            rendererBonePalette.ready,
            gpuPosePaletteBufferAllocated,
            gpuPosePaletteBufferUploaded,
            gpuPosePaletteDescriptorInfoReady,
            gpuPosePaletteDescriptorSetAllocated,
            gpuPosePaletteDescriptorSetWritten,
            gpuPosePaletteDescriptorSetReady,
            gpuPosePaletteDescriptorBinding,
            gpuPosePaletteDescriptorRangeBytes,
            gpuPosePaletteBufferBytes,
            gpuPosePaletteCurrentEntryCount,
            gpuPosePalettePreviousEntryCount,
            importedModelData.sourceMeshWithBonesCount,
            importedModelData.sourceBoneCount,
            importedModelData.sourceSkinnedVertexCount,
            importedModelData.sourceBoneInfluenceCount,
            importedModelData.sourceMaxBoneInfluencesPerVertex,
            importedModelData.skinnedAnimationUnsupported ? 1u : 0u
        };
    } catch (const std::exception& error) {
        return RuntimeModelLoadResult{
            false,
            std::string("Failed to load model: ") + error.what()
        };
    }
}

void RuntimeModelLoader::ForEachMaterial(
    const std::function<void(VulkanMaterial&)>& visitor
) const {
    if (!visitor) {
        return;
    }

    for (const std::unique_ptr<LoadedRuntimeModel>& model : m_LoadedModels) {
        for (VulkanMaterial* material : model->materials) {
            if (material != nullptr) {
                visitor(*material);
            }
        }
    }
}

}
