#pragma once

#include "core.h"

#include <filesystem>

struct GLFWwindow;

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
        void SetCursorCaptured(bool captured);
        std::array<f64, 2> CursorPosition() const;
        bool WasResized() const;
        void ResetResizedFlag();
        void WaitForValidFramebufferSize();
        std::vector<std::filesystem::path> ConsumeDroppedPaths();
        GLFWwindow* NativeHandle() const;

    private:
        void CenterOnMonitor(int monitorIndex);
        static void FramebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height);
        static void DropCallback(GLFWwindow* glfwWindow, int pathCount, const char** paths);

    private:
        GLFWwindow* m_Window;
        int m_Width;
        int m_Height;
        bool m_FramebufferResized = false;
        bool m_LeftMouseWasDown = false;
        std::vector<std::filesystem::path> m_DroppedPaths;
    };
}
