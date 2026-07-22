#include "app/application.h"

#include "app/benchmark_recorder.h"
#if defined(_DEBUG)
#include "app/renderdoc_capture.h"
#endif

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace se {

namespace {

int ReadAutoExitFrameCount() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, "SE_AUTO_EXIT_FRAMES") != 0 || value == nullptr) {
        return 0;
    }

    const int frameCount = std::atoi(value);
    std::free(value);
    return frameCount;
#else
    const char* value = std::getenv("SE_AUTO_EXIT_FRAMES");
    if (!value) {
        return 0;
    }

    return std::atoi(value);
#endif
}

bool ReadShutdownTraceEnabled() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, "SE_SHUTDOWN_TRACE") != 0 || value == nullptr) {
        return false;
    }

    const std::string text(value);
    std::free(value);
#else
    const char* rawValue = std::getenv("SE_SHUTDOWN_TRACE");
    if (!rawValue) {
        return false;
    }
    const std::string text(rawValue);
#endif
    return text == "1" ||
        text == "true" ||
        text == "TRUE" ||
        text == "on" ||
        text == "ON" ||
        text == "yes" ||
        text == "YES";
}

float ElapsedSeconds(std::chrono::steady_clock::time_point startTime) {
    using Seconds = std::chrono::duration<float>;
    return std::chrono::duration_cast<Seconds>(
        std::chrono::steady_clock::now() - startTime
    ).count();
}

float ElapsedMilliseconds(std::chrono::steady_clock::time_point startTime) {
    using Milliseconds = std::chrono::duration<float, std::milli>;
    return std::chrono::duration_cast<Milliseconds>(
        std::chrono::steady_clock::now() - startTime
    ).count();
}

}

Application::Application(
    int width,
    int height,
    std::string title,
    int monitorIndex,
    PipelineSpec pipelineSpec
) : m_Window(width, height, std::move(title), monitorIndex),
    m_Surface(m_Instance, m_Window),
    m_PhysicalDevice(m_Instance, m_Surface),
    m_Device(m_PhysicalDevice),
    m_CommandPool(m_Device, m_PhysicalDevice),
    m_MaterialLibrary(m_Device, m_PhysicalDevice, m_CommandPool),
    m_PipelineSpec(std::move(pipelineSpec)) {
}

Application::~Application() {
    DestroyRenderer();
}

Window& Application::WindowHandle() {
    return m_Window;
}

VulkanDevice& Application::Device() {
    return m_Device;
}

VulkanPhysicalDevice& Application::PhysicalDevice() {
    return m_PhysicalDevice;
}

VulkanCommandPool& Application::CommandPool() {
    return m_CommandPool;
}

VulkanMaterialLibrary& Application::MaterialLibrary() {
    return m_MaterialLibrary;
}

VulkanRenderResources2D& Application::RenderResources() {
    return m_RenderResources;
}

VulkanRenderer* Application::Renderer() {
    return m_Renderer.get();
}

void Application::CreateRenderer() {
    SE_ASSERT(m_Renderer == nullptr, "Application renderer was already created");
    m_Renderer = std::make_unique<VulkanRenderer>(
        m_Window,
        m_Device,
        m_PhysicalDevice,
        m_Surface,
        m_Instance.Handle(),
        m_CommandPool,
        Scene2DForRenderer(),
        Camera2DForRenderer(),
        m_RenderResources,
        m_PipelineSpec
    );
}

void Application::DestroyRenderer() {
    m_Renderer.reset();
}

void Application::Run(UpdateCallback update) {
    SE_ASSERT(m_Renderer != nullptr, "Application renderer must be created before Run");

    const int autoExitFrameCount = ReadAutoExitFrameCount();
    const bool traceShutdown = ReadShutdownTraceEnabled();
    BenchmarkRecorder benchmark(BenchmarkRecorder::ConfigFromEnvironment());
#if defined(_DEBUG)
    RenderDocCaptureController renderDocCapture;
#endif
    int renderedFrameCount = 0;
    const auto startTime = std::chrono::steady_clock::now();
    auto previousFrameTime = startTime;

    while (!m_Window.ShouldClose()) {
        m_Window.PollEvents();
        if (m_Window.ShouldClose()) {
            break;
        }

        const auto currentFrameTime = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(
            currentFrameTime - previousFrameTime
        ).count();
        previousFrameTime = currentFrameTime;

        if (update) {
            update(deltaSeconds, ElapsedSeconds(startTime));
        }

#if defined(_DEBUG)
        const u32 nextRenderedFrame = static_cast<u32>(renderedFrameCount + 1);
        renderDocCapture.BeginFrame(nextRenderedFrame);
#endif
        m_Renderer->DrawFrame();
#if defined(_DEBUG)
        renderDocCapture.EndFrame(nextRenderedFrame);
#endif
        ++renderedFrameCount;
        benchmark.RecordFrame(
            static_cast<u32>(renderedFrameCount),
            ElapsedSeconds(startTime),
            m_Renderer->Stats()
        );

        if (benchmark.ShouldStop() ||
            (autoExitFrameCount > 0 && renderedFrameCount >= autoExitFrameCount)) {
            if (traceShutdown) {
                std::cout << "[shutdown] application break_requested frame="
                    << renderedFrameCount << " elapsed_ms="
                    << ElapsedMilliseconds(startTime) << std::endl;
            }
            break;
        }
    }

    const auto waitIdleStartTime = std::chrono::steady_clock::now();
    if (traceShutdown) {
        std::cout << "[shutdown] application run_loop_end frame="
            << renderedFrameCount << " elapsed_ms="
            << ElapsedMilliseconds(startTime) << std::endl;
    }
    m_Renderer->WaitIdle();
    if (traceShutdown) {
        std::cout << "[shutdown] application wait_idle +"
            << ElapsedMilliseconds(waitIdleStartTime) << "ms" << std::endl;
    }
}

Scene2D* Application::Scene2DForRenderer() {
    return nullptr;
}

Camera2D* Application::Camera2DForRenderer() {
    return nullptr;
}

}
