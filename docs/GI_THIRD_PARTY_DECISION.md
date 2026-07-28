# SelfEngine GI Third-Party Decision

Date: 2026-07-24

## Decision

SelfEngine should not start the GI phase by inventing a custom fully dynamic
global-illumination algorithm. The next renderer-mainline GI slice is:

`Static Irradiance Probe Volume Foundation`

This means the existing `StaticLightProbeGrid` / probe-grid SSBO carrier should
be promoted from deterministic placeholder data into scene-derived static or
slowly changing diffuse irradiance data. It is a production-minded first GI
tier, not a Lumen replacement, not fully dynamic multi-bounce GI, and not final
RTX/path-traced GI.

The first implementation should keep third-party dynamic-GI SDKs as evaluated
candidates, while adapting the mature architecture used by shipping engines:
probe/irradiance volumes, stable authored or baked data, explicit fallback
reasons, debug views, and cross-scene validation.

## Current SelfEngine State

SelfEngine already has the consumer side of a probe-volume-like carrier:

- `SE_PROBE_GRID=1/0` and `SE_PROBE_GRID_BLEND`.
- `StaticLightProbeGrid` FrameGraph visibility.
- A `4x2x4` probe-grid SSBO with 32 probes.
- Seven `vec4` records per probe: one diffuse irradiance record plus six
  directional-lobe records.
- Deferred, legacy forward, and WBOIT shader sampling paths.
- `SE_RENDER_VIEW=probe-grid` and `SE_RENDER_VIEW=probe-grid-cell`.
- CSV/ImGui diagnostics for allocation, dimensions, bounds, fallback reason,
  debug-view state, and update count.

The missing part is the producer. The current producer is
`BuildDeterministicProbeGridData()` in `src/renderer/vulkan/renderer.cpp`; it
generates a stable procedural gradient. That is useful for validating descriptor
layout and shader consumption, but it is not real GI.

## Candidate Survey

| Candidate | Decision | Why |
| --- | --- | --- |
| AMD FidelityFX Brixelizer GI | Keep as later dynamic-GI backend candidate | Open FidelityFX route with Vulkan support and a real GI solution, but it brings sparse SDF/Brixelizer context, denoising/composite contracts, history inputs, and a large integration surface. Too large for the first GI slice. |
| NVIDIA RTXGI / DDGI | Keep as optional RTX/DDGI research candidate and algorithm reference | The DDGI/probe-volume model matches the direction of SelfEngine's probe grid, but integration requires a real ray-tracing acceleration-structure and RT pipeline contract. The license and platform capability path must be reviewed before vendoring. |
| NVIDIA RTXGI v2 NRC/SHaRC | Reject for first slice | More advanced radiance-cache/path-tracing-oriented technology. Useful research later, not a stable first raster-renderer GI tier. |
| AMD FSR Radiance Caching | Reject for first slice | Current positioning is a path-tracing/radiance-cache feature, not the lowest-risk raster probe-volume GI foundation. |
| Unity HDRP Adaptive Probe Volumes | Use as production architecture reference | Not a drop-in library for SelfEngine, but a strong reference for probe-volume data ownership, streaming/volume behavior, and validation expectations. |
| Unreal Engine Volumetric Lightmaps | Use as production architecture reference | Not a drop-in library, but a strong reference for baked/static indirect lighting sampled by movable objects. |
| Godot SDFGI | Reference only | Open-source and useful for studying an SDF-based GI path, but it is a larger dynamic-GI system than the first SelfEngine GI step. |
| Intel Embree + Open Image Denoise | Evaluate for offline/static baking tools, not runtime | Mature third-party building blocks may be useful for a CPU/offline probe baker if SelfEngine needs geometry ray tests or denoised bake output without writing ray kernels. This should stay outside the normal runtime frame cost. |

## Selected First GI Slice

The first GI slice should convert the existing probe-grid carrier into a real
scene-owned static irradiance volume.

Required contract:

- Producer: scene-owned or generated static probe-volume data, not the
  deterministic gradient placeholder. Acceptable first sources are a
  SelfEngine-authored probe-volume asset, an offline-baked probe asset, or a
  clearly labeled static bake tool.
- Resource: renderer-owned probe-grid SSBO or its successor. Prefer SH or an
  engine-documented directional-lobe representation based on established
  irradiance-volume practice. If the existing six-lobe record is kept, document
  its quality limits.
- Consumers: deferred, legacy forward, and WBOIT diffuse ambient/indirect paths.
  Specular reflections remain in the reflection-probe/Ray Query path.
- Fallback: disabled, missing producer, invalid layout, zero blend, out-of-bounds
  volume, or explicit IBL-only fallback must be recorded as separate reasons.
- Debug isolation: keep `SE_PROBE_GRID`, add a source/backend control such as
  `SE_GI_BACKEND=static-probe-volume` or `SE_PROBE_GRID_SOURCE=baked`, and keep
  contribution/cell debug views.
- Portability: LightingShowcase is the target scene, but the contract must also
  pass a structurally different control scene before visual review.

Metrics that must be present before visual acceptance:

- GI/probe source type and backend.
- Probe count, dimensions, spacing, bounds, and cell count.
- Coefficient/record layout and coefficient energy min/max/average.
- Producer revision/hash and update count.
- Fallback reason and fallback count.
- Consumer integration flags for deferred, forward, and WBOIT.
- In-bounds/out-of-bounds sample coverage where measurable.
- CPU bake/import time, GPU buffer size, and per-frame update cost.

## What This Explicitly Does Not Claim

- No claim of fully dynamic GI.
- No claim of multi-bounce Lumen-class behavior.
- No claim that visible emissive meshes emit indirect light unless a bake or
  runtime GI backend explicitly supports it.
- No claim that SSR/fallback probe blending is production-ready. Current
  reflection visual baseline should remain Ray Query + hit IBL while SSR is
  redesigned later.

## Source Links

- AMD FidelityFX Brixelizer: https://gpuopen.com/fidelityfx-brixelizer/
- AMD FidelityFX SDK: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
- NVIDIA RTXGI-DDGI: https://github.com/NVIDIAGameWorks/RTXGI-DDGI
- NVIDIA RTXGI v2: https://github.com/NVIDIA-RTX/RTXGI
- Unity Adaptive Probe Volumes: https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@latest/manual/probevolumes.html
- Unreal Engine Volumetric Lightmaps: https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-lightmaps-in-unreal-engine
- Godot SDFGI: https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_sdfgi.html
- Filament IBL/irradiance documentation: https://google.github.io/filament/Filament.html#lighting/imagebasedlights
- Intel Embree: https://github.com/RenderKit/embree
- Intel Open Image Denoise: https://github.com/RenderKit/oidn
