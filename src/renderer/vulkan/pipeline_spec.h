#pragma once

#include "renderer/vulkan/vulkan_common.h"

namespace se {

enum class VertexLayout {
    Vertex2D,
    Vertex3D,
    Vertex3DSkinned,
    Vertex3DInstanced,
    FullscreenTriangle
};

enum class ColorBlendMode {
    Disabled,
    Alpha,
    Additive,
    ZeroSource
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
    std::array<ColorBlendMode, 8> colorBlendModes{};
    bool supportsInstancing = false;
    bool dynamicViewportScissor = false;
    bool dynamicDepthBias = false;
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
    static PipelineSpec ForwardResidualVelocity3D(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec DlssMask3D(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec WeightedTranslucency3D(
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
    static PipelineSpec BloomPyramid(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec BloomUpsample(
        std::string vertexShaderPath,
        std::string fragmentShaderPath
    );
    static PipelineSpec WeightedTranslucencyResolve(
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
