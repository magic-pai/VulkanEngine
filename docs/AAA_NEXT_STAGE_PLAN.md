# SelfEngine AAA Next Stage Plan

## Stage

Reflection capture, probe lighting, and temporal-readiness deepening.

This stage starts from the current deferred/HDR carrier state and closes the next
highest-value renderer gaps before deferred/HDR can become the default visible
path. UE project work stays frozen except for compile/regression fallout.

## Stage Goal

Make probe lighting and reflection-capture data real renderer-owned systems
instead of debug-only fallbacks, while preparing the renderer for temporal
features that depend on stable lighting, velocity, history, and diagnostics.

The stage is complete when SelfEngine has measurable static light-probe data,
stronger reflection-probe capture/filtering behavior, clear refresh/fallback
policy, and enough probe/temporal diagnostics to make later TAA and dynamic
resolution work evidence-driven.

## Acceptance Evidence

- CSV columns expose probe-grid allocation, dimensions, record layout, update
  count, enabled state, blend strength, and fallback state.
- FrameGraph exposes active physical probe/light-probe resources and the passes
  or integrations that consume them.
- Deferred, legacy forward, and WBOIT-compatible lighting paths either consume
  the same probe data or report an explicit fallback.
- Probe/reflection debug views show selected probes, blending, cubemap sampling,
  capture readiness, and probe-grid contribution without relying on UE viewport
  references.
- Smoke runs cover enabled, disabled, deferred-HDR, WBOIT, and asset-reload or
  refresh-policy cases where relevant.
- No Vulkan validation/VUID/error output is introduced by descriptor, resource,
  or reload lifetime changes.

## Execution Slices

1. Static light-probe grid carrier.
   - Replace the descriptor placeholder for frame binding 9 with a real
     renderer-owned probe-grid SSBO.
   - Populate deterministic probe records with diffuse irradiance and first
     directional lobes.
   - Feed deferred and forward ambient terms through the same grid data.
   - Add CSV, ImGui, FrameGraph, and on/off diagnostics.

2. Probe-grid quality and fallback policy. Implemented.
   - Add grid bounds, blend strength, fallback reason, and unsupported-scene
     diagnostics.
   - Add debug views for interpolated probe contribution and cell occupancy.
   - Add smoke cases for disabled grid, blend-zero fallback, debug views, and
     normal HDR path.

3. Reflection capture resource deepening. Implemented.
   - Add captured-scene resource placeholders with explicit readiness and
     invalidation state.
   - Keep authored cubemap and built-in procedural sources behind the same
     capture-source contract.
   - Add per-probe refresh policy: static, file-signature refresh, forced
     refresh, and future scene-dirty refresh.

4. Production probe filtering. Implemented.
   - Improve authored cubemap filtering toward seam-aware GGX prefiltering.
   - Add diffuse irradiance convolution or SH/light-probe coefficients instead
     of single-color diffuse fallback.
   - Add quality tiers and sample-count diagnostics.

5. Multi-probe blending evidence. Implemented.
   - Strengthen top-4 probe blending with weight normalization, box-projection
     diagnostics, and selected-probe masks.
   - Cover deferred, forward, and WBOIT paths with the same selected-probe set.

6. Temporal foundation handoff. Implemented.
   - Add velocity correctness diagnostics and history-reset state.
   - Add camera jitter state without changing default presentation.
   - Implement the first TAA resolve only after probe/lighting fallbacks are
     stable enough to make temporal artifacts attributable.

## First Slice To Execute Now

Implemented Slice 1: a real static light-probe grid carrier for binding 9. The
initial version is intentionally deterministic and small, because the important
milestone is replacing the unsafe placeholder with a measurable renderer-owned
resource and proving the shader path can consume directional probe data.

## Slice 1 Execution Evidence

- `VulkanProbeGridBuffer` owns a per-frame SSBO for frame descriptor binding 9;
  the previous placeholder binding is gone.
- The first grid is deterministic and static: 4x2x4 probes, 32 total probes,
  7 vec4 records per probe, one diffuse irradiance record, and six directional
  lobe records.
- Deferred lighting, legacy forward, and WBOIT ambient paths sample the same
  grid using trilinear interpolation and normal-weighted directional lobes.
- `SE_PROBE_GRID=1/0` toggles shader integration and
  `SE_PROBE_GRID_BLEND` scales the contribution.
- CSV, ImGui, and FrameGraph expose allocation, enabled state, shader
  integration, update count, fallback count, dimensions, record layout, origin,
  spacing, and blend strength.
- `_quick_build.bat` passes for `SelfEngineForward3D`; the only build output
  warning is the pre-existing MSVC runtime-library conflict warning.
- Smoke evidence:
  `out/benchmarks/aaa_probe_grid_deferred_hdr_smoke.csv`,
  `out/benchmarks/aaa_probe_grid_forward_smoke.csv`,
  `out/benchmarks/aaa_probe_grid_off_smoke.csv`, and
  `out/benchmarks/aaa_probe_grid_wboit_smoke.csv` all have matching 539-column
  rows and 0 frame-graph validation issues.
- The WBOIT smoke uses `SE_BENCHMARK_SCENE=grid` plus transparent material and
  reports 79 weighted-translucency draws, 79 WBOIT bind draws, and one resolve.

## Slice 2 Execution Evidence

- Probe-grid stats now separate configured state from shader integration and
  report fallback reason values: `0` none, `1` disabled, `2` blend zero, `3`
  buffer unavailable, `4` invalid layout, and `5` frame index out of range.
- CSV and ImGui now expose grid bounds, cell count, fallback reason, contribution
  debug state, and cell debug state. The deterministic grid reports 9 cells and
  bounds min/max `-18/-4/-18` to `18/8/18`.
- `SE_RENDER_VIEW=probe-grid` visualizes the interpolated diffuse probe-grid
  contribution through deferred lighting debug index 15.
- `SE_RENDER_VIEW=probe-grid-cell` visualizes probe-grid bounds, cell occupancy,
  grid lines, and out-of-bounds fallback through deferred lighting debug index
  16.
- FrameGraph keeps the allocated `StaticLightProbeGrid` resource visible even
  when the grid is disabled, records `StaticLightProbeGridDebug` for the
  contribution view, and records `StaticLightProbeGridCellDebug` for the cell
  occupancy view.
- `_quick_build.bat` passes for `SelfEngineForward3D` after regenerating
  `deferred_lighting.frag.spv`.
- Smoke evidence:
  `out/benchmarks/aaa_probe_grid_debug_smoke.csv`,
  `out/benchmarks/aaa_probe_grid_cell_debug_smoke.csv`,
  `out/benchmarks/aaa_probe_grid_debug_off_smoke.csv`,
  `out/benchmarks/aaa_probe_grid_blend_zero_smoke.csv`, and
  `out/benchmarks/aaa_probe_grid_quality_deferred_hdr_smoke.csv` all have
  matching 555-column rows and 0 frame-graph validation issues.
- The contribution debug smoke reports fallback reason `0`, shader integration
  `1`, one probe-grid debug draw, and 21 active frame-graph passes. The cell
  debug smoke reports one probe-grid cell debug draw. The disabled smoke reports
  fallback reason `1`, shader integration `0`, no buffer update, and one debug
  draw. The blend-zero smoke reports fallback reason `2`, shader integration
  `0`, and no buffer update. The normal deferred-HDR smoke reports no probe-grid
  debug draw while preserving shader integration and one deferred-lighting draw.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.

## Slice 3 Execution Evidence

- `ReflectionProbe3D` and `RendererReflectionProbe` now carry an explicit
  refresh policy: static `0`, file-signature `1`, forced `2`, and scene-dirty
  `3`.
- Default policy follows capture source intent: built-in procedural probes use
  static, authored cubemaps use file-signature refresh, and captured-scene
  probes use scene-dirty refresh.
- `SE_REFLECTION_PROBE_REFRESH_POLICY=static|file-signature|forced|scene-dirty`
  overrides selected probe policy for diagnostics.
- `SE_REFLECTION_PROBE_SCENE_DIRTY=1` and
  `SE_REFLECTION_PROBE_FORCE_REFRESH=1` expose invalidation/refresh request
  state without implementing dynamic cubemap rendering yet.
- `SE_SCENE_REFLECTION_PROBE_CAPTURED=1` creates a benchmark scene probe whose
  source intent is `CapturedScene`, so captured-scene fallback is proven from
  scene data instead of only renderer overrides.
- Captured-scene probes now report requested count, placeholder allocation,
  placeholder readiness, invalidation count, refresh-request count, selected
  refresh policy, selected placeholder readiness, and selected invalidation
  state in CSV and ImGui.
- FrameGraph exposes `ReflectionCaptureRefreshPolicy` as the refresh-policy
  contract and `CapturedSceneReflectionProbePlaceholder` when a captured-scene
  probe is selected. It also records `ReflectionCaptureRefreshPolicy` and
  `StaticLightProbeGridFallback` diagnostic passes so disabled-but-visible
  resources do not produce validation noise.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Smoke evidence:
  `out/benchmarks/aaa_reflection_capture_refresh_static_builtin_smoke.csv`,
  `out/benchmarks/aaa_reflection_capture_file_signature_authored_smoke.csv`,
  `out/benchmarks/aaa_reflection_capture_captured_placeholder_smoke.csv`,
  `out/benchmarks/aaa_reflection_capture_forced_refresh_smoke.csv`, and
  `out/benchmarks/aaa_reflection_capture_scene_dirty_smoke.csv` all have
  matching 575-column rows and 0 frame-graph validation issues.
- The static built-in smoke reports source `1`, policy `0`, ready `1`,
  fallback `0`, and cubemap sampling `1`.
- The authored file-signature smoke reports source `2`, policy `1`, ready `1`,
  fallback `0`, one authored cubemap loaded, refresh checks, cache hits,
  authored irradiance applied, and cubemap sampling `1`.
- The captured-scene placeholder smoke reports source `3`, policy `3`,
  capture resource ready `0`, fallback reason `3`, placeholder allocated/ready
  `1/1`, and no invalidation until a refresh trigger is present.
- The forced-refresh and scene-dirty smokes both report captured-scene source
  `3`, fallback reason `3`, placeholder allocated/ready `1/1`, invalidated
  `1`, and refresh requested `1`; forced policy reports policy `2`, while the
  scene-dirty smoke reports policy `3` plus scene dirty requested `1`.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is a captured-scene resource placeholder and refresh-policy diagnostic
  contract. It does not implement six-face scene rendering, capture scheduling,
  dynamic cubemap descriptors, temporal capture filtering, or final production
  reflection capture.

## Slice 4 Execution Evidence

- Authored reflection-probe filtering now has a renderer-owned policy surface:
  `SE_REFLECTION_PROBE_FILTER_QUALITY=low|medium|high|ultra` and
  `SE_REFLECTION_PROBE_SEAM_AWARE_FILTER=1/0`.
- Default Medium preserves the previous 64-sample GGX path for the tiny
  authored test fixture; Low reports 16 samples and High reports 128 samples.
- The authored cubemap cache signature now includes filter quality and the
  seam-aware filtering flag, so changing quality or seam policy rebuilds the
  authored mip payload instead of reusing an incompatible cached resource.
- The GGX prefilter bilinear taps can now cross cubemap face boundaries when
  seam-aware filtering is enabled, instead of clamping all taps to the selected
  face edge.
- CSV, ImGui, and FrameGraph expose authored filter quality and seam-aware
  filtering state alongside prefilter mode and sample count.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Smoke evidence:
  `out/benchmarks/aaa_reflection_probe_filter_quality_low_smoke.csv`,
  `out/benchmarks/aaa_reflection_probe_filter_quality_high_smoke.csv`, and
  `out/benchmarks/aaa_reflection_probe_filter_seam_off_smoke.csv` all have
  matching 577-column rows and 0 frame-graph validation issues.
- The Low smoke reports quality `0`, seam-aware `1`, 16 samples, mode `1`,
  resource ready `1`, cubemap sampling `1`, authored loaded `1`, and authored
  irradiance applied `1`.
- The High smoke reports quality `2`, seam-aware `1`, 128 samples, mode `1`,
  resource ready `1`, cubemap sampling `1`, authored loaded `1`, and authored
  irradiance applied `1`.
- The seam-off smoke reports default Medium quality `1`, seam-aware `0`, 64
  samples, mode `1`, resource ready `1`, cubemap sampling `1`, authored loaded
  `1`, and authored irradiance applied `1`.
- Authored cubemap loading now also computes six signed directional diffuse
  lobes from the base cubemap: +X, -X, +Y, -Y, +Z, and -Z. The existing
  single-color irradiance remains the base term, while the lobe deltas provide
  normal-weighted diffuse variation for selected authored probes.
- The frame UBO appends `reflectionProbeDiffuseLobes[24]` after the existing
  fields, preserving previous offsets while carrying six lobes for each top-4
  selected probe. Deferred lighting, legacy forward, and WBOIT blend toward
  the lobe diffuse carrier at rough diffuse roughness while keeping authored
  cubemap sampling for specular.
- CSV, ImGui, and FrameGraph now expose authored diffuse-lobe ready count,
  applied count, lobe count, selected-ready mask, and average lobe energy.
- `_quick_build.bat` passes for `SelfEngineForward3D` after regenerating all
  frame-UBO shader variants.
- Smoke evidence:
  `out/benchmarks/aaa_reflection_probe_diffuse_lobes_authored_smoke.csv`,
  `out/benchmarks/aaa_reflection_probe_diffuse_lobes_forward_smoke.csv`, and
  `out/benchmarks/aaa_reflection_probe_diffuse_lobes_wboit_smoke.csv` all have
  matching 582-column rows and 0 frame-graph validation issues.
- The authored, forward, and WBOIT lobe smokes all report lobe ready/applied
  `1/1`, lobe count `6`, selected lobe mask `1`, average lobe energy
  `0.0681917`, authored irradiance applied `1`, cubemap sampling `1`, and
  clean stdout/stderr logs. The WBOIT smoke also reports 79 weighted
  translucency draws and one resolve.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is still a first production-filtering policy and authored diffuse-lobe
  carrier. It does not implement full irradiance cubemap convolution, baked
  SH import, dynamic scene capture filtering, or final artist quality presets.

## Slice 5 Execution Evidence

- Top-4 reflection-probe selection now reports selected-probe, box-projection,
  scene-owned, and positive-influence masks alongside raw and normalized
  per-slot blend weights.
- When the selection point has zero total raw influence, the renderer records an
  explicit normalization fallback and assigns equal normalized weights instead
  of leaving blend evidence undefined.
- CSV and ImGui expose normalized blend-weight sum, fallback count, masks, and
  top-4 raw/normalized weights. FrameGraph describes
  `SceneReflectionProbeSelection` as selected/box/influence masks and
  normalized weights when available.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Smoke evidence:
  `out/benchmarks/aaa_reflection_probe_blend_weights_three_smoke.csv`,
  `out/benchmarks/aaa_reflection_probe_blend_weights_overflow_smoke.csv`, and
  `out/benchmarks/aaa_reflection_probe_blend_weights_wboit_smoke.csv` all have
  matching 596-column rows and 0 frame-graph validation issues.
- The three-probe and WBOIT smokes report selected count `3`, mask `7`,
  box/scene masks `7/7`, normalized sum `1`, equal weights
  `0.333333/0.333333/0.333333/0`, normalization fallback `1`, and cubemap
  sampling `3`. The WBOIT smoke also reports 79 weighted-translucency draws.
- The overflow smoke reports selected count `4`, dropped count `2`, masks
  `15`, normalized sum `1`, equal weights `0.25` across all four slots,
  normalization fallback `1`, and cubemap sampling `4`.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is diagnostic strengthening for the existing top-4 blend path. It does
  not add new capture images, priority-volume authoring, dynamic scene capture,
  or final artist blending controls.

## Slice 6 Execution Evidence

- The previous placeholder use of `Velocity` for clearcoat roughness and
  transmission payload has been split into a new `GBufferMaterialAux` target, so
  `Velocity` now carries screen-space camera motion vectors.
- The frame UBO appends previous view/projection matrices, temporal jitter
  pixels/UVs, and temporal control flags while keeping default presentation
  unjittered.
- `SE_TEMPORAL_HISTORY_RESET=1` / `SE_TEMPORAL_FORCE_HISTORY_RESET=1` expose a
  forced history-reset path, and `SE_TEMPORAL_JITTER=1` /
  `SE_CAMERA_JITTER=1` expose prepared jitter state without applying it.
- CSV, ImGui, and FrameGraph now expose velocity allocation/format, camera and
  object motion readiness, material-aux migration, temporal history validity,
  reset reason, jitter state, and explicit `taaResolveEnabled=0`.
- FrameGraph exposes `GBufferMaterialAux` and `TemporalFrameState`, records an
  active `TemporalFoundation` pass, and wires temporal history into the GBuffer
  pass only when camera motion is ready.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- Smoke evidence:
  `out/benchmarks/aaa_temporal_foundation_deferred_hdr_smoke.csv`,
  `out/benchmarks/aaa_temporal_foundation_history_reset_smoke.csv`, and
  `out/benchmarks/aaa_temporal_foundation_jitter_smoke.csv` all have matching
  615-column rows and 0 frame-graph validation issues.
- The normal deferred-HDR smoke reports velocity target allocation `1`, velocity
  format `83`, camera motion ready `1`, object motion ready `0`, material aux
  allocated/migrated `1/1`, history valid `1`, history reset `0`, jitter
  disabled, and TAA resolve `0`.
- The forced-reset smoke reports history valid `0`, history reset `1`, reset
  reason `4`, camera motion ready `0`, material aux allocated/migrated `1/1`,
  and TAA resolve `0`.
- The jitter smoke reports jitter enabled `1`, jitter applied `0`, non-zero
  jitter pixels/UVs, history valid `1`, camera motion ready `1`, and TAA
  resolve `0`.
- Smoke stdout/stderr logs contain no `VUID`, validation, error, failed,
  exception, or shader diagnostic matches.
- This is a temporal-readiness handoff. It deliberately does not implement the
  first TAA resolve, object motion vectors, TAA history color, history
  rejection, TAAU, dynamic resolution, motion blur, or temporal denoising.

## Non-Goals

- No UE scene parsing, UE export automation, project-browser behavior, or UE
  viewport parity work.
- No Nanite, Lumen, hardware ray tracing, virtual shadow maps, or native TSR.
- No attempt to make deferred/HDR the default visible path during this stage.
