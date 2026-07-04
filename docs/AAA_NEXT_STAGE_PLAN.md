# SelfEngine AAA Next Stage Plan

## Stage

Temporal AA resolve, history, and upscaling readiness.

This stage starts after the reflection/probe/temporal-foundation stage completed
in `d63e55f Add temporal foundation diagnostics`. UE project work stays frozen
except for compile/regression fallout.

## Stage Goal

Turn the new temporal foundation into a measurable first temporal AA path:
renderer-owned history color storage, velocity-reprojected resolve, explicit
history reset/rejection diagnostics, and enough controls to prepare later TAAU,
dynamic resolution, motion blur, SSR/GTAO denoise, and temporal upscaler plugin
boundaries.

The stage is complete when SelfEngine can enable a conservative TAA resolve from
the deferred/HDR path, prove history availability/reset behavior through CSV,
ImGui, and FrameGraph, visualize temporal inputs and rejection, and keep the
feature explicitly off/fallback-safe by default.

## Acceptance Evidence

- CSV exposes TAA configured/enabled state, history color allocation/readiness,
  history format, copy/update count, resolve readiness, blend weight,
  velocity-reprojection state, rejection/clamp state, and fallback reason.
- FrameGraph exposes temporal history color as a persistent history resource,
  the temporal resolve integration point, and the history update/copy handoff.
- HDR composite can consume previous-frame history only when both camera motion
  and history color are ready; first-frame/forced-reset paths must not sample
  stale history.
- `SE_TAA=1` / `SE_TAA_RESOLVE=1` enables the conservative resolve, while the
  default path remains unchanged and reports an explicit disabled fallback.
- Temporal debug views expose at least the resolved TAA contribution or
  current/history difference, plus existing velocity debug coverage.
- Smoke runs cover default off, enabled deferred-HDR, forced history reset,
  jitter-prepared, and WBOIT/deferred-HDR coexistence where relevant.
- No Vulkan validation/VUID/error output is introduced by history image layout,
  descriptor, render-pass, or copy lifetime changes.

## Execution Slices

1. TAA history color carrier and conservative resolve. Implemented.
   - Allocate renderer-owned temporal history color images matching HDR scene
     color format and extent.
   - Bind history color and velocity into HDR composite descriptors.
   - Add `SE_TAA=1` / `SE_TAA_RESOLVE=1` and a conservative history blend
     weight.
   - Copy current HDR scene color into history after composite sampling so the
     next frame has a real history source.
   - Add CSV, ImGui, FrameGraph, and smoke evidence for off/enabled/reset paths.

2. Rejection and clamping diagnostics. Implemented.
   - Add depth/velocity-aware rejection thresholds and neighborhood color clamp
     controls.
   - Report fallback/rejection reasons and debug the rejected-history mask.
   - Cover fast camera motion, forced reset, and no-history cases.

3. Jitter application gate. Implemented.
   - Apply camera jitter only when explicitly enabled and when TAA resolve is
     active enough to hide shimmer.
   - Keep default presentation unjittered.
   - Add jittered projection diagnostics and pixel/UV sequence evidence.

4. Temporal debug views and visual QA hooks. Implemented.
   - Add `SE_RENDER_VIEW=taa` and follow-up views for history, rejection, and
     velocity-reprojected UVs.
   - Add screenshot/reference-capture hooks if the existing renderer tooling is
     sufficient.

5. Temporal consumers readiness.
   - Expose which systems are allowed to consume temporal history: SSR, GTAO,
     motion blur, dynamic resolution, and future upscaler plugins.
   - Add explicit unsupported/fallback reporting instead of silent no-ops.

6. TAAU/dynamic-resolution boundary.
   - Add render-scale/dynamic-resolution diagnostics and a narrow upscaler
     interface contract.
   - Do not implement a native TSR-class upscaler in this stage.

## First Slice To Execute Now

Implemented Slice 1: a renderer-owned TAA history color carrier and conservative
resolve in HDR composite. The first resolve is intentionally modest: no final
artist-quality rejection, no TAAU, no dynamic resolution, and no default visual
change unless explicitly enabled by environment.

## Slice 1 Execution Evidence

- `VulkanSceneRenderTargets` now allocates a persistent temporal history color
  image set matching `HDRSceneColor` format and extent.
- HDR composite descriptors bind `TemporalHistoryColor` at binding 3 and
  `Velocity` at binding 4 so the composite shader can velocity-reproject the
  previous HDR history.
- `SE_TAA=1` / `SE_TAA_RESOLVE=1` configures the conservative resolve, while
  `SE_TAA_HISTORY_WEIGHT` controls the history blend weight. Default rendering
  remains fallback/off.
- HDR composite blends current HDR scene color with velocity-reprojected history
  only when temporal history, history color, and camera velocity are all ready.
- The command buffer initializes cold history images for shader-read layout and
  copies current `HDRSceneColor` into all temporal history images after
  composite sampling, making the next frame's history source real GPU data.
- `SE_RENDER_VIEW=taa` visualizes the current/history HDR difference through the
  HDR composite debug path.
- CSV, ImGui, and FrameGraph expose TAA configured/enabled state, history color
  allocation/readiness/format, copy count, history weight, velocity reprojection
  state, fallback reason, and debug-view state.
- FrameGraph exposes `TemporalHistoryColor`, imports ready persistent history
  before resolve, records active `TAAResolve` only when the resolve can sample
  valid history, and records `TemporalHistoryColorUpdate` for the frame-end copy.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Smoke evidence:
  `out/benchmarks/aaa_taa_history_off_smoke.csv`,
  `out/benchmarks/aaa_taa_history_enabled_smoke.csv`,
  `out/benchmarks/aaa_taa_history_forced_reset_smoke.csv`,
  `out/benchmarks/aaa_taa_history_debug_smoke.csv`, and
  `out/benchmarks/aaa_taa_history_wboit_smoke.csv` all have matching
  624-column rows and 0 frame-graph validation issues.
- The default-off smoke reports configured/enabled `0/0`, history color
  allocated/ready `1/1`, history format `97`, 3 history copies, fallback reason
  `1`, and no TAA debug view.
- The enabled smoke reports configured/enabled `1/1`, history ready `1`,
  3 history copies, history weight `0.2`, velocity reprojection `1`, fallback
  reason `0`, temporal history valid `1`, and camera motion ready `1`.
- The forced-reset smoke reports configured/enabled `1/0`, temporal reset `1`,
  reset reason `4`, camera motion ready `0`, reprojection `0`, and fallback
  reason `3`.
- The TAA debug smoke reports debug view `1` while keeping TAA enabled and
  validation clean.
- The WBOIT coexistence smoke reports TAA enabled `1`, fallback reason `0`,
  5 weighted-translucency draws, one WBOIT resolve, and clean validation.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is a first conservative TAA resolve and history carrier. It does not yet
  implement final neighborhood clamping, depth/normal rejection, jittered
  projection application, object motion vectors, TAAU, dynamic resolution,
  motion blur, SSR/GTAO temporal denoise, or upscaler plugin integration.

## Slice 2 Execution Evidence

- HDR composite now applies conservative history rejection before blending:
  velocity magnitude above `SE_TAA_VELOCITY_REJECTION_THRESHOLD` or depth
  mismatch above `SE_TAA_DEPTH_REJECTION_THRESHOLD` rejects the history sample.
- `SE_TAA_REJECTION=1/0` gates rejection, and `SE_TAA_CLAMP=1/0` gates a
  current-frame five-tap neighborhood clamp over the reprojected history color.
- The default threshold controls are conservative and measurable:
  velocity threshold `0.035` UV units and depth threshold `0.02`; smoke runs
  override velocity threshold to `0.03` where needed.
- `SE_RENDER_VIEW=taa-rejection` visualizes the rejection mask through HDR
  composite: red for rejected history, green for accepted history, and blue for
  unavailable TAA/history.
- CSV and ImGui expose rejection enabled state, neighborhood clamp enabled
  state, velocity/depth thresholds, and rejection-debug view state.
- `_quick_build.bat` passes for `SelfEngineForward3D`; the only build warning
  is the pre-existing MSVC runtime-library conflict warning.
- Smoke evidence:
  `out/benchmarks/aaa_taa_rejection_enabled_smoke.csv`,
  `out/benchmarks/aaa_taa_rejection_debug_smoke.csv`, and
  `out/benchmarks/aaa_taa_rejection_off_smoke.csv` all have matching
  629-column rows and 0 frame-graph validation issues.
- The enabled smoke reports TAA configured/enabled `1/1`, history ready `1`,
  reprojection `1`, fallback `0`, rejection/clamp `1/1`, velocity threshold
  `0.03`, and depth threshold `0.02`.
- The rejection-debug smoke reports the same enabled/rejection/clamp state plus
  rejection debug view `1`.
- The rejection-off smoke reports TAA configured/enabled `1/1`, fallback `0`,
  rejection/clamp `0/0`, default velocity threshold `0.035`, and depth threshold
  `0.02`.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is still a first conservative rejection/clamp tier. It does not yet
  implement final neighborhood variance clipping, normal-aware rejection,
  reactive/transparency masks, object motion vectors, jittered projection
  application, TAAU, dynamic resolution, or temporal denoising consumers.

## Slice 3 Execution Evidence

- Camera jitter application is now explicitly gated by
  `SE_TAA_APPLY_JITTER=1`, `SE_TEMPORAL_APPLY_JITTER=1`, or
  `SE_CAMERA_JITTER_APPLY=1`.
- Jitter is applied to the current frame projection only when jitter is prepared
  and the TAA resolve is actually enabled. Default presentation and cold/reset
  temporal states remain unjittered.
- The existing `SE_TEMPORAL_JITTER=1` / `SE_CAMERA_JITTER=1` path still prepares
  Halton jitter pixels/UVs without applying projection jitter unless the new
  apply gate is also set.
- FrameGraph `TemporalFoundation` now reports explicit TAA-gated projection
  jitter when `temporal_jitter_applied=1`.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Smoke evidence:
  `out/benchmarks/aaa_taa_jitter_prepared_smoke.csv`,
  `out/benchmarks/aaa_taa_jitter_applied_smoke.csv`, and
  `out/benchmarks/aaa_taa_jitter_forced_reset_smoke.csv` all have matching
  629-column rows and 0 frame-graph validation issues.
- The prepared-only smoke reports TAA configured/enabled `1/1`, jitter enabled
  `1`, jitter applied `0`, non-zero jitter pixels/UVs, temporal history valid
  `1`, reset `0`, and fallback `0`.
- The applied smoke reports TAA configured/enabled `1/1`, jitter enabled/applied
  `1/1`, the same non-zero jitter pixels/UVs, temporal history valid `1`, and
  fallback `0`.
- The forced-reset smoke reports TAA configured/enabled `1/0`, jitter enabled
  `1`, jitter applied `0`, history valid `0`, reset `1`, reset reason `4`, and
  fallback reason `3`, proving the apply gate does not jitter cold/reset frames.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is a first explicit jitter-application gate. It does not yet implement
  final jittered-history storage, per-pass jitter policy, jitter-aware UI
  stabilization, dynamic-resolution jitter scaling, or upscaler plugin
  handoff.

## Slice 4 Execution Evidence

- Temporal debug coverage now includes `SE_RENDER_VIEW=taa`,
  `SE_RENDER_VIEW=taa-rejection`, `SE_RENDER_VIEW=taa-history`, and
  `SE_RENDER_VIEW=taa-reprojection`.
- `taa-history` visualizes the velocity-reprojected HDR history color through
  the HDR composite debug path.
- `taa-reprojection` visualizes reprojected history UVs in RG and velocity
  magnitude in B.
- CSV exposes `temporal_taa_history_debug_view_enabled` and
  `temporal_taa_reprojection_debug_view_enabled` alongside the existing TAA and
  rejection debug-view state.
- `_quick_build.bat` passes for `SelfEngineForward3D`; the only build warning
  is the pre-existing MSVC runtime-library conflict warning.
- Smoke evidence:
  `out/benchmarks/aaa_taa_debug_history_smoke.csv` and
  `out/benchmarks/aaa_taa_debug_reprojection_smoke.csv` both have matching
  631-column rows and 0 frame-graph validation issues.
- The history debug smoke reports TAA enabled `1`, history ready `1`, history
  debug view `1`, reprojection debug view `0`, jitter prepared `1`, and jitter
  applied `0`.
- The reprojection debug smoke reports TAA enabled `1`, history ready `1`,
  history debug view `0`, reprojection debug view `1`, jitter prepared `1`, and
  jitter applied `0`.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is debug-view coverage for the first temporal path. It does not yet add
  screenshot/reference-capture automation, visual-diff baselines, temporal
  history inspection tools outside HDR composite, or editor camera bookmarks.

## Previous Stage Summary

- Static light-probe grid, reflection capture source/refresh diagnostics,
  authored cubemap filtering and diffuse lobes, multi-probe blend-weight
  diagnostics, and temporal foundation diagnostics were completed in the
  previous stage.
- The latest temporal foundation split material auxiliary payload out of
  `Velocity`, added previous camera matrices and jitter state to the frame UBO,
  and proved `taaResolveEnabled=0` through smoke runs.

## Non-Goals

- No UE scene parsing, UE export automation, project-browser behavior, or UE
  viewport parity work.
- No Nanite, Lumen, hardware ray tracing, virtual shadow maps, or native TSR.
- No final TSR/TAAU quality target, no motion blur, no SSR/GTAO temporal
  denoiser, and no dynamic-resolution scaling until the first TAA resolve and
  history diagnostics are stable.
- No attempt to make deferred/HDR the default visible path during this stage.
