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

Implement Slice 4.10: move from benchmark-grid coverage toward imported model
content or start removing the forward/WBOIT object-motion blocker with real
residual/transparent velocity or an explicit pre-upscale ordering policy. Slice
4.9 proves a complex opaque material stress scene can pass the production DLSS
quality gate. The next slice should either add an imported-model visual baseline
or make one blocked residual/transparent route genuinely object-motion ready.
Do not expand into Frame Generation, Ray Reconstruction, Streamline
interposition, or default presentation changes yet.

## Slice 4.4 Execution Plan

Slice 4.4 should remove the reactive/transparency mask blocker without claiming
production image quality. The first implementation is a neutral carrier: allocate
input-resolution mask images, clear them to zero each frame, bind them to NGX,
and report them through the existing quality gate. Material-authored reactive
coverage can follow after object motion vectors are correct.

1. DLSS mask resources.
   - Allocate per-frame bias-current-color and transparency mask images in
     `VulkanSceneRenderTargets` at the active internal render extent.
   - Use a simple single-channel neutral format and clear both masks to zero
     before DLSS evaluate.

2. NGX binding.
   - Extend `TemporalUpscalerEvaluateRequest` with optional mask resources.
   - Bind the bias-current-color mask to `pInBiasCurrentColorMask`.
   - Bind the transparency mask to `pInTransparencyMask`.
   - Keep evaluate tolerant of missing optional masks.

3. Diagnostics and QA.
   - Update the DLSS quality gate so reactive/transparency readiness reflects
     the current frame's mask binding.
   - Update FrameGraph so active DLSS evaluate reads the mask resources.
   - Update visual QA to require mask readiness while keeping the production
     gate blocked on object motion vectors and reference baselines.

## Slice 4.3 Execution Plan

Slice 4.3 should keep the experimental DLSS-present route working while adding
a separate production-quality gate. The gate must make it impossible for
diagnostics, scripts, or docs to mistake "DLSS output is visible" for
"production DLSS image quality is ready".

1. Quality gate diagnostics.
   - Add CSV and ImGui fields for quality gate requested/ready state,
     fallback reason, required/ready/blocker masks, and per-input readiness.
   - Required inputs are DLSS evaluate output, camera motion vectors, object
     motion vectors, reactive mask, transparency mask, exposure policy,
     post-upscale ordering, and stable reference baseline.
   - Preserve the existing experimental DLSS evaluate/present route; this gate
     must not disable the opt-in visible output.

2. FrameGraph visibility.
   - Add a `TemporalUpscalerQualityInputs` pass when DLSS is requested.
   - Keep it roadmap until all quality inputs are ready.
   - Avoid inventing physical FrameGraph resources for reactive/transparency
     masks before the renderer allocates them.

3. Visual QA hardening.
   - Extend `scripts/Test-DlssVisualQa.ps1` to assert the DLSS-present run
     produces visible output while the production quality gate remains blocked.
   - Add coarse sanity thresholds for native-vs-DLSS screenshot difference so
     identical or wildly divergent captures fail deterministically.

4. Documentation and guardrails.
   - Record the blocker mask and ready input mask in the DLSS plan and main AAA
     rendering plan.
   - Advance the next slice toward removing blockers instead of adding Frame
     Generation, Ray Reconstruction, Streamline, or default presentation work.

## Slice 4.2 Execution Plan

Slice 4.2 should add repeatable visual evidence for the experimental DLSS
present route before any production image-quality claim. The first pass should
prefer scriptable renderer-owned captures over manual screenshots, while keeping
runtime DLSS integration unchanged.

1. Visual QA runner.
   - Add a script that runs `SelfEngineForward3D` in deterministic benchmark
     mode for both native deferred HDR and DLSS-present configurations.
   - Produce paired CSV files and paired screenshots under
     `out/reference_captures/dlss_visual_qa/`.
   - Reuse the existing Win32 window-capture approach from the render-smoke
     tooling so the renderer does not need Vulkan readback changes in this
     slice.

2. CSV assertions.
   - Require both runs to produce matching CSV header/data column counts and 0
     frame-graph validation issues.
   - Require the native run to keep temporal-upscale post source inactive.
   - Require the DLSS-present run to report evaluate output ready and post
     source requested/active/fallback `1/1/0`.

3. Screenshot assertions.
   - Verify each screenshot has non-background variation in the central render
     region so blank or minimized captures fail.
   - Compare native and DLSS-present screenshots with simple RGB aggregate
     metrics. This is a sanity check that the route renders a visible image, not
     a production IQ threshold.

4. Documentation and guardrails.
   - Record paths, CSV key results, and image-difference metrics in the DLSS
     plan and main AAA rendering plan.
   - Keep object motion vectors, reactive/transparency masks, exposure policy
     refinement, and real visual-diff baselines as follow-up work.

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

## Slice 4.2 Execution Evidence

- Added `scripts/Test-DlssVisualQa.ps1`, a repeatable visual QA runner for the
  experimental DLSS-present route. The script builds `SelfEngineForward3D` by
  default, runs paired native deferred-HDR and DLSS-present benchmark captures,
  launches both configurations for Win32 window screenshots, checks logs for
  validation/error/shader diagnostics, verifies CSV post-source state, checks
  screenshots for nonblank central-region variation, compares the two images,
  and writes `out/reference_captures/dlss_visual_qa/summary.json`.
- The native benchmark uses deferred HDR at `SE_RENDER_SCALE=0.75` with render
  scale applied, TAA enabled, and temporal jitter prepared. It keeps
  temporal-upscale post source inactive. The DLSS-present benchmark uses the
  same render-scale/TAA setup plus `SE_UPSCALER_PLUGIN=dlss` and
  `SE_DLSS_PRESENT=1`.
- Full verification command:
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1`.
  It rebuilds `SelfEngineForward3D` successfully and reports `DLSS visual QA
  passed`.
- Generated evidence:
  - `out/reference_captures/dlss_visual_qa/native_deferred_hdr.csv`
  - `out/reference_captures/dlss_visual_qa/dlss_present.csv`
  - `out/reference_captures/dlss_visual_qa/native_deferred_hdr.png`
  - `out/reference_captures/dlss_visual_qa/dlss_present.png`
  - `out/reference_captures/dlss_visual_qa/summary.json`
- CSV evidence: both native and DLSS-present rows report matching 755-column
  header/data rows and 0 frame-graph validation issues. Native reports
  post-source `0/0/1`; DLSS-present reports evaluate/output `1/1` and
  post-source `1/1/0`.
- Screenshot evidence: both captures are `1038x614`, both report 8060 sampled
  central pixels with 8060 pixels differing from the sampled background, and the
  paired image comparison samples 14352 pixels with 3419 changed pixels, mean
  RGB delta `29.4288`, and max delta `583`.
- This is the first automated visual QA proof that the DLSS-present route
  renders a visible, nonblank image distinct from the native deferred-HDR
  capture. It is still a sanity/diagnostic comparison rather than a production
  image-quality baseline. Object motion vectors, reactive/transparency masks,
  exposure policy refinement, stable camera/reference baselines, and stricter
  visual-diff thresholds remain follow-up work.

## Slice 4.3 Execution Evidence

- Added a DLSS production-quality gate separate from experimental DLSS
  evaluate/presentation. CSV and ImGui now expose
  `temporal_upscaler_dlss_quality_gate_requested`,
  `temporal_upscaler_dlss_quality_gate_ready`,
  `temporal_upscaler_dlss_quality_gate_fallback_reason`, required/ready/blocker
  masks, and per-input readiness for evaluate output, camera motion vectors,
  object motion vectors, reactive mask, transparency mask, exposure policy,
  post-upscale ordering, and reference baseline.
- Added `TemporalUpscalerQualityInputs` to the FrameGraph when DLSS is
  requested. It remains roadmap while the quality gate is blocked and does not
  invent reactive/transparency mask resources before the renderer physically
  allocates them.
- Extended `scripts/Test-DlssVisualQa.ps1` so the paired visual QA now asserts
  both sides of the current truth: DLSS-present must produce output and activate
  post source, while production-quality DLSS must still report blocked. The
  script also records sanity thresholds `minChangedPixels=512` and
  `maxMeanDelta=160` in `summary.json`.
- Verification commands:
  - `_quick_build.bat`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
  - Invalid-SDK benchmark:
    `out/benchmarks/aaa_dlss_mask_carrier_invalid_sdk_smoke.csv`
  - Invalid-SDK benchmark:
    `out/benchmarks/aaa_dlss_quality_gate_invalid_sdk_smoke.csv`
- `_quick_build.bat` passes for `SelfEngineForward3D`; the existing MSVC
  runtime-library warning remains.
- Latest visual-QA evidence:
  `out/reference_captures/dlss_visual_qa/native_deferred_hdr.csv`,
  `out/reference_captures/dlss_visual_qa/dlss_present.csv`, and
  `out/reference_captures/dlss_visual_qa/summary.json` all reflect the expanded
  769-column rows with 0 frame-graph validation issues.
- Native reports post source `0/0/1` and quality gate `0/0/1`. DLSS-present
  reports evaluate/output `1/1`, post source `1/1/0`, quality gate `1/0/4`,
  quality masks `255/99/156`, and per-input readiness
  `output/camera/object/reactive/transparency/exposure/post/baseline =
  1/1/0/0/0/1/1/0`.
- Screenshot evidence remains nonblank: both captures are `1038x614` with
  8060/8060 sampled central pixels differing from the sampled background. The
  paired comparison samples 14352 pixels with 3581 changed pixels, mean RGB
  delta `31.1256`, and max delta `579`.
- The invalid-SDK smoke preserves deterministic fallback: package ready `0`,
  runtime fallback `3`, DLSS output ready `0`, post source requested/active
  `1/0`, quality gate requested/ready `1/0`, blocker mask `221`, and 0
  frame-graph validation issues.
- This proves the renderer can now distinguish experimental visible DLSS SR/DLAA
  output from production DLSS image-quality readiness. Production readiness is
  still intentionally blocked on object motion vectors, reactive/transparency
  masks, and stable reference baselines.

## Slice 4.4 Execution Evidence

- Added renderer-owned DLSS mask carriers. `VulkanSceneRenderTargets` now
  allocates per-frame `DlssBiasCurrentColorMask` and `DlssTransparencyMask`
  images at the active internal render extent using `R8_UNORM`. The command
  buffer clears both masks to zero before DLSS evaluate, transitions them
  through the same NGX-readable path as the other DLSS inputs, and tracks
  per-swapchain-image initialization.
- Extended the engine-owned `TemporalUpscalerEvaluateRequest` so DLSS evaluate
  can receive optional mask resources without leaking NGX details into renderer
  callers. The compiled NGX path binds the neutral bias-current-color carrier to
  `pInBiasCurrentColorMask` and the transparency carrier to
  `pInTransparencyMask`, then reports per-frame binding readiness through
  `TemporalUpscalerEvaluateStatus`.
- FrameGraph now records `DlssBiasCurrentColorMask` and
  `DlssTransparencyMask` as physical per-frame resources only when temporal
  upscaler evaluate is active, and `TemporalUpscalerEvaluate` reads them in the
  active DLSS path.
- Updated `scripts/Test-DlssVisualQa.ps1` so the DLSS-present run must observe
  reactive/transparency mask readiness while production quality remains blocked
  on object motion vectors and reference baselines.
- Verification commands:
  - `_quick_build.bat`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
- `_quick_build.bat` passes for `SelfEngineForward3D`; the existing MSVC
  runtime-library warning remains.
- Latest visual-QA evidence:
  `out/reference_captures/dlss_visual_qa/native_deferred_hdr.csv`,
  `out/reference_captures/dlss_visual_qa/dlss_present.csv`, and
  `out/reference_captures/dlss_visual_qa/summary.json` report 769-column rows
  with 0 frame-graph validation issues.
- Native reports post source `0/0/1` and quality gate `0/0/1`. DLSS-present
  reports evaluate/output `1/1`, post source `1/1/0`, quality gate `1/0/4`,
  quality masks `255/123/132`, and per-input readiness
  `output/camera/object/reactive/transparency/exposure/post/baseline =
  1/1/0/1/1/1/1/0`.
- Screenshot evidence remains nonblank: both captures are `1038x614` with
  8060/8060 sampled central pixels differing from the sampled background. The
  paired comparison samples 14352 pixels with 2020 changed pixels, mean RGB
  delta `5.0435`, and max delta `566`.
- The invalid-SDK smoke preserves deterministic fallback: package ready `0`,
  runtime fallback `3`, DLSS output ready `0`, post source requested/active/
  fallback `1/0/3`, quality gate requested/ready `1/0`, quality masks
  `255/34/221`, mask readiness `0/0`, and 0 frame-graph validation issues.
- This removes the reactive/transparency mask-carrier blocker from the DLSS
  production-quality gate. Production DLSS image quality is still intentionally
  blocked on object motion vectors and stable visual baselines, so the next
  slice should focus on previous-transform/object-velocity correctness.

## Slice 4.5 Execution Evidence

- Added opaque 3D object-motion readiness for the DLSS quality gate.
  `RenderCommand` now carries `previousModel`, the 3D render-queue cache seeds
  it from the previous command model when a renderable identity persists, and
  scene-queue reuse keeps static commands neutral by copying current model into
  previous model.
- Expanded `ObjectPushConstants` and every shader `ObjectPushConstants` block
  with `previousModel`, then updated `gbuffer_3d.vert` so previous clip
  positions are computed from the previous object transform instead of the
  current transform.
- Recomputed DLSS object-motion readiness after GBuffer, WBOIT, and
  forward-residual command splitting. The gate only marks object motion ready
  when the current 3D frame is covered by opaque GBuffer velocity with no
  transparent or forward-special gap.
- Verification commands:
  - `_quick_build.bat`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
  - Invalid-SDK benchmark:
    `out/benchmarks/aaa_dlss_object_velocity_invalid_sdk_smoke.csv`
- Latest visual-QA evidence reports native quality gate `0/0/1`; DLSS-present
  evaluate/output `1/1`, post source `1/1/0`, quality gate `1/0/9`, quality
  masks `255/127/128`, and per-input readiness
  `output/camera/object/reactive/transparency/exposure/post/baseline =
  1/1/1/1/1/1/1/0`.
- The paired screenshots are `1038x614`, both nonblank, and the comparison
  samples 14352 pixels with 3501 changed pixels, mean RGB delta `22.748`, and
  max delta `580`.
- The invalid-SDK smoke preserves deterministic fallback with package/runtime
  fallback `3/3`, post source `1/0/3`, and 0 frame-graph validation issues.
- This removes the core opaque object-motion blocker. Production readiness is
  still blocked by reference-baseline evidence and broader content coverage.

## Slice 4.6 Execution Evidence

- Added a renderer-owned DLSS visual-QA baseline manifest at
  `docs/reference_baselines/dlss_visual_qa_baseline.json`. The manifest records
  the controlled grid-scene environment, expected native/DLSS quality-gate
  states, and screenshot comparison thresholds for the experimental
  DLSS-present route.
- Added runtime reference-baseline readiness through explicit
  `SE_DLSS_REFERENCE_BASELINE_PATH` / `SE_DLSS_VISUAL_BASELINE_PATH` values. The
  renderer only treats the reference-baseline input as ready when the provided
  manifest path exists as a non-empty regular file; `Test-DlssVisualQa.ps1`
  supplies the controlled grid-scene manifest path.
- Updated `scripts/Test-DlssVisualQa.ps1` to require the baseline manifest,
  pass its path into the DLSS-present benchmark environment, assert that the
  production DLSS quality gate passes, and validate current CSV/screenshot
  metrics against the manifest thresholds.
- Verification commands:
  - `_quick_build.bat`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
  - Invalid-SDK benchmark:
    `out/benchmarks/aaa_dlss_reference_baseline_invalid_sdk_smoke.csv`
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Latest visual-QA evidence:
  `out/reference_captures/dlss_visual_qa/summary.json` reports the baseline
  manifest path, matching 769-column rows, and 0 frame-graph validation issues.
  Native remains post source `0/0/1` and quality gate `0/0/1`. DLSS-present now
  reports evaluate/output `1/1`, post source `1/1/0`, production quality gate
  `1/1/0`, quality masks `255/255/0`, and per-input readiness
  `output/camera/object/reactive/transparency/exposure/post/baseline =
  1/1/1/1/1/1/1/1`.
- The latest paired screenshots are `1038x614`, both nonblank, and the
  comparison samples 14352 pixels with 3492 changed pixels, mean RGB delta
  `22.9727`, and max delta `582`, all within the baseline manifest thresholds.
- The invalid-SDK smoke preserves deterministic fallback even with the baseline
  manifest present: package/runtime fallback `3/3`, DLSS output ready `0`, post
  source `1/0/3`, quality gate `1/0/2`, reference-baseline ready `1`, and 0
  frame-graph validation issues.
- This proves the controlled opaque deferred grid scene can pass the DLSS
  production-quality gate. It is not yet a default presentation change or a
  broad-content production claim; transparent/WBOIT, forward-special residuals,
  animated/skinned/imported assets, and stricter per-scene baselines remain the
  next coverage work.

## Slice 4.7 Execution Evidence

- Added a second renderer-owned DLSS visual-QA baseline manifest at
  `docs/reference_baselines/dlss_wboit_visual_qa_baseline.json` for the
  transparent grid / WBOIT route. This manifest intentionally expects DLSS
  output and post-source activation while preserving a production-quality gate
  blocker for object-motion coverage.
- Extended `scripts/Test-DlssVisualQa.ps1` so the default visual-QA run now
  executes four scenarios: native opaque deferred HDR, DLSS-present opaque
  deferred HDR, native WBOIT transparent deferred HDR, and DLSS-present WBOIT
  transparent deferred HDR. The script validates both baseline manifests, CSV
  quality-gate strings, WBOIT draw/resolve counters, screenshots, and image
  comparison thresholds.
- Verification command:
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
- Latest visual-QA evidence:
  `out/reference_captures/dlss_visual_qa/summary.json` reports both baseline
  manifests, matching 769-column rows for every CSV, and 0 frame-graph
  validation issues.
- Opaque DLSS-present remains production-quality ready for the controlled grid
  baseline with evaluate/output `1/1`, post source `1/1/0`, quality gate
  `1/1/0`, quality masks `255/255/0`, and per-input readiness
  `1/1/1/1/1/1/1/1`.
- WBOIT DLSS-present reports evaluate/output `1/1`, post source `1/1/0`,
  quality gate `1/0/4`, quality masks `255/251/4`, and per-input readiness
  `1/1/0/1/1/1/1/1`, proving transparent coverage is visible but not
  production-cleared. It records 79 WBOIT draws, one WBOIT resolve, and 0
  forward-residual draws.
- Latest screenshots are nonblank. Opaque native-vs-DLSS comparison samples
  14352 pixels with 3494 changed pixels, mean RGB delta `22.9112`, and max
  delta `581`. WBOIT native-vs-DLSS comparison samples 14352 pixels with 257
  changed pixels, mean RGB delta `0.9693`, and max delta `564`, within the
  WBOIT baseline manifest thresholds.
- This expands DLSS coverage to the transparent WBOIT path without weakening
  the production-quality gate. Remaining broad-content work includes
  forward-special residuals, imported/material stress scenes, animated/skinned
  object velocity, and real non-neutral reactive/transparency masks.

## Slice 4.8 Execution Evidence

- Added `SE_BENCHMARK_FORWARD_SPECIAL_MATERIAL=1` to the Forward 3D benchmark
  scene. The switch marks the green benchmark material as
  `MaterialRenderClass::ForwardSpecial`, producing a deterministic
  forward-residual workload without relying on external assets.
- Added a third renderer-owned DLSS visual-QA baseline manifest at
  `docs/reference_baselines/dlss_forward_special_visual_qa_baseline.json` for
  the forward-special residual route. This manifest intentionally expects DLSS
  output and post-source activation while preserving a production-quality gate
  blocker for object-motion coverage across forward-residual draws.
- Extended `scripts/Test-DlssVisualQa.ps1` so the default visual-QA run now
  executes six captures: native opaque, DLSS opaque, native WBOIT, DLSS WBOIT,
  native forward-special residual, and DLSS forward-special residual. The
  script validates all three baseline manifests, CSV quality-gate strings,
  forward-residual draw counters, screenshots, and image comparison thresholds.
- Verification commands:
  - `_quick_build.bat`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 3`
- Latest visual-QA evidence:
  `out/reference_captures/dlss_visual_qa/summary.json` reports all three
  baseline manifests, matching 769-column rows for every CSV, and 0
  frame-graph validation issues.
- Forward-special DLSS-present reports evaluate/output `1/1`, post source
  `1/1/0`, quality gate `1/0/4`, quality masks `255/251/4`, and per-input
  readiness `1/1/0/1/1/1/1/1`, proving the residual path is DLSS-visible but
  not production-cleared. It records 79 hybrid forward-special draws, 79
  forward-residual draws, 79 shared-light-list residual draws, one
  forward-special material, and 0 WBOIT draws.
- Latest screenshots are nonblank. Opaque native-vs-DLSS comparison samples
  14352 pixels with 3877 changed pixels, mean RGB delta `19.8901`, and max
  delta `668`; WBOIT comparison samples 14352 pixels with 308 changed pixels,
  mean RGB delta `1.1214`, and max delta `564`; forward-special comparison
  samples 14352 pixels with 189 changed pixels, mean RGB delta `0.8807`, and
  max delta `564`.
- This expands DLSS coverage to the forward-special residual path without
  weakening the production-quality gate. Remaining broad-content work includes
  imported/material stress scenes, animated/skinned object velocity, forward
  residual motion-vector policy, and real non-neutral reactive/transparency
  masks.

## Slice 4.9 Execution Evidence

- Added a fourth renderer-owned DLSS visual-QA baseline manifest at
  `docs/reference_baselines/dlss_material_stress_visual_qa_baseline.json` for a
  complex opaque material stress route. The scene enables specular texture, UV
  transform, double-sided, clearcoat texture, clearcoat roughness texture,
  transmission texture, and volume material inputs while staying fully in the
  deferred opaque/GBuffer route.
- Extended `scripts/Test-DlssVisualQa.ps1` so the default visual-QA run now
  executes eight captures: native/DLSS pairs for opaque, WBOIT,
  forward-special residual, and material stress. The script validates all four
  baseline manifests, CSV quality-gate strings, draw-route counters, material
  counters, screenshots, and image comparison thresholds. The default screenshot
  delay is now 3 seconds so the expanded matrix remains practical.
- Verification command:
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
- Latest visual-QA evidence:
  `out/reference_captures/dlss_visual_qa/summary.json` reports all four
  baseline manifests, matching 769-column rows for every CSV, and 0
  frame-graph validation issues.
- Material-stress DLSS-present reports evaluate/output `1/1`, post source
  `1/1/0`, production quality gate `1/1/0`, quality masks `255/255/0`, and
  per-input readiness `1/1/1/1/1/1/1/1`, proving this complex opaque material
  scene is production-cleared by the current DLSS gate. It records draw route
  `242/0/0` for GBuffer/forward-residual/WBOIT, one each of specular-texture,
  UV-transform, double-sided, clearcoat-texture,
  clearcoat-roughness-texture, transmission-texture, and volume material
  counters, plus three textured materials.
- Latest screenshots are nonblank. Opaque native-vs-DLSS comparison samples
  14352 pixels with 3957 changed pixels, mean RGB delta `19.2564`, and max
  delta `670`; WBOIT comparison samples 14352 pixels with 301 changed pixels,
  mean RGB delta `1.16`, and max delta `564`; forward-special comparison
  samples 14352 pixels with 168 changed pixels, mean RGB delta `0.9347`, and
  max delta `564`; material-stress comparison samples 14352 pixels with 284
  changed pixels, mean RGB delta `1.0845`, and max delta `564`.
- This expands production-cleared DLSS coverage beyond the controlled opaque
  grid into complex opaque material inputs. Remaining broad-content work
  includes imported-model baselines, animated/skinned object velocity, forward
  residual and WBOIT object-motion policy, and real non-neutral
  reactive/transparency masks.

## Slice 4.10 Execution Evidence

- Moved forward-special/residual composition for the deferred-HDR route into
  the pre-upscale HDR path instead of drawing it only after DLSS in the
  swapchain main pass. `VulkanHdrRenderPass` now loads `SceneDepth` as a
  read-only depth attachment, `VulkanHdrFramebuffer` binds `HDRSceneColor` plus
  `SceneDepth`, and command recording draws the residual commands into
  `HDRSceneColor` before auto exposure, DLSS evaluate, bloom, and final
  composite.
- Added HDR forward-residual graphics pipeline variants and kept the older
  swapchain residual path as a fallback/reference. `forward_3d.frag` now skips
  display tone mapping for the HDR residual route so the pass writes linear HDR
  into `HDRSceneColor`.
- Added a FrameGraph-visible `ForwardResidualHdrPreUpscale` pass between
  weighted-translucency resolve and temporal upscaler evaluation. It reads
  `HDRSceneColor`, `SceneDepth`, the shared light-list carrier when present,
  and IBL resources when allocated, then writes `HDRSceneColor`.
- Verification: `_quick_build.bat` passes for `SelfEngineForward3D`. The focused
  probe `out/benchmarks/aaa_dlss_forward_special_preupscale_probe_dlss.csv`
  reports `framegraph_validation_issues=0`, DLSS evaluate/output `1/1`,
  post source `1/1/0`, quality gate `1/0/4`, quality masks `255/251/4`,
  object motion ready `0`, 79 forward-residual draws, 79 shared-light-list
  residual draws, and `depth_copy_ops=0`.
- The production DLSS gate intentionally remains blocked for forward-special
  content. The pass-ordering problem is now addressed, but residual/transparent
  content still needs real velocity/depth/material policy before the gate can
  mark object motion ready.

## Slice 4.11 Execution Evidence

- Added a pre-upscale forward-special residual velocity pass. The new
  `forward_velocity_3d.frag` reuses the temporal clip-position payload from the
  3D vertex path, respects masked material alpha/opacity discard, and writes
  residual motion into the existing `Velocity` target before DLSS evaluation.
- Added `VulkanForwardResidualVelocityRenderPass` and framebuffer ownership for
  a velocity-only color attachment plus read-only `SceneDepth`, wired
  `PipelineSpec::ForwardResidualVelocity3D`, command-buffer recording, CSV bind
  counters, and a FrameGraph-visible `ForwardResidualVelocityPreUpscale` pass.
- The DLSS object-motion gate now treats forward-special residuals as ready only
  when every residual command is covered by the velocity pass. Transparent/WBOIT
  content remains blocked until it has real transparent/object-motion policy.
- Verification: `_quick_build.bat` passes for `SelfEngineForward3D`. The
  focused probe
  `out/benchmarks/aaa_dlss_forward_special_velocity_probe_dlss.csv` reports
  `framegraph_validation_issues=0`, DLSS output/post source `1/1` and `1/1/0`,
  production quality gate `1/1/0`, masks `255/255/0`, object motion ready `1`,
  79 forward-residual draws, 79 forward-residual velocity draws, 79 shared
  light-list residual draws, and `depth_copy_ops=0`.
- The WBOIT guard probe
  `out/benchmarks/aaa_dlss_wboit_velocity_guard_probe.csv` still reports
  quality gate `1/0/4`, masks `255/251/4`, object motion ready `0`, 79 WBOIT
  draws, 0 forward-residual velocity draws, and `depth_copy_ops=0`.
- Full visual QA now uses an 8-second capture delay to avoid grabbing DLSS
  windows before their first stable present. `scripts\Test-DlssVisualQa.ps1
  -SkipBuild -CaptureDelaySeconds 8` passes with matching `772/772` CSV columns
  and 0 frame-graph validation issues. The forward-special DLSS capture reports
  quality gate `1/1/0`, masks `255/255/0`, per-input readiness
  `1/1/1/1/1/1/1/1`, and 79 residual velocity draws. WBOIT remains correctly
  blocked at `1/0/4`.

## Slice 4.12 Execution Evidence

- Extended the pre-upscale velocity coverage to transparent/WBOIT content by
  reusing `VulkanForwardResidualVelocityRenderPass`, its framebuffer, and the
  forward residual velocity pipeline for weighted-translucency commands. The
  velocity fragment shader now discards fully transparent material alpha so the
  velocity route better matches visible WBOIT coverage.
- Command recording now opens the shared velocity-only pass when either
  forward-special residual velocity commands or WBOIT velocity commands are
  present. WBOIT velocity draws are tracked separately through
  `weighted_translucency_velocity_draws`,
  `weighted_translucency_velocity_material_binds`, and
  `weighted_translucency_velocity_mesh_binds`.
- Added a FrameGraph-visible `WeightedTranslucencyVelocityPreUpscale` pass. The
  DLSS object-motion gate now accepts transparent/WBOIT content only when the
  WBOIT command list is nonempty, every WBOIT command has matching velocity
  coverage, and the velocity render pass, framebuffer, and pipeline are
  allocated. This keeps the gate tied to real draw coverage rather than a
  relaxed readiness flag.
- Verification: `_quick_build.bat` passes for `SelfEngineForward3D`. The
  focused probe
  `out/benchmarks/aaa_dlss_wboit_velocity_probe_dlss.csv` reports
  `framegraph_validation_issues=0`, DLSS output/post source `1/1` and
  `1/1/0`, production quality gate `1/1/0`, masks `255/255/0`, object motion
  ready `1`, 79 WBOIT draws, 79 WBOIT velocity draws, one WBOIT resolve, zero
  forward-residual draws, and `depth_copy_ops=0`.
- Full visual QA passes with matching `775/775` CSV columns and 0 frame-graph
  validation issues across opaque, WBOIT, forward-special, and material-stress
  native/DLSS pairs. The WBOIT DLSS capture reports quality gate `1/1/0`,
  masks `255/255/0`, per-input readiness `1/1/1/1/1/1/1/1`, and
  weighted-translucency counters `79/1/79/0` for draw/resolve/velocity/forward
  residual. Latest WBOIT native-vs-DLSS comparison samples 14352 pixels with
  228 changed pixels, mean RGB delta `0.9675`, and max delta `564`.
- This clears the controlled transparent WBOIT route from the DLSS production
  gate. It still is not a broad production-quality claim for arbitrary content:
  imported-model and skinned velocity, non-neutral reactive/transparency mask
  authoring, larger scene references, temporal stability captures, and default
  presentation policy remain open.
