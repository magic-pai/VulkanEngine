#pragma once

#include "renderer/vulkan/uniform_buffer.h"

#include <unordered_map>

namespace se {

class VulkanMaterial;

struct FrameMaterialSet {
    MaterialBufferObject materialData{};
    std::unordered_map<const VulkanMaterial*, u32> materialIds;
    u32 count = 0;
    u32 overflowCount = 0;
    u32 opaqueCount = 0;
    u32 transparentCount = 0;
    u32 forwardSpecialCount = 0;
    u32 emissiveHintCount = 0;
    u32 specularHintCount = 0;
    u32 specularTextureCount = 0;
    u32 alphaMaskCount = 0;
    u32 alphaBlendCount = 0;
    u32 uvTransformCount = 0;
    u32 doubleSidedCount = 0;
    u32 clearcoatCount = 0;
    u32 clearcoatTextureCount = 0;
    u32 clearcoatRoughnessTextureCount = 0;
    u32 transmissionCount = 0;
    u32 transmissionTextureCount = 0;
    u32 volumeCount = 0;
    u32 opacityTextureCount = 0;
    u32 texturedCount = 0;

    u32 IdFor(const VulkanMaterial* material) const {
        if (material == nullptr) {
            return 0;
        }

        const auto found = materialIds.find(material);
        if (found == materialIds.end()) {
            return 0;
        }

        return found->second;
    }
};

}
