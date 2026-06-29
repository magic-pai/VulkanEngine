# Mission: Vulkan Rendering Engine

## Why

You want to learn Vulkan engine programming by writing the renderer yourself, not by receiving a finished engine. The goal is to build real understanding of the graphics pipeline, GPU resource lifetime, synchronization, and the shape of a small rendering engine.

## Success looks like

- You can set up and verify the Windows Vulkan development environment.
- You can create a GLFW window and Vulkan surface from scratch.
- You can enable validation layers and understand their messages.
- You can build up to a triangle, then buffers, textures, depth, camera, and model loading.
- You can explain why each engine module exists before extracting it.

## Constraints

- Use Windows in `D:\VSproject\SelfEngine`.
- Use the existing Vulkan SDK at `D:\VulkanSDK\1.4.350.0`.
- Prefer the local `thirdParty` folder for GLFW and GLM.
- Codex should guide, review, and debug, not directly write the whole engine for you.

## Out of scope

- Full engine architecture before the first triangle.
- Model loading before Vulkan buffers and descriptors are understood.
- Editor tooling, ECS, scripting, animation, and asset pipeline until the renderer fundamentals are in place.

