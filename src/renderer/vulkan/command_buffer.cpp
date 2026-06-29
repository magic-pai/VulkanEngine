#include "renderer/vulkan/command_buffer.h"

#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/compute_pipeline.h"
#include "renderer/vulkan/depth_buffer.h"
#include "renderer/vulkan/descriptor_sets.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/framebuffer.h"
#include "renderer/vulkan/frame_materials.h"
#include "renderer/vulkan/gpu_timer.h"
#include "renderer/vulkan/graphics_pipeline.h"
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
#include "renderer/render_queue.h"

#include <cstddef>

namespace se {

namespace {

struct DrawStateCache {
    const VulkanMaterial* material = nullptr;
    const VulkanMesh* mesh = nullptr;
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
};
constexpr std::size_t kShadowPushConstantBytes = sizeof(ShadowDepthPushConstants);

static_assert(
    kObjectPushConstantBytes == sizeof(glm::mat4) + sizeof(glm::vec4),
    "Object push constant segment must contain model and tint"
);
static_assert(
    sizeof(RenderMaterialPushConstants) == kMaterialPushConstantBytes,
    "Render material push constants must match the ObjectPushConstants material segment"
);
static_assert(
    sizeof(ShadowDepthPushConstants) <= sizeof(ObjectPushConstants),
    "Shadow push constants must fit inside the graphics pipeline push-constant range"
);

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
    u64& pushConstantByteCount
) {
    RenderMaterialPushConstants materialData = renderCommand.materialPushConstants;
    materialData.materialControls.w = materialId;
    materialData.viewport = glm::vec4(
        static_cast<f32>(extent.width),
        static_cast<f32>(extent.height),
        0.0f,
        0.0f
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
    u64& pushConstantByteCount
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
            pushConstantByteCount
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
    u64& pushConstantByteCount
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
            pushConstantByteCount
        );
    }
}

void DrawShadowCommand(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& graphicsPipeline,
    const RenderCommand& renderCommand,
    const glm::mat4& lightViewProjection,
    DrawStateCache& state,
    u32& meshBindCount,
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
    vkCmdDrawIndexed(commandBuffer, renderCommand.mesh->IndexCount(), 1, 0, 0, 0);
}

void SetShadowViewportAndScissor(
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

void DrawShadowCommands(
    VkCommandBuffer commandBuffer,
    const VulkanGraphicsPipeline& shadowGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedShadowGraphicsPipeline,
    const VulkanDescriptorSets& shadowDescriptorSets,
    std::span<const RenderCommand> shadowRenderCommands,
    std::size_t imageIndex,
    const glm::mat4& lightViewProjection,
    VkOffset2D viewportOffset,
    VkExtent2D viewportExtent,
    u32& meshBindCount,
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
            shadowState,
            meshBindCount,
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
    u64& pushConstantByteCount
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
        pushConstantByteCount
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
            state,
            meshBindCount,
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
        const VulkanHdrRenderPass* hdrRenderPass,
        const VulkanHdrFramebuffer* hdrFramebuffer,
        const VulkanGraphicsPipeline* deferredLightingPipeline,
        const VulkanDescriptorSets* deferredLightingFrameDescriptorSets,
        const VulkanGBufferDescriptorSets* deferredLightingGBufferDescriptorSets,
        int deferredPbrDebugView,
        const VulkanGraphicsPipeline* hdrCompositePipeline,
        const VulkanHdrDescriptorSets* hdrCompositeDescriptorSets,
        bool useHdrCompositeAsMain,
        const VulkanGraphicsPipeline* gBufferDebugPipeline,
        const VulkanGBufferDescriptorSets* gBufferDebugDescriptorSets,
        int gBufferDebugView,
    const VulkanGraphicsPipeline* depthPrefillGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedDepthPrefillGraphicsPipeline,
        const VulkanSceneRenderTargets* sceneRenderTargets,
        const VulkanDepthBuffer* swapchainDepthBuffer,
        const VulkanGBufferRenderPass* gBufferRenderPass,
        const VulkanGBufferFramebuffer* gBufferFramebuffer,
    const VulkanGraphicsPipeline* gBufferGraphicsPipeline,
    const VulkanGraphicsPipeline* doubleSidedGBufferGraphicsPipeline,
    const VulkanDescriptorSets* gBufferDescriptorSets,
        std::span<const RenderCommand> gBufferRenderCommands,
        const VulkanComputePipeline* lightTileCullComputePipeline,
        const VulkanDescriptorSets* lightTileCullDescriptorSets,
        u32 lightTileCullGroupCountX,
        u32 lightTileCullGroupCountY,
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
        RendererBindStats* bindStats
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
    SE_ASSERT(
        descriptorSets.Count() == m_CommandBuffers.size(),
        "Descriptor set count must match command buffer count"
    );
    SE_ASSERT(!renderCommands.empty(), "A command buffer record pass needs at least one render command");

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
    }

    if (shadowRenderPass != nullptr &&
        shadowGraphicsPipeline != nullptr &&
        shadowFramebuffer != nullptr &&
        shadowDescriptorSets != nullptr) {
        if (gpuTimer != nullptr) {
            gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ShadowStart);
        }

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
            { 0, 0 },
            shadowFramebuffer->Extent(),
            shadowMeshBinds,
            shadowPushConstantUpdates,
            shadowPushConstantBytes
        );
        if (bindStats != nullptr) {
            bindStats->shadowMeshBinds += shadowMeshBinds;
            bindStats->pushConstantUpdates += shadowPushConstantUpdates;
            bindStats->pushConstantBytes += shadowPushConstantBytes;
        }

        vkCmdEndRenderPass(commandBuffer);

        if (gpuTimer != nullptr) {
            gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ShadowEnd);
        }
    } else if (gpuTimer != nullptr) {
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ShadowStart);
        gpuTimer->WriteTimestamp(commandBuffer, imageIndex, GpuTimestamp::ShadowEnd);
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
        u32 cascadeAtlasPushConstantUpdates = 0;
        u64 cascadeAtlasPushConstantBytes = 0;
        for (u32 cascadeIndex = 0; cascadeIndex < activeCascadeCount; ++cascadeIndex) {
            const u32 tileX = cascadeIndex % 2u;
            const u32 tileY = cascadeIndex / 2u;
            const VkOffset2D tileOffset{
                static_cast<i32>(tileX * cascadeTileExtent.width),
                static_cast<i32>(tileY * cascadeTileExtent.height)
            };
            DrawShadowCommands(
                commandBuffer,
                *shadowGraphicsPipeline,
                doubleSidedShadowGraphicsPipeline,
                *shadowDescriptorSets,
                shadowRenderCommands,
                imageIndex,
                directionalShadowCascades->cascades[cascadeIndex].viewProjection,
                tileOffset,
                cascadeTileExtent,
                cascadeAtlasMeshBinds,
                cascadeAtlasPushConstantUpdates,
                cascadeAtlasPushConstantBytes
            );
        }

        if (bindStats != nullptr) {
            bindStats->shadowCascadeAtlasPasses += activeCascadeCount;
            bindStats->shadowCascadeAtlasDraws +=
                activeCascadeCount * static_cast<u32>(shadowRenderCommands.size());
            bindStats->shadowCascadeAtlasMeshBinds += cascadeAtlasMeshBinds;
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
        localShadowTiles->assignedCount > 0) {
        VkClearValue localShadowClearValue{};
        localShadowClearValue.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo localShadowPassInfo{};
        localShadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        localShadowPassInfo.renderPass = shadowRenderPass->Handle();
        localShadowPassInfo.framebuffer =
            localShadowFramebuffer->Handle(imageIndex);
        localShadowPassInfo.renderArea.offset = { 0, 0 };
        localShadowPassInfo.renderArea.extent =
            localShadowFramebuffer->Extent();
        localShadowPassInfo.clearValueCount = 1;
        localShadowPassInfo.pClearValues = &localShadowClearValue;

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
        u32 localAtlasPushConstantUpdates = 0;
        u64 localAtlasPushConstantBytes = 0;
        const u32 assignedLocalShadowTileCount = std::min<u32>(
            localShadowTiles->assignedCount,
            static_cast<u32>(localShadowTiles->tiles.size())
        );
        for (u32 tileSetIndex = 0; tileSetIndex < assignedLocalShadowTileCount; ++tileSetIndex) {
            const LocalShadowTile& tile = localShadowTiles->tiles[tileSetIndex];
            const u32 tileX = tile.tileIndex % tileColumns;
            const u32 tileY = tile.tileIndex / tileColumns;
            const VkOffset2D tileOffset{
                static_cast<i32>(tileX * tileExtent.width),
                static_cast<i32>(tileY * tileExtent.height)
            };
            DrawShadowCommands(
                commandBuffer,
                *shadowGraphicsPipeline,
                doubleSidedShadowGraphicsPipeline,
                *shadowDescriptorSets,
                shadowRenderCommands,
                imageIndex,
                tile.viewProjection,
                tileOffset,
                tileExtent,
                localAtlasMeshBinds,
                localAtlasPushConstantUpdates,
                localAtlasPushConstantBytes
            );
        }

        if (bindStats != nullptr) {
            bindStats->localShadowAtlasPasses += assignedLocalShadowTileCount;
            bindStats->localShadowAtlasDraws +=
                assignedLocalShadowTileCount * static_cast<u32>(shadowRenderCommands.size());
            bindStats->localShadowAtlasMeshBinds += localAtlasMeshBinds;
            bindStats->pushConstantUpdates += localAtlasPushConstantUpdates;
            bindStats->pushConstantBytes += localAtlasPushConstantBytes;
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    if (gBufferRenderPass != nullptr && gBufferFramebuffer != nullptr) {
        std::array<VkClearValue, 6> gBufferClearValues{};
        gBufferClearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        gBufferClearValues[1].color = { { 0.5f, 0.5f, 1.0f, 1.0f } };
        gBufferClearValues[2].color = { { 0.0f, 1.0f, 0.0f, 1.0f } };
        gBufferClearValues[3].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        gBufferClearValues[4].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        gBufferClearValues[5].depthStencil = { 1.0f, 0 };

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
                    gBufferPushConstantBytes
                );
            }

            if (bindStats != nullptr) {
                bindStats->gBufferMaterialBinds += gBufferMaterialBinds;
                bindStats->gBufferMeshBinds += gBufferMeshBinds;
                bindStats->pushConstantUpdates += gBufferPushConstantUpdates;
                bindStats->pushConstantBytes += gBufferPushConstantBytes;
            }
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    if (lightTileCullComputePipeline != nullptr &&
        lightTileCullDescriptorSets != nullptr &&
        lightTileCullGroupCountX > 0 &&
        lightTileCullGroupCountY > 0) {
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            lightTileCullComputePipeline->Handle()
        );

        const VkDescriptorSet frameDescriptorSet =
            lightTileCullDescriptorSets->Handle(imageIndex);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            lightTileCullComputePipeline->Layout(),
            0,
            1,
            &frameDescriptorSet,
            0,
            nullptr
        );

        vkCmdDispatch(
            commandBuffer,
            lightTileCullGroupCountX,
            lightTileCullGroupCountY,
            1
        );
        BarrierComputeLightTilesForFragmentRead(commandBuffer);

        if (bindStats != nullptr) {
            ++bindStats->lightTileCullComputeDispatches;
            ++bindStats->lightTileCullComputeFrameBinds;
            bindStats->lightTileCullComputeGroupsX = lightTileCullGroupCountX;
            bindStats->lightTileCullComputeGroupsY = lightTileCullGroupCountY;
        }
    }

    if (hdrRenderPass != nullptr && hdrFramebuffer != nullptr) {
        VkClearValue hdrClearValue{};
        hdrClearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

        VkRenderPassBeginInfo hdrPassInfo{};
        hdrPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        hdrPassInfo.renderPass = hdrRenderPass->Handle();
        hdrPassInfo.framebuffer = hdrFramebuffer->Handle(imageIndex);
        hdrPassInfo.renderArea.offset = { 0, 0 };
        hdrPassInfo.renderArea.extent = hdrFramebuffer->Extent();
        hdrPassInfo.clearValueCount = 1;
        hdrPassInfo.pClearValues = &hdrClearValue;

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
                bindStats->pushConstantUpdates += deferredPushConstantUpdates;
                bindStats->pushConstantBytes += deferredPushConstantBytes;
            }
        }

        vkCmdEndRenderPass(commandBuffer);
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
            hdrCompositeDescriptorSets != nullptr);
    const bool needsResidualDepth =
        useDeferredCompositeAsMain &&
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
            if (gBufferDebugView == 9) {
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
        const u32 residualDraws = DrawForwardResidualCommands(
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
        hdrCompositeDescriptorSets != nullptr) {
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
            hdrCompositeDescriptorSets->Handle(imageIndex);
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

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        if (bindStats != nullptr) {
            ++bindStats->hdrCompositeDraws;
            ++bindStats->hdrCompositeFrameBinds;
            ++bindStats->hdrCompositeTextureBinds;
        }
        u32 residualMaterialBinds = 0;
        u32 residualMeshBinds = 0;
        u32 residualPushConstantUpdates = 0;
        u64 residualPushConstantBytes = 0;
        const u32 residualDraws = DrawForwardResidualCommands(
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
            mainPushConstantBytes
        );
        ++commandIndex;
        }
    }
    if (bindStats != nullptr) {
        bindStats->mainMaterialBinds += mainMaterialBinds;
        bindStats->mainMeshBinds += mainMeshBinds;
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
