#pragma once

#include "renderer/vulkan/render_feature.h"

namespace se {

class VulkanSsrFeature final : public VulkanRenderFeature {
public:
    void AppendFrameGraph(
        const VulkanRenderFeatureFrameGraphContext& context
    ) const override;
    void WriteStats(
        const VulkanRenderFeatureStatsContext& context
    ) const override;
};

}
