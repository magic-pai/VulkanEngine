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
   Implemented.
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

Implement Slice 4.2: DLSS visual QA and quality hardening. Slice 4.1 now routes
`TemporalUpscaleOutput` into the visible post/present path under an explicit
gate after current-frame DLSS evaluate reports output ready. The next slice
should add renderer-owned screenshots/reference captures, compare native HDR
composite vs DLSS-present output, and start quality gates for object motion
vectors, reactive/transparency masks, and post-ordering policy. Do not expand
into Frame Generation, Ray Reconstruction, Streamline interposition, or default
presentation changes yet.

## Slice 4.1 Execution Plan

Slice 4.1 should make the successful DLSS SR/DLAA evaluate output visible
without making DLSS the default presentation path. The implementation should
keep all routing decisions in Vulkan renderer/post-process code and keep NGX
details behind the existing `TemporalUpscaler` boundary.

1. Explicit presentation gate.
   - Add an opt-in runtime gate for using `TemporalUpscaleOutput` as the
     post-process source, with `SE_TEMPORAL_UPSCALE_PRESENT=1` and
     `SE_DLSS_PRESENT=1` accepted aliases.
   - The gate may force the deferred HDR composite route so smoke runs can see
     the output, but DLSS must remain off the default visible path.
   - If the gate is disabled, Slice 4 evaluate diagnostics should remain
     unchanged and the visible image should still use the native HDR composite
     or legacy forward path.

2. Post-process source descriptors.
   - Keep the existing HDR composite and bloom descriptor sets sampling
     `HDRSceneColor`.
   - Add parallel descriptor sets whose first HDR/composite source and bloom
     mip-0 source sample `TemporalUpscaleOutput`.
   - Preserve temporal history, velocity, depth, bloom mip, and color-grading
     bindings so post-process shaders do not need DLSS-specific branches.

3. Ordering.
   - Record DLSS evaluate after HDR scene color, WBOIT resolve, and auto
     exposure inputs are available.
   - Move bloom pyramid and final HDR composite after the evaluate step when the
     route is requested, so bloom, tone mapping, color grading, sharpening, UI,
     and present can operate on the upscaled image.
   - Keep auto exposure metering on the pre-upscale HDR scene color for this
     slice; per-output metering policy remains future quality work.

4. Fallback and diagnostics.
   - Only switch to the temporal-upscale descriptor sets when the current frame
     DLSS evaluate reports output ready.
   - Fall back to the native HDR composite source if evaluate fails, descriptors
     are unavailable, or the composite path is not active.
   - Expose post-source requested/active/fallback diagnostics in CSV, ImGui,
     and FrameGraph separately from NGX create/evaluate diagnostics.

5. Verification.
   - Run `_quick_build.bat`.
   - Run smokes for DLSS-present enabled, invalid SDK with present gate, and
     DLSS requested without the present gate.
   - Require 0 frame-graph validation issues and clean logs for `VUID`,
     validation, error, failed, exception, and shader diagnostics.
   - Add visual/screenshot QA evidence before claiming production image quality.

## Slice 4 Execution Plan

Slice 4 should turn the current readiness proof into a real, per-frame DLSS
Super Resolution / DLAA evaluate call while preserving deterministic fallback.
The first implementation is intentionally scoped to SR/DLAA evaluation evidence;
post-process ordering and default presentation can deepen in follow-up slices
after the evaluate contract is stable.

1. Output resource carrier.
   - Add a display-sized temporal-upscale output image per swapchain image.
   - Use HDR color format and storage/sampled/transfer usage so NGX can write
     the output and later renderer passes can sample or inspect it.
   - Expose output allocation, format, display extent, and active input extent
     in CSV, ImGui, and FrameGraph.

2. NGX feature lifecycle.
   - Add an engine-owned `TemporalUpscalerEvaluateRequest` and
     `TemporalUpscalerEvaluateStatus` so Vulkan renderer code passes opaque
     image handles/views/formats/extents without learning NGX helper details.
   - Create the DLSS feature handle lazily on the first valid evaluate request.
   - Recreate the feature handle when input extent, output extent, quality mode,
     or create flags change.
   - Release the feature handle from the existing NGX shutdown path.

3. Evaluate call.
   - Wrap HDR scene color, scene depth, velocity, and temporal-upscale output as
     `NVSDK_NGX_Resource_VK` image-view resources.
   - Call `NGX_VULKAN_EVALUATE_DLSS_EXT` after HDR scene color/WBOIT/auto
     exposure inputs are produced and before the swapchain main pass.
   - Pass render-subrect dimensions, jitter offsets in input pixels, reset
     state, MV scale, HDR/depth/motion-vector flags, sharpness, and quality
     preset policy.
   - Keep exposure texture/reactive mask/transparency mask optional and null in
     the first evaluate carrier.

4. Fallback and diagnostics.
   - If package/runtime/resource/create/evaluate readiness fails, keep the
     current native/HDR composite fallback and report the exact reason.
   - Replace `UpscalerEvaluatePathMissing` with either `None` when evaluate
     succeeds or a more specific fallback when it does not.
   - Add create/evaluate attempt counts, results, output ready state, input and
     output dimensions, reset flag, jitter, and feature recreation reason to
     CSV, ImGui, and FrameGraph.

5. Verification.
   - Run `_quick_build.bat`.
   - Run DLSS requested, invalid-SDK, and not-requested smokes.
   - Require 0 frame-graph validation issues and clean logs for `VUID`,
     validation, error, failed, exception, and shader diagnostics.
   - Do not claim production image quality until object motion vectors,
     reactive/transparency masks, pre/post-upscale post ordering, and visual QA
     are proven.

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

## Slice 3.5 Execution Evidence

- Added NGX Vulkan feature-requirement diagnostics before the runtime capability
  query. CSV, ImGui, and FrameGraph now expose feature requirement query/result,
  supported mask, minimum hardware/OS, required instance/device extensions,
  missing-available extension counts, and missing-enabled extension counts.
- When DLSS is requested through `SE_UPSCALER_PLUGIN=dlss` or the explicit
  extension gate is set, SelfEngine now enables the NGX-required Vulkan
  instance/device extensions if the selected physical device exposes them:
  `VK_KHR_get_physical_device_properties2`, `VK_KHR_buffer_device_address`,
  `VK_KHR_push_descriptor`, `VK_NVX_binary_import`, and
  `VK_NVX_image_view_handle`.
- Device creation now enables `VkPhysicalDeviceBufferDeviceAddressFeatures`
  when `VK_KHR_buffer_device_address` is enabled and supported.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Smoke evidence:
  `out/benchmarks/aaa_dlss_feature_requirements_smoke.csv`,
  `out/benchmarks/aaa_dlss_feature_requirements_invalid_sdk_smoke.csv`, and
  `out/benchmarks/aaa_dlss_feature_requirements_not_requested_smoke.csv` all
  report 0 frame-graph validation issues.
- The package-present feature-requirements smoke reports provider/package
  `1/1`, package fallback `0`, adapter compiled `1`, initialization attempted
  and initialized `1/1`, feature requirements supported `1`, instance/device
  missing-enabled extension counts `0/0`, `SuperSampling.Available=1`,
  feature-init result `1`, optimal-settings result `1`, evaluate adapter `1`,
  and runtime fallback `0`.
- The same smoke reports display extent `1280x720`, requested/active internal
  extent `960x540`, quality mode `3`, preset `11`, recommended render extent
  `853x480`, min render extent `640x360`, max render extent `1280x720`, and
  sharpness `0.35`.
- The renderer still reports `temporal_upscale_enabled=0` with fallback reason
  `6` (`UpscalerEvaluatePathMissing`) because Slice 4 has not yet created or
  evaluated a DLSS feature.
- The invalid-SDK smoke reports package/runtime fallback `3/3`, no
  initialization attempt, provider available `0`, evaluate adapter `0`, and a
  deterministic temporal-upscale fallback. The not-requested smoke reports
  package/runtime fallback `1/1`, no initialization attempt, temporal upscale
  requested `0`, and no DLSS side effects.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is Vulkan prerequisite readiness and runtime support proof only. It does
  not create a DLSS feature handle, evaluate DLSS, enable Frame Generation,
  enable Ray Reconstruction, change presentation, or claim production image
  quality.

## Slice 4 Execution Evidence

- Added a display-sized `TemporalUpscaleOutput` image per swapchain image. It
  uses the HDR scene color format with storage/sampled/transfer usage and is
  exposed in CSV, ImGui, and FrameGraph when the evaluate pass is active.
- Added `TemporalUpscalerEvaluateRequest` and
  `TemporalUpscalerEvaluateStatus` behind the engine-owned TemporalUpscaler
  boundary. Renderer code passes Vulkan image handles/views/formats/extents,
  jitter/reset, quality mode, and sharpness; NGX helper types remain contained
  in `temporal_upscaler.cpp`.
- Added DLSS feature-handle lifecycle management. The feature is lazily created
  on the first valid evaluate request, reused across matching frames, recreated
  when render extent, output extent, quality mode, or create flags change, and
  released from the existing NGX shutdown path.
- Added a pre-main-pass DLSS evaluate command-buffer step. It transitions
  HDRSceneColor, SceneDepth, Velocity, and TemporalUpscaleOutput to
  `GENERAL`, wraps them as `NVSDK_NGX_Resource_VK`, calls
  `NGX_VULKAN_EVALUATE_DLSS_EXT`, then restores renderer-readable layouts.
- The first evaluate carrier uses `IsHDR | MVLowRes` create flags, input-pixel
  jitter offsets, reset state, render-subrect dimensions, MV scale `1/1`,
  NGX-recommended sharpness, and the existing transformer preset policy. It
  intentionally leaves exposure texture, reactive/transparency masks, and
  optional research GBuffer inputs null.
- CSV and ImGui now report evaluate requested/attempted/fallback,
  parameter-allocation result, feature create attempt/result/recreate reason,
  evaluate result, output ready state, render/output dimensions, create flags,
  reset, jitter, MV scale, and evaluate sharpness.
- FrameGraph now records `TemporalUpscalerEvaluate` as a compute pass and
  `TemporalUpscaleOutput` as its output only when the pass is active, preserving
  0 validation issues in fallback paths.
- `_quick_build.bat` passes for `SelfEngineForward3D`; the existing MSVC
  runtime-library warning remains.
- Smoke evidence:
  `out/benchmarks/aaa_dlss_evaluate_smoke.csv`,
  `out/benchmarks/aaa_dlss_evaluate_invalid_sdk_smoke.csv`, and
  `out/benchmarks/aaa_dlss_evaluate_not_requested_smoke.csv` all report
  matching 752-column rows and 0 frame-graph validation issues.
- The DLSS-requested smoke reports temporal upscale requested/enabled `1/1`,
  fallback `0`, output allocated `1` at `1280x720`, plugin/evaluate adapter
  `1/1`, runtime fallback `0`, evaluate requested/attempted `1/1`, evaluate
  fallback `0`, feature create result `1`, feature created `1`, DLSS evaluate
  result `1`, output ready `1`, and render/output extents `960x540 -> 1280x720`.
  The first recorded row proves feature creation itself: create attempted `1`,
  result `1`, recreated `1`, recreation reason `1` (`FirstCreate`), evaluate
  result `1`, and output ready `1`; later rows reuse the handle.
- The invalid-SDK smoke reports deterministic fallback: plugin requested `1`,
  package ready `0`, plugin/evaluate adapter `0/0`, runtime fallback `3`,
  temporal-upscale fallback `4`, evaluate attempted `0`, and output ready `0`.
- The not-requested smoke reports no DLSS side effects: temporal upscale
  requested/enabled `0/0`, package/runtime fallback `1/1`, evaluate
  requested/attempted `0/0`, and output ready `0`.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is real DLSS SR/DLAA feature creation and evaluate command recording. It
  still does not route the DLSS output into final presentation, enable Frame
  Generation, enable Ray Reconstruction, introduce Streamline interposition,
  change default presentation, or claim production image quality.

## Slice 4.1 Execution Evidence

- Added an explicit DLSS/temporal-upscale presentation gate. Setting
  `SE_TEMPORAL_UPSCALE_PRESENT=1`, `SE_TEMPORAL_UPSCALE_OUTPUT_PRESENT=1`,
  `SE_UPSCALER_PRESENT=1`, or `SE_DLSS_PRESENT=1` requests that
  `TemporalUpscaleOutput` become the visible post-process source. With the gate
  disabled, DLSS evaluate may still run, but the visible post source stays on
  the native HDR composite path.
- Added parallel HDR composite and bloom descriptor sets that bind
  `TemporalUpscaleOutput` as the post source while preserving the existing
  shader binding contract for bloom mips, temporal history, velocity, scene
  depth, and color-grading LUTs. Native HDR descriptor sets remain unchanged.
- Moved DLSS evaluate ahead of bloom/final composite in command recording while
  leaving auto exposure on the pre-upscale HDR scene color for this slice.
  Bloom pyramid and HDR composite select the temporal-upscale descriptor sets
  only when the current frame reports DLSS output ready; otherwise they fall
  back to the native HDR descriptor sets.
- CSV and ImGui now expose `temporal_upscale_post_source_requested`,
  `temporal_upscale_post_source_active`, and
  `temporal_upscale_post_source_fallback_reason` separately from NGX
  create/evaluate diagnostics. FrameGraph records `TemporalUpscalePostSource`
  when the presentation route is requested.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Smoke evidence:
  `out/benchmarks/aaa_dlss_present_smoke.csv`,
  `out/benchmarks/aaa_dlss_present_bloom_smoke.csv`,
  `out/benchmarks/aaa_dlss_present_invalid_sdk_smoke.csv`, and
  `out/benchmarks/aaa_dlss_evaluate_not_present_smoke.csv` all report matching
  755-column rows and 0 frame-graph validation issues.
- The DLSS-present smoke reports evaluate result/output ready `1/1`, post
  source requested/active/fallback `1/1/0`, temporal upscale enabled `1`, and
  one HDR composite draw. The bloom variant reports the same post-source state
  plus bloom pyramid enabled with 4 downsample draws and 3 upsample draws,
  proving the bloom source follows the upscaled output route.
- The invalid-SDK present smoke reports package ready `0`, runtime fallback `3`,
  evaluate attempted/output ready `0/0`, post source requested/active/fallback
  `1/0/3`, and one native HDR composite draw. The evaluate-not-present smoke
  reports evaluate result/output ready `1/1` while post source
  requested/active/fallback remains `0/0/1`, proving evaluate does not change
  presentation without the explicit gate.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This makes DLSS SR/DLAA output visible under an explicit experimental gate.
  It is still not production image-quality validation: renderer-owned
  screenshots/reference captures, object motion vectors, reactive/transparency
  masks, exposure policy refinement, and visual-diff evidence remain follow-up
  work before any production IQ claim.
