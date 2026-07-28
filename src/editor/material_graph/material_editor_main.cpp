#include "editor/material_graph/material_graph_editor.h"

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <d3d11.h>
#include <windows.h>

#include <iterator>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
);

namespace {

ID3D11Device* g_Device = nullptr;
ID3D11DeviceContext* g_DeviceContext = nullptr;
IDXGISwapChain* g_Swapchain = nullptr;
ID3D11RenderTargetView* g_RenderTarget = nullptr;
bool g_SwapchainOccluded = false;
UINT g_ResizeWidth = 0u;
UINT g_ResizeHeight = 0u;

void DestroyRenderTarget() {
    if (g_RenderTarget != nullptr) {
        g_RenderTarget->Release();
        g_RenderTarget = nullptr;
    }
}

bool CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(g_Swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        return false;
    }
    const HRESULT result = g_Device->CreateRenderTargetView(
        backBuffer,
        nullptr,
        &g_RenderTarget
    );
    backBuffer->Release();
    return SUCCEEDED(result);
}

bool CreateDevice(HWND window) {
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2u;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator = 60u;
    description.BufferDesc.RefreshRate.Denominator = 1u;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1u;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL selectedLevel{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0u,
        levels,
        static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION,
        &description,
        &g_Swapchain,
        &g_Device,
        &selectedLevel,
        &g_DeviceContext
    );
    if (result == DXGI_ERROR_UNSUPPORTED) {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0u,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            &description,
            &g_Swapchain,
            &g_Device,
            &selectedLevel,
            &g_DeviceContext
        );
    }
    return SUCCEEDED(result) && CreateRenderTarget();
}

void DestroyDevice() {
    DestroyRenderTarget();
    if (g_Swapchain != nullptr) {
        g_Swapchain->Release();
        g_Swapchain = nullptr;
    }
    if (g_DeviceContext != nullptr) {
        g_DeviceContext->Release();
        g_DeviceContext = nullptr;
    }
    if (g_Device != nullptr) {
        g_Device->Release();
        g_Device = nullptr;
    }
}

LRESULT WINAPI WindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
        return TRUE;
    }

    switch (message) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                g_ResizeWidth = static_cast<UINT>(LOWORD(lParam));
                g_ResizeHeight = static_cast<UINT>(HIWORD(lParam));
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0u) == SC_KEYMENU) {
                return 0;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}

int main() {
    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY)
    );

    WNDCLASSEXW windowClass{
        sizeof(WNDCLASSEXW),
        CS_CLASSDC,
        WindowProcedure,
        0L,
        0L,
        GetModuleHandleW(nullptr),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"SelfEngineMaterialEditorWindow",
        nullptr
    };
    RegisterClassExW(&windowClass);
    HWND window = CreateWindowW(
        windowClass.lpszClassName,
        L"SelfEngine Material Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        static_cast<int>(1180.0f * scale),
        static_cast<int>(720.0f * scale),
        nullptr,
        nullptr,
        windowClass.hInstance,
        nullptr
    );
    if (window == nullptr || !CreateDevice(window)) {
        MessageBoxW(
            nullptr,
            L"The Material Editor could not initialize DirectX 11.",
            L"SelfEngine Material Editor",
            MB_OK | MB_ICONERROR
        );
        DestroyDevice();
        if (window != nullptr) {
            DestroyWindow(window);
        }
        UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
        return 1;
    }

    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.063f, 0.072f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.31f, 0.34f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.13f, 0.27f, 0.29f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.39f, 0.42f, 1.0f);

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(g_Device, g_DeviceContext);

    se::MaterialGraphEditor editor;
    bool done = false;
    while (!done) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        if (g_SwapchainOccluded &&
            g_Swapchain->Present(0u, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10u);
            continue;
        }
        g_SwapchainOccluded = false;

        if (g_ResizeWidth != 0u && g_ResizeHeight != 0u) {
            DestroyRenderTarget();
            g_Swapchain->ResizeBuffers(
                0u,
                g_ResizeWidth,
                g_ResizeHeight,
                DXGI_FORMAT_UNKNOWN,
                0u
            );
            g_ResizeWidth = 0u;
            g_ResizeHeight = 0u;
            if (!CreateRenderTarget()) {
                break;
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        editor.Draw();
        ImGui::Render();

        constexpr float clearColor[4] = { 0.025f, 0.03f, 0.035f, 1.0f };
        g_DeviceContext->OMSetRenderTargets(1u, &g_RenderTarget, nullptr);
        g_DeviceContext->ClearRenderTargetView(g_RenderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT presentResult = g_Swapchain->Present(1u, 0u);
        g_SwapchainOccluded = presentResult == DXGI_STATUS_OCCLUDED;
    }

    editor.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyDevice();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    return 0;
}
