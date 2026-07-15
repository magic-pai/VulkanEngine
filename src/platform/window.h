#pragma once

#include "core.h"

#include <filesystem>
#include <unordered_map>

struct GLFWwindow;
struct GLFWmonitor;

namespace se {
    class Window {
    public:
        Window(int width, int height, const std::string& title);
        Window(int width, int height, const std::string& title, int monitorIndex);
        ~Window();

        void PollEvents();
        bool ShouldClose() const;

        int GetWidth() const;
        int GetHeight() const;
        std::array<int, 2> WindowSize() const;
        bool WasLeftMousePressed();
        bool IsLeftMouseDown() const;
        bool IsRightMouseDown() const;
        bool IsKeyDown(int key) const;
        bool WasKeyPressed(int key);
        void SetCursorCaptured(bool captured);
        std::array<f64, 2> CursorPosition() const;
        bool WasResized() const;
        void ResetResizedFlag();
        void WaitForValidFramebufferSize();
        std::vector<std::filesystem::path> ConsumeDroppedPaths();
        GLFWwindow* NativeHandle() const;

    private:
        void CenterOnMonitor(int monitorIndex);
        void CaptureWindowedBoundsIfUsable();
        void EnterBorderlessFullscreen();
        void ExitBorderlessFullscreen();
        GLFWmonitor* CurrentMonitor() const;
        static void FramebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height);
        static void WindowMaximizeCallback(GLFWwindow* glfwWindow, int maximized);
        static void WindowPosCallback(GLFWwindow* glfwWindow, int x, int y);
        static void WindowSizeCallback(GLFWwindow* glfwWindow, int width, int height);
        static void WindowCloseCallback(GLFWwindow* glfwWindow);
        static void DropCallback(GLFWwindow* glfwWindow, int pathCount, const char** paths);

    private:
        GLFWwindow* m_Window;
        int m_Width;
        int m_Height;
        int m_WindowedX = 0;
        int m_WindowedY = 0;
        int m_WindowedWidth = 0;
        int m_WindowedHeight = 0;
        bool m_FramebufferResized = false;
        bool m_LeftMouseWasDown = false;
        std::unordered_map<int, bool> m_KeyWasDown;
        bool m_DecorationForcedOff = false;
        bool m_MaximizeBorderlessFullscreen = true;
        bool m_BorderlessFullscreen = false;
        bool m_ApplyingWindowModeChange = false;
        std::vector<std::filesystem::path> m_DroppedPaths;
    };
}
