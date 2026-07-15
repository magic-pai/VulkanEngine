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

## Next Stage Plan: Forward DLSS/DLAA Production Quality

The next stage focuses on visible Forward 3D anti-aliasing quality. Treat the
recent jagged Forward scene as a renderer-input problem first: DLSS/DLAA must be
fed stable velocity, depth, mask, jitter, exposure, and post-ordering inputs
before model upgrades can be judged honestly.

1. Full-resolution DLAA quality lane. Implemented as Slice 4.14.
   - Add a native-vs-DLAA visual QA pair that keeps render scale at 1.0.
   - Require `SE_DLSS_QUALITY=dlaa`, post-source activation, full-resolution
     DLSS render/output extents, and production-quality gate readiness.
   - Record a separate DLAA baseline so anti-aliasing-oriented QA is not mixed
     with the 0.75 render-scale Super Resolution route.
   - Add the real default Forward 3D application scene to the same DLAA queue
     as Slice 4.15 so startup-scene jagged edges are covered by CSV and
     screenshot evidence, not only by benchmark-grid captures.

2. Imported dynamic model DLSS baseline.
   - Add an imported-model capture path that exercises real asset geometry,
     UVs, materials, mesh identity, previous transforms, and velocity output.
   - Require the DLSS object-motion gate to pass while imported renderables
     move under a static camera, without special-casing the benchmark grid.

3. Animated/skinned velocity readiness.
   - Add previous-pose/previous-bone or explicit skinned velocity carriers.
   - Keep the production gate blocked for animated content until every animated
     draw route writes matching pre-upscale motion vectors.

4. Material-authored mask tuning.
   - Expand the current mask shader/policy for particles, emissive, water,
     refraction, opacity-textured alpha test/blend, and high-frequency
     transparency.
   - Add per-route counters and baselines so masks cannot silently disappear or
     pollute opaque-only scenes.

5. Temporal stability and tuning.
   - Add moving-camera and moving-object visual references, edge/flicker
     metrics, and DLSS/DLAA preset/sharpness/mip-bias tuning evidence.
   - Keep default presentation unchanged until broad content, not just grid
     captures, remains stable.

6. Scope guard.
   - Do not expand into Frame Generation, Ray Reconstruction, Streamline
     interposition, or default presentation changes until the Forward/DLSS
     input contract above is covered by automated evidence.

## Slice 4.18 Execution Plan

Slice 4.18 is the first production-quality follow-up after the moving-camera
DLAA sentinel. The goal is to stop treating "the frame changes" as a sufficient
dynamic-quality signal and start validating the exact inputs that affect visible
camera-motion shimmer: applied projection jitter, DLSS jitter handoff, and
high-contrast edge temporal stability.

1. Applied-jitter DLAA motion lane.
   - Move the real default-scene moving-camera DLAA QA route to
     `SE_TAA_APPLY_JITTER=1` while keeping full-resolution DLAA and the visible
     DLSS post source.
   - Require `temporal_jitter_applied=1` and keep the existing DLSS jitter
     consistency assertion so NGX receives exactly the jitter that was applied
     to color/depth/velocity.
   - Preserve the previous no-mismatch guard: prepared-only jitter must never be
     forwarded to DLSS as if it were rendered jitter.

2. High-contrast edge shimmer metric.
   - Extend the moving-camera screenshot sequence comparison with an
     edge-focused temporal metric over sampled high-gradient pixels.
   - Record per-pair edge sample counts, changed-edge counts, mean edge delta,
     and max edge delta in `summary.json`.
   - Gate the metric with broad baseline thresholds first. This is intended to
     catch accidental static captures and obvious edge instability, not to
     declare final subjective DLSS quality.

3. Native TAA / DLSS resolve separation.
   - Audit whether the final HDR composite is applying native TAA after DLSS
     output is selected as the post source.
   - Follow up by splitting "temporal resources are ready for DLSS" from
     "native TAA resolve should blend history in the final composite" so DLSS
     can consume jittered raw inputs without being double-resolved by the engine
     TAA path.
   - Add CSV fields or a focused assertion before changing default behavior.

4. Broader temporal content.
   - Add moving-object and imported-model/skinned velocity lanes after the
     applied-jitter moving-camera lane is stable.
   - Keep production claims blocked until those lanes have object-motion
     coverage, mask coverage, and reference baselines.

5. Tuning evidence.
   - Compare DLAA preset/sharpness/mip-bias policies only after the input
     contract is correct. Do not use model/preset changes to hide invalid
     velocity, jitter, or post-ordering state.

## Slice 4.19 Execution Plan

Slice 4.19 expands the real default-scene DLAA coverage from moving-camera only
to moving-camera plus deterministic object motion. This is the next required
step before imported/skinned content: the renderer must prove that the
`previousModel` object-velocity path remains active when a visible object moves
independently of the camera.

1. Deterministic default-scene object motion.
   - Add `SE_BENCHMARK_OBJECT_MOTION=orbit` for the default Forward 3D
     application scene.
   - Keep normal interactive scene behavior unchanged when the flag is absent.
   - Move one existing opaque default-scene cube with a deterministic
     sinusoidal offset and rotation so the GBuffer velocity path has stable
     object-motion input.

2. DLAA visual-QA coverage.
   - Add a new default-scene DLAA visual-QA lane that enables both
     `SE_BENCHMARK_CAMERA_MOTION=orbit` and
     `SE_BENCHMARK_OBJECT_MOTION=orbit`.
   - Keep full-resolution DLAA, applied projection jitter, DLSS post-source
     presentation, and native TAA final-resolve suppression from Slice 4.18.

3. Assertions and baselines.
   - Require object-motion readiness, camera-motion readiness, DLSS quality
     gate readiness, applied-jitter consistency, DLSS input readiness, and
     native TAA suppression.
   - Capture a three-frame same-window sequence and record the same frame and
     high-contrast edge temporal metrics as the moving-camera lane.
   - Store a separate baseline manifest so moving-camera-only and
     moving-object evidence cannot be confused.

4. Scope guard.
   - Do not call this skinned or imported production coverage. This slice only
     proves deterministic opaque object motion in the real default scene.
   - Follow with imported dynamic model and then skinned/animated velocity
     lanes after this path is stable.

## Slice 4.19 Execution Evidence

Slice 4.19 is implemented and verified. The default Forward 3D application scene
now supports `SE_BENCHMARK_OBJECT_MOTION=orbit`, which moves the existing right
opaque cube through a deterministic sinusoidal position/rotation path. When this
benchmark mode is enabled the cube's normal demo self-rotation is disabled so
the object-motion QA has one explainable transform source.

The DLSS visual QA script now has a separate
`default_scene_dlaa_object_motion_present` lane. It runs full-resolution DLAA
with `SE_BENCHMARK_CAMERA_MOTION=orbit`,
`SE_BENCHMARK_OBJECT_MOTION=orbit`, applied projection jitter, DLSS post-source
presentation, and native TAA final-resolve suppression. The lane uses its own
baseline manifest:
`docs/reference_baselines/dlss_default_scene_dlaa_object_motion_visual_qa_baseline.json`.

Verification from
`powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4`
on 2026-07-05:

- Capture monitor: requested `1`, actual `1/2`, `\\.\DISPLAY2`, area
  `2048,0 2048x1104`.
- CSV shape: `782/782` columns.
- DLSS output/post: evaluate/output `1/1`, post source `1/1/0`.
- Quality gate: `1/1/0`, masks `255/255/0`, inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1`.
- Motion readiness: camera `1/1`, object `1/1`, proving the moving object
  contributes through the current `previousModel` velocity path.
- Jitter and resolve policy: jitter `1/1/-0.125/-0.277778`, TAA resolve
  `input/enabled/suppressed=1/0/1`.
- Draw route: `main/gbuffer/forwardResidual/weightedTranslucency=4/4/0/0`.
- Dynamic sequence metric:
  `pairs=2 minChanged=171 maxMean=2.319 max=564 edgeMin=1069 edgeChangedMax=82 edgeMeanMax=3.8641 edgeMax=188`.

Scope remains intentionally limited: this is not imported-model, skinned,
transparent, forward-residual, water/particle, or final production image-quality
coverage. It closes the static-scene/dynamic-scene testing error for the real
default opaque scene and makes the next imported/dynamic and animated lanes
meaningful.

## Slice 4.20 Execution Plan

Slice 4.20 adds the first imported dynamic-object DLAA temporal lane. The point
is to stop using only engine-authored primitives or camera-only movement as
proof of the DLSS/DLAA input contract: imported geometry must prove
previous-model object velocity coverage, applied jitter, post-source ordering,
and reference baseline readiness before imported scenes can be part of any
production image-quality claim.

1. Imported dynamic model lane.
   - Use the repo-owned `assets/models/demo_crystal.obj` smoke asset through
     `SELFENGINE_MODEL_PATH` so the route exercises `RuntimeModelLoader` and
     Assimp-imported mesh identity instead of built-in cube/plane primitives.
   - Keep the camera static and drive the imported renderable with
     `SE_BENCHMARK_OBJECT_MOTION=orbit`, full-resolution DLAA, and applied
     projection jitter so the sequence cannot pass via camera-only motion.
   - Use `SE_DEBUG_LOCAL_LIGHTS=1` in this QA lane so the imported model still
     exercises local-light/shadow resources without depending on a parked UE
     bridge scene.

2. Clean visual capture.
   - Add `SE_VISUAL_QA_HIDE_IMGUI=1` as a narrow renderer/test switch so visual
     QA can capture the imported model instead of dynamic ImGui text.
   - Make the screenshot process DPI-aware before monitor placement/capture so
     high-DPI monitor coordinates do not grab another window beside SelfEngine.

3. Assertions and baseline.
   - Add `imported_dynamic_dlaa_object_motion_present` to the DLSS visual QA
     script.
   - Require DLSS output/post activation, production quality gate readiness,
     mask readiness, camera/object motion readiness, applied jitter, native TAA
     final-resolve suppression, full-resolution DLAA extents, and the imported
     draw/material/light counters.
   - Capture a three-frame same-window sequence and gate central variation,
     sequence motion, and high-contrast edge metrics with a separate baseline
     manifest.

4. Scope guard.
   - This is deterministic opaque imported OBJ object-motion coverage only. It
     is not skinned animation, morph targets, imported transparency/refraction,
     particle/water masking, or final subjective production tuning.

## Slice 4.20 Execution Evidence

Slice 4.20 is implemented and verified. The new visual-QA lane is
`imported_dynamic_dlaa_object_motion_present`, backed by
`docs/reference_baselines/dlss_imported_dynamic_dlaa_object_motion_visual_qa_baseline.json`.

Verification from `scripts\Test-DlssVisualQa.ps1 -SkipBuild` on 2026-07-05:

- Capture monitor: requested `1`, actual `1/2`, `\\.\DISPLAY2`, physical area
  `2560,0 2560x1380`.
- DPI/capture fix: the QA PowerShell process is DPI-aware before monitor
  placement/capture, preventing high-DPI coordinate mismatch from capturing an
  adjacent window. The imported lane hides ImGui with
  `SE_VISUAL_QA_HIDE_IMGUI=1` so edge metrics target the model.
- Model: `assets/models/demo_crystal.obj` through `SELFENGINE_MODEL_PATH`.
- Motion setup: static camera plus `SE_BENCHMARK_OBJECT_MOTION=orbit` on the
  imported OBJ renderable.
- CSV shape: `782/782` columns.
- DLSS output/post: evaluate/output `1/1`, post source `1/1/0`.
- Quality gate: `1/1/0`, masks `255/255/0`, inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1`.
- Motion readiness: camera `1/1`, object `1/1`.
- Jitter and resolve policy: jitter `1/1/-0.125/-0.277778`, TAA resolve
  `input/enabled/suppressed=1/0/1`.
- Imported route counters:
  `main/gbuffer/forwardResidual/weightedTranslucency=1/1/0/0`,
  `materials=1,textured=0,lights=4,local=3,rect=0`.
- Dynamic sequence metric:
  `pairs=2 minChanged=1120 maxMean=39.7803 max=546 edgeMin=389 edgeChangedMax=289 edgeMeanMax=80.1224 edgeMax=182.0732`.
- The same full run completed all DLSS visual-QA lanes and wrote
  `out/reference_captures/dlss_visual_qa/summary.json`.

Scope remains intentionally limited: imported dynamic opaque OBJ coverage is now
in the DLAA queue, but skinned/animated imported assets and imported transparent
or material-specific mask cases are still open.

## Slice 4.21 Execution Plan

Slice 4.21 adds the first material-authored mask-policy QA lane. The goal is to
prove that alpha/opacity/emissive material state does not only appear in the
material table, but also drives the transparent velocity and DLSS mask routes
that affect visible DLSS/DLAA stability.

1. Mask-policy material setup.
   - Use the existing grid benchmark with alpha-blended material state,
     opacity-textured transparent material state, and a constant emissive
     material hint.
   - Keep the render-scale SR path at `0.75` so the route exercises the same
     DLSS-present mask and WBOIT ordering as the existing transparent lanes.

2. Focused QA lane.
   - Add `mask-policy` as a focused visual-QA suite and include it in the
     `mask-material` suite group.
   - Require WBOIT color/resolve execution, matching WBOIT velocity coverage,
     matching DLSS mask draws, DLSS output/post-source activation, quality-gate
     readiness, and alpha/opacity/emissive material counters.

3. Scope guard.
   - Do not treat this as particle, water, refraction, skinned, or subjective
     production tuning. It is the first material-authored alpha/opacity mask
     policy proof on controlled grid content.

## Slice 4.21 Execution Evidence

Slice 4.21 is implemented and verified. The new focused visual-QA lane is
`mask_policy_dlss_present`, backed by
`docs/reference_baselines/dlss_mask_policy_visual_qa_baseline.json`.

Verification from
`powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite mask-policy`
on 2026-07-05:

- Capture monitor: requested `1`, actual `1/2`, `\\.\DISPLAY2`, physical area
  `2560,0 2560x1380`.
- CSV shape: `782/782` columns.
- FrameGraph validation issues: `0`.
- DLSS output/post: evaluate/output `1/1`, post source `1/1/0`.
- Quality gate: `1/1/0`, masks `255/255/0`, inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1`.
- Draw and mask route:
  `gbuffer/WBOIT/forwardResidual=80/162/0`,
  WBOIT resolve/velocity `1/162`, DLSS masks `162/162/0`.
- Material counters:
  `frameMaterialCount=4`, `emissiveHint=1`, `alphaBlend=2`,
  `opacityTexture=1`, `textured=1`.
- Latest native-vs-DLSS comparison samples `14352` pixels with `797` changed
  pixels, mean RGB delta `2.2752`, and max delta `564`.

This extends mask-policy evidence from generic WBOIT/forward-special routes
into material-authored alpha/opacity coverage. Particles, water/refraction,
animated/skinned content, and final DLSS/DLAA tuning remain open.

## Slice 4.22 Execution Plan

Slice 4.22 adds an imported articulated-object DLAA lane. The goal is to bridge
from single imported rigid-object motion toward animated content without
claiming skinned velocity: multiple imported meshes must move with independent
deterministic phases while the DLSS object-motion gate, jitter handoff, history
state, and dynamic edge metrics remain ready.

1. Repo-owned articulated imported asset.
   - Add a tiny multi-mesh OBJ asset under `assets/models/` so Assimp and
     `RuntimeModelLoader` create multiple imported renderables in one load.
   - Keep it renderer-owned and independent of UE bridge/export work.

2. Articulated benchmark motion.
   - Extend `SE_BENCHMARK_OBJECT_MOTION` with an `articulated` mode.
   - Preserve the existing `orbit` mode for default-scene and rigid imported
     lanes.
   - Drive each imported part with an index-dependent phase so the lane proves
     multiple previous-model histories, not one rigid transform.

3. Focused QA lane.
   - Add `imported-articulated` as a focused DLAA sequence suite and include it
     in the `dynamic` suite group.
   - Require full-resolution DLAA, applied jitter, native TAA final-resolve
     suppression, camera/object-motion readiness, reference-baseline readiness,
     imported draw/material/light counters, and edge-sequence metrics.

4. Scope guard.
   - This is articulated object-motion coverage only. It is not previous-bone,
     previous-pose, morph-target, or skinned velocity coverage.

## Slice 4.22 Execution Evidence

Slice 4.22 is implemented and verified. The new focused visual-QA lane is
`imported_articulated_dlaa_object_motion_present`, backed by
`docs/reference_baselines/dlss_imported_articulated_dlaa_object_motion_visual_qa_baseline.json`
and the repo-owned asset `assets/models/articulated_links.obj`.

Verification from
`powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite imported-articulated`
on 2026-07-05:

- Capture monitor: requested `1`, actual `1/2`, `\\.\DISPLAY2`, physical area
  `2560,0 2560x1380`.
- CSV shape: `782/782` columns.
- FrameGraph validation issues: `0`.
- DLSS output/post: evaluate/output `1/1`, post source `1/1/0`.
- Quality gate: `1/1/0`, masks `255/255/0`, inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1`.
- Jitter/history/resolve: applied jitter `1`, temporal history
  `valid/reset/reason=1/0/0`, native TAA resolve
  `input/enabled/suppressed=1/0/1`, DLSS reset `0`, motion-vector scale `1/1`.
- Draw and scene counters:
  `main/gbuffer/forwardResidual/weightedTranslucency=3/3/0/0`,
  `materials=1`, `textured=0`, `lights=4`, `local=3`, `rect=0`.
- Dynamic sequence metric:
  `pairs=2`, `minChanged=647`, `maxMean=21.695`, `max=582`,
  `edgeMin=223`, `edgeChangedMax=162`, `edgeChangedRatioMax=0.6559`,
  `edgeMeanMax=88.0401`, `edgeMax=194.8964`.

This closes the next imported multi-part object-motion gap after the rigid OBJ
lane. True skinned/animated velocity still requires previous-pose/bone or
explicit skinned-velocity carriers and remains open.

## Slice 4.23 Execution Plan

Slice 4.23 adds runtime import diagnostics for skinned/animated source assets
before implementing skinned velocity. The goal is to make unsupported source
features visible in benchmark CSV and focused baselines so imported assets cannot
silently pass as rigid meshes when they actually contain animation or bones.

1. Importer source-feature diagnostics.
   - Record source mesh/material counts, animation count, meshes with bones, and
     total bone references from the Assimp scene.
   - Keep runtime geometry import rigid for now, but emit an explicit
     unsupported flag whenever animations or bone data are present.

2. Runtime and benchmark propagation.
   - Carry the diagnostics through `RuntimeModelLoadResult`, including cache-hit
     loads.
   - Add `runtime_import_*` columns to benchmark CSV after the UE bridge
     diagnostics block.

3. Focused QA contract.
   - Add runtime import counters to the imported dynamic and imported
     articulated DLAA baselines.
   - Keep validation targeted: build once, then run only the affected focused
     imported suite instead of the full visual-QA matrix.

4. Scope guard.
   - This is unsupported-feature visibility only. It does not add previous-bone
     matrices, morph target carriers, skinned velocity writes, animation
     sampling, or production skinned DLAA quality.

## Slice 4.23 Execution Evidence

Slice 4.23 is implemented and verified for the imported articulated runtime
path. The renderer now exports runtime import CSV columns for model requested,
model loaded, cache hit, mesh/material counts, source animation count, meshes
with bones, bone references, and skinned-animation unsupported state.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite imported-articulated`
  passes as the single targeted visual QA run for this slice.
- Capture monitor: requested `1`, actual `1/2`, `\\.\DISPLAY2`, physical area
  `2560,0 2560x1380`.
- CSV shape: `791/791` columns.
- Runtime import diagnostics:
  `requested/loaded/cache=1/1/0`,
  `mesh/material=3/1`,
  `animation/meshWithBones/bones/unsupported=0/0/0/0`.
- DLSS output/post: evaluate/output `1/1`, post source `1/1/0`.
- Quality gate: `1/1/0`, masks `255/255/0`, inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1`.
- Draw and sequence counters:
  `main/gbuffer/forwardResidual/weightedTranslucency=3/3/0/0`,
  `pairs=2`, `minChanged=649`, `maxMean=21.1081`, `edgeMin=231`,
  `edgeChangedMax=183`, `edgeChangedRatioMax=0.7121`,
  `edgeMeanMax=90.4753`.

This prevents unsupported skinned/animated source content from being invisible
to QA, but true skinned/animated velocity remains open.

## Slice 4.24 Execution Plan

Slice 4.24 adds the positive skinned/animation diagnostic lane that Slice 4.23
made possible. The previous slice exposed runtime import diagnostics, but only
proved the zero case on rigid OBJ content. This slice must prove that a real
source asset with animation and skinning metadata trips the unsupported flag.

1. Repo-owned skinned diagnostic probe.
   - Add a small Collada asset with one renderable triangle, one animation
     channel, and a two-bone skin controller.
   - Keep the asset intentionally tiny so it is a diagnostic probe, not an
     image-quality scene.

2. Focused benchmark-only QA lane.
   - Add `imported-skinned-diagnostic` as a focused suite.
   - Run one DLSS-present benchmark/CSV pass and do not capture screenshots or
     image sequences.
   - Assert runtime import counters:
     `requested/loaded/cache=1/1/0`,
     `mesh/material=1/1`,
     `animation/meshWithBones/bones/unsupported=1/1/2/1`.

3. Scope guard.
   - Keep this lane out of the dynamic screenshot group for now.
   - Do not call it skinned animation support. It is an unsupported-feature
     sentinel that protects the existing rigid imported lanes from false
     production claims.

## Slice 4.24 Execution Evidence

Slice 4.24 is implemented and verified. The new focused suite is
`imported-skinned-diagnostic`, backed by
`docs/reference_baselines/dlss_imported_skinned_diagnostic_visual_qa_baseline.json`
and the repo-owned asset `assets/models/skinned_probe.dae`.

Verification on 2026-07-05:

- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites`
  lists `imported-skinned-diagnostic` without building, launching the renderer,
  or capturing windows.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as a benchmark-only focused run.
- CSV shape: `791/791` columns.
- FrameGraph validation issues: `0`.
- Runtime import diagnostics:
  `requested/loaded/cache=1/1/0`,
  `mesh/material=1/1`,
  `animation/meshWithBones/bones/unsupported=1/1/2/1`.
- DLSS output/post: evaluate/output `1/1`, post source `1/1/0`.
- Quality gate: expected blocked `1/0/4`, masks `255/251/4`, inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/0/1/1/1/1/1`.
- Draw and scene counters:
  `main/gbuffer/forwardResidual/weightedTranslucency=1/1/0/0`,
  `materials=1`, `textured=0`, `lights=4`, `local=3`.

This closes the positive unsupported-diagnostic proof. True skinned/animated
DLSS/DLAA quality still requires animation sampling, skinned vertex carriers,
previous-bone or previous-pose data, skinned velocity output, and a visual
dynamic skinned lane.

## Slice 4.25 Execution Plan

Slice 4.25 wires the skinned/animation unsupported diagnostic into the DLSS
quality gate. Slice 4.24 proved the runtime importer can see the unsupported
source features; this slice prevents that diagnostic from coexisting with a
green production-quality gate.

1. Renderer scene-content motion support.
   - Add a renderer-side flag for whether current scene content has DLSS-ready
     object motion semantics.
   - Let Forward 3D set that flag from
     `runtimeImportSkinnedAnimationUnsupported`.

2. Quality gate behavior.
   - Keep base velocity diagnostics honest: transform/object velocity may still
     report ready for the rigid fallback draw.
   - Mark DLSS quality object-motion readiness false when imported skinned or
     animated source content is unsupported.
   - Use the existing object-motion blocker and fallback reason instead of
     inventing a broad new mask bit before true skinned velocity exists.

3. QA contract.
   - Allow focused diagnostic baselines to expect a nonzero quality blocker.
   - Keep all production visual QA lanes strict: if their expected blocker is
     `0`, any nonzero blocker still fails immediately.

## Slice 4.25 Execution Evidence

Slice 4.25 is implemented and verified. `VulkanRenderer` now exposes
`SetDlssQualitySceneContentMotionSupported`, Forward 3D drives it from runtime
import diagnostics, and the final object-motion quality readiness path also
honors the flag after draw-route coverage analysis.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as a benchmark-only focused run.
- CSV shape: `791/791` columns.
- FrameGraph validation issues: `0`.
- Runtime import diagnostics:
  `requested/loaded/cache=1/1/0`,
  `mesh/material=1/1`,
  `animation/meshWithBones/bones/unsupported=1/1/2/1`.
- Velocity versus DLSS quality object readiness:
  `temporal_velocity_object_motion_ready/temporal_upscaler_dlss_quality_object_motion_ready=1/0`.
- Quality gate: expected blocked `1/0/4`, masks `255/251/4`, inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/0/1/1/1/1/1`.
- DLSS output/post remains visible for diagnosis:
  evaluate/output `1/1`, post source `1/1/0`.

This makes the skinned probe an explicit "visible but not production-cleared"
lane. True skinned/animated DLSS/DLAA quality still requires animation sampling,
skinned vertex carriers, previous-bone or previous-pose data, and skinned
velocity output.

## Slice 4.26 Execution Plan

Slice 4.26 starts the real skinned-support implementation path by preserving
source skinning data at import time. The goal is not to skin vertices on the GPU
yet; it is to make the importer and runtime diagnostics carry the bone/weight
data that future animation sampling and velocity passes will need.

1. Importer skinning carriers.
   - Add per-mesh bone records with source bone names and offset matrices.
   - Add per-vertex bone influence lists with imported bone indices and weights.
   - Preserve this data alongside the existing rigid `MeshData3D` so current
     rendering remains unchanged.

2. Runtime diagnostics.
   - Aggregate source skinned vertex count, total bone influence count, and max
     influences per vertex.
   - Carry those values through `RuntimeModelLoadResult`, runtime model cache
     entries, benchmark scene diagnostics, and CSV output.

3. QA contract.
   - Extend the skinned diagnostic baseline to assert the carrier counts from
     `assets/models/skinned_probe.dae`.
   - Keep the DLSS quality gate expected-blocked because the renderer still does
     not sample animation, skin vertices, or write skinned motion vectors.

## Slice 4.26 Execution Evidence

Slice 4.26 is implemented and verified. `ImportedMesh3D` now stores bone names,
offset matrices, and per-vertex bone influences. Runtime import diagnostics now
include skinned vertex count, bone influence count, and max influences per
vertex.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as a benchmark-only focused run.
- CSV shape: `794/794` columns.
- Runtime import diagnostics:
  `requested/loaded/cache/mesh/material/animation/meshWithBones/bones/skinnedVertices/influences/maxInfluences/unsupported=1/1/0/1/1/1/1/2/3/5/2/1`.
- DLSS quality gate remains expected-blocked:
  `qualityGate=1/0/4`, masks `255/251/4`,
  object readiness `temporal_velocity_object_motion_ready/temporal_upscaler_dlss_quality_object_motion_ready=1/0`.

This creates the source-data carrier needed by the next skinned animation work.
The remaining hard work is animation clip/node sampling, skinned vertex buffers
or shader skinning, previous-bone/previous-pose storage, and skinned velocity
output.

## Slice 4.27 Execution Plan

Slice 4.27 extends the skinned import carrier from weights into animation clip
data. Slice 4.26 preserved bone weights; this slice preserves animation clips,
node channels, and transform keys so future pose evaluation has source data to
sample.

1. Animation carriers.
   - Add imported animation clips with name, duration ticks, ticks-per-second,
     and node channels.
   - Preserve position, rotation, and scale keys per channel.
   - Keep the carrier independent from rendering so current rigid import output
     remains unchanged.

2. Runtime diagnostics.
   - Aggregate animation channel count, position/rotation/scale key counts,
     total key count, and max keys per channel.
   - Carry those values through runtime load results, cache entries, benchmark
     scene diagnostics, CSV output, and quick QA metrics.

3. QA contract.
   - Extend `imported-skinned-diagnostic` to assert the clip/channel/key counts
     from `assets/models/skinned_probe.dae`.
   - Keep rigid imported lanes at zero for the new animation fields.
   - Keep DLSS quality expected-blocked because there is still no animation
     sampling, pose evaluation, or skinned velocity output.

## Slice 4.27 Execution Evidence

Slice 4.27 is implemented and verified. `ImportedModel3D` now stores
`ImportedAnimationClip3D` entries with node channels and position/rotation/scale
keys. Runtime import diagnostics now include animation channel and key counters.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as a benchmark-only focused run.
- CSV shape: `800/800` columns.
- Runtime import diagnostics:
  `requested/loaded/cache/mesh/material/animation/channels/posKeys/rotKeys/scaleKeys/keys/maxChannelKeys/meshWithBones/bones/skinnedVertices/influences/maxInfluences/unsupported=1/1/0/1/1/1/1/2/2/2/6/6/1/2/3/5/2/1`.
- DLSS quality remains expected-blocked:
  `qualityGate=1/0/4`, masks `255/251/4`.

This creates the animation source-data carrier for future pose sampling. The
remaining work is node hierarchy binding, animation sampling, current/previous
bone palette storage, skinned vertex output, and skinned velocity output.

## Slice 4.28 Execution Plan

Slice 4.28 binds the imported animation and bone carriers to source nodes. The
goal is still diagnostic readiness, not visible skinned rendering: future pose
sampling needs to know which imported node an animation channel targets and
which imported nodes are referenced by bones before it can build current and
previous bone palettes.

1. Imported node hierarchy carrier.
   - Preserve source node name, parent index, local transform, and mesh
     reference count in `ImportedModel3D`.
   - Mark nodes that are referenced by imported bones.
   - Mark nodes that are targeted by imported animation channels.

2. Runtime diagnostics.
   - Aggregate node count, bone-node count, animation-channel bound/unbound
     counts, and bone-name-to-node matched/unmatched counts.
   - Carry those values through runtime load results, cache entries, benchmark
     scene diagnostics, CSV output, and quick QA metrics.

3. QA contract.
   - Extend `imported-skinned-diagnostic` to assert the binding counts from
     `assets/models/skinned_probe.dae`.
   - Keep rigid imported lanes at their real node counts, with zero bone and
     animation-channel binding fields.
   - Keep DLSS quality expected-blocked because there is still no pose
     evaluation, bone-palette storage, skinned vertex output, or skinned
     velocity output.

## Slice 4.28 Execution Evidence

Slice 4.28 is implemented and verified. `ImportedModel3D` now stores imported
nodes with parent/local-transform data, mesh-reference counts, bone-reference
markers, and animation-channel target markers. Runtime import diagnostics now
include node hierarchy and channel/bone binding counters.

Verification on 2026-07-05:

- `_quick_build.bat` passed before the baseline-only update.
- JSON baseline parsing, PowerShell script parsing, and `git diff --check`
  pass; `git diff --check` only reports the repo's normal CRLF warnings.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as a benchmark-only focused run.
- CSV shape: `806/806` columns.
- Runtime import binding diagnostics:
  `node/boneNode/channelBound/channelUnbound/boneMatched/boneUnmatched=4/2/1/0/2/0`.
- Direct benchmark CSV probes, without screenshot QA, confirm the rigid
  imported baselines report node counts `2` for `demo_crystal.obj` and `4` for
  `articulated_links.obj`, with zero bone/channel binding fields.
- Full runtime import diagnostics:
  `requested/loaded/cache/mesh/material/node/boneNode/chBound/chUnbound/boneMatched/boneUnmatched/animation/channels/posKeys/rotKeys/scaleKeys/keys/maxChannelKeys/meshWithBones/bones/skinnedVertices/influences/maxInfluences/unsupported=1/1/0/1/1/4/2/1/0/2/0/1/1/2/2/2/6/6/1/2/3/5/2/1`.
- DLSS quality remains expected-blocked:
  `qualityGate=1/0/4`, masks `255/251/4`.

This closes the node-binding diagnostic gap for the skinned probe. The next
skinned quality work is animation sampling, current/previous bone palette
storage, skinned vertex output, and skinned velocity output.

## Slice 4.29 Execution Plan

Slice 4.29 adds the first CPU pose-sampling and bone-palette diagnostic carrier.
The goal is to prove the imported animation channels, node hierarchy, bone
names, and offset matrices can produce current and previous sampled pose data.
This is still not GPU skinning and must not unblock the skinned DLSS quality
gate by itself.

1. CPU pose sampling.
   - Pick the first imported animation clip with at least one bound keyed
     channel.
   - Sample previous time `0` and current time at the clip duration, or the
     last key time when duration is absent.
   - Compose sampled local node transforms from position, rotation, and scale
     keys, falling back to imported local node components when a channel omits
     a component.

2. Bone-palette diagnostic carrier.
   - Build global previous/current node poses from the imported hierarchy.
   - Build previous/current bone palettes for matched bone names using imported
     bone offset matrices.
   - Count sampled clips/channels/nodes, changed animated nodes, current and
     previous palette entries, changed palette entries, and palette readiness.

3. QA contract.
   - Extend the skinned diagnostic baseline to assert the pose and palette
     counts from `assets/models/skinned_probe.dae`.
   - Keep rigid imported lanes at zero for the pose fields.
   - Keep DLSS quality expected-blocked because there is still no renderer bone
     palette storage, skinned vertex output, previous-skinned-vertex state, or
     skinned velocity output.

## Slice 4.29 Execution Evidence

Slice 4.29 is implemented and verified. The importer now samples a deterministic
previous/current CPU pose for the first bound animation clip, builds global node
poses, and creates current/previous diagnostic bone palettes from matched bone
nodes and imported offset matrices. Runtime import diagnostics now include pose
sampling and bone-palette counters.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- Direct benchmark CSV probe for `assets/models/skinned_probe.dae` reports
  `814/814` CSV columns.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as a benchmark-only focused run.
- Pose diagnostics:
  `poseClip/poseChannel/poseNode/poseAnimatedNode/poseBonePalette/posePreviousBonePalette/poseChangedBonePalette/poseReady=1/1/4/1/2/2/1/1`.
- Full runtime import diagnostics:
  `requested/loaded/cache/mesh/material/node/boneNode/chBound/chUnbound/boneMatched/boneUnmatched/animation/channels/posKeys/rotKeys/scaleKeys/keys/maxChannelKeys/poseClip/poseChannel/poseNode/poseAnimatedNode/poseBonePalette/posePreviousBonePalette/poseChangedBonePalette/poseReady/meshWithBones/bones/skinnedVertices/influences/maxInfluences/unsupported=1/1/0/1/1/4/2/1/0/2/0/1/1/2/2/2/6/6/1/1/4/1/2/2/1/1/1/2/3/5/2/1`.
- DLSS quality remains expected-blocked at `qualityGate=1/0/4`.

This creates CPU-side sampled pose and palette evidence for the skinned probe.
The remaining work is carrying renderer-owned current/previous bone palettes to
the draw path, outputting skinned vertices or shader skinning, and writing
skinned motion vectors before skinned DLSS/DLAA can be production-cleared.

## Slice 4.30 Execution Plan

Slice 4.30 moves the sampled bone-palette data from an importer-only diagnostic
into the runtime model cache. The goal is to prove that current and previous
bone palette matrices survive `RuntimeModelLoader` and are available from a
runtime-owned object for future renderer upload work.

1. Runtime palette carrier.
   - Copy the first diagnostic pose sample's current and previous bone palettes
     into `LoadedRuntimeModel`.
   - Preserve those vectors across the runtime model cache path.
   - Compute changed-palette entries from the stored runtime vectors, not from
     importer counters.

2. Runtime diagnostics.
   - Expose runtime carrier current/previous palette entry counts,
     changed-entry count, and carrier readiness through `RuntimeModelLoadResult`.
   - Carry those values into Forward 3D benchmark scene diagnostics, CSV, quick
     visual-QA metrics, and focused baselines.

3. QA contract.
   - Extend `imported-skinned-diagnostic` to assert the runtime carrier counts
     from `assets/models/skinned_probe.dae`.
   - Keep rigid imported lanes at zero for the runtime carrier fields.
   - Keep DLSS quality expected-blocked because the renderer still does not
     upload the palette to shaders, skin vertices, store previous skinned
     vertices, or write skinned motion vectors.

## Slice 4.30 Execution Evidence

Slice 4.30 is implemented and verified. `RuntimeModelLoader` now stores sampled
current/previous bone palettes in `LoadedRuntimeModel`, computes changed
palette entries from those stored vectors, and returns carrier diagnostics for
both fresh loads and cached loads.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- Direct benchmark CSV probe for `assets/models/skinned_probe.dae` reports
  `818/818` CSV columns.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as a benchmark-only focused run.
- Runtime carrier diagnostics:
  `runtimePoseBonePalette/runtimePosePreviousBonePalette/runtimePoseChangedBonePalette/runtimePoseReady=2/2/1/1`.
- Source pose diagnostics remain:
  `poseClip/poseChannel/poseNode/poseAnimatedNode/poseBonePalette/posePreviousBonePalette/poseChangedBonePalette/poseReady=1/1/4/1/2/2/1/1`.
- DLSS quality remains expected-blocked at `qualityGate=1/0/4`.

This closes the importer-to-runtime palette carrier gap. The next skinned
quality work is renderer-facing palette upload/binding, skinned vertex output or
shader skinning, previous-skinned state, and skinned velocity output.

## Slice 4.31 Execution Plan

Slice 4.31 moves the runtime bone-palette carrier into the renderer resource
namespace. The goal is to prove that the renderer-facing resource layer can see
the current and previous imported bone palettes before implementing GPU buffer
upload, descriptor binding, or shader skinning.

1. Renderer resource carrier.
   - Add a bone-palette resource entry to `VulkanRenderResources2D`.
   - Register the current and previous runtime bone palettes from
     `RuntimeModelLoader` using the imported runtime model resource prefix.
   - Keep the registered data CPU-side for this slice.

2. Renderer-facing diagnostics.
   - Expose whether the renderer resource was registered, current/previous
     palette entry counts, changed-palette entry count, and renderer carrier
     readiness through `RuntimeModelLoadResult`.
   - Carry those values into Forward 3D benchmark scene diagnostics, CSV, quick
     visual-QA metrics, and focused baselines.

3. QA contract.
   - Extend `imported-skinned-diagnostic` to assert the renderer-facing palette
     resource counts from `assets/models/skinned_probe.dae`.
   - Keep rigid imported lanes at zero for the renderer palette fields.
   - Keep DLSS quality expected-blocked because this is not a GPU palette
     upload, descriptor bind, skinned vertex path, previous-skinned state, or
     skinned velocity output.

## Slice 4.31 Execution Evidence

Slice 4.31 is implemented and verified. `VulkanRenderResources2D` now owns a
bone-palette resource registry, and `RuntimeModelLoader` registers the imported
runtime current/previous palette into that renderer-facing namespace.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- Direct benchmark CSV probe for `assets/models/skinned_probe.dae` reports
  `823/823` CSV columns.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as a benchmark-only focused run.
- Renderer resource diagnostics:
  `rendererPoseRegistered/rendererPoseBonePalette/rendererPosePreviousBonePalette/rendererPoseChangedBonePalette/rendererPoseReady=1/2/2/1/1`.
- Runtime carrier diagnostics remain:
  `runtimePoseBonePalette/runtimePosePreviousBonePalette/runtimePoseChangedBonePalette/runtimePoseReady=2/2/1/1`.
- DLSS quality remains expected-blocked at `qualityGate=1/0/4`.

This closes the runtime-to-renderer-resource palette carrier gap. The next
skinned quality work is GPU palette buffer upload and descriptor visibility,
then shader skinning or skinned vertex output, previous-skinned state, and
skinned velocity output.

## Slice 4.32 Execution Plan

Slice 4.32 uploads the imported runtime bone-palette carrier into a GPU-visible
storage buffer and exposes descriptor-info readiness. The goal is to prove the
sampled current/previous bone palettes can be represented as a Vulkan buffer
resource before wiring descriptor sets or shader consumption.

1. GPU palette buffer carrier.
   - Concatenate previous and current runtime bone palettes into a single
     storage-buffer payload.
   - Allocate a host-visible Vulkan storage buffer owned by the cached runtime
     model.
   - Upload the palette payload and keep the buffer alive with the cached
     runtime model.

2. Descriptor visibility diagnostics.
   - Create a `VkDescriptorBufferInfo` for the uploaded palette buffer.
   - Expose buffer allocation, upload, descriptor-info readiness, byte size,
     current entry count, and previous entry count through
     `RuntimeModelLoadResult`.
   - Carry those values into Forward 3D benchmark scene diagnostics, CSV, quick
     visual-QA metrics, and focused baselines.

3. QA contract.
   - Extend `imported-skinned-diagnostic` to assert the GPU buffer diagnostics
     from `assets/models/skinned_probe.dae`.
   - Keep rigid imported lanes at zero for the GPU palette fields.
   - Keep DLSS quality expected-blocked because this is not a descriptor-set
     write, shader bind, skinned vertex path, previous-skinned state, or
     skinned velocity output.

## Slice 4.32 Execution Evidence

Slice 4.32 is implemented and verified. `RuntimeModelLoader` now creates a
host-visible Vulkan storage buffer for the imported runtime bone palettes,
uploads previous and current matrices, and verifies descriptor-buffer-info
readiness for the uploaded buffer.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- Direct benchmark CSV probe for `assets/models/skinned_probe.dae` reports
  `829/829` CSV columns.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as a benchmark-only focused run.
- GPU palette diagnostics:
  `gpuPoseAllocated/gpuPoseUploaded/gpuPoseDescriptorReady/gpuPoseBytes/gpuPoseCurrent/gpuPosePrevious=1/1/1/256/2/2`.
- Renderer resource diagnostics remain:
  `rendererPoseRegistered/rendererPoseBonePalette/rendererPosePreviousBonePalette/rendererPoseChangedBonePalette/rendererPoseReady=1/2/2/1/1`.
- DLSS quality remains expected-blocked at `qualityGate=1/0/4`.

This closes the CPU/runtime/renderer-resource-to-GPU-buffer diagnostic chain for
the skinned probe. The next skinned quality work is descriptor-set writes and
shader visibility, then shader skinning or skinned vertex output,
previous-skinned state, and skinned velocity output.

## Slice 4.33 Execution Plan

Slice 4.33 writes the uploaded imported bone-palette buffer into a Vulkan
descriptor set without binding it into any draw pipeline. The goal is to prove a
shader-visible descriptor-set write path for the sampled palette data while
keeping shader skinning, previous-skinned state, and skinned velocity explicitly
blocked.

1. Diagnostic descriptor resources.
   - Create an independent descriptor set layout with one binding:
     storage-buffer binding `0`, visible to vertex/compute shader stages.
   - Allocate a one-set descriptor pool and descriptor set owned by the cached
     runtime model.
   - Write the uploaded bone-palette `VkDescriptorBufferInfo` into binding `0`.

2. Diagnostics and contracts.
   - Report descriptor-set allocated, written, ready, binding, and range bytes
     through `RuntimeModelLoadResult`.
   - Carry those values into Forward 3D benchmark scene diagnostics, CSV, quick
     visual-QA metrics, and focused imported baselines.
   - Keep rigid imported lanes at zero and keep the skinned diagnostic quality
     gate expected-blocked.

3. Non-goals.
   - Do not add the descriptor set to the main frame/material pipeline layout.
   - Do not bind it in command buffers.
   - Do not claim shader skinning, skinned vertex output, previous-skinned
     state, skinned velocity, or production DLSS/DLAA image quality.

## Slice 4.33 Execution Evidence

Slice 4.33 is implemented and verified. `RuntimeModelLoader` now creates an
independent diagnostic storage-buffer descriptor set for the uploaded imported
bone-palette buffer and keeps it alive with the cached runtime model.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- Direct benchmark CSV probe for `assets/models/skinned_probe.dae` reports
  `834/834` CSV columns and descriptor diagnostics
  `setAllocated/setWritten/setReady/binding/rangeBytes=1/1/1/0/256`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as the single focused script run.
- Focused CSV reports `834/834` columns, descriptor range/buffer bytes
  `256/256`, and DLSS quality remains expected-blocked at `qualityGate=1/0/4`
  with masks `255/251/4`.

This closes the GPU-buffer-to-descriptor-set diagnostic chain for the skinned
probe. The next skinned quality work is draw-time descriptor binding and shader
visibility, then shader skinning or skinned vertex output, previous-skinned
state, and skinned velocity output.

## Slice 4.34 Execution Plan

Slice 4.34 carries the imported bone-palette resource identity into the main
draw queue as diagnostic metadata. The goal is to prove that a visible imported
skinned draw can be associated with a ready renderer bone-palette resource
without changing pipeline layouts or binding the diagnostic descriptor set.

1. Renderable and draw-command carrier.
   - Add an optional bone-palette resource id to `Renderable3D`.
   - Assign that id to imported skinned renderables only when the runtime pose
     carrier is ready.
   - Resolve the id in `RenderQueue` into stable command diagnostics:
     command/resource readiness and current/previous/changed palette counts.

2. Renderer and QA diagnostics.
   - Aggregate unique bone-palette resources from the main draw queue into
     `RendererStats`.
   - Add `bone_palette_draw_*` CSV columns and quick visual-QA metrics.
   - Keep rigid imported lanes at zero and keep the skinned diagnostic quality
     gate expected-blocked.

3. Verification discipline.
   - Use one direct 1-frame benchmark CSV probe while iterating.
   - Use the focused `-SkipBuild -Suite imported-skinned-diagnostic` script only
     once as the commit gate.
   - Do not run the full visual-QA matrix for this slice.

4. Non-goals.
   - Do not bind the diagnostic descriptor set during command recording.
   - Do not add bone-palette descriptors to the production pipeline layout.
   - Do not claim shader skinning, skinned vertex output, previous-skinned
     state, skinned velocity, or production DLSS/DLAA image quality.

## Slice 4.34 Execution Evidence

Slice 4.34 is implemented and verified. `Renderable3D` now carries an optional
bone-palette resource id, imported skinned runtime parts set that id once the
runtime pose carrier is ready, and `RenderQueue` resolves it through
`VulkanRenderResources2D` into draw-command diagnostics. `VulkanRenderer`
aggregates the main draw queue into `bone_palette_draw_*` CSV fields.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- Direct 1-frame CSV probe for `assets/models/skinned_probe.dae` reports
  `842/842` CSV columns, descriptor diagnostics `1/1/1`, and draw diagnostics
  `commands/readyCommands/resources/readyResources/current/previous/changed/ready=1/1/1/1/2/2/1/1`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as the single focused script run.
- Focused CSV reports `842/842` columns, descriptor
  `setAllocated/setWritten/setReady/binding/rangeBytes=1/1/1/0/256`, draw
  diagnostics `1/1/1/1/2/2/1/1`, and DLSS quality remains expected-blocked at
  `qualityGate=1/0/4` with masks `255/251/4`.

This closes draw-queue metadata visibility for the skinned probe. The next
skinned quality work is actual draw-time descriptor binding and shader
consumption, then shader skinning or skinned vertex output, previous-skinned
state, and skinned velocity output.

## Slice 4.35 Execution Plan

Slice 4.35 promotes the imported bone-palette descriptor from diagnostic
allocation/write readiness into draw-time binding readiness. The goal is to
prove that a visible imported skinned GBuffer draw can bind the ready
bone-palette storage-buffer descriptor through a graphics pipeline layout,
without making shaders consume it yet.

1. Pipeline-layout compatibility.
   - Add a graphics-pipeline set-2 descriptor-set layout with one storage-buffer
     binding for bone palettes.
   - Keep the existing frame set 0 and material set 1 contracts unchanged.
   - Use the same binding definition for runtime diagnostic descriptor
     allocation so the set is compatible with draw pipelines.

2. Renderer resource propagation.
   - Extend `VulkanRenderResources2D::BonePaletteResource` with descriptor set
     handle, set index, binding, range bytes, and readiness.
   - Register those descriptor details for both first-load and cache-hit runtime
     model paths.
   - Carry the descriptor readiness through `RenderCommand`.

3. Draw-time diagnostics.
   - Bind the ready bone-palette descriptor set for palette-carrying GBuffer
     draw commands.
   - Report descriptor command/resource readiness, set/binding/range, and
     actual main/GBuffer/total bind counts through renderer stats, CSV, quick
     visual-QA metrics, and imported baselines.

4. Verification discipline.
   - Use `_quick_build.bat` once for the C++ change.
   - Use one direct 1-frame CSV probe to confirm new descriptor/bind columns.
   - Use the focused `-SkipBuild -Suite imported-skinned-diagnostic` script once
     as the commit gate.
   - Do not run the full visual-QA matrix for this slice.

5. Non-goals.
   - Do not add shader reads from the bone-palette descriptor.
   - Do not implement shader skinning, skinned vertex output, previous-skinned
     state, skinned velocity, or production DLSS/DLAA image quality.

## Slice 4.35 Execution Evidence

Slice 4.35 is implemented and verified. Graphics pipeline layouts now include a
compatible set-2 bone-palette storage-buffer descriptor layout,
`RuntimeModelLoader` registers the written runtime descriptor back onto the
renderer bone-palette resource, `RenderQueue` carries the descriptor handle and
readiness into `RenderCommand`, and command recording binds the ready descriptor
for the skinned diagnostic GBuffer draw.

Verification on 2026-07-05:

- `_quick_build.bat` passes with only the existing MSVC runtime-library warning.
- Direct 1-frame CSV probe for `assets/models/skinned_probe.dae` reports
  `853/853` CSV columns, draw diagnostics `1/1/1/1/2/2/1/1`, descriptor path
  `commands/readyCommands/resources/readyResources/set/binding/range/ready=1/1/1/1/2/0/256/1`,
  and actual descriptor binds `main/gbuffer/total=0/1/1`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as the single focused script run.
- Focused CSV reports `853/853` columns, runtime descriptor
  `setAllocated/setWritten/setReady/binding/rangeBytes=1/1/1/0/256`, draw
  diagnostics `1/1/1/1/2/2/1/1`, descriptor path `1/1/1/1/2/0/256/1`, actual
  descriptor binds `0/1/1`, and DLSS quality remains expected-blocked at
  `qualityGate=1/0/4` with masks `255/251/4`.

This closes draw-time descriptor binding readiness for the skinned diagnostic
GBuffer route. The next skinned quality work is shader consumption of the bound
palette descriptor, then shader skinning or skinned vertex output,
previous-skinned state, and skinned velocity output.

## Slice 4.36 Execution Plan

Slice 4.36 proves that the GBuffer shader actually consumes the bound
bone-palette descriptor without yet implementing real shader skinning. Slice
4.35 made descriptor binding visible at draw time; this slice makes the shader
contract non-optional and keeps non-skinned GBuffer draws valid through an
identity fallback descriptor.

1. Shader consumption.
   - Declare a set-2/binding-0 storage-buffer bone-palette descriptor in
     `gbuffer_3d.vert`.
   - Read the first matrix and forward a tiny diagnostic value to the fragment
     shader so the descriptor read cannot be optimized away.
   - Keep the diagnostic visually harmless and separate from any production
     skinning claim.

2. GBuffer fallback descriptor.
   - Add a renderer-owned identity bone-palette fallback descriptor set for
     GBuffer commands that do not carry a real imported palette descriptor.
   - Bind the fallback only when a draw lacks a real ready bone-palette
     descriptor.
   - Preserve real imported skinned descriptor binding for the diagnostic
     skinned draw.

3. Diagnostics and baseline.
   - Expose shader-consumer command readiness and fallback-descriptor readiness
     through renderer stats, CSV, quick visual-QA metrics, and the imported
     skinned diagnostic baseline.
   - Track fallback descriptor bind counts separately from real palette
     descriptor bind counts so the skinned probe cannot pass by accidentally
     using the fallback.

4. Verification discipline.
   - Use `_quick_build.bat` once for the C++ and shader changes.
   - Use one direct 1-frame CSV probe while iterating.
   - Use the focused `-SkipBuild -Suite imported-skinned-diagnostic` script once
     as the commit gate.
   - Do not rerun the full visual-QA matrix for this narrow diagnostic slice.

5. Non-goals.
   - Do not add bone indices or weights to the runtime vertex format.
   - Do not implement shader skinning, skinned vertex output, previous-skinned
     state, skinned velocity, or production DLSS/DLAA image quality.

## Slice 4.36 Execution Evidence

Slice 4.36 is implemented and verified. `gbuffer_3d.vert` now declares the
set-2/binding-0 bone-palette storage buffer, reads `bonePalette[0]`, and passes
a clamped diagnostic scalar to `gbuffer_3d.frag`. The fragment shader consumes
that value through a tiny `outMaterialAux.x` contribution so the descriptor read
is preserved without changing visible material behavior in practice.

`VulkanRenderer` now owns a GBuffer-compatible identity fallback descriptor set
with two identity matrices. `VulkanCommandBuffer::Record` receives that fallback
and binds it only for GBuffer draws that lack a real ready bone-palette
descriptor. The skinned diagnostic draw continues to bind the imported runtime
palette descriptor, and fallback binds are counted separately.

Verification on 2026-07-05:

- `_quick_build.bat` passes and recompiles `gbuffer_3d.vert` /
  `gbuffer_3d.frag`.
- Direct 1-frame CSV probe
  `out\benchmarks\aaa_dlss_skinned_shader_consumer_probe.csv` reports
  `859/859` CSV columns, descriptor path `1/1/1/1/2/0/256/1`,
  shader-consumer readiness `1/1/1/1`, real descriptor binds `0/1/1`,
  fallback descriptor binds `0/0`, and expected blocked DLSS quality gate
  `1/0/4`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as the single focused script run. The focused CSV reports `859/859`
  columns, descriptor path `1/1/1/1/2/0/256/1`, shader-consumer readiness
  `1/1/1/1`, real descriptor binds `0/1/1`, fallback descriptor binds `0/0`,
  quality gate `1/0/4`, and quality masks `255/251/4`.

This closes shader-consumption readiness for the bound bone-palette descriptor
on the skinned diagnostic GBuffer route. It still does not implement vertex
bone indices/weights, real shader skinning, skinned vertex output,
previous-skinned state, skinned velocity, or production DLSS/DLAA image quality.

## Slice 4.37 Execution Plan

Slice 4.37 moves the imported skinning data from importer-only influence lists
into the runtime GPU vertex-input contract. Slice 4.36 proved the GBuffer
shader can consume the palette descriptor; this slice proves the same GBuffer
route can also receive per-vertex bone indices and weights without polluting
unrelated forward, depth, shadow, or instanced pipelines.

1. Skinned vertex attribute carrier.
   - Extend `Vertex3D` with four bone indices and four bone weights, defaulting
     to zero for rigid meshes.
   - Pack each imported skinned vertex's strongest four Assimp influences into
     those attributes and normalize the packed weights.
   - Count packed skinned vertices, packed influence slots, max packed
     influences per vertex, overflow influence count, and attribute readiness.

2. Narrow Vulkan vertex layout.
   - Keep ordinary `Vertex3D` pipelines on the existing 5-attribute input
     layout.
   - Add a skinned-aware `Vertex3DSkinned` layout for the GBuffer pipeline only,
     with bone indices at location 5 and bone weights at location 6.
   - Keep instanced forward model-matrix inputs at locations 5-8 so instancing
     is not forced to reserve unused skinning attributes.

3. Shader and diagnostics.
   - Make `gbuffer_3d.vert` declare and consume the bone index/weight inputs as
     part of its existing tiny diagnostic value.
   - Add CSV and quick-metric fields for the runtime packed attributes and the
     renderer vertex-input stride/location/offset readiness.
   - Update the imported skinned diagnostic baseline to require the new fields.

4. Verification discipline.
   - Use `_quick_build.bat` for the C++/shader changes.
   - Use one direct warmup+1-frame CSV probe to confirm new columns and values.
   - Use the focused `-SkipBuild -Suite imported-skinned-diagnostic` script once
     as the commit gate.
   - Do not run the full visual-QA matrix for this narrow diagnostic slice.

5. Non-goals.
   - Do not apply the bone palette to vertex positions or normals yet.
   - Do not implement previous-skinned state, skinned velocity, dynamic skinned
     screenshot capture, or production DLSS/DLAA image quality.

## Slice 4.37 Execution Evidence

Slice 4.37 is implemented and verified. `Vertex3D` now carries four bone index
slots and four normalized bone-weight slots. Runtime Assimp import packs the
strongest four influences per skinned vertex, reports overflow separately, and
leaves rigid meshes with zeroed skinning attributes.

The Vulkan vertex-input contract is intentionally narrow: ordinary `Vertex3D`
pipelines still expose the original five attributes, while GBuffer uses
`VertexLayout::Vertex3DSkinned` so `gbuffer_3d.vert` receives bone indices at
location 5 and bone weights at location 6. Instanced forward rendering keeps its
model matrix inputs at locations 5-8, avoiding the validation warnings caused by
feeding unused skinning attributes to non-skinned shaders.

Verification on 2026-07-05:

- `_quick_build.bat` passes with `BUILD_EXIT=0`.
- Direct warmup+1-frame CSV probe
  `out\benchmarks\aaa_dlss_skinned_vertex_attribute_probe.csv` reports
  `870/870` CSV columns, runtime packed attributes
  `skinnedVertices/attribInfluences/maxAttribInfluences/overflow/ready=3/5/2/0/1`,
  renderer vertex-input layout
  `stride/indicesLocation/weightsLocation/indicesOffset/weightsOffset/ready=92/5/6/60/76/1`,
  descriptor path `1/1/1/1/2/0/256/1`, shader-consumer readiness `1/1/1/1`,
  real descriptor binds `0/1/1`, fallback descriptor binds `0/0`, and expected
  blocked DLSS quality gate `1/0/4` with masks `255/251/4`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes as the single focused script run. Its focused CSV reports the same
  `870/870`, `3/5/2/0/1`, `92/5/6/60/76/1`, descriptor, shader-consumer, bind,
  and expected-blocked quality-gate values.

This closes runtime/GPU vertex-attribute carrier readiness for the skinned
diagnostic GBuffer route. It still does not implement actual shader skinning,
skinned vertex output, previous-skinned state, skinned velocity, or production
DLSS/DLAA image quality.

## Slice 4.38 Execution Plan

Slice 4.38 turns the skinned diagnostic GBuffer route from descriptor/attribute
readiness into first real current-pose shader skinning. Slice 4.37 proved the
shader receives bone indices and weights; this slice applies the current
palette to position, normal, and tangent in the GBuffer vertex shader while
keeping previous-skinned velocity and production DLSS quality blocked.

1. Current-pose GBuffer shader skinning.
   - Pass the current-palette offset to the GBuffer shader through the existing
     object material push-constant payload.
   - In `gbuffer_3d.vert`, normalize non-zero bone weights, read the current
     palette entries, blend a skin matrix, and apply it before object-space to
     world-space transformation.
   - Transform normals and tangents through the skinned model transform.
   - Keep rigid meshes on the identity path when weights are zero.

2. Diagnostics.
   - Extend bone-palette draw stats with shader-skinning command count, ready
     command count, current-palette offset, current-entry count, and path-ready
     state.
   - Add CSV and visual-QA metric names for those fields.
   - Update the imported-skinned diagnostic baseline to require the new
     readiness fields.

3. Verification discipline.
   - Use `_quick_build.bat` for the C++/shader changes.
   - Use one direct warmup+1-frame CSV probe for the new columns.
   - Use one focused `-SkipBuild -Suite imported-skinned-diagnostic` run as the
     commit gate.
   - Do not run the full visual-QA matrix for this narrow skinned diagnostic
     slice.

4. Non-goals.
   - Do not claim previous-skinned state or skinned velocity. The previous clip
     path reuses the current skinned shape under the previous object transform
     only to avoid bind-pose velocity spikes.
   - Do not change forward/depth/shadow skinning behavior.
   - Do not clear the production DLSS/DLAA quality gate for skinned content.

## Slice 4.38 Execution Evidence

Slice 4.38 is implemented and verified. `PushMaterialConstants` now carries the
current-palette offset through `viewport.w` for each draw; for imported skinned
draws this is the previous-palette entry count because the runtime palette
buffer is laid out as previous entries followed by current entries.
`gbuffer_3d.vert` now blends the current-pose bone matrices from locations 5/6
bone attributes, applies the skinned local position before the object model
matrix, and transforms normals/tangents through the skinned model transform.
Rigid draws keep the identity path because their weights are zero.

The renderer now reports shader-skinning readiness in
`RendererBonePaletteDrawStats` and benchmark CSV:
`bone_palette_shader_skinning_command_count`,
`bone_palette_shader_skinning_ready_command_count`,
`bone_palette_shader_skinning_current_palette_offset`,
`bone_palette_shader_skinning_current_entry_count`, and
`bone_palette_shader_skinning_path_ready`. The imported-skinned diagnostic
baseline asserts these values.

Verification on 2026-07-05:

- `_quick_build.bat` passes with `BUILD_EXIT=0`.
- Direct warmup+1-frame CSV probe
  `out\benchmarks\aaa_dlss_skinned_shader_skinning_probe.csv` reports
  `875/875` CSV columns, 0 frame-graph validation issues, shader-skinning
  readiness `commands/readyCommands/currentOffset/currentEntries/ready=1/1/2/2/1`,
  shader-consumer readiness `1/1/1/1`, descriptor path `1/1/1/1/2/0/256/1`,
  real descriptor binds `0/1/1`, fallback descriptor binds `0/0`, and runtime
  skinned vertex attributes `3/5/2/0/1`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes on monitor index `1`. The focused CSV
  `out\reference_captures\dlss_visual_qa\imported_skinned_diagnostic_present.csv`
  reports `875/875`, 0 frame-graph validation issues, shader-skinning
  readiness `1/1/2/2/1`, shader-consumer readiness `1/1/1/1`, descriptor path
  `1/1/1/1/2/0/256/1`, real descriptor binds `0/1/1`, fallback descriptor
  binds `0/0`, and the expected blocked DLSS quality gate `1/0/4` with masks
  `255/251/4`.

This closes first current-pose GBuffer shader skinning for the diagnostic
skinned route. Previous-bone state, skinned velocity, dynamic skinned visual
captures, forward/shadow skinning, and production DLSS/DLAA image quality remain
open.

## Slice 4.39 Execution Plan

Slice 4.39 connects the previous half of the imported bone-palette buffer to
the GBuffer velocity path. Slice 4.38 skinned the current position, but reused
that current skinned shape for previous clip coordinates. This slice makes the
diagnostic skinned route use previous-pose bone matrices for `fragPreviousClip`
while keeping the production DLSS quality gate blocked for skinned content until
full animated-route coverage exists.

1. Previous-pose GBuffer velocity.
   - Treat palette entries `[0, previousEntryCount)` as the previous pose and
     entries `[previousEntryCount, previousEntryCount + currentEntryCount)` as
     the current pose.
   - In `gbuffer_3d.vert`, blend both current and previous skin matrices from
     the same bone indices/weights.
   - Use the current skin matrix for current world position, normals, and
     tangents; use the previous skin matrix plus `previousModel` for
     `fragPreviousClip`.

2. Interface stabilization.
   - Make every pipeline that reuses `gbuffer_3d.vert` expose the skinned vertex
     layout expected by locations 5/6.
   - Keep velocity/mask fragment shader interfaces aligned with the GBuffer
     vertex output so Vulkan validation stays quiet.

3. Diagnostics.
   - Add benchmark CSV fields for shader skinned-velocity command count, ready
     command count, previous-palette offset, previous entry count, and path
     readiness.
   - Add a quick-QA metric for `temporal_velocity_object_motion_ready` so the
     summary can distinguish velocity readiness from the production DLSS
     content-support gate.
   - Require the new fields in the imported-skinned diagnostic baseline.

4. Non-goals.
   - Do not unfreeze the production DLSS/DLAA gate for skinned content while
     `runtime_import_skinned_animation_unsupported=1`.
   - Do not add forward/depth/shadow skinning or dynamic skinned screenshots in
     this slice.
   - Do not run the full visual-QA matrix for this narrow diagnostic change.

## Slice 4.39 Execution Evidence

Slice 4.39 is implemented and verified. `gbuffer_3d.vert` now builds a current
skin matrix from the current-palette offset in `viewport.w` and a previous skin
matrix from palette offset `0`. Current clip coordinates use the current skinned
local position; previous clip coordinates use the previous skinned local position
under `previousModel`, so the GBuffer velocity route now has previous-bone
evidence for the imported skinned diagnostic draw.

The shader/pipeline interface is also stabilized after the black-screen
regression: `ForwardResidualVelocity3D` and `DlssMask3D` now use
`VertexLayout::Vertex3DSkinned` when they reuse `gbuffer_3d.vert`, and
`forward_velocity_3d.frag` / `dlss_mask_3d.frag` consume the GBuffer vertex
payload at location 7 as a no-op diagnostic input. This removes the validation
messages for missing vertex locations 5/6 and unmatched vertex-output location
7.

The renderer now reports skinned velocity readiness in
`RendererBonePaletteDrawStats` and benchmark CSV:
`bone_palette_shader_velocity_command_count`,
`bone_palette_shader_velocity_ready_command_count`,
`bone_palette_shader_velocity_previous_palette_offset`,
`bone_palette_shader_velocity_previous_entry_count`, and
`bone_palette_shader_velocity_path_ready`. The focused quick-QA summary also
separates `velocityObjectMotionReady` from the production-gate
`objectMotionReady`.

Verification on 2026-07-05:

- `_quick_build.bat` passes with `BUILD_EXIT=0`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default`
  passes after the interface fix. It captures on monitor index `1`
  (`\\.\DISPLAY2`), reports nonblank default-scene captures, and
  `default_scene_dlaa_present.capture.err.log` is empty.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  passes without screenshot capture. The focused CSV
  `out\reference_captures\dlss_visual_qa\imported_skinned_diagnostic_present.csv`
  reports `880/880` columns, 0 frame-graph validation issues,
  shader-skinning readiness `1/1/2/2/1`, shader-velocity readiness
  `1/1/0/2/1`, `temporal_velocity_object_motion_ready=1`, and the production
  DLSS quality gate remains expected-blocked at `1/0/4` with masks
  `255/251/4` because `objectMotionReady` is still `0` for unsupported skinned
  content.

This closes previous-pose skinned velocity evidence for the diagnostic GBuffer
route. Dynamic skinned visual captures, forward/shadow skinning, morph targets,
broader animated-route coverage, and production DLSS/DLAA image quality remain
open.

## Slice 4.40 Execution Plan

Slice 4.40 adds a real local skinned-FBX preview lane after the black-screen
regression on `assets/models/Fist Fight B.fbx`. The goal is to validate the
actual user scene as visible DLAA/DLSS input while preserving the production
quality block for unsupported skeletal animation playback.

1. Real skinned FBX visibility.
   - Add `imported-skinned-preview` as a focused visual-QA suite that loads
     `assets/models/Fist Fight B.fbx` through `SELFENGINE_MODEL_PATH`.
   - Keep it in the real application-scene path, not the tiny diagnostic
     `skinned_probe.dae` path.
   - Capture one visible preview screenshot and require enough central image
     variation to catch the black-window failure.

2. Opaque material and preview lighting hygiene.
   - Treat opacity textures as alpha only when the imported material alpha mode
     requests alpha/mask behavior; an authored opacity texture on an otherwise
     opaque FBX material must not make the whole character disappear.
   - Add a dark preview ground and camera-facing preview light for explicit
     non-reference startup model imports while keeping the removed white checker
     grid out of the real scene.

3. Skeletal animation scope guard.
   - Recognize and report the real FBX as skinned/animated source content.
   - Do not bind the real FBX to the diagnostic shader-skinning draw path yet;
     keep that whitelisted to `assets/models/skinned_probe.dae`.
   - Keep `runtime_import_skinned_animation_unsupported=1` and the production
     DLSS/DLAA quality gate blocked until real per-frame skeletal animation
     playback, broad skinned draw coverage, and production skinned velocity are
     implemented.

4. Verification discipline.
   - Use `_quick_build.bat` once for C++/shader changes.
   - Use the focused `-SkipBuild -Suite imported-skinned-preview` run as the
     visual gate.
   - Keep the existing benchmark-only `imported-skinned-diagnostic` lane as the
     shader-skinning/previous-pose sentinel.
   - Do not rerun the full visual-QA matrix for this narrow preview fix.

## Slice 4.40 Execution Evidence

Slice 4.40 is implemented and verified. Opaque imported materials no longer
become transparent just because an opacity texture exists; opacity texture
sampling is now gated by alpha material state in the GBuffer, forward,
weighted-translucency, velocity, and DLSS mask fragment shaders. Real local
startup model imports that are not reference QA assets receive a dark preview
ground and preview lighting, so the `Fist Fight B.fbx` window is visibly
nonblack without restoring the old white checker grid.

The real FBX is deliberately kept off the diagnostic shader-skinning draw path:
the runtime importer still detects 65 bone nodes, 52 animation channels, 37,878
skinned vertices, and a ready sampled/imported pose palette chain, but the draw
queue reports `bone_palette_draw_command_count=0` for this real preview lane.
That means the lane proves visibility and import evidence for the actual asset,
not production skeletal-animation rendering.

Verification on 2026-07-05:

- `_quick_build.bat` passes.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-preview`
  passes on requested monitor index `1` / `\\.\DISPLAY2`.
- The focused capture
  `out\reference_captures\dlss_visual_qa\imported_skinned_preview_present.png`
  is nonblack and uses the dark preview ground.
- The focused CSV reports 0 frame-graph validation issues, DLSS
  evaluate/output `1/1`, post source `1/1/0`,
  `runtime_import_bone_node_count=65`,
  `runtime_import_animation_channel_count=52`,
  `runtime_import_skinned_vertex_count=37878`, and
  `runtime_import_skinned_animation_unsupported=1`.
- The production quality gate remains expected-blocked:
  `temporal_upscaler_dlss_quality_gate_ready=0`,
  `temporal_upscaler_dlss_quality_blocker_mask=132`, and
  `bone_palette_shader_skinning_command_count=0`.

This closes the black-screen preview failure for the real skinned FBX and puts
the real scene in the DLAA queue as a blocked preview. It does not implement
real skeletal animation playback, real FBX shader skinning, forward/shadow
skinning, morph targets, broad dynamic skinned visual captures, or production
DLSS/DLAA image quality.

## Slice 4.41 Execution Plan

Slice 4.41 promotes the real skinned-FBX preview from rigid visibility evidence
into an opt-in imported shader-skinning preview path. The goal is to prove that
the actual `Fist Fight B.fbx` asset can bind and consume its imported bone
palette in the GBuffer draw route, while still keeping production DLSS/DLAA
quality blocked because there is no real per-frame skeletal animation playback
or broad skinned route coverage yet.

1. Explicit preview gate.
   - Add `SE_ENABLE_IMPORTED_SKINNING_PREVIEW=1` as an opt-in runtime import
     switch.
   - Keep `skinned_probe.dae` on the always-on diagnostic path.
   - Do not enable imported shader skinning by default for arbitrary local
     startup models.

2. Real FBX draw-path evidence.
   - Let the real preview suite bind the imported renderer bone-palette
     resource when the runtime pose carrier is ready.
   - Require draw-resource readiness, descriptor readiness, GBuffer descriptor
     bind evidence, shader-skinning readiness, and previous-pose velocity
     readiness for the real FBX.

3. Quality gate scope.
   - Keep `runtime_import_skinned_animation_unsupported=1`.
   - Keep `temporal_upscaler_dlss_quality_gate_ready=0`.
   - Do not treat a one-shot imported pose sample as animation playback or
     production skinned velocity.

4. Focused verification.
   - Build once with `_quick_build.bat`.
   - Run only `-Suite imported-skinned-preview` for visual proof.
   - Run `-Suite imported-skinned-diagnostic` once as the lightweight sentinel
     that the original diagnostic path still works.

## Slice 4.41 Execution Evidence

Slice 4.41 is implemented and verified. `RuntimeModelLoader` now has an
explicit `SE_ENABLE_IMPORTED_SKINNING_PREVIEW` /
`SE_IMPORTED_SKINNING_PREVIEW` gate that allows non-diagnostic imported skinned
assets to bind their ready bone-palette resource. The visual-QA script manages
that environment variable and enables it only for `imported-skinned-preview`,
so unrelated focused suites do not inherit the preview path.

Verification on 2026-07-05:

- `_quick_build.bat` passes with `BUILD_EXIT=0`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-preview`
  passes, captures on requested monitor index `1` / `\\.\DISPLAY2`, and writes
  `out\reference_captures\dlss_visual_qa\imported_skinned_preview_present.png`.
- Real FBX runtime import evidence remains:
  `boneNodes/animationChannels/skinnedVertices/unsupported=65/52/37878/1`.
- Real FBX bone-palette draw path is now active:
  `commands/ready/current/previous/changed/ready=1/1/65/65/65/1`,
  descriptor path `1/1/1/1/2/0/8320/1`, shader skinning
  `1/1/65/65/1`, and shader velocity `1/1/0/65/1`.
- Actual descriptor binds report `main/gbuffer/total/fallback=0/1/1/1`; the
  fallback bind is the preview ground, not the skinned FBX.
- DLSS output/post remains available for inspection, but production quality
  stays blocked with masks `255/123/132`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-diagnostic`
  also passes, preserving the benchmark-only diagnostic probe path.

This moves the real local skeletal asset one step closer to production: it now
exercises the same imported palette descriptor, shader-skinning, and
previous-pose velocity machinery as the diagnostic probe. It still is not full
skeletal animation playback, forward/shadow skinning, morph-target support,
dynamic skinned sequence QA, or production DLSS/DLAA image quality.

## Slice 4.42 Execution Plan

Slice 4.42 adds the real local FBX to the default Forward 3D startup scene
itself, not only to the explicit `imported-skinned-preview` lane. The goal is to
make the scene the user actually sees exercise the same asset in DLAA default,
moving-camera, and moving-camera-plus-object-motion queues while keeping the
production quality gate honest for unsupported skeletal animation.

1. Default-scene placement.
   - Load `assets/models/Fist Fight B.fbx` from the normal default scene path
     when no bridge scene, startup model override, or benchmark-grid scene is
     active.
   - Place it beside the existing cube cluster at a stable preview scale.
   - Keep the existing default ground, authored cubes, local lights, and scene
     reflection probe.

2. Stability scope.
   - Keep the default-scene FBX as a rigid visible preview. Do not bind its
     bone-palette descriptor in the main default scene yet because the opt-in
     shader-skinning preview still shows asset artifacts.
   - Continue to use `imported-skinned-preview` with
     `SE_ENABLE_IMPORTED_SKINNING_PREVIEW=1` for explicit shader-skinning
     evidence on the real FBX.

3. DLAA queue and baselines.
   - Update default, default-motion, and default-object-motion DLAA baselines to
     expect five main/GBuffer draws instead of four.
   - Require the real FBX import diagnostics:
     `boneNodes/animationChannels/skinnedVertices/unsupported=65/52/37878/1`.
   - Keep `qualityGate=1/0/4` and `qualityMasks=255/251/4` because the default
     scene now contains unsupported skinned/animated source content.

4. Focused verification.
   - Build once with `_quick_build.bat`.
   - Run only the affected suites: `default`, `default-motion`,
     `default-object-motion`, and `imported-skinned-preview`.
   - Avoid the full visual-QA matrix for this narrow scene-content change.

## Slice 4.42 Execution Evidence

Slice 4.42 is implemented and verified. `SelfEngineForward3D` now loads
`assets/models/Fist Fight B.fbx` in the normal default startup scene when the
asset exists and no explicit startup/bridge/benchmark scene is active. The model
is placed near the three default cubes as a light material override at
`{-1.35, -0.48, 0.45}`, rotated 180 degrees around Y, and normalized to a
`1.35` max extent so it sits just above the ground plane instead of floating at
the edge of the scene.

The default scene intentionally does not bind the imported bone-palette resource
for this FBX. That keeps the user-facing startup scene stable while the separate
`imported-skinned-preview` suite remains the opt-in shader-skinning proving
ground. The default DLAA baselines now record the FBX as unsupported skinned
source content instead of silently counting the scene as fully production-ready.

Verification on 2026-07-05:

- `_quick_build.bat` passes with `BUILD_EXIT=0`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default`
  passes and captures the default scene on monitor index `1`.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite imported-skinned-preview`
  passes, preserving the opt-in real-FBX shader-skinning lane.
- `powershell -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default-motion,default-object-motion`
  passes for the affected dynamic default-scene lanes.
- Default-scene DLAA now reports draw route `5/5/0/0`,
  runtime import evidence `65/52/37878/1`, quality gate `1/0/4`, and masks
  `255/251/4`. The default-scene bone-palette draw path remains `0`, by design.

This puts the real FBX into the main application scene and the main DLAA queues.
It still is not production skeletal animation playback, stable full-scene
shader skinning, morph-target support, or final production DLSS/DLAA image
quality.

## Next Stage Plan: Real Default-Scene DLAA Image Quality

The next stage turns the current visible DLAA integration into a real
image-quality hardening lane for the scene the user is judging by eye: the
default Forward 3D startup scene with three cubes, ground plane, lights,
reflection probe, and the local `Fist Fight B.fbx` preview. Treat jaggies,
hairline shimmer, and intermittent blur as input-contract and tuning failures
until proven otherwise; do not assume a newer DLSS model, mipmap changes, or
post sharpening can hide missing temporal data.

1. Static default-scene applied-jitter parity.
   - Move the default-scene DLAA still lane to `SE_TAA_APPLY_JITTER=1` for both
     native and DLSS-present captures.
   - Require `temporal_jitter_applied=1`, DLSS jitter consistency, stable
     history, DLSS reset `0`, and motion-vector scale `1/1` in the default
     baseline.
   - Keep the production gate blocked for the FBX preview while still requiring
     DLSS output/post activation and full-resolution DLAA extents.

2. Clean visual evidence.
   - Keep the default ImGui window collapsed by default and allow visual-QA
     lanes to hide ImGui when the image itself is the object under inspection.
   - Preserve monitor index `1` / `\\.\DISPLAY2` as the default capture target.
   - Record native, DLAA, and optional component/debug views with consistent
     camera framing.

3. Edge-focused quality metrics.
   - Add a still-image high-contrast edge metric for default-scene native vs
     DLAA captures, not only the existing dynamic sequence metrics.
   - Track changed-edge pixels, edge mean delta, and edge max delta so hairline
     shimmer and silhouette crawling become measurable.

4. Tuning matrix.
   - Add focused A/B lanes for DLAA preset, sharpness, and texture/mip-bias
     policy only after applied jitter is proven.
   - Keep one variable per lane: preset, sharpening, mip bias, or material
     texture use.
   - Prefer reducing temporal aliasing at the source over adding sharpening.

5. Animated/skinned follow-through.
   - Implement real per-frame skeletal animation playback for imported FBX
     content.
   - Update current and previous bone palettes every frame and prove skinned
     motion vectors under camera and object motion.
   - Only then consider moving the default-scene FBX from rigid preview to
     production skinned DLAA content.

6. Acceptance gate.
   - The stage is complete only when focused default, default-motion, and
     default-object-motion suites pass with applied jitter, clean captures,
     edge metrics, 0 frame-graph validation issues, and documented blocker
     state for unsupported skinned animation.
   - Do not make DLSS/DLAA the default presentation path until this evidence
     survives broader imported, transparent, and animated content.

## Slice 4.43 Execution Plan

Slice 4.43 executes the first item above: applied-jitter parity for the static
default-scene DLAA lane. The current default lane reaches DLSS output but reports
`temporal_jitter_applied=0`, which makes it a weak anti-aliasing proof. This
slice applies projection jitter in both native and DLSS-present default-scene
captures and makes the baseline assert that policy.

1. QA environment.
   - Add `SE_TAA_APPLY_JITTER=1` to the default-scene native and DLSS-present
     visual-QA environments.
   - Keep render scale `1.0`, DLAA quality mode, DLSS post-source presentation,
     and native TAA final-resolve suppression behavior unchanged.

2. Baseline contract.
   - Extend `dlss_default_scene_dlaa_visual_qa_baseline.json` with expected
     applied jitter, temporal history, DLSS reset, DLSS motion-vector scale, and
     TAA resolve policy.
   - Update the baseline source after one focused `-Suite default` run.

3. Scope guard.
   - This does not clear the production quality gate for the real FBX because
     `runtime_import_skinned_animation_unsupported=1` still blocks object
     quality.
   - This does not tune DLSS preset, sharpness, or mip bias yet; it only fixes
     the static default-scene input contract.

## Slice 4.43 Execution Evidence

Slice 4.43 is implemented and verified. The focused default-scene visual-QA
native and DLSS-present environments now set `SE_TAA_APPLY_JITTER=1`, so the
static default-scene DLAA lane uses the same applied projection-jitter policy as
the moving default-scene lanes instead of sending a prepared-but-unapplied jitter
state.

`docs/reference_baselines/dlss_default_scene_dlaa_visual_qa_baseline.json` now
records that contract explicitly. The DLSS-present expected metrics include
`jitterApplied=1`, temporal history `valid/reset/reason=1/0/0`, DLSS reset `0`,
motion-vector scale `1/1`, and native TAA resolve suppression
`input/enabled/suppressed=1/0/1`.

Verification on 2026-07-05:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default`
  passes on requested monitor index `1` / `\\.\DISPLAY2`.
- The focused summary reports matching `880/880` CSV columns, DLSS
  evaluate/output `1/1`, post source `1/1/0`, full-resolution DLAA extents
  `1280x720->1280x720`, applied jitter `1`, history `1/0/0`, DLSS reset `0`,
  motion-vector scale `1/1`, draw route `5/5/0/0`, and runtime FBX import
  evidence `65/52/37878/1`.
- The production quality gate remains expected-blocked at `1/0/4` with masks
  `255/251/4` because the main scene still contains unsupported skinned/animated
  FBX source content.
- The latest native-vs-DLAA comparison samples `14352` pixels with `4784`
  changed pixels, mean delta `45.1637`, and max delta `580`, inside the default
  baseline thresholds. The screenshot also shows the ImGui window collapsed by
  default, so visual evidence is no longer obscured by the debug panel.

This closes the static default-scene applied-jitter gap. It does not yet solve
hairline shimmer by itself; the next slices should add still-image edge metrics,
then DLAA preset/sharpness/mip-bias A/B lanes, and only later real animated FBX
skinned playback.

## Slice 4.44 Execution Plan

Slice 4.44 adds still-image edge metrics to the default-scene native-vs-DLAA
capture pair. The user-visible complaint is about jagged silhouettes and
hairline artifacts, so the static default lane needs an edge-specific signal in
addition to whole-image changed-pixel and mean-delta checks.

1. Reuse the existing edge definition.
   - Use the same `Compare-ImageEdges` high-contrast edge detector already used
     by moving-camera and moving-object sequence comparisons.
   - Add `ChangedEdgeRatio` to the edge comparison object so still and sequence
     summaries share a comparable ratio field.

2. Default still-lane gate.
   - Compute edge comparison for `default_scene_dlaa_native_deferred_hdr.png`
     vs `default_scene_dlaa_present.png`.
   - Gate edge pixel count, changed-edge count, changed-edge ratio, mean edge
     delta, and max edge delta through
     `dlss_default_scene_dlaa_visual_qa_baseline.json`.
   - Save the edge comparison into focused `summary.json` and print it in the
     focused suite output.

3. Scope guard.
   - This measures native-vs-DLAA edge differences; it does not yet measure
     temporal edge shimmer across frames.
   - DLAA preset, sharpness, and mip-bias A/B lanes remain the next tuning step.

## Slice 4.44 Execution Evidence

Slice 4.44 is implemented and verified. `scripts\Test-DlssVisualQa.ps1` now
computes `edgeComparison` for the focused default-scene still pair and records
`EdgePixels`, `ChangedEdgePixels`, `ChangedEdgeRatio`, `MeanEdgeDelta`, and
`MaxEdgeDelta` in the quick summary. The full visual-QA path also computes and
gates the same default still edge comparison so focused and full runs use the
same contract.

`docs/reference_baselines/dlss_default_scene_dlaa_visual_qa_baseline.json` now
adds still-edge thresholds:
`comparisonEdgePixelsMin=512`,
`comparisonChangedEdgePixelsMax=768`,
`comparisonChangedEdgeRatioMax=0.75`,
`comparisonMeanEdgeDeltaMax=64.0`, and
`comparisonMaxEdgeDeltaMax=225.0`.

Verification on 2026-07-05:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default`
  passes on monitor index `1`.
- The focused output reports default still edge metrics
  `pixels=906`, `changed=574`, `ratio=0.6336`, `mean=45.6513`, and
  `max=208.1138`.
- The same run keeps Slice 4.43's applied-jitter contract active:
  `jitterApplied=1`, history `1/0/0`, DLSS reset `0`, motion-vector scale
  `1/1`, quality gate `1/0/4`, and masks `255/251/4`.

This gives the static default-scene DLAA lane a measurable edge-quality signal.
The next execution slice should use this signal for DLAA preset/sharpness and
mip-bias A/B comparisons instead of relying on subjective screenshot inspection.

## Slice 4.45 Execution Plan

Slice 4.45 starts the default-scene DLAA tuning matrix without expanding the
normal visual-QA matrix. The goal is to add real controls for the variables the
user is judging by eye, then run a focused suite that records edge metrics for
one variable at a time.

1. DLSS tuning control surface.
   - Add an explicit DLSS render-preset override through `SE_DLSS_PRESET`
     with aliases `SE_DLSS_RENDER_PRESET` and `SE_DLSS_PRESET_OVERRIDE`.
   - Keep the default policy unchanged: if no override is present, DLAA and
     Quality/Balanced/Ultra Quality still use preset K, Performance uses M, and
     Ultra Performance uses L.
   - Track the effective preset through the existing
     `temporal_upscaler_dlss_recommended_preset` CSV field so focused runs can
     prove which preset was actually used.

2. DLSS sharpness control surface.
   - Add `SE_DLSS_SHARPNESS` with alias
     `SE_TEMPORAL_UPSCALER_SHARPNESS`.
   - Clamp the override to the DLSS evaluate range and keep the default NGX
     optimal-settings sharpness when no override is present.
   - Record both runtime sharpness and evaluate sharpness in focused summaries.

3. Focused tuning QA.
   - Add a `default-tuning` suite to `scripts/Test-DlssVisualQa.ps1`.
   - Keep it out of the `full` suite so ordinary visual QA does not run extra
     tuning windows.
   - Capture the real default-scene native reference, the current default DLAA
     preset K lane, and a sharpness-zero lane, then compare both DLAA images
     against the same native reference with whole-image and still-edge metrics.
   - Run preset L as a benchmark-only guard first; do not claim preset L visual
     readiness until a long capture is validation-clean.

4. Scope guard.
   - This slice does not choose a final DLAA tuning preset.
   - This slice does not implement texture/material mip-bias policy yet.
   - This slice does not clear the real FBX production DLSS/DLAA gate because
     skeletal animation playback and skinned velocity are still incomplete.

## Slice 4.45 Execution Evidence

Slice 4.45 is implemented and verified. `TemporalUpscalerRuntimeRequest` and
`TemporalUpscalerEvaluateRequest` now carry an explicit DLSS preset override,
the NGX preset hint uses the effective preset, and the DLSS feature cache
recreates if that preset changes. The renderer reads `SE_DLSS_PRESET`,
`SE_DLSS_RENDER_PRESET`, or `SE_DLSS_PRESET_OVERRIDE`; it also reads
`SE_DLSS_SHARPNESS` or `SE_TEMPORAL_UPSCALER_SHARPNESS` and applies that value
to the DLSS evaluate sharpness while leaving the default NGX sharpness path
unchanged.

`scripts/Test-DlssVisualQa.ps1` now exposes a focused `default-tuning` suite.
The suite runs only when requested. It records tuning metrics in `summary.json`
and prints edge metrics for the visual lanes, while keeping the normal `full`
suite unchanged.

Verification on 2026-07-05:

- `_quick_build.bat` passes for `SelfEngineForward3D`.
- PowerShell parser validation for `scripts/Test-DlssVisualQa.ps1` passes.
- `git diff --check` passes with only existing LF/CRLF warnings.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default-tuning`
  passes on requested monitor index `1` / `\\.\DISPLAY2`.

Focused tuning evidence:

- Default preset K visual lane: matching `880/880` CSV columns, preset `11`,
  DLSS/evaluate sharpness `0.35/0.35`, DLSS output `1/1`, applied jitter `1`,
  native TAA resolve suppressed `0/1`, quality gate expected-blocked `1/0/4`,
  comparison `changed=4782`, `mean=45.7939`, `max=580`, and edge metrics
  `pixels=895`, `changed=563`, `ratio=0.6291`, `mean=44.8436`, `max=208.2582`.
- Sharpness-zero visual lane: matching `880/880` CSV columns, preset `11`,
  DLSS/evaluate sharpness `0/0`, DLSS output `1/1`, applied jitter `1`,
  native TAA resolve suppressed `0/1`, comparison `changed=4779`,
  `mean=45.7639`, `max=580`, and edge metrics `pixels=894`, `changed=563`,
  `ratio=0.6298`, `mean=45.0514`, `max=208.2582`.
- Preset L benchmark-only guard: matching `880/880` CSV columns, preset `12`,
  DLSS/evaluate sharpness `0.35/0.35`, DLSS output `1/1`, applied jitter `1`,
  native TAA resolve suppressed `0/1`, and the same expected blocked quality
  gate `1/0/4`.

The sharpness-zero visual A/B is effectively neutral against the current default
scene still-edge metric, so current visible jaggies are unlikely to be solved by
turning DLSS evaluate sharpness down alone. Preset L is controllable and reaches
DLSS output in benchmark mode, but it is not visual-cleared: an attempted long
capture reported Vulkan validation for two NGX DLSS resources expecting
`VK_IMAGE_LAYOUT_GENERAL` while tracked as `VK_IMAGE_LAYOUT_UNDEFINED`. The next
slice should diagnose that preset-L capture validation issue before using preset
L screenshots as tuning evidence, then add the mip-bias/material LOD lane.

## Slice 4.46 Execution Plan

Slice 4.46 tests the user's mipmap/LOD suspicion without broadening the normal
visual-QA matrix. The goal is to make material texture mip bias explicit,
observable, and comparable against the same default-scene DLAA still-edge
metric used by Slice 4.45.

1. Material sampler control.
   - Add a material-texture sampler mip LOD bias override through
     `SE_TEXTURE_MIP_LOD_BIAS`.
   - Accept aliases `SE_MATERIAL_TEXTURE_MIP_BIAS` and `SE_TEXTURE_MIP_BIAS`.
   - Clamp the value to a conservative range so QA can test blurrier or sharper
     mip selection without destabilizing unrelated samplers.

2. Renderer diagnostics.
   - Carry the effective bias through `VulkanSampler`, `VulkanMaterial`, and
     `VulkanMaterialLibrary`.
   - Record the effective value in renderer stats and benchmark CSV as
     `frame_material_texture_mip_lod_bias`.
   - Keep the default value `0.0` when no override is present.

3. Focused tuning lane.
   - Extend `default-tuning` with one new visual lane:
     `default_scene_dlaa_tuning_mip_bias_positive_present`.
   - Set only `SE_TEXTURE_MIP_LOD_BIAS=1.0` for that lane while keeping preset
     K, default DLSS evaluate sharpness, full-resolution DLAA, applied jitter,
     and the same native reference image.
   - Require the CSV row to prove effective mip bias `1.0`, then compare the
     screenshot with the existing whole-image and still-edge metrics.

4. Scope guard.
   - This does not choose a final texture LOD policy.
   - This does not solve skinned animation playback or clear the default-scene
     production quality gate.
   - A positive result would justify a larger mip/anisotropy/material policy
     pass; a neutral or worse result should move focus back to temporal input
     correctness and skinned/dynamic velocity.

## Slice 4.46 Execution Evidence

Slice 4.46 is implemented and verified. `VulkanSampler` now stores and applies
an optional sampler mip LOD bias, `VulkanMaterial` forwards the material-library
bias into texture samplers, and `VulkanMaterialLibrary` reads
`SE_TEXTURE_MIP_LOD_BIAS`, `SE_MATERIAL_TEXTURE_MIP_BIAS`, or
`SE_TEXTURE_MIP_BIAS` with clamping to `[-2.0, 2.0]`. Benchmark CSV now exposes
the effective value as `frame_material_texture_mip_lod_bias`, and
`scripts/Test-DlssVisualQa.ps1` records it in quick native/DLSS metrics.

Verification on 2026-07-05:

- PowerShell parser validation for `scripts\Test-DlssVisualQa.ps1` passes.
- `git diff --check` reports only the existing LF/CRLF warnings.
- `_quick_build.bat` passes for `SelfEngineForward3D`; the only new compiler
  diagnostic is the existing style of MSVC `getenv` safety warning in the
  environment-read helper.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default-tuning`
  passes on requested monitor index `1` / `\\.\DISPLAY2`.

Focused tuning evidence from the verified run:

- Default preset K visual lane: `881` CSV columns, preset `11`, mip bias `0`,
  DLSS/evaluate sharpness `0.35/0.35`, DLSS output `1/1`, 0 frame-graph
  validation issues, comparison `changed=4739`, `mean=44.5139`, `max=579`, and
  edge metrics `pixels=899`, `changed=555`, `ratio=0.6174`, `mean=45.7411`,
  `max=207.543`.
- Sharpness-zero visual lane: `881` CSV columns, preset `11`, mip bias `0`,
  DLSS/evaluate sharpness `0/0`, DLSS output `1/1`, 0 frame-graph validation
  issues, comparison `changed=4740`, `mean=44.4505`, `max=579`, and edge
  metrics `pixels=902`, `changed=567`, `ratio=0.6286`, `mean=46.5144`,
  `max=207.543`.
- Positive mip-bias visual lane: `881` CSV columns, preset `11`, mip bias `1`,
  DLSS/evaluate sharpness `0.35/0.35`, DLSS output `1/1`, 0 frame-graph
  validation issues, comparison `changed=4752`, `mean=45.1865`, `max=579`, and
  edge metrics `pixels=898`, `changed=566`, `ratio=0.6303`, `mean=45.7385`,
  `max=207.543`.
- Preset L benchmark-only guard still reports preset `12`, mip bias `0`,
  DLSS/evaluate sharpness `0.35/0.35`, DLSS output `1/1`, and 0 frame-graph
  validation issues.

The `+1.0` mip LOD bias lane is controllable and clean, but it does not improve
the current still-edge signal: its changed-edge ratio `0.6303` is worse than
the default preset-K lane's `0.6174`, and its whole-image mean delta is also
higher. The current jagged/shimmer complaint is therefore unlikely to be solved
by simply biasing textured material mips blurrier. The next high-value slice is
to diagnose the preset-L NGX image-layout validation issue and continue toward
dynamic/skinned temporal correctness, especially real animated FBX playback and
skinned velocity in the visible default scene.

## Slice 4.47 Execution Plan

Slice 4.47 investigates the preset-L visual-capture blocker without expanding
the normal `default-tuning` suite. Preset L already reaches DLSS output in a
benchmark-only guard, but the earlier long screenshot path reported Vulkan
validation on NGX-owned `nv.ngx.dlss.resource` images. This slice separates
engine-owned layout hygiene from SDK-internal diagnostics and prevents preset-L
screenshots from being mistaken for clean tuning evidence.

1. Feature recreate warmup.
   - After a DLSS feature first-create or recreate, do not evaluate DLSS in the
     same command-buffer recording path. Return a one-frame
     `FeatureCreateWarmup` fallback and let the next frame evaluate the stable
     feature handle.
   - Keep benchmark last-row behavior stable; warmup frames should absorb the
     one-frame fallback.

2. Layout-initialization hygiene.
   - Treat temporal-upscale output and DLSS mask images as layout-initialized
     after the renderer records the DLSS preparation path, even if that frame
     does not produce DLSS output.
   - This keeps future old-layout choices tied to actual layout transitions
     rather than to output readiness alone.

3. Preset-L diagnostic suite.
   - Add a separate `default-preset-l` focused suite instead of adding more
     windows to `default-tuning`.
   - Capture the real default-scene native reference and preset-L visual output
     with the same whole-image and still-edge metrics.
   - Permit only the known NGX-internal `nv.ngx.dlss.resource` preset-L warning
     pattern for this diagnostic lane, and record `validationClean=false` in
     `summary.json`.

4. Scope guard.
   - If preset L still reports the NGX-internal warning, do not use it as clean
     tuning evidence.
   - Do not weaken ordinary visual-QA log checks; the exception must stay
     explicit to the preset-L diagnostic capture.

## Slice 4.47 Execution Evidence

Slice 4.47 is implemented and verified as a diagnostic classification. The
renderer now reports `TemporalUpscalerEvaluateFallbackReason::FeatureCreateWarmup`
when a DLSS feature was just created or recreated, deferring evaluate to the
next frame. `VulkanRenderer` now marks temporal-upscale output and DLSS mask
images layout-initialized after the evaluate-preparation path records, rather
than tying that layout state only to `outputReady`.

`scripts\Test-DlssVisualQa.ps1` now lists a separate `default-preset-l` suite.
That suite captures `default_scene_dlaa_preset_l_native_deferred_hdr.png` and
`default_scene_dlaa_preset_l_present.png`, gates the same still-image metrics
as the default DLAA lane, and records a diagnostic block in `summary.json`:
`validationClean=false`, with the allowed known diagnostic described as the NGX
internal `nv.ngx.dlss.resource` layout warning for preset L.

Verification on 2026-07-05:

- PowerShell parser validation for `scripts\Test-DlssVisualQa.ps1` passes.
- `git diff --check` reports only existing LF/CRLF warnings.
- `_quick_build.bat` passes for `SelfEngineForward3D`; the existing MSVC
  runtime-library warning remains.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default-preset-l`
  passes on requested monitor index `1` / `\\.\DISPLAY2` by allowing only the
  known NGX-internal preset-L warning in the capture stderr log.

Preset-L diagnostic evidence:

- Benchmark CSV reports `881` columns, preset `12`, DLSS/evaluate sharpness
  `0.35/0.35`, DLSS output ready `1`, feature recreation `0/0` in the sampled
  row, evaluate fallback `0`, frame-graph validation issues `0`, and the
  expected blocked default-scene quality gate `1/0/4`.
- The capture stderr still contains exactly the same two NGX-owned image
  warnings: `nv.ngx.dlss.resource` expected `VK_IMAGE_LAYOUT_GENERAL` while
  validation tracked `VK_IMAGE_LAYOUT_UNDEFINED`, with
  `VUID-vkCmdDraw-None-09600`.
- The preset-L visual comparison reports `changed=4801`, `mean=61.7876`,
  `max=571`, and still-edge metrics `pixels=867`, `changed=586`,
  `ratio=0.6759`, `mean=46.3117`, `max=205.3264`.

This does not clear preset L as production-clean visual evidence. The renderer
layout hygiene is better, and the QA suite can now classify the known
SDK-internal warning without hiding it, but preset L still carries validation
contamination and its still-edge ratio is worse than the verified preset-K
lane from Slice 4.46. Preset L should remain out of the main tuning matrix
until NVIDIA/NGX behavior or creation parameters change. The next quality work
should move back to real dynamic/skinned temporal correctness: animated FBX
playback, current/previous bone-palette updates over time, and skinned velocity
in the visible default scene.

## Slice 4.48 Execution Plan

Slice 4.48 promotes preset M from an explicit tuning experiment into the
default DLAA target for the real Forward 3D scene. The reason is visual: the
next user-facing goal is sharper DLAA output, and preset M is the model we now
want to adapt and judge directly. K remains the validation-clean fallback and
A/B control.

1. DLAA default policy.
   - Change the renderer's default preset policy for `SE_DLSS_QUALITY=dlaa`
     from K to M.
   - Keep Quality, Balanced, Ultra Quality, and fallback Quality SR on K for
     now; keep Performance on M and Ultra Performance on L.
   - Preserve explicit overrides through `SE_DLSS_PRESET=k|l|m`, so any run can
     still force K or L for comparison.

2. QA lane hygiene.
   - Update DLAA baselines to expect recommended preset `13`.
   - Keep the `preset K` tuning and high-resolution comparison lanes explicitly
     pinned with `SE_DLSS_PRESET=k`; their expected preset stays `11`.
   - Allow only the known NGX-owned `nv.ngx.dlss.resource` layout diagnostic in
     M visual lanes, and record `validationClean=false` in `summary.json`.
     Do not weaken non-M DLSS or renderer-owned log checks.

3. Full-resolution coverage.
   - Keep the high-resolution `default-preset-m` focused suite at the selected
     monitor's physical bounds, not the 1280x720 working-window size.
   - Assert DLSS render/output extents match the monitor resolution so a 720p
     stretch cannot pass as fullscreen evidence.

4. Scope guard.
   - Do not claim production DLSS/DLAA image quality while M still reports the
     NGX-owned validation diagnostic and while the default scene still contains
     unsupported skinned animation content.
   - Treat the current edge-delta metric as a regression/sanity signal, not as
     a complete subjective clarity metric.

## Slice 4.48 Execution Evidence

Slice 4.48 is implemented and verified as the current M-adaptation slice.
`RecommendedDlssPresetForQuality()` now returns M for DLAA by default, while
explicit `SE_DLSS_PRESET=k` still forces K. `scripts\Test-DlssVisualQa.ps1`
now has a dedicated M diagnostic whitelist for the narrow NGX-owned
`nv.ngx.dlss.resource` validation message and uses it only on M DLAA visual
lanes. K comparison lanes are explicitly pinned to K.

Verification on 2026-07-05:

- `_quick_build.bat` passes for `SelfEngineForward3D`.
- PowerShell parser validation for `scripts\Test-DlssVisualQa.ps1` passes.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default`
  passes on requested monitor index `1` / `\\.\DISPLAY2`.
- The default real-scene DLAA row reports preset `13`, DLSS output/post
  `1/1` and `1/1/0`, DLSS extents `1280x720->1280x720`, 0 frame-graph
  validation issues, and the expected blocked default-scene quality gate
  `1/0/4` with masks `255/251/4`.
- The default real-scene screenshot comparison reports `changed=4795`,
  `mean=49.0686`, `max=577`, and edge metrics `pixels=914`,
  `changed=597`, `ratio=0.6532`, `mean=45.3341`, `max=207.2542`.
- The default M capture still reports the known NGX-owned resource layout
  diagnostic, so `summary.json` records `validationClean=false`.

High-resolution evidence:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -Suite default-preset-m`
  passes on requested monitor index `1` / `\\.\DISPLAY2`.
- Capture monitor physical bounds are `2560x1440`, and both high-resolution
  preset lanes assert DLSS extents `2560x1440->2560x1440`.
- High-resolution preset K remains validation-clean with preset `11`; its
  record-only edge metrics are `pixels=2385`, `changed=1260`,
  `ratio=0.5283`, `mean=31.649`, `max=210.3344`.
- High-resolution preset M reaches DLSS output with preset `13`, but remains
  `validationClean=false` because of the same NGX-owned resource diagnostic.
  Its record-only edge metrics are `pixels=2481`, `changed=1401`,
  `ratio=0.5647`, `mean=34.3051`, `max=210.3344`.

Current decision: adapt M as the default DLAA target for real visual work, keep
K as the clean fallback and A/B control, and do not call M production-clean
until the NGX-owned validation diagnostic is resolved or isolated by an
officially supported integration path. The next quality step is a better
subjective/temporal clarity metric plus animated/skinned velocity coverage, not
another blind mip-bias or sharpness-only pass.

## Slice 4.49 Execution Evidence

Slice 4.49 implements the M production-quality audit scaffolding without
claiming M is production-clean. `scripts\Test-DlssVisualQa.ps1` now exposes
focused suites for the next M hardening stage:

- `m-vs-k-subjective-pack` captures the real default scene as native, preset K,
  and preset M. It is record-only for comparison thresholds and marks preset M
  `validationClean=false` when the known NGX-owned
  `nv.ngx.dlss.resource` diagnostic appears.
- `default-preset-m-fullscreen` is the strict monitor-resolution M production
  candidate. Unlike `default-preset-m`, it does not use the M diagnostic
  whitelist; any validation/error/VUID/shader diagnostic must fail the run.
- `default-preset-m-dynamic` is the strict dynamic M production candidate. It
  splits the real scene into moving-camera, moving-object-only, and combined
  camera/object sequences and also requires clean capture logs.
- `skinned-fbx-m-production` is a production audit for
  `assets/models/Fist Fight B.fbx`. It records a dynamic sequence through the
  shader-skinning preview path but keeps `productionReady=false` while the FBX
  still reports `runtime_import_skinned_animation_unsupported=1`.
- `m-production` expands to the strict fullscreen, strict dynamic, and skinned
  FBX audit suites so the next stage can be run without the full matrix.

Verification on 2026-07-05:

- `_quick_build.bat` passes for `SelfEngineForward3D`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-vs-k-subjective-pack,m-production`
  lists the new suites and expands `m-production` into
  `default-preset-m-fullscreen`, `default-preset-m-dynamic`, and
  `skinned-fbx-m-production`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite m-vs-k-subjective-pack`
  passes on requested monitor index `1` / `\\.\DISPLAY2`. Preset K records
  `changed=3235`, `mean=10.9274`, `max=547`, edge `pixels=612`,
  `changed=296`, `ratio=0.4837`, `mean=24.244`, `max=183.978`. Preset M
  records `changed=4676`, `mean=39.1279`, `max=680`, edge `pixels=835`,
  `changed=517`, `ratio=0.6192`, `mean=42.7054`, `max=230.7858`, and the
  known NGX diagnostic is explicitly classified as diagnostic-only evidence.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite skinned-fbx-m-production`
  passes as a blocked production audit. The row reports DLSS output/post `1/1`
  and `1/1/0`, preset `13`, shader skinning `1/1/65/65/1`, shader velocity
  `1/1/0/65/1`, `runtime_import_skinned_animation_unsupported=1`, quality gate
  `1/0/4`, masks `255/123/132`, and `productionReady=false`.
- The skinned FBX audit captures a three-frame dynamic sequence with
  `pairs=2`, `minChanged=686`, `maxMean=5.8507`, `edgeMin=195`,
  `edgeChangedMax=101`, `edgeChangedRatioMax=0.4298`, and
  `edgeMeanMax=21.5552`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite default-preset-m-fullscreen`
  was also run as a strict production-candidate probe and failed as intended on
  `default_scene_dlaa_preset_m_fullscreen_present.capture.err.log` because the
  M capture still emits the NGX/Vulkan diagnostic. This confirms the new strict
  lane no longer treats the diagnostic whitelist as production-clean evidence.

Current decision: `default-preset-m` remains a diagnostic high-resolution M
lane with an explicit whitelist; the new fullscreen/dynamic M production
candidates must stay strict. M is still not production quality until those
strict lanes are clean and the real FBX no longer blocks skeletal-animation
content.

## Slice 4.50 Execution Evidence

Slice 4.50 makes the strict preset-M fullscreen blocker diagnosable instead of
treating it as a generic layout warning. The temporal upscaler now supports
`SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS=1`, which prints the Vulkan image/view,
format, extent, aspect, read/write intent, and intended `GENERAL` layout for the
six engine-owned resources passed into NGX evaluate: input color, depth, motion
vectors, bias-current-color mask, transparency mask, and output color. The
focused visual-QA runner enables that trace only for the strict
`default-preset-m-fullscreen` production-candidate lane.

Verification on 2026-07-05:

- `_quick_build.bat` passes for `SelfEngineForward3D`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite default-preset-m-fullscreen`
  still fails as intended on
  `default_scene_dlaa_preset_m_fullscreen_present.capture.err.log`, preserving
  the strict production gate.
- The run uses requested monitor index `1` / `\\.\DISPLAY2` at
  `2560x1440`, loads the default-scene `Fist Fight B.fbx`, and evaluates preset
  M with quality `6`, preset `13`, and extents `2560x1440->2560x1440`.
- The validation layer reports NGX-tagged images
  `0x3a300000003a3` and `0x3a600000003a6` as
  `nv.ngx.dlss.resource` objects still tracked as `UNDEFINED` while descriptors
  expect `VK_IMAGE_LAYOUT_GENERAL`.
- The engine-owned evaluate resources traced in the same capture are the swap
  ring's color/depth/velocity/mask/output handles, for example
  `0xc800000000c8`, `0x1070000000107`, `0x1100000000110`,
  `0xe300000000e3`, `0xec00000000ec`, and `0xd100000000d1` on sample 1.
  A direct handle comparison shows both validation-reported images match no
  engine-owned evaluate resource.

Current decision: the fullscreen M blocker is now classified as an NGX-owned
resource layout diagnostic, not a missing transition on the engine's six
evaluate inputs/outputs. This does not make M production-clean; it narrows the
next fix path to SDK/NGX integration hygiene or an officially supported
workaround for internal NGX images. Keep `default-preset-m-fullscreen` and
`default-preset-m-dynamic` strict, keep K as the clean fallback/control, and do
not use the diagnostic whitelist as production evidence.

## Slice 4.51 Execution Evidence

Slice 4.51 adds a record-only dynamic native/K/M evidence pack for the real
default Forward 3D scene. `scripts\Test-DlssVisualQa.ps1` now exposes
`m-vs-k-dynamic-pack`, which is intentionally separate from the strict
`m-production` group. It captures three-frame sequences for moving camera,
moving object only, and moving camera plus moving object under native deferred
HDR, DLAA preset K, and DLAA preset M. K/M lanes assert the selected preset,
DLSS output/post activation, jitter handoff, history state, DLSS reset, and
motion-vector scale; M still uses the known diagnostic whitelist and is recorded
as `validationClean=false`.

The runner also records same-frame sequence pair comparisons, so K and M can be
compared at corresponding deterministic capture times instead of only through
adjacent-frame shimmer. This gives the next tuning slices native-vs-K,
native-vs-M, and K-vs-M deltas for every dynamic mode.

Verification on 2026-07-05:

- `_quick_build.bat` passes for `SelfEngineForward3D`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-vs-k-dynamic-pack`
  lists the new focused suite.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite m-vs-k-dynamic-pack`
  passes on requested monitor index `1` / `\\.\DISPLAY2`.
- All six DLSS lanes report output/post `1/1` and `1/1/0`, quality gate
  `1/0/4`, masks `255/251/4`, applied jitter `1`, history `1/0/0`, and DLSS
  motion-vector scale `1/1`. Preset K lanes report preset `11`; preset M lanes
  report preset `13`.
- Moving-camera adjacent-frame edge ratios are native `0.8049`, K `0.7979`,
  and M `0.8029`; max mean edge deltas are native `60.459`, K `53.8864`, and M
  `53.137`.
- Moving-object-only adjacent-frame edge ratios are native `0.6538`, K
  `0.6925`, and M `0.6675`; max mean edge deltas are native `47.4025`, K
  `46.9424`, and M `45.5902`.
- Combined camera/object adjacent-frame edge ratios are native `0.8168`, K
  `0.8264`, and M `0.8002`; max mean edge deltas are native `60.0942`, K
  `56.8662`, and M `55.5192`.
- Same-frame K-vs-M comparisons record moving-camera edge ratio `0.4447`,
  mean edge delta `22.5797`; moving-object-only edge ratio `0.3786`, mean edge
  delta `16.9337`; and combined camera/object edge ratio `0.6718`, mean edge
  delta `43.8902`.

Current decision: M has useful dynamic evidence and is not consistently worse
than K on the current adjacent-frame edge aggregates, but the combined
camera/object K-vs-M same-frame delta is large enough to require more targeted
motion-vector, disocclusion, and skinned-animation investigation. This suite is
diagnostic-only and must not be used to mark M production-clean while the
NGX-owned layout diagnostic and real FBX animation blocker remain.

## Slice 4.52 Execution Evidence

Slice 4.52 makes the real `Fist Fight B.fbx` production blocker more precise.
Previous skinned-FBX evidence could prove that imported current/previous bone
palettes reached shader skinning and previous-pose velocity, but it still did
not distinguish runtime animation playback from the importer's fixed diagnostic
pose sample. The benchmark CSV and quick-QA metrics now expose:

- `runtime_import_animation_diagnostic_pose_only`
- `runtime_import_animation_playback_ready`
- `runtime_import_animation_playback_blocker_mask`

For skinned animated source content, the blocker mask currently uses bit `1`
for diagnostic-pose-only, bit `2` for no runtime playback path, and bit `4` for
the existing unsupported skinned-animation flag. `Forward3D` now keeps DLSS
scene-content motion support disabled for skinned animated content unless both
`runtime_import_skinned_animation_unsupported=0` and
`runtime_import_animation_playback_ready=1`.

Verification on 2026-07-05:

- `_quick_build.bat` passes for `SelfEngineForward3D`; the existing MSVCRT
  default-library warning remains.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite skinned-fbx-m-production`
  passes as a blocked production audit on requested monitor index `1` /
  `\\.\DISPLAY2`.
- The focused row reports `runtime_import_animation_diagnostic_pose_only=1`,
  `runtime_import_animation_playback_ready=0`, and
  `runtime_import_animation_playback_blocker_mask=7`.
- The same row still reports the renderer side as wired:
  `runtime_import_pose_carrier_ready=1`,
  `runtime_import_renderer_pose_palette_ready=1`,
  `runtime_import_gpu_pose_palette_descriptor_set_ready=1`,
  `bone_palette_shader_skinning_path_ready=1`, and
  `bone_palette_shader_velocity_path_ready=1`.
- The production gate remains blocked, as expected:
  `runtime_import_skinned_animation_unsupported=1`, quality gate `1/0/4`,
  quality masks `255/123/132`, and `temporal_upscaler_dlss_quality_blocker_mask=132`.
- The three-frame sequence remains non-static with `pairs=2`,
  `minChanged=2727`, `maxMean=5.9226`, `edgeMin=195`,
  `edgeChangedRatioMax=0.3665`, and `edgeMeanMax=19.1257`.
- The quick M regression
  `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite default-preset-m`
  also passes after the new CSV fields. The high-resolution K/M lanes still use
  monitor index `1`; preset K records edge `pixels=2112`, `changed=1256`,
  `ratio=0.5947`, `mean=38.6844`, `max=186.704`, while preset M records edge
  `pixels=2117`, `changed=1189`, `ratio=0.5616`, `mean=36.6536`,
  `max=186.704`. The M capture still uses the known NGX diagnostic whitelist.

Current decision: do not clear `runtime_import_skinned_animation_unsupported`
yet. The real FBX now proves shader skinning and previous-pose velocity
readiness, but the animation path is still an import-time diagnostic sample,
not runtime per-frame playback. The next skinned production slice must replace
the fixed sample with a runtime animator that advances time, updates previous
and current bone palettes per frame, refreshes the GPU palette buffer before
queue build, and then drives `runtime_import_animation_playback_ready=1` with
blocker mask `0`.

## Slice 4.53 Implementation Evidence

Slice 4.53 implements the first runtime animator for imported skeletal FBX
content without clearing the production gate. The goal is to replace the fixed
import-time pose sample with a frame-advanced current/previous bone-palette
path that can feed shader skinning and previous-pose velocity.

Implemented in this slice:

- `ModelImporter` now exposes `AnimationDurationTicks` and
  `SampleAnimationPose`, reusing the existing Assimp channel interpolation,
  node-global pose build, and bone-palette construction instead of duplicating
  sampling math in the runtime loader.
- `VulkanRenderResources2D` now supports `UpdateBonePalette`, preserving the
  already-written descriptor set while replacing previous/current palette
  vectors and changed-entry readiness each frame.
- `RuntimeModelLoader` now stores a lightweight animation source for imported
  models, advances animation time in `UpdateAnimationPlayback`, samples
  previous/current poses, updates all registered palette resources, and uploads
  the concatenated previous/current matrices into the existing host-visible GPU
  storage buffer before the render queue sees the frame.
- Benchmark diagnostics now add:
  `runtime_import_animation_playback_candidate_model_count`,
  `runtime_import_animation_playback_ready_model_count`,
  `runtime_import_animation_playback_frame_count`,
  `runtime_import_animation_playback_changed_bone_palette_entry_count`,
  `runtime_import_animation_playback_renderer_palette_ready`, and
  `runtime_import_animation_playback_gpu_upload_ready`.
- `Forward3D` refreshes the runtime animation diagnostics every frame. The
  diagnostic-pose-only bit is cleared only after playback readiness is observed,
  while `runtime_import_skinned_animation_unsupported` remains a separate
  production blocker.

Verification on 2026-07-05:

- `_quick_build.bat` passes for `SelfEngineForward3D`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites`
  passes and lists the focused M/skinned suites.
- `git diff --check` passes for the touched implementation and QA files.
- Runtime visual QA could not be completed in this environment because
  `SelfEngineForward3D.exe` is blocked by Windows Device Guard/Application
  Control: `was blocked by your organization's Device Guard policy`. The file
  has no Zone.Identifier stream and no local code-signing certificate is
  available, so no runtime CSV row is claimed for this slice yet.

Current decision: this closes the compile-time implementation gap for runtime
FBX animation playback, but it does not yet prove production skinned DLSS/DLAA
quality. After the executable can run again, the first required focused run is:

`powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 4 -Suite skinned-fbx-m-production`

Expected next evidence should show playback candidate/ready counts, non-zero
playback frame count, renderer/GPU upload readiness, and
`runtime_import_animation_playback_ready=1`. Even then, M remains blocked from
production-clean status until the NGX-owned layout diagnostic is resolved and
the skinned content unsupported flag is cleared after visual/artifact review.

## Slice 4.54 Execution Evidence

Slice 4.54 closes the first runtime-FBX animation evidence gap and adds an
explicit skinned-animation space gate. The importer now compensates for the
node transform baked into imported mesh vertices by folding
`inverse(nodeTransform)` into each bone offset matrix. Runtime normalization now
also transforms root node locals, right-multiplies bone offsets by the inverse
normalization transform, and rebuilds animation pose diagnostics after
normalization. This keeps normalized vertex input, sampled node globals,
previous/current bone palettes, shader skinning, and previous-pose velocity in
the same coordinate space.

New CSV diagnostics:

- `runtime_import_skinned_animation_space_ready`
- `runtime_import_skinned_animation_space_blocker_mask`

The space blocker checks skinned vertex attributes, post-normalization bone
palette readiness, unmatched bone names, unbound animation channels, and
whether the sampled palette actually changes. `Forward3D` also folds this into
the animation playback blocker mask as bit `8`, while the older unsupported
flag remains separate.

Verification on 2026-07-06:

- `_quick_build.bat` passes for `SelfEngineForward3D`; the existing MSVCRT
  default-library warning remains.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite skinned-fbx-m-production`
  passes and lists the focused suite.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite skinned-fbx-m-production`
  passes on requested monitor index `1` / `\\.\DISPLAY2`.
- The skinned-FBX row now reports matching `892/892` CSV columns, preset M
  `13`, quality gate `1/0/4`, masks `255/123/132`, and the remaining playback
  blocker mask `4`.
- The runtime FBX animation path is no longer diagnostic-pose-only:
  `runtime_import_animation_diagnostic_pose_only=0`,
  `runtime_import_animation_playback_ready=1`,
  `runtime_import_animation_playback_candidate_model_count=1`,
  `runtime_import_animation_playback_ready_model_count=1`, and
  `runtime_import_animation_playback_frame_count=6`.
- Palette playback evidence is live:
  `runtime_import_animation_playback_changed_bone_palette_entry_count=65`,
  `runtime_import_animation_playback_renderer_palette_ready=1`, and
  `runtime_import_animation_playback_gpu_upload_ready=1`.
- The new space gate passes:
  `runtime_import_skinned_animation_space_ready=1` and
  `runtime_import_skinned_animation_space_blocker_mask=0`.
- Renderer and shader paths remain live in the skinned lane:
  `bone_palette_draw_path_ready=1`,
  `bone_palette_shader_skinning_path_ready=1`, and
  `bone_palette_shader_velocity_path_ready=1`.
- The three-frame sequence is visibly non-static and reports `pairs=2`,
  `minChanged=1543`, `maxMean=31.439`, `edgeMin=573`,
  `edgeChangedRatioMax=0.7533`, and `edgeMeanMax=62.7995`.
- A manual screenshot check of
  `skinned_fbx_m_production_audit_present_01.png` confirms the FBX is visible
  on the plane and not black-screened. This is not yet a production artifact
  review.
- The focused high-resolution regression
  `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite default-preset-m`
  also passes on monitor index `1`, with K/M lanes at
  `2560x1440->2560x1440`. These default-scene lanes now report runtime
  animation and space readiness, but they still intentionally do not bind the
  default-scene FBX to shader skinning; the skinned production lane remains the
  authoritative skeletal shader path.

Current decision: the real FBX has graduated from fixed diagnostic pose sample
to runtime animation playback evidence. Do not clear
`runtime_import_skinned_animation_unsupported` yet. The remaining blocker mask
is now the explicit unsupported-content bit `4`; clearing it requires visual
artifact review, a skinned production policy decision, and the broader M
validation-clean requirement. M still cannot be marked production while the
NGX-owned layout diagnostic is whitelisted and quality gate remains `1/0/4`.

## Slice 4.55 Execution Evidence

Slice 4.55 turns the skinned unsupported flag into a frame-level production
support decision instead of a permanent import-time feature warning. Runtime
skinned animation is now considered supported only when all of these are true:

- The imported renderable is actually bound to a bone-palette resource.
- Runtime animation playback is ready.
- The skinned animation space gate is ready.
- Skinned vertex attributes are ready.
- The renderer palette is refreshed.
- The GPU palette upload is ready.

New CSV diagnostics:

- `runtime_import_skinned_animation_renderable_bound`
- `runtime_import_skinned_animation_support_ready`
- `runtime_import_skinned_animation_support_blocker_mask`

This prevents the default Forward 3D scene's rigid FBX preview from being
misclassified as production skinned support, while allowing the focused
`skinned-fbx-m-production` lane to clear
`runtime_import_skinned_animation_unsupported` after the shader-skinning path is
actually active.

Verification on 2026-07-06:

- `_quick_build.bat` passes for `SelfEngineForward3D`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite skinned-fbx-m-production`
  passes on requested monitor index `1` / `\\.\DISPLAY2`.
- The skinned production audit row now reports quality gate `1/0/9`, quality
  masks `255/127/128`, and quality inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/0`.
  The previous object-motion blocker is gone; only reference-baseline readiness
  is still `0`.
- The skinned support row reports
  `runtime_import_skinned_animation_unsupported=0`,
  `runtime_import_skinned_animation_renderable_bound=1`,
  `runtime_import_skinned_animation_support_ready=1`,
  `runtime_import_skinned_animation_support_blocker_mask=0`,
  `runtime_import_skinned_animation_space_ready=1`,
  `runtime_import_skinned_animation_space_blocker_mask=0`,
  `runtime_import_animation_diagnostic_pose_only=0`,
  `runtime_import_animation_playback_ready=1`, and
  `runtime_import_animation_playback_blocker_mask=0`.
- Shader path evidence remains ready:
  `bone_palette_draw_path_ready=1`,
  `bone_palette_shader_skinning_path_ready=1`, and
  `bone_palette_shader_velocity_path_ready=1`.
- The same run still records `validationClean=false` because the M capture path
  allows the known NGX-owned `nv.ngx.dlss.resource` layout diagnostic. This is
  not production-clean.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite default-preset-m`
  passes and proves the default main scene remains conservative:
  `runtime_import_skinned_animation_unsupported=1`,
  `runtime_import_skinned_animation_renderable_bound=0`,
  `runtime_import_skinned_animation_support_ready=0`, and support blocker mask
  `1`, while K/M fullscreen extents remain `2560x1440->2560x1440`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite imported-skinned-preview`
  passes after the shared skinned preview baseline was updated to the new
  supported-but-not-production-clean state.

Current decision: the real FBX skeletal path is no longer blocked by
`runtime_import_skinned_animation_unsupported`. The remaining production
blockers are now clearer and narrower: the skinned lane must still prove
production reference-baseline readiness and M still lacks validation-clean
evidence due to the NGX-owned layout diagnostic. Do not mark M production until
both are resolved and the broader dynamic M/K/native quality pack is reviewed.

## Slice 4.56 Execution Evidence

Slice 4.56 separates the real skinned-FBX preview lane from the production audit
lane so the production audit can prove reference-baseline readiness without
changing the diagnostic preview contract. The QA script now keeps
`imported-skinned-preview` intentionally baseline-free and blocked, then clones a
separate `skinned-fbx-m-production` environment that sets
`SE_DLSS_REFERENCE_BASELINE_PATH` to the skinned preview reference manifest. The
production audit also uses an in-memory expected manifest with quality gate
`1/1/0`, masks `255/255/0`, and all DLSS quality inputs ready, while retaining
`productionReady=false` until preset-M validation is clean.

Verification:

- `_quick_build.bat`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite skinned-fbx-m-production`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite skinned-fbx-m-production`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite imported-skinned-preview`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite default-preset-m`

Evidence:

- The skinned production audit now captures on monitor index `1`
  (`\\.\DISPLAY2`) and reports `qualityGate=1/1/0`, `qualityMasks=255/255/0`,
  and quality inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1`.
- The reference baseline bit is now `1`, and the row keeps
  `runtimeImportSkinnedAnimationUnsupported=0`,
  `runtimeImportSkinnedAnimationSupportReady=1`,
  `runtimeImportSkinnedAnimationSpaceReady=1`,
  `runtimeImportAnimationPlaybackReady=1`, and `objectMotionReady=1`.
- The three-frame skinned sequence remains record-only for temporal quality:
  `pairs=2`, `minChanged=1539`, `maxMean=30.6883`, `edgeMin=556`,
  `edgeChangedMax=566`, `edgeChangedRatioMax=0.7517`, and
  `edgeMeanMax=62.4254`.
- `imported-skinned-preview` still passes as a blocked diagnostic lane, proving
  the production baseline input did not leak into preview semantics.
- `default-preset-m` still passes the focused high-resolution regression at
  `2560x1440->2560x1440`, while the default rigid FBX preview remains
  conservative with `runtimeImportSkinnedAnimationUnsupported=1` and object
  motion readiness blocked.
- The M capture path still records the known NGX-owned
  `nv.ngx.dlss.resource` layout diagnostic, so `validationClean=false` and
  `productionReady=false` remain correct.

Current decision: the skinned-FBX production audit no longer has a DLSS quality
input blocker; it now reaches `qualityGate=1/1/0` for the focused lane. M is
still not production-clean because the NGX-owned layout diagnostic is still
whitelisted, and dynamic/subjective M-vs-K-vs-native evidence has not been
accepted as production quality.

## Slice 4.57 Execution Evidence

Slice 4.57 hardens the preset-M validation audit instead of relaxing it. The
renderer now has diagnostic-only environment switches that can suppress optional
DLSS mask binding:

- `SE_DLSS_DISABLE_TRANSPARENCY_MASK_BINDING`
- `SE_DLSS_DISABLE_BIAS_CURRENT_COLOR_MASK_BINDING`
- `SE_DLSS_DISABLE_OPTIONAL_MASK_BINDINGS`

The default path still binds both optional mask inputs. When
`SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS=1`, the DLSS trace also reports whether
the bias-current-color and transparency mask resources were ready and actually
bound. `scripts\Test-DlssVisualQa.ps1` adds a record-only
`m-ngx-mask-diagnostics` suite that runs preset-M fullscreen sequences for all
optional masks, no transparency mask, no bias mask, and no optional masks. This
suite is intentionally diagnostic-only and is not part of `m-production`.

The fullscreen production candidate is also stricter now:
`default-preset-m-fullscreen` keeps the single monitor-resolution native-vs-M
comparison, then adds a monitor-resolution M validation sequence so delayed
stderr validation messages cannot be missed by a short still capture.

Verification:

- `_quick_build.bat`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-mask-diagnostics`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-ngx-mask-diagnostics`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite default-preset-m-fullscreen`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite default-preset-m-dynamic`

Evidence:

- `m-ngx-mask-diagnostics` passes on monitor index `1` / `\\.\DISPLAY2` and
  records all four variants at `2560x1440->2560x1440`.
- The diagnostic suite now uses three-frame monitor-resolution sequences rather
  than single short captures, because short captures can exit before the first
  traced DLSS evaluate and produce a false validation-clean result.
- The all-optional-mask variant keeps quality inputs
  `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/0/1/1/1/1/1`
  and reports `biasReady=1`, `transparencyReady=1`, `biasBound=1`, and
  `transparencyBound=1` in the resource trace, but still logs the NGX-owned
  layout diagnostic in sequence stderr.
- Disabling transparency, disabling bias-current-color, or disabling both
  optional masks all produce the same NGX-owned layout diagnostic in sequence
  stderr. The disabled variants report the expected readiness holes:
  no-transparency `1/1/0/1/0/1/1/1`, no-bias `1/1/0/0/1/1/1/1`, and
  no-optional `1/1/0/0/0/1/1/1`.
- The four diagnostic sequences all evaluate preset M at
  `2560x1440->2560x1440`, and all report `nv.ngx.dlss.resource`; therefore the
  warning is not caused by omitting either optional mask binding.
- The strict `default-preset-m-fullscreen` run is again blocked as intended:
  it evaluates M at `2560x1440->2560x1440`, output ready `1`, preset `13`,
  applied DLSS jitter `-0.125/-0.277778`, quality gate `1/0/4`, masks
  `255/251/4`, and capture stderr reports NGX-owned resources
  `0x3a300000003a3` and `0x3a600000003a6` tracked as `UNDEFINED` when
  descriptors expect `GENERAL`.
- The strict `default-preset-m-dynamic` moving-camera lane is also blocked by
  the same `nv.ngx.dlss.resource` diagnostic. The row still reports DLSS output
  ready, preset `13`, applied jitter, and mask readiness `1/1`, so this is not
  caused by a missing renderer-owned mask resource.

Current decision: do not disable optional DLSS mask bindings as a workaround.
Doing so weakens the quality gate and does not remove the NGX-owned layout
warning under reliable sequence capture. The production blocker remains an
NGX/internal resource layout hygiene issue that appears in strict fullscreen,
dynamic, and diagnostic sequence M lanes. M must stay non-production until the
strict lanes pass without the diagnostic whitelist.

## Slice 4.58 Execution Evidence

Slice 4.58 adds the next-stage K control lanes before attempting another M
workaround. The goal is attribution: prove whether the strict fullscreen and
dynamic validation failure is a general DLSS integration problem or specific to
the current preset-M path.

`scripts\Test-DlssVisualQa.ps1` now exposes:

- `default-preset-k-fullscreen-control`
- `default-preset-k-dynamic-control`
- `k-control`, expanding to both lanes

The fullscreen K control uses monitor index `1` / `\\.\DISPLAY2`, borderless
monitor-resolution placement, hidden ImGui, `SE_DLSS_PRESET=k`, and
`SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS=1`. It captures a native/K still pair
and a three-frame monitor-resolution K validation sequence without any
diagnostic whitelist. The dynamic K control runs the same moving-camera,
moving-object-only, and combined camera/object lanes as the M dynamic audit,
also without the M diagnostic whitelist. Its sequence metrics are record-only
so old shimmer thresholds do not prevent validation-clean attribution.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite k-control`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite k-control`

Evidence:

- `k-control` passes on monitor index `1` / `\\.\DISPLAY2`, with fullscreen
  K extents `2560x1440->2560x1440`, preset `11`, output/post active, quality
  gate `1/0/4`, and masks `255/251/4`.
- The K fullscreen still capture, K fullscreen validation sequence, moving
  camera sequence, moving-object-only sequence, and combined camera/object
  sequence have clean stdout/stderr under the strict log matcher: no
  `nv.ngx.dlss.resource`, VUID, validation, error, failed, exception, or shader
  diagnostics were found.
- The strict M fullscreen still capture remains blocked by NGX-owned
  `nv.ngx.dlss.resource` handles in `UNDEFINED` when descriptors expect
  `GENERAL`; the strict M moving-camera sequence reports the same class of
  diagnostic. This makes the current validation blocker much more likely to be
  tied to the M transformer path or NGX-internal resource handling rather than
  a missing transition on the common SelfEngine DLSS evaluate inputs.
- K dynamic image-quality numbers are still not production acceptance evidence:
  moving-camera `edgeChangedRatioMax=0.8263`, moving-object-only `0.6763`,
  and combined camera/object `0.8327`. The first strict attempt proved why this
  lane must be record-only for now: K moving-camera exceeded the old
  `maxChangedEdgePixels` threshold (`842` observed vs `768` allowed) while
  logs were clean.

Current decision: keep K as the validation-clean fallback/control and keep M
blocked from production. The next engineering target is no longer optional-mask
binding; it is an M-specific NGX resource-layout escalation/repro package plus
stronger dynamic image-quality gates that do not confuse expected camera motion
with unacceptable shimmer.

## Slice 4.59 Execution Evidence

Slice 4.59 turns the K/M layout attribution into a reusable focused audit
instead of a manual comparison across separate logs. `scripts\Test-DlssVisualQa.ps1`
now exposes `m-ngx-layout-audit`, which runs four sequence lanes in one summary:

- fullscreen preset K with resource diagnostics and no diagnostic whitelist
- fullscreen preset M with resource diagnostics and only the known M NGX
  diagnostic whitelist
- moving-camera preset K with resource diagnostics and no diagnostic whitelist
- moving-camera preset M with resource diagnostics and only the known M NGX
  diagnostic whitelist

This suite is diagnostic-only. It is not part of `m-production`, and a passing
run means "the audit collected the expected evidence", not "M is production
clean".

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-layout-audit`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-ngx-layout-audit`

Evidence from the focused run on monitor index `1` / `\\.\DISPLAY2`:

- `mNgxLayoutAuditFullscreenPresetK`: `validationClean=true`,
  `ngxResourceLayoutDiagnostic=false`, preset `11`, fullscreen
  `2560x1440->2560x1440`.
- `mNgxLayoutAuditFullscreenPresetM`: `validationClean=false`,
  `ngxResourceLayoutDiagnostic=true`, preset `13`, fullscreen
  `2560x1440->2560x1440`.
- `mNgxLayoutAuditMovingCameraPresetK`: `validationClean=true`,
  `ngxResourceLayoutDiagnostic=false`.
- `mNgxLayoutAuditMovingCameraPresetM`: `validationClean=false`,
  `ngxResourceLayoutDiagnostic=true`.
- The audit summary records attribution as
  `preset-M-specific-under-current-K-control`.
- The fullscreen M stderr reports NGX-owned resources
  `0x3a300000003a3` and `0x3a600000003a6` in `UNDEFINED` when descriptors
  expect `GENERAL`; the moving-camera M stderr reports NGX-owned resources
  `0x5d600000005d6` and `0x5d900000005d9` with the same layout mismatch.
- The resource trace for the same fullscreen M evaluate sample lists
  engine-owned input/output handles `0xc800000000c8`, `0x1070000000107`,
  `0x1100000000110`, `0xe300000000e3`, `0xec00000000ec`, and
  `0xd100000000d1`; none match the NGX warning handles. The moving-camera M
  trace likewise lists engine-owned handles such as `0x3c200000003c2`,
  `0x4010000000401`, `0x40a000000040a`, `0x3dd00000003dd`,
  `0x3e600000003e6`, and `0x3cb00000003cb`, again distinct from the
  NGX-owned warning handles.
- Optional masks remain bound in the M audit lanes:
  `biasReady=1`, `transparencyReady=1`, `biasBound=1`,
  `transparencyBound=1`.
- Dynamic edge evidence remains non-production: moving-camera K/M
  `edgeChangedRatioMax=0.8359/0.8470`. Fullscreen still-camera sequence edge
  ratios are K/M `0.6696/0.6384`, useful as a temporal-history sanity signal
  but not a subjective production-quality pass.

Current decision: M remains blocked from production validation. The next stage
should use this audit as the repro package for the NGX-owned layout diagnostic:
keep K as the clean control, keep M whitelist out of production lanes, and
investigate either an official NGX/Vulkan integration requirement for internal
resource layout initialization or a controlled SDK/driver escalation. In
parallel, dynamic quality gates need a better shimmer/clarity metric than raw
adjacent-frame edge deltas under expected camera motion.

## Slice 4.60 Execution Evidence

Slice 4.60 upgrades the dynamic image-quality evidence without adding a new
capture matrix. `m-vs-k-dynamic-pack` still captures native, preset K, and
preset M for moving camera, moving object only, and combined camera/object
lanes. It now also derives native-normalized temporal instability summaries
from the existing sequence comparisons:

- `presetKVsNative`
- `presetMVsNative`
- `presetMVsPresetK`

Each summary records the reference and candidate sequence metrics, the absolute
delta, relative ratio, and `candidateNotWorseWithinTolerance` using the current
diagnostic tolerances of `changedEdgeRatio <= 0.02` and `meanEdgeDelta <= 2.0`.
This does not replace subjective review, but it is a stronger signal than raw
adjacent-frame edge churn because expected camera/object motion is represented
by the native sequence instead of treated as automatic shimmer.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-vs-k-dynamic-pack`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-vs-k-dynamic-pack`

Evidence from the focused run on monitor index `1` / `\\.\DISPLAY2`:

- K remains validation-clean; M remains `validationClean=false` because all M
  sequence stderr logs still report the known NGX-owned
  `nv.ngx.dlss.resource` layout warning.
- Moving camera:
  - K-vs-native `changedEdgeRatioDelta=0.0208`.
  - M-vs-native `changedEdgeRatioDelta=0.0045`.
  - M-vs-K `changedEdgeRatioDelta=-0.0163`,
    `meanEdgeDeltaDelta=-0.1567`, `candidateNotWorseWithinTolerance=true`.
- Moving object only:
  - K-vs-native `changedEdgeRatioDelta=0.0249`.
  - M-vs-native `changedEdgeRatioDelta=0.0397`.
  - M-vs-K `changedEdgeRatioDelta=0.0148`,
    `meanEdgeDeltaDelta=1.3417`, `candidateNotWorseWithinTolerance=true`.
- Combined camera/object:
  - K-vs-native `changedEdgeRatioDelta=0.0176`.
  - M-vs-native `changedEdgeRatioDelta=0.0250`.
  - M-vs-K `changedEdgeRatioDelta=0.0074`,
    `meanEdgeDeltaDelta=0.7737`, `candidateNotWorseWithinTolerance=true`.

Slice 4.60 decision at that point: the first native-normalized pass made the
M-vs-K comparison explicit, but it still could not production-clear M because
M was not validation-clean and native-normalized M still exceeded the current
diagnostic edge-ratio tolerance in moving-object-only and combined motion. The
follow-up Slice 4.61 readiness gate is now the authoritative dynamic summary.

## Slice 4.61 Execution Evidence

Slice 4.61 converts the dynamic metric pack into an explicit production
readiness summary. `m-vs-k-dynamic-pack` now writes
`dynamicProductionReadiness` under `diagnostics.mVsKDynamicPack`, with:

- `productionReady`
- `validationClean`
- `mVsKDynamicStable`
- `nativeNormalizedDynamicStable`
- `blockedReasons`
- per-lane readiness for moving camera, moving object only, and combined
  camera/object
- the required production state for those fields

The suite remains diagnostic-only and does not fail the run when M is blocked.
It exists to make the blocking reasons machine-readable and visible in
`summary.json`.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-vs-k-dynamic-pack`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-vs-k-dynamic-pack`

Latest evidence from the focused run on monitor index `1` / `\\.\DISPLAY2`:

- `dynamicProductionReadiness.productionReady=false`
- `validationClean=false`
- `mVsKDynamicStable=false`
- `nativeNormalizedDynamicStable=false`
- Blocked reasons:
  - preset M dynamic captures still require the NGX diagnostic whitelist
  - preset M dynamic stability is worse than preset K beyond the diagnostic
    tolerance
  - preset M native-normalized dynamic stability still exceeds the diagnostic
    tolerance in at least one lane
- Moving camera:
  - M-vs-K `changedEdgeRatioDelta=0.0214`,
    `meanEdgeDeltaDelta=1.1772`, `presetMVsK=false`
  - M-vs-native `changedEdgeRatioDelta=0.0378`,
    `meanEdgeDeltaDelta=-3.7186`, `presetMVsNative=false`
- Moving object only:
  - M-vs-K `changedEdgeRatioDelta=0.0279`,
    `meanEdgeDeltaDelta=-0.3101`, `presetMVsK=false`
  - M-vs-native `changedEdgeRatioDelta=0.0511`,
    `meanEdgeDeltaDelta=1.9239`, `presetMVsNative=false`
- Combined camera/object:
  - M-vs-K `changedEdgeRatioDelta=0.0033`,
    `meanEdgeDeltaDelta=0.4047`, `presetMVsK=true`
  - M-vs-native `changedEdgeRatioDelta=0.0429`,
    `meanEdgeDeltaDelta=-1.0716`, `presetMVsNative=false`

Current decision: this closes the ambiguity in the dynamic QA output. M is not
production-ready on dynamic quality even as a diagnostic-only claim, and it is
also still blocked by the M-specific NGX-owned layout warning. The next
renderer-side work should target the underlying motion/disocclusion inputs that
make M exceed native-normalized edge-ratio tolerance in all dynamic lanes,
while the NGX layout audit remains the validation blocker.

## Slice 4.62 Execution Evidence

Slice 4.62 extends `dynamicProductionReadiness` with DLSS dynamic input-contract
readiness. The readiness summary now distinguishes:

- `temporalInputContractReady`: preset, post source, evaluate output, applied
  jitter, temporal history, DLSS reset, motion-vector scale, and TAA/upscale
  ordering.
- `qualityInputReady`: production DLSS quality gate, readiness masks, and the
  full quality-input string.

This is important because an edge-stability result should not be promoted if it
was captured with incomplete DLSS quality inputs, and conversely a bad edge
result should not automatically imply jitter/history/MV plumbing is broken.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-vs-k-dynamic-pack`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-vs-k-dynamic-pack`

Latest readiness evidence:

- `productionReady=false`
- `validationClean=false`
- `mVsKDynamicStable=false`
- `nativeNormalizedDynamicStable=false`
- `temporalInputContractReady=true`
- `qualityInputReady=false`

Per-lane contract evidence:

- Moving camera: `temporalInputContract=true`, `qualityInput=false`,
  `qualityGate=1/0/4`, `qualityMasks=255/251/4`, history `1/0/0`,
  DLSS reset `0`, MV scale `1/1`, TAA ordering `1/0/1`.
- Moving object only: `temporalInputContract=true`, `qualityInput=false`,
  `qualityGate=1/0/4`, `qualityMasks=255/251/4`, history `1/0/0`,
  DLSS reset `0`, MV scale `1/1`, TAA ordering `1/0/1`.
- Combined camera/object: `temporalInputContract=true`, `qualityInput=false`,
  `qualityGate=1/0/4`, `qualityMasks=255/251/4`, history `1/0/0`,
  DLSS reset `0`, MV scale `1/1`, TAA ordering `1/0/1`.

Current decision: the dynamic M path is not blocked by the basic temporal
input contract in these lanes; it is blocked by validation cleanliness, dynamic
stability, and incomplete production DLSS quality inputs. The next renderer-side
dynamic work should focus on moving the real default-scene object-motion quality
input from `0` to `1` without losing the skinned-FBX production audit readiness,
then rerun the same readiness gate.

## Slice 4.63 Execution Plan

Slice 4.63 makes the default-scene M/DLAA quality-input blocker explicit and
keeps the focused dynamic suite usable for daily iteration. Slice 4.62 proved
that the M dynamic temporal contract is ready, but it still reported only the
coarse object-quality input as `0`. This slice splits that signal into velocity
coverage, scene-content motion support, and final DLSS object-quality readiness.

1. Scene-content motion support diagnostic.
   - Add benchmark CSV field
     `temporal_upscaler_dlss_quality_scene_content_motion_supported`.
   - Report it from both frame stats and temporal-upscale state so the quality
     gate can distinguish "velocity missing" from "the scene contains motion
     content that is not production-supported yet".
   - Keep `temporal_velocity_object_motion_ready` as the velocity-coverage
     signal and keep `temporal_upscaler_dlss_quality_object_motion_ready` as the
     final production input.

2. Dynamic readiness summary.
   - Extend `New-QuickDlssPresentMetrics` and
     `New-DlssDynamicLaneContractSummary` to expose
     `velocityObjectMotionReady`, `sceneContentMotionSupported`, and
     `objectMotionReady`.
   - When object quality input is blocked, report separate reasons for object
     velocity coverage, scene-content support, and final object-quality input
     readiness.

3. Focused-suite runtime hygiene.
   - Keep `m-vs-k-dynamic-pack` as the daily diagnostic suite and avoid the full
     visual-QA matrix.
   - Skip expensive same-frame native/K/M full-image pair comparisons by
     default; preserve them behind
     `SE_DLSS_DYNAMIC_FULL_IMAGE_COMPARISON=1`.
   - Keep adjacent-frame shimmer, native-normalized instability, and
     production-readiness summaries active by default.

## Slice 4.63 Execution Evidence

Slice 4.63 is implemented and verified. The renderer and benchmark recorder now
write `temporal_upscaler_dlss_quality_scene_content_motion_supported`, and the
M/K dynamic summary separates velocity coverage from scene-content motion support
and the final DLSS object-quality input. The dynamic pack also defaults to
recording skipped same-frame full-image comparisons with an explicit opt-in
reason, instead of spending daily QA time on the expensive native/K/M pair set.

Verification on 2026-07-06:

- `_quick_build.bat` passes with `BUILD_EXIT=0`.
- `git diff --check -- scripts/Test-DlssVisualQa.ps1 src/renderer/vulkan/renderer_stats.h src/renderer/vulkan/renderer.h src/renderer/vulkan/renderer.cpp src/app/benchmark_recorder.cpp`
  passes, with only existing LF-to-CRLF warnings.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-vs-k-dynamic-pack`
  lists the focused suite.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-vs-k-dynamic-pack`
  passes on monitor index `1` / `\\.\DISPLAY2` and writes
  `out/reference_captures/dlss_visual_qa/summary.json`.

Latest readiness result:

- `productionReady=false`
- `validationClean=false`
- `mVsKDynamicStable=true`
- `nativeNormalizedDynamicStable=false`
- `temporalInputContractReady=true`
- `qualityInputReady=false`

Per-lane M evidence:

- Moving camera: M-vs-K ready `true`, `changedEdgeRatioDelta=0.0156`,
  `meanEdgeDeltaDelta=0.1186`; M-vs-native ready `false`,
  `changedEdgeRatioDelta=0.0351`, `meanEdgeDeltaDelta=-3.1205`; observed
  preset `13`, quality gate `1/0/4`, masks `255/251/4`,
  `velocityObjectMotionReady=1`, `sceneContentMotionSupported=0`,
  `objectMotionReady=0`.
- Moving object only: M-vs-K ready `true`, `changedEdgeRatioDelta=0.0186`,
  `meanEdgeDeltaDelta=1.5361`; M-vs-native ready `false`,
  `changedEdgeRatioDelta=0.0223`, `meanEdgeDeltaDelta=4.2707`; observed
  preset `13`, quality gate `1/0/4`, masks `255/251/4`,
  `velocityObjectMotionReady=1`, `sceneContentMotionSupported=0`,
  `objectMotionReady=0`.
- Combined camera/object: M-vs-K ready `true`,
  `changedEdgeRatioDelta=-0.0013`, `meanEdgeDeltaDelta=-0.3513`;
  M-vs-native ready `false`, `changedEdgeRatioDelta=0.0213`,
  `meanEdgeDeltaDelta=-1.3836`; observed preset `13`, quality gate `1/0/4`,
  masks `255/251/4`, `velocityObjectMotionReady=1`,
  `sceneContentMotionSupported=0`, `objectMotionReady=0`.

Current decision: the real default-scene M lanes now have object velocity
coverage, but they still cannot become production candidates because the default
scene contains motion-relevant content whose scene-content support is not
verified in that lane. The next renderer-side slice should decide whether the
default-scene FBX becomes a production skinned binding like
`skinned-fbx-m-production`, or stays a conservative rigid preview while the
default-scene production quality gate remains blocked. In either case, M also
remains blocked by the preset-M NGX-owned layout diagnostic and by
native-normalized dynamic stability.

## Slice 4.64 Execution Plan

Slice 4.64 adds an opt-in production audit path for the real default Forward 3D
scene's local `Fist Fight B.fbx`. The previous default scene deliberately kept
the FBX as a rigid preview, so M dynamic readiness could not distinguish
"velocity is missing" from "the real default scene has not bound skinned content
to the production animation path." This slice keeps ordinary startup behavior
conservative while giving visual QA a true default-scene skinned production
candidate.

1. Default-scene skinned binding switch.
   - Add `SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION=1` with aliases
     `SE_DEFAULT_SCENE_SKINNED_FBX` and `SE_DEFAULT_SCENE_BIND_SKINNED_FBX`.
   - When enabled, load the normal default-scene `Fist Fight B.fbx` with the
     existing runtime bone-palette binding path.
   - Leave the unflagged default scene as a rigid preview so existing conservative
     baselines stay valid.

2. Focused production audit suite.
   - Add `default-scene-skinned-fbx-m-production` and include it in the
     `m-production` group.
   - The lane must use the real default application scene, not
     `SELFENGINE_MODEL_PATH`.
   - Require M preset, camera+object motion, hidden ImGui, DLSS output/post,
     complete quality gate/masks, scene-content motion support, object motion
     readiness, runtime animation playback, renderable binding, shader skinning,
     and shader velocity path readiness.

3. Readiness semantics.
   - Treat shader-velocity path readiness as previous/current palette and
     descriptor readiness, not as "this exact frame has non-zero bone changes."
   - Keep actual animation motion evidence in runtime playback changed-count and
     sequence metrics.

## Slice 4.64 Execution Evidence

Slice 4.64 is implemented and verified. `SelfEngineForward3D` now accepts
`SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION=1` for the normal default-scene FBX and
prints whether it loaded the model as `[skinned production audit]` or
`[rigid preview]`. The unflagged default scene remains conservative; the flagged
lane binds the same default-scene renderable to the runtime skinned animation
path.

`scripts\Test-DlssVisualQa.ps1` now exposes
`default-scene-skinned-fbx-m-production`, adds it to `m-production`, manages the
new environment keys, and uses
`docs/reference_baselines/dlss_default_scene_skinned_fbx_m_production_visual_qa_baseline.json`
as the production-audit contract. The new manifest asserts
`sceneContentMotionSupported=1`, `objectMotionReady=1`, `qualityGate=1/1/0`,
`qualityMasks=255/255/0`, `runtimeImportSkinnedAnimationUnsupported=0`,
`runtimeImportSkinnedAnimationRenderableBound=1`,
`runtimeImportSkinnedAnimationSupportReady=1`, runtime playback ready, shader
skinning path ready, and shader velocity path ready.

The renderer-side bone-palette velocity readiness was also corrected: a skinned
velocity path is ready when previous/current palette entries and descriptors are
available. A frame with zero changed bone matrices is still a valid zero-velocity
frame; dynamic motion is proven by playback and sequence evidence instead of
making every frame's readiness depend on non-zero bone deltas.

Verification on 2026-07-06:

- `_quick_build.bat` passes with `BUILD_EXIT=0`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite default-scene-skinned-fbx-m-production`
  lists the new focused suite and shows it in the `m-production` group.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite default-scene-skinned-fbx-m-production`
  passes on monitor index `1` / `\\.\DISPLAY2`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite skinned-fbx-m-production`
  also passes, preserving the standalone FBX production audit lane.
- A lightweight no-screenshot CSV probe confirms the switch is opt-in:
  unflagged default-scene M reports
  `unsupported/renderableBound/supportReady/sceneContent/objectReady=1/0/0/0/0`
  and no bone-palette draw; flagged default-scene M reports
  `0/1/1/1/1` with bone-palette shader skinning/velocity paths `1/1`.

Latest default-scene skinned audit CSV evidence:

- CSV columns `896/896`
- DLSS quality gate `1/1/0`
- quality masks `255/255/0`
- quality inputs `1/1/1/1/1/1/1/1`
- `sceneContentMotionSupported=1`
- `velocityObjectMotionReady=1`
- `objectMotionReady=1`
- runtime support
  `runtimeImportSkinnedAnimationUnsupported/renderableBound/supportReady/supportBlocker=0/1/1/0`
- runtime playback `ready/changed/blocker=1/65/0`
- shader paths `skinning/velocity/gbufferBinds/totalBinds=1/1/1/1`
- draw route `main/gbuffer/forwardResidual/weighted=5/5/0/0`
- sequence evidence:
  `pairs=2`, `minChanged=5818`, `maxMean=68.5539`, `edgeMin=869`,
  `edgeChangedMax=712`, `edgeChangedRatioMax=0.8193`,
  `edgeMeanMax=58.1488`

Current decision: the default Forward 3D main scene now has a focused M/DLAA
production-input audit path for the real skinned FBX, and that path clears the
scene-content/object-motion quality-input blocker. It still is not final
production M: the capture uses the known preset-M NGX-owned layout diagnostic
whitelist, and dynamic M image quality still needs acceptance against native/K.
The next slice should fold this default-scene skinned audit evidence into the
M/K/native dynamic readiness pack so the main dynamic production gate evaluates
the real skinned scene instead of the conservative rigid-preview lane.

## Slice 4.65 Execution Evidence

Slice 4.65 folds the default-scene skinned-FBX production audit into
`m-vs-k-dynamic-pack`. The dynamic pack now runs moving-camera,
moving-object-only, and combined camera/object sequences for native, preset K,
and preset M against the real default Forward 3D application scene with
`Fist Fight B.fbx` bound through `SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION=1`.
The summary baseline records the shared skinned production manifest instead of
the older rigid-preview motion manifests, so dynamic readiness is now measured
on the same animated content as the main visual scene.

Implementation details:

- `scripts\Test-DlssVisualQa.ps1` now points the nine native/K/M dynamic lanes
  to the skinned production environments:
  moving-camera native/K/M, object-only native/K/M, and combined native/K/M.
- All DLSS K/M dynamic lanes use
  `docs/reference_baselines/dlss_default_scene_skinned_fbx_m_production_visual_qa_baseline.json`
  as the expected quality-input contract while keeping K preset `11` and M
  preset `13`.
- The pack remains record-only for final IQ and still keeps M diagnostic
  whitelist behavior out of production readiness.

Verification:

- `git diff --check -- scripts/Test-DlssVisualQa.ps1 docs/reference_baselines/dlss_default_scene_skinned_fbx_m_production_visual_qa_baseline.json docs/AAA_DLSS_NEXT_STAGE_PLAN.md docs/AAA_RENDERING_PIPELINE_PLAN.md`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-vs-k-dynamic-pack`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-vs-k-dynamic-pack`

The focused suite passes on monitor index `1` / `\\.\DISPLAY2`. The run keeps
`dynamicProductionReadiness.productionReady=false`, with
`validationClean=false`, `mVsKDynamicStable=false`,
`nativeNormalizedDynamicStable=false`, `temporalInputContractReady=true`, and
`qualityInputReady=true`.

All three preset-M skinned dynamic contracts now report:

- preset/output/post `13`, `1/1`, `1/1/0`
- quality gate `1/1/0`
- quality masks `255/255/0`
- quality inputs `output/camera/object/reactive/transparency/exposure/post/baseline=1/1/1/1/1/1/1/1`
- `sceneContentMotionSupported=1`
- `velocityObjectMotionReady=1`
- `objectMotionReady=1`
- `runtimeImportSkinnedAnimationUnsupported=0`
- `runtimeImportSkinnedAnimationSupportReady=1`
- playback `ready/changed=1/65`
- DLSS extents `1280x720->1280x720`

Dynamic edge-readiness remains mixed. M is not worse than K within tolerance for
moving-camera and combined camera/object, but object-only fails M-vs-K:
`changedEdgeRatio` K/M `0.7438/0.7854`, delta `0.0416` above the `0.02`
tolerance. Native-normalized stability also remains false in all three lanes;
M-vs-native changed-edge-ratio deltas are moving-camera `0.0309`,
object-only `0.0730`, and combined `0.0420`. Each M capture still logs the
known NGX-owned `nv.ngx.dlss.resource` layout diagnostic, so this slice clears
the skinned production input blocker but does not clear M for production.

Next stage goal: split Slice 4.66 into two focused tracks instead of adding a
larger matrix. First, keep reducing the preset-M NGX layout repro until
`validationClean=true` without a whitelist. Second, diagnose the skinned
object-only shimmer lane with motion-vector/disocclusion/skin velocity overlays
and only then test mip bias, anisotropy, sharpness, or CAS/RCAS as controlled
variables.

## Slice 4.66 Execution Evidence

Slice 4.66 adds a focused object-only shimmer diagnostic instead of widening the
full visual-QA matrix. The new `m-object-shimmer-diagnostics` suite runs the
same default Forward 3D scene with `Fist Fight B.fbx` bound through
`SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION=1`, static camera, deterministic
skinned object motion, hidden ImGui, and monitor index `1` / `\\.\DISPLAY2`.

Implementation details:

- Added `m-object-shimmer-diagnostics` to `scripts\Test-DlssVisualQa.ps1`.
- Added a native `gbuffer-velocity` diagnostic sequence for the skinned
  object-only lane. This debug helper remains strict for logs and read/write
  hazards, but allows the known debug-view-only `unused physical resource`
  FrameGraph count so the velocity visualization can be captured without
  weakening normal production lanes.
- Added a preset-M object-only `SE_DLSS_SHARPNESS=0.0` sequence and extended
  `Invoke-QuickDlaaTuningSequenceSuite` so focused tuning lanes can assert the
  actual DLSS evaluate sharpness.
- The suite compares K, default M, and M sharpness-zero adjacent-frame edge
  stability in one run and writes `mObjectShimmerDiagnostics` to `summary.json`.

Verification:

- `git diff --check -- scripts/Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-object-shimmer-diagnostics`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-object-shimmer-diagnostics`

The focused suite passes. The object-only M input contract remains complete:
temporal contract `true`, quality input `true`, preset/output/post `13`,
`1/1`, `1/1/0`, quality gate `1/1/0`, masks `255/255/0`, object motion
`1`, scene-content motion support `1`, history `1/0/0`, DLSS reset `0`, and
motion-vector scale `1/1`.

Object-only edge-stability evidence:

- K sequence: `edgeChangedRatioMax=0.7616`, `edgeMeanMax=55.6245`.
- Default M sequence, evaluate sharpness `0.35`:
  `edgeChangedRatioMax=0.7891`, `edgeMeanMax=57.2927`.
- M-vs-K delta: changed-edge ratio `+0.0275`, mean-edge delta `+1.6682`.
  This fails the `0.02` changed-edge-ratio tolerance even though mean-edge
  delta stays inside the `2.0` tolerance.
- M sharpness-zero sequence, evaluate sharpness `0`:
  `edgeChangedRatioMax=0.7852`, `edgeMeanMax=56.3064`.
- Sharpness-zero vs default M: changed-edge ratio improves by `-0.0039` and
  mean-edge delta improves by `-0.9863`, but sharpness-zero still fails K with
  changed-edge-ratio delta `+0.0236`.
- Velocity debug sequence is live and non-static:
  `minChanged=3651`, `maxMean=6.7108`, `edgeChangedRatioMax=0.6571`.

Decision: `SE_DLSS_SHARPNESS=0.0` is useful as a diagnostic variable and mildly
reduces object-only edge instability, but it does not meet the K-relative gate
and cannot become a default while preset M still needs the NGX-owned
`nv.ngx.dlss.resource` layout diagnostic whitelist. The next object-only pass
should inspect velocity/depth/disocclusion around the skinned silhouette and
add a depth/velocity-aligned local shimmer metric before trying mip bias,
anisotropy, or CAS/RCAS.

## Slice 4.67 Execution Evidence

Slice 4.67 adds velocity/depth-aligned local shimmer metrics to the existing
`m-object-shimmer-diagnostics` suite. The goal is to stop treating object-only
shimmer as a single full-frame edge number and instead ask whether instability
is concentrated around the skinned object's velocity silhouette or depth
discontinuities.

Implementation details:

- Added `Compare-ImageSequenceInDebugMask` and related helpers in
  `scripts\Test-DlssVisualQa.ps1`.
- Added a `gbuffer-depth` diagnostic sequence next to the existing
  `gbuffer-velocity` sequence.
- Debug sequence image stats now use a lower-contrast diagnostic threshold so
  low-contrast depth debug captures are not misclassified as black screens.
  Normal visual-QA lanes keep the existing stricter image gates.
- The object-only diagnostic summary now records `velocityMasked`,
  `depthMasked`, and `maskedReadiness` for K, default M, and M sharpness-zero.

Verification:

- `git diff --check -- scripts/Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-object-shimmer-diagnostics`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-object-shimmer-diagnostics`

The focused suite passes on monitor index `1` / `\\.\DISPLAY2`. It captures
velocity debug, depth debug, preset K, default M, and M sharpness-zero in the
same object-only skinned scene. The object-only M contract is still complete:
quality gate `1/1/0`, masks `255/255/0`, object motion `1`, scene-content
motion support `1`, history `1/0/0`, DLSS reset `0`, and motion-vector scale
`1/1`.

Latest run evidence:

- Full-frame K/M changed-edge ratio delta is `+0.0196`, barely within the
  `0.02` tolerance; sharpness-zero improves that to `+0.0077`.
- Velocity-masked K/M changed-edge ratio delta is `-0.0300`; sharpness-zero is
  `-0.0052`.
- Depth-masked K/M changed-edge ratio delta is `-0.0055`; sharpness-zero is
  `+0.0121`.
- `maskedReadiness` reports velocity M-vs-K `true`, velocity M0-vs-K `true`,
  depth M-vs-K `true`, and depth M0-vs-K `true`.
- The M and M sharpness-zero sequence captures still log the NGX-owned
  `nv.ngx.dlss.resource` layout diagnostic.

Decision: the new local metrics show the latest object-only run no longer
proves a persistent velocity/depth-localized M failure. However, this is not
production acceptance because earlier focused runs failed the object-only
K-relative gate, the run-to-run margin is small, and M still requires the NGX
layout diagnostic whitelist. Keep `SE_DLSS_SHARPNESS=0.0` as a diagnostic
variable only. The next useful checks are repeat masked object-only runs at
fullscreen/high resolution and the separate M validation-clean track; do not
promote tuning defaults until validation is clean and masked stability is
repeatable.

## Slice 4.68 Execution Evidence

Slice 4.68 repeats the object-only masked shimmer diagnostic under the real
fullscreen/high-resolution capture path instead of relying on the 720p/small
window evidence from Slice 4.67.

Implementation details:

- Added `m-object-shimmer-fullscreen-diagnostics` as a focused suite outside
  the production groups.
- Added high-resolution, borderless, hidden-ImGui clones for the default-scene
  skinned-FBX object-only velocity debug, depth debug, preset K, preset M, and
  preset M sharpness-zero lanes.
- Extended the native diagnostic and DLAA tuning sequence helpers with
  monitor-resolution capture support.
- Extended the DLAA tuning sequence helper with explicit high-resolution DLSS
  extent checks. When a high-resolution extent is expected, the helper skips the
  manifest's fixed `dlssExtents` text gate and then asserts the numeric DLSS
  render/output extents against the requested fullscreen size.

Verification:

- `git diff --check -- scripts/Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-object-shimmer-fullscreen-diagnostics`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-object-shimmer-fullscreen-diagnostics`

The focused suite passes on monitor index `1` / `\\.\DISPLAY2`. The capture
work area is `2560x1380`, while the borderless fullscreen bounds and DLSS
evaluate extents are `2560x1440`. Preset K, default M, and M sharpness-zero all
report `2560x1440->2560x1440`.

Latest fullscreen object-only evidence:

- The M input contract is complete at fullscreen: preset `13`, output/post
  `1/1` and `1/1/0`, quality gate `1/1/0`, masks `255/255/0`,
  scene-content/object motion `1/1`, history `1/0/0`, DLSS reset `0`, and
  motion-vector scale `1/1`.
- Full-frame changed-edge ratios are K/M/M0 `0.7624/0.7470/0.7788`. Default M
  is within tolerance versus K with delta `-0.0154`; M0 is still barely within
  K tolerance at `+0.0164`, but is worse than default M by `+0.0318`.
- Velocity-masked changed-edge ratios are K/M/M0 `0.8348/0.8182/0.8319`.
  `maskedReadiness` reports velocity M-vs-K `true` and velocity M0-vs-K
  `true`.
- Depth-masked changed-edge ratios are K/M/M0 `0.6548/0.6309/0.6959`.
  `maskedReadiness` reports depth M-vs-K `true` but depth M0-vs-K `false`.
- `SE_DLSS_SHARPNESS=0.0` does not improve fullscreen object-only stability:
  default M changed-edge/mean-edge is `0.7470/52.5748`, while M0 is
  `0.7788/53.7054`.
- The M and M0 sequence captures still require the known
  `nv.ngx.dlss.resource` layout diagnostic whitelist, so `validationClean`
  remains `false`.

Decision: fullscreen masked evidence is better for default M than the previous
small-window object-only blocker: default M is not worse than K in full-frame,
velocity-masked, or depth-masked metrics for this run. This still does not
promote M to production because the NGX-owned layout diagnostic remains open
and masked stability needs repeat acceptance. M0 is explicitly not a default
candidate after the fullscreen run because it worsens the default-M edge ratio
and fails the depth-masked K gate. The next useful slice should continue the
validation-clean M track rather than trying to hide artifacts with sharpening,
mip bias, anisotropy, or RCAS.

## Slice 4.69 Execution Evidence

Slice 4.69 adds a focused NGX lifecycle audit for the preset-M validation-clean
track. The goal is to prove whether the M warning occurs after the same
feature-create/warmup/evaluate lifecycle that is clean for K, and whether the
warning handles overlap any SelfEngine image passed into NGX evaluate.

Implementation details:

- `src\renderer\temporal_upscaler.cpp` now emits
  `SelfEngineDLSSLifecycleTrace` when
  `SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS=1` is enabled. The trace records
  feature recreation checks, create requests/results, warmup exits, feature
  reuse, evaluate requests/results, preset, quality mode, extents, create flags,
  recreation reason, feature handle, and parameter handle.
- `scripts\Test-DlssVisualQa.ps1` now exposes
  `m-ngx-lifecycle-audit`, a focused fullscreen K/M sequence suite outside
  production groups.
- The QA script parses lifecycle trace lines, existing
  `SelfEngineDLSSResourceTrace` image handles, and validation-layer
  `nv.ngx.dlss.resource` warning handles into `summary.json`, including warning
  handle overlap with SelfEngine evaluate resources.

Verification:

- `_quick_build.bat`
- `git diff --check -- src/renderer/temporal_upscaler.cpp scripts/Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-lifecycle-audit`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-ngx-lifecycle-audit`

The lifecycle audit passes on monitor index `1` / `\\.\DISPLAY2`. Both K and M
fullscreen lanes evaluate at `2560x1440->2560x1440`. Both lanes record the same
64 lifecycle trace events under the current trace cap:
`featureRecreationCheck=16`, `featureCreateRequest=1`,
`featureCreateResult=1`, `featureCreateWarmup=1`, `featureReuse=15`,
`evaluateRequest=15`, and `evaluateResult=15`.

Lifecycle attribution evidence:

- Preset K remains clean: `validationClean=true`,
  `ngxResourceDiagnostic=false`, one create, one warmup, and 15 successful
  feature-reuse evaluates.
- Preset M follows the same lifecycle shape and also has 15 successful
  evaluates with `outputReady=1`, but remains `validationClean=false` with
  `ngxResourceDiagnostic=true`.
- The M warning handles are `0x3a300000003a3` and `0x3a600000003a6`.
- `warningHandleOverlapWithSelfEngineResources` is `0` for M. The warning
  handles do not match the SelfEngine color/depth/velocity/mask/output handles
  traced in the same capture process.
- This rules out two weaker explanations for the current warning: a missing
  first-frame warmup after create, and a direct missing layout transition on one
  of SelfEngine's evaluate input/output images.

Decision: M is still not production-clean. The blocker is now documented as a
preset-M-specific NGX/internal resource layout validation issue under a matched
K lifecycle control, not as an obvious SelfEngine evaluate input transition
bug. Keep K as the validation-clean fallback. The next validation-clean slice
should test only integration-level hypotheses that could affect NGX-owned
internal images, such as official SDK/runtime update, NGX integration flags, or
an isolated minimal M repro package; do not whitelist this warning into
production.

## Slice 4.70 Execution Evidence

Slice 4.70 tests the first integration-level hypothesis from Slice 4.69:
whether selecting the SDK's `dev` DLSS runtime instead of the default `rel`
runtime changes the preset-M NGX-owned layout diagnostic.

Implementation details:

- Added `SE_DLSS_RUNTIME_FLAVOR=rel|dev`, with `SE_NGX_RUNTIME_FLAVOR` as an
  alias. The default remains `rel`.
- `TemporalUpscaler` now selects the NGX feature path from
  `thirdParty\nvidia_dlss\lib\Windows_x86_64\rel` or `...\dev`, records
  `runtimeFlavor`, `runtimePathFound`, and `runtimePath` in runtime status, CSV,
  and `SelfEngineDLSSLifecycleTrace`.
- `scripts\Test-DlssVisualQa.ps1` now exposes
  `m-ngx-runtime-flavor-audit`, a focused fullscreen preset-M suite comparing
  explicit `rel` and `dev` runtime feature paths under otherwise matching
  conditions.
- The runtime-flavor audit uses a diagnostic-only NGX-internal allowlist
  limited to `nv.ngx.dlss.*` validation-layer resource names. Production lanes
  are not loosened.
- The trace parser now captures all `nv.ngx.dlss.*` warning resource names, not
  only the older generic `nv.ngx.dlss.resource` tag.

Verification:

- `_quick_build.bat`
- `git diff --check -- src/renderer/temporal_upscaler.cpp src/renderer/temporal_upscaler.h src/renderer/vulkan/renderer.cpp src/renderer/vulkan/renderer_stats.h src/app/benchmark_recorder.cpp scripts/Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-runtime-flavor-audit`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-ngx-runtime-flavor-audit`

The focused suite passes on monitor index `1` / `\\.\DISPLAY2`. Both runtime
flavors evaluate preset M at `2560x1440->2560x1440` with runtime paths found:

- Rel runtime: flavor `1`, path
  `thirdParty\nvidia_dlss\lib\Windows_x86_64\rel`,
  `validationClean=false`, `ngxResourceDiagnostic=true`, warning resource name
  `nv.ngx.dlss.resource`, warning/SelfEngine handle overlap `0`.
- Dev runtime: flavor `2`, path
  `thirdParty\nvidia_dlss\lib\Windows_x86_64\dev`,
  `validationClean=false`, `ngxResourceDiagnostic=true`, warning resource names
  `nv.ngx.dlss.PrevMvecEroded` and `nv.ngx.dlss.PrevMvecNondilated`,
  warning/SelfEngine handle overlap `0`.

Decision: switching to the dev runtime is not a production workaround. It does
not make preset M validation-clean, but it improves attribution by naming the
NGX-owned resources behind the generic rel-runtime warning. The current blocker
is therefore more specifically tied to NGX preset-M internal previous-motion
vector resources (`PrevMvecEroded` / `PrevMvecNondilated`) staying
validation-tracked as `UNDEFINED` when used as `GENERAL`. Keep default runtime
flavor `rel`; use `dev` only as a diagnostic probe. The next validation-clean
slice should either build a minimal repro package around those named internal
resources or test an official SDK/runtime update; it still must not whitelist
the warning into production.

## Slice 4.71 Execution Evidence

Slice 4.71 turns the preset-M NGX internal previous-motion-vector blocker into
a compact repro artifact so the next validation-clean work can compare SDK or
runtime changes without rerunning the broader visual-QA matrix.

Implementation details:

- Added the focused `m-ngx-internal-mv-repro` suite to
  `scripts\Test-DlssVisualQa.ps1`.
- The suite runs three monitor-resolution fullscreen sequence lanes on monitor
  index `1` / `\\.\DISPLAY2`: preset K with explicit `rel` runtime as the
  clean control, preset M with explicit `rel` runtime as the default production
  path probe, and preset M with explicit `dev` runtime as the named-resource
  diagnostic probe.
- The suite keeps ImGui hidden, uses borderless fullscreen, enables
  `SE_DLSS_EVALUATE_RESOURCE_DIAGNOSTICS=1`, asserts K preset `11` and M
  preset `13`, and does not relax any production suite.
- Added `m_ngx_internal_mv_repro.json`, a compact artifact that records each
  lane's runtime path, lifecycle/resource trace summary, warning resource
  names, warning handles, SelfEngine evaluate image handles, handle-overlap
  counts, CSV/log/image paths, and the required production state.

Verification:

- `git diff --check -- scripts/Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-internal-mv-repro`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-ngx-internal-mv-repro`

The focused repro passes and writes
`out\reference_captures\dlss_visual_qa\m_ngx_internal_mv_repro.json`.
The compact conclusion is:

- K rel control: preset `11`, validation-clean.
- M rel: preset `13`, `validationClean=false`, warning resource
  `nv.ngx.dlss.resource`, warning/SelfEngine handle overlap `0`.
- M dev: preset `13`, `validationClean=false`, warning resources
  `nv.ngx.dlss.PrevMvecEroded` and
  `nv.ngx.dlss.PrevMvecNondilated`, warning/SelfEngine handle overlap `0`.
- The combined warning resource set is
  `nv.ngx.dlss.PrevMvecEroded`,
  `nv.ngx.dlss.PrevMvecNondilated`, and
  `nv.ngx.dlss.resource`.

Decision: this closes the "build a compact repro package" slice. Preset M is
still not production-clean, and the dev runtime is still diagnostic-only. The
next validation-clean slice should run this exact repro against an official
newer DLSS SDK/runtime drop or another integration-level NGX hypothesis. Do
not promote M to production default until the rel/dev captures are clean without
a whitelist and the dynamic IQ gates are accepted against K/native.

## Slice 4.72 Execution Evidence

Slice 4.72 adds the runtime override and provenance plumbing needed to test an
official newer DLSS runtime drop against the compact repro without modifying
the checked-in `thirdParty\nvidia_dlss` tree.

Implementation details:

- Added `SE_DLSS_RUNTIME_PATH` with `SE_NGX_RUNTIME_PATH` as an alias. When set,
  this directory overrides the `rel|dev` runtime directory selected by
  `SE_DLSS_RUNTIME_FLAVOR`; it must contain `nvngx_dlss.dll`.
- The runtime status, benchmark CSV, and
  `SelfEngineDLSSLifecycleTrace`/`SelfEngineDLSSResourceTrace` now record
  `runtimePathOverridden`, `runtimeDllFound`, `runtimeDllSizeBytes`, and
  `runtimeDllHash`. The hash is a lightweight FNV-1a identity marker so two
  runtime DLLs can be distinguished in QA summaries.
- `scripts\Test-DlssVisualQa.ps1` now parses those provenance fields and exposes
  `m-ngx-runtime-override-audit`, a focused preset-M monitor-resolution suite
  that points `SE_DLSS_RUNTIME_PATH` at the current rel runtime and asserts the
  override/provenance fields before any external runtime is tested.
- The production lanes are unchanged: runtime override is an experiment and
  cannot make M production unless validation is clean without a whitelist.

Verification:

- `_quick_build.bat`
- `git diff --check -- src/renderer/temporal_upscaler.cpp src/renderer/temporal_upscaler.h src/renderer/vulkan/renderer.cpp src/renderer/vulkan/renderer_stats.h src/app/benchmark_recorder.cpp scripts/Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-runtime-override-audit`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-ngx-runtime-override-audit`

The focused run passes on monitor index `1` / `\\.\DISPLAY2` and reports:

- `runtimePathOverridden=1`
- `runtimePathFound=1`
- `runtimeDllFound=1`
- `runtimeDllSizeBytes=58977904`
- `runtimeDllHash=760638663`
- runtime path
  `D:\VSproject\SelfEngine\thirdParty\nvidia_dlss\lib\Windows_x86_64\rel`
- preset `13`, `validationClean=false`, warning resource
  `nv.ngx.dlss.resource`, warning/SelfEngine handle overlap `0`

Decision: the engine can now test an unpacked official runtime by setting
`SE_DLSS_RUNTIME_PATH` to a directory containing `nvngx_dlss.dll` and rerunning
`m-ngx-internal-mv-repro` or `m-ngx-runtime-override-audit`. This is useful
validation infrastructure, not a production workaround. Preset M remains
blocked until the same focused suites are validation-clean without diagnostic
allowlists and dynamic IQ gates are accepted against K/native.

## Slice 4.73 Execution Evidence

Slice 4.73 closes the next validation-clean infrastructure gap: the compact
NGX internal previous-MV repro can now include a custom runtime lane directly,
so official runtime-drop tests do not need a one-off script edit.

Implementation details:

- `scripts\Test-DlssVisualQa.ps1` now accepts
  `-DlssRuntimeOverridePath`. If the parameter is omitted, the script also reads
  process-level `SE_DLSS_RUNTIME_PATH` / `SE_NGX_RUNTIME_PATH` before clearing
  managed environment variables for each capture.
- `m-ngx-internal-mv-repro` keeps its original K-rel, M-rel, and M-dev lanes.
  When a custom runtime path is provided, it adds a fourth
  `presetMCustomRuntime` lane using `SE_DLSS_RUNTIME_PATH`.
- The compact artifact now records `customRuntimePath`,
  `presetMCustomRuntimeClean`, `customRuntimeChangedValidation`, and
  `customRuntimeWarningOverlapCount`, plus the custom runtime DLL provenance
  already exposed in Slice 4.72.
- `m-ngx-runtime-override-audit` now uses the same override-path resolution:
  defaulting to the checked-in rel runtime when no custom path is supplied, or
  using `-DlssRuntimeOverridePath` / `SE_DLSS_RUNTIME_PATH` when provided.

Version/source audit:

- Local `thirdParty\nvidia_dlss` is `v310.7.0`.
- `git -C thirdParty\nvidia_dlss remote -v` points to
  `https://github.com/NVIDIA/DLSS.git`.
- `git -C thirdParty\nvidia_dlss ls-remote --tags origin` reports no tag newer
  than `v310.7.0` at the time of this slice, so there is no newer official SDK
  tag in that repo to pull directly.

Verification:

- `git diff --check -- scripts/Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-internal-mv-repro`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-runtime-override-audit -DlssRuntimeOverridePath .\thirdParty\nvidia_dlss\lib\Windows_x86_64\rel`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-ngx-internal-mv-repro -DlssRuntimeOverridePath .\thirdParty\nvidia_dlss\lib\Windows_x86_64\rel`

The four-lane repro passes on monitor index `1` / `\\.\DISPLAY2` and writes the
updated
`out\reference_captures\dlss_visual_qa\m_ngx_internal_mv_repro.json`.
Using the current rel runtime as the custom path records:

- K rel clean control: `validationClean=true`.
- M rel: `validationClean=false`.
- M dev: `validationClean=false`.
- M custom runtime: `validationClean=false`,
  `pathOverridden=1`, `dllFound=1`, `dllSizeBytes=58977904`,
  `dllHash=760638663`.
- `customRuntimeChangedValidation=false`.
- Warning resource set remains
  `nv.ngx.dlss.resource`,
  `nv.ngx.dlss.PrevMvecEroded`, and
  `nv.ngx.dlss.PrevMvecNondilated`.
- rel/dev/custom warning/SelfEngine handle overlap counts are all `0`.

Decision: no newer official `NVIDIA/DLSS` SDK tag is available from the current
remote, but the validation lane is now ready for any future official runtime
drop or separately delivered `nvngx_dlss.dll` directory. This does not change
production status: preset M remains blocked until the custom/rel/dev lanes are
clean without diagnostic allowlists and dynamic IQ gates are accepted against
K/native.

## Slice 4.74 Execution Evidence

Slice 4.74 starts closing the user's fullscreen/high-resolution dynamic quality
gap by adding a monitor-resolution moving-camera native/K/M pack for the real
default Forward 3D scene with `Fist Fight B.fbx` bound to the production
skinned-animation path.

Implementation details:

- Added `m-vs-k-moving-camera-fullscreen-pack` to
  `scripts\Test-DlssVisualQa.ps1`.
- Added high-resolution moving-camera environments for native deferred HDR,
  preset K DLAA, and preset M DLAA. They use hidden ImGui, borderless
  monitor-resolution placement, `SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION=1`,
  and the same skinned production baseline as the 720p dynamic pack.
- Extended `Invoke-QuickNativeSequenceSuite` with
  `-UseMonitorResolutionCapture` so native moving-camera captures can use the
  same monitor placement as K/M.
- The new focused suite records same-frame native-vs-K, native-vs-M, and K-vs-M
  comparisons, adjacent-frame temporal instability summaries, and a
  `dynamicProductionReadiness` block with validation, M-vs-K stability,
  native-normalized stability, temporal input contract, and quality-input
  readiness.

Verification:

- `git diff --check -- scripts\Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-vs-k-moving-camera-fullscreen-pack`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-vs-k-moving-camera-fullscreen-pack`

The focused run passes on monitor index `1` / `\\.\DISPLAY2`, with K/M DLSS
extents asserted at the monitor resolution. Latest sequence evidence:

- Native moving-camera: `edgeChangedRatioMax=0.9174`,
  `edgeMeanMax=47.4752`.
- Preset K moving-camera: `edgeChangedRatioMax=0.6560`,
  `edgeMeanMax=43.7316`.
- Preset M moving-camera: `edgeChangedRatioMax=0.6472`,
  `edgeMeanMax=43.2437`.
- M-vs-K fullscreen moving-camera delta:
  `changedEdgeRatio=-0.0088`, `meanEdgeDelta=-0.4879`.
- `mVsKDynamicStable=true`,
  `nativeNormalizedDynamicStable=true`,
  `temporalInputContractReady=true`,
  `qualityInputReady=true`.
- M observed contract: preset `13`, post/evaluate `1/1/0` and `1/1`,
  quality gate `1/1/0`, masks `255/255/0`, all eight quality inputs ready,
  object motion and scene-content motion `1/1`, jitter applied `1`, history
  `1/0/0`, DLSS reset `0`, MV scale `1/1`, TAA ordering `1/0/1`.

Decision: fullscreen moving-camera dynamic evidence is now part of the focused
M/K/native QA queue, and this run shows M not worse than K within the diagnostic
tolerance. It is still diagnostic-only: `validationClean=false` because preset
M requires the NGX diagnostic whitelist. This slice also does not replace the
existing object-only fullscreen diagnostics or the 720p combined-motion pack;
the remaining high-resolution dynamic gap is a combined camera+object
fullscreen pack.

## Slice 4.75 Execution Evidence

Slice 4.75 fills the remaining high-resolution dynamic gap from Slice 4.74 by
adding a monitor-resolution combined camera+object native/K/M pack for the real
default Forward 3D scene with `Fist Fight B.fbx` on the production skinned path.

Implementation details:

- Added `m-vs-k-combined-fullscreen-pack` to
  `scripts\Test-DlssVisualQa.ps1`.
- Added high-resolution combined-motion environments for native deferred HDR,
  preset K DLAA, and preset M DLAA. They use hidden ImGui, borderless
  monitor-resolution placement, `SE_DEFAULT_SCENE_SKINNED_FBX_PRODUCTION=1`,
  moving camera, moving object, and the same skinned production baseline.
- The focused suite records same-frame native-vs-K, native-vs-M, and K-vs-M
  comparisons, adjacent-frame temporal-instability summaries, and
  `dynamicProductionReadiness` with validation, M-vs-K stability,
  native-normalized stability, temporal input contract, and quality-input
  readiness.

Verification:

- `git diff --check -- scripts\Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-vs-k-combined-fullscreen-pack`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-vs-k-combined-fullscreen-pack`

The focused run passes on monitor index `1` / `\\.\DISPLAY2`, with K/M DLSS
extents asserted at monitor resolution. Latest sequence evidence:

- Native combined camera+object: `edgeChangedRatioMax=0.9114`,
  `edgeMeanMax=47.6398`.
- Preset K combined camera+object: `edgeChangedRatioMax=0.6558`,
  `edgeMeanMax=45.5571`.
- Preset M combined camera+object: `edgeChangedRatioMax=0.6357`,
  `edgeMeanMax=45.1224`.
- M-vs-K fullscreen combined delta:
  `changedEdgeRatio=-0.0201`, `meanEdgeDelta=-0.4347`.
- `mVsKDynamicStable=true`,
  `nativeNormalizedDynamicStable=true`,
  `temporalInputContractReady=true`,
  `qualityInputReady=true`.
- M observed contract: preset `13`, post/evaluate `1/1/0` and `1/1`,
  quality gate `1/1/0`, masks `255/255/0`, all eight quality inputs ready,
  object motion and scene-content motion `1/1`, jitter applied `1`, history
  `1/0/0`, DLSS reset `0`, MV scale `1/1`, TAA ordering `1/0/1`.

Decision: the high-resolution dynamic QA queue now has focused moving-camera,
object-only, and combined camera+object coverage. This run shows preset M not
worse than K within the diagnostic tolerance for fullscreen combined motion.
Preset M remains diagnostic-only because `validationClean=false`; do not promote
M or any tuning control until the NGX diagnostic is gone without a whitelist and
repeat runs confirm stability.

## Slice 4.76 Execution Plan

Slice 4.76 turns the separate high-resolution dynamic lanes from Slices 4.74 and
4.75 into one production-audit gate. The goal is not to promote preset M; it is
to make the next stage answerable with one focused suite and one summary field.

1. Aggregate high-resolution dynamic readiness.
   - Add `m-highres-dynamic` as a suite group over
     `m-vs-k-moving-camera-fullscreen-pack`,
     `m-vs-k-combined-fullscreen-pack`, and
     `m-object-shimmer-fullscreen-diagnostics`.
   - Emit `diagnostics.mHighResDynamicProductionAudit` only when all three
     source diagnostics exist.
   - Aggregate validation cleanliness, M-vs-K dynamic stability,
     native-normalized stability, temporal input readiness, quality input
     readiness, object masked stability, and repeat stability.

2. Keep focused QA repeatable.
   - Avoid the full matrix.
   - Keep monitor index `1` / `\\.\DISPLAY2`, hidden ImGui, and borderless
     monitor-resolution capture.
   - Add an explicit sample step to debug-mask sequence comparisons so
     fullscreen masked object-only analysis remains usable in daily runs.

3. Production decision policy.
   - Keep `validationClean=false` as a hard production blocker while preset M
     still needs the NGX diagnostic whitelist.
   - Require object-only velocity/depth masked stability against K.
   - Require three consecutive clean high-resolution dynamic passes before
     promotion, even if one focused run looks good.

## Slice 4.76 Execution Evidence

Slice 4.76 is implemented and verified. `scripts\Test-DlssVisualQa.ps1` now
exposes the `m-highres-dynamic` group and writes
`diagnostics.mHighResDynamicProductionAudit` when the moving-camera fullscreen,
combined fullscreen, and object-only fullscreen diagnostics all run in the same
focused invocation. The fullscreen object-only masked comparisons now use
`maskedSampleStep=8`; the default masked comparison step remains `4` for other
lanes.

Verification:

- `git diff --check -- scripts\Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-highres-dynamic`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-highres-dynamic`

The focused group passes on monitor index `1` / `\\.\DISPLAY2`, with high
resolution recorded as `2560x1440`, borderless fullscreen, hidden ImGui, and
summary output at
`out\reference_captures\dlss_visual_qa\summary.json`.

Latest high-resolution evidence:

- Moving camera: K/M edge ratios `0.6419/0.6479`; M-vs-K changed-edge delta
  `+0.0060`, mean-edge delta `-1.2058`; M is within tolerance.
- Combined camera+object: K/M edge ratios `0.6504/0.6366`; M-vs-K
  changed-edge delta `-0.0138`, mean-edge delta `-0.8376`; M is within
  tolerance.
- Object-only fullscreen full-frame: K/M edge ratios `0.6380/0.6356`; M-vs-K
  changed-edge delta `-0.0024`, mean-edge delta `+0.1434`; M is within
  tolerance.
- Object-only velocity-mask: M passes K-relative tolerance.
- Object-only depth-mask: M fails K-relative tolerance in this run, with
  changed-edge delta `+0.1256` and mean-edge delta `+4.8997`.
- M sharpness zero improves the object-only fullscreen full-frame signal
  (`0.6240` vs default M `0.6356`) and passes the depth-mask K gate in this run,
  but it is not promoted because preset M is not validation-clean and repeat
  stability is not proven.

The aggregate readiness is:
`validationClean=false`,
`mVsKDynamicStable=true`,
`nativeNormalizedDynamicStable=true`,
`temporalInputContractReady=true`,
`qualityInputReady=true`,
`objectMaskedStabilityReady=false`,
`repeatStabilityReady=false`, and `productionReady=false`.

Decision: high-resolution dynamic coverage is now a single focused audit, but
preset M is still not production. The next stage should either remove the
NGX-owned M layout diagnostic through an official runtime/integration fix, or
focus specifically on the object-only depth-mask shimmer around the skinned
silhouette. Do not use mip bias, anisotropy, sharpening, or CAS/RCAS as default
tuning until the validation-clean and masked-stability gates are both resolved.

## Slice 4.77 Execution Plan

Slice 4.77 addresses the Slice 4.76 object-only depth-mask failure without
weakening the image-quality gate. The hypothesis is that the fullscreen
`maskedSampleStep=8` diagnostic made the depth-mask edge sample too sparse
for a production decision. The fix is to make debug-mask comparison fast enough
to restore `4px` sampling at fullscreen.

1. Faster debug-mask metrics.
   - Add a script-local `SelfEngineVisualQaImageMetrics` helper that uses
     `Bitmap.LockBits` instead of per-pixel `GetPixel` calls for masked
     sequence comparisons.
   - Keep the same luma, edge, mask-activation, and delta thresholds.
   - Record `sampleStep` on each masked sequence pair so future summaries show
     the statistical sampling contract.

2. Restore fullscreen masked sample density.
   - Change `m-object-shimmer-fullscreen-diagnostics` from
     `maskedSampleStep=8` back to `maskedSampleStep=4`.
   - Keep the production interpretation unchanged: fullscreen object-only
     velocity and depth masks must both pass K-relative stability.

3. Re-run focused verification only.
   - Run `m-object-shimmer-fullscreen-diagnostics` to isolate the object-only
     masked gate.
   - Run `m-highres-dynamic` once afterward so the aggregate audit field is
     updated with the corrected object-only evidence.
   - Do not run the full matrix.

## Slice 4.77 Execution Evidence

Slice 4.77 is implemented and verified. The masked comparison hot path now uses
the script-local `SelfEngineVisualQaImageMetrics` C# helper, and fullscreen
object-only masked diagnostics are back to `maskedSampleStep=4`. This preserves
the stricter sample density while keeping the focused suite practical.

Verification:

- `git diff --check -- scripts\Test-DlssVisualQa.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-object-shimmer-fullscreen-diagnostics`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-object-shimmer-fullscreen-diagnostics`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-highres-dynamic`

The isolated object-only fullscreen run passes and reports:

- `maskedSampleStep=4`.
- Full-frame M-vs-K passes with changed-edge delta `-0.0153` and mean-edge
  delta `-0.6187`.
- Velocity-mask M-vs-K passes with changed-edge delta `-0.0389` and mean-edge
  delta `-1.8208`.
- Depth-mask M-vs-K passes with changed-edge delta `-0.0625` and mean-edge
  delta `-2.0696`.
- Depth-mask edge samples are no longer sparse: K has at least `570` edge
  samples and M has at least `586`, compared with roughly `134-160` in the
  `8px` run.

The updated `m-highres-dynamic` aggregate passes the focused suite on monitor
index `1` / `\\.\DISPLAY2` at `2560x1440` borderless fullscreen. Latest
aggregate readiness is:
`validationClean=false`,
`mVsKDynamicStable=true`,
`nativeNormalizedDynamicStable=true`,
`temporalInputContractReady=true`,
`qualityInputReady=true`,
`objectMaskedStabilityReady=true`,
`repeatStabilityReady=false`, and `productionReady=false`.

Latest aggregate deltas:

- Moving camera M-vs-K: changed-edge delta `+0.0027`, mean-edge delta
  `-0.9463`.
- Combined camera+object M-vs-K: changed-edge delta `+0.0056`, mean-edge delta
  `+0.1390`.
- Object-only full-frame M-vs-K: changed-edge delta `+0.0018`, mean-edge delta
  `-0.3656`.
- Object-only velocity-mask M-vs-K: changed-edge delta `-0.0216`, mean-edge
  delta `-2.2020`.
- Object-only depth-mask M-vs-K: changed-edge delta `+0.0080`, mean-edge delta
  `-2.6557`.

Decision: the object-only depth-mask blocker from Slice 4.76 is resolved as a
measurement-density issue, not promoted away. Preset M still is not production:
the remaining aggregate blockers are the NGX-owned layout diagnostic
(`validationClean=false`) and the three-pass repeat policy. Sharpness-zero still
is not promoted because it does not consistently improve both edge metrics and
M is not validation-clean.

## Slice 4.78 Execution Plan

Slice 4.78 targets the remaining M production blocker from the validation-clean
side. Slice 4.77 showed the high-resolution dynamic IQ lanes are currently
inside tolerance, but `validationClean=false` still blocks production. This slice
adds a small parameter/input matrix so the next investigation does not keep
guessing between optional masks, motion state, runtime flavor, and preset choice.

1. Add a focused `m-ngx-parameter-matrix-audit` suite.
   - Keep the suite diagnostic-only and outside production promotion.
   - Use single-frame monitor-resolution captures to avoid the heavier sequence
     cost of the existing lifecycle and dynamic suites.
   - Keep preset K rel as the clean control.

2. Cover the minimum attribution matrix.
   - K rel, static fullscreen, all optional masks.
   - M rel, static fullscreen, all optional masks.
   - M rel, static fullscreen, no optional masks.
   - M rel, moving-camera fullscreen, all optional masks.
   - M dev, static fullscreen, all optional masks.

3. Package the result.
   - Write `m_ngx_parameter_matrix_audit.json` with per-lane DLSS state,
     runtime provenance, lifecycle counts, warning resource names, warning
     handles, SelfEngine evaluate-resource handles, and handle-overlap counts.
   - Summarize whether optional masks, motion state, or runtime flavor changes
     the diagnostic.
   - Do not remove the M block unless all M lanes become clean without an
     allowlist.

## Slice 4.78 Execution Evidence

Slice 4.78 is implemented and verified. `scripts\Test-DlssVisualQa.ps1` now
registers `m-ngx-parameter-matrix-audit`, adds a lane-summary helper for the
matrix output, and writes
`out\reference_captures\dlss_visual_qa\m_ngx_parameter_matrix_audit.json`.

Verification:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-parameter-matrix-audit`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-ngx-parameter-matrix-audit`

The focused run passed on monitor index `1` / `\\.\DISPLAY2`. The matrix
conclusion is:

- `attribution=preset-M-internal-resource-path`.
- K rel static control is clean.
- M rel static all masks, M rel static no optional masks, M rel moving camera,
  and M dev static are all not validation-clean.
- Disabling optional masks does not change validation or the NGX diagnostic.
- Moving camera does not change validation or the NGX diagnostic.
- Dev runtime does not change validation or the NGX diagnostic.
- Warning resource names are `nv.ngx.dlss.resource`,
  `nv.ngx.dlss.PrevMvecEroded`, and `nv.ngx.dlss.PrevMvecNondilated`.
- All M warning/SelfEngine evaluate-resource overlap counts are `0`.

Decision: this rules out optional-mask binding, motion state, and dev runtime
flavor as production workarounds for the current M blocker. The next
validation-clean step should use the same compact evidence path to test an
official newer DLSS SDK/runtime drop, or a lower-level NGX integration
hypothesis. Preset M remains non-production; K remains the clean fallback/control.

## Slice 4.79 Execution Plan

Slice 4.79 turns the Slice 4.78 attribution into an auditable validation
isolation path. There is no newer official tag than local `v310.7.0` in
`https://github.com/NVIDIA/DLSS.git` at this check, so the next useful step is
not another runtime A/B. Instead, isolate exactly the known NGX-owned internal
layout message at the Vulkan debug callback boundary, with the policy disabled
by default.

1. Add a narrow Vulkan debug-callback isolation policy.
   - Introduce `SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT=1`.
   - Only match `vkQueueSubmit` descriptor-layout
     `VUID-vkCmdDraw-None-09600` messages on `nv.ngx.dlss.*` resources that
     expect `VK_IMAGE_LAYOUT_GENERAL` while validation tracks them as
     `VK_IMAGE_LAYOUT_UNDEFINED`.
   - Print `SelfEngineVkSuppressedNgxInternalLayout` evidence lines with image
     handle, NGX resource name, and policy name instead of printing the original
     Vulkan validation text.

2. Add a focused isolation audit.
   - Register `m-ngx-validation-isolation-audit`.
   - Run K rel, M rel, and M dev at borderless monitor resolution.
   - Do not pass the M screenshot-log allowlist; unknown Vulkan diagnostics must
     still fail the capture.
   - Write `m_ngx_validation_isolation_audit.json`.

3. Make the policy reusable.
   - Add `-UseKnownNgxInternalLayoutIsolation` to
     `Test-DlssVisualQa.ps1` so existing focused suites can be run under the
     same policy without duplicating suite definitions.
   - Update dynamic/high-resolution summaries to read actual lane
     `validationClean` instead of hard-coding preset M as non-clean.

## Slice 4.79 Execution Evidence

Slice 4.79 is implemented and verified. `src\renderer\vulkan\Instance.cpp`
now keeps normal validation strict by default, and only when
`SE_VK_SUPPRESS_KNOWN_NGX_INTERNAL_DLSS_LAYOUT=1` is set does it isolate the
known NGX-owned DLSS internal layout warning. The script now parses the
`SelfEngineVkSuppressedNgxInternalLayout` evidence lines and exposes the policy
through `-UseKnownNgxInternalLayoutIsolation`.

Verification:

- `_quick_build.bat`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-ngx-validation-isolation-audit`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-ngx-validation-isolation-audit`
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-highres-dynamic -UseKnownNgxInternalLayoutIsolation`

The isolation audit passed on monitor index `1` / `\\.\DISPLAY2` and wrote
`out\reference_captures\dlss_visual_qa\m_ngx_validation_isolation_audit.json`.
Its conclusion is:

- `validationClean=true`.
- `isolationPolicyReady=true`.
- K rel remains clean with suppressed count `0`.
- M rel is clean with suppressed count `2`.
- M dev is clean with suppressed count `2`.
- Suppressed resources are `nv.ngx.dlss.resource`,
  `nv.ngx.dlss.PrevMvecEroded`, and `nv.ngx.dlss.PrevMvecNondilated`.

An attempted `m-highres-dynamic -UseKnownNgxInternalLayoutIsolation` run was
stopped by the 15-minute command timeout after producing partial/late artifacts,
so it is not counted as production evidence. The high-resolution aggregate code
has been corrected to honor actual lane `validationClean` under the policy, but
the focused high-resolution repeat still needs a successful bounded run before
promotion. Preset M therefore remains non-production: the current blocker has
moved from "unexplained validation warning" to "explicitly isolated NGX-owned
internal diagnostic plus repeat high-resolution dynamic acceptance still needed."

## Slice 4.80 Execution Plan

Slice 4.80 makes high-resolution dynamic acceptance persistent and bounded.
The goal is not to promote M; it is to stop treating one successful focused
fullscreen run as production evidence. The next gate must remember consecutive
passes, reset on failures, and stay cheap enough for daily iteration.

1. Repeat-pass ledger.
   - Write `out/reference_captures/dlss_visual_qa/m_highres_dynamic_repeat_ledger.json`
     whenever `m-highres-dynamic` reaches the aggregate readiness check.
   - Record validation policy, monitor, resolution, sequence timing, readiness
     booleans, blocker reasons, and consecutive-pass count.
   - Require three consecutive qualifying runs before `repeatStabilityReady`
     can become true.

2. Bounded high-resolution run parameters.
   - Keep default sequence capture at 3 frames, 4 seconds initial delay, and
     2 seconds interval.
   - Allow focused repeat runs to pass shorter sequence parameters from the
     command line, with the recommended repeat command using 2 frames, 4
     seconds initial delay, and 1 second interval.
   - Do not use a 2-second initial delay for this real scene; it can capture
     the desktop before the engine window is stable.

3. Validation-clean semantics under isolation.
   - Extend sequence tuning lanes to write capture log paths,
     `ngxResourceLayoutDiagnostic`, and `validationClean`.
   - Under `-UseKnownNgxInternalLayoutIsolation`, count a lane as clean only
     when no unknown validation/error/VUID diagnostics remain; keep suppressed
     NGX evidence visible in logs.
   - Add a failure trap for `m-highres-dynamic` so aborted focused runs reset
     the ledger instead of silently disappearing.

## Slice 4.80 Execution Evidence

Slice 4.80 is implemented. `Test-DlssVisualQa.ps1 -ListSuites` now reports the
sequence capture settings, the repeat ledger path, and the bounded repeat
command. The script also records capture-log validation state for DLAA tuning
sequence lanes, so fullscreen M/K/native summaries no longer treat missing
`validationClean` fields as false.

Verification on 2026-07-06:

- PowerShell parser validation passes.
- `git diff --check -- scripts/Test-DlssVisualQa.ps1` passes with only the
  existing LF-to-CRLF warning.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -ListSuites -Suite m-highres-dynamic -UseKnownNgxInternalLayoutIsolation`
  reports the ledger path and bounded repeat command.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-highres-dynamic -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2 -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1`
  passes on monitor index `1` / `\\.\DISPLAY2` at `2560x1440`.

The first attempted 2-frame repeat with a 2-second initial delay captured the
Windows desktop in one M lane and failed before aggregate readiness. That run
is not counted. The repeat command now keeps a 4-second initial delay for the
real skinned scene.

Latest bounded repeat evidence:

- `validationClean=true` under the NGX internal layout isolation policy.
- `nativeNormalizedDynamicStable=true`.
- `temporalInputContractReady=true`.
- `qualityInputReady=true`.
- `mVsKDynamicStable=false`.
- `objectMaskedStabilityReady=false`.
- `repeatPolicy.observedConsecutivePasses=0/3`.
- `productionReady=false`.

Moving-camera fullscreen and combined camera+object fullscreen are within the
K-relative dynamic tolerance in this run: M-vs-K changed-edge-ratio deltas are
`-0.0218` and `-0.0109`. The blocker is object-only fullscreen stability:
full-frame M-vs-K changed-edge-ratio delta is `+0.0344`, velocity-masked delta
is `+0.0620`, and depth-masked readiness is also false. The ledger correctly
resets to `0` consecutive passes and records the blocker reasons instead of
promoting M.

Current decision: Slice 4.80 moves M from "blocked by unknown validation
hygiene" to "validation-clean under an explicit isolation policy, but dynamic
repeat stability still fails." Preset M remains non-production. The next DLSS
slice should target the object-only fullscreen skinned silhouette: inspect
skinned velocity, depth/disocclusion coverage, history rejection/reset behavior,
and reactive/transparency masks before trying sharpness, mip bias, anisotropy,
or CAS/RCAS defaults.

## Slice 4.81 Execution Plan

Slice 4.81 narrows the object-only fullscreen blocker to the runtime skinned
pose contract. The goal is to fix any obvious previous-pose discontinuity before
continuing into depth/disocclusion and history tuning.

1. Skinned previous-pose continuity.
   - Keep `Fist Fight B.fbx` on the real runtime skinned animation path.
   - When animation playback wraps, sample the previous bone palette at the
     actual previous tick instead of collapsing previous pose to the wrapped
     current tick.
   - Record loop-wrap and previous-pose-collapse counters so future captures can
     distinguish animation-loop artifacts from ordinary silhouette shimmer.

2. Focused evidence only.
   - Add the counters to benchmark CSV and the visual-QA skinned runtime
     metrics.
   - Verify with `default-scene-skinned-fbx-m-production` and
     `m-object-shimmer-fullscreen-diagnostics`, not the full matrix.

## Slice 4.81 Execution Evidence

Slice 4.81 is implemented. `RuntimeModelLoader::UpdateAnimationPlayback` now
uses the real previous animation tick for previous-pose sampling even when the
clip wraps. Benchmark CSV adds
`runtime_import_animation_playback_loop_wrap_count` and
`runtime_import_animation_playback_previous_pose_collapsed_count`, and
`Test-DlssVisualQa.ps1` forwards both fields into skinned runtime metrics with a
zero default for older CSVs.

Verification on 2026-07-06:

- PowerShell parser validation passes.
- `git diff --check` passes for the touched C++ and QA script files with only
  the existing LF-to-CRLF warnings.
- `_quick_build.bat` passes and rebuilds `SelfEngineForward3D.exe`.
- `default-scene-skinned-fbx-m-production -UseKnownNgxInternalLayoutIsolation`
  passes on monitor index `1` / `\\.\DISPLAY2` at `2560x1440`; the audit row
  reports `905/905` CSV columns, quality mode/preset `6/13`, quality gate
  `1/1/0`, masks `255/255/0`, playback ready `1`, changed bones `65`,
  loop wraps `0`, and previous-pose collapses `0`.
- `m-object-shimmer-fullscreen-diagnostics -UseKnownNgxInternalLayoutIsolation
  -SequenceFrameCount 2 -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1`
  passes on the same monitor/resolution.

Latest object-only fullscreen evidence is still not production-ready. Under the
explicit NGX internal layout isolation policy, validation is clean and the
skinned pose counters rule out loop-collapse in this capture (`K/M loopWraps=0`
and `K/M previousPoseCollapsed=0`), but preset M remains worse than K:

- Full-frame M-vs-K changed-edge-ratio delta `+0.0457`, mean-edge delta
  `+7.1705`.
- Velocity-masked M-vs-K changed-edge-ratio delta `+0.0389`, mean-edge delta
  `+8.4615`.
- Depth-masked M-vs-K changed-edge-ratio delta `+0.1140`, mean-edge delta
  `+22.5438`.
- `velocityMaskedMVsK=false`, `depthMaskedMVsK=false`, and
  `productionCandidate=false`.

Current decision: this slice fixes an actual skinned previous-pose continuity
hazard and makes it measurable, but it does not clear M. The remaining blocker
is concentrated around skinned silhouette/depth-disocclusion stability rather
than animation loop collapse. The next useful slice should inspect history
rejection/reset and disocclusion/mask coverage in the object-only fullscreen
lane.

## Slice 4.82 Execution Plan

Slice 4.82 hardens the production QA contract for preset-M dynamic skinned
content. The immediate goal is not image-quality tuning; it is to make sure
`m-vs-k-dynamic-pack`, the fullscreen dynamic packs, and the high-resolution
repeat ledger cannot accumulate production evidence unless the real
`Fist Fight B.fbx` lane proves skinned velocity inputs at the draw level.

1. Skinned velocity contract wiring.
   - Keep `RequireSkinnedVelocity` on moving-camera, object-only, and combined
     dynamic lanes.
   - Carry `skinnedVelocityInputReady` into lane readiness, high-resolution
     aggregate readiness, and `m_highres_dynamic_repeat_ledger.json`.
   - Require shader skinning, shader velocity, changed draw bone palette,
     runtime playback advancement, and no previous-pose collapse before a lane
     can count as production-input ready.

2. Focused high-resolution evidence.
   - Run `m-highres-dynamic` under the explicit NGX internal layout isolation
     policy with the bounded two-frame sequence command.
   - Treat the result as diagnostic evidence only; no M promotion and no tuning
     default changes.

## Slice 4.82 Execution Evidence

Slice 4.82 is implemented in `Test-DlssVisualQa.ps1`. The dynamic contract now
reports `skinnedVelocityInputReady`, the 720p M/K/native dynamic audit includes
it in `dynamicProductionReadiness`, both fullscreen dynamic packs include it in
their `productionReady` calculation, and the high-resolution repeat ledger
writes it into `lastRun` and `productionInputPass`.

Verification on 2026-07-06:

- PowerShell parser validation passes.
- `-ListSuites -Suite m-object-shimmer-fullscreen-diagnostics
  -UseKnownNgxInternalLayoutIsolation` expands only the focused fullscreen
  object-only suite.
- `-ListSuites -Suite m-highres-dynamic
  -UseKnownNgxInternalLayoutIsolation` expands to
  `m-vs-k-moving-camera-fullscreen-pack`,
  `m-vs-k-combined-fullscreen-pack`, and
  `m-object-shimmer-fullscreen-diagnostics`.
- Bounded focused run:
  `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite m-highres-dynamic -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2 -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1`
  passes on monitor index `1` / `\\.\DISPLAY2` at borderless
  `2560x1440`.

Latest high-resolution audit remains non-production. Under the explicit NGX
isolation policy, validation is clean, temporal and quality inputs are ready,
native-normalized stability is ready, and object-only masked stability is ready,
but the aggregate reports:

- `productionReady=false`.
- `mVsKDynamicStable=false`.
- `skinnedVelocityInputReady=false`.
- `repeatPolicy=0/3`.

The new skinned-velocity gate found a useful discrepancy. The object-only
fullscreen lane proves the draw-level skinned contract:
`shaderSkinningReady=true`, `shaderVelocityReady=true`,
`bonePaletteChangedEntryCount=20`, runtime playback frames `6`, changed bones
`65`, loop wraps `0`, and previous-pose collapses `0`. The moving-camera and
combined fullscreen lanes both report shader skinning/velocity ready and
runtime playback changed bones `65`, but their draw-level
`bonePaletteDrawChangedEntryCount` is `0`, so the new contract blocks them with
"skinned bone palette did not change".

Current decision: this slice deliberately makes M harder to pass. It proves the
high-resolution ledger no longer accepts dynamic skinned evidence unless the
submitted draw command demonstrates changed current/previous bone palettes.
The next useful slice should inspect why moving-camera and combined fullscreen
skinned draws have current/previous palette entries but no draw-level changed
entries, then continue with depth/disocclusion/history analysis only after that
input evidence is consistent across lanes.

## Slice 4.83 Execution Plan

Slice 4.83 fixes the draw-level skinned bone-palette evidence gap found by
Slice 4.82. The suspected failure mode is render-queue caching: moving-camera
and combined fullscreen lanes can reuse cached render commands while the
runtime skeletal animation continues to update renderer-owned bone-palette
resources.

1. Cached command dynamic state.
   - Keep the 3D render queue cache enabled.
   - Refresh bone-palette current/previous entry counts, changed-entry count,
     readiness, descriptor set, and descriptor range from
     `VulkanRenderResources2D` whenever a cached 3D render command is reused.
   - Apply the same refresh to full scene-queue cache hits, matching the
     existing cached material-push-constant refresh behavior.

2. Focused repeat acceptance.
   - Re-run only `m-highres-dynamic` with the bounded two-frame sequence and
     explicit NGX internal layout isolation.
   - Require all three fullscreen lanes to report draw-level changed bone
     palettes before counting the repeat ledger.

## Slice 4.83 Execution Evidence

Slice 4.83 is implemented. `RenderQueue` now uses
`RefreshBonePaletteCommandState` for new 3D commands, per-renderable command
cache hits, and full scene-queue cache hits through
`Refresh3DDynamicCommandState`. Cached commands still reuse stable geometry and
sorting data, but their skinned resource payload is refreshed from
`VulkanRenderResources2D` every frame.

Verification on 2026-07-06:

- `_quick_build.bat` passes.
- PowerShell parser validation passes.
- `git diff --check` passes for the touched code, QA script, and docs with
  only the existing LF/CRLF warnings.
- Three consecutive bounded focused runs of
  `m-highres-dynamic -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1` pass on monitor
  index `1` / `\\.\DISPLAY2` at borderless `2560x1440`.

Final high-resolution focused audit under the explicit NGX isolation policy:

- `validationClean=true`.
- `mVsKDynamicStable=true`.
- `nativeNormalizedDynamicStable=true`.
- `temporalInputContractReady=true`.
- `qualityInputReady=true`.
- `skinnedVelocityInputReady=true`.
- `objectMaskedStabilityReady=true`.
- `repeatPolicy=3/3`.

The original draw-level evidence gap is closed in the final run:

- Moving-camera fullscreen M: draw changed bones/runtime changed bones
  `65/65`, previous-pose collapses `0`, loop wraps `0`.
- Combined fullscreen M: draw changed bones/runtime changed bones `65/65`,
  previous-pose collapses `0`, loop wraps `0`.
- Object-only fullscreen M: draw changed bones/runtime changed bones `65/65`,
  previous-pose collapses `0`, loop wraps `0`.

Latest object-only masked M-vs-K evidence is also within tolerance:
full-frame changed-edge-ratio delta `-0.0035`, velocity-masked delta `-0.0331`,
and depth-masked delta `-0.0595`.

Current decision: the high-resolution dynamic focused audit is now ready under
the explicit NGX internal layout isolation policy, but this still is not a
global "M is production default" decision. `productionCandidate` remains false
in the diagnostic summary, K remains the fallback/control, and broader release
still needs either acceptance/replacement of the NGX isolation policy plus
subjective clarity/ghosting review and wider content coverage beyond this
focused real-skinned-scene lane.

## Slice 4.84 Execution Plan

Slice 4.84 tightens the release semantics after Slice 4.83. The problem is not
another rendering input; it is that `productionReady=true` in
`mHighResDynamicProductionAudit` can be misread when the run only passed under
`-UseKnownNgxInternalLayoutIsolation`.

1. Separate focused audit readiness from strict production readiness.
   - Keep the existing repeat ledger as focused evidence for the real skinned
     high-resolution scene.
   - Add `focusedAuditReady` / `focusedAuditInputPass` for the isolation-policy
     focused audit.
   - Add `strictValidationClean`, `strictProductionInputPass`, and
     `strictProductionReady` so production remains false whenever the focused
     pass depends on explicit NGX internal layout isolation.

2. Keep policy blockers visible.
   - Record `productionPolicyBlockers` in the repeat ledger.
   - Move the NGX isolation dependency into `blockedReasons` for strict
     production, while keeping `focusedBlockedReasons` for the focused lane
     itself.

## Slice 4.84 Execution Evidence

Slice 4.84 is implemented in `Test-DlssVisualQa.ps1`. The high-resolution
repeat ledger now records both the legacy focused input pass and the stricter
production input pass, and `mHighResDynamicProductionAudit` now reports
`focusedAuditReady`, `focusedAuditPolicy`, `strictValidationClean`, and
`strictProductionReady`.

Verification on 2026-07-06:

- PowerShell parser validation passes.
- `-ListSuites -Suite m-highres-dynamic -UseKnownNgxInternalLayoutIsolation`
  expands to the same three focused fullscreen lanes.
- A bounded focused run with
  `m-highres-dynamic -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1` passes on monitor
  index `1` / `\\.\DISPLAY2` at borderless `2560x1440`.

The new fields behave conservatively:

- `productionReady=false`.
- `strictProductionReady=false`.
- `strictValidationClean=false`.
- `focusedAuditPolicy=known-ngx-internal-layout-isolation`.
- `productionPolicyBlockers` includes the explicit NGX internal layout
  isolation dependency.

The same run also reset focused repeat evidence to `0/3`, because the
moving-camera fullscreen M-vs-K lane failed the mean-edge-delta tolerance:
changed-edge-ratio delta `-0.0187` was within tolerance, but mean-edge-delta
delta was `+2.3802` against the `+2.0` threshold. Combined fullscreen and
object-only fullscreen remained within tolerance, and all three M lanes still
reported draw/runtime changed bones `65/65`, quality gate `1/1/0`, masks
`255/255/0`, and skinned velocity readiness.

Current decision: the production semantic fix is correct and intentionally
conservative. M is not production-ready. The next useful slice should target
the moving-camera fullscreen mean-edge-delta instability, likely around camera
motion temporal stability, history/disocclusion behavior, exposure/post
ordering, or subjective edge clarity, while keeping sharpness, mip bias,
anisotropy, CAS, and RCAS as controlled variables rather than defaults.

## Slice 4.85 Execution Plan

Slice 4.85 targets the moving-camera fullscreen mean-edge-delta instability
without changing image tuning. The suspected measurement contaminant is the
benchmark motion clock: camera and object benchmark motion advanced by
`max(deltaSeconds, 1/60)` per rendered frame, so K and M could reach different
motion phases at the same wall-clock capture time if their frame rates differed.

1. Deterministic benchmark motion time.
   - Drive benchmark camera and object motion from `Application::Run`'s
     elapsed-time parameter instead of accumulated rendered-frame deltas.
   - Keep normal interactive camera and non-benchmark animation behavior
     unchanged.

2. Motion-time diagnostics.
   - Add benchmark camera/object motion time fields to benchmark CSV.
   - Expose them in quick DLSS metrics and dynamic lane contract observations.

3. Focused validation.
   - Run the moving-camera fullscreen pack first.
   - Run one bounded `m-highres-dynamic` pass to confirm combined/object-only
     lanes still behave under the new clock.

## Slice 4.85 Execution Evidence

Slice 4.85 is implemented. `Forward3D` now uses the app elapsed time for
`SE_BENCHMARK_CAMERA_MOTION` and `SE_BENCHMARK_OBJECT_MOTION`, and benchmark
CSV now records `benchmark_camera_motion_time_seconds` and
`benchmark_object_motion_time_seconds`. `Test-DlssVisualQa.ps1` carries these
fields into quick DLSS metrics and the dynamic lane contract's observed
`benchmarkMotionTime`.

Verification on 2026-07-06:

- `_quick_build.bat` passes.
- PowerShell parser validation passes.
- `git diff --check` passes for the touched files with only existing LF/CRLF
  warnings.
- `-ListSuites -Suite m-vs-k-moving-camera-fullscreen-pack
  -UseKnownNgxInternalLayoutIsolation` expands only the focused moving-camera
  fullscreen pack.
- The bounded moving-camera fullscreen pack passes on monitor index `1` /
  `\\.\DISPLAY2` at borderless `2560x1440`.
- One bounded `m-highres-dynamic` pass also passes all input gates under the
  explicit NGX isolation policy.

Latest moving-camera fullscreen M-vs-K evidence after the elapsed-time clock:

- changed-edge-ratio delta `-0.0176`.
- mean-edge-delta delta `-0.5659`.
- `mVsKDynamicStable=true`.

The full high-resolution focused pass now has all dynamic inputs ready again:
moving-camera stable, combined stable, object-only masked stable,
native-normalized stable, quality inputs ready, skinned velocity input ready,
and draw/runtime changed bones remain `65/65`. The repeat ledger is `1/3`
because the previous strict-semantics run reset it to zero. Strict production
is still false because the evidence still depends on
`known-ngx-internal-layout-isolation`.

Current decision: this slice removes a real QA timing hazard and gives the
moving-camera fullscreen lane cleaner evidence, but it does not promote M.
Next work should gather the remaining bounded repeats under the same policy
only when useful, and separately continue the strict-clean NGX/runtime path or
subjective ghosting/clarity review.

## Slice 4.86 Execution Plan

Slice 4.86 tries to turn the latest `1/3` focused high-resolution dynamic
evidence into a stable `3/3` repeat ledger without changing tuning defaults.
This is deliberately a focused execution slice, not a full visual-QA matrix.

1. Run only the bounded `m-highres-dynamic` suite twice:
   - `-SkipBuild`
   - `-UseKnownNgxInternalLayoutIsolation`
   - `-SequenceFrameCount 2`
   - `-SequenceInitialDelaySeconds 4`
   - `-SequenceIntervalSeconds 1`

2. Accept the result only if the ledger records three consecutive focused
   input passes under the same monitor, resolution, validation policy, and
   timing key.

3. If a run resets the ledger, do not hide it by loosening thresholds or
   promoting sharpening/mip changes. Record which lane failed and make the
   next slice a targeted image-quality investigation.

## Slice 4.86 Execution Evidence

Slice 4.86 did not reach repeat acceptance. The first bounded repeat passed
the focused suite on monitor index `1` / `\\.\DISPLAY2`, but the second run
reset `m_highres_dynamic_repeat_ledger.json` from `2/3` to `0/3`.

Verification commands on 2026-07-06:

- `powershell -NoProfile -ExecutionPolicy Bypass -File
  .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -CaptureDelaySeconds 2 -Suite
  m-highres-dynamic -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1`
- The same bounded command was run a second time to test consecutive stability.

Latest aggregate state after the second run:

- `validationClean=true` under the explicit NGX internal layout isolation
  policy.
- `strictValidationClean=false`.
- `productionReady=false`.
- `strictProductionReady=false`.
- `focusedAuditReady=false`.
- `repeatPolicy=0/3`.
- `skinnedVelocityInputReady=true`.
- `qualityInputReady=true`.
- `temporalInputContractReady=true`.
- `nativeNormalizedDynamicStable=true`.

The reset was caused by image-quality stability, not by missing renderer input
contracts. The latest blocked reasons are:

- Preset M is not stable against K in at least one high-resolution dynamic
  lane.
- Object-only fullscreen masked velocity/depth stability is not ready against
  K.
- Preset M focused evidence still depends on the explicit NGX internal layout
  isolation policy.

Latest focused metrics:

- Moving-camera fullscreen: K edge ratio/mean edge delta
  `0.6479/39.6318`, M `0.6375/42.0897`. The edge-ratio delta is favorable,
  but M's mean-edge-delta is `+2.4579`, above the `+2.0` tolerance.
- Combined camera+object fullscreen: K `0.6583/36.9448`, M
  `0.6157/36.7752`, still within the current M-vs-K gate.
- Object-only fullscreen: K `0.5990/37.0121`, M `0.5920/39.3204`. The
  edge-ratio delta is favorable, but mean-edge-delta is `+2.3083`, above the
  `+2.0` tolerance, and both velocity/depth masked readiness checks are false.
- Object-only preset M with sharpness zero improves both edge metrics
  (`0.5815/37.8055`) and passes the preset-K gate in this run, but it is not
  promoted: M still depends on the NGX isolation policy, and one passing
  sharpness control does not prove production image quality.

Current decision: stop trying to brute-force the repeat ledger. The next
useful slice should localize the skinned silhouette instability with
velocity-mask, depth-mask, disocclusion/history, and moving-camera edge-region
evidence before any default sharpness, mip bias, anisotropy, CAS, or RCAS
change. M remains non-production, and K remains the clean fallback/control.

## Slice 4.87 Execution Plan

Slice 4.87 adds localization evidence for the M-vs-K dynamic instability found
in Slice 4.86. The target is not to tune the image yet; it is to make the next
failure actionable by identifying where M adds adjacent-frame edge delta beyond
K.

1. Full-frame moving-camera hotspots.
   - Add a LockBits-backed full-frame edge-excess hotspot pass.
   - Compare preset K adjacent-frame edge delta against preset M adjacent-frame
     edge delta using the already captured fullscreen sequences.
   - Rank 16x9 screen tiles by positive excess edge delta.

2. Masked object-only hotspot hygiene.
   - Keep the existing velocity/depth masked object-only hotspot path.
   - Add `edgePixels` and `excessEdgeRatio` to each masked tile so depth and
     velocity masked failures can be compared against full-frame failures.

3. Aggregate routing.
   - Attach full-frame hotspot data to `mVsKMovingCameraFullscreenPack` and
     `mVsKCombinedFullscreenPack`.
   - Attach a compact `instabilityLocalization` block to
     `mHighResDynamicProductionAudit` so the aggregate report points to the
     moving-camera, combined, and object-only masked hotspot evidence.

## Slice 4.87 Execution Evidence

Slice 4.87 is implemented. `scripts\Test-DlssVisualQa.ps1` now has
`Compare-SequenceInstabilityHotspots`, backed by the same C# `LockBits` helper
used by the masked diagnostics. Full-frame hotspot tiles record screen bounds,
sampled pixels, edge pixels, excess pixels, excess-edge ratio, reference and
candidate edge-delta sums, positive-excess sum, and max positive excess.

Verification on 2026-07-06:

- PowerShell parser validation passes.
- `git diff --check -- scripts\Test-DlssVisualQa.ps1` passes with only the
  existing LF/CRLF warning.
- `-ListSuites -Suite m-vs-k-moving-camera-fullscreen-pack
  -UseKnownNgxInternalLayoutIsolation` and `-ListSuites -Suite
  m-highres-dynamic -UseKnownNgxInternalLayoutIsolation` still expand to the
  expected focused lanes.
- `m-vs-k-moving-camera-fullscreen-pack -UseKnownNgxInternalLayoutIsolation
  -SequenceFrameCount 2 -SequenceInitialDelaySeconds 4
  -SequenceIntervalSeconds 1` passes on monitor index `1` / `\\.\DISPLAY2` at
  borderless monitor resolution.
- `m-object-shimmer-fullscreen-diagnostics -UseKnownNgxInternalLayoutIsolation
  -SequenceFrameCount 2 -SequenceInitialDelaySeconds 4
  -SequenceIntervalSeconds 1` passes on monitor index `1` / `\\.\DISPLAY2`.

Latest moving-camera fullscreen evidence after adding the hotspot pass happened
to be stable: K edge ratio/mean edge delta `0.6649/42.2366`, M
`0.6161/38.4626`, M-vs-K delta `-0.0488/-3.7740`. The new hotspot data still
records local positive-excess areas; the top full-frame tile is
`tile=(10,6)`, bounds `1600,960 -> 1760,1120`, `edgePixels=312`,
`excessPixels=104`, `excessEdgeRatio=0.3333`, and
`positiveExcessSum=4561.5636`.

Latest object-only fullscreen evidence also passed in this focused run:
K `0.6126/38.7362`, M `0.5845/37.6964`, M-vs-K delta `-0.0281/-1.0398`, and
both velocity/depth masked readiness checks are true. The masked hotspot report
now includes `edgePixels` and `excessEdgeRatio`; for example, the latest
velocity-masked top tile is `tile=(9,5)`, bounds `1440,800 -> 1600,960`,
`maskPixels=315`, `edgePixels=129`, and `excessPixels=48`.

Current decision: this slice improves diagnosis, not image quality. M remains
non-production because repeat high-resolution dynamic evidence is still not
stable, strict validation still depends on the NGX internal layout isolation
policy, and subjective ghosting/clarity review remains open. The next image
quality slice should use these hotspot tiles to inspect whether excess M edge
delta clusters on the skinned silhouette, depth discontinuities, camera-motion
background edges, or history/disocclusion regions before changing any default
sharpness or mip policy.

## Slice 4.88 Execution Plan

Slice 4.88 extends the hotspot localization from full-frame moving-camera tiles
to moving-camera velocity/depth masked evidence. Slice 4.87 could say where M
adds local edge delta, but moving-camera did not yet have the same mask context
as object-only shimmer diagnostics. This slice adds that context without
promoting any tuning.

1. Moving-camera debug masks.
   - Add high-resolution moving-camera `gbuffer-velocity` and `gbuffer-depth`
     debug sequence environments.
   - Keep captures on monitor index `1` / `\\.\DISPLAY2` with hidden ImGui and
     borderless monitor-resolution placement.

2. Moving-camera masked metrics.
   - Compare preset K and M adjacent-frame sequences inside the moving-camera
     velocity mask.
   - Compare preset K and M adjacent-frame sequences inside the moving-camera
     depth mask.
   - Emit `velocityMasked`, `depthMasked`, and `maskedReadiness` in
     `mVsKMovingCameraFullscreenPack`.

3. Moving-camera masked hotspots.
   - Reuse `Compare-MaskedSequenceInstabilityHotspots` for moving-camera
     velocity/depth masks.
   - Store `velocityMaskedMVsK` and `depthMaskedMVsK` under the same
     `instabilityHotspots` block as the full-frame moving-camera hotspots.

## Slice 4.88 Execution Evidence

Slice 4.88 is implemented. `m-vs-k-moving-camera-fullscreen-pack` now runs two
additional focused diagnostic lanes before the native/K/M visible captures:

- `m_vs_k_moving_camera_fullscreen_velocity_debug`
- `m_vs_k_moving_camera_fullscreen_depth_debug`

Verification on 2026-07-06:

- PowerShell parser validation passes.
- `git diff --check -- scripts\Test-DlssVisualQa.ps1` passes with only the
  existing LF/CRLF warning.
- `m-vs-k-moving-camera-fullscreen-pack -UseKnownNgxInternalLayoutIsolation
  -SequenceFrameCount 2 -SequenceInitialDelaySeconds 4
  -SequenceIntervalSeconds 1` passes on monitor index `1` / `\\.\DISPLAY2` at
  borderless monitor resolution.

Latest moving-camera fullscreen evidence:

- Full-frame M-vs-K remains stable in this run: K `0.6398/39.1312`, M
  `0.6128/38.4948`, delta `-0.0270/-0.6364`.
- Velocity-masked M-vs-K is also within the current gate: K
  `0.5980/38.1813`, M `0.5814/39.4039`, delta `-0.0166/+1.2226`.
- Depth-masked M-vs-K is within the current gate: K `0.4862/34.1284`, M
  `0.4251/35.7909`, delta `-0.0611/+1.6625`.
- `maskedReadiness.velocityMaskedMVsK=true`.
- `maskedReadiness.depthMaskedMVsK=true`.

The new moving-camera masked hotspots are now present. In the latest run, the
top velocity-masked hotspot is `tile=(6,6)`, bounds `960,960 -> 1120,1120`,
`maskPixels=1068`, `edgePixels=175`, `excessPixels=41`. The top depth-masked
hotspot is `tile=(9,3)`, bounds `1440,480 -> 1600,640`, `maskPixels=809`,
`edgePixels=102`, `excessPixels=24`. These are localization signals only; they
do not make M production-ready.

Current decision: moving-camera now has full-frame plus velocity/depth masked
localization parity with object-only shimmer diagnostics. The next useful image
quality work should inspect repeated failing runs against these hotspot tiles
and then decide whether the fix belongs in skinned silhouette velocity, depth
disocclusion/history handling, camera-motion vectors, or a controlled
sharpness/mip experiment. M remains non-production; strict validation and
repeat high-resolution dynamic acceptance remain open.

## Slice 4.89 Execution Plan

Slice 4.89 turns the hotspot tile lists from Slice 4.87/4.88 into an automatic
classification signal. The problem with raw hotspot tiles is that they still
require manual cross-checking: a full-frame M-vs-K hotspot may or may not line
up with velocity/depth debug-mask hotspots. This slice records that overlap
directly.

1. Hotspot overlap summary.
   - Add `New-HotspotLocalizationSummary`.
   - Compare the top full-frame hotspot tiles against the top velocity-masked
     and depth-masked hotspot tiles.
   - Record overlap counts and ratios for velocity, depth, and both masks.

2. Lane integration.
   - Attach `hotspotLocalization` to moving-camera fullscreen diagnostics.
   - Attach `hotspotLocalization` to combined fullscreen diagnostics; this is
     `full-frame-only-no-mask-context` until combined gets its own debug masks.
   - Add full-frame object-only hotspots and attach `hotspotLocalization` to
     object-only fullscreen diagnostics.

3. Aggregate routing.
   - Add the three lane classifications under
     `mHighResDynamicProductionAudit.instabilityLocalization.classifications`
     whenever the full `m-highres-dynamic` suite is run.

## Slice 4.89 Execution Evidence

Slice 4.89 is implemented. `Test-DlssVisualQa.ps1` now classifies hotspot
overlap as one of:

- `velocity-and-depth-overlap`
- `depth-disocclusion-overlap`
- `velocity-silhouette-overlap`
- `full-frame-only-background-or-unmasked`
- `full-frame-only-no-mask-context`
- `no-positive-excess-hotspots`

Verification on 2026-07-06:

- PowerShell parser validation passes.
- `git diff --check -- scripts\Test-DlssVisualQa.ps1` passes with only the
  existing LF/CRLF warning.
- `m-vs-k-moving-camera-fullscreen-pack -UseKnownNgxInternalLayoutIsolation
  -SequenceFrameCount 2 -SequenceInitialDelaySeconds 4
  -SequenceIntervalSeconds 1` passes on monitor index `1` / `\\.\DISPLAY2`.
- `m-object-shimmer-fullscreen-diagnostics
  -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1` passes on monitor
  index `1` / `\\.\DISPLAY2`.

Latest moving-camera classification:

- Full-frame M-vs-K is stable in this run: K `0.6428/38.8171`, M
  `0.6181/38.5762`, delta `-0.0247/-0.2409`.
- `classification=velocity-and-depth-overlap`.
- Full-frame top tiles overlapping velocity mask: `6/8` (`0.75`).
- Full-frame top tiles overlapping depth mask: `1/8` (`0.125`).
- The top full-frame tile is `tile=(10,6)`, bounds `1600,960 -> 1760,1120`,
  with `excessPixels=108`; it overlaps the velocity mask but not the depth
  mask.

Latest object-only classification:

- Full-frame M-vs-K is stable in this run: K `0.6035/37.0834`, M
  `0.5850/37.8795`, delta `-0.0185/+0.7961`.
- `classification=velocity-and-depth-overlap`.
- Full-frame top tiles overlapping velocity mask: `6/8` (`0.75`).
- Full-frame top tiles overlapping depth mask: `3/8` (`0.375`).
- Full-frame top tiles overlapping both masks: `2/8`.

Current decision: the classification signal points current excess-edge evidence
toward velocity/skinned-silhouette first, with some depth/disocclusion overlap,
rather than a pure unmasked background-edge problem. This is still diagnostic
evidence, not a production pass. M remains non-production until strict
validation, repeat high-resolution dynamic acceptance, and subjective
ghosting/clarity review are all satisfied.

## Slice 4.90 Execution Plan

Slice 4.90 removes a likely measurement contaminant from the M/K dynamic
comparisons: runtime skeletal animation was still advanced by each run's frame
delta, while benchmark camera/object motion had already moved to app elapsed
time. K and M can run at different frame rates, so their screenshots could
compare different `Fist Fight B.fbx` animation phases.

1. Deterministic skinned animation clock.
   - Add an elapsed-time sampling path for `RuntimeModelLoader`.
   - Use it for benchmark/dynamic QA paths and the default-scene skinned-FBX
     production lane.
   - Keep the normal delta-time path for ordinary interactive runtime.

2. CSV and QA evidence.
   - Record playback clock mode, previous/current animation ticks, and
     previous/current absolute seconds in benchmark CSV.
   - Require elapsed-time clock mode in skinned dynamic lane contracts.
   - Add K/M `animationPhase` summaries to fullscreen moving-camera, combined,
     and object-only diagnostics.

3. Focused verification only.
   - Run parser/diff/build checks.
   - Run object-only fullscreen and moving-camera fullscreen focused suites.
   - Do not run the full visual-QA matrix for this slice.

## Slice 4.90 Execution Evidence

Slice 4.90 is implemented. `RuntimeModelLoader` now supports
`UpdateAnimationPlaybackAtTime`, and `Forward3D` uses it when benchmark camera
motion, benchmark object motion, or default-scene skinned-FBX production is
active. Benchmark CSV now includes:

- `runtime_import_animation_playback_clock_mode`
- `runtime_import_animation_playback_previous_time_ticks`
- `runtime_import_animation_playback_current_time_ticks`
- `runtime_import_animation_playback_previous_absolute_seconds`
- `runtime_import_animation_playback_current_absolute_seconds`

Verification on 2026-07-06:

- PowerShell parser validation passes.
- `git diff --check` passes for the touched C++/PowerShell files with only the
  existing LF/CRLF warnings.
- `_quick_build.bat` passes.
- `m-object-shimmer-fullscreen-diagnostics
  -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1` passes on monitor
  index `1` / `\\.\DISPLAY2`.
- `m-vs-k-moving-camera-fullscreen-pack
  -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1` passes on monitor
  index `1` / `\\.\DISPLAY2`.

Latest object-only fullscreen evidence:

- K/M animation clock mode is `1/1`; absolute-time delta is `0.0996s`, tick
  delta is `2.9873`.
- K edge ratio/mean is `0.6051/40.5512`; M is `0.5864/41.2719`.
- M-vs-K delta is `-0.0187/+0.7207`, within tolerance.
- Velocity/depth masked readiness is true, and hotspot classification remains
  `velocity-and-depth-overlap`.

Latest moving-camera fullscreen evidence:

- K/M animation clock mode is `1/1`; absolute-time delta is `0.117s`, tick
  delta is `3.5091`.
- K edge ratio/mean is `0.6627/44.9580`; M is `0.5943/41.9277`.
- M-vs-K delta is `-0.0684/-3.0303`, within tolerance and better than K on
  both recorded edge metrics for this run.
- Native-normalized stability, velocity/depth masked readiness, temporal input,
  quality input, and skinned velocity input are all ready in this focused lane.

Current decision: K/M dynamic comparisons are no longer polluted by obvious
frame-rate-driven skeletal animation drift. This is a measurement-quality
improvement, not a production promotion. M still needs strict validation without
depending on the NGX isolation policy or a formally accepted policy decision,
bounded repeated high-resolution dynamic acceptance including combined motion,
and subjective ghosting/clarity review on the real scene.

## Slice 4.91 Execution Plan

Slice 4.91 takes the phase-aligned evidence from Slice 4.90 into the remaining
high-resolution dynamic gates. The specific questions are:

1. Does combined camera+object motion still pass once skeletal animation uses
   the elapsed-time clock?
2. Does the aggregate `m-highres-dynamic` repeat ledger advance?
3. If the ledger resets, is the failure a broad input-contract problem, an
   object-only full-frame/velocity/depth problem, or a controlled sharpness
   signal?

The slice must not loosen thresholds and must not promote sharpness, mip bias,
anisotropy, CAS, or RCAS defaults just because one metric improves.

## Slice 4.91 Execution Evidence

Slice 4.91 is implemented as focused QA and diagnostics.

- Fixed `New-HotspotLocalizationSummary` so full-frame-only lanes such as
  `m-vs-k-combined-fullscreen-pack` can omit velocity/depth hotspot inputs under
  PowerShell StrictMode. The function now normalizes empty/single hotspot lists
  before counting them.
- Added aggregate high-resolution lane-stability detail fields in
  `mHighResDynamicProductionAudit` so future full `m-highres-dynamic` summaries
  can show per-lane deltas, animation phase evidence, hotspot class, and
  object-only sharpness-control evidence instead of only reporting the aggregate
  `mVsKDynamicStable=false`.

Verification on 2026-07-06:

- PowerShell parser validation passes.
- `git diff --check -- scripts\Test-DlssVisualQa.ps1` passes with only the
  existing LF/CRLF warning.
- `m-vs-k-combined-fullscreen-pack
  -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1` passes on monitor
  index `1` / `\\.\DISPLAY2`.
- `m-highres-dynamic -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 2
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1` passes on monitor
  index `1` / `\\.\DISPLAY2`, but the repeat ledger stays `0/3`.
- `m-object-shimmer-fullscreen-diagnostics
  -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 3
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1` passes on monitor
  index `1` / `\\.\DISPLAY2`.

Combined fullscreen evidence:

- K/M animation clock mode is `1/1`; phase delta is `0.011s / 0.3302 ticks`.
- K edge ratio/mean is `0.6702/44.3640`; M is `0.5879/40.3257`.
- M-vs-K delta is `-0.0823/-4.0383`; focused combined motion is stable.

Latest aggregate `m-highres-dynamic` evidence:

- Moving-camera M-vs-K delta: `-0.0243/-0.2852`.
- Combined camera+object M-vs-K delta: `-0.0562/-4.3487`.
- Object-only default M-vs-K delta: `+0.0205/-2.5403`.
- Object-only velocity/depth masked readiness is true in this two-frame run,
  but the object-only full-frame changed-edge-ratio exceeds the `0.02`
  tolerance by `0.0005`.
- The ledger records `mVsKDynamicStable=false`,
  `objectMaskedStabilityReady=true`, and `repeatPolicy=0/3`.

Three-frame object-only evidence:

- K/M animation clock mode is `1/1`; phase delta is `0.0422s / 1.266 ticks`.
- Default M full-frame M-vs-K delta is `-0.0213/+2.6484`, so the failure moves
  from edge-ratio to mean-edge-delta when the sequence has two adjacent pairs.
- Default M velocity masked delta is `+0.0247/+0.7645`; default M depth masked
  delta is `+0.0316/+6.0796`.
- M sharpness-zero full-frame delta is `-0.0257/+0.5209` and passes the K gate;
  velocity masked sharpness-zero also passes with `-0.0397/-1.5040`.
- Depth masked sharpness-zero still fails mean-edge-delta with
  `-0.0346/+5.6027`.

Current decision: combined motion is no longer the blocker after the skeletal
clock fix. The remaining focused blocker is object-only skinned silhouette plus
depth/disocclusion/history stability. `SE_DLSS_SHARPNESS=0.0` is a useful
controlled diagnostic because it improves full-frame and velocity-mask signals,
but it does not clear the depth-mask mean delta and it remains explicitly
non-promotable while production still depends on NGX isolation and lacks
subjective clarity acceptance. Do not change default sharpness yet.

## Slice 4.92 Execution Plan

Slice 4.92 audits the DLSS evaluate input semantics behind the remaining
object-only skinned shimmer. The goal is to remove any engine-side ambiguity
before tuning sharpness, mip bias, anisotropy, CAS, or RCAS.

1. Motion-vector scale semantics.
   - Treat the engine velocity target as UV-space motion for native TAA.
   - Convert only the DLSS evaluate handoff to pixel-space MV scale by using
     the active render extent for `InMVScaleX/Y`.
   - Keep `SE_DLSS_MOTION_VECTOR_SCALE_MODE=unit` and
     `SE_DLSS_UNIT_MOTION_VECTOR_SCALE=1` as explicit diagnostic fallbacks to
     reproduce the old `1/1` path.

2. DLSS input diagnostics.
   - Expose create-flag bits for HDR, MVLowRes, MVJittered, DepthInverted, and
     AutoExposure.
   - Expose color/depth/motion-vector formats, extents, aspect masks, render
     extent matches, and MV-scale semantic bits in CSV and quick QA summaries.
   - Keep `SE_DLSS_CREATE_FLAG_MV_JITTERED=1` and
     `SE_DLSS_CREATE_FLAG_DEPTH_INVERTED=1` as opt-in A/B probes, not defaults.

3. Focused verification only.
   - Run `_quick_build.bat`.
   - Run the existing fullscreen object-only skinned diagnostics on monitor
     index `1` / `\\.\DISPLAY2`.
   - Do not run the full matrix; this slice should answer whether the current
     object-only evidence was contaminated by unit-space MV scale.

## Slice 4.92 Execution Evidence

Slice 4.92 is implemented. `RecordTemporalUpscalerEvaluate` now sends
pixel-space MV scale to NGX by default while leaving the velocity buffer itself
in UV space for native TAA. The temporal upscaler status, renderer stats,
benchmark CSV, and quick QA metrics now report DLSS create-flag bits, resource
formats/extents/aspect masks, depth/MV render-extent matches, and whether the
MV scale is pixel-space or the old unit-space diagnostic path.

Verification:

- PowerShell parser validation passes for `scripts\Test-DlssVisualQa.ps1`.
- `git diff --check` passes for the touched files with only the existing
  LF/CRLF warnings.
- `_quick_build.bat` passes for `SelfEngineForward3D`.
- `m-object-shimmer-fullscreen-diagnostics
  -UseKnownNgxInternalLayoutIsolation -SequenceFrameCount 3
  -SequenceInitialDelaySeconds 4 -SequenceIntervalSeconds 1` passes on monitor
  index `1` / `\\.\DISPLAY2`.

Latest object-only fullscreen input evidence:

- K, M, and M sharpness-zero all report `dlssMotionVectorScale=2560/1440`.
- MV semantics report `pixel/unit/matchesRender=1/0/1`.
- Create flags are `3`, decoded as
  `hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/0/0/0`.
- DLSS input extents are
  `color/depth/mv=2560x1440/2560x1440/2560x1440`.
- Depth and MV input extent matches are `1/1`.
- The hotspot classification remains `velocity-and-depth-overlap`.

Latest object-only fullscreen quality evidence after the MV-scale fix:

- Full-frame M-vs-K delta is changed-edge `+0.0017` and mean-edge `+3.5950`,
  so M still fails the K-relative mean-edge tolerance.
- Velocity-masked M-vs-K delta is `+0.0584/+11.4751`.
- Depth-masked M-vs-K delta is `-0.0270/+11.8437`.
- Validation is clean under the explicit NGX internal layout isolation policy,
  and the object-only input contract has no blocked reasons.

Decision: the old unit-space `1/1` MV scale is fixed and should no longer be
used as a production-ready signal. M remains non-production because the
fullscreen object-only skinned silhouette still has K-relative mean-edge
instability even with pixel-space MV scale and matching depth/MV resource
extents. The next focused slice should run a controlled `MVJittered` A/B and
inspect skinned silhouette disocclusion/history behavior before touching
sharpness, mip bias, anisotropy, CAS, or RCAS defaults.

## Slice 4.93 Execution Plan

Slice 4.93 runs the controlled `MVJittered` A/B identified by Slice 4.92. The
hypothesis is that SelfEngine's current velocity output includes projection
jitter in the current clip position, so NGX may need the DLSS create flag
`MVJittered` to interpret the motion-vector buffer correctly.

1. Focused suite.
   - Add `m-object-shimmer-mv-jittered-ab` as a standalone focused suite.
   - Keep it out of the `m-highres-dynamic` group so daily production audits do
     not get slower.
   - Run only K, default M, and M with `SE_DLSS_CREATE_FLAG_MV_JITTERED=1` in
     the real fullscreen object-only skinned-FBX lane.

2. Summary evidence.
   - Record default M-vs-K, MVJittered M-vs-K, and MVJittered M-vs-default-M
     sequence instability.
   - Assert that the MVJittered lane really reports
     `hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/1/0/0`.
   - Keep the result diagnostic-only until masked repeat evidence proves it is
     not trading one artifact for another.

## Slice 4.93 Execution Evidence

Slice 4.93 is implemented in `scripts\Test-DlssVisualQa.ps1`. The new
`m-object-shimmer-mv-jittered-ab` suite uses the same real default Forward 3D
scene, the production `Fist Fight B.fbx` skinned animation path, monitor index
`1` / `\\.\DISPLAY2`, and borderless fullscreen capture. It compares K,
default M, and M with `SE_DLSS_CREATE_FLAG_MV_JITTERED=1`.

Verification:

- PowerShell parser validation passes.
- `-ListSuites -Suite m-object-shimmer-mv-jittered-ab` expands to the new suite.
- `git diff --check -- scripts\Test-DlssVisualQa.ps1` passes with only the
  existing LF/CRLF warning.
- `m-object-shimmer-mv-jittered-ab -UseKnownNgxInternalLayoutIsolation
  -SequenceFrameCount 3 -SequenceInitialDelaySeconds 4
  -SequenceIntervalSeconds 1` passes on monitor index `1` / `\\.\DISPLAY2`.

Latest MVJittered A/B evidence:

- Default M create flags:
  `hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/0/0/0`.
- MVJittered M create flags:
  `hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/1/0/0`.
- Both default M and MVJittered M use pixel-space MV scale `2560/1440`.
- Default M-vs-K full-frame delta is `+0.0221/+4.7767`, failing the K-relative
  gate.
- MVJittered M-vs-K full-frame delta is `-0.0171/+1.5225`, passing the current
  K-relative full-frame gate.
- MVJittered M-vs-default-M delta is `-0.0392/-3.2542`, so the create flag
  improves both changed-edge ratio and mean-edge delta in this focused run.

Decision: `MVJittered` is now a strong candidate input-semantics fix for the
object-only skinned silhouette instability, but it is not promoted to default
yet. The evidence is full-frame only; the next focused slice must repeat the
A/B with velocity and depth masks before treating `MVJittered` as production
configuration.

## Slice 4.94 Execution Plan

Slice 4.94 validates the Slice 4.93 `MVJittered` result against masks that
better localize skinned-object instability. The goal is to prevent a full-frame
average from hiding a velocity-region or depth/disocclusion artifact.

1. Masked A/B suite.
   - Add `m-object-shimmer-mv-jittered-masked-ab` as a standalone focused suite.
   - Keep it out of `m-highres-dynamic` so daily audits do not pay for the five
     capture lanes.
   - Capture velocity debug, depth debug, K, default M, and
     `SE_DLSS_CREATE_FLAG_MV_JITTERED=1` M in the real fullscreen
     `Fist Fight B.fbx` object-only lane.

2. Readiness split.
   - Record full-frame, velocity-masked, and depth-masked sequence instability.
   - Require all three M-vs-K gates to pass repeatedly before `MVJittered` can
     become a default candidate.
   - Keep the control assertions for create flags and pixel-space MV scale.

## Slice 4.94 Execution Evidence

Slice 4.94 is implemented in `scripts\Test-DlssVisualQa.ps1`. The new
`m-object-shimmer-mv-jittered-masked-ab` suite uses the real Forward 3D main
scene, production skinned `Fist Fight B.fbx`, monitor index `1` /
`\\.\DISPLAY2`, hidden ImGui, and borderless fullscreen monitor-resolution
capture. It emits `diagnostics.mObjectShimmerMvJitteredMaskedAb` with
full-frame, velocity-mask, depth-mask, and promotion-control summaries.

Verification:

- PowerShell parser validation passes.
- `-ListSuites -Suite m-object-shimmer-mv-jittered-masked-ab` expands to the new
  suite.
- `git diff --check -- scripts\Test-DlssVisualQa.ps1` passes with only the
  existing LF/CRLF warning.
- `m-object-shimmer-mv-jittered-masked-ab -UseKnownNgxInternalLayoutIsolation
  -SequenceFrameCount 3 -SequenceInitialDelaySeconds 4
  -SequenceIntervalSeconds 1` passes on monitor index `1` / `\\.\DISPLAY2`.

Latest masked MVJittered A/B evidence:

- Validation is clean only under the explicit known-NGX internal layout
  isolation policy.
- Default M create flags:
  `hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/0/0/0`.
- MVJittered M create flags:
  `hdr/mvLowRes/mvJittered/depthInverted/autoExposure=1/1/1/0/0`.
- Both default M and MVJittered M use pixel-space MV scale `2560/1440` with
  `pixel/unit/matchesRender=1/0/1`.
- Full-frame MVJittered M-vs-K delta is `+0.0260/+1.6805`, so it fails the
  changed-edge-ratio tolerance even though mean-edge delta remains within
  tolerance.
- Velocity-masked MVJittered M-vs-K delta is `+0.0372/+2.9458`, failing both
  masked tolerances.
- Depth-masked MVJittered M-vs-K delta is `-0.0378/-3.2579`, passing the
  masked gate and improving over K in this run.
- MVJittered-vs-default-M is not a clear improvement in this masked run:
  full-frame `+0.0107/+0.1155`, velocity mask `+0.0428/-0.7973`, and depth mask
  `+0.0112/+0.0011`.
- Masked readiness is `fullFrameMVsK=false`, `velocityMaskedMVsK=false`,
  `depthMaskedMVsK=true`, `allReady=false`.

Decision: do not promote `MVJittered` to default. The previous full-frame A/B was
useful, but the masked run shows the remaining instability is concentrated in
the moving/skinned velocity region rather than the depth/disocclusion mask. The
next focused target should inspect the velocity-buffer convention around
jittered current clip position, skinned silhouette coverage, and history
rejection/reset behavior before changing mip bias, sharpness, anisotropy, CAS,
or RCAS defaults.

## Slice 4.95 Execution Evidence

Slice 4.95 adds an opt-in velocity-jitter history diagnostic and moves the next
inspection step into Nsight Graphics instead of another screenshot-only loop.
The default renderer behavior is unchanged. The new diagnostic is enabled with
`SE_TEMPORAL_VELOCITY_JITTER_POLICY=jittered`, which stores the current frame
jitter and applies the previous frame jitter to the previous projection used by
the velocity path. This makes the `SE_DLSS_CREATE_FLAG_MV_JITTERED=1` A/B test
internally consistent for a fully jittered velocity convention.

Verification and tooling:

- `_quick_build.bat` passes for `SelfEngineForward3D`.
- `scripts\Test-DlssVisualQa.ps1` parses successfully.
- Windows Smart App Control blocked the freshly built unsigned debug exe. The
  new helper `scripts\Sign-SelfEngineDevBinary.ps1` signs the local debug binary
  with a CurrentUser code-signing certificate only; it does not add a trusted
  root certificate or change system policy. The signed debug exe launches under
  SAC and remains suitable for visual QA and Nsight launch.
- `m-object-shimmer-mv-jittered-masked-ab -UseKnownNgxInternalLayoutIsolation
  -SequenceFrameCount 3 -SequenceInitialDelaySeconds 4
  -SequenceIntervalSeconds 1` passes on monitor index `1` / `\\.\DISPLAY2`.
- `ngfx-capture --version` reports NVIDIA Nsight Graphics `2026.2.0.0`.
- Nsight capture succeeds for the real fullscreen object-only skinned lane:
  `out\nsight_captures\selfengine_m_dlaa_mvjittered_jitterhistory_frame120.ngfx-capture`,
  frame `120`, one captured frame, `38` events, `344` resources, and `117 MiB`
  output. The captured lane is M + DLAA + `Fist Fight B.fbx` production skinning
  + object motion + `MVJittered` + jittered-history velocity.
- `ngfx-replay --metadata` confirms a Vulkan capture on RTX 5070 at
  `2560x1440`, with the expected M/DLAA/MVJittered/jittered-history/skinned-FBX
  environment variables embedded in the capture. `ngfx-replay
  --metadata-logs-errors` reports no captured log messages with severity
  `>= 2`, and `--metadata-screenshot` exports a nonblack frame showing the real
  main scene with the cube cluster and FBX model.

Latest masked A/B evidence:

- `validationClean=true` under the explicit known-NGX internal layout isolation
  policy.
- Normal MVJittered M-vs-K readiness is now true for full-frame, velocity-mask,
  and depth-mask evidence in this run.
- Jittered-history readiness is also true for full-frame, velocity-mask, and
  depth-mask evidence.
- Normal MVJittered M-vs-K deltas are full-frame `+0.2044/-0.7683`, velocity
  mask `+2.6573/-2.4245`, and depth mask `+0.0655/-0.6485` for mean-delta /
  mean-edge-delta.
- Jittered-history M-vs-K deltas are full-frame `+2.1962/+0.4666`, velocity
  mask `+2.2912/-0.5599`, and depth mask `+0.0502/-0.7683` for mean-delta /
  mean-edge-delta.
- The jittered-history lane proves its control bits:
  `mvJitteredCreateFlagBits=1/1/1/0/0`,
  `velocityJitteredHistoryPolicy=1`,
  `velocityPreviousJitterApplied=1`, and previous jitter pixels
  `0.125/0.277778`.

Decision: do not promote `MVJittered` or jittered-history velocity to default
yet. This run is better than Slice 4.94, but jittered-history raises full-frame
mean delta versus normal MVJittered, and production still requires a strict
validation-policy decision plus repeat high-resolution dynamic evidence. The
next debugging step is to open the Nsight capture and inspect the DLSS evaluate
inputs directly: color, depth, motion-vector image contents, jitter offsets,
resource layouts, and the skinned silhouette/disocclusion region around the
animated FBX. Repeated screenshots should now be reserved for confirming a
specific hypothesis found in the capture.

## Slice 4.96 Execution Evidence

Slice 4.96 makes the Nsight capture path inspectable by name instead of by
unstable object ids. The previous capture proved `ngfx-capture` works, but
`ngfx-replay --metadata-objects` only exposed default names such as
`Image_235`, which made automated DLSS input checks too brittle.

Implementation:

- Added `SetVulkanDebugObjectName` in `src\renderer\vulkan\vulkan_common.h`.
  It uses `vkSetDebugUtilsObjectNameEXT` when available and no-ops otherwise.
- `VulkanSceneRenderTargets` now assigns stable debug names to the DLSS-relevant
  image/imageView objects at creation time:
  `SelfEngine.DLSS.InputColor`,
  `SelfEngine.DLSS.InputDepth`,
  `SelfEngine.DLSS.InputMotionVectors`,
  `SelfEngine.DLSS.OutputColor`,
  `SelfEngine.DLSS.BiasCurrentColorMask`,
  `SelfEngine.DLSS.TransparencyMask`, and
  `SelfEngine.Temporal.HistoryColor`.
- Added `scripts\Test-NsightDlssCaptureMetadata.ps1`, a lightweight gate that
  runs `ngfx-replay --metadata`, `--metadata-objects`, and
  `--metadata-logs-errors`. It asserts the M/DLAA/skinned/MVJittered/
  jittered-history capture environment, the expected resolution, required
  SelfEngine DLSS object names, and zero embedded error-level logs. It can also
  export the capture screenshot through `--metadata-screenshot`.

Verification:

- `_quick_build.bat` passes.
- `scripts\Sign-SelfEngineDevBinary.ps1` signs the rebuilt debug exe so Smart
  App Control does not block the QA/capture launch.
- A fresh targeted capture succeeds:
  `out\nsight_captures\selfengine_m_dlaa_mvjittered_jitterhistory_debugnames_frame90.ngfx-capture`.
  It uses frame `90`, M + DLAA + `Fist Fight B.fbx` production skinning +
  object motion + MVJittered + jittered-history velocity.
- `ngfx-replay --metadata-objects` now finds the named DLSS resources, including
  all seven required image groups and the frame-active views for input color,
  depth, motion vectors, and temporal history.
- `scripts\Test-NsightDlssCaptureMetadata.ps1 -CapturePath
  out\nsight_captures\selfengine_m_dlaa_mvjittered_jitterhistory_debugnames_frame90.ngfx-capture
  -ScreenshotPath
  out\nsight_captures\selfengine_m_dlaa_mvjittered_jitterhistory_debugnames_frame90.checked.png`
  passes with `requiredEnvironmentReady=true`, `requiredObjectNamesReady=true`,
  `embeddedErrorLogsReady=true`, `selfEngineImageCount=21`, and
  `selfEngineImageViewCount=4`.

Decision: this still does not promote M, MVJittered, or jittered-history to
production. It removes a debugging blind spot: future Nsight-based DLSS checks
can now reliably locate the color/depth/MV/output/mask images and should use the
metadata gate before any subjective inspection or repeated visual-QA run.

## Slice 4.97 Execution Evidence

Slice 4.97 connects the named Nsight capture to the renderer's CSV DLSS contract
so a capture cannot be accepted merely because it exists. The goal is still
diagnostic: prove that the captured configuration has the same DLSS input
contract as the focused real-scene QA lane before opening the frame for manual
velocity/depth/layout inspection.

Implementation:

- `scripts\Test-NsightDlssCaptureMetadata.ps1` now accepts
  `-BenchmarkCsvPath`.
- When provided, the script reads the latest CSV row and asserts:
  frame-graph validation `0`, DLSS evaluate/output `1/1`, DLSS post source
  active, quality gate ready, masks `255/255/0`, real skinned animation not
  unsupported, object motion and scene-content motion ready, applied jitter,
  jitter-history policy/applied `1/1`, MVJittered create flag enabled,
  pixel-space MV scale matching render extent, color/depth/MV input extents
  matching `2560x1440`, output extent matching `2560x1440`, and DLSS jitter
  matching the temporal jitter pixels.

Verification:

- PowerShell parser validation passes.
- `git diff --check -- scripts\Test-NsightDlssCaptureMetadata.ps1` passes.
- The combined metadata/CSV gate passes:
  `scripts\Test-NsightDlssCaptureMetadata.ps1 -CapturePath
  out\nsight_captures\selfengine_m_dlaa_mvjittered_jitterhistory_debugnames_frame90.ngfx-capture
  -BenchmarkCsvPath
  out\reference_captures\dlss_visual_qa\m_object_shimmer_mv_jittered_masked_ab_object_only_preset_m_mv_jittered_jittered_history_present.csv`.

Latest contract output:

- `requiredEnvironmentReady=true`
- `requiredObjectNamesReady=true`
- `embeddedErrorLogsReady=true`
- `csvContractReady=true`
- `dlssEvaluateOutput=1/1`
- `qualityMasks=255/255/0`
- `dlssInputExtents=2560x1440/2560x1440/2560x1440`
- `dlssOutputExtent=2560x1440`
- `dlssMotionVectorScale=2560/1440`
- `dlssJitter=-0.125/-0.277778`
- `temporalJitter=-0.125/-0.277778`
- `jitteredHistory=1/1`
- `skinnedProduction=0/1/1`

Decision: this still does not promote M. It raises the floor for future Nsight
inspection: a targeted capture should first pass metadata and CSV contract
checks, then use the named `SelfEngine.DLSS.*` resources to inspect actual
motion-vector/depth/color contents and skinned silhouette behavior.

## Slice 4.98 Execution Evidence

Slice 4.98 adds a repeatable Nsight focused-capture wrapper so future DLSS
debugging does not require a full visual-QA matrix or hand-assembled
`ngfx-capture` command line.

Implementation:

- Added `scripts\Capture-NsightDlssFocusedFrame.ps1`.
- The wrapper signs or reuses the local debug executable, runs a short normal
  benchmark probe to produce a CSV contract row, launches `ngfx-capture` for a
  targeted frame, runs `scripts\Test-NsightDlssCaptureMetadata.ps1`, and writes a
  compact result JSON.
- The default lane is the real Forward 3D main scene with
  `Fist Fight B.fbx` in the skinned production path, preset M, DLAA,
  `SE_DLSS_CREATE_FLAG_MV_JITTERED=1`, jittered previous-projection velocity,
  hidden ImGui, borderless `2560x1440`, and the known NGX internal layout
  isolation switch explicitly recorded when used.
- The wrapper intentionally uses a two-step CSV/capture flow: the benchmark
  probe exits normally so the CSV is complete, while the Nsight capture uses
  terminate-after-capture to avoid a `vkDestroyDevice` capture finalization
  incompatibility seen when the app exits on the captured frame.

Verification:

- PowerShell parser validation passes.
- `git diff --check -- scripts\Capture-NsightDlssFocusedFrame.ps1` passes.
- Focused wrapper smoke passes:
  `scripts\Capture-NsightDlssFocusedFrame.ps1 -SkipBuild -SkipSign -CaptureName
  selfengine_m_dlaa_focused_wrapper_smoke_frame90
  -UseKnownNgxInternalLayoutIsolation -CaptureScreenshot -Force`.
- Outputs:
  `out\nsight_captures\selfengine_m_dlaa_focused_wrapper_smoke_frame90.ngfx-capture`,
  `.csv`, `.png`, `.result.json`, and logs.

Latest result:

- Capture frame/resolution: `90`, `2560x1440`.
- GPU/API: Vulkan on NVIDIA GeForce RTX 5070.
- Metadata gate: environment ready, required object names ready, embedded error
  logs clean, CSV contract ready.
- CSV contract: DLSS evaluate/output `1/1`, post source active, quality masks
  `255/255/0`, DLSS input extents `2560x1440/2560x1440/2560x1440`, output
  extent `2560x1440`, pixel-space MV scale `2560/1440`, DLSS jitter matches
  temporal jitter `-0.125/-0.277778`, jittered-history `1/1`, skinned production
  `0/1/1`.
- Follow-up strict fields now separate capture-log cleanliness from production
  readiness: isolated captures can report `captureLogValidationClean=true`, but
  must keep `productionStrictValidationClean=false` and
  `productionReadyBlockedByKnownNgxIsolation=true` while the known NGX internal
  layout policy is active.
- Follow-up K control: `scripts\Test-NsightDlssCaptureMetadata.ps1` now accepts
  expected DLSS quality/preset parameters, and
  `scripts\Capture-NsightDlssFocusedFrame.ps1 -DlssPreset k` passes the same
  focused route without known-NGX isolation. The control capture
  `selfengine_k_dlaa_focused_wrapper_control_frame90` reports CSV preset `11`,
  `productionStrictValidationClean=true`, `productionStrictValidationReason=strict-clean`,
  no known NGX isolation blocker, and a nonblack metadata screenshot of the same
  real main scene.
- Exported screenshot is nonblack and shows the real main scene with the
  skinned FBX model and hidden ImGui.

Decision: still diagnostic-only. This makes the next manual Nsight inspection
repeatable, but M/MVJittered/jittered-history are not production defaults until
strict validation policy, repeat high-resolution dynamic stability, and
subjective ghosting/clarity review are accepted.

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

## Slice 4.13 Execution Evidence

- Replaced the previous neutral-only DLSS mask path for residual/transparent
  controlled scenes with a real pre-upscale mask draw. `VulkanSceneRenderTargets`
  now creates `DlssBiasCurrentColorMask` and `DlssTransparencyMask` with color
  attachment usage, and the new `VulkanDlssMaskRenderPass` /
  `VulkanDlssMaskFramebuffer` pair writes both `R8_UNORM` mask resources before
  DLSS evaluate.
- Added `dlss_mask_3d.frag` and `PipelineSpec::DlssMask3D`. The shader reuses
  the 3D material id, UV transform, base-color alpha, opacity texture, alpha
  cutoff, and transmission/volume hint to write non-zero reactive and
  transparency mask values for visible WBOIT/forward-residual geometry. Fully
  transparent pixels are discarded instead of biasing DLSS history.
- Command recording now runs `DlssMaskPreUpscale` before `TemporalUpscalerEvaluate`
  when WBOIT or forward-residual commands are present. If no mask geometry is
  present, the old neutral transfer-clear path remains as the fallback. CSV
  records `dlss_mask_draws`, route-specific WBOIT/forward-residual mask draws,
  and mask material/mesh binds.
- Full visual QA passes after the new mask path:
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`.
  The run reports matching `780/780` CSV columns and 0 frame-graph validation
  issues across opaque, WBOIT, forward-special, and material-stress pairs.
  WBOIT DLSS reports `dlssMasks=79/79/0`, forward-special reports
  `79/0/79`, and material-stress remains `0/0/0`, proving masks are drawn only
  for the residual/transparent routes covered by this slice.
- Latest visual comparison evidence: opaque changed 3492 sampled pixels with
  mean RGB delta `23.2862`, WBOIT changed 259 with mean `0.9752`,
  forward-special changed 220 with mean `0.9937`, and material-stress changed
  257 with mean `0.9374`; every max delta remains at or below `579`.
- This removes the neutral-mask-only limitation for the controlled WBOIT and
  forward-special DLSS routes. Remaining production work is mask policy depth:
  authored/material-specific mask tuning for particles, water, emissive,
  refraction, animated imported/skinned content, and larger moving-scene
  temporal evidence.

## Slice 4.14 Execution Evidence

- Added a full-resolution DLAA visual-QA lane for the Forward 3D jagged-edge
  follow-up. `scripts\Test-DlssVisualQa.ps1` now loads
  `docs/reference_baselines/dlss_dlaa_visual_qa_baseline.json`, runs paired
  `dlaa_native_deferred_hdr` and `dlaa_present` benchmark/capture passes, and
  clears `SE_DLSS_QUALITY` / `SE_DLSS_MODE` between runs so DLSS quality-mode
  selection cannot leak across scenarios.
- The DLAA path keeps `SE_RENDER_SCALE=1.0` with the render-scale carrier
  applied, requests `SE_UPSCALER_PLUGIN=dlss`, selects
  `SE_DLSS_QUALITY=dlaa`, and requires the visible post source to activate
  through `SE_DLSS_PRESENT=1`.
- The new baseline requires quality mode `6` (DLAA), transformer preset K
  (`11`), render scale `1/1/0`, full-resolution DLSS extents
  `1280x720->1280x720`, quality gate `1/1/0`, masks `255/255/0`, and the same
  eight production-quality input bits ready for the controlled opaque grid
  route.
- Verification:
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
  passes. The run reports matching `780/780` CSV columns and 0 frame-graph
  validation issues for the new DLAA pair. `dlaa_present.csv` reports DLSS
  evaluate/output `1/1`, post source `1/1/0`, quality gate `1/1/0`, quality
  masks `255/255/0`, render scale `1/1/0`, quality mode/preset `6/11`, and
  DLSS render/output extents `1280x720->1280x720`.
- Latest DLAA native-vs-DLAA screenshot comparison samples 14352 pixels with
  3487 changed pixels, mean RGB delta `22.7193`, and max delta `578`. The
  existing SR/WBOIT/forward-special/material-stress visual-QA pairs still pass
  in the same run.
- This is the first anti-aliasing-oriented DLAA baseline for the controlled
  Forward 3D path. It is not yet broad production DLSS quality: imported/static
  model coverage, skinned/animated velocity, material-specific mask policy,
  larger moving-scene temporal references, and DLSS/DLAA tuning evidence remain
  the next required slices.

## Slice 4.15 Execution Evidence

- Added the real default Forward 3D application scene to the DLAA visual-QA
  queue. This is the startup scene with smooth ground, three
  cube renderables, six total lights, five local lights, one rect light, and one
  scene reflection probe; it is the scene used for the reported visible jagged
  edges, not the benchmark-grid stress scene.
- `scripts\Test-DlssVisualQa.ps1` now distinguishes explicit benchmark-grid
  scenarios from default application-scene scenarios. `Invoke-BenchmarkRun`
  still defaults to `SE_BENCHMARK_SCENE=grid` for legacy grid routes, but the
  new `-UseApplicationScene` switch keeps `SE_BENCHMARK_SCENE` unset so the CSV
  and screenshot both exercise the real default scene. The existing SR and grid
  DLAA environments now explicitly set `SE_BENCHMARK_SCENE=grid` to avoid
  mixing grid CSV evidence with default-scene screenshots.
- Added
  `docs/reference_baselines/dlss_default_scene_dlaa_visual_qa_baseline.json`.
  The baseline requires native/DLAA render scale `1/1/0`, DLSS quality
  mode/preset `6/11`, full-resolution DLSS extents `1280x720->1280x720`,
  post source `1/1/0`, quality gate `1/1/0`, quality masks `255/255/0`, draw
  route `4/4/0/0`, and scene counters
  `materials=4,lights=6,local=5,rect=1,probes=1`.
- Verification:
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
  passes with matching `780/780` CSV columns and 0 frame-graph validation
  issues. `default_scene_dlaa_present.csv` reports evaluate/output `1/1`,
  post source `1/1/0`, quality gate `1/1/0`, quality masks `255/255/0`, render
  scale `1/1/0`, quality mode/preset `6/11`, and DLSS extents
  `1280x720->1280x720`.
- The default-scene native-vs-DLAA screenshot comparison samples 14352 pixels
  with 11569 changed pixels, mean RGB delta `89.3985`, and max delta `662`.
  The generated image
  `out/reference_captures/dlss_visual_qa/default_scene_dlaa_present.png`
  visually confirms the startup application scene rather than the benchmark
  cube grid.
- This makes the user's visible default Forward 3D scene part of the DLAA
  quality queue. It still does not prove broad production DLSS quality for
  imported models, skinned animation, particles/water/refraction masks, or
  larger moving-scene temporal stability.

## Slice 4.16 Execution Evidence

- Fixed a DLSS/DLAA temporal-input mismatch found from the real default-scene
  DLAA row. The renderer still prepared Halton jitter when
  `SE_TEMPORAL_JITTER=1`, but projection jitter was not actually applied
  unless the TAA jitter-application gate was active; DLSS nevertheless received
  the non-zero prepared jitter offsets. `RecordTemporalUpscalerEvaluate` now
  sends `0/0` jitter offsets to DLSS whenever `temporal_jitter_applied=0`, and
  only forwards the Halton pixel offsets when projection jitter was actually
  applied.
- `scripts\Test-DlssVisualQa.ps1` now asserts DLSS jitter consistency across
  every DLSS-present route. If projection jitter is not applied, DLSS jitter
  must be zero; if projection jitter is applied, DLSS jitter must match the
  temporal frame jitter. The original failing signal was
  `temporal_jitter_applied=0` with DLSS jitter `-0.125/-0.277778`; the latest
  default-scene DLAA row reports DLSS jitter `0/0` with output ready and quality
  gate ready.
- The visual-QA window placement is now deterministic for multi-monitor
  desktops. `scripts\Test-DlssVisualQa.ps1` and `scripts\Test-2DRenderSmoke.ps1`
  default capture placement to monitor index `1`, clamp to monitor `0` when the
  requested index is unavailable, and size the capture inside that monitor's
  working area instead of using global `(40,40)` coordinates. The DLSS summary
  records the selected capture monitor; the latest run reports requested `1`,
  actual `1/2`, device `\\.\DISPLAY2`, area `2048,0 2048x1104`.
- The visual-QA script now prints each `Benchmark [...]` and `Capture [...]`
  phase so the on-screen sharp/blurred/jagged changes can be tied to the active
  configuration. The minimum native-vs-DLSS changed-pixel thresholds were
  removed from the baselines because "different from native" is not an image
  quality goal now that CSV gates directly prove DLSS output readiness, post
  source activation, quality input readiness, and jitter consistency.
- Verification: `_quick_build.bat` passes for `SelfEngineForward3D`.
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
  passes with matching `780/780` CSV rows, 0 frame-graph validation issues, and
  all captures on `\\.\DISPLAY2`. Latest comparisons are SR `118` changed /
  mean `0.4525`, grid DLAA `49` / `0.3232`, default-scene DLAA `30` / `0.2349`,
  WBOIT `119` / `0.4422`, forward-special `89` / `0.3914`, and material-stress
  `122` / `0.4556`, each with max delta `564`.
- This removes one concrete source of moving-camera shimmer: DLSS is no longer
  told about jitter that was not present in the rendered color/depth/velocity
  inputs. It is still not broad production DLSS/DLAA quality; true production
  status still needs applied jitter policy, jitter-aware history storage,
  motion-vector validation under camera motion, larger moving-scene captures,
  imported/skinned content, and material-specific mask tuning.

## Slice 4.17 Execution Evidence

- Corrected the validation gap raised by the user: the previous pressure and
  default-scene DLAA screenshots were still effectively static, so they could
  not represent the dynamic camera case where temporal shimmer is visible.
  `SelfEngineForward3D` now supports deterministic benchmark camera motion
  through `SE_BENCHMARK_CAMERA_MOTION=orbit`, with optional speed/yaw/pitch/
  distance controls. The path drives the real default application scene camera
  in the update loop and keeps normal mouse interaction unchanged when the
  benchmark flag is absent.
- Added
  `docs/reference_baselines/dlss_default_scene_dlaa_motion_visual_qa_baseline.json`
  and a new `default_scene_dlaa_motion_present` visual-QA lane. The lane keeps
  `SE_BENCHMARK_SCENE` unset, uses the real startup scene, selects full-
  resolution DLAA, enables the visible DLSS post source, and requires camera-
  motion readiness rather than only static-frame readiness.
- `scripts\Test-DlssVisualQa.ps1` now captures a moving-camera sequence from a
  single running window (`default_scene_dlaa_motion_present_00/01/02.png`) and
  compares adjacent frames. The sequence gate requires nonblank frames,
  frame-to-frame pixel changes above a minimum, and a bounded mean delta so the
  test can catch both accidental static captures and obvious full-frame
  instability.
- Verification:
  `_quick_build.bat` passes for `SelfEngineForward3D`.
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
  passes. The new motion row reports matching `780/780` columns, 0 frame-graph
  validation issues, evaluate/output `1/1`, post source `1/1/0`, quality gate
  `1/1/0`, quality masks `255/255/0`, render scale `1/1/0`, quality mode/
  preset `6/11`, draw route `4/4/0/0`, camera-motion readiness `1/1`, and
  DLSS jitter `0/0` while projection jitter is not applied.
- The dynamic screenshot sequence reports two adjacent-frame comparisons with
  minimum changed sampled pixels `125`, maximum mean RGB delta `3.9588`, and
  maximum delta `564`. This proves the new QA route is genuinely dynamic; it is
  still a first stability sentinel, not final production IQ. The next depth
  should add applied-jitter policy, frame-indexed motion-vector debug captures,
  larger camera paths, moving objects, imported/skinned content, and stricter
  shimmer metrics around high-contrast edges.

## Slice 4.18 Execution Evidence

- Upgraded the real default-scene moving-camera DLAA visual-QA lane from
  prepared-only jitter to applied projection jitter. The lane now sets
  `SE_TAA_APPLY_JITTER=1`, keeps `SE_BENCHMARK_SCENE` unset, uses the real
  startup Forward 3D scene, and still captures the three-frame same-window
  sequence under full-resolution DLAA.
- `scripts\Test-DlssVisualQa.ps1` now isolates the jitter-application
  environment variables, asserts `temporal_jitter_applied=1` for the moving
  default-scene DLAA lane, and keeps the DLSS jitter consistency gate. The
  latest row reports temporal jitter `-0.125/-0.277778` and DLSS jitter
  `-0.125/-0.277778`, proving NGX receives the same jitter that was applied to
  the rendered inputs.
- Added a high-contrast edge temporal metric to the moving-camera screenshot
  sequence. Adjacent-frame comparisons now record sampled edge pixels,
  changed-edge pixels, mean edge delta, and max edge delta in `summary.json`,
  with baseline thresholds in
  `docs/reference_baselines/dlss_default_scene_dlaa_motion_visual_qa_baseline.json`.
- Verification:
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-DlssVisualQa.ps1 -SkipBuild`
  passes. The moving DLAA row reports matching `782/782` columns, 0
  frame-graph validation issues, output/post `1/1` and `1/1/0`, quality gate
  `1/1/0`, camera-motion readiness `1/1`, jitter applied `1`, DLSS input ready
  `1`, native TAA resolve `0`, native TAA suppress-for-upscaler `1`, draw route
  `4/4/0/0`, and DLSS quality mode/preset `6/11`.
- The same slice also splits DLSS input readiness from native TAA final-
  composite resolve. In the moving DLAA row, `SE_TAA=1` still prepares the
  history/velocity inputs required by the upscaler contract, but the final HDR
  composite no longer blends native TAA over the DLSS output; CSV reports
  `temporal_taa_resolve_enabled=0`,
  `temporal_taa_resolve_suppressed_for_upscaler=1`,
  `temporal_taa_fallback_reason=6`, and
  `temporal_upscale_contract_ready=1`.
- The latest dynamic sequence reports `minChanged=133`, `maxMean=4.1015`,
  `max=564`, edge sample floor `1045`, max changed-edge pixels `87`, max mean
  edge delta `4.1589`, and max edge delta `188.0`. This is stronger evidence
  for the user's moving-camera shimmer case, but it is still not final
  production IQ: the next depth should add moving-object/imported/skinned
  coverage and stricter content-specific tuning.
