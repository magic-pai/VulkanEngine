#include "window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace se {
    Window::Window(int width, int height, const std::string& title)
        : Window(width, height, title, 0) {
    }

    Window::Window(int width, int height, const std::string& title, int monitorIndex)
        : m_Window(nullptr), m_Width(width), m_Height(height) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        if (!m_Window) {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }

        CenterOnMonitor(monitorIndex);

        glfwSetWindowUserPointer(m_Window, this);
        glfwSetFramebufferSizeCallback(m_Window, FramebufferResizeCallback);
        glfwSetDropCallback(m_Window, DropCallback);
        glfwGetFramebufferSize(m_Window, &m_Width, &m_Height);
    }

    Window::~Window() {
        if(m_Window) {
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
        }

        glfwTerminate();
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

    void Window::FramebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height) {
        auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
        window->m_Width = width;
        window->m_Height = height;
        window->m_FramebufferResized = true;
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
