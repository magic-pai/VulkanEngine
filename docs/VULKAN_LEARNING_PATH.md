# Vulkan Engine Learning Path

Goal: learn Vulkan by building a renderer one small, explainable step at a time. I will guide, review, debug, and ask questions, but you should write the core code yourself.

## Rule Of Thumb

Do not start with an engine architecture. Start with working Vulkan facts. Every abstraction should appear only after you have written the thing twice and can name what it hides.

## Phase 0: Environment

Do:

- Run `.\scripts\Test-VulkanEnvironment.ps1`
- Confirm `vulkaninfo --summary` sees your GPU
- Confirm `glslc`, `cmake`, `ninja`, and `cl` are available

You are done when the script says the required environment is ready.

## Phase 1: CMake And A Plain Window

Write yourself:

- `CMakeLists.txt`
- `src/main.cpp`
- A GLFW window with `GLFW_CLIENT_API` set to `GLFW_NO_API`

Learn:

- How CMake finds include directories and `.lib` files
- Why Vulkan owns rendering instead of GLFW or OpenGL
- How Windows static libraries differ from headers

Checkpoint: a blank window opens and closes cleanly.

## Phase 2: Instance And Validation

Write yourself:

- `VkApplicationInfo`
- `VkInstanceCreateInfo`
- `vkCreateInstance`
- validation layer enablement in debug builds
- `VK_EXT_debug_utils` messenger

Learn:

- instance extensions vs device extensions
- validation layers vs drivers
- why Vulkan returns `VkResult` instead of throwing exceptions

Checkpoint: validation messages appear when you intentionally make a small mistake.

## Phase 3: Surface, Physical Device, Queues

Write yourself:

- `glfwCreateWindowSurface`
- physical device enumeration
- queue family selection for graphics and presentation
- logical device creation

Learn:

- why presentation is not the same as graphics
- what a queue family represents
- how to reject unsuitable GPUs

Checkpoint: your program prints the selected GPU and queue family indices.

## Phase 4: Swapchain

Write yourself:

- swapchain support query
- surface format selection
- present mode selection
- extent selection
- swapchain images and image views

Learn:

- why the swapchain is owned by the presentation engine
- why resize handling matters
- why `VK_KHR_swapchain` is a device extension

Checkpoint: swapchain creation succeeds and cleans up without validation errors.

## Phase 5: Commands And Synchronization

Write yourself:

- command pool
- command buffers
- fences
- semaphores
- acquire, submit, present loop

Learn:

- CPU waiting vs GPU waiting
- binary semaphores vs fences
- why frames-in-flight exist

Checkpoint: your loop presents frames without drawing yet.

## Phase 6: First Triangle

Write yourself:

- vertex and fragment shaders
- shader compilation through `glslc`
- pipeline layout
- graphics pipeline
- dynamic rendering or a render pass path, chosen deliberately
- draw command recording

Learn:

- shader modules vs pipeline objects
- viewport/scissor state
- color attachment layouts

Checkpoint: a triangle appears. This is the first real win.

## Phase 7: Engine Shape

Only after the triangle, start extracting modules:

- `PlatformWindow`
- `VulkanInstance`
- `VulkanDevice`
- `Swapchain`
- `CommandContext`
- `Renderer`

Learn:

- resource lifetime ownership
- RAII wrappers
- why destruction order matters

Checkpoint: the same triangle renders, but the code is easier to navigate.

## Phase 8: Real Rendering Features

Add in this order:

- vertex and index buffers
- staging buffers
- uniform buffers
- descriptor sets
- textures and samplers
- depth buffer
- camera
- model loading with Assimp

Checkpoint: a textured mesh renders with a movable camera.

## How We Work Together

For each phase, you can ask me for:

- the concept before you code
- a tiny exercise
- review of your implementation
- help reading validation layer errors
- help designing the next abstraction after the code works

If you paste a compile error or validation message, I will help you trace it instead of replacing your code wholesale.

