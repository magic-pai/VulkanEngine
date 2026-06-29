# Vulkan Environment

This workspace is set up as a learning project, not a generated engine. The scripts here only prepare and check the toolchain so you can write each Vulkan step yourself.

## Current Machine Status

- Workspace: `D:\VSproject\SelfEngine`
- Vulkan SDK: `D:\VulkanSDK\1.4.350.0`
- Vulkan runtime check: `vulkaninfo --summary` succeeds
- GPU seen by Vulkan: `NVIDIA GeForce RTX 5070`
- Vulkan validation layer: `VK_LAYER_KHRONOS_validation` is available
- Visual Studio Build Tools 2022 C++ toolchain is installed
- CMake is installed through Visual Studio Build Tools: `3.31.6-msvc6`
- Ninja is installed through Visual Studio Build Tools: `1.12.1`

The only wrinkle is that MSVC, CMake, and Ninja are not globally on `PATH`. Use the script below to load them for the current PowerShell session.

## Daily Setup

From the workspace root:

```powershell
.\scripts\Enter-VulkanDev.ps1
.\scripts\Test-VulkanEnvironment.ps1
```

If PowerShell blocks scripts in a fresh terminal, allow scripts only for that terminal:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

## Third-Party Libraries

Already present:

- GLFW 3.4 headers: `thirdParty\include\GLFW`
- GLFW static library: `thirdParty\lib\glfw3.lib`
- GLM headers: `thirdParty\include\glm`
- Assimp headers: `thirdParty\include\assimp`
- SDL headers: `thirdParty\include\sdl`

Deferred until later:

- Assimp libraries are not present. You only need them when you start loading models.
- `zlibstaticd.lib` is also absent, and may be needed by a matching debug Assimp build.
- RenderDoc is not installed. It is optional at first, but worth installing before serious frame debugging.

## Recommended Learning Stack

Use this order for the first pass:

1. MSVC + CMake + Ninja
2. GLFW for window creation and Vulkan surfaces
3. GLM for math
4. Vulkan SDK tools: `glslc`, `vkconfig`, validation layers, `vulkaninfo`
5. RenderDoc once you reach your first triangle or swapchain bugs
6. Assimp only after buffers, descriptors, textures, and camera movement are comfortable

## What Is Not Needed Yet

- OpenGL loaders such as GLAD
- SDL, unless you decide to use SDL instead of GLFW
- Assimp, before model loading
- An editor, material system, ECS, job system, or asset pipeline

Those are engine topics. The first milestone is smaller: create a window, create a Vulkan instance, enable validation, create a surface, select a GPU, create a logical device, and understand every line you wrote.

