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
- `shaders/Reproject.hlsl` declares the two RGB radiance storage outputs as
  `RWTexture2D<float4>` and stores `value.xyzz`. This keeps the AMD RGB
  radiance algorithm unchanged while matching the RGBA32F Vulkan storage image
  used by SelfEngine; RGB32F sampled/storage images are not portable on the
  current device.

The patches are semantic-preserving HLSL-to-SPIR-V compatibility changes for
the Vulkan SDK DXC path used by SelfEngine. The original AMD source targets an
older sample toolchain where vector ternaries were accepted.

Current integration state:
- The third-party source and HLSL shader compilation path are part of the
  SelfEngine build.
- The runtime bridge now executes the first seven command-stream
  passes when `SE_SSR_BACKEND=ffx-sssr`: `ClassifyTiles.hlsl`,
  `PrepareIndirectArgs.hlsl`, `PrepareBlueNoiseTexture.hlsl`,
  `Intersect.hlsl`, `Reproject.hlsl`, `Prefilter.hlsl`, and
  `ResolveTemporal.hlsl`.
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
- `Reproject.hlsl` consumes SelfEngine motion vectors as UV-space
  `currentUv - previousUv`, matching the GBuffer/TAA/SSR temporal contract.
  The constants buffer records motion-vector mode `1` and scale `1,1` by
  default. The legacy AMD-sample-style NDC conversion `0.5,-0.5` remains only
  as the Debug control `SE_SSR_FFX_MOTION_VECTOR_MODE=legacy-ndc`.
- `Prefilter.hlsl` consumes current radiance from the Intersect output,
  average radiance plus variance/sample-count from Reproject, current depth,
  extracted roughness, GBuffer normal, and the denoiser tile list. It writes
  prefiltered radiance, variance, and sample-count carrier images with typed
  Vulkan storage formats (`R32G32B32A32_SFLOAT` and `R32_SFLOAT`). The dispatch
  also uses the denoiser indirect record at byte offset `12`.
- `ResolveTemporal.hlsl` consumes extracted roughness, Reproject average
  radiance, Prefilter radiance/variance/sample-count, Reprojected radiance, and
  the denoiser tile list. It writes RadianceHistory, VarianceHistory, and
  SampleCountHistory in the existing Reproject history resource set. The bridge
  copies AverageRadiance to AverageRadianceHistory before resolve and mirrors
  the updated FFX history state to the other swapchain slots after resolve.
- The runtime bridge is data-gated by
  `scripts\Test-FidelityFxSssrIntegration.ps1`, including source/build checks,
  LightingShowcase and Forward3D FBX FFX lanes, and an internal-backend control
  lane that proves the FFX dispatches are suppressed when not requested. The
  gate also requires the FFX FrameGraph resources, pass split, history writeback,
  and Vulkan validation logs to be clean.
- Runtime image contribution now has a controlled Deferred composite bridge:
  when `SE_SSR_BACKEND=ffx-sssr`, a previous temporal frame exists, and the FFX
  history has completed at least once, GBuffer binding 17 samples the previous
  FFX `RadianceHistory` instead of the internal `SSRResolved` image. The shader
  uses an explicit SSR control bit to treat this as radiance history, bypassing
  the internal alpha-confidence/metadata contract and blending conservatively
  over the stable probe/IBL fallback.

Production visual contract (SelfEngine FFX contract v11):
- The default ray density is `4 rays/quad`. Sparse `1` and `2` ray modes remain
  explicit diagnostics because quad replication produced distance-dependent
  rings on glossy receivers in the current integration.
- Miss fallback uses a stable primary reflection direction and selects the
  prefiltered environment mip from receiver roughness. The stochastic GGX
  direction and LOD0 fallback remain reverse controls for isolation.
- ResolveTemporal exports AMD-style glossy validity as composite confidence
  (`mode 0`). SelfEngine's experimental sample-count/variance confidence is
  retained as a comparison mode, not the production default.
- The current visible FFX output is cleared before dispatch so pixels omitted
  by classification cannot preserve stale radiance from a previous frame.

Debug reverse controls:
- `SE_SSR_FFX_SAMPLES_PER_QUAD=1`
- `SE_SSR_FFX_STABLE_ENVIRONMENT_FALLBACK_OFF=1`
- `SE_SSR_FFX_PERFECT_REFLECTION_DIRECTIONS_OFF=1`
- `SE_SSR_FFX_SAMPLE_VARIANCE_CONFIDENCE=1`
- `SE_SSR_FFX_CLEAR_VISIBLE_OUTPUT_OFF=1`

Validation note:
- The accepted GPU behavior was run with the exact v10 combination that became
  the v11 defaults. The v10 integration matrix passed `916 pass / 0 fail`, the
  general SSR regression passed `691 pass / 0 fail`, and the user accepted the
  real LightingShowcase result with no cross, concentric ring, snow noise, or
  motion corruption.
- The v11 static source/default contract passes `56 pass / 0 fail`; Debug
  shader, Forward3D, and LightingShowcase builds also pass.
- A newly linked v11 executable is currently denied by Windows Smart App
  Control policy `4551` / policy ID
  `{0283ac0f-fff1-49ae-ada1-8a933130cad6}` even when its local Authenticode
  signature reports `Valid`. Do not describe v11 as runtime-launched until the
  device compatibility task resolves that policy boundary. Use
  `scripts\Test-FidelityFxSssrIntegration.ps1 -StaticOnly -SkipBuild -Strict`
  for the source/default contract gate in the meantime.
