#include "renderer/vulkan/pipeline_spec.h"

#include <filesystem>
#include <utility>

namespace se {

PipelineSpec PipelineSpec::Default2D(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec{};
    spec.vertexShaderPath = std::move(vertexShaderPath);
    spec.fragmentShaderPath = std::move(fragmentShaderPath);
    return spec;
}

PipelineSpec PipelineSpec::DefaultForward3D(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec{};
    spec.vertexShaderPath = std::move(vertexShaderPath);
    spec.fragmentShaderPath = std::move(fragmentShaderPath);
    const std::filesystem::path vertexPath(spec.vertexShaderPath);
    spec.instancedVertexShaderPath =
        (vertexPath.parent_path() / "forward_3d_instanced.vert.spv").string();
    spec.vertexLayout = VertexLayout::Vertex3D;
    spec.supportsInstancing = true;
    spec.cullMode = VK_CULL_MODE_BACK_BIT;
    spec.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    spec.depthTestEnabled = true;
    spec.depthWriteEnabled = true;
    spec.alphaBlendEnabled = false;
    spec.colorBlendModes[0] = ColorBlendMode::Disabled;
    return spec;
}

PipelineSpec PipelineSpec::ForwardResidual3D(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DefaultForward3D(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    spec.supportsInstancing = false;
    spec.instancedVertexShaderPath.clear();
    spec.depthWriteEnabled = false;
    spec.alphaBlendEnabled = true;
    spec.colorBlendModes[0] = ColorBlendMode::Alpha;
    return spec;
}

PipelineSpec PipelineSpec::ForwardResidualVelocity3D(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DefaultForward3D(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    spec.supportsInstancing = false;
    spec.instancedVertexShaderPath.clear();
    spec.depthWriteEnabled = false;
    spec.alphaBlendEnabled = false;
    spec.colorAttachmentCount = 1;
    spec.colorBlendModes[0] = ColorBlendMode::Disabled;
    spec.dynamicViewportScissor = true;
    spec.vertexLayout = VertexLayout::Vertex3DSkinned;
    return spec;
}

PipelineSpec PipelineSpec::DlssMask3D(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DefaultForward3D(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    spec.supportsInstancing = false;
    spec.instancedVertexShaderPath.clear();
    spec.depthWriteEnabled = false;
    spec.alphaBlendEnabled = false;
    spec.colorAttachmentCount = 2;
    spec.colorBlendModes[0] = ColorBlendMode::Disabled;
    spec.colorBlendModes[1] = ColorBlendMode::Disabled;
    spec.dynamicViewportScissor = true;
    spec.vertexLayout = VertexLayout::Vertex3DSkinned;
    return spec;
}

PipelineSpec PipelineSpec::WeightedTranslucency3D(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DefaultForward3D(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    spec.supportsInstancing = false;
    spec.instancedVertexShaderPath.clear();
    spec.depthWriteEnabled = false;
    spec.alphaBlendEnabled = false;
    spec.colorAttachmentCount = 2;
    spec.colorBlendModes[0] = ColorBlendMode::Additive;
    spec.colorBlendModes[1] = ColorBlendMode::ZeroSource;
    spec.dynamicViewportScissor = true;
    return spec;
}

PipelineSpec PipelineSpec::DepthPrefill3D(std::string vertexShaderPath) {
    PipelineSpec spec{};
    spec.vertexShaderPath = std::move(vertexShaderPath);
    spec.vertexLayout = VertexLayout::Vertex3D;
    spec.cullMode = VK_CULL_MODE_BACK_BIT;
    spec.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    spec.depthTestEnabled = true;
    spec.depthWriteEnabled = true;
    spec.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    spec.alphaBlendEnabled = false;
    spec.hasFragmentShader = false;
    spec.hasColorAttachment = true;
    spec.colorAttachmentCount = 1;
    spec.colorBlendModes[0] = ColorBlendMode::Disabled;
    return spec;
}

PipelineSpec PipelineSpec::GBuffer3D(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DefaultForward3D(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    spec.supportsInstancing = false;
    spec.instancedVertexShaderPath.clear();
    spec.vertexLayout = VertexLayout::Vertex3DSkinned;
    spec.colorAttachmentCount = 6;
    spec.colorBlendModes[0] = ColorBlendMode::Disabled;
    spec.dynamicViewportScissor = true;
    return spec;
}

PipelineSpec PipelineSpec::DeferredLighting(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec{};
    spec.vertexShaderPath = std::move(vertexShaderPath);
    spec.fragmentShaderPath = std::move(fragmentShaderPath);
    spec.vertexLayout = VertexLayout::FullscreenTriangle;
    spec.cullMode = VK_CULL_MODE_NONE;
    spec.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    spec.depthTestEnabled = false;
    spec.depthWriteEnabled = false;
    spec.alphaBlendEnabled = false;
    spec.colorAttachmentCount = 1;
    spec.colorBlendModes[0] = ColorBlendMode::Disabled;
    spec.dynamicViewportScissor = true;
    return spec;
}

PipelineSpec PipelineSpec::FidelityFxSssrApply(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DeferredLighting(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    spec.alphaBlendEnabled = true;
    spec.colorBlendModes[0] = ColorBlendMode::DestinationAlphaAdditive;
    return spec;
}

PipelineSpec PipelineSpec::HdrComposite(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DeferredLighting(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    return spec;
}

PipelineSpec PipelineSpec::BloomPyramid(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DeferredLighting(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    spec.dynamicViewportScissor = true;
    return spec;
}

PipelineSpec PipelineSpec::BloomUpsample(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = BloomPyramid(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    spec.alphaBlendEnabled = true;
    spec.colorBlendModes[0] = ColorBlendMode::Additive;
    return spec;
}

PipelineSpec PipelineSpec::WeightedTranslucencyResolve(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DeferredLighting(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    spec.alphaBlendEnabled = true;
    spec.colorBlendModes[0] = ColorBlendMode::Alpha;
    return spec;
}

PipelineSpec PipelineSpec::GBufferDebug(
    std::string vertexShaderPath,
    std::string fragmentShaderPath
) {
    PipelineSpec spec = DeferredLighting(
        std::move(vertexShaderPath),
        std::move(fragmentShaderPath)
    );
    return spec;
}

PipelineSpec PipelineSpec::ShadowDepth(std::string vertexShaderPath, VkExtent2D extent) {
    PipelineSpec spec{};
    spec.vertexShaderPath = std::move(vertexShaderPath);
    spec.vertexLayout = VertexLayout::Vertex3DSkinned;
    spec.cullMode = VK_CULL_MODE_BACK_BIT;
    spec.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    spec.depthTestEnabled = true;
    spec.depthWriteEnabled = true;
    spec.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    spec.alphaBlendEnabled = false;
    spec.hasFragmentShader = false;
    spec.hasColorAttachment = false;
    spec.colorAttachmentCount = 0;
    spec.dynamicViewportScissor = true;
    spec.dynamicDepthBias = true;
    spec.fixedExtent = extent;
    return spec;
}

PipelineSpec PipelineSpec::DoubleSided(PipelineSpec spec) {
    spec.cullMode = VK_CULL_MODE_NONE;
    return spec;
}

}
