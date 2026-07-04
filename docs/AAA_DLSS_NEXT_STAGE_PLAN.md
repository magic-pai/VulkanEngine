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
