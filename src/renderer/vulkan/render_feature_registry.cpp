#include "renderer/vulkan/render_feature_registry.h"

namespace se {

void VulkanRenderFeatureRegistry::Add(
    std::unique_ptr<VulkanRenderFeature> feature
) {
    if (feature != nullptr) {
        m_Features.push_back(std::move(feature));
    }
}

void VulkanRenderFeatureRegistry::AppendFrameGraph(
    const VulkanRenderFeatureFrameGraphContext& context
) const {
    for (const std::unique_ptr<VulkanRenderFeature>& feature : m_Features) {
        feature->AppendFrameGraph(context);
    }
}

void VulkanRenderFeatureRegistry::WriteStats(
    const VulkanRenderFeatureStatsContext& context
) const {
    for (const std::unique_ptr<VulkanRenderFeature>& feature : m_Features) {
        feature->WriteStats(context);
    }
}

}
