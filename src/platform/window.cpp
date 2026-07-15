#include "window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace se {
    namespace {
        const auto kWindowTraceStartTime = std::chrono::steady_clock::now();

        std::string ReadWindowEnvironmentString(const char* name) {
#ifdef _WIN32
            char* value = nullptr;
            std::size_t valueSize = 0;
            if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
                return {};
            }

            std::string result(value);
            std::free(value);
            return result;
#else
            const char* value = std::getenv(name);
            return value != nullptr ? std::string(value) : std::string{};
#endif
        }

        bool WindowEnvironmentFlagEnabled(const char* name) {
            const std::string value = ReadWindowEnvironmentString(name);
            return value == "1" ||
                value == "true" ||
                value == "TRUE" ||
                value == "on" ||
                value == "ON" ||
                value == "yes" ||
                value == "YES";
        }

        bool WindowEnvironmentFlagDisabled(const char* name) {
            const std::string value = ReadWindowEnvironmentString(name);
            return value == "0" ||
                value == "false" ||
                value == "FALSE" ||
                value == "off" ||
                value == "OFF" ||
                value == "no" ||
                value == "NO";
        }

        bool BorderlessWindowRequested() {
            return WindowEnvironmentFlagEnabled("SE_WINDOW_BORDERLESS") ||
                WindowEnvironmentFlagEnabled("SE_BORDERLESS_FULLSCREEN");
        }

        bool MaximizeBorderlessFullscreenRequested() {
            return !WindowEnvironmentFlagDisabled(
                "SE_MAXIMIZE_BORDERLESS_FULLSCREEN"
            ) && !WindowEnvironmentFlagDisabled(
                "SE_WINDOW_MAXIMIZE_BORDERLESS_FULLSCREEN"
            );
        }

        bool WindowShutdownTraceEnabled() {
            return WindowEnvironmentFlagEnabled("SE_SHUTDOWN_TRACE") ||
                WindowEnvironmentFlagEnabled("SE_WINDOW_SHUTDOWN_TRACE");
        }

        f32 WindowElapsedMilliseconds(std::chrono::steady_clock::time_point startTime) {
            return std::chrono::duration<f32, std::milli>(
                std::chrono::steady_clock::now() - startTime
            ).count();
        }

        int MonitorOverlapArea(
            int windowX,
            int windowY,
            int windowWidth,
            int windowHeight,
            GLFWmonitor* monitor
        ) {
            if (monitor == nullptr) {
                return 0;
            }

            const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
            if (videoMode == nullptr) {
                return 0;
            }

            int monitorX = 0;
            int monitorY = 0;
            glfwGetMonitorPos(monitor, &monitorX, &monitorY);
            const int overlapWidth = std::max(
                0,
                std::min(windowX + windowWidth, monitorX + videoMode->width) -
                    std::max(windowX, monitorX)
            );
            const int overlapHeight = std::max(
                0,
                std::min(windowY + windowHeight, monitorY + videoMode->height) -
                    std::max(windowY, monitorY)
            );
            return overlapWidth * overlapHeight;
        }
    }

    Window::Window(int width, int height, const std::string& title)
        : Window(width, height, title, 0) {
    }

    Window::Window(int width, int height, const std::string& title, int monitorIndex)
        : m_Window(nullptr),
          m_Width(width),
          m_Height(height),
          m_WindowedWidth(width),
          m_WindowedHeight(height),
          m_DecorationForcedOff(BorderlessWindowRequested()),
          m_MaximizeBorderlessFullscreen(MaximizeBorderlessFullscreenRequested()) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        if (m_DecorationForcedOff) {
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        }

        m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        if (!m_Window) {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }

        CenterOnMonitor(monitorIndex);
        glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);

        glfwSetWindowUserPointer(m_Window, this);
        glfwSetFramebufferSizeCallback(m_Window, FramebufferResizeCallback);
        glfwSetWindowMaximizeCallback(m_Window, WindowMaximizeCallback);
        glfwSetWindowPosCallback(m_Window, WindowPosCallback);
        glfwSetWindowSizeCallback(m_Window, WindowSizeCallback);
        glfwSetWindowCloseCallback(m_Window, WindowCloseCallback);
        glfwSetDropCallback(m_Window, DropCallback);
        glfwGetFramebufferSize(m_Window, &m_Width, &m_Height);
    }

    Window::~Window() {
        const bool traceShutdown = WindowShutdownTraceEnabled();
        const auto destructorStartTime = std::chrono::steady_clock::now();
        if (traceShutdown) {
            std::cout << "[shutdown] window destroy_begin +"
                << WindowElapsedMilliseconds(kWindowTraceStartTime) << "ms"
                << std::endl;
        }
        if(m_Window) {
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
        }
        if (traceShutdown) {
            std::cout << "[shutdown] window glfw_destroy_window +"
                << WindowElapsedMilliseconds(destructorStartTime) << "ms"
                << std::endl;
        }

        glfwTerminate();
        if (traceShutdown) {
            std::cout << "[shutdown] window glfw_terminate +"
                << WindowElapsedMilliseconds(destructorStartTime) << "ms"
                << std::endl;
        }
    }

    void Window::PollEvents() {
        glfwPollEvents();
    }

    bool Window::ShouldClose() const {
        return glfwWindowShouldClose(m_Window);
    }

    int Window::GetWidth() const {
        return m_Width;
    }

    int Window::GetHeight() const {
        return m_Height;
    }

    std::array<int, 2> Window::WindowSize() const {
        std::array<int, 2> size{};
        glfwGetWindowSize(m_Window, &size[0], &size[1]);

        return size;
    }

    bool Window::WasLeftMousePressed() {
        const bool isDown = glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool wasPressed = isDown && !m_LeftMouseWasDown;
        m_LeftMouseWasDown = isDown;

        return wasPressed;
    }

    bool Window::IsLeftMouseDown() const {
        return glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    }

    bool Window::IsRightMouseDown() const {
        return glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    }

    bool Window::IsKeyDown(int key) const {
        return glfwGetKey(m_Window, key) == GLFW_PRESS;
    }

    bool Window::WasKeyPressed(int key) {
        const bool isDown = glfwGetKey(m_Window, key) == GLFW_PRESS;
        const bool wasDown = m_KeyWasDown[key];
        m_KeyWasDown[key] = isDown;

        return isDown && !wasDown;
    }

    void Window::SetCursorCaptured(bool captured) {
        glfwSetInputMode(
            m_Window,
            GLFW_CURSOR,
            captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
        );

        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(
                m_Window,
                GLFW_RAW_MOUSE_MOTION,
                captured ? GLFW_TRUE : GLFW_FALSE
            );
        }
    }

    std::array<f64, 2> Window::CursorPosition() const {
        std::array<f64, 2> position{};
        glfwGetCursorPos(m_Window, &position[0], &position[1]);

        return position;
    }

    bool Window::WasResized() const {
        return m_FramebufferResized;
    }

    void Window::ResetResizedFlag() {
        m_FramebufferResized = false;
    }

    void Window::WaitForValidFramebufferSize() {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_Window, &width, &height);

        while ((width == 0 || height == 0) && !glfwWindowShouldClose(m_Window)) {
            glfwWaitEvents();
            glfwGetFramebufferSize(m_Window, &width, &height);
        }

        m_Width = width;
        m_Height = height;
    }

    std::vector<std::filesystem::path> Window::ConsumeDroppedPaths() {
        std::vector<std::filesystem::path> paths;
        paths.swap(m_DroppedPaths);

        return paths;
    }

    GLFWwindow* Window::NativeHandle() const {
        return m_Window;
    }

    void Window::CenterOnMonitor(int monitorIndex) {
        int monitorCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

        if (!monitors || monitorCount == 0) {
            return;
        }

        if (monitorIndex < 0 || monitorIndex >= monitorCount) {
            monitorIndex = 0;
        }

        GLFWmonitor* monitor = monitors[monitorIndex];
        const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
        if (!videoMode) {
            return;
        }

        int monitorX = 0;
        int monitorY = 0;
        glfwGetMonitorPos(monitor, &monitorX, &monitorY);

        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);

        const int windowX = monitorX + (videoMode->width - windowWidth) / 2;
        const int windowY = monitorY + (videoMode->height - windowHeight) / 2;
        glfwSetWindowPos(m_Window, windowX, windowY);
    }

    void Window::CaptureWindowedBoundsIfUsable() {
        if (m_Window == nullptr ||
            m_BorderlessFullscreen ||
            m_ApplyingWindowModeChange ||
            glfwGetWindowAttrib(m_Window, GLFW_MAXIMIZED) == GLFW_TRUE) {
            return;
        }

        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        glfwGetWindowPos(m_Window, &x, &y);
        glfwGetWindowSize(m_Window, &width, &height);
        if (width <= 0 || height <= 0) {
            return;
        }

        GLFWmonitor* monitor = CurrentMonitor();
        if (monitor != nullptr) {
            const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
            int monitorX = 0;
            int monitorY = 0;
            glfwGetMonitorPos(monitor, &monitorX, &monitorY);
            if (videoMode != nullptr &&
                x == monitorX &&
                y == monitorY &&
                width == videoMode->width &&
                height == videoMode->height) {
                return;
            }
        }

        m_WindowedX = x;
        m_WindowedY = y;
        m_WindowedWidth = width;
        m_WindowedHeight = height;
    }

    GLFWmonitor* Window::CurrentMonitor() const {
        if (m_Window == nullptr) {
            return glfwGetPrimaryMonitor();
        }

        int monitorCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        if (monitors == nullptr || monitorCount == 0) {
            return glfwGetPrimaryMonitor();
        }

        int windowX = 0;
        int windowY = 0;
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowPos(m_Window, &windowX, &windowY);
        glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);

        GLFWmonitor* bestMonitor = monitors[0];
        int bestOverlap = -1;
        for (int index = 0; index < monitorCount; ++index) {
            const int overlap = MonitorOverlapArea(
                windowX,
                windowY,
                windowWidth,
                windowHeight,
                monitors[index]
            );
            if (overlap > bestOverlap) {
                bestOverlap = overlap;
                bestMonitor = monitors[index];
            }
        }

        return bestMonitor;
    }

    void Window::EnterBorderlessFullscreen() {
        if (m_Window == nullptr || m_BorderlessFullscreen) {
            return;
        }

        CaptureWindowedBoundsIfUsable();
        GLFWmonitor* monitor = CurrentMonitor();
        if (monitor == nullptr) {
            return;
        }
        const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
        if (videoMode == nullptr) {
            return;
        }

        int monitorX = 0;
        int monitorY = 0;
        glfwGetMonitorPos(monitor, &monitorX, &monitorY);

        m_ApplyingWindowModeChange = true;
        glfwSetWindowAttrib(m_Window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowMonitor(
            m_Window,
            nullptr,
            monitorX,
            monitorY,
            videoMode->width,
            videoMode->height,
            GLFW_DONT_CARE
        );
        glfwGetFramebufferSize(m_Window, &m_Width, &m_Height);
        m_FramebufferResized = true;
        m_BorderlessFullscreen = true;
        m_ApplyingWindowModeChange = false;
    }

    void Window::ExitBorderlessFullscreen() {
        if (m_Window == nullptr || !m_BorderlessFullscreen) {
            return;
        }

        const int restoredWidth = std::max(m_WindowedWidth, 320);
        const int restoredHeight = std::max(m_WindowedHeight, 240);
        m_ApplyingWindowModeChange = true;
        if (!m_DecorationForcedOff) {
            glfwSetWindowAttrib(m_Window, GLFW_DECORATED, GLFW_TRUE);
        }
        glfwSetWindowMonitor(
            m_Window,
            nullptr,
            m_WindowedX,
            m_WindowedY,
            restoredWidth,
            restoredHeight,
            GLFW_DONT_CARE
        );
        glfwGetFramebufferSize(m_Window, &m_Width, &m_Height);
        m_FramebufferResized = true;
        m_BorderlessFullscreen = false;
        m_ApplyingWindowModeChange = false;
    }

    void Window::FramebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height) {
        auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
        window->m_Width = width;
        window->m_Height = height;
        window->m_FramebufferResized = true;
    }

    void Window::WindowMaximizeCallback(GLFWwindow* glfwWindow, int maximized) {
        auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
        if (window == nullptr ||
            window->m_ApplyingWindowModeChange ||
            !window->m_MaximizeBorderlessFullscreen) {
            return;
        }

        if (maximized == GLFW_TRUE) {
            window->EnterBorderlessFullscreen();
        } else {
            window->ExitBorderlessFullscreen();
        }
    }

    void Window::WindowPosCallback(GLFWwindow* glfwWindow, int, int) {
        auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
        if (window != nullptr) {
            window->CaptureWindowedBoundsIfUsable();
        }
    }

    void Window::WindowSizeCallback(GLFWwindow* glfwWindow, int, int) {
        auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
        if (window != nullptr) {
            window->CaptureWindowedBoundsIfUsable();
        }
    }

    void Window::WindowCloseCallback(GLFWwindow* glfwWindow) {
        if (!WindowShutdownTraceEnabled()) {
            return;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(glfwWindow, &width, &height);
        std::cout << "[shutdown] window close_callback +"
            << WindowElapsedMilliseconds(kWindowTraceStartTime) << "ms"
            << " framebuffer=" << width << "x" << height
            << std::endl;
    }

    void Window::DropCallback(GLFWwindow* glfwWindow, int pathCount, const char** paths) {
        auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
        if (window == nullptr || paths == nullptr) {
            return;
        }

        for (int index = 0; index < pathCount; ++index) {
            if (paths[index] != nullptr && paths[index][0] != '\0') {
                window->m_DroppedPaths.emplace_back(paths[index]);
            }
        }
    }
}
