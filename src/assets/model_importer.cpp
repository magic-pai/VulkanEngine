#include "assets/model_importer.h"

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/texture.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace se {

namespace {

glm::vec3 ToVec3(const aiVector3D& value) {
    return glm::vec3(value.x, value.y, value.z);
}

glm::vec3 ToVec3(const aiColor3D& value) {
    return glm::vec3(value.r, value.g, value.b);
}

glm::vec3 ToVec3(const aiColor4D& value) {
    return glm::vec3(value.r, value.g, value.b);
}

glm::vec2 ToVec2(const aiVector3D& value) {
    return glm::vec2(value.x, value.y);
}

glm::mat4 ToMat4(const aiMatrix4x4& value) {
    return glm::mat4(
        value.a1, value.b1, value.c1, value.d1,
        value.a2, value.b2, value.c2, value.d2,
        value.a3, value.b3, value.c3, value.d3,
        value.a4, value.b4, value.c4, value.d4
    );
}

glm::quat ToQuat(const aiQuaternion& value) {
    return glm::quat(value.w, value.x, value.y, value.z);
}

std::string ToString(const aiString& value) {
    return std::string(value.C_Str());
}

std::string PathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (size <= 0) {
        return {};
    }

    std::string text(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        text.data(),
        size,
        nullptr,
        nullptr
    );
    return text;
#else
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
#endif
}

std::filesystem::path PathFromModelText(const std::string& text) {
    if (text.empty()) {
        return {};
    }

#ifdef _WIN32
    const int wideSize = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (wideSize > 0) {
        std::wstring wide(static_cast<std::size_t>(wideSize), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            wideSize
        );
        return std::filesystem::path(wide);
    }
#endif

    return std::filesystem::path(text);
}

ImportedTextureKind MapTextureKind(aiTextureType type) {
    switch (type) {
    case aiTextureType_BASE_COLOR: return ImportedTextureKind::BaseColor;
    case aiTextureType_DIFFUSE: return ImportedTextureKind::Diffuse;
    case aiTextureType_NORMALS:
    case aiTextureType_NORMAL_CAMERA: return ImportedTextureKind::Normal;
    case aiTextureType_HEIGHT:
    case aiTextureType_DISPLACEMENT: return ImportedTextureKind::Height;
    case aiTextureType_GLTF_METALLIC_ROUGHNESS: return ImportedTextureKind::MetallicRoughness;
    case aiTextureType_METALNESS: return ImportedTextureKind::Metallic;
    case aiTextureType_DIFFUSE_ROUGHNESS: return ImportedTextureKind::Roughness;
    case aiTextureType_SPECULAR: return ImportedTextureKind::Specular;
    case aiTextureType_AMBIENT_OCCLUSION:
    case aiTextureType_LIGHTMAP: return ImportedTextureKind::AmbientOcclusion;
    case aiTextureType_EMISSIVE: return ImportedTextureKind::Emissive;
    case aiTextureType_OPACITY: return ImportedTextureKind::Opacity;
    case aiTextureType_CLEARCOAT: return ImportedTextureKind::Clearcoat;
    case aiTextureType_TRANSMISSION: return ImportedTextureKind::Transmission;
    default: return ImportedTextureKind::Unknown;
    }
}

bool IsColorSpaceTexture(ImportedTextureKind kind) {
    return kind == ImportedTextureKind::BaseColor ||
        kind == ImportedTextureKind::Diffuse ||
        kind == ImportedTextureKind::Emissive ||
        kind == ImportedTextureKind::SpecularColor;
}

std::filesystem::path ResolveTexturePath(
    const std::filesystem::path& modelPath,
    const std::string& texturePath
) {
    if (texturePath.empty() || texturePath[0] == '*') {
        return {};
    }

    const std::filesystem::path parsed = PathFromModelText(texturePath);
    std::error_code error;
    if (parsed.is_absolute() && std::filesystem::is_regular_file(parsed, error) && !error) {
        return parsed;
    }

    const std::filesystem::path modelDirectory = modelPath.parent_path();
    const std::array<std::filesystem::path, 4> candidates = {
        modelDirectory / parsed,
        modelDirectory / parsed.filename(),
        modelDirectory.parent_path() / parsed,
        modelDirectory.parent_path() / parsed.filename()
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }

    return parsed;
}

std::string TextureHint(const aiTexture& texture) {
    std::size_t length = 0;
    while (length < sizeof(texture.achFormatHint) &&
           texture.achFormatHint[length] != '\0') {
        ++length;
    }

    return std::string(texture.achFormatHint, length);
}

bool ParseEmbeddedTextureIndex(const std::string& texturePath, unsigned int& index) {
    if (texturePath.size() < 2 || texturePath[0] != '*') {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(texturePath.c_str() + 1, &end, 10);
    if (end == nullptr || *end != '\0') {
        return false;
    }

    index = static_cast<unsigned int>(parsed);
    return true;
}

bool SameTextureName(const std::string& left, const std::string& right) {
    if (left == right) {
        return true;
    }

    const std::filesystem::path leftPath = PathFromModelText(left);
    const std::filesystem::path rightPath = PathFromModelText(right);
    return !leftPath.filename().empty() && leftPath.filename() == rightPath.filename();
}

const aiTexture* FindEmbeddedTexture(const aiScene* scene, const std::string& texturePath) {
    if (scene == nullptr || scene->mNumTextures == 0 || texturePath.empty()) {
        return nullptr;
    }

    unsigned int index = 0;
    if (ParseEmbeddedTextureIndex(texturePath, index) && index < scene->mNumTextures) {
        return scene->mTextures[index];
    }

    for (unsigned int textureIndex = 0; textureIndex < scene->mNumTextures; ++textureIndex) {
        const aiTexture* texture = scene->mTextures[textureIndex];
        if (texture != nullptr && SameTextureName(ToString(texture->mFilename), texturePath)) {
            return texture;
        }
    }

    return nullptr;
}

bool CopyEmbeddedTexture(const aiTexture* texture, EmbeddedTextureData& outTexture) {
    if (texture == nullptr || texture->pcData == nullptr || texture->mWidth == 0) {
        return false;
    }

    outTexture = {};
    outTexture.name = ToString(texture->mFilename);
    outTexture.formatHint = TextureHint(*texture);

    if (texture->mHeight == 0) {
        const auto* begin = reinterpret_cast<const u8*>(texture->pcData);
        outTexture.bytes.assign(begin, begin + texture->mWidth);
        outTexture.compressed = true;
        return !outTexture.bytes.empty();
    }

    outTexture.width = texture->mWidth;
    outTexture.height = texture->mHeight;
    outTexture.compressed = false;
    outTexture.bytes.resize(
        static_cast<std::size_t>(outTexture.width) *
        static_cast<std::size_t>(outTexture.height) *
        4
    );

    for (std::size_t i = 0;
         i < static_cast<std::size_t>(outTexture.width) * outTexture.height;
         ++i) {
        const aiTexel& texel = texture->pcData[i];
        outTexture.bytes[i * 4 + 0] = texel.r;
        outTexture.bytes[i * 4 + 1] = texel.g;
        outTexture.bytes[i * 4 + 2] = texel.b;
        outTexture.bytes[i * 4 + 3] = texel.a;
    }

    return true;
}

std::string ShadingModelToString(const aiMaterial* source) {
    int shadingModel = 0;
    if (source->Get(AI_MATKEY_SHADING_MODEL, shadingModel) != AI_SUCCESS) {
        return {};
    }

    switch (static_cast<aiShadingMode>(shadingModel)) {
    case aiShadingMode_Flat: return "Flat";
    case aiShadingMode_Gouraud: return "Gouraud";
    case aiShadingMode_Phong: return "Phong";
    case aiShadingMode_Blinn: return "Blinn";
    case aiShadingMode_Toon: return "Toon";
    case aiShadingMode_OrenNayar: return "OrenNayar";
    case aiShadingMode_Minnaert: return "Minnaert";
    case aiShadingMode_CookTorrance: return "CookTorrance";
    case aiShadingMode_NoShading: return "NoShading";
    case aiShadingMode_Fresnel: return "Fresnel";
    case aiShadingMode_PBR_BRDF: return "PBR_BRDF";
    default: return {};
    }
}

ImportedAlphaMode AlphaModeFromString(const std::string& value) {
    if (value == "MASK" || value == "mask" || value == "Mask") {
        return ImportedAlphaMode::Mask;
    }
    if (value == "BLEND" || value == "blend" || value == "Blend") {
        return ImportedAlphaMode::Blend;
    }

    return ImportedAlphaMode::Opaque;
}

void AppendMaterialTextures(
    const aiScene* scene,
    const aiMaterial* source,
    const std::filesystem::path& modelPath,
    ImportedMaterial3D& material
) {
    static const aiTextureType kOrderedTypes[] = {
        aiTextureType_BASE_COLOR,
        aiTextureType_DIFFUSE,
        aiTextureType_NORMALS,
        aiTextureType_NORMAL_CAMERA,
        aiTextureType_HEIGHT,
        aiTextureType_DISPLACEMENT,
        aiTextureType_GLTF_METALLIC_ROUGHNESS,
        aiTextureType_METALNESS,
        aiTextureType_DIFFUSE_ROUGHNESS,
        aiTextureType_SPECULAR,
        aiTextureType_CLEARCOAT,
        aiTextureType_TRANSMISSION,
        aiTextureType_AMBIENT_OCCLUSION,
        aiTextureType_EMISSIVE,
        aiTextureType_OPACITY,
        aiTextureType_LIGHTMAP,
        aiTextureType_UNKNOWN,
        aiTextureType_AMBIENT
    };

    for (aiTextureType type : kOrderedTypes) {
        const unsigned int textureCount = source->GetTextureCount(type);
        for (unsigned int textureIndex = 0; textureIndex < textureCount; ++textureIndex) {
            aiString texturePath;
            unsigned int uvIndex = 0;
            if (source->GetTexture(type, textureIndex, &texturePath, nullptr, &uvIndex, nullptr, nullptr, nullptr) != AI_SUCCESS) {
                continue;
            }

            const std::string texturePathUtf8 = ToString(texturePath);
            ImportedTexture3D texture;
            texture.kind = MapTextureKind(type);
            if (type == aiTextureType_SPECULAR && textureIndex > 0) {
                texture.kind = ImportedTextureKind::SpecularColor;
            }
            if (type == aiTextureType_CLEARCOAT) {
                if (textureIndex == 1) {
                    texture.kind = ImportedTextureKind::ClearcoatRoughness;
                } else if (textureIndex > 1) {
                    texture.kind = ImportedTextureKind::Unknown;
                }
            }
            texture.semantic = texturePathUtf8;
            texture.uvChannel = static_cast<i32>(uvIndex);

            int uvSource = texture.uvChannel;
            if (source->Get(AI_MATKEY_UVWSRC(type, textureIndex), uvSource) == AI_SUCCESS) {
                texture.uvChannel = uvSource;
            }

            aiUVTransform uvTransform;
            if (source->Get(AI_MATKEY_UVTRANSFORM(type, textureIndex), uvTransform) == AI_SUCCESS) {
                texture.uvOffset = glm::vec2(uvTransform.mTranslation.x, uvTransform.mTranslation.y);
                texture.uvScale = glm::vec2(uvTransform.mScaling.x, uvTransform.mScaling.y);
                texture.uvRotation = uvTransform.mRotation;
                texture.hasUvTransform = true;
            }

            texture.isColorSpace = IsColorSpaceTexture(texture.kind);
            texture.externalPath = ResolveTexturePath(modelPath, texturePathUtf8);
            if (const aiTexture* embeddedTexture = FindEmbeddedTexture(scene, texturePathUtf8)) {
                CopyEmbeddedTexture(embeddedTexture, texture.embedded);
            }
            material.textures.push_back(std::move(texture));
        }
    }
}

void PopulateMaterial(
    const aiScene* scene,
    const aiMaterial* source,
    const std::filesystem::path& modelPath,
    ImportedMaterial3D& material
) {
    aiString name;
    if (source->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
        material.name = ToString(name);
    }

    bool hasBaseColorFactor = false;
    aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
    if (source->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
        material.baseColor = ToVec3(baseColor);
        material.opacity = baseColor.a;
        hasBaseColorFactor = true;
    } else {
        aiColor3D diffuse(0.78f, 0.78f, 0.78f);
        if (source->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
            material.baseColor = ToVec3(diffuse);
            hasBaseColorFactor = true;
        }
    }

    aiColor3D emissive(0.0f, 0.0f, 0.0f);
    if (source->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
        material.emissiveColor = ToVec3(emissive);
    }

    aiColor3D specular(1.0f, 1.0f, 1.0f);
    if (source->Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS) {
        material.specularColor = ToVec3(specular);
    }

    if (source->Get(AI_MATKEY_SPECULAR_FACTOR, material.specularFactor) != AI_SUCCESS) {
        material.specularFactor = 1.0f;
    }
    source->Get(AI_MATKEY_CLEARCOAT_FACTOR, material.clearcoatFactor);
    source->Get(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, material.clearcoatRoughness);
    source->Get(AI_MATKEY_TRANSMISSION_FACTOR, material.transmissionFactor);
    source->Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, material.volumeThicknessFactor);
    source->Get(AI_MATKEY_VOLUME_ATTENUATION_DISTANCE, material.volumeAttenuationDistance);
    aiColor3D volumeAttenuationColor(1.0f, 1.0f, 1.0f);
    if (source->Get(AI_MATKEY_VOLUME_ATTENUATION_COLOR, volumeAttenuationColor) == AI_SUCCESS) {
        material.volumeAttenuationColor = ToVec3(volumeAttenuationColor);
    }

    f32 opacity = 1.0f;
    if (source->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
        material.opacity = opacity;
    }

    aiString alphaMode;
    if (source->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
        material.alphaMode = AlphaModeFromString(ToString(alphaMode));
    }
    source->Get(AI_MATKEY_GLTF_ALPHACUTOFF, material.alphaCutoff);

    bool hasMetallicFactor = false;
    bool hasRoughnessFactor = false;
    if (source->Get(AI_MATKEY_METALLIC_FACTOR, material.metallicFactor) == AI_SUCCESS) {
        hasMetallicFactor = true;
    }
    if (source->Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughnessFactor) == AI_SUCCESS) {
        hasRoughnessFactor = true;
    }

    source->Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), material.normalScale);
    source->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_AMBIENT_OCCLUSION, 0), material.occlusionStrength);
    source->Get(AI_MATKEY_EMISSIVE_INTENSITY, material.emissiveIntensity);

    int twoSided = 0;
    if (source->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) {
        material.doubleSided = twoSided != 0;
    }

    material.shadingModel = ShadingModelToString(source);
    AppendMaterialTextures(scene, source, modelPath, material);

    const bool hasBaseColorTexture =
        material.FindTexture(ImportedTextureKind::BaseColor) ||
        material.FindTexture(ImportedTextureKind::Diffuse);
    const bool hasMetallicRoughnessTexture =
        material.FindTexture(ImportedTextureKind::MetallicRoughness) ||
        material.FindTexture(ImportedTextureKind::Metallic) ||
        material.FindTexture(ImportedTextureKind::Roughness);

    if (hasBaseColorTexture && !hasBaseColorFactor) {
        material.baseColor = glm::vec3(1.0f);
    }
    if (hasMetallicRoughnessTexture && !hasMetallicFactor) {
        material.metallicFactor = 1.0f;
    }
    if (hasMetallicRoughnessTexture && !hasRoughnessFactor) {
        material.roughnessFactor = 1.0f;
    }
}

std::array<f32, 3> ToArray(const glm::vec3& value) {
    return { value.x, value.y, value.z };
}

std::array<f32, 2> ToArray(const glm::vec2& value) {
    return { value.x, value.y };
}

std::array<f32, 4> ToArray(const glm::vec4& value) {
    return { value.x, value.y, value.z, value.w };
}

glm::vec3 SafeNormal(const glm::vec3& normal) {
    if (glm::dot(normal, normal) <= 0.000001f) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    return glm::normalize(normal);
}

glm::vec4 FallbackTangent(const glm::vec3& normal) {
    const glm::vec3 reference = std::abs(normal.y) < 0.999f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangent = SafeNormal(glm::cross(reference, normal));

    return glm::vec4(tangent, 1.0f);
}

glm::vec4 ReadTangent(
    const aiMesh* mesh,
    const glm::mat3& normalMatrix,
    const glm::vec3& normal,
    u32 vertexIndex
) {
    if (mesh == nullptr || !mesh->HasTangentsAndBitangents()) {
        return FallbackTangent(normal);
    }

    glm::vec3 tangent = normalMatrix * ToVec3(mesh->mTangents[vertexIndex]);
    tangent = tangent - normal * glm::dot(normal, tangent);
    if (glm::dot(tangent, tangent) <= 0.000001f) {
        return FallbackTangent(normal);
    }

    tangent = glm::normalize(tangent);
    const glm::vec3 bitangent = SafeNormal(normalMatrix * ToVec3(mesh->mBitangents[vertexIndex]));
    const f32 handedness = glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f
        ? -1.0f
        : 1.0f;

    return glm::vec4(tangent, handedness);
}

glm::vec3 VertexColor(const aiMesh* mesh, u32 vertexIndex) {
    if (mesh != nullptr && mesh->HasVertexColors(0)) {
        const aiColor4D& color = mesh->mColors[0][vertexIndex];
        return glm::vec3(
            std::clamp(color.r, 0.0f, 1.0f),
            std::clamp(color.g, 0.0f, 1.0f),
            std::clamp(color.b, 0.0f, 1.0f)
        );
    }

    return glm::vec3(1.0f);
}

glm::vec2 ReadTexCoord(const aiMesh* mesh, u32 channel, u32 vertexIndex) {
    if (mesh == nullptr || !mesh->HasTextureCoords(channel) || vertexIndex >= mesh->mNumVertices) {
        return glm::vec2(0.0f);
    }

    return ToVec2(mesh->mTextureCoords[channel][vertexIndex]);
}

u32 PrimaryUvChannel(const ImportedMaterial3D& material) {
    const ImportedTexture3D* primaryTexture = material.FindPrimaryTexture();
    if (primaryTexture == nullptr || primaryTexture->uvChannel < 0) {
        return 0;
    }

    return static_cast<u32>(primaryTexture->uvChannel);
}

void PopulateModelSourceDiagnostics(ImportedModel3D& model, const aiScene* scene) {
    SE_ASSERT(scene != nullptr, "Assimp scene must not be null");

    model.sourceMeshCount = scene->mNumMeshes;
    model.sourceMaterialCount = scene->mNumMaterials;
    model.sourceAnimationCount = scene->mNumAnimations;
    model.sourceMeshWithBonesCount = 0;
    model.sourceBoneCount = 0;
    model.sourceSkinnedVertexCount = 0;
    model.sourceBoneInfluenceCount = 0;
    model.sourceMaxBoneInfluencesPerVertex = 0;

    for (u32 meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr || mesh->mNumBones == 0) {
            continue;
        }

        ++model.sourceMeshWithBonesCount;
        model.sourceBoneCount += mesh->mNumBones;

        std::vector<u32> influencesPerVertex(mesh->mNumVertices, 0u);
        for (u32 boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            const aiBone* bone = mesh->mBones[boneIndex];
            if (bone == nullptr) {
                continue;
            }

            for (u32 weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                const aiVertexWeight& weight = bone->mWeights[weightIndex];
                if (weight.mVertexId >= influencesPerVertex.size() ||
                    weight.mWeight <= 0.0f) {
                    continue;
                }

                ++model.sourceBoneInfluenceCount;
                ++influencesPerVertex[weight.mVertexId];
            }
        }

        for (u32 influenceCount : influencesPerVertex) {
            if (influenceCount == 0u) {
                continue;
            }

            ++model.sourceSkinnedVertexCount;
            model.sourceMaxBoneInfluencesPerVertex =
                std::max(model.sourceMaxBoneInfluencesPerVertex, influenceCount);
        }
    }

    model.skinnedAnimationUnsupported =
        model.sourceAnimationCount > 0 ||
        model.sourceMeshWithBonesCount > 0 ||
        model.sourceBoneCount > 0;
}

void PopulateModelAnimations(ImportedModel3D& model, const aiScene* scene) {
    SE_ASSERT(scene != nullptr, "Assimp scene must not be null");

    model.animations.clear();
    model.animations.reserve(scene->mNumAnimations);
    model.sourceAnimationChannelCount = 0;
    model.sourceAnimationPositionKeyCount = 0;
    model.sourceAnimationRotationKeyCount = 0;
    model.sourceAnimationScaleKeyCount = 0;
    model.sourceAnimationKeyCount = 0;
    model.sourceMaxAnimationKeysPerChannel = 0;

    for (u32 animationIndex = 0;
         animationIndex < scene->mNumAnimations;
         ++animationIndex) {
        const aiAnimation* sourceAnimation = scene->mAnimations[animationIndex];
        if (sourceAnimation == nullptr) {
            continue;
        }

        ImportedAnimationClip3D clip;
        clip.name = sourceAnimation->mName.length > 0
            ? ToString(sourceAnimation->mName)
            : "Animation" + std::to_string(animationIndex);
        clip.durationTicks = sourceAnimation->mDuration;
        clip.ticksPerSecond = sourceAnimation->mTicksPerSecond;
        clip.channels.reserve(sourceAnimation->mNumChannels);

        for (u32 channelIndex = 0;
             channelIndex < sourceAnimation->mNumChannels;
             ++channelIndex) {
            const aiNodeAnim* sourceChannel =
                sourceAnimation->mChannels[channelIndex];
            if (sourceChannel == nullptr) {
                continue;
            }

            ImportedAnimationChannel3D channel;
            channel.nodeName = ToString(sourceChannel->mNodeName);
            channel.positions.reserve(sourceChannel->mNumPositionKeys);
            channel.rotations.reserve(sourceChannel->mNumRotationKeys);
            channel.scales.reserve(sourceChannel->mNumScalingKeys);

            for (u32 keyIndex = 0;
                 keyIndex < sourceChannel->mNumPositionKeys;
                 ++keyIndex) {
                const aiVectorKey& key = sourceChannel->mPositionKeys[keyIndex];
                channel.positions.push_back({ key.mTime, ToVec3(key.mValue) });
            }
            for (u32 keyIndex = 0;
                 keyIndex < sourceChannel->mNumRotationKeys;
                 ++keyIndex) {
                const aiQuatKey& key = sourceChannel->mRotationKeys[keyIndex];
                channel.rotations.push_back({ key.mTime, ToQuat(key.mValue) });
            }
            for (u32 keyIndex = 0;
                 keyIndex < sourceChannel->mNumScalingKeys;
                 ++keyIndex) {
                const aiVectorKey& key = sourceChannel->mScalingKeys[keyIndex];
                channel.scales.push_back({ key.mTime, ToVec3(key.mValue) });
            }

            const u32 channelKeyCount =
                static_cast<u32>(channel.positions.size() +
                    channel.rotations.size() +
                    channel.scales.size());
            ++model.sourceAnimationChannelCount;
            model.sourceAnimationPositionKeyCount +=
                static_cast<u32>(channel.positions.size());
            model.sourceAnimationRotationKeyCount +=
                static_cast<u32>(channel.rotations.size());
            model.sourceAnimationScaleKeyCount +=
                static_cast<u32>(channel.scales.size());
            model.sourceAnimationKeyCount += channelKeyCount;
            model.sourceMaxAnimationKeysPerChannel =
                std::max(model.sourceMaxAnimationKeysPerChannel, channelKeyCount);

            clip.channels.push_back(std::move(channel));
        }

        model.animations.push_back(std::move(clip));
    }
}

std::unordered_set<std::string> CollectSourceBoneNames(const aiScene* scene) {
    SE_ASSERT(scene != nullptr, "Assimp scene must not be null");

    std::unordered_set<std::string> names;
    for (u32 meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr) {
            continue;
        }

        for (u32 boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            const aiBone* bone = mesh->mBones[boneIndex];
            if (bone == nullptr || bone->mName.length == 0) {
                continue;
            }

            names.insert(ToString(bone->mName));
        }
    }

    return names;
}

std::unordered_set<std::string> CollectAnimationChannelNames(
    const ImportedModel3D& model
) {
    std::unordered_set<std::string> names;
    for (const ImportedAnimationClip3D& clip : model.animations) {
        for (const ImportedAnimationChannel3D& channel : clip.channels) {
            if (!channel.nodeName.empty()) {
                names.insert(channel.nodeName);
            }
        }
    }

    return names;
}

void AppendImportedNode(
    ImportedModel3D& model,
    const aiNode* node,
    i32 parentIndex,
    const std::unordered_set<std::string>& boneNames,
    const std::unordered_set<std::string>& animationChannelNames
) {
    SE_ASSERT(node != nullptr, "Assimp node must not be null");

    const std::string nodeName = node->mName.C_Str();
    ImportedNode3D importedNode;
    importedNode.name = nodeName;
    importedNode.parentIndex = parentIndex;
    importedNode.localTransform = ToMat4(node->mTransformation);
    importedNode.meshReferenceCount = node->mNumMeshes;
    importedNode.boneReferenced = boneNames.find(nodeName) != boneNames.end();
    importedNode.animationChannelTarget =
        animationChannelNames.find(nodeName) != animationChannelNames.end();

    const i32 nodeIndex = static_cast<i32>(model.nodes.size());
    model.nodes.push_back(std::move(importedNode));

    for (u32 childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        if (node->mChildren[childIndex] != nullptr) {
            AppendImportedNode(
                model,
                node->mChildren[childIndex],
                nodeIndex,
                boneNames,
                animationChannelNames
            );
        }
    }
}

void PopulateModelNodeHierarchy(ImportedModel3D& model, const aiScene* scene) {
    SE_ASSERT(scene != nullptr, "Assimp scene must not be null");
    SE_ASSERT(scene->mRootNode != nullptr, "Assimp root node must not be null");

    model.nodes.clear();
    model.sourceNodeCount = 0;
    model.sourceBoneNodeCount = 0;
    model.sourceAnimationChannelBoundCount = 0;
    model.sourceAnimationChannelUnboundCount = 0;
    model.sourceBoneNameMatchedNodeCount = 0;
    model.sourceBoneNameUnmatchedCount = 0;

    const std::unordered_set<std::string> boneNames =
        CollectSourceBoneNames(scene);
    const std::unordered_set<std::string> animationChannelNames =
        CollectAnimationChannelNames(model);
    AppendImportedNode(
        model,
        scene->mRootNode,
        -1,
        boneNames,
        animationChannelNames
    );

    std::unordered_set<std::string> nodeNames;
    nodeNames.reserve(model.nodes.size());
    for (const ImportedNode3D& node : model.nodes) {
        nodeNames.insert(node.name);
        if (node.boneReferenced) {
            ++model.sourceBoneNodeCount;
        }
    }

    for (const ImportedAnimationClip3D& clip : model.animations) {
        for (const ImportedAnimationChannel3D& channel : clip.channels) {
            if (nodeNames.find(channel.nodeName) != nodeNames.end()) {
                ++model.sourceAnimationChannelBoundCount;
            } else {
                ++model.sourceAnimationChannelUnboundCount;
            }
        }
    }

    for (const std::string& boneName : boneNames) {
        if (nodeNames.find(boneName) != nodeNames.end()) {
            ++model.sourceBoneNameMatchedNodeCount;
        } else {
            ++model.sourceBoneNameUnmatchedCount;
        }
    }

    model.sourceNodeCount = static_cast<u32>(model.nodes.size());
}

struct TransformComponents {
    glm::vec3 translation{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f };
};

TransformComponents DecomposeTransform(const glm::mat4& transform) {
    TransformComponents components;
    components.translation = glm::vec3(transform[3]);
    components.scale = glm::vec3(
        glm::length(glm::vec3(transform[0])),
        glm::length(glm::vec3(transform[1])),
        glm::length(glm::vec3(transform[2]))
    );

    glm::mat3 rotationMatrix{ 1.0f };
    if (components.scale.x > 0.0f) {
        rotationMatrix[0] = glm::vec3(transform[0]) / components.scale.x;
    }
    if (components.scale.y > 0.0f) {
        rotationMatrix[1] = glm::vec3(transform[1]) / components.scale.y;
    }
    if (components.scale.z > 0.0f) {
        rotationMatrix[2] = glm::vec3(transform[2]) / components.scale.z;
    }
    components.rotation = glm::normalize(glm::quat_cast(rotationMatrix));
    return components;
}

glm::mat4 ComposeTransform(const TransformComponents& components) {
    return glm::translate(glm::mat4(1.0f), components.translation) *
        glm::mat4_cast(components.rotation) *
        glm::scale(glm::mat4(1.0f), components.scale);
}

glm::vec3 SampleVec3Keys(
    const std::vector<ImportedAnimationVec3Key>& keys,
    f64 timeTicks,
    glm::vec3 fallback
) {
    if (keys.empty()) {
        return fallback;
    }
    if (timeTicks <= keys.front().time) {
        return keys.front().value;
    }
    if (timeTicks >= keys.back().time) {
        return keys.back().value;
    }

    for (std::size_t keyIndex = 1; keyIndex < keys.size(); ++keyIndex) {
        const ImportedAnimationVec3Key& next = keys[keyIndex];
        if (timeTicks > next.time) {
            continue;
        }

        const ImportedAnimationVec3Key& previous = keys[keyIndex - 1];
        const f64 duration = next.time - previous.time;
        const f32 t = duration > 0.0
            ? static_cast<f32>((timeTicks - previous.time) / duration)
            : 0.0f;
        return glm::mix(previous.value, next.value, std::clamp(t, 0.0f, 1.0f));
    }

    return keys.back().value;
}

glm::quat SampleQuatKeys(
    const std::vector<ImportedAnimationQuatKey>& keys,
    f64 timeTicks,
    glm::quat fallback
) {
    if (keys.empty()) {
        return fallback;
    }
    if (timeTicks <= keys.front().time) {
        return glm::normalize(keys.front().value);
    }
    if (timeTicks >= keys.back().time) {
        return glm::normalize(keys.back().value);
    }

    for (std::size_t keyIndex = 1; keyIndex < keys.size(); ++keyIndex) {
        const ImportedAnimationQuatKey& next = keys[keyIndex];
        if (timeTicks > next.time) {
            continue;
        }

        const ImportedAnimationQuatKey& previous = keys[keyIndex - 1];
        const f64 duration = next.time - previous.time;
        const f32 t = duration > 0.0
            ? static_cast<f32>((timeTicks - previous.time) / duration)
            : 0.0f;
        return glm::normalize(
            glm::slerp(previous.value, next.value, std::clamp(t, 0.0f, 1.0f))
        );
    }

    return glm::normalize(keys.back().value);
}

f64 LastAnimationKeyTime(const ImportedAnimationClip3D& clip) {
    f64 result = 0.0;
    for (const ImportedAnimationChannel3D& channel : clip.channels) {
        if (!channel.positions.empty()) {
            result = std::max(result, channel.positions.back().time);
        }
        if (!channel.rotations.empty()) {
            result = std::max(result, channel.rotations.back().time);
        }
        if (!channel.scales.empty()) {
            result = std::max(result, channel.scales.back().time);
        }
    }

    return result;
}

std::unordered_map<std::string, u32> BuildNodeNameLookup(
    const ImportedModel3D& model
) {
    std::unordered_map<std::string, u32> lookup;
    lookup.reserve(model.nodes.size());
    for (u32 nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
        if (!model.nodes[nodeIndex].name.empty()) {
            lookup[model.nodes[nodeIndex].name] = nodeIndex;
        }
    }

    return lookup;
}

std::vector<glm::mat4> SampleLocalPose(
    const ImportedModel3D& model,
    const ImportedAnimationClip3D& clip,
    const std::unordered_map<std::string, u32>& nodeLookup,
    f64 timeTicks
) {
    std::vector<glm::mat4> localTransforms;
    localTransforms.reserve(model.nodes.size());
    for (const ImportedNode3D& node : model.nodes) {
        localTransforms.push_back(node.localTransform);
    }

    for (const ImportedAnimationChannel3D& channel : clip.channels) {
        const auto foundNode = nodeLookup.find(channel.nodeName);
        if (foundNode == nodeLookup.end()) {
            continue;
        }

        const u32 nodeIndex = foundNode->second;
        const TransformComponents base =
            DecomposeTransform(model.nodes[nodeIndex].localTransform);
        TransformComponents sampled = base;
        sampled.translation =
            SampleVec3Keys(channel.positions, timeTicks, base.translation);
        sampled.rotation =
            SampleQuatKeys(channel.rotations, timeTicks, base.rotation);
        sampled.scale =
            SampleVec3Keys(channel.scales, timeTicks, base.scale);
        localTransforms[nodeIndex] = ComposeTransform(sampled);
    }

    return localTransforms;
}

std::vector<glm::mat4> BuildGlobalPose(
    const ImportedModel3D& model,
    const std::vector<glm::mat4>& localTransforms
) {
    std::vector<glm::mat4> globalTransforms(
        localTransforms.size(),
        glm::mat4(1.0f)
    );
    for (u32 nodeIndex = 0; nodeIndex < localTransforms.size(); ++nodeIndex) {
        const i32 parentIndex = model.nodes[nodeIndex].parentIndex;
        if (parentIndex >= 0 &&
            static_cast<std::size_t>(parentIndex) < globalTransforms.size()) {
            globalTransforms[nodeIndex] =
                globalTransforms[static_cast<std::size_t>(parentIndex)] *
                localTransforms[nodeIndex];
        } else {
            globalTransforms[nodeIndex] = localTransforms[nodeIndex];
        }
    }

    return globalTransforms;
}

std::vector<glm::mat4> BuildBonePalette(
    const ImportedModel3D& model,
    const std::unordered_map<std::string, u32>& nodeLookup,
    const std::vector<glm::mat4>& globalTransforms
) {
    std::vector<glm::mat4> palette;
    palette.reserve(model.sourceBoneCount);
    for (const ImportedMesh3D& mesh : model.meshes) {
        for (const ImportedMesh3D::Bone& bone : mesh.bones) {
            const auto foundNode = nodeLookup.find(bone.name);
            if (foundNode == nodeLookup.end()) {
                continue;
            }

            const u32 nodeIndex = foundNode->second;
            if (nodeIndex >= globalTransforms.size()) {
                continue;
            }

            palette.push_back(globalTransforms[nodeIndex] * bone.offsetMatrix);
        }
    }

    return palette;
}

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

u32 CountSampledChannels(
    const ImportedAnimationClip3D& clip,
    const std::unordered_map<std::string, u32>& nodeLookup
) {
    u32 sampled = 0;
    for (const ImportedAnimationChannel3D& channel : clip.channels) {
        if (nodeLookup.find(channel.nodeName) == nodeLookup.end()) {
            continue;
        }
        if (channel.positions.empty() &&
            channel.rotations.empty() &&
            channel.scales.empty()) {
            continue;
        }

        ++sampled;
    }

    return sampled;
}

void PopulateModelAnimationPoseDiagnostics(ImportedModel3D& model) {
    model.diagnosticPoseSamples.clear();
    model.sourcePoseSampledClipCount = 0;
    model.sourcePoseSampledChannelCount = 0;
    model.sourcePoseSampledNodeCount = 0;
    model.sourcePoseAnimatedNodeCount = 0;
    model.sourcePoseBonePaletteEntryCount = 0;
    model.sourcePosePreviousBonePaletteEntryCount = 0;
    model.sourcePoseChangedBonePaletteEntryCount = 0;
    model.sourcePoseBonePaletteReady = 0;

    if (model.animations.empty() ||
        model.nodes.empty()) {
        return;
    }

    const std::unordered_map<std::string, u32> nodeLookup =
        BuildNodeNameLookup(model);
    for (u32 clipIndex = 0; clipIndex < model.animations.size(); ++clipIndex) {
        const ImportedAnimationClip3D& clip = model.animations[clipIndex];
        const u32 sampledChannelCount = CountSampledChannels(clip, nodeLookup);
        if (sampledChannelCount == 0) {
            continue;
        }

        const f64 previousTimeTicks = 0.0;
        const f64 currentTimeTicks = clip.durationTicks > 0.0
            ? clip.durationTicks
            : LastAnimationKeyTime(clip);
        if (currentTimeTicks <= previousTimeTicks) {
            continue;
        }

        ImportedPoseSample3D sample;
        sample.clipIndex = clipIndex;
        sample.previousTimeTicks = previousTimeTicks;
        sample.currentTimeTicks = currentTimeTicks;

        const std::vector<glm::mat4> previousLocalPose =
            SampleLocalPose(model, clip, nodeLookup, previousTimeTicks);
        const std::vector<glm::mat4> currentLocalPose =
            SampleLocalPose(model, clip, nodeLookup, currentTimeTicks);
        sample.previousNodeGlobalTransforms =
            BuildGlobalPose(model, previousLocalPose);
        sample.currentNodeGlobalTransforms =
            BuildGlobalPose(model, currentLocalPose);
        sample.previousBonePalette =
            BuildBonePalette(model, nodeLookup, sample.previousNodeGlobalTransforms);
        sample.currentBonePalette =
            BuildBonePalette(model, nodeLookup, sample.currentNodeGlobalTransforms);

        model.sourcePoseSampledClipCount = 1;
        model.sourcePoseSampledChannelCount = sampledChannelCount;
        model.sourcePoseSampledNodeCount =
            static_cast<u32>(sample.currentNodeGlobalTransforms.size());
        model.sourcePoseAnimatedNodeCount = CountChangedMatrices(
            sample.previousNodeGlobalTransforms,
            sample.currentNodeGlobalTransforms
        );
        model.sourcePoseBonePaletteEntryCount =
            static_cast<u32>(sample.currentBonePalette.size());
        model.sourcePosePreviousBonePaletteEntryCount =
            static_cast<u32>(sample.previousBonePalette.size());
        model.sourcePoseChangedBonePaletteEntryCount =
            CountChangedMatrices(sample.previousBonePalette, sample.currentBonePalette);
        model.sourcePoseBonePaletteReady =
            model.sourceBoneCount > 0 &&
            model.sourcePoseBonePaletteEntryCount == model.sourceBoneCount &&
            model.sourcePosePreviousBonePaletteEntryCount == model.sourceBoneCount
                ? 1u
                : 0u;

        model.diagnosticPoseSamples.push_back(std::move(sample));
        return;
    }
}

void PopulateMeshSkinning(ImportedMesh3D& mesh, const aiMesh* source) {
    SE_ASSERT(source != nullptr, "Assimp mesh must not be null");
    if (source->mNumBones == 0 || source->mNumVertices == 0) {
        return;
    }

    mesh.bones.reserve(source->mNumBones);
    mesh.vertexBoneInfluences.resize(source->mNumVertices);
    for (u32 boneIndex = 0; boneIndex < source->mNumBones; ++boneIndex) {
        const aiBone* sourceBone = source->mBones[boneIndex];
        if (sourceBone == nullptr) {
            continue;
        }

        const u32 importedBoneIndex = static_cast<u32>(mesh.bones.size());
        mesh.bones.push_back({
            ToString(sourceBone->mName),
            ToMat4(sourceBone->mOffsetMatrix)
        });

        for (u32 weightIndex = 0; weightIndex < sourceBone->mNumWeights; ++weightIndex) {
            const aiVertexWeight& weight = sourceBone->mWeights[weightIndex];
            if (weight.mVertexId >= mesh.vertexBoneInfluences.size() ||
                weight.mWeight <= 0.0f) {
                continue;
            }

            mesh.vertexBoneInfluences[weight.mVertexId].push_back({
                importedBoneIndex,
                weight.mWeight
            });
        }
    }
}

ImportedMesh3D ConvertMeshInstance(
    const aiMesh* source,
    const ImportedModel3D& model,
    const glm::mat4& transform,
    std::string nodeName
) {
    SE_ASSERT(source != nullptr, "Assimp mesh must not be null");
    ImportedMesh3D mesh;
    mesh.name = source->mName.length > 0
        ? ToString(source->mName)
        : std::move(nodeName);
    mesh.materialIndex = std::min<u32>(
        source->mMaterialIndex,
        static_cast<u32>(model.materials.empty() ? 0 : model.materials.size() - 1)
    );

    const ImportedMaterial3D& material = model.materials[mesh.materialIndex];
    const u32 uvChannel = PrimaryUvChannel(material);
    const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(transform));

    mesh.mesh.vertices.reserve(source->mNumVertices);
    for (u32 vertexIndex = 0; vertexIndex < source->mNumVertices; ++vertexIndex) {
        const glm::vec3 position =
            glm::vec3(transform * glm::vec4(ToVec3(source->mVertices[vertexIndex]), 1.0f));
        const glm::vec3 sourceNormal = source->HasNormals()
            ? ToVec3(source->mNormals[vertexIndex])
            : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 normal = SafeNormal(normalMatrix * sourceNormal);
        const glm::vec4 tangent = ReadTangent(source, normalMatrix, normal, vertexIndex);

        mesh.mesh.vertices.push_back({
            ToArray(position),
            ToArray(normal),
            ToArray(VertexColor(source, vertexIndex)),
            ToArray(ReadTexCoord(source, uvChannel, vertexIndex)),
            ToArray(tangent)
        });
        mesh.boundsMin = glm::min(mesh.boundsMin, position);
        mesh.boundsMax = glm::max(mesh.boundsMax, position);
    }

    mesh.mesh.indices.reserve(source->mNumFaces * 3);
    for (u32 faceIndex = 0; faceIndex < source->mNumFaces; ++faceIndex) {
        const aiFace& face = source->mFaces[faceIndex];
        if (face.mNumIndices != 3) {
            continue;
        }

        mesh.mesh.indices.push_back(face.mIndices[0]);
        mesh.mesh.indices.push_back(face.mIndices[1]);
        mesh.mesh.indices.push_back(face.mIndices[2]);
    }

    PopulateMeshSkinning(mesh, source);

    return mesh;
}

void AppendNodeMeshes(
    const aiScene* scene,
    const aiNode* node,
    const glm::mat4& parentTransform,
    ImportedModel3D& model
) {
    SE_ASSERT(scene != nullptr, "Assimp scene must not be null");
    SE_ASSERT(node != nullptr, "Assimp node must not be null");

    const glm::mat4 nodeTransform = parentTransform * ToMat4(node->mTransformation);
    const std::string nodeName = node->mName.C_Str();

    for (u32 slot = 0; slot < node->mNumMeshes; ++slot) {
        const u32 meshIndex = node->mMeshes[slot];
        if (meshIndex >= scene->mNumMeshes || scene->mMeshes[meshIndex] == nullptr) {
            model.messages.push_back({
                ImportSeverity::Warning,
                "Skipped invalid mesh reference under node '" + nodeName + "'"
            });
            continue;
        }

        ImportedMesh3D mesh = ConvertMeshInstance(
            scene->mMeshes[meshIndex],
            model,
            nodeTransform,
            nodeName
        );

        if (mesh.mesh.vertices.empty() || mesh.mesh.indices.empty()) {
            model.messages.push_back({
                ImportSeverity::Warning,
                "Skipped mesh without triangle data: " + mesh.name
            });
            continue;
        }

        model.boundsMin = glm::min(model.boundsMin, mesh.boundsMin);
        model.boundsMax = glm::max(model.boundsMax, mesh.boundsMax);
        model.meshes.push_back(std::move(mesh));
    }

    for (u32 childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        AppendNodeMeshes(scene, node->mChildren[childIndex], nodeTransform, model);
    }
}

void ConfigureImporter(Assimp::Importer& importer, const ModelImportOptions& options) {
    importer.SetPropertyInteger(
        AI_CONFIG_PP_SBP_REMOVE,
        aiPrimitiveType_POINT | aiPrimitiveType_LINE
    );

    if (options.fastImport) {
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
        importer.SetPropertyBool(
            AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS,
            options.readSkinningMetadata
        );
        importer.SetPropertyBool(
            AI_CONFIG_IMPORT_FBX_READ_WEIGHTS,
            options.readSkinningMetadata
        );
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION, true);
    }
}

unsigned int ImportFlags(const ModelImportOptions& options) {
    unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_SortByPType;

    if (!options.fastImport) {
        flags |= aiProcess_CalcTangentSpace;
    }

    if (options.optimizeMeshes && !options.fastImport) {
        flags |= aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality;
    }

    if (options.validateScene && !options.fastImport) {
        flags |= aiProcess_FindInvalidData | aiProcess_FixInfacingNormals | aiProcess_ValidateDataStructure;
    }

    if (options.rebuildNormals) {
        flags |= aiProcess_DropNormals | aiProcess_GenSmoothNormals;
    } else if (options.validateScene && !options.fastImport) {
        flags |= aiProcess_GenSmoothNormals;
    }

    return flags;
}

}

bool EmbeddedTextureData::Empty() const {
    return bytes.empty();
}

bool ImportedTexture3D::HasAnyTexture() const {
    return HasEmbeddedTexture() || !externalPath.empty();
}

bool ImportedTexture3D::HasEmbeddedTexture() const {
    return !embedded.Empty();
}

const ImportedTexture3D* ImportedMaterial3D::FindTexture(ImportedTextureKind kind) const {
    for (const ImportedTexture3D& texture : textures) {
        if (texture.kind == kind && texture.HasAnyTexture()) {
            return &texture;
        }
    }

    return nullptr;
}

const ImportedTexture3D* ImportedMaterial3D::FindPrimaryTexture() const {
    for (const ImportedTexture3D& texture : textures) {
        if ((texture.kind == ImportedTextureKind::BaseColor ||
             texture.kind == ImportedTextureKind::Diffuse) &&
            texture.HasAnyTexture()) {
            return &texture;
        }
    }

    for (const ImportedTexture3D& texture : textures) {
        if (texture.kind == ImportedTextureKind::Emissive && texture.HasAnyTexture()) {
            return &texture;
        }
    }

    for (const ImportedTexture3D& texture : textures) {
        if (texture.kind == ImportedTextureKind::Unknown && texture.HasAnyTexture()) {
            return &texture;
        }
    }

    return nullptr;
}

bool ImportedModel3D::Empty() const {
    return meshes.empty();
}

std::size_t ImportedModel3D::VertexCount() const {
    std::size_t count = 0;
    for (const ImportedMesh3D& mesh : meshes) {
        count += mesh.mesh.vertices.size();
    }

    return count;
}

std::size_t ImportedModel3D::TriangleCount() const {
    std::size_t count = 0;
    for (const ImportedMesh3D& mesh : meshes) {
        count += mesh.mesh.indices.size() / 3;
    }

    return count;
}

ImportedModel3D ModelImporter::LoadModel3D(
    const std::filesystem::path& path,
    const ModelImportOptions& options
) {
    Assimp::Importer importer;
    ConfigureImporter(importer, options);

    const aiScene* scene = importer.ReadFile(PathToUtf8(path), ImportFlags(options));
    if (scene == nullptr || scene->mRootNode == nullptr || scene->mNumMeshes == 0) {
        throw std::runtime_error(
            "Assimp failed to load model '" + PathToUtf8(path) + "': " + importer.GetErrorString()
        );
    }

    ImportedModel3D model;
    model.sourcePath = path;
    model.boundsMin = glm::vec3(std::numeric_limits<f32>::max());
    model.boundsMax = glm::vec3(std::numeric_limits<f32>::lowest());
    PopulateModelSourceDiagnostics(model, scene);
    PopulateModelAnimations(model, scene);
    PopulateModelNodeHierarchy(model, scene);

    model.materials.reserve(scene->mNumMaterials > 0 ? scene->mNumMaterials : 1);
    for (u32 materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        const aiMaterial* source = scene->mMaterials[materialIndex];
        if (source == nullptr) {
            continue;
        }

        ImportedMaterial3D material;
        PopulateMaterial(scene, source, path, material);
        model.materials.push_back(std::move(material));
    }

    if (model.materials.empty()) {
        model.materials.push_back(ImportedMaterial3D{});
    }

    AppendNodeMeshes(scene, scene->mRootNode, glm::mat4(1.0f), model);
    if (model.meshes.empty()) {
        throw std::runtime_error("No triangle mesh data found in model: " + PathToUtf8(path));
    }
    PopulateModelAnimationPoseDiagnostics(model);

    if (model.skinnedAnimationUnsupported) {
        model.messages.push_back({
            ImportSeverity::Warning,
            "Source model exposes " + std::to_string(model.sourceAnimationCount) +
                " animation(s), " +
                std::to_string(model.sourceMeshWithBonesCount) +
                " mesh(es) with bones, and " +
                std::to_string(model.sourceBoneCount) +
                " bone reference(s) across " +
                std::to_string(model.sourceBoneInfluenceCount) +
                " vertex influence(s) and " +
                std::to_string(model.sourceAnimationChannelCount) +
                " animation channel(s); runtime import currently treats it as rigid mesh data."
        });
    }

    model.messages.push_back({
        ImportSeverity::Info,
        "Loaded " + std::to_string(model.meshes.size()) +
            " mesh(es), " + std::to_string(model.materials.size()) +
            " material(s), " + std::to_string(model.TriangleCount()) +
            " triangle(s)."
    });

    return model;
}

MeshData3D ModelImporter::LoadMeshData3D(const std::filesystem::path& path) {
    const ImportedModel3D model = LoadModel3D(path);

    MeshData3D combined;
    for (const ImportedMesh3D& mesh : model.meshes) {
        const u32 vertexOffset = static_cast<u32>(combined.vertices.size());
        combined.vertices.insert(
            combined.vertices.end(),
            mesh.mesh.vertices.begin(),
            mesh.mesh.vertices.end()
        );

        combined.indices.reserve(combined.indices.size() + mesh.mesh.indices.size());
        for (u32 index : mesh.mesh.indices) {
            combined.indices.push_back(vertexOffset + index);
        }
    }

    return combined;
}

}
