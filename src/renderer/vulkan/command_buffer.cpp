#include "renderer/vulkan/command_buffer.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/compute_pipeline.h"
#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/descriptor_set_layout.h"
#include "renderer/vulkan/descriptor_sets.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/fidelityfx_sssr_adapter.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/frame_graph.h"
#include "renderer/vulkan/framebuffer.h"
#include "renderer/vulkan/frame_materials.h"
#include "renderer/vulkan/gpu_timer.h"
#include "renderer/vulkan/graphics_pipeline.h"
#include "renderer/vulkan/hybrid_reflection_acceleration_structures.h"
#include "renderer/vulkan/imgui_layer.h"
#include "renderer/vulkan/instance_buffer.h"
#include "renderer/vulkan/material.h"
#include "renderer/vulkan/mesh.h"
#include "renderer/vulkan/render_pass.h"
#include "renderer/vulkan/render_targets.h"
#include "renderer/vulkan/renderer.h"
#include "renderer/vulkan/shadow_framebuffer.h"
#include "renderer/vulkan/shadow_render_pass.h"
#include "renderer/vulkan/swapchain.h"
#include "renderer/vulkan/uniform_buffer.h"
#include "renderer/vulkan/renderer_stats.h"
#include "renderer/vulkan/vulkan_common.h"
#include "renderer/render_queue.h"

#include <cstddef>

namespace se {

namespace {

struct DrawStateCache {
    const VulkanMaterial* material = nullptr;
    const VulkanMesh* mesh = nullptr;
    VkDescriptorSet bonePaletteDescriptorSet = VK_NULL_HANDLE;
};

bool IsDoubleSidedCommand(const RenderCommand& renderCommand) {
    return renderCommand.material != nullptr &&
        renderCommand.material->Properties().doubleSided;
}

const VulkanGraphicsPipeline& PipelineForCommand(
    const VulkanGraphicsPipeline& defaultPipeline,
    const VulkanGraphicsPipeline* doubleSidedPipeline,
    const RenderCommand& renderCommand
) {
    if (doubleSidedPipeline != nullptr && IsDoubleSidedCommand(renderCommand)) {
        return *doubleSidedPipeline;
    }

    return defaultPipeline;
}

constexpr std::size_t kObjectPushConstantBytes =
    offsetof(ObjectPushConstants, materialBaseColorFactor);
constexpr std::size_t kMaterialPushConstantOffset =
    offsetof(ObjectPushConstants, materialBaseColorFactor);
constexpr std::size_t kMaterialPushConstantBytes =
    sizeof(ObjectPushConstants) - kMaterialPushConstantOffset;
struct ShadowDepthPushConstants {
    alignas(16) glm::mat4 model{ 1.0f };
    alignas(16) glm::mat4 lightViewProjection{ 1.0f };
    alignas(16) glm::vec4 skinningControls{ 0.0f };
};
constexpr std::size_t kShadowPushConstantBytes = sizeof(ShadowDepthPushConstants);

static_assert(
    kObjectPushConstantBytes == sizeof(glm::mat4) * 2 + sizeof(glm::vec4),
    "Object push constant segment must contain model, previous model, and tint"
);
static_assert(
    sizeof(RenderMaterialPushConstants) == kMaterialPushConstantBytes,
    "Render material push constants must match the ObjectPushConstants material segment"
);
static_assert(
    sizeof(ShadowDepthPushConstants) <= sizeof(ObjectPushConstants),
    "Shadow push constants must fit inside the graphics pipeline push-constant range"
);

bool DlssUnitMotionVectorScaleRequested() {
    const std::string mode = LowerAscii(
        ReadVulkanEnvironmentString("SE_DLSS_MOTION_VECTOR_SCALE_MODE")
    );
    return mode == "unit" ||
        mode == "normalized" ||
        VulkanEnvironmentFlagEnabled("SE_DLSS_UNIT_MOTION_VECTOR_SCALE") ||
        VulkanEnvironmentFlagEnabled("SE_DLSS_MV_SCALE_UNIT");
}

glm::vec2 DlssMotionVectorDirection() {
    std::string mode = LowerAscii(
        ReadVulkanEnvironmentString("SE_DLSS_MOTION_VECTOR_DIRECTION")
    );
    if (mode.empty()) {
        mode = LowerAscii(ReadVulkanEnvironmentString("SE_DLSS_MV_DIRECTION"));
    }
    if (mode.empty()) {
        mode = LowerAscii(ReadVulkanEnvironmentString("SE_DLSS_MV_SIGN"));
    }

    if (mode == "engine" ||
        mode == "native" ||
        mode == "normal" ||
        mode == "raw" ||
        mode == "motion" ||
        mode == "current-previous" ||
        mode == "current_to_previous_raw") {
        return glm::vec2(1.0f);
    }
    if (mode == "invert-x" || mode == "previous-current-x") {
        return glm::vec2(-1.0f, 1.0f);
    }
    if (mode == "invert-y" || mode == "previous-current-y") {
        return glm::vec2(1.0f, -1.0f);
    }

    return glm::vec2(-1.0f);
}

glm::vec2 DlssMotionVectorScale(const VkExtent2D& renderExtent) {
    if (DlssUnitMotionVectorScaleRequested()) {
        return DlssMotionVectorDirection();
    }
    const glm::vec2 direction = DlssMotionVectorDirection();
    return glm::vec2(
        direction.x * static_cast<f32>(renderExtent.width),
        direction.y * static_cast<f32>(renderExtent.height)
    );
}

glm::vec2 DlssJitterOffsetPixels(const glm::vec2& jitterPixels) {
    std::string mode = LowerAscii(
        ReadVulkanEnvironmentString("SE_DLSS_JITTER_OFFSET_MODE")
    );
    if (mode.empty()) {
        mode = LowerAscii(ReadVulkanEnvironmentString("SE_DLSS_JITTER_MODE"));
    }

    if (VulkanEnvironmentFlagEnabled("SE_DLSS_ZERO_JITTER_OFFSET") ||
        mode == "0" ||
        mode == "zero" ||
        mode == "none" ||
        mode == "off") {
        return glm::vec2(0.0f);
    }
    if (VulkanEnvironmentFlagEnabled("SE_DLSS_INVERT_JITTER_OFFSET") ||
        mode == "invert" ||
        mode == "inverted" ||
        mode == "negate" ||
        mode == "negative") {
        return -jitterPixels;
    }
    if (mode == "normal" ||
        mode == "raw" ||
        mode == "passthrough" ||
        mode == "pass-through") {
        return jitterPixels;
    }
    if (mode == "invert-x" || mode == "negative-x") {
        return glm::vec2(-jitterPixels.x, jitterPixels.y);
    }
    if (mode == "invert-y" || mode == "negative-y") {
        return glm::vec2(jitterPixels.x, -jitterPixels.y);
    }

    return -jitterPixels;
}

bool DlssBypassPostSourceRequested() {
    return VulkanEnvironmentFlagEnabled("SE_DLSS_BYPASS_POST_SOURCE") ||
        VulkanEnvironmentFlagEnabled("SE_TEMPORAL_UPSCALE_BYPASS_POST_SOURCE");
}

bool DlssClearOutputBeforeEvaluateRequested() {
    return VulkanEnvironmentFlagEnabled("SE_DLSS_CLEAR_OUTPUT_BEFORE_EVALUATE");
}

ObjectPushConstants DeferredLightingPushConstants(std::span<const RenderCommand> renderCommands) {
    ObjectPushConstants lightingConstants{};
    lightingConstants.cameraDirection = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

    for (const RenderCommand& renderCommand : renderCommands) {
        if (renderCommand.mesh == nullptr) {
            continue;
        }

        lightingConstants.cameraPosition = renderCommand.materialPushConstants.cameraPosition;
        lightingConstants.cameraDirection = renderCommand.materialPushConstants.cameraDirection;
        lightingConstants.cameraDirection.w = 1.0f;
        break;
    }

    return lightingConstants;
}

void PushConstants(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    VkShaderStageFlags shaderStages,
    std::size_t offset,
    std::size_t size,
    const void* data,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount
) {
    vkCmdPushConstants(
        commandBuffer,
        graphicsPipeline.Layout(),
        shaderStages,
        static_cast<u32>(offset),
        static_cast<u32>(size),
        data
    );
    ++pushConstantUpdateCount;
    pushConstantByteCount += static_cast<u64>(size);
}

void PushMaterialConstants(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const RenderCommand& renderCommand,
    const VkExtent2D& extent,
    f32 materialId,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount,
    f32 hdrOutputFlag = 0.0f
) {
    RenderMaterialPushConstants materialData = renderCommand.materialPushConstants;
    materialData.materialControls.w = materialId;
    materialData.viewport = glm::vec4(
        static_cast<f32>(extent.width),
        static_cast<f32>(extent.height),
        hdrOutputFlag,
        static_cast<f32>(renderCommand.bonePalettePreviousEntryCount)
    );

    PushConstants(
        commandBuffer,
        graphicsPipeline,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        kMaterialPushConstantOffset,
        kMaterialPushConstantBytes,
        &materialData,
        pushConstantUpdateCount,
        pushConstantByteCount
    );
}

void PushObjectConstants(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const RenderCommand& renderCommand,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount
) {
    ObjectPushConstants objectData{};
    objectData.model = renderCommand.model;
    objectData.previousModel = renderCommand.previousModel;
    objectData.tint = renderCommand.tint;

    PushConstants(
        commandBuffer,
        graphicsPipeline,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        kObjectPushConstantBytes,
        &objectData,
        pushConstantUpdateCount,
        pushConstantByteCount
    );
}

void PushShadowObjectConstants(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const RenderCommand& renderCommand,
    const glm::mat4& lightViewProjection,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount
) {
    ShadowDepthPushConstants objectData{};
    objectData.model = renderCommand.model;
    objectData.lightViewProjection = lightViewProjection;
    const bool realBonePaletteDescriptorReady =
        renderCommand.bonePaletteDescriptorSet != VK_NULL_HANDLE &&
        renderCommand.bonePaletteDescriptorSetReady != 0u;
    objectData.skinningControls.x = realBonePaletteDescriptorReady
        ? static_cast<f32>(renderCommand.bonePalettePreviousEntryCount)
        : 0.0f;

    PushConstants(
        commandBuffer,
        graphicsPipeline,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        kShadowPushConstantBytes,
        &objectData,
        pushConstantUpdateCount,
        pushConstantByteCount
    );
}

bool BindMaterialIfNeeded(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const VulkanMaterialDescriptorSets& materialDescriptorSets,
    const RenderCommand& renderCommand,
    std::size_t imageIndex,
    DrawStateCache& state
) {
    if (state.material == renderCommand.material) {
        return false;
    }

    const VkDescriptorSet materialDescriptorSet =
        materialDescriptorSets.Handle(*renderCommand.material, imageIndex);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        graphicsPipeline.Layout(),
        1,
        1,
        &materialDescriptorSet,
        0,
        nullptr
    );
    state.material = renderCommand.material;

    return true;
}

bool BindMeshIfNeeded(
    VkCommandBuffer commandBuffer,
    const RenderCommand& renderCommand,
    DrawStateCache& state
) {
    if (state.mesh == renderCommand.mesh) {
        return false;
    }

    renderCommand.mesh->Bind(commandBuffer);
    state.mesh = renderCommand.mesh;

    return true;
}

bool BindBonePaletteIfNeeded(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const RenderCommand& renderCommand,
    DrawStateCache& state
) {
    if (renderCommand.bonePaletteDescriptorSet == VK_NULL_HANDLE ||
        renderCommand.bonePaletteDescriptorSetReady == 0u) {
        return false;
    }
    SE_ASSERT(
        renderCommand.bonePaletteDescriptorSetIndex == kBonePaletteDescriptorSetIndex,
        "Bone palette descriptor set index must match the graphics pipeline layout"
    );

    if (state.bonePaletteDescriptorSet == renderCommand.bonePaletteDescriptorSet) {
        return false;
    }

    const VkDescriptorSet descriptorSet = renderCommand.bonePaletteDescriptorSet;
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        graphicsPipeline.Layout(),
        renderCommand.bonePaletteDescriptorSetIndex,
        1,
        &descriptorSet,
        0,
        nullptr
    );
    state.bonePaletteDescriptorSet = renderCommand.bonePaletteDescriptorSet;

    return true;
}

bool BindBonePaletteFallbackIfNeeded(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    VkDescriptorSet fallbackDescriptorSet,
    u32 fallbackDescriptorReady,
    DrawStateCache& state
) {
    if (fallbackDescriptorSet == VK_NULL_HANDLE || fallbackDescriptorReady == 0u) {
        return false;
    }

    if (state.bonePaletteDescriptorSet == fallbackDescriptorSet) {
        return false;
    }

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        graphicsPipeline.Layout(),
        kBonePaletteDescriptorSetIndex,
        1,
        &fallbackDescriptorSet,
        0,
        nullptr
    );
    state.bonePaletteDescriptorSet = fallbackDescriptorSet;

    return true;
}

void DrawRenderCommand(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const VulkanMaterialDescriptorSets& materialDescriptorSets,
    const RenderCommand& renderCommand,
    const VkExtent2D& extent,
    std::size_t imageIndex,
    f32 materialId,
    DrawStateCache& state,
    u32& materialBindCount,
    u32& meshBindCount,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount,
    f32 hdrOutputFlag = 0.0f,
    u32* bonePaletteDescriptorBindCount = nullptr
) {
    SE_ASSERT(renderCommand.mesh != nullptr, "RenderCommand must reference a mesh");
    SE_ASSERT(renderCommand.material != nullptr, "RenderCommand must reference a material");

    if (BindMaterialIfNeeded(
        commandBuffer,
        graphicsPipeline,
        materialDescriptorSets,
        renderCommand,
        imageIndex,
        state
    )) {
        ++materialBindCount;
        PushMaterialConstants(
            commandBuffer,
            graphicsPipeline,
            renderCommand,
            extent,
            materialId,
            pushConstantUpdateCount,
            pushConstantByteCount,
            hdrOutputFlag
        );
    }

    PushObjectConstants(
        commandBuffer,
        graphicsPipeline,
        renderCommand,
        pushConstantUpdateCount,
        pushConstantByteCount
    );

    if (BindMeshIfNeeded(commandBuffer, renderCommand, state)) {
        ++meshBindCount;
    }
    if (BindBonePaletteIfNeeded(
            commandBuffer,
            graphicsPipeline,
            renderCommand,
            state
        ) &&
        bonePaletteDescriptorBindCount != nullptr) {
        ++(*bonePaletteDescriptorBindCount);
    }
    vkCmdDrawIndexed(commandBuffer, renderCommand.mesh->IndexCount(), 1, 0, 0, 0);
}

void DrawForwardCommands(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedGraphicsPipeline,
    const VulkanDescriptorSets& descriptorSets,
    const VulkanMaterialDescriptorSets& materialDescriptorSets,
    const FrameMaterialSet* frameMaterials,
    std::span<const RenderCommand> renderCommands,
    const VkExtent2D& extent,
    std::size_t imageIndex,
    DrawStateCache& state,
    u32& materialBindCount,
    u32& meshBindCount,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount,
    f32 hdrOutputFlag = 0.0f
) {
    if (renderCommands.empty()) {
        return;
    }

    const VulkanGraphicsPipeline* boundPipeline = nullptr;
    auto bindPipeline = [&](const VulkanGraphicsPipeline& pipeline) {
        if (boundPipeline == &pipeline) {
            return;
        }

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.Handle()
        );

        const VkDescriptorSet descriptorSet = descriptorSets.Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.Layout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr
        );
        state = DrawStateCache{};
        boundPipeline = &pipeline;
    };

    state = DrawStateCache{};
    for (const RenderCommand& renderCommand : renderCommands) {
        const VulkanGraphicsPipeline& activePipeline = PipelineForCommand(
            graphicsPipeline,
            doubleSidedGraphicsPipeline,
            renderCommand
        );
        bindPipeline(activePipeline);
        const u32 materialId = frameMaterials != nullptr
            ? frameMaterials->IdFor(renderCommand.material)
            : 0;
        DrawRenderCommand(
            commandBuffer,
            activePipeline,
            materialDescriptorSets,
            renderCommand,
            extent,
            imageIndex,
            static_cast<f32>(materialId),
            state,
            materialBindCount,
            meshBindCount,
            pushConstantUpdateCount,
            pushConstantByteCount,
            hdrOutputFlag
        );
    }
}

void DrawShadowCommand(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const RenderCommand& renderCommand,
    const glm::mat4& lightViewProjection,
    VkDescriptorSet bonePaletteFallbackDescriptorSet,
    u32 bonePaletteFallbackDescriptorReady,
    DrawStateCache& state,
    u32& meshBindCount,
    u32& bonePaletteDescriptorBindCount,
    u32& bonePaletteFallbackDescriptorBindCount,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount
) {
    SE_ASSERT(renderCommand.mesh != nullptr, "Shadow RenderCommand must reference a mesh");

    PushShadowObjectConstants(
        commandBuffer,
        graphicsPipeline,
        renderCommand,
        lightViewProjection,
        pushConstantUpdateCount,
        pushConstantByteCount
    );

    if (BindMeshIfNeeded(commandBuffer, renderCommand, state)) {
        ++meshBindCount;
    }
    const bool realBonePaletteDescriptorReady =
        renderCommand.bonePaletteDescriptorSet != VK_NULL_HANDLE &&
        renderCommand.bonePaletteDescriptorSetReady != 0u;
    if (!realBonePaletteDescriptorReady &&
        BindBonePaletteFallbackIfNeeded(
            commandBuffer,
            graphicsPipeline,
            bonePaletteFallbackDescriptorSet,
            bonePaletteFallbackDescriptorReady,
            state
        )) {
        ++bonePaletteFallbackDescriptorBindCount;
    }
    if (BindBonePaletteIfNeeded(
            commandBuffer,
            graphicsPipeline,
            renderCommand,
            state
        )) {
        ++bonePaletteDescriptorBindCount;
    }
    vkCmdDrawIndexed(commandBuffer, renderCommand.mesh->IndexCount(), 1, 0, 0, 0);
}

void SetViewportAndScissor(
    VkCommandBuffer commandBuffer,
    VkOffset2D offset,
    VkExtent2D extent
) {
    VkViewport viewport{};
    viewport.x = static_cast<f32>(offset.x);
    viewport.y = static_cast<f32>(offset.y);
    viewport.width = static_cast<f32>(extent.width);
    viewport.height = static_cast<f32>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = offset;
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void SetShadowViewportAndScissor(
    VkCommandBuffer commandBuffer,
    VkOffset2D offset,
    VkExtent2D extent
) {
    SetViewportAndScissor(commandBuffer, offset, extent);
}

void DrawShadowCommands(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& shadowGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedShadowGraphicsPipeline,
    const VulkanDescriptorSets& shadowDescriptorSets,
    std::span<const RenderCommand> shadowRenderCommands,
    std::size_t imageIndex,
    const glm::mat4& lightViewProjection,
    const ShadowDepthBiasControls& shadowDepthBias,
    VkDescriptorSet bonePaletteFallbackDescriptorSet,
    u32 bonePaletteFallbackDescriptorReady,
    VkOffset2D viewportOffset,
    VkExtent2D viewportExtent,
    u32& meshBindCount,
    u32& bonePaletteDescriptorBindCount,
    u32& bonePaletteFallbackDescriptorBindCount,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount
) {
    SetShadowViewportAndScissor(commandBuffer, viewportOffset, viewportExtent);

    DrawStateCache shadowState{};
    const VulkanGraphicsPipeline* boundShadowPipeline = nullptr;
    auto bindShadowPipeline = [&](const VulkanGraphicsPipeline& pipeline) {
        if (boundShadowPipeline == &pipeline) {
            return;
        }

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.Handle()
        );
        vkCmdSetDepthBias(
            commandBuffer,
            shadowDepthBias.enabled ? shadowDepthBias.constantFactor : 0.0f,
            shadowDepthBias.enabled ? shadowDepthBias.clamp : 0.0f,
            shadowDepthBias.enabled ? shadowDepthBias.slopeFactor : 0.0f
        );

        const VkDescriptorSet descriptorSet = shadowDescriptorSets.Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.Layout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr
        );
        shadowState = DrawStateCache{};
        boundShadowPipeline = &pipeline;
    };

    for (const RenderCommand& renderCommand : shadowRenderCommands) {
        const VulkanGraphicsPipeline& activeShadowPipeline =
            PipelineForCommand(
                shadowGraphicsPipeline,
                doubleSidedShadowGraphicsPipeline,
                renderCommand
            );
        bindShadowPipeline(activeShadowPipeline);
        DrawShadowCommand(
            commandBuffer,
            activeShadowPipeline,
            renderCommand,
            lightViewProjection,
            bonePaletteFallbackDescriptorSet,
            bonePaletteFallbackDescriptorReady,
            shadowState,
            meshBindCount,
            bonePaletteDescriptorBindCount,
            bonePaletteFallbackDescriptorBindCount,
            pushConstantUpdateCount,
            pushConstantByteCount
        );
    }
}

u32 DrawForwardResidualCommands(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline* residualPipeline,
    const VulkanGraphicsPipeline* doubleSidedResidualPipeline,
    const VulkanDescriptorSets& descriptorSets,
    const VulkanMaterialDescriptorSets& materialDescriptorSets,
    const FrameMaterialSet* frameMaterials,
    std::span<const RenderCommand> renderCommands,
    const VkExtent2D& extent,
    std::size_t imageIndex,
    u32& materialBindCount,
    u32& meshBindCount,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount,
    f32 hdrOutputFlag = 0.0f
) {
    if (residualPipeline == nullptr || renderCommands.empty()) {
        return 0;
    }

    DrawStateCache residualState{};
    DrawForwardCommands(
        commandBuffer,
        *residualPipeline,
        doubleSidedResidualPipeline,
        descriptorSets,
        materialDescriptorSets,
        frameMaterials,
        renderCommands,
        extent,
        imageIndex,
        residualState,
        materialBindCount,
        meshBindCount,
        pushConstantUpdateCount,
        pushConstantByteCount,
        hdrOutputFlag
    );
    return static_cast<u32>(renderCommands.size());
}

u32 DrawDepthPrefillCommands(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline* depthPrefillPipeline,
    const VulkanGraphicsPipeline* doubleSidedDepthPrefillPipeline,
    const VulkanDescriptorSets& descriptorSets,
    std::span<const RenderCommand> renderCommands,
    std::size_t imageIndex,
    u32& meshBindCount,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount
) {
    if (depthPrefillPipeline == nullptr || renderCommands.empty()) {
        return 0;
    }

    DrawStateCache state{};
    const VulkanGraphicsPipeline* boundPipeline = nullptr;
    auto bindDepthPipeline = [&](const VulkanGraphicsPipeline& pipeline) {
        if (boundPipeline == &pipeline) {
            return;
        }

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.Handle()
        );

        const VkDescriptorSet descriptorSet = descriptorSets.Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.Layout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr
        );
        state = DrawStateCache{};
        boundPipeline = &pipeline;
    };
    u32 ignoredBonePaletteDescriptorBinds = 0;
    u32 ignoredBonePaletteFallbackDescriptorBinds = 0;
    for (const RenderCommand& renderCommand : renderCommands) {
        const VulkanGraphicsPipeline& activePipeline = PipelineForCommand(
            *depthPrefillPipeline,
            doubleSidedDepthPrefillPipeline,
            renderCommand
        );
        bindDepthPipeline(activePipeline);
        DrawShadowCommand(
            commandBuffer,
            activePipeline,
            renderCommand,
            glm::mat4{ 1.0f },
            VK_NULL_HANDLE,
            0u,
            state,
            meshBindCount,
            ignoredBonePaletteDescriptorBinds,
            ignoredBonePaletteFallbackDescriptorBinds,
            pushConstantUpdateCount,
            pushConstantByteCount
        );
    }

    return static_cast<u32>(renderCommands.size());
}

void TransitionDepthImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStageMask,
        dstStageMask,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

void BarrierComputeLightTilesForFragmentRead(VkCommandBuffer commandBuffer) {
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        1,
        &memoryBarrier,
        0,
        nullptr,
        0,
        nullptr
    );
}

void BarrierComputeAutoExposureForFragmentRead(VkCommandBuffer commandBuffer) {
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        1,
        &memoryBarrier,
        0,
        nullptr,
        0,
        nullptr
    );
}

void TransitionColorImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

void GenerateColorMipmaps(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkExtent2D extent,
    VkFormat format,
    u32 mipLevels,
    const VulkanPhysicalDevice& physicalDevice
) {
    if (mipLevels <= 1u) {
        return;
    }
    SE_ASSERT(
        physicalDevice.SupportsLinearBlit(format),
        "HDR scene color mip chain requires linear blit support"
    );

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    if (mipLevels > 1u) {
        barrier.subresourceRange.baseMipLevel = 1u;
        barrier.subresourceRange.levelCount = mipLevels - 1u;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
    }

    i32 mipWidth = static_cast<i32>(extent.width);
    i32 mipHeight = static_cast<i32>(extent.height);
    for (u32 mipLevel = 1u; mipLevel < mipLevels; ++mipLevel) {
        if (mipLevel > 1u) {
            barrier.subresourceRange.baseMipLevel = mipLevel - 1u;
            barrier.subresourceRange.levelCount = 1u;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier
            );
        }

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = mipLevel - 1u;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = {
            mipWidth > 1 ? mipWidth / 2 : 1,
            mipHeight > 1 ? mipHeight / 2 : 1,
            1
        };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = mipLevel;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR
        );

        barrier.subresourceRange.baseMipLevel = mipLevel - 1u;
        barrier.subresourceRange.levelCount = 1u;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );

        if (mipWidth > 1) {
            mipWidth /= 2;
        }
        if (mipHeight > 1) {
            mipHeight /= 2;
        }
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1u;
    barrier.subresourceRange.levelCount = 1u;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

void PrepareDlssMaskInput(
    VkCommandBuffer commandBuffer,
    VkImage image,
    bool initialized
) {
    TransitionColorImage(
        commandBuffer,
        image,
        initialized
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        initialized
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    VkClearColorValue clearValue{};
    clearValue.float32[0] = 0.0f;
    clearValue.float32[1] = 0.0f;
    clearValue.float32[2] = 0.0f;
    clearValue.float32[3] = 0.0f;
    const VkImageSubresourceRange range{
        VK_IMAGE_ASPECT_COLOR_BIT,
        0,
        1,
        0,
        1
    };
    vkCmdClearColorImage(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearValue,
        1,
        &range
    );

    TransitionColorImage(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    );
}

void PrepareTemporalHistoryColorForSampling(
    VkCommandBuffer commandBuffer,
    const VulkanSceneRenderTargets& renderTargets
) {
    for (std::size_t index = 0; index < renderTargets.Count(); ++index) {
        TransitionColorImage(
            commandBuffer,
            renderTargets.TemporalHistoryColorImage(index),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            0,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        );
    }
}

void ClearSsrReconstructionImages(
    VkCommandBuffer commandBuffer,
    const VulkanSceneRenderTargets& renderTargets,
    bool initialized
) {
    const VkClearColorValue clearValue{{ 0.0f, 0.0f, 0.0f, 0.0f }};
    const VkImageSubresourceRange range{
        VK_IMAGE_ASPECT_COLOR_BIT,
        0,
        1,
        0,
        1
    };
    auto clearImage = [&](VkImage image) {
        TransitionColorImage(
            commandBuffer,
            image,
            initialized
                ? VK_IMAGE_LAYOUT_GENERAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            initialized ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            initialized ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT
        );
        vkCmdClearColorImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearValue,
            1,
            &range
        );
        TransitionColorImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        );
    };

    for (std::size_t index = 0; index < renderTargets.Count(); ++index) {
        clearImage(renderTargets.SsrRawImage(index));
        clearImage(renderTargets.SsrResolvedImage(index));
        clearImage(renderTargets.SsrHistoryColorImage(index));
        clearImage(renderTargets.SsrHistoryMetadataImage(index));
    }
}

void BarrierSsrComputeImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkAccessFlags sourceAccess,
    VkAccessFlags destinationAccess,
    VkPipelineStageFlags sourceStage,
    VkPipelineStageFlags destinationStage
) {
    TransitionColorImage(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL,
        sourceAccess,
        destinationAccess,
        sourceStage,
        destinationStage
    );
}

void CopySsrHistoryToOtherImages(
    VkCommandBuffer commandBuffer,
    const VulkanSceneRenderTargets& renderTargets,
    std::size_t sourceIndex
) {
    if (renderTargets.Count() <= 1 || sourceIndex >= renderTargets.Count()) {
        return;
    }

    const VkExtent2D extent = renderTargets.Extent();
    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent = { extent.width, extent.height, 1 };
    for (std::size_t destinationIndex = 0;
         destinationIndex < renderTargets.Count();
         ++destinationIndex) {
        if (destinationIndex == sourceIndex) {
            continue;
        }
        auto copyImage = [&](VkImage source, VkImage destination) {
            TransitionColorImage(
                commandBuffer,
                source,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT
            );
            TransitionColorImage(
                commandBuffer,
                destination,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT
            );
            vkCmdCopyImage(
                commandBuffer,
                source,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                destination,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy
            );
            TransitionColorImage(
                commandBuffer,
                destination,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            );
            TransitionColorImage(
                commandBuffer,
                source,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            );
        };
        copyImage(
            renderTargets.SsrHistoryColorImage(sourceIndex),
            renderTargets.SsrHistoryColorImage(destinationIndex)
        );
        copyImage(
            renderTargets.SsrHistoryMetadataImage(sourceIndex),
            renderTargets.SsrHistoryMetadataImage(destinationIndex)
        );
        copyImage(
            renderTargets.SsrResolvedImage(sourceIndex),
            renderTargets.SsrResolvedImage(destinationIndex)
        );
    }
}

void CopyFfxSssrImage(
    VkCommandBuffer commandBuffer,
    VkImage source,
    VkImage destination,
    VkExtent2D extent
) {
    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent = { extent.width, extent.height, 1 };

    constexpr VkAccessFlags kGeneralAccess =
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_SHADER_WRITE_BIT |
        VK_ACCESS_TRANSFER_WRITE_BIT;
    constexpr VkPipelineStageFlags kGeneralStages =
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_TRANSFER_BIT;

    TransitionColorImage(
        commandBuffer,
        source,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        kGeneralAccess,
        VK_ACCESS_TRANSFER_READ_BIT,
        kGeneralStages,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );
    TransitionColorImage(
        commandBuffer,
        destination,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        kGeneralAccess,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        kGeneralStages,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );
    vkCmdCopyImage(
        commandBuffer,
        source,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destination,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy
    );
    TransitionColorImage(
        commandBuffer,
        destination,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
    );
    TransitionColorImage(
        commandBuffer,
        source,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
}

u32 CopyFfxSssrCurrentDenoiserStateToHistory(
    VkCommandBuffer commandBuffer,
    const VulkanFfxSssrReprojectResources& reprojectResources,
    const VulkanFfxSssrPrefilterResources& prefilterResources,
    std::size_t imageIndex
) {
    if (imageIndex >= reprojectResources.Count() ||
        imageIndex >= prefilterResources.Count()) {
        return 0u;
    }

    CopyFfxSssrImage(
        commandBuffer,
        reprojectResources.AverageRadianceImage(imageIndex),
        reprojectResources.AverageRadianceHistoryImage(imageIndex),
        reprojectResources.AverageExtent()
    );
    CopyFfxSssrImage(
        commandBuffer,
        prefilterResources.SampleCountImage(imageIndex),
        reprojectResources.SampleCountHistoryImage(imageIndex),
        reprojectResources.Extent()
    );
    CopyFfxSssrImage(
        commandBuffer,
        reprojectResources.HitConfidenceImage(imageIndex),
        reprojectResources.HitConfidenceHistoryImage(imageIndex),
        reprojectResources.Extent()
    );
    return 3u;
}

void ClearFfxSssrVisibleOutput(
    VkCommandBuffer commandBuffer,
    const VulkanFfxSssrReprojectResources& reprojectResources,
    std::size_t imageIndex
) {
    if (imageIndex >= reprojectResources.Count()) {
        return;
    }

    VkImageMemoryBarrier clearBarrier{};
    clearBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    clearBarrier.srcAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    clearBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    clearBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    clearBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.image = reprojectResources.RadianceHistoryImage(imageIndex);
    clearBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearBarrier.subresourceRange.baseMipLevel = 0u;
    clearBarrier.subresourceRange.levelCount = 1u;
    clearBarrier.subresourceRange.baseArrayLayer = 0u;
    clearBarrier.subresourceRange.layerCount = 1u;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1u,
        &clearBarrier
    );

    const VkClearColorValue clearValue{};
    vkCmdClearColorImage(
        commandBuffer,
        reprojectResources.RadianceHistoryImage(imageIndex),
        VK_IMAGE_LAYOUT_GENERAL,
        &clearValue,
        1u,
        &clearBarrier.subresourceRange
    );
}

u32 CopyFfxSssrHistoryToOtherImages(
    VkCommandBuffer commandBuffer,
    const VulkanFfxSssrReprojectResources& reprojectResources,
    std::size_t sourceIndex
) {
    if (reprojectResources.Count() <= 1 ||
        sourceIndex >= reprojectResources.Count()) {
        return 0u;
    }

    u32 copyCount = 0u;
    for (std::size_t destinationIndex = 0;
         destinationIndex < reprojectResources.Count();
         ++destinationIndex) {
        if (destinationIndex == sourceIndex) {
            continue;
        }

        CopyFfxSssrImage(
            commandBuffer,
            reprojectResources.RadianceHistoryImage(sourceIndex),
            reprojectResources.RadianceHistoryImage(destinationIndex),
            reprojectResources.Extent()
        );
        CopyFfxSssrImage(
            commandBuffer,
            reprojectResources.AverageRadianceHistoryImage(sourceIndex),
            reprojectResources.AverageRadianceHistoryImage(destinationIndex),
            reprojectResources.AverageExtent()
        );
        CopyFfxSssrImage(
            commandBuffer,
            reprojectResources.VarianceHistoryImage(sourceIndex),
            reprojectResources.VarianceHistoryImage(destinationIndex),
            reprojectResources.Extent()
        );
        CopyFfxSssrImage(
            commandBuffer,
            reprojectResources.SampleCountHistoryImage(sourceIndex),
            reprojectResources.SampleCountHistoryImage(destinationIndex),
            reprojectResources.Extent()
        );
        CopyFfxSssrImage(
            commandBuffer,
            reprojectResources.HitConfidenceHistoryImage(sourceIndex),
            reprojectResources.HitConfidenceHistoryImage(destinationIndex),
            reprojectResources.Extent()
        );
        copyCount += 5u;
    }
    return copyCount;
}

VkImage TemporalHistoryCopySourceImage(
    const VulkanSceneRenderTargets& renderTargets,
    std::size_t sourceImageIndex,
    TemporalHistoryColorCopySource copySource
) {
    return copySource == TemporalHistoryColorCopySource::TaaResolvedColor
        ? renderTargets.TemporalResolvedColorImage(sourceImageIndex)
        : renderTargets.HdrSceneColorImage(sourceImageIndex);
}

void CopyColorToTemporalHistory(
    VkCommandBuffer commandBuffer,
    const VulkanSceneRenderTargets& renderTargets,
    std::size_t sourceImageIndex,
    TemporalHistoryColorCopySource copySource
) {
    if (sourceImageIndex >= renderTargets.Count()) {
        return;
    }

    const VkImage sourceImage =
        TemporalHistoryCopySourceImage(renderTargets, sourceImageIndex, copySource);
    const bool sourceWrittenByResolvePass =
        copySource == TemporalHistoryColorCopySource::TaaResolvedColor;

    TransitionColorImage(
        commandBuffer,
        sourceImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        sourceWrittenByResolvePass
            ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            : VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        sourceWrittenByResolvePass
            ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    const VkExtent2D extent = renderTargets.Extent();
    VkImageCopy copyRegion{};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.extent = { extent.width, extent.height, 1 };

    for (std::size_t index = 0; index < renderTargets.Count(); ++index) {
        TransitionColorImage(
            commandBuffer,
            renderTargets.TemporalHistoryColorImage(index),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT
        );
        vkCmdCopyImage(
            commandBuffer,
            sourceImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            renderTargets.TemporalHistoryColorImage(index),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion
        );
        TransitionColorImage(
            commandBuffer,
            renderTargets.TemporalHistoryColorImage(index),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        );
    }

    TransitionColorImage(
        commandBuffer,
        sourceImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
}

void RecordTaaResolveHistoryPass(
    VkCommandBuffer commandBuffer,
    const VulkanHdrRenderPass& renderPass,
    const VulkanHdrFramebuffer& framebuffer,
    const VulkanGraphicsPipeline& pipeline,
    const VulkanDescriptorSets& frameDescriptorSets,
    const VulkanHdrDescriptorSets& hdrDescriptorSets,
    std::size_t imageIndex
) {
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass.Handle();
    renderPassInfo.framebuffer = framebuffer.Handle(imageIndex);
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = framebuffer.Extent();
    renderPassInfo.clearValueCount = static_cast<u32>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE
    );

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline.Handle()
    );

    const VkDescriptorSet frameDescriptorSet =
        frameDescriptorSets.Handle(imageIndex);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline.Layout(),
        0,
        1,
        &frameDescriptorSet,
        0,
        nullptr
    );

    const VkDescriptorSet hdrDescriptorSet =
        hdrDescriptorSets.Handle(imageIndex);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline.Layout(),
        1,
        1,
        &hdrDescriptorSet,
        0,
        nullptr
    );

    SetViewportAndScissor(commandBuffer, { 0, 0 }, framebuffer.Extent());
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}

void RecordTemporalUpscalerEvaluate(
    VkCommandBuffer commandBuffer,
    VkDevice device,
    const VulkanSceneRenderTargets& renderTargets,
    std::size_t imageIndex,
    const FrameTemporalState& temporalState,
    const FrameTemporalUpscaleState& temporalUpscaleState,
    bool temporalUpscaleOutputInitialized,
    bool dlssMaskInputsInitialized,
    bool dlssMaskInputsPrepared,
    TemporalUpscalerEvaluateStatus& evaluateStatus
) {
    evaluateStatus = TemporalUpscalerEvaluateStatus{};
    evaluateStatus.requested =
        temporalUpscaleState.temporalUpscaleRequested ? 1u : 0u;
    if (!temporalUpscaleState.temporalUpscaleEnabled ||
        !temporalUpscaleState.temporalUpscaleContractReady ||
        !temporalUpscaleState.upscalerPluginAvailable ||
        imageIndex >= renderTargets.Count()) {
        return;
    }

    TransitionColorImage(
        commandBuffer,
        renderTargets.HdrSceneColorImage(imageIndex),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    );
    TransitionDepthImage(
        commandBuffer,
        renderTargets.SceneDepthImage(imageIndex),
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    );
    TransitionColorImage(
        commandBuffer,
        renderTargets.VelocityImage(imageIndex),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    );
    if (!dlssMaskInputsPrepared) {
        PrepareDlssMaskInput(
            commandBuffer,
            renderTargets.DlssBiasCurrentColorMaskImage(imageIndex),
            dlssMaskInputsInitialized
        );
        PrepareDlssMaskInput(
            commandBuffer,
            renderTargets.DlssTransparencyMaskImage(imageIndex),
            dlssMaskInputsInitialized
        );
    }
    const VkImage temporalUpscaleOutputImage =
        renderTargets.TemporalUpscaleOutputImage(imageIndex);
    if (DlssClearOutputBeforeEvaluateRequested()) {
        TransitionColorImage(
            commandBuffer,
            temporalUpscaleOutputImage,
            temporalUpscaleOutputInitialized
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            temporalUpscaleOutputInitialized ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            temporalUpscaleOutputInitialized
                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT
        );

        VkClearColorValue clearValue{};
        clearValue.float32[0] = 0.0f;
        clearValue.float32[1] = 1.0f;
        clearValue.float32[2] = 0.0f;
        clearValue.float32[3] = 1.0f;
        const VkImageSubresourceRange range{
            VK_IMAGE_ASPECT_COLOR_BIT,
            0,
            1,
            0,
            1
        };
        vkCmdClearColorImage(
            commandBuffer,
            temporalUpscaleOutputImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearValue,
            1,
            &range
        );

        TransitionColorImage(
            commandBuffer,
            temporalUpscaleOutputImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        );
    } else {
        TransitionColorImage(
            commandBuffer,
            temporalUpscaleOutputImage,
            temporalUpscaleOutputInitialized
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            temporalUpscaleOutputInitialized ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_SHADER_WRITE_BIT,
            temporalUpscaleOutputInitialized
                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        );
    }

    const VkExtent2D renderExtent = renderTargets.Extent();
    const VkExtent2D outputExtent = renderTargets.DisplayExtent();
    const glm::vec2 rawDlssJitterPixels = temporalState.jitterApplied
        ? temporalState.jitterPixels
        : glm::vec2(0.0f);
    const glm::vec2 dlssJitterPixels =
        DlssJitterOffsetPixels(rawDlssJitterPixels);
    const glm::vec2 dlssMotionVectorScale =
        DlssMotionVectorScale(renderExtent);
    evaluateStatus = EvaluateTemporalUpscaler(
        TemporalUpscalerEvaluateRequest{
            temporalUpscaleState.upscalerRuntime,
            device,
            commandBuffer,
            TemporalUpscalerVulkanImageResource{
                renderTargets.HdrSceneColorImage(imageIndex),
                renderTargets.HdrSceneColorAttachmentView(imageIndex),
                renderTargets.HdrSceneColorFormat(),
                renderExtent,
                VK_IMAGE_ASPECT_COLOR_BIT,
                false
            },
            TemporalUpscalerVulkanImageResource{
                renderTargets.SceneDepthImage(imageIndex),
                renderTargets.SceneDepthView(imageIndex),
                renderTargets.SceneDepthFormat(),
                renderExtent,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                false
            },
            TemporalUpscalerVulkanImageResource{
                renderTargets.VelocityImage(imageIndex),
                renderTargets.VelocityView(imageIndex),
                renderTargets.VelocityFormat(),
                renderExtent,
                VK_IMAGE_ASPECT_COLOR_BIT,
                false
            },
            TemporalUpscalerVulkanImageResource{
                renderTargets.DlssBiasCurrentColorMaskImage(imageIndex),
                renderTargets.DlssBiasCurrentColorMaskView(imageIndex),
                renderTargets.DlssBiasCurrentColorMaskFormat(),
                renderExtent,
                VK_IMAGE_ASPECT_COLOR_BIT,
                false
            },
            TemporalUpscalerVulkanImageResource{
                renderTargets.DlssTransparencyMaskImage(imageIndex),
                renderTargets.DlssTransparencyMaskView(imageIndex),
                renderTargets.DlssTransparencyMaskFormat(),
                renderExtent,
                VK_IMAGE_ASPECT_COLOR_BIT,
                false
            },
            TemporalUpscalerVulkanImageResource{
                renderTargets.TemporalUpscaleOutputImage(imageIndex),
                renderTargets.TemporalUpscaleOutputView(imageIndex),
                renderTargets.TemporalUpscaleOutputFormat(),
                outputExtent,
                VK_IMAGE_ASPECT_COLOR_BIT,
                true
            },
            renderExtent,
            outputExtent,
            temporalUpscaleState.dlssQualityMode,
            temporalUpscaleState.dlssPreset,
            (temporalState.historyReset || !temporalUpscaleOutputInitialized) ? 1u : 0u,
            dlssJitterPixels.x,
            dlssJitterPixels.y,
            dlssMotionVectorScale.x,
            dlssMotionVectorScale.y,
            temporalUpscaleState.upscalerRuntime.sharpness
        }
    );

    TransitionColorImage(
        commandBuffer,
        renderTargets.TemporalUpscaleOutputImage(imageIndex),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    TransitionColorImage(
        commandBuffer,
        renderTargets.DlssTransparencyMaskImage(imageIndex),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    TransitionColorImage(
        commandBuffer,
        renderTargets.DlssBiasCurrentColorMaskImage(imageIndex),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    TransitionColorImage(
        commandBuffer,
        renderTargets.VelocityImage(imageIndex),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    TransitionDepthImage(
        commandBuffer,
        renderTargets.SceneDepthImage(imageIndex),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    TransitionColorImage(
        commandBuffer,
        renderTargets.HdrSceneColorImage(imageIndex),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
}

void RecordBloomFullscreenPass(
    VkCommandBuffer commandBuffer,
    const VulkanBloomRenderPass& renderPass,
    const VulkanBloomFramebuffer& framebuffer,
    const VulkanGraphicsPipeline& pipeline,
    const VulkanDescriptorSets& frameDescriptorSets,
    const VulkanBloomDescriptorSets& bloomDescriptorSets,
    std::size_t imageIndex,
    u32 mipIndex,
    bool upsample,
    RendererBindStats* bindStats
) {
    VkClearValue clearValue{};
    clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = renderPass.Handle();
    passInfo.framebuffer = framebuffer.Handle(imageIndex, mipIndex);
    passInfo.renderArea.offset = { 0, 0 };
    passInfo.renderArea.extent = framebuffer.MipExtent(mipIndex);
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Handle());

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<f32>(passInfo.renderArea.extent.width);
    viewport.height = static_cast<f32>(passInfo.renderArea.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = passInfo.renderArea.extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const VkDescriptorSet frameDescriptorSet = frameDescriptorSets.Handle(imageIndex);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline.Layout(),
        0,
        1,
        &frameDescriptorSet,
        0,
        nullptr
    );

    const VkDescriptorSet bloomDescriptorSet = upsample
        ? bloomDescriptorSets.UpsampleHandle(imageIndex, mipIndex)
        : bloomDescriptorSets.DownsampleHandle(imageIndex, mipIndex);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline.Layout(),
        1,
        1,
        &bloomDescriptorSet,
        0,
        nullptr
    );

    ObjectPushConstants passConstants{};
    passConstants.materialControls.x = static_cast<f32>(mipIndex);
    vkCmdPushConstants(
        commandBuffer,
        pipeline.Layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(ObjectPushConstants),
        &passConstants
    );

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    if (bindStats != nullptr) {
        ++bindStats->pushConstantUpdates;
        bindStats->pushConstantBytes += sizeof(ObjectPushConstants);
        if (upsample) {
            ++bindStats->bloomUpsampleDraws;
            ++bindStats->bloomUpsampleFrameBinds;
            ++bindStats->bloomUpsampleTextureBinds;
        } else {
            ++bindStats->bloomDownsampleDraws;
            ++bindStats->bloomDownsampleFrameBinds;
            ++bindStats->bloomDownsampleTextureBinds;
        }
    }
}

void ClearBloomMipForSampledRead(
    VkCommandBuffer commandBuffer,
    const VulkanBloomRenderPass& renderPass,
    const VulkanBloomFramebuffer& framebuffer,
    std::size_t imageIndex
) {
    VkClearValue clearValue{};
    clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = renderPass.Handle();
    passInfo.framebuffer = framebuffer.Handle(imageIndex, 0);
    passInfo.renderArea.offset = { 0, 0 };
    passInfo.renderArea.extent = framebuffer.MipExtent(0);
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(commandBuffer);
}

bool CopySceneDepthToSwapchainDepth(
    VkCommandBuffer commandBuffer,
    const VulkanSceneRenderTargets* sceneRenderTargets,
    const VulkanDepthBuffer* swapchainDepthBuffer,
    std::size_t imageIndex,
    VkExtent2D extent
) {
    if (sceneRenderTargets == nullptr || swapchainDepthBuffer == nullptr) {
        return false;
    }
    if (imageIndex >= sceneRenderTargets->Count() ||
        imageIndex >= swapchainDepthBuffer->Count()) {
        return false;
    }

    if (sceneRenderTargets->Extent().width != extent.width ||
        sceneRenderTargets->Extent().height != extent.height) {
        return false;
    }

    const VkImage sceneDepthImage = sceneRenderTargets->SceneDepthImage(imageIndex);
    const VkImage swapchainDepthImage = swapchainDepthBuffer->Image(imageIndex);

    TransitionDepthImage(
        commandBuffer,
        sceneDepthImage,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );
    TransitionDepthImage(
        commandBuffer,
        swapchainDepthImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.extent = { extent.width, extent.height, 1 };

    vkCmdCopyImage(
        commandBuffer,
        sceneDepthImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchainDepthImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    TransitionDepthImage(
        commandBuffer,
        sceneDepthImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    TransitionDepthImage(
        commandBuffer,
        swapchainDepthImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
    );

    return true;
}

void DrawInstancedRenderCommand(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const VulkanMaterialDescriptorSets& materialDescriptorSets,
    const RenderCommand& renderCommand,
    const RenderInstanceBatch& batch,
    const VkExtent2D& extent,
    std::size_t imageIndex,
    f32 materialId,
    DrawStateCache& state,
    u32& materialBindCount,
    u32& meshBindCount,
    u32& pushConstantUpdateCount,
    u64& pushConstantByteCount,
    u32& instancedDrawCount,
    u32& instancedInstanceCount
) {
    SE_ASSERT(renderCommand.mesh != nullptr, "Instanced RenderCommand must reference a mesh");
    SE_ASSERT(renderCommand.material != nullptr, "Instanced RenderCommand must reference a material");
    SE_ASSERT(batch.commandCount > 1, "Instanced batch must draw at least two instances");

    if (BindMaterialIfNeeded(
        commandBuffer,
        graphicsPipeline,
        materialDescriptorSets,
        renderCommand,
        imageIndex,
        state
    )) {
        ++materialBindCount;
        PushMaterialConstants(
            commandBuffer,
            graphicsPipeline,
            renderCommand,
            extent,
            materialId,
            pushConstantUpdateCount,
            pushConstantByteCount
        );
    }

    ObjectPushConstants objectData{};
    objectData.model = glm::mat4(1.0f);
    objectData.previousModel = glm::mat4(1.0f);
    objectData.tint = renderCommand.tint;
    PushConstants(
        commandBuffer,
        graphicsPipeline,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        kObjectPushConstantBytes,
        &objectData,
        pushConstantUpdateCount,
        pushConstantByteCount
    );

    if (BindMeshIfNeeded(commandBuffer, renderCommand, state)) {
        ++meshBindCount;
    }
    vkCmdDrawIndexed(
        commandBuffer,
        renderCommand.mesh->IndexCount(),
        batch.commandCount,
        0,
        0,
        batch.firstInstance
    );
    ++instancedDrawCount;
    instancedInstanceCount += batch.commandCount;
}

}

ReflectionCaptureDrawStats RecordReflectionCaptureCommands(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedGraphicsPipeline,
    const VulkanDescriptorSets& descriptorSets,
    const VulkanMaterialDescriptorSets& materialDescriptorSets,
    const FrameMaterialSet& frameMaterials,
    std::span<const RenderCommand> renderCommands,
    const VkExtent2D& extent,
    std::size_t imageIndex
) {
    ReflectionCaptureDrawStats stats{};
    DrawStateCache state{};
    DrawForwardCommands(
        commandBuffer,
        graphicsPipeline,
        doubleSidedGraphicsPipeline,
        descriptorSets,
        materialDescriptorSets,
        &frameMaterials,
        renderCommands,
        extent,
        imageIndex,
        state,
        stats.materialBindCount,
        stats.meshBindCount,
        stats.pushConstantUpdateCount,
        stats.pushConstantByteCount,
        1.0f
    );
    stats.drawCount = static_cast<u32>(renderCommands.size());
    return stats;
}

ReflectionCaptureDirectionalShadowDrawStats
RecordReflectionCaptureDirectionalShadow(
    VkCommandBuffer commandBuffer,
    const VulkanShadowRenderPass& shadowRenderPass,
    const VulkanGraphicsPipeline& shadowGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedShadowGraphicsPipeline,
    const VulkanShadowFramebuffer& shadowFramebuffer,
    const VulkanDescriptorSets& shadowDescriptorSets,
    std::span<const RenderCommand> shadowRenderCommands,
    std::size_t imageIndex,
    const glm::mat4& lightViewProjection,
    const ShadowDepthBiasControls& shadowDepthBias,
    VkDescriptorSet bonePaletteFallbackDescriptorSet,
    u32 bonePaletteFallbackDescriptorReady
) {
    ReflectionCaptureDirectionalShadowDrawStats stats{};
    if (shadowRenderCommands.empty()) {
        return stats;
    }

    VkClearValue clearValue{};
    clearValue.depthStencil = { 1.0f, 0u };
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = shadowRenderPass.Handle();
    renderPassInfo.framebuffer = shadowFramebuffer.Handle(imageIndex);
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = shadowFramebuffer.Extent();
    renderPassInfo.clearValueCount = 1u;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE
    );
    DrawShadowCommands(
        commandBuffer,
        shadowGraphicsPipeline,
        doubleSidedShadowGraphicsPipeline,
        shadowDescriptorSets,
        shadowRenderCommands,
        imageIndex,
        lightViewProjection,
        shadowDepthBias,
        bonePaletteFallbackDescriptorSet,
        bonePaletteFallbackDescriptorReady,
        { 0, 0 },
        shadowFramebuffer.Extent(),
        stats.meshBindCount,
        stats.bonePaletteDescriptorBindCount,
        stats.bonePaletteFallbackDescriptorBindCount,
        stats.pushConstantUpdateCount,
        stats.pushConstantByteCount
    );
    vkCmdEndRenderPass(commandBuffer);
    stats.passCount = 1u;
    stats.drawCount = static_cast<u32>(shadowRenderCommands.size());
    return stats;
}

ReflectionCaptureLocalShadowDrawStats
RecordReflectionCaptureLocalShadows(
    VkCommandBuffer commandBuffer,
    const VulkanShadowRenderPass& shadowRenderPass,
    const VulkanGraphicsPipeline& shadowGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedShadowGraphicsPipeline,
    const VulkanShadowFramebuffer& localShadowFramebuffer,
    const VulkanDescriptorSets& shadowDescriptorSets,
    const LocalShadowTileSet& localShadowTiles,
    std::span<const std::span<const RenderCommand>> localShadowTileRenderCommands,
    std::size_t imageIndex,
    const ShadowDepthBiasControls& shadowDepthBias,
    VkDescriptorSet bonePaletteFallbackDescriptorSet,
    u32 bonePaletteFallbackDescriptorReady
) {
    ReflectionCaptureLocalShadowDrawStats stats{};
    const u32 assignedTileCount = std::min<u32>(
        localShadowTiles.assignedCount,
        static_cast<u32>(localShadowTiles.tiles.size())
    );
    if (assignedTileCount == 0u) {
        return stats;
    }

    VkClearValue clearValue{};
    clearValue.depthStencil = { 1.0f, 0u };
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = shadowRenderPass.Handle();
    renderPassInfo.framebuffer = localShadowFramebuffer.Handle(imageIndex);
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = localShadowFramebuffer.Extent();
    renderPassInfo.clearValueCount = 1u;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE
    );

    const VkExtent2D atlasExtent = localShadowFramebuffer.Extent();
    const u32 tileColumns = std::max(localShadowTiles.tileColumns, 1u);
    const VkExtent2D tileExtent{
        localShadowTiles.tileSize > 0u
            ? localShadowTiles.tileSize
            : std::max(atlasExtent.width / tileColumns, 1u),
        localShadowTiles.tileSize > 0u
            ? localShadowTiles.tileSize
            : std::max(atlasExtent.height / tileColumns, 1u)
    };
    for (u32 tileSetIndex = 0u;
         tileSetIndex < assignedTileCount;
         ++tileSetIndex) {
        const LocalShadowTile& tile = localShadowTiles.tiles[tileSetIndex];
        const u32 tileX = tile.tileIndex % tileColumns;
        const u32 tileY = tile.tileIndex / tileColumns;
        const VkOffset2D tileOffset{
            static_cast<i32>(tileX * tileExtent.width),
            static_cast<i32>(tileY * tileExtent.height)
        };
        const std::span<const RenderCommand> tileRenderCommands =
            tileSetIndex < localShadowTileRenderCommands.size()
                ? localShadowTileRenderCommands[tileSetIndex]
                : std::span<const RenderCommand>{};
        DrawShadowCommands(
            commandBuffer,
            shadowGraphicsPipeline,
            doubleSidedShadowGraphicsPipeline,
            shadowDescriptorSets,
            tileRenderCommands,
            imageIndex,
            tile.viewProjection,
            shadowDepthBias,
            bonePaletteFallbackDescriptorSet,
            bonePaletteFallbackDescriptorReady,
            tileOffset,
            tileExtent,
            stats.meshBindCount,
            stats.bonePaletteDescriptorBindCount,
            stats.bonePaletteFallbackDescriptorBindCount,
            stats.pushConstantUpdateCount,
            stats.pushConstantByteCount
        );
        ++stats.tilePassCount;
        stats.drawCount += static_cast<u32>(tileRenderCommands.size());
    }

    vkCmdEndRenderPass(commandBuffer);
    return stats;
}

VulkanCommandBuffer::VulkanCommandBuffer(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    const VulkanFramebuffer& framebuffer
) : m_Device(device.Handle()), m_CommandPool(commandPool.Handle()) {
    try {
        AllocateCommandBuffers(device, commandPool, framebuffer.Handles().size());
    } catch (...) {
        Release();
        throw;
    }
}

VulkanCommandBuffer::~VulkanCommandBuffer() {
    Release();
}

const std::vector<VkCommandBuffer>& VulkanCommandBuffer::Handles() const {
    return m_CommandBuffers;
}

VkCommandBuffer VulkanCommandBuffer::Handle(std::size_t index) const {
    SE_ASSERT(index < m_CommandBuffers.size(), "Command buffer index is out of range");
    return m_CommandBuffers[index];
}

void VulkanCommandBuffer::Recreate(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    const VulkanFramebuffer& framebuffer
) {
    Release();
    m_Device = device.Handle();
    m_CommandPool = commandPool.Handle();

    try {
        AllocateCommandBuffers(device, commandPool, framebuffer.Handles().size());
    } catch (...) {
        Release();
        throw;
    }
}

void VulkanCommandBuffer::AllocateCommandBuffers(
    const VulkanDevice& device,
    const VulkanCommandPool& commandPool,
    std::size_t count
) {
    SE_ASSERT(count > 0, "Command buffer count must be greater than zero");

    m_CommandBuffers.resize(count);

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool.Handle();
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = static_cast<u32>(m_CommandBuffers.size());

    if (vkAllocateCommandBuffers(device.Handle(), &allocateInfo, m_CommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan command buffers");
    }
}

void VulkanCommandBuffer::Record(
    std::size_t imageIndex,
    const VulkanRenderPass& renderPass,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedGraphicsPipeline,
    const VulkanDescriptorSets& descriptorSets,
    const VulkanMaterialDescriptorSets& materialDescriptorSets,
    std::span<const RenderCommand> renderCommands,
    const VulkanFramebuffer& framebuffer,
    const VulkanRenderPass* depthLoadRenderPass,
    const VulkanFramebuffer* depthLoadFramebuffer,
    const VulkanSwapchain& swapchain,
    const VulkanPhysicalDevice& physicalDevice,
    VulkanImGuiLayer* imguiLayer,
    const VulkanShadowRenderPass* shadowRenderPass,
    const VulkanGraphicsPipeline* shadowGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedShadowGraphicsPipeline,
    const VulkanShadowFramebuffer* shadowFramebuffer,
    const VulkanDescriptorSets* shadowDescriptorSets,
    std::span<const RenderCommand> shadowRenderCommands,
    const VulkanShadowFramebuffer* directionalShadowCascadeFramebuffer,
    const DirectionalShadowCascadeSet* directionalShadowCascades,
    const VulkanShadowFramebuffer* localShadowFramebuffer,
    const LocalShadowTileSet* localShadowTiles,
    std::span<const std::span<const RenderCommand>> directionalShadowCascadeRenderCommands,
    std::span<const std::span<const RenderCommand>> localShadowTileRenderCommands,
    ShadowDepthBiasControls shadowDepthBias,
    bool skipCachedLocalShadowTiles,
    const VulkanHdrRenderPass* hdrRenderPass,
    const VulkanHdrFramebuffer* hdrFramebuffer,
    const VulkanGraphicsPipeline* deferredLightingPipeline,
    const VulkanDescriptorSets* deferredLightingFrameDescriptorSets,
    const VulkanGBufferDescriptorSets* deferredLightingGBufferDescriptorSets,
    int deferredPbrDebugView,
    const VulkanGraphicsPipeline* hdrCompositePipeline,
    const VulkanHdrDescriptorSets* hdrCompositeDescriptorSets,
    const VulkanHdrDescriptorSets* temporalUpscaleHdrCompositeDescriptorSets,
    const VulkanBloomRenderPass* bloomDownsampleRenderPass,
    const VulkanBloomRenderPass* bloomUpsampleRenderPass,
    const VulkanBloomFramebuffer* bloomDownsampleFramebuffer,
    const VulkanBloomFramebuffer* bloomUpsampleFramebuffer,
    const VulkanGraphicsPipeline* bloomDownsamplePipeline,
    const VulkanGraphicsPipeline* bloomUpsamplePipeline,
    const VulkanBloomDescriptorSets* bloomDescriptorSets,
    const VulkanBloomDescriptorSets* temporalUpscaleBloomDescriptorSets,
    bool recordBloomPyramid,
    bool useHdrCompositeAsMain,
    bool temporalUpscalePostSourceRequested,
    bool bloomDebugView,
    bool toneMappingDebugView,
    bool autoExposureDebugView,
    bool colorGradingDebugView,
    bool sharpeningDebugView,
    bool temporalHistoryColorInitialized,
    bool recordTemporalHistoryColorCopy,
    TemporalHistoryColorCopySource temporalHistoryColorCopySource,
    const VulkanHdrFramebuffer* taaResolveFramebuffer,
    const VulkanGraphicsPipeline* taaResolvePipeline,
    const FrameTemporalState* temporalState,
    const FrameTemporalUpscaleState* temporalUpscaleState,
    bool temporalUpscaleOutputInitialized,
    bool dlssMaskInputsInitialized,
    TemporalUpscalerEvaluateStatus* temporalUpscalerEvaluateStatus,
    TemporalUpscalePostSourceStatus* temporalUpscalePostSourceStatus,
    const VulkanGraphicsPipeline* gBufferDebugPipeline,
    const VulkanGBufferDescriptorSets* gBufferDebugDescriptorSets,
    int gBufferDebugView,
    const VulkanGraphicsPipeline* depthPrefillGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedDepthPrefillGraphicsPipeline,
    const VulkanSceneRenderTargets* sceneRenderTargets,
    const VulkanDepthBuffer* swapchainDepthBuffer,
    const VulkanGBufferRenderPass* gBufferRenderPass,
    const VulkanGBufferFramebuffer* gBufferFramebuffer,
    const VulkanForwardResidualVelocityRenderPass* forwardResidualVelocityRenderPass,
    const VulkanForwardResidualVelocityFramebuffer* forwardResidualVelocityFramebuffer,
    const VulkanGraphicsPipeline* forwardResidualVelocityGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedForwardResidualVelocityGraphicsPipeline,
    std::span<const RenderCommand> forwardResidualVelocityRenderCommands,
    std::span<const RenderCommand> weightedTranslucencyVelocityRenderCommands,
    const VulkanDlssMaskRenderPass* dlssMaskRenderPass,
    const VulkanDlssMaskFramebuffer* dlssMaskFramebuffer,
    const VulkanGraphicsPipeline* dlssMaskGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedDlssMaskGraphicsPipeline,
    std::span<const RenderCommand> dlssMaskWeightedTranslucencyRenderCommands,
    std::span<const RenderCommand> dlssMaskForwardResidualRenderCommands,
    const VulkanWeightedTranslucencyRenderPass* weightedTranslucencyRenderPass,
    const VulkanWeightedTranslucencyFramebuffer* weightedTranslucencyFramebuffer,
    const VulkanGraphicsPipeline* weightedTranslucencyGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedWeightedTranslucencyGraphicsPipeline,
    const VulkanGraphicsPipeline* weightedTranslucencyResolvePipeline,
    const VulkanWeightedTranslucencyDescriptorSets* weightedTranslucencyDescriptorSets,
    std::span<const RenderCommand> weightedTranslucencyRenderCommands,
    int weightedTranslucencyDebugView,
    const VulkanGraphicsPipeline* gBufferGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedGBufferGraphicsPipeline,
    const VulkanDescriptorSets* gBufferDescriptorSets,
    std::span<const RenderCommand> gBufferRenderCommands,
    VkDescriptorSet gBufferBonePaletteFallbackDescriptorSet,
    u32 gBufferBonePaletteFallbackDescriptorReady,
    const VulkanComputePipeline* lightTileCullComputePipeline,
    const VulkanDescriptorSets* lightTileCullDescriptorSets,
    u32 lightTileCullGroupCountX,
    u32 lightTileCullGroupCountY,
    u32 lightTileCullGroupCountZ,
    const VulkanComputePipeline* lightClusterCullComputePipeline,
    const VulkanComputePipeline* autoExposureComputePipeline,
    const VulkanDescriptorSets* autoExposureFrameDescriptorSets,
    const VulkanHdrDescriptorSets* autoExposureHdrDescriptorSets,
    bool recordAutoExposureCompute,
    const VulkanGraphicsPipeline* forwardResidualHdrGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedForwardResidualHdrGraphicsPipeline,
    const VulkanGraphicsPipeline* forwardResidualGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedForwardResidualGraphicsPipeline,
    std::span<const RenderCommand> forwardResidualRenderCommands,
    const FrameMaterialSet* frameMaterials,
    const VulkanGraphicsPipeline* overlayGraphicsPipeline,
    const VulkanDescriptorSets* overlayDescriptorSets,
    std::span<const RenderCommand> overlayRenderCommands,
    const VulkanGraphicsPipeline* instancedGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedInstancedGraphicsPipeline,
    const VulkanInstanceBuffer* instanceBuffer,
    std::span<const RenderInstanceBatch> instanceBatches,
    const VulkanGpuTimer* gpuTimer,
    RendererBindStats* bindStats,
    RenderFrameGraphPlan* frameGraph,
    const VulkanComputePipeline* hizBuildPipeline,
    const VulkanHiZDescriptorSets* hizDescriptorSets,
    const VulkanDepthPyramid* hizDepthPyramid,
    const VulkanSceneRenderTargets* hizSourceTargets,
    const VulkanComputePipeline* ssrTracePipeline,
    const VulkanComputePipeline* ssrTemporalPipeline,
    const VulkanComputePipeline* ssrSpatialPipeline,
    const VulkanComputePipeline* ssrDiagnosticsPipeline,
    const VulkanSsrReconstructionDescriptorSets* ssrDescriptorSets,
    const VulkanFfxSssrConstantsResources* ffxSssrConstantsResources,
    const VulkanComputePipeline* ffxSssrClassifyTilesPipeline,
    const VulkanFfxSssrClassifyTilesResources* ffxSssrClassifyTilesResources,
    const VulkanComputePipeline* ffxSssrPrepareIndirectArgsPipeline,
    const VulkanFfxSssrPrepareIndirectArgsResources*
        ffxSssrPrepareIndirectArgsResources,
    const VulkanComputePipeline* ffxSssrBlueNoisePipeline,
    const VulkanFfxSssrBlueNoiseResources* ffxSssrBlueNoiseResources,
    const VulkanComputePipeline* ffxSssrIntersectPipeline,
    const VulkanFfxSssrIntersectResources* ffxSssrIntersectResources,
    const VulkanComputePipeline* ffxSssrReprojectPipeline,
    const VulkanFfxSssrReprojectResources* ffxSssrReprojectResources,
    const VulkanComputePipeline* ffxSssrPrefilterPipeline,
    const VulkanFfxSssrPrefilterResources* ffxSssrPrefilterResources,
    const VulkanComputePipeline* ffxSssrResolveTemporalPipeline,
    const VulkanFfxSssrResolveTemporalResources*
        ffxSssrResolveTemporalResources,
    const VulkanGraphicsPipeline* ffxSssrApplyPipeline,
    const VulkanGBufferDescriptorSets* ffxSssrApplyGBufferDescriptorSets,
    bool ffxSssrSameFrameCompositeEnabled,
    bool ffxSssrPrepareIndirectArgsEnabled,
    bool ffxSssrVisibleOutputClearEnabled,
    const VulkanSceneRenderTargets* ssrTargets,
    bool ssrReconstructionEnabled,
    bool ssrImagesInitialized,
    bool ssrHistoryReset,
    VulkanHybridReflectionAccelerationStructures*
        hybridReflectionAccelerationStructures,
    RendererHybridReflectionStats* hybridReflectionStats
) const {
    const std::vector<VkFramebuffer>& framebuffers = framebuffer.Handles();
    const VkExtent2D extent = swapchain.Extent();

    SE_ASSERT(imageIndex < m_CommandBuffers.size(), "Command buffer index is out of range");
    SE_ASSERT(
        framebuffers.size() == m_CommandBuffers.size(),
        "Framebuffer count must match command buffer count"
    );
    if (shadowFramebuffer != nullptr) {
        SE_ASSERT(
            shadowFramebuffer->Count() == m_CommandBuffers.size(),
            "Shadow framebuffer count must match command buffer count"
        );
    }
    if (directionalShadowCascadeFramebuffer != nullptr) {
        SE_ASSERT(
            directionalShadowCascadeFramebuffer->Count() == m_CommandBuffers.size(),
            "Directional shadow cascade framebuffer count must match command buffer count"
        );
    }
    if (localShadowFramebuffer != nullptr) {
        SE_ASSERT(
            localShadowFramebuffer->Count() == m_CommandBuffers.size(),
            "Local shadow framebuffer count must match command buffer count"
        );
    }
    if (hdrFramebuffer != nullptr) {
        SE_ASSERT(
            hdrFramebuffer->Count() == m_CommandBuffers.size(),
            "HDR framebuffer count must match command buffer count"
        );
    }
    if (taaResolveFramebuffer != nullptr) {
        SE_ASSERT(
            taaResolveFramebuffer->Count() == m_CommandBuffers.size(),
            "TAA resolve framebuffer count must match command buffer count"
        );
    }
    if (depthLoadFramebuffer != nullptr) {
        SE_ASSERT(
            depthLoadFramebuffer->Handles().size() == m_CommandBuffers.size(),
            "Depth-load framebuffer count must match command buffer count"
        );
    }
    if (gBufferFramebuffer != nullptr) {
        SE_ASSERT(
            gBufferFramebuffer->Count() == m_CommandBuffers.size(),
            "GBuffer framebuffer count must match command buffer count"
        );
    }
    if (forwardResidualVelocityFramebuffer != nullptr) {
        SE_ASSERT(
            forwardResidualVelocityFramebuffer->Count() == m_CommandBuffers.size(),
            "Forward residual velocity framebuffer count must match command buffer count"
        );
    }
    if (dlssMaskFramebuffer != nullptr) {
        SE_ASSERT(
            dlssMaskFramebuffer->Count() == m_CommandBuffers.size(),
            "DLSS mask framebuffer count must match command buffer count"
        );
    }
    SE_ASSERT(
        descriptorSets.Count() == m_CommandBuffers.size(),
        "Descriptor set count must match command buffer count"
    );
    // Allow empty render commands (e.g., looking at sky with no geometry visible)
    VkCommandBuffer commandBuffer = m_CommandBuffers[imageIndex];
    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording Vulkan command buffer");
    }

    if (gpuTimer != nullptr) {
        gpuTimer->ResetFrame(commandBuffer, imageIndex);
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::FrameStart);
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ShadowStart);
    }

    if (hybridReflectionAccelerationStructures != nullptr &&
        hybridReflectionStats != nullptr) {
        hybridReflectionAccelerationStructures->RecordBuilds(
            commandBuffer,
            static_cast<u32>(imageIndex),
            *hybridReflectionStats
        );
    }

    if (shadowRenderPass != nullptr &&
        shadowGraphicsPipeline != nullptr &&
        shadowFramebuffer != nullptr &&
        shadowDescriptorSets != nullptr) {
        VkClearValue shadowClearValue{};
        shadowClearValue.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo shadowPassInfo{};
        shadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowPassInfo.renderPass = shadowRenderPass->Handle();
        shadowPassInfo.framebuffer = shadowFramebuffer->Handle(imageIndex);
        shadowPassInfo.renderArea.offset = { 0, 0 };
        shadowPassInfo.renderArea.extent = shadowFramebuffer->Extent();
        shadowPassInfo.clearValueCount = 1;
        shadowPassInfo.pClearValues = &shadowClearValue;

        vkCmdBeginRenderPass(
            commandBuffer,
            &shadowPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );

        u32 shadowMeshBinds = 0;
        u32 shadowBonePaletteDescriptorBinds = 0;
        u32 shadowBonePaletteFallbackDescriptorBinds = 0;
        u32 shadowPushConstantUpdates = 0;
        u64 shadowPushConstantBytes = 0;
        const glm::mat4 legacyLightViewProjection =
            directionalShadowCascades != nullptr &&
            directionalShadowCascades->activeCount > 0
                ? directionalShadowCascades->cascades[0].viewProjection
                : glm::mat4{ 1.0f };
        DrawShadowCommands(
            commandBuffer,
            *shadowGraphicsPipeline,
            doubleSidedShadowGraphicsPipeline,
            *shadowDescriptorSets,
            shadowRenderCommands,
            imageIndex,
            legacyLightViewProjection,
            shadowDepthBias,
            gBufferBonePaletteFallbackDescriptorSet,
            gBufferBonePaletteFallbackDescriptorReady,
            { 0, 0 },
            shadowFramebuffer->Extent(),
            shadowMeshBinds,
            shadowBonePaletteDescriptorBinds,
            shadowBonePaletteFallbackDescriptorBinds,
            shadowPushConstantUpdates,
            shadowPushConstantBytes
        );
        if (bindStats != nullptr) {
            bindStats->shadowMeshBinds += shadowMeshBinds;
            bindStats->bonePaletteDescriptorBinds += shadowBonePaletteDescriptorBinds;
            bindStats->bonePaletteFallbackDescriptorBinds +=
                shadowBonePaletteFallbackDescriptorBinds;
            bindStats->pushConstantUpdates += shadowPushConstantUpdates;
            bindStats->pushConstantBytes += shadowPushConstantBytes;
        }

        vkCmdEndRenderPass(commandBuffer);

    }

    if (shadowRenderPass != nullptr &&
        shadowGraphicsPipeline != nullptr &&
        directionalShadowCascadeFramebuffer != nullptr &&
        shadowDescriptorSets != nullptr &&
        directionalShadowCascades != nullptr &&
        directionalShadowCascades->activeCount > 0) {
        VkClearValue cascadeClearValue{};
        cascadeClearValue.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo cascadePassInfo{};
        cascadePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        cascadePassInfo.renderPass = shadowRenderPass->Handle();
        cascadePassInfo.framebuffer =
            directionalShadowCascadeFramebuffer->Handle(imageIndex);
        cascadePassInfo.renderArea.offset = { 0, 0 };
        cascadePassInfo.renderArea.extent =
            directionalShadowCascadeFramebuffer->Extent();
        cascadePassInfo.clearValueCount = 1;
        cascadePassInfo.pClearValues = &cascadeClearValue;

        vkCmdBeginRenderPass(
            commandBuffer,
            &cascadePassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );

        const VkExtent2D cascadeAtlasExtent =
            directionalShadowCascadeFramebuffer->Extent();
        const VkExtent2D cascadeTileExtent{
            std::max(cascadeAtlasExtent.width / 2u, 1u),
            std::max(cascadeAtlasExtent.height / 2u, 1u)
        };
        const u32 activeCascadeCount = std::min<u32>(
            directionalShadowCascades->activeCount,
            static_cast<u32>(kMaxDirectionalShadowCascades)
        );
        u32 cascadeAtlasMeshBinds = 0;
        u32 cascadeAtlasBonePaletteDescriptorBinds = 0;
        u32 cascadeAtlasBonePaletteFallbackDescriptorBinds = 0;
        u32 cascadeAtlasPushConstantUpdates = 0;
        u64 cascadeAtlasPushConstantBytes = 0;
        u32 cascadeAtlasDraws = 0;
        for (u32 cascadeIndex = 0; cascadeIndex < activeCascadeCount; ++cascadeIndex) {
            const u32 tileX = cascadeIndex % 2u;
            const u32 tileY = cascadeIndex / 2u;
            const VkOffset2D tileOffset{
                static_cast<i32>(tileX * cascadeTileExtent.width),
                static_cast<i32>(tileY * cascadeTileExtent.height)
            };
            const std::span<const RenderCommand> cascadeRenderCommands =
                cascadeIndex < directionalShadowCascadeRenderCommands.size()
                    ? directionalShadowCascadeRenderCommands[cascadeIndex]
                    : shadowRenderCommands;
            DrawShadowCommands(
                commandBuffer,
                *shadowGraphicsPipeline,
                doubleSidedShadowGraphicsPipeline,
                *shadowDescriptorSets,
                cascadeRenderCommands,
                imageIndex,
                directionalShadowCascades->cascades[cascadeIndex].viewProjection,
                shadowDepthBias,
                gBufferBonePaletteFallbackDescriptorSet,
                gBufferBonePaletteFallbackDescriptorReady,
                tileOffset,
                cascadeTileExtent,
                cascadeAtlasMeshBinds,
                cascadeAtlasBonePaletteDescriptorBinds,
                cascadeAtlasBonePaletteFallbackDescriptorBinds,
                cascadeAtlasPushConstantUpdates,
                cascadeAtlasPushConstantBytes
            );
            cascadeAtlasDraws += static_cast<u32>(cascadeRenderCommands.size());
        }

        if (bindStats != nullptr) {
            bindStats->shadowCascadeAtlasPasses += activeCascadeCount;
            bindStats->shadowCascadeAtlasDraws += cascadeAtlasDraws;
            bindStats->shadowCascadeAtlasMeshBinds += cascadeAtlasMeshBinds;
            bindStats->bonePaletteDescriptorBinds +=
                cascadeAtlasBonePaletteDescriptorBinds;
            bindStats->bonePaletteFallbackDescriptorBinds +=
                cascadeAtlasBonePaletteFallbackDescriptorBinds;
            bindStats->pushConstantUpdates += cascadeAtlasPushConstantUpdates;
            bindStats->pushConstantBytes += cascadeAtlasPushConstantBytes;
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    if (shadowRenderPass != nullptr &&
        shadowGraphicsPipeline != nullptr &&
        localShadowFramebuffer != nullptr &&
        shadowDescriptorSets != nullptr &&
        localShadowTiles != nullptr &&
        localShadowTiles->assignedCount > 0 &&
        !skipCachedLocalShadowTiles) {
        const bool reuseCachedLocalShadowTiles =
            localShadowTiles->cacheSkippedTiles > 0 &&
            localShadowTiles->cacheSkippedTiles < localShadowTiles->assignedCount;
        VkClearValue localShadowClearValue{};
        localShadowClearValue.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo localShadowPassInfo{};
        localShadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        localShadowPassInfo.renderPass = reuseCachedLocalShadowTiles
            ? shadowRenderPass->LoadHandle()
            : shadowRenderPass->Handle();
        localShadowPassInfo.framebuffer =
            localShadowFramebuffer->Handle(imageIndex);
        localShadowPassInfo.renderArea.offset = { 0, 0 };
        localShadowPassInfo.renderArea.extent =
            localShadowFramebuffer->Extent();
        localShadowPassInfo.clearValueCount = reuseCachedLocalShadowTiles ? 0u : 1u;
        localShadowPassInfo.pClearValues =
            reuseCachedLocalShadowTiles ? nullptr : &localShadowClearValue;

        vkCmdBeginRenderPass(
            commandBuffer,
            &localShadowPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );

        const VkExtent2D localAtlasExtent = localShadowFramebuffer->Extent();
        const u32 tileColumns = std::max(localShadowTiles->tileColumns, 1u);
        const VkExtent2D tileExtent{
            localShadowTiles->tileSize > 0
                ? localShadowTiles->tileSize
                : std::max(localAtlasExtent.width / tileColumns, 1u),
            localShadowTiles->tileSize > 0
                ? localShadowTiles->tileSize
                : std::max(localAtlasExtent.height / tileColumns, 1u)
        };
        u32 localAtlasMeshBinds = 0;
        u32 localAtlasBonePaletteDescriptorBinds = 0;
        u32 localAtlasBonePaletteFallbackDescriptorBinds = 0;
        u32 localAtlasPushConstantUpdates = 0;
        u64 localAtlasPushConstantBytes = 0;
        u32 localAtlasDraws = 0;
        const u32 assignedLocalShadowTileCount = std::min<u32>(
            localShadowTiles->assignedCount,
            static_cast<u32>(localShadowTiles->tiles.size())
        );
        u32 recordedLocalShadowTileCount = 0;
        for (u32 tileSetIndex = 0; tileSetIndex < assignedLocalShadowTileCount; ++tileSetIndex) {
            const LocalShadowTile& tile = localShadowTiles->tiles[tileSetIndex];
            if (reuseCachedLocalShadowTiles && tile.cacheReusable) {
                continue;
            }

            const u32 tileX = tile.tileIndex % tileColumns;
            const u32 tileY = tile.tileIndex / tileColumns;
            const VkOffset2D tileOffset{
                static_cast<i32>(tileX * tileExtent.width),
                static_cast<i32>(tileY * tileExtent.height)
            };
            if (reuseCachedLocalShadowTiles) {
                VkClearAttachment clearAttachment{};
                clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

                VkClearRect clearRect{};
                clearRect.rect.offset = tileOffset;
                clearRect.rect.extent = {
                    std::min(tileExtent.width, localAtlasExtent.width - tileOffset.x),
                    std::min(tileExtent.height, localAtlasExtent.height - tileOffset.y)
                };
                clearRect.baseArrayLayer = 0;
                clearRect.layerCount = 1;

                vkCmdClearAttachments(
                    commandBuffer,
                    1,
                    &clearAttachment,
                    1,
                    &clearRect
                );
            }
            const std::span<const RenderCommand> tileRenderCommands =
                tileSetIndex < localShadowTileRenderCommands.size()
                    ? localShadowTileRenderCommands[tileSetIndex]
                    : shadowRenderCommands;
            DrawShadowCommands(
                commandBuffer,
                *shadowGraphicsPipeline,
                doubleSidedShadowGraphicsPipeline,
                *shadowDescriptorSets,
                tileRenderCommands,
                imageIndex,
                tile.viewProjection,
                shadowDepthBias,
                gBufferBonePaletteFallbackDescriptorSet,
                gBufferBonePaletteFallbackDescriptorReady,
                tileOffset,
                tileExtent,
                localAtlasMeshBinds,
                localAtlasBonePaletteDescriptorBinds,
                localAtlasBonePaletteFallbackDescriptorBinds,
                localAtlasPushConstantUpdates,
                localAtlasPushConstantBytes
            );
            ++recordedLocalShadowTileCount;
            localAtlasDraws += static_cast<u32>(tileRenderCommands.size());
        }

        if (bindStats != nullptr) {
            bindStats->localShadowAtlasPasses += recordedLocalShadowTileCount;
            bindStats->localShadowAtlasDraws += localAtlasDraws;
            bindStats->localShadowAtlasMeshBinds += localAtlasMeshBinds;
            bindStats->bonePaletteDescriptorBinds +=
                localAtlasBonePaletteDescriptorBinds;
            bindStats->bonePaletteFallbackDescriptorBinds +=
                localAtlasBonePaletteFallbackDescriptorBinds;
            bindStats->pushConstantUpdates += localAtlasPushConstantUpdates;
            bindStats->pushConstantBytes += localAtlasPushConstantBytes;
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    if (gpuTimer != nullptr) {
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ShadowEnd);
    }

    if (gBufferRenderPass != nullptr && gBufferFramebuffer != nullptr) {
        std::array<VkClearValue, 7> gBufferClearValues{};
        gBufferClearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        gBufferClearValues[1].color = { { 0.5f, 0.5f, 1.0f, 1.0f } };
        gBufferClearValues[2].color = { { 0.0f, 1.0f, 0.0f, 1.0f } };
        gBufferClearValues[3].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        gBufferClearValues[4].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        gBufferClearValues[5].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        gBufferClearValues[6].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo gBufferPassInfo{};
        gBufferPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        gBufferPassInfo.renderPass = gBufferRenderPass->Handle();
        gBufferPassInfo.framebuffer = gBufferFramebuffer->Handle(imageIndex);
        gBufferPassInfo.renderArea.offset = { 0, 0 };
        gBufferPassInfo.renderArea.extent = gBufferFramebuffer->Extent();
        gBufferPassInfo.clearValueCount = static_cast<u32>(gBufferClearValues.size());
        gBufferPassInfo.pClearValues = gBufferClearValues.data();

        vkCmdBeginRenderPass(
            commandBuffer,
            &gBufferPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );
        SetViewportAndScissor(commandBuffer, { 0, 0 }, gBufferFramebuffer->Extent());

        if (gBufferGraphicsPipeline != nullptr &&
            gBufferDescriptorSets != nullptr &&
            !gBufferRenderCommands.empty()) {
            DrawStateCache gBufferState{};
            const VulkanGraphicsPipeline* boundGBufferPipeline = nullptr;
            auto bindGBufferPipeline = [&](const VulkanGraphicsPipeline& pipeline) {
                if (boundGBufferPipeline == &pipeline) {
                    return;
                }

                vkCmdBindPipeline(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.Handle()
                );

                const VkDescriptorSet gBufferDescriptorSet =
                    gBufferDescriptorSets->Handle(imageIndex);
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.Layout(),
                    0,
                    1,
                    &gBufferDescriptorSet,
                    0,
                    nullptr
                );
                gBufferState = DrawStateCache{};
                boundGBufferPipeline = &pipeline;
            };
            u32 gBufferMaterialBinds = 0;
            u32 gBufferMeshBinds = 0;
            u32 gBufferBonePaletteDescriptorBinds = 0;
            u32 gBufferBonePaletteFallbackDescriptorBinds = 0;
            u32 gBufferPushConstantUpdates = 0;
            u64 gBufferPushConstantBytes = 0;
            for (const RenderCommand& renderCommand : gBufferRenderCommands) {
                const VulkanGraphicsPipeline& activeGBufferPipeline =
                    PipelineForCommand(
                        *gBufferGraphicsPipeline,
                        doubleSidedGBufferGraphicsPipeline,
                        renderCommand
                    );
                bindGBufferPipeline(activeGBufferPipeline);
                const u32 materialId = frameMaterials != nullptr
                    ? frameMaterials->IdFor(renderCommand.material)
                    : 0;
                const bool realBonePaletteDescriptorReady =
                    renderCommand.bonePaletteDescriptorSet != VK_NULL_HANDLE &&
                    renderCommand.bonePaletteDescriptorSetReady != 0u;
                if (!realBonePaletteDescriptorReady &&
                    BindBonePaletteFallbackIfNeeded(
                        commandBuffer,
                        activeGBufferPipeline,
                        gBufferBonePaletteFallbackDescriptorSet,
                        gBufferBonePaletteFallbackDescriptorReady,
                        gBufferState
                    )) {
                    ++gBufferBonePaletteFallbackDescriptorBinds;
                }

                DrawRenderCommand(
                    commandBuffer,
                    activeGBufferPipeline,
                    materialDescriptorSets,
                    renderCommand,
                    gBufferFramebuffer->Extent(),
                    imageIndex,
                    static_cast<f32>(materialId),
                    gBufferState,
                    gBufferMaterialBinds,
                    gBufferMeshBinds,
                    gBufferPushConstantUpdates,
                    gBufferPushConstantBytes,
                    0.0f,
                    &gBufferBonePaletteDescriptorBinds
                );
            }

            if (bindStats != nullptr) {
                bindStats->gBufferMaterialBinds += gBufferMaterialBinds;
                bindStats->gBufferMeshBinds += gBufferMeshBinds;
                bindStats->gBufferBonePaletteDescriptorBinds +=
                    gBufferBonePaletteDescriptorBinds;
                bindStats->bonePaletteDescriptorBinds +=
                    gBufferBonePaletteDescriptorBinds;
                bindStats->gBufferBonePaletteFallbackDescriptorBinds +=
                    gBufferBonePaletteFallbackDescriptorBinds;
                bindStats->bonePaletteFallbackDescriptorBinds +=
                    gBufferBonePaletteFallbackDescriptorBinds;
                bindStats->pushConstantUpdates += gBufferPushConstantUpdates;
                bindStats->pushConstantBytes += gBufferPushConstantBytes;
            }
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    if (forwardResidualVelocityRenderPass != nullptr &&
        forwardResidualVelocityFramebuffer != nullptr &&
        forwardResidualVelocityGraphicsPipeline != nullptr &&
        (!forwardResidualVelocityRenderCommands.empty() ||
            !weightedTranslucencyVelocityRenderCommands.empty())) {
        VkRenderPassBeginInfo velocityPassInfo{};
        velocityPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        velocityPassInfo.renderPass = forwardResidualVelocityRenderPass->Handle();
        velocityPassInfo.framebuffer =
            forwardResidualVelocityFramebuffer->Handle(imageIndex);
        velocityPassInfo.renderArea.offset = { 0, 0 };
        velocityPassInfo.renderArea.extent =
            forwardResidualVelocityFramebuffer->Extent();
        velocityPassInfo.clearValueCount = 0;
        velocityPassInfo.pClearValues = nullptr;

        vkCmdBeginRenderPass(
            commandBuffer,
            &velocityPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );
        SetViewportAndScissor(
            commandBuffer,
            { 0, 0 },
            forwardResidualVelocityFramebuffer->Extent()
        );

        u32 velocityMaterialBinds = 0;
        u32 velocityMeshBinds = 0;
        u32 velocityPushConstantUpdates = 0;
        u64 velocityPushConstantBytes = 0;
        const u32 velocityDraws = DrawForwardResidualCommands(
            commandBuffer,
            forwardResidualVelocityGraphicsPipeline,
            doubleSidedForwardResidualVelocityGraphicsPipeline,
            descriptorSets,
            materialDescriptorSets,
            frameMaterials,
            forwardResidualVelocityRenderCommands,
            forwardResidualVelocityFramebuffer->Extent(),
            imageIndex,
            velocityMaterialBinds,
            velocityMeshBinds,
            velocityPushConstantUpdates,
            velocityPushConstantBytes
        );
        if (bindStats != nullptr) {
            bindStats->forwardResidualVelocityDraws += velocityDraws;
            bindStats->forwardResidualVelocityMaterialBinds += velocityMaterialBinds;
            bindStats->forwardResidualVelocityMeshBinds += velocityMeshBinds;
            bindStats->pushConstantUpdates += velocityPushConstantUpdates;
            bindStats->pushConstantBytes += velocityPushConstantBytes;
        }

        if (!weightedTranslucencyVelocityRenderCommands.empty()) {
            u32 weightedVelocityMaterialBinds = 0;
            u32 weightedVelocityMeshBinds = 0;
            u32 weightedVelocityPushConstantUpdates = 0;
            u64 weightedVelocityPushConstantBytes = 0;
            const u32 weightedVelocityDraws = DrawForwardResidualCommands(
                commandBuffer,
                forwardResidualVelocityGraphicsPipeline,
                doubleSidedForwardResidualVelocityGraphicsPipeline,
                descriptorSets,
                materialDescriptorSets,
                frameMaterials,
                weightedTranslucencyVelocityRenderCommands,
                forwardResidualVelocityFramebuffer->Extent(),
                imageIndex,
                weightedVelocityMaterialBinds,
                weightedVelocityMeshBinds,
                weightedVelocityPushConstantUpdates,
                weightedVelocityPushConstantBytes
            );
            if (bindStats != nullptr) {
                bindStats->weightedTranslucencyVelocityDraws += weightedVelocityDraws;
                bindStats->weightedTranslucencyVelocityMaterialBinds += weightedVelocityMaterialBinds;
                bindStats->weightedTranslucencyVelocityMeshBinds += weightedVelocityMeshBinds;
                bindStats->pushConstantUpdates += weightedVelocityPushConstantUpdates;
                bindStats->pushConstantBytes += weightedVelocityPushConstantBytes;
            }
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    // SSR trace can sample completed scene-color history, so establish its
    // first-frame layout before the compute chain, not just before Deferred.
    if (deferredLightingPipeline != nullptr &&
        sceneRenderTargets != nullptr &&
        !temporalHistoryColorInitialized) {
        PrepareTemporalHistoryColorForSampling(
            commandBuffer,
            *sceneRenderTargets
        );
        temporalHistoryColorInitialized = true;
    }

    bool hizDepthPyramidBuilt = false;
    if (hizBuildPipeline != nullptr &&
        hizDescriptorSets != nullptr &&
        hizDepthPyramid != nullptr &&
        hizSourceTargets != nullptr &&
        hizDepthPyramid->MipCount() > 0) {
        VkImageMemoryBarrier sourceDepthBarrier{};
        sourceDepthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        sourceDepthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceDepthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceDepthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        sourceDepthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        sourceDepthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        sourceDepthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        sourceDepthBarrier.image = hizSourceTargets->SceneDepthImage(imageIndex);
        sourceDepthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        sourceDepthBarrier.subresourceRange.baseMipLevel = 0;
        sourceDepthBarrier.subresourceRange.levelCount = 1;
        sourceDepthBarrier.subresourceRange.baseArrayLayer = 0;
        sourceDepthBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &sourceDepthBarrier
        );

        VkImageMemoryBarrier initializeBarrier{};
        initializeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        initializeBarrier.srcAccessMask = 0;
        initializeBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        initializeBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        initializeBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        initializeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        initializeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        initializeBarrier.image = hizDepthPyramid->Image(imageIndex);
        initializeBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        initializeBarrier.subresourceRange.baseMipLevel = 0;
        initializeBarrier.subresourceRange.levelCount = hizDepthPyramid->MipCount();
        initializeBarrier.subresourceRange.baseArrayLayer = 0;
        initializeBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &initializeBarrier
        );

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            hizBuildPipeline->Handle()
        );
        for (u32 mipIndex = 0; mipIndex < hizDepthPyramid->MipCount(); ++mipIndex) {
            const VkDescriptorSet descriptorSet =
                hizDescriptorSets->Handle(imageIndex, mipIndex);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                hizBuildPipeline->Layout(),
                0,
                1,
                &descriptorSet,
                0,
                nullptr
            );
            const VkExtent2D mipExtent = hizDepthPyramid->MipExtent(mipIndex);
            vkCmdDispatch(
                commandBuffer,
                (mipExtent.width + 7u) / 8u,
                (mipExtent.height + 7u) / 8u,
                1
            );
            if (bindStats != nullptr) {
                ++bindStats->ssrHiZBuildDispatches;
                ++bindStats->ssrHiZBuildDescriptorBinds;
            }

            VkImageMemoryBarrier mipBarrier{};
            mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            mipBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipBarrier.image = hizDepthPyramid->Image(imageIndex);
            mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mipBarrier.subresourceRange.baseMipLevel = mipIndex;
            mipBarrier.subresourceRange.levelCount = 1;
            mipBarrier.subresourceRange.baseArrayLayer = 0;
            mipBarrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &mipBarrier
            );
        }
        hizDepthPyramidBuilt = true;
    }

    if (ssrTargets != nullptr &&
        (!ssrImagesInitialized || ssrHistoryReset)) {
        ClearSsrReconstructionImages(
            commandBuffer,
            *ssrTargets,
            ssrImagesInitialized
        );
    }

    const bool ffxSssrPrepareIndirectArgsReady =
        ffxSssrPrepareIndirectArgsEnabled &&
        ffxSssrConstantsResources != nullptr &&
        ffxSssrConstantsResources->Count() > imageIndex &&
        ffxSssrPrepareIndirectArgsPipeline != nullptr &&
        ffxSssrPrepareIndirectArgsResources != nullptr &&
        ffxSssrPrepareIndirectArgsResources->Count() > imageIndex;
    const bool ffxSssrClassifyTilesReady =
        ffxSssrPrepareIndirectArgsReady &&
        ffxSssrClassifyTilesPipeline != nullptr &&
        ffxSssrClassifyTilesResources != nullptr &&
        ffxSssrClassifyTilesResources->Count() > imageIndex &&
        ffxSssrClassifyTilesResources->GroupCountX() > 0u &&
        ffxSssrClassifyTilesResources->GroupCountY() > 0u;
    const bool ffxSssrBlueNoiseReady =
        ffxSssrClassifyTilesReady &&
        ffxSssrBlueNoisePipeline != nullptr &&
        ffxSssrBlueNoiseResources != nullptr &&
        ffxSssrBlueNoiseResources->Count() > imageIndex &&
        ffxSssrBlueNoiseResources->GroupCountX() > 0u &&
        ffxSssrBlueNoiseResources->GroupCountY() > 0u;
    const bool ffxSssrIntersectReady =
        ffxSssrBlueNoiseReady &&
        ffxSssrIntersectPipeline != nullptr &&
        ffxSssrIntersectResources != nullptr &&
        ffxSssrIntersectResources->Count() > imageIndex &&
        ssrTargets != nullptr &&
        ssrTargets->Count() > imageIndex &&
        hdrRenderPass != nullptr &&
        hdrFramebuffer != nullptr;
    const bool ffxSssrReprojectReady =
        ffxSssrIntersectReady &&
        ffxSssrReprojectPipeline != nullptr &&
        ffxSssrReprojectResources != nullptr &&
        ffxSssrReprojectResources->Count() > imageIndex;
    const bool ffxSssrPrefilterReady =
        ffxSssrReprojectReady &&
        ffxSssrPrefilterPipeline != nullptr &&
        ffxSssrPrefilterResources != nullptr &&
        ffxSssrPrefilterResources->Count() > imageIndex;
    const bool ffxSssrResolveTemporalReady =
        ffxSssrPrefilterReady &&
        ffxSssrResolveTemporalPipeline != nullptr &&
        ffxSssrResolveTemporalResources != nullptr &&
        ffxSssrResolveTemporalResources->Count() > imageIndex;
    bool ffxSssrResolveTemporalDispatched = false;
    if (ffxSssrPrepareIndirectArgsReady) {
        const VkBuffer rayCounterBuffer =
            ffxSssrPrepareIndirectArgsResources->RayCounterBuffer(imageIndex);
        const VkBuffer indirectArgsBuffer =
            ffxSssrPrepareIndirectArgsResources->IndirectArgsBuffer(imageIndex);
        const VkDescriptorSet ffxConstantsDescriptorSet =
            ffxSssrConstantsResources->Handle(imageIndex);
        vkCmdFillBuffer(
            commandBuffer,
            rayCounterBuffer,
            0,
            ffxSssrPrepareIndirectArgsResources->RayCounterBufferSize(),
            0u
        );
        vkCmdFillBuffer(
            commandBuffer,
            indirectArgsBuffer,
            0,
            ffxSssrPrepareIndirectArgsResources->IndirectArgsBufferSize(),
            0u
        );
        if (ffxSssrClassifyTilesReady) {
            vkCmdFillBuffer(
                commandBuffer,
                ffxSssrClassifyTilesResources->RayListBuffer(imageIndex),
                0,
                ffxSssrClassifyTilesResources->RayListBufferSize(),
                0u
            );
            vkCmdFillBuffer(
                commandBuffer,
                ffxSssrClassifyTilesResources->DenoiserTileListBuffer(imageIndex),
                0,
                ffxSssrClassifyTilesResources->DenoiserTileListBufferSize(),
                0u
            );
        }

        std::array<VkBufferMemoryBarrier, 4> transferToCompute{};
        u32 transferToComputeCount = 0u;
        auto addTransferToComputeBarrier =
            [&](VkBuffer buffer, VkDeviceSize size) {
                VkBufferMemoryBarrier& barrier =
                    transferToCompute[transferToComputeCount++];
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask =
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = buffer;
                barrier.offset = 0;
                barrier.size = size;
            };
        addTransferToComputeBarrier(
            rayCounterBuffer,
            ffxSssrPrepareIndirectArgsResources->RayCounterBufferSize()
        );
        addTransferToComputeBarrier(
            indirectArgsBuffer,
            ffxSssrPrepareIndirectArgsResources->IndirectArgsBufferSize()
        );
        if (ffxSssrClassifyTilesReady) {
            addTransferToComputeBarrier(
                ffxSssrClassifyTilesResources->RayListBuffer(imageIndex),
                ffxSssrClassifyTilesResources->RayListBufferSize()
            );
            addTransferToComputeBarrier(
                ffxSssrClassifyTilesResources->DenoiserTileListBuffer(imageIndex),
                ffxSssrClassifyTilesResources->DenoiserTileListBufferSize()
            );
        }
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            transferToComputeCount,
            transferToCompute.data(),
            0,
            nullptr
        );

        if (ffxSssrClassifyTilesReady) {
            BarrierSsrComputeImage(
                commandBuffer,
                ffxSssrClassifyTilesResources->IntersectionOutputImage(imageIndex),
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            );
            BarrierSsrComputeImage(
                commandBuffer,
                ffxSssrClassifyTilesResources->ExtractedRoughnessImage(imageIndex),
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            );
            BarrierSsrComputeImage(
                commandBuffer,
                ffxSssrClassifyTilesResources->HitConfidenceImage(imageIndex),
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            );

            const VkDescriptorSet classifyDescriptorSet =
                ffxSssrClassifyTilesResources->Handle(imageIndex);
            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                ffxSssrClassifyTilesPipeline->Handle()
            );
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                ffxSssrClassifyTilesPipeline->Layout(),
                0,
                1,
                &ffxConstantsDescriptorSet,
                0,
                nullptr
            );
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                ffxSssrClassifyTilesPipeline->Layout(),
                1,
                1,
                &classifyDescriptorSet,
                0,
                nullptr
            );
            vkCmdDispatch(
                commandBuffer,
                ffxSssrClassifyTilesResources->GroupCountX(),
                ffxSssrClassifyTilesResources->GroupCountY(),
                1u
            );

            VkBufferMemoryBarrier classifyToPrepare{};
            classifyToPrepare.sType =
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            classifyToPrepare.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            classifyToPrepare.dstAccessMask =
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            classifyToPrepare.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            classifyToPrepare.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            classifyToPrepare.buffer = rayCounterBuffer;
            classifyToPrepare.offset = 0;
            classifyToPrepare.size =
                ffxSssrPrepareIndirectArgsResources->RayCounterBufferSize();
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0,
                nullptr,
                1u,
                &classifyToPrepare,
                0,
                nullptr
            );

            if (bindStats != nullptr) {
                ++bindStats->ffxSssrClassifyTilesDispatches;
                bindStats->ffxSssrClassifyTilesDescriptorBinds += 2u;
                bindStats->ffxSssrClassifyTilesGroupCountX =
                    ffxSssrClassifyTilesResources->GroupCountX();
                bindStats->ffxSssrClassifyTilesGroupCountY =
                    ffxSssrClassifyTilesResources->GroupCountY();
            }
        }

        const VkDescriptorSet ffxDescriptorSet =
            ffxSssrPrepareIndirectArgsResources->Handle(imageIndex);
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            ffxSssrPrepareIndirectArgsPipeline->Handle()
        );
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            ffxSssrPrepareIndirectArgsPipeline->Layout(),
            0,
            1,
            &ffxConstantsDescriptorSet,
            0,
            nullptr
        );
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            ffxSssrPrepareIndirectArgsPipeline->Layout(),
            1,
            1,
            &ffxDescriptorSet,
            0,
            nullptr
        );
        vkCmdDispatch(commandBuffer, 1u, 1u, 1u);

        std::array<VkBufferMemoryBarrier, 2> computeToConsumer{};
        computeToConsumer[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        computeToConsumer[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        computeToConsumer[0].dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT |
            VK_ACCESS_SHADER_WRITE_BIT |
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        computeToConsumer[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeToConsumer[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeToConsumer[0].buffer = rayCounterBuffer;
        computeToConsumer[0].offset = 0;
        computeToConsumer[0].size =
            ffxSssrPrepareIndirectArgsResources->RayCounterBufferSize();
        computeToConsumer[1] = computeToConsumer[0];
        computeToConsumer[1].buffer = indirectArgsBuffer;
        computeToConsumer[1].size =
            ffxSssrPrepareIndirectArgsResources->IndirectArgsBufferSize();
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            0,
            0,
            nullptr,
            static_cast<u32>(computeToConsumer.size()),
            computeToConsumer.data(),
            0,
            nullptr
        );

        if (bindStats != nullptr) {
            ++bindStats->ffxSssrPrepareIndirectArgsDispatches;
            bindStats->ffxSssrPrepareIndirectArgsDescriptorBinds += 2u;
        }
    }

    const bool ssrReconstructionReady =
        ssrReconstructionEnabled &&
        deferredLightingFrameDescriptorSets != nullptr &&
        ssrTracePipeline != nullptr &&
        ssrTemporalPipeline != nullptr &&
        ssrSpatialPipeline != nullptr &&
        ssrDescriptorSets != nullptr &&
        ssrTargets != nullptr &&
        ssrDescriptorSets->Count() == ssrTargets->Count() &&
        ssrTargets->Count() > 1u;
    if (ssrReconstructionReady) {
        const VkExtent2D ssrExtent = ssrTargets->Extent();
        const u32 groupCountX = (ssrExtent.width + 7u) / 8u;
        const u32 groupCountY = (ssrExtent.height + 7u) / 8u;
        const VkDescriptorSet frameDescriptorSet =
            deferredLightingFrameDescriptorSets->Handle(imageIndex);
        const VkDescriptorSet reconstructionDescriptorSet =
            ssrDescriptorSets->Handle(imageIndex);

        auto bindSsrPipeline = [&](const VulkanComputePipeline& pipeline) {
            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline.Handle()
            );
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline.Layout(),
                0,
                1,
                &frameDescriptorSet,
                0,
                nullptr
            );
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline.Layout(),
                1,
                1,
                &reconstructionDescriptorSet,
                0,
                nullptr
            );
        };

        BarrierSsrComputeImage(
            commandBuffer,
            ssrTargets->SsrRawImage(imageIndex),
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );
        bindSsrPipeline(*ssrTracePipeline);
        vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
        if (bindStats != nullptr) {
            ++bindStats->ssrReconstructionTraceDispatches;
        }
        BarrierSsrComputeImage(
            commandBuffer,
            ssrTargets->SsrRawImage(imageIndex),
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

    }

    if (lightTileCullComputePipeline != nullptr &&
        lightTileCullDescriptorSets != nullptr &&
        lightTileCullGroupCountX > 0 &&
        lightTileCullGroupCountY > 0) {
        const bool useClustered = lightClusterCullComputePipeline != nullptr;
        const VulkanComputePipeline& cullPipeline = useClustered
            ? *lightClusterCullComputePipeline : *lightTileCullComputePipeline;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline.Handle());

        const VkDescriptorSet frameDescriptorSet =
            lightTileCullDescriptorSets->Handle(imageIndex);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            cullPipeline.Layout(), 0, 1, &frameDescriptorSet, 0, nullptr);

        vkCmdDispatch(commandBuffer,
            lightTileCullGroupCountX,
            lightTileCullGroupCountY,
            lightTileCullGroupCountZ);
        BarrierComputeLightTilesForFragmentRead(commandBuffer);
        if (frameGraph != nullptr) {
            RecordRenderFrameGraphBarrierExecution(
                *frameGraph,
                RenderFrameGraphBarrierBridge::LightTileCullFragmentRead
            );
        }

        if (bindStats != nullptr) {
            ++bindStats->lightTileCullComputeDispatches;
            ++bindStats->lightTileCullComputeFrameBinds;
            bindStats->lightTileCullComputeGroupsX = lightTileCullGroupCountX;
            bindStats->lightTileCullComputeGroupsY = lightTileCullGroupCountY;
        }
    }

    if (weightedTranslucencyRenderPass != nullptr &&
        weightedTranslucencyFramebuffer != nullptr) {
        std::array<VkClearValue, 2> translucencyClearValues{};
        translucencyClearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        translucencyClearValues[1].color = { { 1.0f, 0.0f, 0.0f, 0.0f } };

        VkRenderPassBeginInfo translucencyPassInfo{};
        translucencyPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        translucencyPassInfo.renderPass = weightedTranslucencyRenderPass->Handle();
        translucencyPassInfo.framebuffer =
            weightedTranslucencyFramebuffer->Handle(imageIndex);
        translucencyPassInfo.renderArea.offset = { 0, 0 };
        translucencyPassInfo.renderArea.extent =
            weightedTranslucencyFramebuffer->Extent();
        translucencyPassInfo.clearValueCount =
            static_cast<u32>(translucencyClearValues.size());
        translucencyPassInfo.pClearValues = translucencyClearValues.data();

        vkCmdBeginRenderPass(
            commandBuffer,
            &translucencyPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );
        SetViewportAndScissor(
            commandBuffer,
            { 0, 0 },
            weightedTranslucencyFramebuffer->Extent()
        );

        if (weightedTranslucencyGraphicsPipeline != nullptr &&
            !weightedTranslucencyRenderCommands.empty()) {
            u32 translucencyMaterialBinds = 0;
            u32 translucencyMeshBinds = 0;
            u32 translucencyPushConstantUpdates = 0;
            u64 translucencyPushConstantBytes = 0;
            DrawStateCache translucencyState{};
            DrawForwardCommands(
                commandBuffer,
                *weightedTranslucencyGraphicsPipeline,
                doubleSidedWeightedTranslucencyGraphicsPipeline,
                descriptorSets,
                materialDescriptorSets,
                frameMaterials,
                weightedTranslucencyRenderCommands,
                weightedTranslucencyFramebuffer->Extent(),
                imageIndex,
                translucencyState,
                translucencyMaterialBinds,
                translucencyMeshBinds,
                translucencyPushConstantUpdates,
                translucencyPushConstantBytes
            );

            if (bindStats != nullptr) {
                const u32 translucencyDrawCount =
                    static_cast<u32>(weightedTranslucencyRenderCommands.size());
                bindStats->weightedTranslucencyDraws += translucencyDrawCount;
                bindStats->weightedTranslucencySharedLightListDraws +=
                    lightTileCullComputePipeline != nullptr ? translucencyDrawCount : 0u;
                bindStats->weightedTranslucencyShadowReadyDraws +=
                    ((directionalShadowCascades != nullptr &&
                        directionalShadowCascades->activeCount > 0) ||
                        (localShadowTiles != nullptr && localShadowTiles->assignedCount > 0))
                            ? translucencyDrawCount
                            : 0u;
                bindStats->weightedTranslucencyMaterialBinds += translucencyMaterialBinds;
                bindStats->weightedTranslucencyMeshBinds += translucencyMeshBinds;
                bindStats->pushConstantUpdates += translucencyPushConstantUpdates;
                bindStats->pushConstantBytes += translucencyPushConstantBytes;
            }
        }
        vkCmdEndRenderPass(commandBuffer);

        if (bindStats != nullptr) {
            ++bindStats->weightedTranslucencyClearPasses;
        }
    }

    if (deferredLightingPipeline != nullptr &&
        sceneRenderTargets != nullptr &&
        !temporalHistoryColorInitialized) {
        PrepareTemporalHistoryColorForSampling(commandBuffer, *sceneRenderTargets);
        temporalHistoryColorInitialized = true;
    }

    bool forwardResidualDrawnInHdr = false;
    if (hdrRenderPass != nullptr && hdrFramebuffer != nullptr) {
        std::array<VkClearValue, 2> hdrClearValues{};
        hdrClearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        hdrClearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo hdrPassInfo{};
        hdrPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        hdrPassInfo.renderPass = hdrRenderPass->Handle();
        hdrPassInfo.framebuffer = hdrFramebuffer->Handle(imageIndex);
        hdrPassInfo.renderArea.offset = { 0, 0 };
        hdrPassInfo.renderArea.extent = hdrFramebuffer->Extent();
        hdrPassInfo.clearValueCount = static_cast<u32>(hdrClearValues.size());
        hdrPassInfo.pClearValues = hdrClearValues.data();

        vkCmdBeginRenderPass(
            commandBuffer,
            &hdrPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );

        if (deferredLightingPipeline != nullptr &&
            deferredLightingFrameDescriptorSets != nullptr &&
            deferredLightingGBufferDescriptorSets != nullptr) {
            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                deferredLightingPipeline->Handle()
            );

            const VkDescriptorSet frameDescriptorSet =
                deferredLightingFrameDescriptorSets->Handle(imageIndex);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                deferredLightingPipeline->Layout(),
                0,
                1,
                &frameDescriptorSet,
                0,
                nullptr
            );

            const VkDescriptorSet gBufferDescriptorSet =
                deferredLightingGBufferDescriptorSets->Handle(imageIndex);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                deferredLightingPipeline->Layout(),
                1,
                1,
                &gBufferDescriptorSet,
                0,
                nullptr
            );

            SetViewportAndScissor(commandBuffer, { 0, 0 }, hdrFramebuffer->Extent());

            const ObjectPushConstants lightingConstants =
                DeferredLightingPushConstants(gBufferRenderCommands);
            ObjectPushConstants deferredConstants = lightingConstants;
            deferredConstants.materialControls.w = static_cast<f32>(deferredPbrDebugView);
            u32 deferredPushConstantUpdates = 0;
            u64 deferredPushConstantBytes = 0;
            PushConstants(
                commandBuffer,
                *deferredLightingPipeline,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(ObjectPushConstants),
                &deferredConstants,
                deferredPushConstantUpdates,
                deferredPushConstantBytes
            );
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);

            if (bindStats != nullptr) {
                ++bindStats->deferredLightingDraws;
                ++bindStats->deferredLightingFrameBinds;
                ++bindStats->deferredLightingGBufferBinds;
                if (hizDepthPyramidBuilt) {
                    ++bindStats->ssrHiZConsumerDraws;
                }
                if (deferredPbrDebugView > 0) {
                    ++bindStats->deferredPbrDebugDraws;
                    ++bindStats->deferredPbrDebugFrameBinds;
                    ++bindStats->deferredPbrDebugGBufferBinds;
                }
                if (deferredPbrDebugView == 7) {
                    ++bindStats->localShadowAtlasDebugDraws;
                    ++bindStats->localShadowAtlasDebugFrameBinds;
                    ++bindStats->localShadowAtlasDebugTextureBinds;
                }
                if (deferredPbrDebugView == 8) {
                    ++bindStats->localShadowVisibilityDebugDraws;
                    ++bindStats->localShadowVisibilityDebugFrameBinds;
                    ++bindStats->localShadowVisibilityDebugTextureBinds;
                }
                if (deferredPbrDebugView == 9) {
                    ++bindStats->contactShadowDebugDraws;
                    ++bindStats->contactShadowDebugFrameBinds;
                    ++bindStats->contactShadowDebugGBufferBinds;
                }
                if (deferredPbrDebugView == 10) {
                    ++bindStats->localShadowFaceDebugDraws;
                    ++bindStats->localShadowFaceDebugFrameBinds;
                    ++bindStats->localShadowFaceDebugTextureBinds;
                }
                if (deferredPbrDebugView == 11) {
                    ++bindStats->ssaoDebugDraws;
                    ++bindStats->ssaoDebugFrameBinds;
                    ++bindStats->ssaoDebugGBufferBinds;
                }
                if (deferredPbrDebugView == 12) {
                    ++bindStats->ssrDebugDraws;
                    ++bindStats->ssrDebugFrameBinds;
                    ++bindStats->ssrDebugGBufferBinds;
                }
                if (deferredPbrDebugView == 13) {
                    ++bindStats->reflectionProbeDebugDraws;
                    ++bindStats->reflectionProbeDebugFrameBinds;
                    ++bindStats->reflectionProbeDebugGBufferBinds;
                }
                if (deferredPbrDebugView == 14) {
                    ++bindStats->heightFogDebugDraws;
                    ++bindStats->heightFogDebugFrameBinds;
                    ++bindStats->heightFogDebugGBufferBinds;
                }
                if (deferredPbrDebugView == 15) {
                    ++bindStats->probeGridDebugDraws;
                    ++bindStats->probeGridDebugFrameBinds;
                    ++bindStats->probeGridDebugGBufferBinds;
                }
                if (deferredPbrDebugView == 16) {
                    ++bindStats->probeGridCellDebugDraws;
                    ++bindStats->probeGridCellDebugFrameBinds;
                    ++bindStats->probeGridCellDebugGBufferBinds;
                }
                bindStats->pushConstantUpdates += deferredPushConstantUpdates;
                bindStats->pushConstantBytes += deferredPushConstantBytes;
            }
        }

        if (weightedTranslucencyResolvePipeline != nullptr &&
            weightedTranslucencyDescriptorSets != nullptr) {
            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                weightedTranslucencyResolvePipeline->Handle()
            );

            const VkDescriptorSet frameDescriptorSet = descriptorSets.Handle(imageIndex);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                weightedTranslucencyResolvePipeline->Layout(),
                0,
                1,
                &frameDescriptorSet,
                0,
                nullptr
            );

            const VkDescriptorSet translucencyDescriptorSet =
                weightedTranslucencyDescriptorSets->Handle(imageIndex);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                weightedTranslucencyResolvePipeline->Layout(),
                1,
                1,
                &translucencyDescriptorSet,
                0,
                nullptr
            );

            SetViewportAndScissor(commandBuffer, { 0, 0 }, hdrFramebuffer->Extent());

            ObjectPushConstants resolveConstants =
                DeferredLightingPushConstants(gBufferRenderCommands);
            resolveConstants.materialControls = glm::vec4(
                0.0f,
                0.0f,
                0.0f,
                static_cast<f32>(std::max(weightedTranslucencyDebugView, 0))
            );
            u32 resolvePushConstantUpdates = 0;
            u64 resolvePushConstantBytes = 0;
            PushConstants(
                commandBuffer,
                *weightedTranslucencyResolvePipeline,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(ObjectPushConstants),
                &resolveConstants,
                resolvePushConstantUpdates,
                resolvePushConstantBytes
            );
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
            if (bindStats != nullptr) {
                ++bindStats->weightedTranslucencyResolveDraws;
                ++bindStats->weightedTranslucencyResolveFrameBinds;
                ++bindStats->weightedTranslucencyResolveTextureBinds;
                if (weightedTranslucencyDebugView > 0) {
                    ++bindStats->weightedTranslucencyDebugDraws;
                    ++bindStats->weightedTranslucencyDebugFrameBinds;
                    ++bindStats->weightedTranslucencyDebugTextureBinds;
                }
                bindStats->pushConstantUpdates += resolvePushConstantUpdates;
                bindStats->pushConstantBytes += resolvePushConstantBytes;
            }
        }

        if (forwardResidualHdrGraphicsPipeline != nullptr &&
            !forwardResidualRenderCommands.empty()) {
            SetViewportAndScissor(commandBuffer, { 0, 0 }, hdrFramebuffer->Extent());

            u32 residualMaterialBinds = 0;
            u32 residualMeshBinds = 0;
            u32 residualPushConstantUpdates = 0;
            u64 residualPushConstantBytes = 0;
            const u32 residualDraws = DrawForwardResidualCommands(
                commandBuffer,
                forwardResidualHdrGraphicsPipeline,
                doubleSidedForwardResidualHdrGraphicsPipeline,
                descriptorSets,
                materialDescriptorSets,
                frameMaterials,
                forwardResidualRenderCommands,
                hdrFramebuffer->Extent(),
                imageIndex,
                residualMaterialBinds,
                residualMeshBinds,
                residualPushConstantUpdates,
                residualPushConstantBytes,
                1.0f
            );
            forwardResidualDrawnInHdr = residualDraws > 0;
            if (bindStats != nullptr) {
                bindStats->forwardResidualDraws += residualDraws;
                if (residualDraws > 0) {
                    ++bindStats->forwardResidualFrameBinds;
                    bindStats->forwardResidualSharedLightListDraws += residualDraws;
                }
                bindStats->forwardResidualMaterialBinds += residualMaterialBinds;
                bindStats->forwardResidualMeshBinds += residualMeshBinds;
                bindStats->pushConstantUpdates += residualPushConstantUpdates;
                bindStats->pushConstantBytes += residualPushConstantBytes;
            }
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    if (ffxSssrIntersectReady) {
        const VkDescriptorSet ffxConstantsDescriptorSet =
            ffxSssrConstantsResources->Handle(imageIndex);

        TransitionColorImage(
            commandBuffer,
            ssrTargets->HdrSceneColorImage(imageIndex),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

        BarrierSsrComputeImage(
            commandBuffer,
            ffxSssrBlueNoiseResources->BlueNoiseImage(imageIndex),
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

        const VkDescriptorSet blueNoiseDescriptorSet =
            ffxSssrBlueNoiseResources->Handle(imageIndex);
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            ffxSssrBlueNoisePipeline->Handle()
        );
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            ffxSssrBlueNoisePipeline->Layout(),
            0,
            1,
            &ffxConstantsDescriptorSet,
            0,
            nullptr
        );
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            ffxSssrBlueNoisePipeline->Layout(),
            1,
            1,
            &blueNoiseDescriptorSet,
            0,
            nullptr
        );
        vkCmdDispatch(
            commandBuffer,
            ffxSssrBlueNoiseResources->GroupCountX(),
            ffxSssrBlueNoiseResources->GroupCountY(),
            1u
        );

        if (bindStats != nullptr) {
            ++bindStats->ffxSssrBlueNoiseDispatches;
            bindStats->ffxSssrBlueNoiseDescriptorBinds += 2u;
            bindStats->ffxSssrBlueNoiseGroupCountX =
                ffxSssrBlueNoiseResources->GroupCountX();
            bindStats->ffxSssrBlueNoiseGroupCountY =
                ffxSssrBlueNoiseResources->GroupCountY();
        }

        VkImageMemoryBarrier blueNoiseToIntersect{};
        blueNoiseToIntersect.sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        blueNoiseToIntersect.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        blueNoiseToIntersect.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        blueNoiseToIntersect.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        blueNoiseToIntersect.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        blueNoiseToIntersect.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        blueNoiseToIntersect.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        blueNoiseToIntersect.image =
            ffxSssrBlueNoiseResources->BlueNoiseImage(imageIndex);
        blueNoiseToIntersect.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        blueNoiseToIntersect.subresourceRange.baseMipLevel = 0;
        blueNoiseToIntersect.subresourceRange.levelCount = 1;
        blueNoiseToIntersect.subresourceRange.baseArrayLayer = 0;
        blueNoiseToIntersect.subresourceRange.layerCount = 1;

        VkImageMemoryBarrier roughnessToIntersect{};
        roughnessToIntersect.sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        roughnessToIntersect.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        roughnessToIntersect.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        roughnessToIntersect.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        roughnessToIntersect.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        roughnessToIntersect.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        roughnessToIntersect.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        roughnessToIntersect.image =
            ffxSssrClassifyTilesResources->ExtractedRoughnessImage(imageIndex);
        roughnessToIntersect.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        roughnessToIntersect.subresourceRange.baseMipLevel = 0;
        roughnessToIntersect.subresourceRange.levelCount = 1;
        roughnessToIntersect.subresourceRange.baseArrayLayer = 0;
        roughnessToIntersect.subresourceRange.layerCount = 1;

        VkImageMemoryBarrier intersectionForWrite = roughnessToIntersect;
        intersectionForWrite.srcAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        intersectionForWrite.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        intersectionForWrite.image =
            ffxSssrClassifyTilesResources->IntersectionOutputImage(imageIndex);

        VkImageMemoryBarrier hitConfidenceForWrite = intersectionForWrite;
        hitConfidenceForWrite.image =
            ffxSssrClassifyTilesResources->HitConfidenceImage(imageIndex);
        std::array<VkImageMemoryBarrier, 4> imageBarriers{
            blueNoiseToIntersect,
            roughnessToIntersect,
            intersectionForWrite,
            hitConfidenceForWrite
        };
        VkBufferMemoryBarrier rayListToIntersect{};
        rayListToIntersect.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        rayListToIntersect.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        rayListToIntersect.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        rayListToIntersect.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rayListToIntersect.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        rayListToIntersect.buffer =
            ffxSssrClassifyTilesResources->RayListBuffer(imageIndex);
        rayListToIntersect.offset = 0;
        rayListToIntersect.size =
            ffxSssrClassifyTilesResources->RayListBufferSize();

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            0,
            0,
            nullptr,
            1u,
            &rayListToIntersect,
            static_cast<u32>(imageBarriers.size()),
            imageBarriers.data()
        );

        const VkDescriptorSet intersectDescriptorSet =
            ffxSssrIntersectResources->Handle(imageIndex);
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            ffxSssrIntersectPipeline->Handle()
        );
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            ffxSssrIntersectPipeline->Layout(),
            0,
            1,
            &ffxConstantsDescriptorSet,
            0,
            nullptr
        );
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            ffxSssrIntersectPipeline->Layout(),
            1,
            1,
            &intersectDescriptorSet,
            0,
            nullptr
        );
        vkCmdDispatchIndirect(
            commandBuffer,
            ffxSssrPrepareIndirectArgsResources->IndirectArgsBuffer(imageIndex),
            0
        );

        if (bindStats != nullptr) {
            ++bindStats->ffxSssrIntersectDispatches;
            bindStats->ffxSssrIntersectDescriptorBinds += 2u;
        }

        if (ffxSssrReprojectReady) {
            std::array<VkImageMemoryBarrier, 7> reprojectImageBarriers{};
            auto setReprojectImageBarrier = [&](
                std::size_t barrierIndex,
                VkImage image,
                VkAccessFlags sourceAccess,
                VkAccessFlags destinationAccess
            ) {
                VkImageMemoryBarrier& barrier =
                    reprojectImageBarriers[barrierIndex];
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcAccessMask = sourceAccess;
                barrier.dstAccessMask = destinationAccess;
                barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = image;
                barrier.subresourceRange.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0u;
                barrier.subresourceRange.levelCount = 1u;
                barrier.subresourceRange.baseArrayLayer = 0u;
                barrier.subresourceRange.layerCount = 1u;
            };
            setReprojectImageBarrier(
                0u,
                ffxSssrClassifyTilesResources->IntersectionOutputImage(
                    imageIndex
                ),
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT
            );
            setReprojectImageBarrier(
                1u,
                ffxSssrReprojectResources->ReprojectedRadianceImage(imageIndex),
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_WRITE_BIT
            );
            setReprojectImageBarrier(
                2u,
                ffxSssrReprojectResources->AverageRadianceImage(imageIndex),
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_WRITE_BIT
            );
            setReprojectImageBarrier(
                3u,
                ffxSssrReprojectResources->VarianceImage(imageIndex),
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_WRITE_BIT
            );
            setReprojectImageBarrier(
                4u,
                ffxSssrReprojectResources->SampleCountImage(imageIndex),
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_WRITE_BIT
            );
            setReprojectImageBarrier(
                5u,
                ffxSssrClassifyTilesResources->HitConfidenceImage(imageIndex),
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT
            );
            setReprojectImageBarrier(
                6u,
                ffxSssrReprojectResources->HitConfidenceImage(imageIndex),
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_WRITE_BIT
            );

            VkBufferMemoryBarrier denoiserTilesToReproject{};
            denoiserTilesToReproject.sType =
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            denoiserTilesToReproject.srcAccessMask =
                VK_ACCESS_SHADER_WRITE_BIT;
            denoiserTilesToReproject.dstAccessMask =
                VK_ACCESS_SHADER_READ_BIT;
            denoiserTilesToReproject.srcQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            denoiserTilesToReproject.dstQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            denoiserTilesToReproject.buffer =
                ffxSssrClassifyTilesResources->DenoiserTileListBuffer(imageIndex);
            denoiserTilesToReproject.offset = 0u;
            denoiserTilesToReproject.size =
                ffxSssrClassifyTilesResources->DenoiserTileListBufferSize();

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                0,
                0,
                nullptr,
                1u,
                &denoiserTilesToReproject,
                static_cast<u32>(reprojectImageBarriers.size()),
                reprojectImageBarriers.data()
            );

            const VkDescriptorSet reprojectDescriptorSet =
                ffxSssrReprojectResources->Handle(imageIndex);
            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                ffxSssrReprojectPipeline->Handle()
            );
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                ffxSssrReprojectPipeline->Layout(),
                0,
                1,
                &ffxConstantsDescriptorSet,
                0,
                nullptr
            );
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                ffxSssrReprojectPipeline->Layout(),
                1,
                1,
                &reprojectDescriptorSet,
                0,
                nullptr
            );
            constexpr VkDeviceSize kFfxSssrDenoiserIndirectArgsOffset =
                sizeof(u32) * 3u;
            vkCmdDispatchIndirect(
                commandBuffer,
                ffxSssrPrepareIndirectArgsResources->IndirectArgsBuffer(
                    imageIndex
                ),
                kFfxSssrDenoiserIndirectArgsOffset
            );

            if (bindStats != nullptr) {
                ++bindStats->ffxSssrReprojectDispatches;
                bindStats->ffxSssrReprojectDescriptorBinds += 2u;
            }

            if (ffxSssrPrefilterReady) {
                std::array<VkImageMemoryBarrier, 6> prefilterImageBarriers{};
                auto setPrefilterImageBarrier = [&](
                    std::size_t barrierIndex,
                    VkImage image,
                    VkAccessFlags sourceAccess,
                    VkAccessFlags destinationAccess
                ) {
                    VkImageMemoryBarrier& barrier =
                        prefilterImageBarriers[barrierIndex];
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.srcAccessMask = sourceAccess;
                    barrier.dstAccessMask = destinationAccess;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = image;
                    barrier.subresourceRange.aspectMask =
                        VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier.subresourceRange.baseMipLevel = 0u;
                    barrier.subresourceRange.levelCount = 1u;
                    barrier.subresourceRange.baseArrayLayer = 0u;
                    barrier.subresourceRange.layerCount = 1u;
                };
                setPrefilterImageBarrier(
                    0u,
                    ffxSssrReprojectResources->AverageRadianceImage(
                        imageIndex
                    ),
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT
                );
                setPrefilterImageBarrier(
                    1u,
                    ffxSssrReprojectResources->VarianceImage(imageIndex),
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT
                );
                setPrefilterImageBarrier(
                    2u,
                    ffxSssrReprojectResources->SampleCountImage(imageIndex),
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT
                );
                setPrefilterImageBarrier(
                    3u,
                    ffxSssrPrefilterResources->RadianceImage(imageIndex),
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT
                );
                setPrefilterImageBarrier(
                    4u,
                    ffxSssrPrefilterResources->VarianceImage(imageIndex),
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT
                );
                setPrefilterImageBarrier(
                    5u,
                    ffxSssrPrefilterResources->SampleCountImage(imageIndex),
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT
                );
                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                    0,
                    0,
                    nullptr,
                    0,
                    nullptr,
                    static_cast<u32>(prefilterImageBarriers.size()),
                    prefilterImageBarriers.data()
                );

                const VkDescriptorSet prefilterDescriptorSet =
                    ffxSssrPrefilterResources->Handle(imageIndex);
                vkCmdBindPipeline(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    ffxSssrPrefilterPipeline->Handle()
                );
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    ffxSssrPrefilterPipeline->Layout(),
                    0,
                    1,
                    &ffxConstantsDescriptorSet,
                    0,
                    nullptr
                );
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    ffxSssrPrefilterPipeline->Layout(),
                    1,
                    1,
                    &prefilterDescriptorSet,
                    0,
                    nullptr
                );
                vkCmdDispatchIndirect(
                    commandBuffer,
                    ffxSssrPrepareIndirectArgsResources->IndirectArgsBuffer(
                        imageIndex
                    ),
                    kFfxSssrDenoiserIndirectArgsOffset
                );

                if (bindStats != nullptr) {
                    ++bindStats->ffxSssrPrefilterDispatches;
                    bindStats->ffxSssrPrefilterDescriptorBinds += 2u;
                }

                if (ffxSssrResolveTemporalReady) {
                    if (ffxSssrVisibleOutputClearEnabled) {
                        ClearFfxSssrVisibleOutput(
                            commandBuffer,
                            *ffxSssrReprojectResources,
                            imageIndex
                        );
                        if (bindStats != nullptr) {
                            ++bindStats->ffxSssrVisibleOutputClears;
                        }
                    }
                    u32 historyCopies =
                        CopyFfxSssrCurrentDenoiserStateToHistory(
                            commandBuffer,
                            *ffxSssrReprojectResources,
                            *ffxSssrPrefilterResources,
                            imageIndex
                        );

                    std::array<VkImageMemoryBarrier, 9> resolveImageBarriers{};
                    auto setResolveImageBarrier = [&](
                        std::size_t barrierIndex,
                        VkImage image,
                        VkAccessFlags sourceAccess,
                        VkAccessFlags destinationAccess
                    ) {
                        VkImageMemoryBarrier& barrier =
                            resolveImageBarriers[barrierIndex];
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.srcAccessMask = sourceAccess;
                        barrier.dstAccessMask = destinationAccess;
                        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                        barrier.srcQueueFamilyIndex =
                            VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex =
                            VK_QUEUE_FAMILY_IGNORED;
                        barrier.image = image;
                        barrier.subresourceRange.aspectMask =
                            VK_IMAGE_ASPECT_COLOR_BIT;
                        barrier.subresourceRange.baseMipLevel = 0u;
                        barrier.subresourceRange.levelCount = 1u;
                        barrier.subresourceRange.baseArrayLayer = 0u;
                        barrier.subresourceRange.layerCount = 1u;
                    };
                    setResolveImageBarrier(
                        0u,
                        ffxSssrClassifyTilesResources
                            ->ExtractedRoughnessImage(imageIndex),
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT
                    );
                    setResolveImageBarrier(
                        1u,
                        ffxSssrReprojectResources->AverageRadianceImage(
                            imageIndex
                        ),
                        VK_ACCESS_SHADER_WRITE_BIT |
                            VK_ACCESS_TRANSFER_READ_BIT,
                        VK_ACCESS_SHADER_READ_BIT
                    );
                    setResolveImageBarrier(
                        2u,
                        ffxSssrPrefilterResources->RadianceImage(imageIndex),
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT
                    );
                    setResolveImageBarrier(
                        3u,
                        ffxSssrReprojectResources
                            ->ReprojectedRadianceImage(imageIndex),
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT
                    );
                    setResolveImageBarrier(
                        4u,
                        ffxSssrPrefilterResources->VarianceImage(imageIndex),
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT
                    );
                    setResolveImageBarrier(
                        5u,
                        ffxSssrPrefilterResources->SampleCountImage(
                            imageIndex
                        ),
                        VK_ACCESS_SHADER_WRITE_BIT |
                            VK_ACCESS_TRANSFER_READ_BIT,
                        VK_ACCESS_SHADER_READ_BIT
                    );
                    setResolveImageBarrier(
                        6u,
                        ffxSssrReprojectResources->RadianceHistoryImage(
                            imageIndex
                        ),
                        VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_SHADER_WRITE_BIT |
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT
                    );
                    setResolveImageBarrier(
                        7u,
                        ffxSssrReprojectResources->VarianceHistoryImage(
                            imageIndex
                        ),
                        VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_SHADER_WRITE_BIT |
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT
                    );
                    setResolveImageBarrier(
                        8u,
                        ffxSssrReprojectResources->SampleCountHistoryImage(
                            imageIndex
                        ),
                        VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_SHADER_WRITE_BIT |
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT
                    );

                    vkCmdPipelineBarrier(
                        commandBuffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                        0,
                        0,
                        nullptr,
                        0,
                        nullptr,
                        static_cast<u32>(resolveImageBarriers.size()),
                        resolveImageBarriers.data()
                    );

                    const VkDescriptorSet resolveTemporalDescriptorSet =
                        ffxSssrResolveTemporalResources->Handle(imageIndex);
                    vkCmdBindPipeline(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        ffxSssrResolveTemporalPipeline->Handle()
                    );
                    vkCmdBindDescriptorSets(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        ffxSssrResolveTemporalPipeline->Layout(),
                        0,
                        1,
                        &ffxConstantsDescriptorSet,
                        0,
                        nullptr
                    );
                    vkCmdBindDescriptorSets(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        ffxSssrResolveTemporalPipeline->Layout(),
                        1,
                        1,
                        &resolveTemporalDescriptorSet,
                        0,
                        nullptr
                    );
                    vkCmdDispatchIndirect(
                        commandBuffer,
                        ffxSssrPrepareIndirectArgsResources
                            ->IndirectArgsBuffer(imageIndex),
                        kFfxSssrDenoiserIndirectArgsOffset
                    );
                    ffxSssrResolveTemporalDispatched = true;
                    historyCopies += CopyFfxSssrHistoryToOtherImages(
                        commandBuffer,
                        *ffxSssrReprojectResources,
                        imageIndex
                    );

                    if (bindStats != nullptr) {
                        ++bindStats->ffxSssrResolveTemporalDispatches;
                        bindStats->ffxSssrResolveTemporalDescriptorBinds += 2u;
                        bindStats->ffxSssrResolveTemporalHistoryCopies +=
                            historyCopies;
                    }
                }
            }
        }
    }

    const bool ffxSssrSameFrameApplyReady =
        ffxSssrSameFrameCompositeEnabled &&
        ffxSssrResolveTemporalDispatched &&
        ffxSssrApplyPipeline != nullptr &&
        ffxSssrApplyGBufferDescriptorSets != nullptr &&
        ffxSssrApplyGBufferDescriptorSets->Count() > imageIndex &&
        deferredLightingFrameDescriptorSets != nullptr &&
        deferredLightingFrameDescriptorSets->Count() > imageIndex &&
        ffxSssrReprojectResources != nullptr &&
        ffxSssrReprojectResources->Count() > imageIndex &&
        hdrRenderPass != nullptr &&
        hdrFramebuffer != nullptr;
    if (ffxSssrSameFrameApplyReady) {
        VkImageMemoryBarrier radianceForApply{};
        radianceForApply.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        radianceForApply.srcAccessMask =
            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        radianceForApply.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        radianceForApply.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        radianceForApply.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        radianceForApply.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        radianceForApply.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        radianceForApply.image =
            ffxSssrReprojectResources->RadianceHistoryImage(imageIndex);
        radianceForApply.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        radianceForApply.subresourceRange.baseMipLevel = 0u;
        radianceForApply.subresourceRange.levelCount = 1u;
        radianceForApply.subresourceRange.baseArrayLayer = 0u;
        radianceForApply.subresourceRange.layerCount = 1u;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1u,
            &radianceForApply
        );
        VkImageMemoryBarrier hitConfidenceForApply = radianceForApply;
        hitConfidenceForApply.image =
            ffxSssrReprojectResources->HitConfidenceImage(imageIndex);
        hitConfidenceForApply.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1u,
            &hitConfidenceForApply
        );

        VkRenderPassBeginInfo applyPassInfo{};
        applyPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        applyPassInfo.renderPass = hdrRenderPass->LoadHandle();
        applyPassInfo.framebuffer = hdrFramebuffer->Handle(imageIndex);
        applyPassInfo.renderArea.offset = { 0, 0 };
        applyPassInfo.renderArea.extent = hdrFramebuffer->Extent();
        vkCmdBeginRenderPass(
            commandBuffer,
            &applyPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );
        SetViewportAndScissor(
            commandBuffer,
            { 0, 0 },
            hdrFramebuffer->Extent()
        );
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            ffxSssrApplyPipeline->Handle()
        );
        const VkDescriptorSet frameDescriptorSet =
            deferredLightingFrameDescriptorSets->Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            ffxSssrApplyPipeline->Layout(),
            0,
            1,
            &frameDescriptorSet,
            0,
            nullptr
        );
        const VkDescriptorSet applyGBufferDescriptorSet =
            ffxSssrApplyGBufferDescriptorSets->Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            ffxSssrApplyPipeline->Layout(),
            1,
            1,
            &applyGBufferDescriptorSet,
            0,
            nullptr
        );
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);

        if (bindStats != nullptr) {
            ++bindStats->ffxSssrApplyDraws;
            ++bindStats->ffxSssrApplyFrameBinds;
            ++bindStats->ffxSssrApplyGBufferBinds;
        }
    }

    // The default FFX path applies ResolveTemporal output in this frame.
    // The reverse control retains the old next-frame Deferred history path.
    const bool ssrPostReconstructionReady =
        ssrReconstructionEnabled &&
        deferredLightingFrameDescriptorSets != nullptr &&
        ssrTemporalPipeline != nullptr &&
        ssrSpatialPipeline != nullptr &&
        ssrDescriptorSets != nullptr &&
        ssrTargets != nullptr &&
        ssrDescriptorSets->Count() == ssrTargets->Count() &&
        ssrTargets->Count() > 1u &&
        hdrRenderPass != nullptr &&
        hdrFramebuffer != nullptr;
    if (ssrPostReconstructionReady) {
        const VkExtent2D ssrExtent = ssrTargets->Extent();
        const u32 groupCountX = (ssrExtent.width + 7u) / 8u;
        const u32 groupCountY = (ssrExtent.height + 7u) / 8u;
        const VkDescriptorSet frameDescriptorSet =
            deferredLightingFrameDescriptorSets->Handle(imageIndex);
        const VkDescriptorSet reconstructionDescriptorSet =
            ssrDescriptorSets->Handle(imageIndex);

        TransitionColorImage(
            commandBuffer,
            ssrTargets->HdrSceneColorImage(imageIndex),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );
        if (ssrTargets->HdrSceneColorMipLevels() > 1u) {
            GenerateColorMipmaps(
                commandBuffer,
                ssrTargets->HdrSceneColorImage(imageIndex),
                ssrTargets->Extent(),
                ssrTargets->HdrSceneColorFormat(),
                ssrTargets->HdrSceneColorMipLevels(),
                physicalDevice
            );
        }
        BarrierSsrComputeImage(
            commandBuffer,
            ssrTargets->SsrHistoryColorImage(imageIndex),
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );
        BarrierSsrComputeImage(
            commandBuffer,
            ssrTargets->SsrHistoryMetadataImage(imageIndex),
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

        auto bindSsrPostPipeline = [&](const VulkanComputePipeline& pipeline) {
            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline.Handle()
            );
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline.Layout(),
                0,
                1,
                &frameDescriptorSet,
                0,
                nullptr
            );
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline.Layout(),
                1,
                1,
                &reconstructionDescriptorSet,
                0,
                nullptr
            );
        };

        bindSsrPostPipeline(*ssrTemporalPipeline);
        vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
        if (bindStats != nullptr) {
            ++bindStats->ssrReconstructionTemporalDispatches;
        }
        BarrierSsrComputeImage(
            commandBuffer,
            ssrTargets->SsrHistoryColorImage(imageIndex),
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );
        BarrierSsrComputeImage(
            commandBuffer,
            ssrTargets->SsrHistoryMetadataImage(imageIndex),
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );
        BarrierSsrComputeImage(
            commandBuffer,
            ssrTargets->SsrResolvedImage(imageIndex),
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );

        bindSsrPostPipeline(*ssrSpatialPipeline);
        vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
        if (bindStats != nullptr) {
            ++bindStats->ssrReconstructionSpatialDispatches;
        }
        BarrierSsrComputeImage(
            commandBuffer,
            ssrTargets->SsrResolvedImage(imageIndex),
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            ssrDiagnosticsPipeline != nullptr
                ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        );
#if !defined(NDEBUG)
        if (ssrDiagnosticsPipeline != nullptr) {
            VkMemoryBarrier diagnosticsBarrier{};
            diagnosticsBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            diagnosticsBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            diagnosticsBarrier.dstAccessMask =
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                1,
                &diagnosticsBarrier,
                0,
                nullptr,
                0,
                nullptr
            );
            bindSsrPostPipeline(*ssrDiagnosticsPipeline);
            vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
        }
#else
        (void)ssrDiagnosticsPipeline;
#endif
        CopySsrHistoryToOtherImages(
            commandBuffer,
            *ssrTargets,
            imageIndex
        );
        if (bindStats != nullptr) {
            bindStats->ssrReconstructionHistoryCopies +=
                static_cast<u32>(ssrTargets->Count() > 0
                    ? ssrTargets->Count() - 1u
                    : 0u);
        }
    }

    if (recordAutoExposureCompute &&
        autoExposureComputePipeline != nullptr &&
        autoExposureFrameDescriptorSets != nullptr &&
        autoExposureHdrDescriptorSets != nullptr) {
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            autoExposureComputePipeline->Handle()
        );

        const VkDescriptorSet frameDescriptorSet =
            autoExposureFrameDescriptorSets->Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            autoExposureComputePipeline->Layout(),
            0,
            1,
            &frameDescriptorSet,
            0,
            nullptr
        );

        const VkDescriptorSet hdrDescriptorSet =
            autoExposureHdrDescriptorSets->Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            autoExposureComputePipeline->Layout(),
            1,
            1,
            &hdrDescriptorSet,
            0,
            nullptr
        );

        vkCmdDispatch(commandBuffer, 1, 1, 1);
        BarrierComputeAutoExposureForFragmentRead(commandBuffer);
        if (frameGraph != nullptr) {
            RecordRenderFrameGraphBarrierExecution(
                *frameGraph,
                RenderFrameGraphBarrierBridge::AutoExposureHistoryFragmentRead
            );
        }

        if (bindStats != nullptr) {
            ++bindStats->autoExposureHistogramDispatches;
            ++bindStats->autoExposureHistogramFrameBinds;
            ++bindStats->autoExposureHistogramTextureBinds;
            bindStats->autoExposureHistogramGroupsX = 1;
            bindStats->autoExposureHistogramGroupsY = 1;
        }
    }

    bool dlssMaskInputsPreparedForEvaluate = false;
    if (dlssMaskRenderPass != nullptr &&
        dlssMaskFramebuffer != nullptr &&
        dlssMaskGraphicsPipeline != nullptr &&
        temporalUpscaleState != nullptr &&
        temporalUpscaleState->temporalUpscaleEnabled &&
        temporalUpscaleState->upscalerPluginAvailable &&
        (!dlssMaskWeightedTranslucencyRenderCommands.empty() ||
            !dlssMaskForwardResidualRenderCommands.empty())) {
        std::array<VkClearValue, 2> maskClearValues{};
        maskClearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        maskClearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

        VkRenderPassBeginInfo maskPassInfo{};
        maskPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        maskPassInfo.renderPass = dlssMaskRenderPass->Handle();
        maskPassInfo.framebuffer = dlssMaskFramebuffer->Handle(imageIndex);
        maskPassInfo.renderArea.offset = { 0, 0 };
        maskPassInfo.renderArea.extent = dlssMaskFramebuffer->Extent();
        maskPassInfo.clearValueCount = static_cast<u32>(maskClearValues.size());
        maskPassInfo.pClearValues = maskClearValues.data();

        vkCmdBeginRenderPass(
            commandBuffer,
            &maskPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );
        SetViewportAndScissor(commandBuffer, { 0, 0 }, dlssMaskFramebuffer->Extent());

        u32 maskMaterialBinds = 0;
        u32 maskMeshBinds = 0;
        u32 maskPushConstantUpdates = 0;
        u64 maskPushConstantBytes = 0;
        const u32 weightedMaskDraws = DrawForwardResidualCommands(
            commandBuffer,
            dlssMaskGraphicsPipeline,
            doubleSidedDlssMaskGraphicsPipeline,
            descriptorSets,
            materialDescriptorSets,
            frameMaterials,
            dlssMaskWeightedTranslucencyRenderCommands,
            dlssMaskFramebuffer->Extent(),
            imageIndex,
            maskMaterialBinds,
            maskMeshBinds,
            maskPushConstantUpdates,
            maskPushConstantBytes
        );
        const u32 forwardResidualMaskDraws = DrawForwardResidualCommands(
            commandBuffer,
            dlssMaskGraphicsPipeline,
            doubleSidedDlssMaskGraphicsPipeline,
            descriptorSets,
            materialDescriptorSets,
            frameMaterials,
            dlssMaskForwardResidualRenderCommands,
            dlssMaskFramebuffer->Extent(),
            imageIndex,
            maskMaterialBinds,
            maskMeshBinds,
            maskPushConstantUpdates,
            maskPushConstantBytes
        );

        vkCmdEndRenderPass(commandBuffer);
        dlssMaskInputsPreparedForEvaluate = true;

        if (bindStats != nullptr) {
            bindStats->dlssMaskDraws += weightedMaskDraws + forwardResidualMaskDraws;
            bindStats->dlssMaskWeightedTranslucencyDraws += weightedMaskDraws;
            bindStats->dlssMaskForwardResidualDraws += forwardResidualMaskDraws;
            bindStats->dlssMaskMaterialBinds += maskMaterialBinds;
            bindStats->dlssMaskMeshBinds += maskMeshBinds;
            bindStats->pushConstantUpdates += maskPushConstantUpdates;
            bindStats->pushConstantBytes += maskPushConstantBytes;
        }
    }

    if (sceneRenderTargets != nullptr &&
        temporalState != nullptr &&
        temporalUpscaleState != nullptr &&
        temporalUpscalerEvaluateStatus != nullptr) {
        RecordTemporalUpscalerEvaluate(
            commandBuffer,
            m_Device,
            *sceneRenderTargets,
            imageIndex,
            *temporalState,
            *temporalUpscaleState,
            temporalUpscaleOutputInitialized,
            dlssMaskInputsInitialized,
            dlssMaskInputsPreparedForEvaluate,
            *temporalUpscalerEvaluateStatus
        );
    } else if (temporalUpscalerEvaluateStatus != nullptr) {
        *temporalUpscalerEvaluateStatus = TemporalUpscalerEvaluateStatus{};
    }

    TemporalUpscalePostSourceStatus postSourceStatus{};
    postSourceStatus.requested = temporalUpscalePostSourceRequested ? 1u : 0u;
    if (!temporalUpscalePostSourceRequested) {
        postSourceStatus.fallbackReason =
            TemporalUpscalePostSourceFallbackReason::Disabled;
    } else if (!useHdrCompositeAsMain ||
        hdrCompositePipeline == nullptr ||
        hdrCompositeDescriptorSets == nullptr) {
        postSourceStatus.fallbackReason =
            TemporalUpscalePostSourceFallbackReason::CompositeUnavailable;
    } else if (temporalUpscalerEvaluateStatus == nullptr ||
        temporalUpscalerEvaluateStatus->outputReady == 0u) {
        postSourceStatus.fallbackReason =
            TemporalUpscalePostSourceFallbackReason::EvaluateOutputUnavailable;
    } else if (temporalUpscaleHdrCompositeDescriptorSets == nullptr ||
        (recordBloomPyramid && temporalUpscaleBloomDescriptorSets == nullptr)) {
        postSourceStatus.fallbackReason =
            TemporalUpscalePostSourceFallbackReason::DescriptorUnavailable;
    } else {
        postSourceStatus.active = 1u;
        postSourceStatus.fallbackReason =
            TemporalUpscalePostSourceFallbackReason::None;
    }
    if (temporalUpscalePostSourceStatus != nullptr) {
        *temporalUpscalePostSourceStatus = postSourceStatus;
    }

    const bool bypassTemporalUpscalePostSource =
        DlssBypassPostSourceRequested();
    if (bypassTemporalUpscalePostSource && postSourceStatus.active > 0u) {
        postSourceStatus.active = 0u;
        postSourceStatus.fallbackReason =
            TemporalUpscalePostSourceFallbackReason::Disabled;
        if (temporalUpscalePostSourceStatus != nullptr) {
            *temporalUpscalePostSourceStatus = postSourceStatus;
        }
    }

    const bool routeTemporalUpscaleOutputToPost =
        postSourceStatus.active > 0u;
    const VulkanHdrDescriptorSets* activeHdrCompositeDescriptorSets =
        routeTemporalUpscaleOutputToPost
            ? temporalUpscaleHdrCompositeDescriptorSets
            : hdrCompositeDescriptorSets;
    const VulkanBloomDescriptorSets* activeBloomDescriptorSets =
        routeTemporalUpscaleOutputToPost
            ? temporalUpscaleBloomDescriptorSets
            : bloomDescriptorSets;

    if (recordBloomPyramid &&
        bloomDownsampleRenderPass != nullptr &&
        bloomUpsampleRenderPass != nullptr &&
        bloomDownsampleFramebuffer != nullptr &&
        bloomUpsampleFramebuffer != nullptr &&
        bloomDownsamplePipeline != nullptr &&
        bloomUpsamplePipeline != nullptr &&
        activeBloomDescriptorSets != nullptr &&
        activeBloomDescriptorSets->MipCount() > 0) {
        const u32 mipCount = activeBloomDescriptorSets->MipCount();
        for (u32 mipIndex = 0; mipIndex < mipCount; ++mipIndex) {
            RecordBloomFullscreenPass(
                commandBuffer,
                *bloomDownsampleRenderPass,
                *bloomDownsampleFramebuffer,
                *bloomDownsamplePipeline,
                descriptorSets,
                *activeBloomDescriptorSets,
                imageIndex,
                mipIndex,
                false,
                bindStats
            );
        }
        if (mipCount > 1) {
            for (u32 mipIndex = mipCount - 1; mipIndex > 0; --mipIndex) {
                RecordBloomFullscreenPass(
                    commandBuffer,
                    *bloomUpsampleRenderPass,
                    *bloomUpsampleFramebuffer,
                    *bloomUpsamplePipeline,
                    descriptorSets,
                    *activeBloomDescriptorSets,
                    imageIndex,
                    mipIndex - 1,
                    true,
                    bindStats
                );
            }
        }
    } else if (useHdrCompositeAsMain &&
        hdrCompositePipeline != nullptr &&
        activeHdrCompositeDescriptorSets != nullptr &&
        bloomDownsampleRenderPass != nullptr &&
        bloomDownsampleFramebuffer != nullptr) {
        ClearBloomMipForSampledRead(
            commandBuffer,
            *bloomDownsampleRenderPass,
            *bloomDownsampleFramebuffer,
            imageIndex
        );
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { { 0.05f, 0.07f, 0.10f, 1.0f } };
    clearValues[1].depthStencil = { 1.0f, 0 };
    const bool useDeferredCompositeAsMain =
        (gBufferDebugView >= 0 &&
            gBufferDebugPipeline != nullptr &&
            gBufferDebugDescriptorSets != nullptr) ||
        (useHdrCompositeAsMain &&
            hdrCompositePipeline != nullptr &&
            activeHdrCompositeDescriptorSets != nullptr);
    const bool needsResidualDepth =
        useDeferredCompositeAsMain &&
        !forwardResidualDrawnInHdr &&
        !forwardResidualRenderCommands.empty() &&
        !gBufferRenderCommands.empty();
    bool copiedSceneDepth = false;
    if (needsResidualDepth) {
        copiedSceneDepth = CopySceneDepthToSwapchainDepth(
            commandBuffer,
            sceneRenderTargets,
            swapchainDepthBuffer,
            imageIndex,
            extent
        );
        if (copiedSceneDepth && bindStats != nullptr) {
            ++bindStats->depthCopyOps;
        }
    }

    const bool useDepthLoadMainPass =
        copiedSceneDepth &&
        depthLoadRenderPass != nullptr &&
        depthLoadFramebuffer != nullptr;
    const VulkanRenderPass& activeRenderPass = useDepthLoadMainPass
        ? *depthLoadRenderPass
        : renderPass;
    const std::vector<VkFramebuffer>& activeFramebuffers = useDepthLoadMainPass
        ? depthLoadFramebuffer->Handles()
        : framebuffers;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = activeRenderPass.Handle();
    renderPassInfo.framebuffer = activeFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = static_cast<u32>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE
    );

    if (gpuTimer != nullptr) {
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::MainStart);
    }

    DrawStateCache mainState{};
    u32 mainMaterialBinds = 0;
    u32 mainMeshBinds = 0;
    u32 mainBonePaletteDescriptorBinds = 0;
    u32 mainPushConstantUpdates = 0;
    u64 mainPushConstantBytes = 0;
    u32 mainInstancedDraws = 0;
    u32 mainInstancedInstances = 0;
    std::size_t commandIndex = 0;
    std::size_t batchIndex = 0;
    bool regularPipelineBound = true;
    const VulkanGraphicsPipeline* boundInstancedPipeline = nullptr;
    auto bindInstancedPipeline = [&](const VulkanGraphicsPipeline& pipeline) {
        if (boundInstancedPipeline == &pipeline) {
            return;
        }

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.Handle()
        );
        const VkDescriptorSet instancedDescriptorSet = descriptorSets.Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.Layout(),
            0,
            1,
            &instancedDescriptorSet,
            0,
            nullptr
        );
        if (instanceBuffer != nullptr) {
            instanceBuffer->Bind(commandBuffer, imageIndex);
        }
        mainState = DrawStateCache{};
        boundInstancedPipeline = &pipeline;
        regularPipelineBound = false;
    };
    if (needsResidualDepth && !copiedSceneDepth) {
        u32 depthPrefillMeshBinds = 0;
        u32 depthPrefillPushConstantUpdates = 0;
        u64 depthPrefillPushConstantBytes = 0;
        const u32 depthPrefillDraws = DrawDepthPrefillCommands(
            commandBuffer,
            depthPrefillGraphicsPipeline,
            doubleSidedDepthPrefillGraphicsPipeline,
            descriptorSets,
            gBufferRenderCommands,
            imageIndex,
            depthPrefillMeshBinds,
            depthPrefillPushConstantUpdates,
            depthPrefillPushConstantBytes
        );
        if (bindStats != nullptr) {
            bindStats->depthPrefillDraws += depthPrefillDraws;
            bindStats->depthPrefillMeshBinds += depthPrefillMeshBinds;
            bindStats->pushConstantUpdates += depthPrefillPushConstantUpdates;
            bindStats->pushConstantBytes += depthPrefillPushConstantBytes;
        }
    }
    if (gBufferDebugView >= 0 &&
        gBufferDebugPipeline != nullptr &&
        gBufferDebugDescriptorSets != nullptr) {
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            gBufferDebugPipeline->Handle()
        );

        const VkDescriptorSet frameDescriptorSet = descriptorSets.Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            gBufferDebugPipeline->Layout(),
            0,
            1,
            &frameDescriptorSet,
            0,
            nullptr
        );

        const VkDescriptorSet gBufferDescriptorSet =
            gBufferDebugDescriptorSets->Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            gBufferDebugPipeline->Layout(),
            1,
            1,
            &gBufferDescriptorSet,
            0,
            nullptr
        );

        SetViewportAndScissor(commandBuffer, { 0, 0 }, renderPassInfo.renderArea.extent);

        ObjectPushConstants debugConstants =
            DeferredLightingPushConstants(gBufferRenderCommands);
        debugConstants.materialControls = glm::vec4(static_cast<f32>(gBufferDebugView), 0.0f, 0.0f, 0.0f);
        u32 debugPushConstantUpdates = 0;
        u64 debugPushConstantBytes = 0;
        PushConstants(
            commandBuffer,
            *gBufferDebugPipeline,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(ObjectPushConstants),
            &debugConstants,
            debugPushConstantUpdates,
            debugPushConstantBytes
        );
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        if (bindStats != nullptr) {
            ++bindStats->gBufferDebugDraws;
            ++bindStats->gBufferDebugFrameBinds;
            ++bindStats->gBufferDebugTextureBinds;
            if (gBufferDebugView == 8) {
                ++bindStats->deferredShadowDebugDraws;
                ++bindStats->deferredShadowDebugFrameBinds;
                ++bindStats->deferredShadowDebugTextureBinds;
            }
            if (gBufferDebugView == 9 || gBufferDebugView == 10) {
                ++bindStats->shadowCascadeDebugDraws;
                ++bindStats->shadowCascadeDebugFrameBinds;
                ++bindStats->shadowCascadeDebugTextureBinds;
            }
            bindStats->pushConstantUpdates += debugPushConstantUpdates;
            bindStats->pushConstantBytes += debugPushConstantBytes;
        }
        u32 residualMaterialBinds = 0;
        u32 residualMeshBinds = 0;
        u32 residualPushConstantUpdates = 0;
        u64 residualPushConstantBytes = 0;
        const u32 residualDraws = forwardResidualDrawnInHdr
            ? 0u
            : DrawForwardResidualCommands(
                commandBuffer,
                forwardResidualGraphicsPipeline,
                doubleSidedForwardResidualGraphicsPipeline,
                descriptorSets,
                materialDescriptorSets,
                frameMaterials,
                forwardResidualRenderCommands,
                extent,
                imageIndex,
                residualMaterialBinds,
                residualMeshBinds,
                residualPushConstantUpdates,
                residualPushConstantBytes
            );
        if (bindStats != nullptr) {
            bindStats->forwardResidualDraws += residualDraws;
            if (residualDraws > 0) {
                ++bindStats->forwardResidualFrameBinds;
                bindStats->forwardResidualSharedLightListDraws += residualDraws;
            }
            bindStats->forwardResidualMaterialBinds += residualMaterialBinds;
            bindStats->forwardResidualMeshBinds += residualMeshBinds;
            bindStats->pushConstantUpdates += residualPushConstantUpdates;
            bindStats->pushConstantBytes += residualPushConstantBytes;
        }
    } else if (useHdrCompositeAsMain &&
        hdrCompositePipeline != nullptr &&
        activeHdrCompositeDescriptorSets != nullptr) {
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            hdrCompositePipeline->Handle()
        );

        const VkDescriptorSet frameDescriptorSet = descriptorSets.Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            hdrCompositePipeline->Layout(),
            0,
            1,
            &frameDescriptorSet,
            0,
            nullptr
        );

        const VkDescriptorSet hdrDescriptorSet =
            activeHdrCompositeDescriptorSets->Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            hdrCompositePipeline->Layout(),
            1,
            1,
            &hdrDescriptorSet,
            0,
            nullptr
        );

        SetViewportAndScissor(commandBuffer, { 0, 0 }, renderPassInfo.renderArea.extent);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        if (bindStats != nullptr) {
            ++bindStats->hdrCompositeDraws;
            ++bindStats->hdrCompositeFrameBinds;
            ++bindStats->hdrCompositeTextureBinds;
            if (bloomDebugView) {
                ++bindStats->bloomDebugDraws;
                ++bindStats->bloomDebugFrameBinds;
                ++bindStats->bloomDebugTextureBinds;
            }
            if (toneMappingDebugView) {
                ++bindStats->toneMappingDebugDraws;
                ++bindStats->toneMappingDebugFrameBinds;
                ++bindStats->toneMappingDebugTextureBinds;
            }
            if (autoExposureDebugView) {
                ++bindStats->autoExposureDebugDraws;
                ++bindStats->autoExposureDebugFrameBinds;
                ++bindStats->autoExposureDebugTextureBinds;
            }
            if (colorGradingDebugView) {
                ++bindStats->colorGradingDebugDraws;
                ++bindStats->colorGradingDebugFrameBinds;
                ++bindStats->colorGradingDebugTextureBinds;
            }
            if (sharpeningDebugView) {
                ++bindStats->sharpeningDebugDraws;
                ++bindStats->sharpeningDebugFrameBinds;
                ++bindStats->sharpeningDebugTextureBinds;
            }
        }
        u32 residualMaterialBinds = 0;
        u32 residualMeshBinds = 0;
        u32 residualPushConstantUpdates = 0;
        u64 residualPushConstantBytes = 0;
        const u32 residualDraws = forwardResidualDrawnInHdr
            ? 0u
            : DrawForwardResidualCommands(
                commandBuffer,
                forwardResidualGraphicsPipeline,
                doubleSidedForwardResidualGraphicsPipeline,
                descriptorSets,
                materialDescriptorSets,
                frameMaterials,
                forwardResidualRenderCommands,
                extent,
                imageIndex,
                residualMaterialBinds,
                residualMeshBinds,
                residualPushConstantUpdates,
                residualPushConstantBytes
            );
        if (bindStats != nullptr) {
            bindStats->forwardResidualDraws += residualDraws;
            if (residualDraws > 0) {
                ++bindStats->forwardResidualFrameBinds;
                bindStats->forwardResidualSharedLightListDraws += residualDraws;
            }
            bindStats->forwardResidualMaterialBinds += residualMaterialBinds;
            bindStats->forwardResidualMeshBinds += residualMeshBinds;
            bindStats->pushConstantUpdates += residualPushConstantUpdates;
            bindStats->pushConstantBytes += residualPushConstantBytes;
        }
    } else {
        const VulkanGraphicsPipeline* boundRegularPipeline = nullptr;
        auto bindRegularPipeline = [&](const VulkanGraphicsPipeline& pipeline) {
            if (boundRegularPipeline == &pipeline) {
                return;
            }

            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline.Handle()
            );

            const VkDescriptorSet descriptorSet = descriptorSets.Handle(imageIndex);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline.Layout(),
                0,
                1,
                &descriptorSet,
                0,
                nullptr
            );
            mainState = DrawStateCache{};
            boundRegularPipeline = &pipeline;
        };
        bindRegularPipeline(graphicsPipeline);

        while (commandIndex < renderCommands.size()) {
        const bool useInstancedBatch =
            instancedGraphicsPipeline != nullptr &&
            instanceBuffer != nullptr &&
            batchIndex < instanceBatches.size() &&
            instanceBatches[batchIndex].firstCommandIndex == commandIndex;
        if (useInstancedBatch) {
            const RenderInstanceBatch& batch = instanceBatches[batchIndex];
            const VulkanGraphicsPipeline& activeInstancedPipeline = PipelineForCommand(
                *instancedGraphicsPipeline,
                doubleSidedInstancedGraphicsPipeline,
                renderCommands[commandIndex]
            );
            bindInstancedPipeline(activeInstancedPipeline);
            boundRegularPipeline = nullptr;
            const u32 materialId = frameMaterials != nullptr
                ? frameMaterials->IdFor(renderCommands[commandIndex].material)
                : 0;
            DrawInstancedRenderCommand(
                commandBuffer,
                activeInstancedPipeline,
                materialDescriptorSets,
                renderCommands[commandIndex],
                batch,
                extent,
                imageIndex,
                static_cast<f32>(materialId),
                mainState,
                mainMaterialBinds,
                mainMeshBinds,
                mainPushConstantUpdates,
                mainPushConstantBytes,
                mainInstancedDraws,
                mainInstancedInstances
            );
            commandIndex += batch.commandCount;
            ++batchIndex;
            continue;
        }

        if (!regularPipelineBound) {
            bindRegularPipeline(graphicsPipeline);
            regularPipelineBound = true;
            boundInstancedPipeline = nullptr;
        }

        const VulkanGraphicsPipeline& activeRegularPipeline = PipelineForCommand(
            graphicsPipeline,
            doubleSidedGraphicsPipeline,
            renderCommands[commandIndex]
        );
        bindRegularPipeline(activeRegularPipeline);
        const u32 materialId = frameMaterials != nullptr
            ? frameMaterials->IdFor(renderCommands[commandIndex].material)
            : 0;
        DrawRenderCommand(
            commandBuffer,
            activeRegularPipeline,
            materialDescriptorSets,
            renderCommands[commandIndex],
            extent,
            imageIndex,
            static_cast<f32>(materialId),
            mainState,
            mainMaterialBinds,
            mainMeshBinds,
            mainPushConstantUpdates,
            mainPushConstantBytes,
            0.0f,
            &mainBonePaletteDescriptorBinds
        );
        ++commandIndex;
        }
    }
    if (bindStats != nullptr) {
        bindStats->mainMaterialBinds += mainMaterialBinds;
        bindStats->mainMeshBinds += mainMeshBinds;
        bindStats->mainBonePaletteDescriptorBinds += mainBonePaletteDescriptorBinds;
        bindStats->bonePaletteDescriptorBinds += mainBonePaletteDescriptorBinds;
        bindStats->pushConstantUpdates += mainPushConstantUpdates;
        bindStats->pushConstantBytes += mainPushConstantBytes;
        bindStats->mainInstancedDraws += mainInstancedDraws;
        bindStats->mainInstancedInstances += mainInstancedInstances;
    }

    if (gpuTimer != nullptr) {
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::MainEnd);
    }

    if (overlayGraphicsPipeline != nullptr &&
        overlayDescriptorSets != nullptr &&
        !overlayRenderCommands.empty()) {
        if (gpuTimer != nullptr) {
            gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::OverlayStart);
        }

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            overlayGraphicsPipeline->Handle()
        );

        const VkDescriptorSet overlayDescriptorSet = overlayDescriptorSets->Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            overlayGraphicsPipeline->Layout(),
            0,
            1,
            &overlayDescriptorSet,
            0,
            nullptr
        );

        DrawStateCache overlayState{};
        u32 overlayMaterialBinds = 0;
        u32 overlayMeshBinds = 0;
        u32 overlayPushConstantUpdates = 0;
        u64 overlayPushConstantBytes = 0;
        for (const RenderCommand& renderCommand : overlayRenderCommands) {
            DrawRenderCommand(
                commandBuffer,
                *overlayGraphicsPipeline,
                materialDescriptorSets,
                renderCommand,
                extent,
                imageIndex,
                0.0f,
                overlayState,
                overlayMaterialBinds,
                overlayMeshBinds,
                overlayPushConstantUpdates,
                overlayPushConstantBytes
            );
        }
        if (bindStats != nullptr) {
            bindStats->overlayMaterialBinds += overlayMaterialBinds;
            bindStats->overlayMeshBinds += overlayMeshBinds;
            bindStats->pushConstantUpdates += overlayPushConstantUpdates;
            bindStats->pushConstantBytes += overlayPushConstantBytes;
        }

        if (gpuTimer != nullptr) {
            gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::OverlayEnd);
        }
    } else if (gpuTimer != nullptr) {
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::OverlayStart);
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::OverlayEnd);
    }

    if (imguiLayer != nullptr) {
        if (gpuTimer != nullptr) {
            gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ImGuiStart);
        }

        imguiLayer->Render(commandBuffer);

        if (gpuTimer != nullptr) {
            gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ImGuiEnd);
        }
    } else if (gpuTimer != nullptr) {
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ImGuiStart);
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ImGuiEnd);
    }

    vkCmdEndRenderPass(commandBuffer);

    const bool recordResolvedTemporalHistoryColor =
        recordTemporalHistoryColorCopy &&
        temporalHistoryColorCopySource == TemporalHistoryColorCopySource::TaaResolvedColor &&
        sceneRenderTargets != nullptr &&
        temporalState != nullptr &&
        temporalState->taaResolveEnabled &&
        hdrRenderPass != nullptr &&
        taaResolveFramebuffer != nullptr &&
        taaResolvePipeline != nullptr &&
        hdrCompositeDescriptorSets != nullptr;
    if (recordResolvedTemporalHistoryColor) {
        RecordTaaResolveHistoryPass(
            commandBuffer,
            *hdrRenderPass,
            *taaResolveFramebuffer,
            *taaResolvePipeline,
            descriptorSets,
            *hdrCompositeDescriptorSets,
            imageIndex
        );
    }

    if (recordTemporalHistoryColorCopy && sceneRenderTargets != nullptr) {
        const TemporalHistoryColorCopySource actualCopySource =
            recordResolvedTemporalHistoryColor
                ? TemporalHistoryColorCopySource::TaaResolvedColor
                : TemporalHistoryColorCopySource::HdrSceneColor;
        CopyColorToTemporalHistory(
            commandBuffer,
            *sceneRenderTargets,
            imageIndex,
            actualCopySource
        );
    }

    if (gpuTimer != nullptr) {
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::FrameEnd);
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record Vulkan command buffer");
    }
}

void VulkanCommandBuffer::Release() {
    if (!m_CommandBuffers.empty()) {
        vkFreeCommandBuffers(
            m_Device,
            m_CommandPool,
            static_cast<u32>(m_CommandBuffers.size()),
            m_CommandBuffers.data()
        );

        m_CommandBuffers.clear();
    }
}

}
