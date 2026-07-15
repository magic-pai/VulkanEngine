#pragma once

#include "assets/model_importer.h"
#include "renderer/vulkan/buffer.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/mesh_lod.h"
#include "scene/scene_3d.h"

#include <functional>
#include <unordered_map>

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanMaterialLibrary;
class VulkanPhysicalDevice;
class VulkanRenderResources2D;
class RuntimeBonePaletteDescriptorSet;

struct RuntimeModelLoadResult {
    bool loaded = false;
    std::string message;
    bool cacheHit = false;
    u32 meshCount = 0;
    u32 materialCount = 0;
    u32 sourceNodeCount = 0;
    u32 sourceBoneNodeCount = 0;
    u32 sourceAnimationChannelBoundCount = 0;
    u32 sourceAnimationChannelUnboundCount = 0;
    u32 sourceBoneNameMatchedNodeCount = 0;
    u32 sourceBoneNameUnmatchedCount = 0;
    u32 sourceAnimationCount = 0;
    u32 sourceAnimationChannelCount = 0;
    u32 sourceAnimationPositionKeyCount = 0;
    u32 sourceAnimationRotationKeyCount = 0;
    u32 sourceAnimationScaleKeyCount = 0;
    u32 sourceAnimationKeyCount = 0;
    u32 sourceMaxAnimationKeysPerChannel = 0;
    u32 sourcePoseSampledClipCount = 0;
    u32 sourcePoseSampledChannelCount = 0;
    u32 sourcePoseSampledNodeCount = 0;
    u32 sourcePoseAnimatedNodeCount = 0;
    u32 sourcePoseBonePaletteEntryCount = 0;
    u32 sourcePosePreviousBonePaletteEntryCount = 0;
    u32 sourcePoseChangedBonePaletteEntryCount = 0;
    u32 sourcePoseBonePaletteReady = 0;
    u32 runtimePoseCarrierBonePaletteEntryCount = 0;
    u32 runtimePoseCarrierPreviousBonePaletteEntryCount = 0;
    u32 runtimePoseCarrierChangedBonePaletteEntryCount = 0;
    u32 runtimePoseCarrierReady = 0;
    u32 rendererPosePaletteRegistered = 0;
    u32 rendererPosePaletteBonePaletteEntryCount = 0;
    u32 rendererPosePalettePreviousBonePaletteEntryCount = 0;
    u32 rendererPosePaletteChangedBonePaletteEntryCount = 0;
    u32 rendererPosePaletteReady = 0;
    u32 gpuPosePaletteBufferAllocated = 0;
    u32 gpuPosePaletteBufferUploaded = 0;
    u32 gpuPosePaletteDescriptorInfoReady = 0;
    u32 gpuPosePaletteDescriptorSetAllocated = 0;
    u32 gpuPosePaletteDescriptorSetWritten = 0;
    u32 gpuPosePaletteDescriptorSetReady = 0;
    u32 gpuPosePaletteDescriptorBinding = 0;
    u32 gpuPosePaletteDescriptorRangeBytes = 0;
    u32 gpuPosePaletteBufferBytes = 0;
    u32 gpuPosePaletteCurrentEntryCount = 0;
    u32 gpuPosePalettePreviousEntryCount = 0;
    u32 sourceMeshWithBonesCount = 0;
    u32 sourceBoneCount = 0;
    u32 sourceSkinnedVertexCount = 0;
    u32 sourceBoneInfluenceCount = 0;
    u32 sourceMaxBoneInfluencesPerVertex = 0;
    u32 sourceSkinnedVertexAttributeCount = 0;
    u32 sourceBoneAttributeInfluenceCount = 0;
    u32 sourceMaxBoneAttributeInfluencesPerVertex = 0;
    u32 sourceBoneInfluenceOverflowCount = 0;
    u32 sourceSkinnedVertexAttributeReady = 0;
    u32 runtimeSkinnedAnimationSpaceReady = 0;
    u32 runtimeSkinnedAnimationSpaceBlockerMask = 0;
    u32 runtimeSkinnedAnimationRenderableBound = 0;
    u32 skinnedAnimationUnsupported = 0;
};

struct RuntimeModelAnimationPlaybackDiagnostics {
    u32 candidateModelCount = 0;
    u32 readyModelCount = 0;
    u32 frameCount = 0;
    u32 loopWrapCount = 0;
    u32 previousPoseCollapsedCount = 0;
    f64 previousTimeTicks = 0.0;
    f64 currentTimeTicks = 0.0;
    f64 previousAbsoluteSeconds = -1.0;
    f64 currentAbsoluteSeconds = -1.0;
    u32 changedBonePaletteEntryCount = 0;
    u32 rendererPaletteReady = 0;
    u32 gpuUploadReady = 0;
    u32 ready = 0;
};

class RuntimeModelLoader {
public:
    RuntimeModelLoader(
        VulkanDevice& device,
        VulkanPhysicalDevice& physicalDevice,
        VulkanCommandPool& commandPool,
        VulkanMaterialLibrary& materialLibrary,
        VulkanRenderResources2D& renderResources,
        Scene3D& scene,
        std::string fallbackTexturePath
    );

    RuntimeModelLoadResult LoadIntoScene(
        const std::filesystem::path& modelPath,
        glm::vec3 position,
        glm::vec3 rotationDegrees = glm::vec3(0.0f),
        // Use <= 0 to preserve imported scene scale for bridge/exported content.
        f32 targetMaxExtent = 2.1f,
        glm::vec3 scale = glm::vec3(1.0f),
        bool bindBonePalettePreview = false
    );
    void UpdateAnimationPlayback(f32 deltaSeconds);
    void UpdateAnimationPlaybackAtTime(f32 elapsedSeconds);
    RuntimeModelAnimationPlaybackDiagnostics AnimationPlaybackDiagnostics() const;
    void ForEachMaterial(const std::function<void(VulkanMaterial&)>& visitor) const;

private:
    struct LoadedRuntimeModel {
        ~LoadedRuntimeModel();

        std::vector<std::unique_ptr<VulkanMesh>> meshes;
        std::vector<VulkanMaterial*> materials;
        std::vector<std::string> meshIds;
        std::vector<std::string> materialIds;
        std::vector<MeshLodChain> lodChains;
        u32 sourceNodeCount = 0;
        u32 sourceBoneNodeCount = 0;
        u32 sourceAnimationChannelBoundCount = 0;
        u32 sourceAnimationChannelUnboundCount = 0;
        u32 sourceBoneNameMatchedNodeCount = 0;
        u32 sourceBoneNameUnmatchedCount = 0;
        u32 sourceAnimationCount = 0;
        u32 sourceAnimationChannelCount = 0;
        u32 sourceAnimationPositionKeyCount = 0;
        u32 sourceAnimationRotationKeyCount = 0;
        u32 sourceAnimationScaleKeyCount = 0;
        u32 sourceAnimationKeyCount = 0;
        u32 sourceMaxAnimationKeysPerChannel = 0;
        u32 sourcePoseSampledClipCount = 0;
        u32 sourcePoseSampledChannelCount = 0;
        u32 sourcePoseSampledNodeCount = 0;
        u32 sourcePoseAnimatedNodeCount = 0;
        u32 sourcePoseBonePaletteEntryCount = 0;
        u32 sourcePosePreviousBonePaletteEntryCount = 0;
        u32 sourcePoseChangedBonePaletteEntryCount = 0;
        u32 sourcePoseBonePaletteReady = 0;
        std::vector<glm::mat4> runtimePreviousBonePalette;
        std::vector<glm::mat4> runtimeCurrentBonePalette;
        u32 runtimePoseCarrierChangedBonePaletteEntryCount = 0;
        u32 runtimePoseCarrierReady = 0;
        std::string bonePaletteResourceId;
        std::vector<std::string> bonePaletteResourceIds;
        std::unique_ptr<VulkanBuffer> gpuBonePaletteBuffer;
        std::unique_ptr<RuntimeBonePaletteDescriptorSet> gpuBonePaletteDescriptorSet;
        u32 gpuPosePaletteBufferUploaded = 0;
        u32 gpuPosePaletteDescriptorInfoReady = 0;
        u32 gpuPosePaletteDescriptorSetAllocated = 0;
        u32 gpuPosePaletteDescriptorSetWritten = 0;
        u32 gpuPosePaletteDescriptorSetReady = 0;
        u32 gpuPosePaletteDescriptorBinding = 0;
        u32 gpuPosePaletteDescriptorRangeBytes = 0;
        u32 gpuPosePaletteCurrentEntryCount = 0;
        u32 gpuPosePalettePreviousEntryCount = 0;
        u32 sourceMeshWithBonesCount = 0;
        u32 sourceBoneCount = 0;
        u32 sourceSkinnedVertexCount = 0;
        u32 sourceBoneInfluenceCount = 0;
        u32 sourceMaxBoneInfluencesPerVertex = 0;
        u32 sourceSkinnedVertexAttributeCount = 0;
        u32 sourceBoneAttributeInfluenceCount = 0;
        u32 sourceMaxBoneAttributeInfluencesPerVertex = 0;
        u32 sourceBoneInfluenceOverflowCount = 0;
        u32 sourceSkinnedVertexAttributeReady = 0;
        u32 runtimeSkinnedAnimationSpaceReady = 0;
        u32 runtimeSkinnedAnimationSpaceBlockerMask = 0;
        u32 runtimeSkinnedAnimationRenderableBound = 0;
        u32 skinnedAnimationUnsupported = 0;
        ImportedModel3D animationSource;
        u32 animationPlaybackCandidate = 0;
        u32 animationPlaybackClipIndex = 0;
        f64 animationPlaybackDurationTicks = 0.0;
        f64 animationPlaybackTicksPerSecond = 0.0;
        f64 animationPlaybackPreviousTimeTicks = 0.0;
        f64 animationPlaybackCurrentTimeTicks = 0.0;
        f64 animationPlaybackPreviousAbsoluteSeconds = -1.0;
        f64 animationPlaybackCurrentAbsoluteSeconds = -1.0;
        u32 runtimeAnimationPlaybackReady = 0;
        u32 runtimeAnimationPlaybackFrameCount = 0;
        u32 runtimeAnimationPlaybackLoopWrapCount = 0;
        u32 runtimeAnimationPlaybackPreviousPoseCollapsedCount = 0;
        u32 runtimeAnimationPlaybackChangedBonePaletteEntryCount = 0;
        u32 runtimeAnimationPlaybackRendererPaletteReady = 0;
        u32 runtimeAnimationPlaybackGpuUploadReady = 0;
    };

    void UpdateAnimationPlaybackSample(
        LoadedRuntimeModel& model,
        f64 previousTimeTicks,
        f64 currentTimeTicks,
        bool wrapped,
        bool countContinuityDiagnostics
    );

    VulkanDevice& m_Device;
    VulkanPhysicalDevice& m_PhysicalDevice;
    VulkanCommandPool& m_CommandPool;
    VulkanMaterialLibrary& m_MaterialLibrary;
    VulkanRenderResources2D& m_RenderResources;
    Scene3D& m_Scene;
    std::string m_FallbackTexturePath;
    std::vector<std::unique_ptr<LoadedRuntimeModel>> m_LoadedModels;
    std::unordered_map<std::string, std::size_t> m_ModelCache;
    u32 m_NextModelId = 0;
};

}
