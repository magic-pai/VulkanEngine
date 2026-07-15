#pragma once

#include "renderer/vulkan/vulkan_common.h"

#include <functional>

namespace se {

class Camera2D;
class Camera3D;
class VulkanDevice;
class VulkanPhysicalDevice;
class VulkanRenderPass;
class VulkanRenderResources2D;
class VulkanSwapchain;
class Scene2D;
class Scene3D;
struct VulkanRenderDebugSettings;
struct RendererStats;
struct VulkanShadowSettings;
class Window;

class VulkanImGuiLayer {
public:
    using TemporalAntialiasingModeCallback = std::function<void(u32)>;

    VulkanImGuiLayer(
        Window& window,
        VkInstance instance,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanDevice& device,
        const VulkanRenderPass& renderPass,
        const VulkanSwapchain& swapchain
    );

    ~VulkanImGuiLayer();

    SE_DISABLE_COPY(VulkanImGuiLayer);
    SE_DISABLE_MOVE(VulkanImGuiLayer);

    void BeginFrame(
        Scene2D* scene = nullptr,
        Camera2D* camera = nullptr,
        Scene3D* scene3D = nullptr,
        Camera3D* camera3D = nullptr,
        const VulkanRenderResources2D* renderResources = nullptr,
        const RendererStats* rendererStats = nullptr,
        VulkanRenderDebugSettings* renderDebugSettings = nullptr,
        VulkanShadowSettings* shadowSettings = nullptr,
        u32 temporalAntialiasingMode = 0,
        TemporalAntialiasingModeCallback temporalAntialiasingModeCallback = {}
    );
    void Render(VkCommandBuffer commandBuffer);
    void OnSwapchainRecreated(const VulkanSwapchain& swapchain);

private:
    void CreateContext(Window& window);
    void InitializeVulkanBackend(
        VkInstance instance,
        const VulkanPhysicalDevice& physicalDevice,
        const VulkanDevice& device,
        const VulkanRenderPass& renderPass,
        const VulkanSwapchain& swapchain
    );

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    bool m_Initialized = false;
};

}
