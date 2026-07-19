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
- The runtime bridge now executes the first six official command-stream
  passes when `SE_SSR_BACKEND=ffx-sssr`: `ClassifyTiles.hlsl`,
  `PrepareIndirectArgs.hlsl`, `PrepareBlueNoiseTexture.hlsl`,
  `Intersect.hlsl`, `Reproject.hlsl`, and `Prefilter.hlsl`.
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
- `PrepareBlueNoiseTexture.hlsl` uses the official 128x128 1spp Sobol,
  ranking, and scrambling tables extracted from the AMD sample at the commit
  above. The tables are vendored in `data/blue_noise_tables_128x128_1spp.inl`
  and bound as `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER`; the generated
  blue-noise image is `VK_FORMAT_R32G32_SFLOAT` and dispatches as `16x16`
  groups.
- `Intersect.hlsl` consumes the lit HDR scene color, depth pyramid, GBuffer
  normal, extracted roughness, environment cubemap, generated blue noise, ray
  list, and ray counter, then writes the existing `ClassifyTiles` intersection
  output image through the official descriptor layout. The dispatch is indirect
  and uses the `PrepareIndirectArgs` output buffer.
- `Reproject.hlsl` consumes the intersection output, extracted roughness,
  current scene depth, GBuffer normal, motion vectors, generated blue noise,
  denoiser tile list, and bootstrap history images. It writes reprojected
  radiance, average radiance, variance, and sample-count images. The dispatch
  is indirect at byte offset `12`, matching the second three-uint dispatch
  record written by `PrepareIndirectArgs.hlsl`.
- `Prefilter.hlsl` consumes current radiance from the Intersect output,
  average radiance plus variance/sample-count from Reproject, current depth,
  extracted roughness, GBuffer normal, and the denoiser tile list. It writes
  prefiltered radiance, variance, and sample-count carrier images with typed
  Vulkan storage formats (`R32G32B32A32_SFLOAT` and `R32_SFLOAT`). The dispatch
  also uses the denoiser indirect record at byte offset `12`.
- The runtime bridge is data-gated by
  `scripts\Test-FidelityFxSssrIntegration.ps1`, including source/build checks,
  LightingShowcase and Forward3D FBX FFX lanes, and an internal-backend control
  lane that proves the FFX dispatches are suppressed when not requested. The
  gate also requires the FFX FrameGraph resources and official pass split to be
  validation-clean.
- Runtime image contribution remains on the existing stable probe/IBL fallback
  until the remaining FidelityFX ResolveTemporal/DNSR, real history swap, and
  final resolve path are wired and validated. The current Prefilter output is
  intentionally an auditable intermediate, not the final visible reflection.
