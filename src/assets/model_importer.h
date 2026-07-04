#pragma once

#include "scene/mesh_factory.h"

#include <filesystem>
#include <limits>
#include <glm/glm.hpp>

namespace se {

enum class ImportedTextureKind {
    BaseColor,
    Diffuse,
    Normal,
    Height,
    MetallicRoughness,
    Metallic,
    Roughness,
    Specular,
    AmbientOcclusion,
    Emissive,
    Opacity,
    Unknown,
    SpecularColor,
    Clearcoat,
    ClearcoatRoughness,
    Transmission
};

enum class ImportSeverity {
    Info,
    Warning,
    Error
};

enum class ImportedAlphaMode {
    Opaque,
    Mask,
    Blend
};

struct EmbeddedTextureData {
    std::string name;
    std::string formatHint;
    std::vector<u8> bytes;
    u32 width = 0;
    u32 height = 0;
    bool compressed = false;

    bool Empty() const;
};

struct ImportedTexture3D {
    ImportedTextureKind kind = ImportedTextureKind::Unknown;
    std::string semantic;
    i32 uvChannel = 0;
    glm::vec2 uvOffset{ 0.0f };
    glm::vec2 uvScale{ 1.0f };
    f32 uvRotation = 0.0f;
    bool hasUvTransform = false;
    bool isColorSpace = true;
    std::filesystem::path externalPath;
    EmbeddedTextureData embedded;

    bool HasEmbeddedTexture() const;
    bool HasAnyTexture() const;
};

struct ImportedMaterial3D {
    std::string name;
    glm::vec3 baseColor{ 0.78f };
    glm::vec3 emissiveColor{ 0.0f };
    glm::vec3 specularColor{ 1.0f };
    f32 metallicFactor = 0.0f;
    f32 roughnessFactor = 0.65f;
    f32 specularFactor = 1.0f;
    f32 clearcoatFactor = 0.0f;
    f32 clearcoatRoughness = 0.0f;
    f32 transmissionFactor = 0.0f;
    f32 volumeThicknessFactor = 0.0f;
    f32 volumeAttenuationDistance = 0.0f;
    glm::vec3 volumeAttenuationColor{ 1.0f };
    f32 normalScale = 1.0f;
    f32 occlusionStrength = 1.0f;
    f32 emissiveIntensity = 1.0f;
    f32 opacity = 1.0f;
    f32 alphaCutoff = 0.5f;
    ImportedAlphaMode alphaMode = ImportedAlphaMode::Opaque;
    bool doubleSided = false;
    std::string shadingModel;
    std::vector<ImportedTexture3D> textures;

    const ImportedTexture3D* FindTexture(ImportedTextureKind kind) const;
    const ImportedTexture3D* FindPrimaryTexture() const;
};

struct ImportedMesh3D {
    std::string name;
    MeshData3D mesh;
    u32 materialIndex = 0;
    struct Bone {
        std::string name;
        glm::mat4 offsetMatrix{ 1.0f };
    };
    struct BoneInfluence {
        u32 boneIndex = 0;
        f32 weight = 0.0f;
    };
    std::vector<Bone> bones;
    std::vector<std::vector<BoneInfluence>> vertexBoneInfluences;
    glm::vec3 boundsMin{ std::numeric_limits<f32>::max() };
    glm::vec3 boundsMax{ std::numeric_limits<f32>::lowest() };
};

struct ImportMessage {
    ImportSeverity severity = ImportSeverity::Info;
    std::string text;
};

struct ImportedModel3D {
    std::filesystem::path sourcePath;
    std::vector<ImportedMesh3D> meshes;
    std::vector<ImportedMaterial3D> materials;
    std::vector<ImportMessage> messages;
    u32 sourceMeshCount = 0;
    u32 sourceMaterialCount = 0;
    u32 sourceAnimationCount = 0;
    u32 sourceMeshWithBonesCount = 0;
    u32 sourceBoneCount = 0;
    u32 sourceSkinnedVertexCount = 0;
    u32 sourceBoneInfluenceCount = 0;
    u32 sourceMaxBoneInfluencesPerVertex = 0;
    bool skinnedAnimationUnsupported = false;
    glm::vec3 boundsMin{ -0.5f };
    glm::vec3 boundsMax{ 0.5f };

    bool Empty() const;
    std::size_t VertexCount() const;
    std::size_t TriangleCount() const;
};

struct ModelImportOptions {
    bool rebuildNormals = false;
    bool fastImport = false;
    bool readSkinningMetadata = false;
    bool validateScene = true;
    bool optimizeMeshes = true;
};

class ModelImporter {
public:
    static ImportedModel3D LoadModel3D(
        const std::filesystem::path& path,
        const ModelImportOptions& options = ModelImportOptions{}
    );

    static MeshData3D LoadMeshData3D(const std::filesystem::path& path);
};

}
