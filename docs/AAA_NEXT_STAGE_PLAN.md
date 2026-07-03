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

2. Probe-grid quality and fallback policy.
   - Add grid bounds, blend strength, fallback reason, and unsupported-scene
     diagnostics.
   - Add debug views for interpolated probe contribution and cell occupancy.
   - Add smoke cases for disabled grid, missing/empty grid, and normal HDR path.

3. Reflection capture resource deepening.
   - Add captured-scene resource placeholders with explicit readiness and
     invalidation state.
   - Keep authored cubemap and built-in procedural sources behind the same
     capture-source contract.
   - Add per-probe refresh policy: static, file-signature refresh, forced
     refresh, and future scene-dirty refresh.

4. Production probe filtering.
   - Improve authored cubemap filtering toward seam-aware GGX prefiltering.
   - Add diffuse irradiance convolution or SH/light-probe coefficients instead
     of single-color diffuse fallback.
   - Add quality tiers and sample-count diagnostics.

5. Multi-probe blending evidence.
   - Strengthen top-4 probe blending with weight normalization, box-projection
     diagnostics, and selected-probe masks.
   - Cover deferred, forward, and WBOIT paths with the same selected-probe set.

6. Temporal foundation handoff.
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

## Non-Goals

- No UE scene parsing, UE export automation, project-browser behavior, or UE
  viewport parity work.
- No Nanite, Lumen, hardware ray tracing, virtual shadow maps, or native TSR.
- No attempt to make deferred/HDR the default visible path during this stage.
