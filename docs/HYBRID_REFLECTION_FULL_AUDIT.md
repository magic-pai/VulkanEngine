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
- `audit_index.csv`: compact per-frame invariant counts, Apply blend mode,
  DNSR confidence source, shader/blend/actual HDR energy, and HDR alpha state.
- `runtime_object_summary.csv`: CPU-derived versus GPU-atomic counters.
- `runtime_receiver_hit_matrix.csv`: receiver-to-hit object attribution.
- `runtime_receiver_quality.csv`: spatial source, identity, luminance, blend,
  and final contribution discontinuity metrics.
- `benchmark_frame_matches.csv`: explicit producer-frame, delayed GPU-readback,
  and application Benchmark-frame correlation. This prevents swapchain
  readback latency from being mistaken for missing frame coverage.
- `image_stage_contract.csv`: per-capture expected, recorded, and manifested
  image-stage masks. Contract v3 captures nine DNSR carriers plus the four
  applicable composition/present stages: TAA requires `12287`; a ready
  DLSS/DLAA output additionally requires `TemporalUpscaleOutput` (`16383`).
- `dnsr_confidence_quality.csv` and `dnsr_confidence_transitions.csv`:
  per-receiver spatial discontinuities at Intersect/Reproject/Resolve and
  same-pixel stage deltas. Resolve confidence must exactly preserve the
  Reproject-owned history carrier consumed by Apply.
- `reflection_composition_summary.csv`: finite/range/energy/alpha summaries for
  HDR before Apply, HDR after Apply, temporal input/output, and presentation.
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
  contribution luminance, flags, and spatial axis. The default fast gate keeps
  only the matching per-receiver counts in `runtime_receiver_quality.csv`.
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
- Raw CSV rows, lossless image bytes, object-ID bytes, summaries, and manifests
  do not conserve their declared counts.
- FrameGraph reports a missing/read-before-write/unused-resource issue.
- Apply shader contribution, Vulkan blend expectation, and observed HDR delta
  do not conserve energy within half-float tolerance.
- Captured producer frames, delayed readbacks, and Benchmark rows cannot be
  matched uniquely by record count and capture sequence, or their expected
  renderer-to-application frame offset changes.
- Required image stages are missing, duplicated, mislabeled, use the wrong
  producer frame/image index/extent, or disagree with DLSS output readiness.
- The per-capture object-ID snapshot is missing, duplicated, or does not cover
  the complete internal render extent.
- Apply discontinuity summaries do not conserve their pair-level evidence, or
  a blend discontinuity cannot be attributed to the recorded confidence
  transition.
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
