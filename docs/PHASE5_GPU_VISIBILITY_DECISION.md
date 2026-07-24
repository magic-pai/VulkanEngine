# Phase 5 GPU Visibility Decision

## Scope

The current Phase 5 visibility slice consumes the existing Hi-Z classification on
the GPU. The target is a conservative frame-slot-history visibility path that
prepares indirect draw arguments without a CPU readback or a same-frame depth stall.

The first production contract is deliberately narrower than a Nanite-style renderer:

- classic mesh draw commands remain the source of render identity, bounds, mesh,
  material, and pipeline ownership;
- the regular-Z HZB from the previous compatible use of the swapchain image is the
  visibility producer;
- the GPU classification pass writes one `VkDrawIndexedIndirectCommand` per bounded
  candidate; occluded commands receive `indexCount=0`, while visible and uncertain
  commands preserve the ordinary indexed draw;
- exact candidate count/content, non-jittered camera ViewProjection, projection
  extent, and bounded temporal-jitter delta gate reuse. Any mismatch keeps the
  ordinary direct draw path;
- near-plane, camera-inside, invalid-projection, stale-history, capacity, unsupported
  pipeline, and resource failures preserve the ordinary CPU draw path;
- Debug may read back compact counters, but Release must not read back visibility or
  block on the GPU for classification;
- the first consumers are rigid opaque main-camera commands in the deferred GBuffer
  and legacy forward main pass. Shadow, reflection capture, weighted translucency,
  skinned meshes, instance batches, and unsupported material paths remain explicit
  direct-draw fallbacks until their per-pass contracts are independently proven.

This slice deliberately does not compact commands and does not use an indirect-count
draw yet. Per-object push constants, material descriptor ownership, and mesh bindings
still require individual draw setup, so pretending that the current fixed command
array is full GPU-driven MDI would not reduce the remaining CPU submission work.

## Candidate Matrix

| Candidate | Source / license | Decision | Reason |
| --- | --- | --- | --- |
| AMD FidelityFX SPD | [GPUOpen SPD](https://gpuopen.com/fidelityfx-spd/), MIT-compatible FidelityFX source already vendored under `thirdParty/fidelityfx_sssr` | Adopt for the HZB build follow-up | Production single-pass downsampling, already present in the tree, and avoids inventing another pyramid reducer. The current reducer remains the correctness oracle during migration. |
| Vulkan indirect drawing/count | [Khronos Vulkan Guide](https://docs.vulkan.org/guide/latest/draw_indirect.html), Vulkan specification | Adopt as the API contract | Native Vulkan submission path; avoids a new runtime dependency. Availability and enabled feature bits must be reported and must trigger the ordinary draw fallback when absent. |
| bgfx GPU-driven rendering | [bgfx example 37](https://github.com/bkaradzic/bgfx/tree/master/examples/37-gpudrivenrendering), BSD-2-Clause | Reference only | Useful GPU-driven visibility/argument-generation example, but its renderer abstraction, shader/resource ownership, and backend model do not fit SelfEngine's existing Vulkan render queue. |
| CPU readback of Hi-Z results | Existing SelfEngine Debug audit | Reject for Release | Adds a synchronization/readback stall and breaks the Phase 5 performance goal. Retain only as a Debug correctness oracle. |
| Nanite-style virtualized geometry | Unreal/Nanite architecture references | Defer | Requires cluster hierarchy, page streaming, visibility buffers, material resolve, and a new renderer architecture; explicitly outside this bounded Phase 5 slice. |

### Temporal Jitter Compatibility

| Candidate | Source / license | Decision | Reason |
| --- | --- | --- | --- |
| Non-jittered camera identity plus jittered classification | [Unity `Camera.nonJitteredProjectionMatrix`](https://docs.unity3d.com/ScriptReference/Camera-nonJitteredProjectionMatrix.html), [Unity `Camera.previousViewProjectionMatrix`](https://docs.unity3d.com/ScriptReference/Camera-previousViewProjectionMatrix.html), engine API reference | Adopt | Production engines expose the stable camera projection separately from the per-frame temporal sample. SelfEngine already owns both matrices, so this needs only a narrow internal contract change. |
| Explicit temporal jitter and conservative pixel guard | [AMD FidelityFX FSR2 2.3.3 manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/), MIT FidelityFX source | Adopt | The temporal sample remains part of rasterization and HZB classification. Reuse records the actual sample delta and accepts it only inside an explicit guard that also expands candidate rectangles. |
| Unreal no-AA projection identity | [Unreal Engine `FViewMatrices::GetProjectionNoAAMatrix`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FViewMatrices/GetProjectionNoAAMatrix), engine API reference | Reference | Independently confirms the production convention of retaining a projection without temporal AA offsets. Importing Unreal renderer code is neither license- nor architecture-appropriate. |
| Disable TAA/DLSS jitter for Hi-Z | Diagnostic control only | Reject for production | It makes exact matrix matching pass by removing a required temporal input and visibly regresses anti-aliasing. Keep only as an explicit control lane. |
| Integrate another occlusion-culling library | No compatible owner found | Reject | A library cannot replace SelfEngine's existing render identities, HZB frame-slot lifetime, descriptor ownership, or per-draw bindings without replacing the renderer path. Mature engine contracts are adapted instead of adding a competing runtime. |

The production contract keeps two identities. The classification shader receives the
actual jittered ViewProjection that produced the HZB. Historical reuse compares the
non-jittered ViewProjection, then separately verifies that the actual temporal-sample
delta is within the pixel guard stored with the indirect commands. The same guard
expands the classified screen rectangle. Camera motion, candidate changes, extent or
resource recreation, and a jitter delta outside the guard remain conservative direct
draw fallbacks.

## Data Contract

The strict health gate must prove, for LightingShowcase and an independent Forward3D
control scene:

- HZB producer dimensions, mip count, format, frame revision, and build dispatches;
- candidate and command identity conservation between RenderQueue and GPU buffers;
- indirect argument capacity, consumer readiness, submitted indirect count, direct
  fallback counts, visible/uncertain/culled totals, and content identity hash;
- ordinary draw fallback reason, zero Release host-visible diagnostic allocation,
  and zero Release visibility readback;
- no draw is submitted from stale or invalid visibility history;
- generated triangle count is never greater than the conservative CPU command set;
- the disabled and unsupported-feature controls preserve the original draw path.

## Tool Ownership

- RenderDoc owns dispatch/draw ordering, descriptors, buffer contents, barriers, and
  indirect command inspection.
- Nsight Graphics owns NVIDIA GPU cost and indirect/compute scheduling questions.
- Vulkan Validation owns synchronization, resource lifetime, and feature legality.
- SelfEngine Debug counters own render identity, fallback policy, and producer/consumer
  conservation semantics.

## Limits

This document does not claim that the full Phase 5 is complete. Asset metadata,
instance compaction, material/mesh bucketing, compacted multi-draw-indirect command
generation, meshlet compatibility metadata, FidelityFX SPD HZB replacement, and
broader per-pass LOD/visibility consumers remain separate slices.

## Implemented Evidence

### Jitter-Compatible Contract V3 (2026-07-25)

- `scripts\Test-GpuOcclusionHealth.ps1 -SkipBuild -Strict -OutputDirectory
  tmp\gpu_occlusion_jitter_v3` passes `104 / 0` across real Native TAA jitter,
  Forward3D FBX, zero-jitter control, real DLSS Performance jitter, camera motion,
  disabled, and Release lanes. Unexpected Vulkan Validation messages are `0`;
  the DLSS lane isolates exactly two previously attributed
  `nv.ngx.dlss.resource` / `VUID-vkCmdDraw-None-09600` vendor-internal blocks.
- Native TAA records current jitter `-0.0625/-0.138889`, frame-slot delta
  `0.1875/0.0555556`, and guard `0.5`; DLSS Performance records
  `-0.125/-0.277778`, delta `0.375/0.111111`, and guard `1.0`. Both report
  canonical ViewProjection, extent, and jitter-guard matches and consume indirect
  commands on every steady row. Camera motion reports fallback reason `4` and zero
  indirect submissions. Release consumes `39` indirect commands with readback
  `ready=0/valid=0`.
- RenderDoc 1.44 frame 12 inspection passes `13 / 0`; integration passes `33 / 0`.
  The DLSS Performance render extent owns a `640x360`, 10-mip `R32_FLOAT` HZB,
  one classification dispatch, four distinct named classification resources, and
  `39` prior-frame indirect consumers before command refresh. RenderDoc injection
  disables the NGX evaluate call, so actual DLSS activation is proven by the
  strict non-capture lane, not inferred from the capture.
- Release DLSS Performance Vulkan timestamps record `300` samples per state.
  Disabled/enabled medians are `4.3454/4.5120 ms`, delta `+0.1666 ms`
  (`+3.83%`); p95 is `4.5674/4.9871 ms`. A separate `200`-sample-per-state
  consumer/fallback control records `4.3268/4.3717 ms`, showing no measurable
  penalty from the 39 indirect consumers; the bounded cost is the current HZB
  mip build and classification work targeted by the later FidelityFX SPD slice.
- Nsight Graphics 2026.2 headless capture was trialed through the existing wrapper
  and direct CLI. Injection connected to the Vulkan process, then exited with code
  `1` before producing a capture, including diagnostic mode. No custom replacement
  tooling was added; Vulkan timestamps and RenderDoc remain the reproducible proof
  for this slice, and Nsight GPU Trace remains a follow-up when headless capture is
  available on this machine.

Visual acceptance completed in one interactive `SelfEnginePbrModelShowcase` window
using `assets/models/lvjuren.glb`, DLSS Performance, production temporal jitter,
LOD, and GPU Hi-Z. The model remained interactive and the user judged the overall
result good. LightingShowcase remains a procedural Hi-Z stress/control lane, but
future LOD acceptance must finish on real imported PBR geometry.

- `scripts\Test-GpuOcclusionHealth.ps1 -SkipBuild -Strict -OutputDirectory
  tmp\gpu_occlusion_indirect_health_v3`: `83 pass / 0 fail` with zero
  FrameGraph/Vulkan Validation findings across Debug LightingShowcase, Debug
  Forward3D FBX, static consume, camera-motion fallback, disabled, and Release lanes.
- Static LightingShowcase submits `39` indirect commands and classifies
  `15 visible + 22 occluded + 2 uncertain`; Forward3D submits `6` rigid indirect
  commands while preserving `1` skinned direct fallback. Camera motion invalidates
  reuse conservatively. Release submits `39` indirect commands with readback
  `ready=0/valid=0`.
- RenderDoc 1.44 frame 12 inspection passes `13 / 0`: the physical
  `1280x720`, 11-mip `R32_FLOAT` HZB is fully built, classification binds four
  distinct resources including `SelfEngine.GpuOcclusion.Indirect[2]`, and that
  buffer has `39` indirect uses containing `22` zero-index and `17` visible
  commands before its compute refresh.
- Release Vulkan timestamp comparison records `600` samples per state:
  disabled median `14.6916 ms`, enabled median `14.6576 ms`, delta
  `-0.0340 ms` (`-0.23%`, run noise). The current allocation budget is
  `14,745,156` bytes for the HZB images and `1,229,088` bytes for bounded
  candidate/result/indirect buffers across swapchain images.
