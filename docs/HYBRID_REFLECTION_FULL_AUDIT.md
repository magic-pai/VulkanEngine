# Hybrid Reflection Full Audit

## Scope

The full audit is an opt-in Debug/development contract for tracing one rendered
frame from scene ownership through RenderQueue, GBuffer, TLAS, FidelityFX SSSR,
Ray Query, DNSR, Apply, temporal reconstruction, and presentation.

Enable it with `SE_HYBRID_REFLECTIONS_FULL_AUDIT=1`. Normal Release frames do
not allocate its readback buffers, scan images, or write audit files. Release
builds compile the Full Audit request to disabled even if the environment
variable is set accidentally.

Pass `-CaptureRenderDocOnFailure` when a strict fast gate is expected to need
GPU-frame inspection. A failed analysis then launches the same executable,
scene, AA mode, and rendered frame through RenderDoc and writes
`observability_escalation.json`; the original strict failure remains the
command verdict even when the `.rdc` capture succeeds.

`SE_HYBRID_REFLECTIONS_FULL_AUDIT_START_FRAME` delays capture until temporal
history and asynchronously prepared scene resources have reached steady state;
the capture wrapper defaults this to frame 12.

## Producers And Identity

- `Renderable3D::RenderIdentity()` is the stable cross-pass identity.
- `reflection_audit_object_id` is the compact per-capture GPU identity.
- `submission_index` is local to one RenderQueue and is never accepted as a
  cross-pass identity.
- TLAS metadata, GBuffer object IDs, ray records, Apply records, and queue
  records must all resolve back to the same scene object and render identity.
- Skinned or otherwise TLAS-ineligible objects remain explicit scene records
  with an exclusion reason rather than disappearing from coverage.

## Output Contract

Readable metadata and summaries:

- `frames.csv`: capture dimensions, counts, stage mask, and metadata readiness.
- `objects.csv`, `instances.csv`, `instance_counters.csv`: scene/TLAS identity,
  bounds, material, transform, and CPU/GPU counter ownership.
- `lights.csv`, `probes.csv`, `queue_commands.csv`: full scene light, probe, and
  per-queue command metadata.
- `audit_index.csv`: compact per-frame invariant counts, including the v8
  HDR reflection-visibility ownership contract, v5
  object-stable mirror Probe contract, v4 single-sided nearest-front-side
  contract, Apply blend mode, DNSR confidence source, shader/blend/actual HDR
  energy, and HDR alpha state.
- `runtime_object_summary.csv`: CPU-derived versus GPU-atomic counters.
- `runtime_receiver_hit_matrix.csv`: receiver-to-hit object attribution.
- `runtime_receiver_quality.csv`: spatial source, identity, luminance, blend,
  and final contribution discontinuity metrics. Unstable Apply pairs are
  partitioned into source transition, same-source/different-hit,
  same-source/same-hit, same-source current miss, and unclassified buckets;
  both pair and large-contribution-jump totals must conserve.
- `benchmark_frame_matches.csv`: explicit producer-frame, delayed GPU-readback,
  and application Benchmark-frame correlation. This prevents swapchain
  readback latency from being mistaken for missing frame coverage.
- `image_stage_contract.csv`: per-capture expected, recorded, and manifested
  image-stage masks. Contract v4 captures nine DNSR carriers plus the four
  applicable composition/present stages: TAA requires `12287`; a ready
  DLSS/DLAA output additionally requires `TemporalUpscaleOutput` (`16383`).
- `dnsr_confidence_quality.csv` and `dnsr_confidence_transitions.csv`:
  per-receiver spatial discontinuities at Intersect/Reproject/Resolve and
  same-pixel stage deltas. Resolve confidence must exactly preserve the
  Reproject-owned history carrier consumed by Apply.
- `reflection_composition_summary.csv`: finite/range/energy/alpha summaries for
  HDR before Apply, HDR after Apply, temporal input/output, and presentation.
  It also records finite/negative counts only at pixels actually consumed by
  Apply, so unused Classify/Intersect texels cannot masquerade as final DNSR
  input failures.
- `image_snapshot_manifest.csv`: format, extent, raw-binary state, optional
  image filename, and object-ID snapshot ownership for every stage.
- `source_manifest.csv` and `analysis.json`: raw-evidence sizes, compact checks,
  findings, and verdict.
- `-ExtendedReport` additionally writes `pipeline.csv`, `resources.csv`, and
  `benchmark_long.csv`. Raw-evidence and verbose-pixel capture imply this mode.
  These expanded browsing views are not part of the default fast gate because
  they duplicate data already present in `benchmark.csv`.

Deep raw evidence:

- `rays.csv`, `apply.csv`, and `gbuffer_samples.csv` retain all captured GPU
  records for object/pixel-level investigation.
- `runtime_apply_discontinuities.csv` retains one row per unstable neighboring
  Apply pair, including both pixels' confidence, blend, source luminance,
  contribution luminance, flags, spatial axis, source class, hit identity, and
  current source confidence. The default fast gate keeps only the matching
  per-receiver classification counts in `runtime_receiver_quality.csv`.
- Image stages are stored losslessly as compact `.bin` payloads. The manifest
  records the exact Vulkan format and dimensions needed to decode them.
- Contract v3 snapshots DNSR Intersect radiance/confidence, Reproject
  radiance/confidence, Prefilter radiance/variance/sample count, and Resolve
  radiance/confidence before the existing HDR Apply/temporal/present stages.
- `SE_HYBRID_REFLECTIONS_FULL_AUDIT_VERBOSE_PIXEL_CSV=1` additionally expands
  every image pixel into `reflection_composition.csv`. This is intentionally
  off by default because one 1280x720 frame produces about 348 MB of text.

## Strict Invariants

`Analyze-HybridReflectionFullAudit.ps1 -Strict` blocks acceptance when:

- Scene, queue, GBuffer, TLAS, ray, and Apply identities disagree.
- Bounds, transforms, light tile ranges, probe readiness, or queue ownership
  are invalid.
- CPU-derived ray/object counters disagree with GPU atomic counters.
- A ray stage is unordered, self-intersects, crosses the receiver hemisphere,
  or resolves an unknown receiver/hit.
- Apply/GBuffer/image values are non-finite, or reflection source radiance is
  negative. Signed replacement deltas are tracked separately and are valid.
  Only `HdrAfterApply` and the matching TAA/DLSS input may carry this signed
  correction; DNSR Intersect/Reproject/Prefilter/Resolve radiance remains
  finite and non-negative at every active consumer.
- A mirror pixel consumes more than one local Probe, its final Probe mask does
  not match the object assignment, the GBuffer assignment disagrees with CPU
  GBuffer-queue metadata, or one mirror object uses multiple Probe identities
  in the same captured frame.
- Raw CSV rows, lossless image bytes, object-ID bytes, summaries, and manifests
  do not conserve their declared counts.
- FrameGraph reports a missing/read-before-write/unused-resource issue.
- Apply shader contribution, Vulkan blend expectation, and observed HDR delta
  do not conserve energy within half-float tolerance.
- The HDR alpha visibility carrier is missing at Apply, or a high-confidence
  mirror hit fails to fully replace its Probe source.
- Captured producer frames, delayed readbacks, and Benchmark rows cannot be
  matched uniquely by record count and capture sequence, or their expected
  renderer-to-application frame offset changes.
- Required image stages are missing, duplicated, mislabeled, use the wrong
  producer frame/image index/extent, or disagree with DLSS output readiness.
- The per-capture object-ID snapshot is missing, duplicated, or does not cover
  the complete internal render extent.
- Apply discontinuity summaries do not conserve their pair-level evidence, or
  a blend discontinuity cannot be attributed to the recorded confidence
  transition and source/hit class. Same-source explicit-current-miss boundaries
  and same-source/same-hit large contribution jumps are blocking warnings;
  ordinary source transitions are reported separately rather than treated as
  proof of an error.
- A selected receiver cannot reach the expected object or DNSR consumer.
- A final-output quality warning remains unexplained. Strict mode treats
  warnings as blockers, not informational success.

## External Tools

Vulkan Validation, Nsight Graphics, and RenderDoc remain first-line tools for
API legality, barriers, descriptors, event ordering, resource inspection, and
GPU timing. They do not know SelfEngine's scene object identity, RenderQueue
ownership, Probe fallback policy, or expected reflection-energy equation.
The full audit complements those mature tools instead of replacing their
API-level responsibilities.

## Acceptance Matrix

Before opening one visual acceptance window, run strict full audit on:

1. `SelfEngineLightingShowcase` with the mirror/metal material stress scene.
2. `SelfEngineForward3D` with the animated `Fist Fight B.fbx` scene.
3. RT disabled.
4. Hybrid consumer disabled.
5. DNSR injection disabled or the matching FidelityFX fallback lane.
6. The explicit skinned/TLAS fallback lane.

All failures and warnings must be zero. A passing data gate proves ownership,
lifecycle, and conservation; the final real-scene window remains the user's
visual quality decision.

## Production Reflection Fusion Gate

The production fusion keeps AMD FidelityFX SSSR/DNSR as the temporal owner
instead of adding a second denoiser history system. Full-rate mirror pixels
with roughness at most `0.08` and confidence at least `0.995` consume the
current Intersect radiance directly; all other pixels retain the normal
Reproject, Prefilter, ResolveTemporal, and Apply chain. NVIDIA NRD remains a
rejected replacement for this slice because it would duplicate the established
history, hit-distance, and invalidation contract without a measured quality
benefit.

Ray Query hits resolve box-projected, mip-aware local Probe IBL before global
IBL fallback. Mirror receivers with roughness at most `0.08` bypass screen-hit
acceptance and source fusion: each full-rate pixel traces Ray Query directly.
A valid hit writes current Intersect radiance with confidence `1` and owns the
Apply replacement with blend weight `1`; a miss, invalid ray, rejected self hit,
or unresolved hit clears current radiance/confidence so the existing Probe term
remains. Non-mirror glossy receivers retain the `0.95` screen confidence
threshold, `0.35` transition band, compatible confidence filtering, and normal
FidelityFX DNSR path. The bounded default cost is two shadowed local lights and
two rectangle visibility samples; all lights still contribute radiance.

Debug reverse controls isolate each policy:

- `SE_HYBRID_REFLECTIONS_SOURCE_FUSION_OFF=1`
- `SE_HYBRID_REFLECTIONS_DIRECT_MIRROR_OFF=1`
- `SE_SSR_FFX_CONFIDENCE_SPATIAL_FILTER_OFF=1`
- `SE_HYBRID_REFLECTIONS_HIT_IBL_OFF=1`
- Existing DNSR injection and Ray Query controls remain independent.

The final data gate records FidelityFX contract `16`, Ray Query contract `6`,
and Full Audit contract `8`. Deferred writes the exact indirect-specular
visibility into HDR alpha. Weighted translucency and Forward residual RGB
blending preserve that carrier; Apply uses `DST_ALPHA + ONE` so
`HDR + (resolved - Probe) * visibility` replaces rather than stacks the Probe
term. `SE_HYBRID_REFLECTIONS_HDR_ALPHA_PRESERVATION_OFF=1` is the Debug reverse
control.

LightingShowcase direct-mirror Full Audit passes `1103 / 0`: `19,611` mirror
candidates conserve as `16,070` Ray Query hits plus `3,541` explicit Probe
fallbacks. Apply consumes current Intersect radiance for `16,066` mirror pixels,
with zero high-confidence partial Probe blends. Shader/blend/actual Apply
luminance is `8500.08 / 8461.97 / 8459.08`, and destination alpha is
`0.8359..1.0`. The direct-mirror reverse control also passes its fallback
contract with enabled/candidate/hit/fallback `0/0/0/0`; animated Forward3D
passes `351 / 0` with zero direct-mirror candidates in that scene.

RenderDoc 1.44 frame `12` independently verifies the physical descriptors and
fixed-function blend state.
Apply bindings `16/17/18` are distinct views of `IntersectRadiance`,
`RadianceHistory`, and `HitConfidenceHistory`. Ray Query bindings `24/25/26`
are distinct six-face multi-mip local specular Probes, matching single-mip
diffuse Probes, and a non-null spatial Probe buffer. Apply is physically
`DstAlpha + One`. The reusable inspection passes `11 / 0`; the combined
RenderDoc gate passes `36 / 0`. The user accepted the visible Release
LightingShowcase direct-mirror result as sufficient for the current baseline.

Production references and selection:

- AMD FidelityFX SSSR remains adopted for traversal/DNSR and its BRDF apply
  contract: https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/
- AMD's official Vulkan sample applies reflection radiance as an owned BRDF
  term; SelfEngine's replacement variant additionally preserves the existing
  Probe baseline for misses: https://github.com/GPUOpen-Effects/FidelityFX-SSSR
- Unreal Engine's production reflection environment documents separate screen,
  ray-traced/Lumen, and capture reflection sources; it is used as an ownership
  hierarchy reference, not copied code: https://dev.epicgames.com/documentation/en-us/unreal-engine/reflections-environment-in-unreal-engine
- NVIDIA NRD remains rejected for this defect because another denoiser cannot
  repair a fixed-function composition no-op and would duplicate DNSR history.
- Khronos Ray Query sample and NVIDIA Vulkan ray-tracing guidance define the
  adopted inline traversal contract:
  https://github.khronos.org/Vulkan-Site/samples/latest/samples/extensions/ray_queries/README.html
  and https://developer.nvidia.com/blog/vulkan-raytracing/
- AMD Hybrid Reflections remains the production reference for assigning screen
  and hardware reflection sources without stacking them:
  https://gpuopen.com/fidelityfx-hybrid-reflections/

`SE_SSR_FFX_ZERO_CONFIDENCE_HISTORY_REJECTION_OFF=1` is the narrow reverse
control for the default Reproject rule that rejects stale hit provenance when
the current source confidence is explicitly zero. The capture wrapper exposes
the same control as `-DisableZeroConfidenceHistoryRejection` and records the
expected state in `launch_contract.json`.

`SE_SSR_FFX_RADIANCE_SANITIZE_OFF=1` disables the default finite/non-negative
radiance boundary shared by Classify, Intersect, Reproject, Prefilter, and
ResolveTemporal. The capture wrapper exposes it as
`-DisableRadianceSanitization`; use it only as a Debug reverse control.

`SE_REFLECTION_PROBE_OBJECT_STABLE_OFF=1` restores per-pixel Probe selection
for a reverse control. The capture wrapper exposes it as
`-DisableObjectStableProbeSelection`. Add
`-EnableHardPixelProbeSwitch` to distinguish winner-take-all object splits
from continuous multi-Probe ghosting. Both controls are expected to fail the
v5 mirror-source consistency checks in an overlapping-Probe mirror scene.

Overlay queue kind 5 may contain Debug light gizmos that deliberately have no
scene-object row. They must instead carry nonzero auxiliary audit/render
identities and valid bounds. Probe index `UINT_MAX` is the aggregate selected
probe state and requires a ready descriptor but does not own a per-probe mip
count. A Ray Query hit on its receiver TLAS instance is rejected before hit
attributes/lighting and reported as `self_hit_rejected_count`; active
`self_hit_count` remains a strict failure.
