#pragma once

#include "platform/window.h"
#include "renderer/vulkan/command_pool.h"
#include "renderer/vulkan/device.h"
#include "renderer/vulkan/instance.h"
#include "renderer/vulkan/material_library.h"
#include "renderer/vulkan/physical_device.h"
#include "renderer/vulkan/pipeline_spec.h"
#include "renderer/vulkan/render_resources_2d.h"
#include "renderer/vulkan/renderer.h"
#include "renderer/vulkan/surface.h"

#include <chrono>
#include <functional>

namespace se {

class Camera2D;
class Scene2D;

class Application {
public:
    using UpdateCallback = std::function<void(float deltaSeconds, float elapsedSeconds)>;

    Application(
        int width,
        int height,
        std::string title,
        int monitorIndex,
        PipelineSpec pipelineSpec
    );
    ~Application();

    SE_DISABLE_COPY(Application);
    SE_DISABLE_MOVE(Application);

    Window& WindowHandle();
    VulkanDevice& Device();
    VulkanPhysicalDevice& PhysicalDevice();
    VulkanCommandPool& CommandPool();
    VulkanMaterialLibrary& MaterialLibrary();
    VulkanRenderResources2D& RenderResources();
    VulkanRenderer* Renderer();

    void CreateRenderer();
    void Run(UpdateCallback update);

protected:
    void DestroyRenderer();
    virtual Scene2D* Scene2DForRenderer();
    virtual Camera2D* Camera2DForRenderer();

private:
    Window m_Window;
    VulkanInstance m_Instance;
    VulkanSurface m_Surface;
    VulkanPhysicalDevice m_PhysicalDevice;
    VulkanDevice m_Device;
    VulkanCommandPool m_CommandPool;
    VulkanMaterialLibrary m_MaterialLibrary;
    VulkanRenderResources2D m_RenderResources;
    PipelineSpec m_PipelineSpec;
    std::unique_ptr<VulkanRenderer> m_Renderer;
};

}
