#pragma once

#include "renderer/vulkan/render_feature.h"

#include <memory>
#include <vector>

namespace se {

class VulkanRenderFeatureRegistry {
public:
    void Add(std::unique_ptr<VulkanRenderFeature> feature);
    void AppendFrameGraph(
        const VulkanRenderFeatureFrameGraphContext& context
    ) const;
    void WriteStats(const VulkanRenderFeatureStatsContext& context) const;

private:
    std::vector<std::unique_ptr<VulkanRenderFeature>> m_Features;
};

}
