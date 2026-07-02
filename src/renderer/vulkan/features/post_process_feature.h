#pragma once

#include "renderer/vulkan/render_feature.h"

namespace se {

class VulkanPostProcessFeature final : public VulkanRenderFeature {
public:
    void AppendFrameGraph(
        const VulkanRenderFeatureFrameGraphContext& context
    ) const override;
    void WriteStats(
        const VulkanRenderFeatureStatsContext& context
    ) const override;
};

}
