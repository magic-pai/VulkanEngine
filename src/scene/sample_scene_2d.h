#pragma once

#include "renderer/vulkan/mesh.h"

namespace se {

class VulkanCommandPool;
class VulkanDevice;
class VulkanMaterialLibrary;
class VulkanPhysicalDevice;
class VulkanRenderResources2D;
class Scene2D;

class SampleScene2D {
public:
    SampleScene2D(
        const VulkanDevice& device,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanCommandPool& commandPool,
        VulkanMaterialLibrary& materialLibrary,
        VulkanRenderResources2D& renderResources,
        Scene2D& scene,
        std::string assetDirectory
    );

    ~SampleScene2D() = default;

    SE_DISABLE_COPY(SampleScene2D);
    SE_DISABLE_MOVE(SampleScene2D);

private:
    VulkanMesh m_QuadMesh;
};

}
