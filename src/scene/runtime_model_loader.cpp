#include "scene/runtime_model_loader.h"

#include "renderer/vulkan/material.h"
#include "renderer/vulkan/material_library.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "renderer/vulkan/upload_batch.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>

namespace se {

namespace {

constexpr bool kUploadAuxiliaryTexturesOnImport = true;

struct ResolvedTextureSource {
    const ImportedTexture3D* texture = nullptr;
    std::optional<std::string> externalPath;

    bool UsesFallback() const {
        return texture == nullptr;
    }
};

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
                true};
        }

        ModelImportOptions importOptions{};
        importOptions.fastImport = true;
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
        for (ImportedMesh3D& importedMeshData : importedModelData.meshes) {
            loadedModel->meshes.push_back(std::make_unique<VulkanMesh>(
                m_Device,
                m_PhysicalDevice,
                m_CommandPool,
                std::move(importedMeshData.mesh.vertices),
                std::move(importedMeshData.mesh.indices),
                &uploadBatch
            ));
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
        m_ModelCache[lookupKey] = m_LoadedModels.size();
        m_LoadedModels.push_back(std::move(loadedModel));

        return RuntimeModelLoadResult{
            true,
            "Loaded " + PathLabel(modelPath) + " (" +
                std::to_string(meshCount) + " mesh(es), " +
                std::to_string(materialCount) + " material(s))",
            false
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
