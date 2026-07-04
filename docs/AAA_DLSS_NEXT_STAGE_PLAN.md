# AAA DLSS 4.5 Experimental Integration Plan

## Stage Goal

Add an experimental DLSS 4.5 Super Resolution / DLAA path behind the
engine-owned temporal upscaler interface. The stage should prove that SelfEngine
can discover a local NVIDIA DLSS SDK package, report capability and fallback
state, prepare render-scale inputs, and eventually evaluate DLSS SR without
spreading NGX or Streamline details through the renderer.

This stage is complete when DLSS SR/DLAA can be requested with
`SE_UPSCALER_PLUGIN=dlss`, capability and fallback state are visible in CSV,
ImGui, and FrameGraph, native/TAA fallback remains deterministic, and at least
one smoke run proves the DLSS request path cleanly falls back on unsupported
machines or incomplete packages.

## Scope

- Target DLSS Super Resolution / DLAA first.
- Use the existing temporal-upscale contract from the previous stage:
  `HDRSceneColor`, `SceneDepth`, `Velocity`, `TemporalHistoryColor`, and
  `TemporalFrameState`.
- Keep the external seam small: renderer code should ask an engine-owned
  temporal-upscaler module for provider/package/evaluate readiness rather than
  directly knowing NGX or Streamline details.
- Preserve the current fallback path whenever DLSS is unavailable, unsupported,
  or not requested.

## Non-Goals

- No DLSS Frame Generation, Dynamic Multi Frame Generation, or Reflex/present
  scheduling in this stage.
- No Ray Reconstruction integration in this stage.
- No production image-quality claim until object motion vectors, reactive masks,
  exposure/jitter policy, and pre/post-upscale pass ordering are proven.
- No UE scene parsing/export/viewport parity work.

## Acceptance Evidence

- CSV exposes DLSS provider request, package directory/header/import-library/
  runtime readiness, package version if discoverable, SR/FG/RR/transformer
  symbol readiness, provider fallback reason, and whether the provider is
  evaluate-ready.
- ImGui exposes the same provider/package readiness in the performance panel.
- FrameGraph keeps the existing `TemporalUpscaleBoundary` visible when DLSS is
  requested and distinguishes package-ready from evaluate-enabled state.
- Smoke runs cover:
  - DLSS requested with a local SDK package present.
  - DLSS requested with an invalid SDK path.
  - DLSS not requested.
- `_quick_build.bat` passes.
- Logs contain no `VUID`, validation, error, failed, exception, or shader
  diagnostic matches.

## Execution Slices

1. DLSS package/capability probe. Implemented.
   - Add a deep `TemporalUpscaler` module with a small provider-probe interface.
   - Detect `thirdParty/nvidia_dlss` or `SE_NVIDIA_DLSS_SDK_DIR`.
   - Report headers, import libraries, runtime DLLs, DLSS SR helper symbols,
     Frame Generation helper symbols, Ray Reconstruction helper symbols, and
     transformer preset symbols.
   - Keep `temporalUpscalerPluginAvailable=0` until an actual evaluate adapter
     exists; report package readiness separately.

2. Internal render-scale resource carrier. Implemented.
   - Make render-scale affect internal scene target extents under an explicit
     experimental gate.
   - Keep swapchain/display extent unchanged.
   - Reset temporal history on internal/display extent changes and expose
     fallback reasons.

3. DLSS adapter initialization and capability query.
   Implemented.
   - Add a Vulkan DLSS adapter behind the TemporalUpscaler interface.
   - Initialize NGX/Streamline only when requested and when package readiness is
     strong enough.
   - Query support, driver requirements, recommended render resolutions, and
     quality modes.

3.5. Vulkan DLSS feature-requirements readiness.
   - Query and expose NGX Vulkan feature requirements, required instance/device
     extensions, and device-creation prerequisites before attempting SR
     evaluation.
   - Resolve or explicitly report any reason `SuperSampling.Available` remains
     false after NGX init succeeds.
   - Do not proceed to SR evaluation until the current Vulkan device reports DLSS
     Super Resolution support.

4. DLSS SR/DLAA evaluate path.
   - Tag/bind color, depth, motion vectors, jitter, exposure/reset state, and
     output.
   - Run DLSS before display-space post/UI.
   - Keep fallback to native/TAA on any failure.

5. Quality and ordering hardening.
   - Split pre-upscale and post-upscale post-process ordering.
   - Add object motion-vector readiness gates, reactive/transparency mask
     placeholders, and DLSS-specific debug views.
   - Add reference screenshots/visual QA before claiming production readiness.

## Next Slice To Execute Now

Implement Slice 3.5: Vulkan DLSS feature-requirements readiness. Slice 3 now
initializes NGX and queries capability parameters, but the current Vulkan device
reports `SuperSampling.Available=0` with feature-init result `0xBAD00002`.
Before Slice 4 can safely evaluate DLSS SR/DLAA, expose NGX feature
requirements and required Vulkan extension/device prerequisites, then either
enable the missing prerequisites or keep a deterministic unsupported fallback.

## Slice 1 Execution Evidence

- Added an engine-owned `TemporalUpscaler` module with a small package-probe
  interface. Renderer code passes provider name and optional SDK root; the
  module owns filesystem probing and DLSS symbol checks.
- `SE_UPSCALER_PLUGIN=dlss` / `SE_TEMPORAL_UPSCALER_PLUGIN=dlss` request the
  DLSS provider. `SE_NVIDIA_DLSS_SDK_DIR` / `SE_DLSS_SDK_DIR` can override the
  SDK root; otherwise the default root is `thirdParty/nvidia_dlss`.
- CSV and ImGui now expose provider kind, package fallback reason, package
  directory/header/import-library/runtime readiness, DLSS SR/Frame Generation/
  Ray Reconstruction/transformer-preset symbols, discovered package version,
  package-ready state, and evaluate-adapter availability.
- FrameGraph now records `TemporalUpscalerPackageProbe` when a provider is
  requested or a package is ready. This pass does not read/write GPU resources.
- `_quick_build.bat` passes for `SelfEngineForward3D` with only the pre-existing
  MSVC runtime-library warning.
- Smoke evidence:
  `out/benchmarks/aaa_dlss_package_present_smoke.csv`,
  `out/benchmarks/aaa_dlss_invalid_sdk_smoke.csv`, and
  `out/benchmarks/aaa_dlss_not_requested_smoke.csv` all have matching
  675-column rows and 0 frame-graph validation issues.
- The package-present smoke reports plugin requested/available `1/0`, provider
  kind `1`, package fallback reason `8`, directory/header/import-library/runtime
  readiness `1/1/1/1`, SR/FG/RR/transformer symbols `1/1/1/1`, package version
  `310.7.0`, package ready `1`, evaluate adapter available `0`, temporal
  upscale fallback reason `4`, and contract ready `1`.
- The invalid-SDK smoke reports plugin requested `1`, provider kind `1`,
  package fallback reason `3`, all package/symbol/version readiness `0`,
  package ready `0`, evaluate adapter available `0`, temporal upscale fallback
  reason `4`, and contract ready `1`.
- The not-requested smoke reports plugin requested `0`, provider kind `0`,
  package fallback reason `1`, package ready `0`, evaluate adapter available
  `0`, and temporal upscale fallback reason `1`.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is package/capability readiness only. It does not initialize NGX or
  Streamline, link DLSS libraries into the engine, evaluate DLSS, resize render
  targets, enable Frame Generation/Ray Reconstruction, or change presentation.

## Slice 2 Execution Evidence

- Added an explicit internal render-scale apply gate. `SE_RENDER_SCALE`,
  `SE_TEMPORAL_RENDER_SCALE`, and `SE_INTERNAL_RENDER_SCALE` still request a
  diagnostic internal scale by default. The renderer only applies that scale to
  scene targets when `SE_RENDER_SCALE_APPLY=1` or
  `SE_INTERNAL_RENDER_SCALE_APPLY=1` is set.
- `VulkanSceneRenderTargets` can now be created/recreated with an explicit
  internal extent. HDR scene color, temporal history color, weighted
  translucency targets, scene depth, velocity, and GBuffer attachments follow
  that internal extent while the swapchain and display extent remain unchanged.
- Temporal state now tracks the active internal scene extent for jitter UVs,
  history extent checks, and temporal-upscale diagnostics. Light-tile assignment
  also uses the active internal extent so deferred lighting tile indices match
  the resized GBuffer/HDR path.
- Fullscreen deferred/HDR/debug pipelines and internal GBuffer/WBOIT geometry
  passes now use dynamic viewport/scissor where they render into framebuffer
  extents that can differ from the swapchain. The forward-residual depth-copy
  path refuses unsafe scene-depth copies when scene and swapchain extents differ,
  falling back to depth prefill instead.
- FrameGraph resource scale text now distinguishes display extent from internal
  scene extent for HDRSceneColor, SceneDepth, Velocity, GBuffer, weighted
  translucency, and temporal history resources.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Smoke evidence:
  `out/benchmarks/aaa_dlss_render_scale_requested_smoke.csv`,
  `out/benchmarks/aaa_dlss_render_scale_applied_smoke.csv`, and
  `out/benchmarks/aaa_dlss_render_scale_dlss_requested_smoke.csv` all report
  0 frame-graph validation issues.
- The requested-only smoke reports render scale requested/active/applied
  `0.75/1/0`, display extent `1280x720`, requested extent `960x540`, active
  extent `1280x720`, temporal upscale requested/enabled `1/0`, fallback reason
  `4`, and contract ready `1`.
- The apply-gated smoke reports render scale requested/active/applied
  `0.75/0.75/1`, display extent `1280x720`, requested and active extent
  `960x540`, light tile grid `60x34`, temporal upscale requested/enabled
  `1/0`, fallback reason `4`, and contract ready `1`.
- The DLSS-requested apply-gated smoke reports the same active internal extent,
  provider kind `1`, package ready `1`, SR/transformer symbols `1/1`, SDK
  version `310.7.0`, evaluate adapter `0`, temporal-upscale fallback reason
  `4`, and contract ready `1`.
- The WBOIT apply-gated smoke
  `out/benchmarks/aaa_dlss_render_scale_wboit_smoke.csv` reports active extent
  `960x540`, weighted translucency accum/revealage extent `960x540`, 5 WBOIT
  draws, one WBOIT resolve, 0 forward-residual draws, 0 depth-copy ops, and 0
  frame-graph validation issues.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is an internal render-scale carrier only. It does not initialize NGX or
  Streamline, query DLSS runtime support, evaluate DLSS, enable dynamic
  resolution policy, add a native TAAU/TSR pass, or change default presentation.

## Slice 3 Execution Evidence

- Added optional CMake detection for a local NVIDIA DLSS SDK. When the package
  has headers, a matching Windows import library, and `nvngx_dlss.dll`, the
  engine defines `SE_ENABLE_NVIDIA_DLSS=1`, includes NGX headers, links the
  configuration-matched dynamic-CRT NGX import library, and copies
  `nvngx_dlss.dll` beside the built executable. Builds without the SDK keep the
  adapter compiled out and report an explicit runtime fallback.
- Added `TemporalUpscalerRuntimeStatus` behind the engine-owned
  `TemporalUpscaler` boundary. Renderer code passes Vulkan instance, physical
  device, device, function pointers, display extent, and selected DLSS quality
  mode; NGX types remain contained in `temporal_upscaler.cpp`.
- `SE_DLSS_QUALITY` / `SE_DLSS_MODE` select the diagnostic quality mode. The
  default is Quality. The runtime status reports the selected quality mode and
  the DLSS 4.5 transformer preset policy: K for DLAA/Quality/Balanced/Ultra
  Quality, M for Performance, and L for Ultra Performance.
- Runtime diagnostics now expose adapter compile state, initialization attempt,
  initialization result, capability-parameter readiness/result, Super Resolution
  support, driver-update requirement, minimum driver, feature-init result,
  optimal-settings query/result, recommended render dimensions, sharpness, and
  evaluate-adapter availability in CSV, ImGui, and FrameGraph.
- FrameGraph now records `TemporalUpscalerRuntimeCapability` separately from
  `TemporalUpscalerPackageProbe`, so package readiness and actual NGX runtime
  capability are distinguishable.
- `_quick_build.bat` passes for `SelfEngineForward3D` after CMake regeneration
  with the local SDK enabled, and also passes when configured with
  `SELFENGINE_NVIDIA_DLSS_SDK_DIR=Z:/SelfEngine/missing_dlss_sdk` so the adapter
  is compiled out. The existing MSVC runtime-library warning remains.
- Smoke evidence:
  `out/benchmarks/aaa_dlss_runtime_capability_smoke.csv`,
  `out/benchmarks/aaa_dlss_runtime_invalid_sdk_smoke.csv`, and
  `out/benchmarks/aaa_dlss_runtime_not_requested_smoke.csv` all report
  0 frame-graph validation issues.
- The package-present runtime smoke reports provider/package
  `1/1`, package fallback `0`, adapter compiled `1`, initialization attempted
  and initialized `1/1`, initialization result `1`, capability parameters ready
  `1`, quality/preset `3/11`, contract ready `1`, and evaluate adapter `0`.
  The current machine reports `SuperSampling.Available=0`,
  `SuperSampling.FeatureInitResult=0xBAD00002`, runtime fallback `8`
  (`SuperResolutionUnavailable`), and temporal-upscale fallback `4`.
- The invalid-SDK smoke reports provider `1`, package ready `0`, package/runtime
  fallback `3/3`, adapter compiled `1`, initialization attempted `0`, and
  0 validation issues.
- The not-requested smoke reports provider `0`, package/runtime fallback `1/1`,
  initialization attempted `0`, temporal-upscale requested `0`, and 0 validation
  issues.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is runtime initialization and capability diagnostics only. It does not
  create a DLSS feature handle, evaluate DLSS, enable Frame Generation, enable
  Ray Reconstruction, change presentation, or claim production image quality.
