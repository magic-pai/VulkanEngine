#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

enum class VertexLayout {
    Vertex2D,
    Vertex3D,
    Vertex3DInstanced,
    FullscreenTriangle
};

struct PipelineSpec {
    std::string vertexShaderPath;
    std::string fragmentShaderPath;
    std::string instancedVertexShaderPath;
    VertexLayout vertexLayout = VertexLayout::Vertex2D;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
    bool depthTestEnabled = false;
    bool depthWriteEnabled = false;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    bool alphaBlendEnabled = true;
    bool hasFragmentShader = true;
    bool hasColorAttachment = true;
    u32 colorAttachmentCount = 1;
    bool supportsInstancing = false;
    bool dynamicViewportScissor = false;
    VkExtent2D fixedExtent{};

    static PipelineSpec Default2D(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec DefaultForward3D(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec ForwardResidual3D(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec DepthPrefill3D(std::string vertexShaderPath);
    static PipelineSpec GBuffer3D(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec DeferredLighting(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec HdrComposite(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec GBufferDebug(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec ShadowDepth(std::string vertexShaderPath, VkExtent2D extent);
    static PipelineSpec DoubleSided(PipelineSpec spec);
};

}
