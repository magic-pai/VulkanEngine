#pragma once

#include "renderer/vulkan/render_feature.h"

namespace se {

class VulkanReflectionProbeFallbackFeature final : public VulkanRenderFeature {
public:
    void AppendFrameGraph(
        const VulkanRenderFeatureFrameGraphContext& context
    ) const override;
    void WriteStats(
        const VulkanRenderFeatureStatsContext& context
    ) const override;
};

}
