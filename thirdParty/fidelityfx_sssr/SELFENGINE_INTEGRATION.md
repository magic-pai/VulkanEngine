# SelfEngine FidelityFX SSSR Integration

Source:
- AMD GPUOpen-Effects/FidelityFX-SSSR
- Repository: https://github.com/GPUOpen-Effects/FidelityFX-SSSR
- Commit: 34dcacd1feefcfab2855b82e76c7d711f2020a75

Submodule sources:
- AMD GPUOpen-Effects/FidelityFX-Denoiser
- Commit: d7dfecbabe7b9523b14e7b067216e06b86e8d189
- AMD GPUOpen-Effects/FidelityFX-SPD
- Commit: 7c796c6d9fa6a9439e3610478148cfd742d97daf

Included subset:
- `include/ffx-sssr/ffx_sssr.h`
- `include/ffx-dnsr/ffx_denoiser_reflections_*.h`
- `include/ffx-spd/ffx_a.h`
- `include/ffx-spd/ffx_spd.h`
- `shaders/*.hlsl`

SelfEngine compatibility patches:
- `include/ffx-dnsr/ffx_denoiser_reflections_common.h`
  uses `select` for `uint2` round-up instead of a vector ternary.
- `include/ffx-sssr/ffx_sssr.h`
  uses `select` for vector conditions in ray traversal setup.

The patches are semantic-preserving HLSL-to-SPIR-V compatibility changes for
the Vulkan SDK DXC path used by SelfEngine. The original AMD source targets an
older sample toolchain where vector ternaries were accepted.

Current integration state:
- The third-party source and HLSL shader compilation path are part of the
  SelfEngine build.
- The runtime bridge now executes the first two official command-stream passes
  when `SE_SSR_BACKEND=ffx-sssr`: `ClassifyTiles.hlsl` followed by
  `PrepareIndirectArgs.hlsl`.
- SelfEngine creates the vendor-shaped constants buffer, the per-swapchain ray
  counter, ray list, denoiser tile list, extracted roughness image, variance
  placeholder, and intersection output resources. AMD typed `RWBuffer<uint>`
  resources are bound as Vulkan storage texel buffers with
  `VK_FORMAT_R32_UINT`.
- `ClassifyTiles.hlsl` consumes the SelfEngine GBuffer normal/roughness image,
  scene depth, environment prefilter map, and roughness/variance inputs, then
  writes classified rays, denoiser tiles, extracted roughness, and intersection
  placeholders. Its `RWTexture2D<float4>` intersection output is bound as
  `VK_FORMAT_R32G32B32A32_SFLOAT`; Vulkan storage image validation rejects a
  half-float `R16G16B16A16` binding for that SPIR-V `Rgba32f` declaration.
- The runtime bridge is data-gated by
  `scripts\Test-FidelityFxSssrIntegration.ps1`, including source/build checks,
  LightingShowcase and Forward3D FBX FFX lanes, and an internal-backend control
  lane that proves the FFX dispatch is suppressed when not requested.
- Runtime image contribution remains on the existing stable probe/IBL fallback
  until the remaining FidelityFX Intersect, Reproject, Prefilter,
  ResolveTemporal/DNSR, and final resolve path are wired and validated.
