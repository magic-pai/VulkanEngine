# SelfEngine Rendering Lessons

This file records compact debugging lessons for SelfEngine rendering issues. Keep entries practical: symptom, false leads, cause, control test, fix, prevention, validation.

## 2026-07-21 - SSR Must Preserve Hit Provenance Before IBL Fallback

Symptom:
- LightingShowcase metal spheres mostly showed broad environment/IBL shapes. Narrow emissive strips inside wall fixtures and nearby geometry were weak or absent.

False leads:
- Increasing sphere metallic or lowering roughness.
- Changing the cubemap or treating the round direct-light highlights as reflected fixture geometry.

Cause:
- FFX SSSR only has screen-space access to geometry visible in the current HDR frame. Misses are blended with the environment map in Intersect, and the current Intersect fallback is bound to the global prefiltered IBL view.
- The production ResolveTemporal alpha represented glossy validity, not true screen-hit provenance, so the final consumer could not distinguish screen radiance from environment fallback.

Control test:
- Enable `SE_SSR_FFX_HIT_ATTRIBUTION=1` in Debug and require high-confidence hits + partial hits + environment-fallback samples to equal the prepared ray count.

Fix:
- Added Debug-only traced-sample attribution counters to the existing FFX ray-counter buffer. The counters record high-confidence hit samples, partial hit samples, environment-fallback samples, and quantized confidence sum without changing reflection color or filtering.

Prevention:
- Do not tune a reflective showcase until the producer reports hit provenance separately from environment fallback. Treat the attribution values as traced samples because FFX may copy one base ray across quad lanes.
- The next production reflection step must carry a separate hit-confidence resource and use the selected local probe/IBL only as explicit fallback.

Validation:
- `Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict` passed `1090 / 0` across LightingShowcase and Forward3D, including attribution lanes and zero Vulkan/FrameGraph diagnostics.
- LightingShowcase: `4066` high-confidence, `1761` partial, `150795` environment fallback out of `156622` traced samples.
- Forward3D FBX: `73` high-confidence, `36` partial, `2635` environment fallback out of `2744` traced samples.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict` passed `691 / 0`.

## 2026-07-21 - Post-Lighting SSSR Output Must Be Composited In The Same Frame

Symptom:
- When a glossy sphere moved partly off-screen and rapidly returned, it showed its base material/IBL state first and gained the screen-space reflection several frames later.

False leads:
- Tuning material roughness, SSR confidence, history rejection, or probe strength.
- Weakening receiver depth/normal/disocclusion validation to keep stale history alive near the screen edge.

Cause:
- SelfEngine resolved FidelityFX SSSR after Deferred lighting but only consumed `RadianceHistory` in Deferred on the next frame. A newly visible receiver therefore had no same-frame screen-space result, and conservative history rejection could extend the visible delay.

Control test:
- The default FFX lane must record `ResolveTemporal dispatch=1`, `Apply draw=1`, `sourceImageIndex=currentImageIndex`, `sourceFrameAge=0`, and `radianceSource=5`. `SE_SSR_FFX_SAME_FRAME_COMPOSITE_OFF=1` must suppress Apply and restore the delayed `radianceSource=4` path.

Fix:
- Added an AMD-style fullscreen ApplyReflections pass after ResolveTemporal and before TAA/DLSS.
- Deferred preserves a weighted IBL baseline in HDR alpha; destination-alpha additive blending replaces that baseline with current-frame FFX radiance and restores alpha to one.
- Added an HDR load render pass, explicit compute/transfer-to-fragment synchronization, FrameGraph modeling, CSV counters, source identity/frame age, and a reverse control.

Prevention:
- Treat a post-lighting reflection producer consumed by next-frame lighting as temporal history, not current output. Audit final producer-to-visible-consumer ordering whenever entry-frame or re-entry latency appears.
- Do not weaken disocclusion validation to hide a scheduling defect. Prefer same-frame composition with an explicit off-screen IBL/probe fallback.

Validation:
- `Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict` passed `981 / 0` across LightingShowcase, Forward3D FBX, internal-backend, and delayed-path controls with zero FrameGraph/Vulkan issues.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict` passed `691 / 0`; `Test-SsrHitValidationMath.ps1` passed `30 / 0`.
- The user confirmed in the real LightingShowcase window that rapidly returning a partly off-screen glossy sphere no longer exposes its base material before reflections appear.

## 2026-07-21 - FFX SSSR Glossy Stability Is A Multi-Part Sampling Contract

Symptom:
- LightingShowcase glossy spheres showed view-dependent crosses, concentric rings, dirty color blocks, and close-range television-like snow. Individual artifacts could disappear while another returned during camera translation or distance changes.

False leads:
- Treating the cubemap, wall color, SSR thickness, or one temporal reprojection branch as the sole cause.
- Raising ray density alone, changing confidence alone, or clearing output alone and assuming a single control could close the issue.

Cause:
- The visible instability came from several contracts interacting: sparse `1 ray/quad` replication, stochastic GGX directions that did not converge under the current DNSR bridge, LOD0 environment fallback on random directions, a custom sample-count/variance alpha that exposed spherical confidence contours, and stale pixels where the current frame did not write visible output.

Control test:
- Hold the FFX backend and all other rendering settings fixed, then enable `4 rays/quad`, stable roughness-mip environment fallback, deterministic primary reflection directions, AMD glossy-validity confidence, and visible-output clearing together. Reverse controls must independently restore the old behavior for diagnosis.

Fix:
- Promoted the accepted combination to FFX contract v11 defaults: samples `4`, stable environment fallback on, perfect reflection directions on, composite confidence mode `0`, and visible-output clear on.
- Preserved explicit Debug controls for sparse sampling, random directions, unstable fallback, sample/variance confidence, and uncleared output.

Prevention:
- Do not classify glossy SSSR stability as a one-parameter tuning problem. Validate ray coverage, direction stability, fallback LOD, confidence semantics, and full-frame output initialization as one producer-consumer contract.
- Lock visually accepted production defaults with source-level assertions so a later refactor cannot silently restore diagnostic behavior.

Validation:
- The exact GPU combination passed the v10 runtime matrix with `916 pass / 0 fail`; the general SSR regression passed `691 pass / 0 fail`, and the octahedral normal/history codec gate passed `6 pass / 0 fail`.
- The v11 source/default gate passed `56 pass / 0 fail` after rebuilding `SelfEngineShaders`, `SelfEngineForward3D`, and `SelfEngineLightingShowcase` in Debug.
- The user confirmed that the final LightingShowcase window had no cross, ring, snow noise, exposed material base color, or camera-motion instability.
- Contract v11 changes only the CPU defaults, reverse controls, and audit version; Windows Smart App Control blocked the new executable hash despite a valid local signature, so v11 is covered by the static gate rather than falsely claimed as a launched binary.

## 2026-07-20 - FFX SSR History Needs Receiver Validation, Not UV Alone

Symptom:
- LightingShowcase glossy spheres showed distance-dependent rings and dirty waves that got worse during WASD camera translation, but calmed down when the camera stopped.

False leads:
- Treating the remaining artifact as a pure motion-vector scale problem.
- Disabling hit-position reprojection alone and assuming the denoiser history was then fully safe.

Cause:
- The FFX visible composite was trusting radiance-history UV validity too much. It needed the same receiver-side depth/normal/roughness validation that the internal SSR path already used, otherwise moving camera samples could bleed unstable history across glossy boundaries.

Control test:
- `SE_SSR_FFX_HIT_REPROJECTION_OFF=1` isolated the hit-position branch, then the receiver validation gate proved the visible composite still needed previous receiver metadata checks.

Fix:
- Kept the FFX motion-vector and hit-reprojection controls, but made the visible deferred composite route its FFX history through `SsrDeferredReceiverHistoryConfidence(...)` instead of a UV-only bypass.
- Added CSV and static-script coverage for the hit-reprojection control plus the receiver-validation contract.

Prevention:
- A third-party temporal producer is not production-ready just because its history image is valid. The visible consumer still needs receiver identity and rejection checks.

Validation:
- `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_receiver_validation_gate` passed `191 pass / 0 fail`.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -OutputDirectory tmp\ssr_regression_after_ffx_receiver_validation_gate` passed `691 pass / 0 fail`.

## 2026-07-20 - FidelityFX SSSR History Composite Needs Its Own Consumer Contract

Symptom:
- The AMD SSSR ResolveTemporal output was valid as a history producer, but the final image still sampled the internal `SSRResolved` texture.

False leads:
- Treating ResolveTemporal dispatch success as proof of visible SSR contribution.
- Reusing the internal resolved-alpha and metadata confidence contract for FFX radiance history.

Cause:
- The FFX pass chain produces `RadianceHistory` after Deferred lighting, so the visible consumer must sample the previous completed history on the next frame. That history stores radiance, not the internal `SSRResolved.a` confidence payload, and it does not match the internal `SSRHistoryMetadata` identity.

Control test:
- Run `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_deferred_composite_bridge` and require LightingShowcase plus Forward3D FFX lanes to report deferred composite requested/active/descriptor/history/source `1/1/1/1/1` and `radianceSource=4`; the internal-backend lane must keep those values at `0` and avoid `radianceSource=4`.

Fix:
- Let GBuffer binding 17 optionally bind the previous FFX `RadianceHistoryView`.
- Added an SSR control bit for the Deferred shader to treat binding 17 as FFX radiance history, bypass internal metadata confidence, and blend conservatively over probe/IBL fallback.
- Added CSV fields and a strict FFX integration gate for the Deferred composite bridge.

Prevention:
- Every temporal third-party producer needs an explicit visible consumer contract; do not infer it from successful intermediate dispatches.
- Do not share confidence/metadata semantics between independently produced reflection histories unless resource identity and payload layout are proven.

Validation:
- Debug `SelfEngineShaders`, `SelfEngineForward3D`, and `SelfEngineLightingShowcase` builds passed.
- `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_deferred_composite_bridge` passed `125 pass / 0 fail`.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -OutputDirectory tmp\ssr_regression_after_ffx_deferred_composite` passed `691 pass / 0 fail`.

## 2026-07-20 - FidelityFX SSSR ResolveTemporal Must Be Validation-Clean History

Symptom:
- The FFX ResolveTemporal bridge reported a correct CSV contract, but stderr still contained Vulkan validation errors in the FFX lanes.

False leads:
- Treating CSV pass counts and FrameGraph validation as enough proof.
- Binding Reproject `RWTexture2D<float3>` outputs to RGBA32F images without checking the SPIR-V write component count.
- Switching those resources to RGB32F without checking device sampled/storage format support.

Cause:
- Vulkan validation rejects a three-component `OpImageWrite` into a four-component RGBA image view. The current device also does not support RGB32F sampled/storage images, so the portable Vulkan bridge must keep RGBA32F resources and patch the vendored Reproject shader callbacks to write `value.xyzz`.
- The FFX Intersect lit-scene input only needs mip0, but binding the full HDR scene-color view before mip generation exposed undefined higher mips to validation.

Control test:
- Run `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_resolve_temporal_bridge` and require both FFX real-scene lanes to report contract `7`, ResolveTemporal ready `1/1/1/1/1`, dispatch/bind/history-copy `1/2/10`, and Vulkan validation diagnostics `0`; the internal-backend control must keep dispatch/binds/history-copy at `0/0/0`.

Fix:
- Added the official ResolveTemporal descriptor/resources/pipeline/dispatch bridge and wrote its output into the Reproject history resource set.
- Patched `thirdParty/fidelityfx_sssr/shaders/Reproject.hlsl` to use RGBA storage outputs with `.xyzz` stores while preserving RGB radiance semantics.
- Bound Intersect's lit-scene input to the HDR scene-color mip0 attachment view.
- Extended the FFX integration gate to fail on Vulkan validation diagnostics.

Prevention:
- Every third-party compute pass must prove both CSV resource/dispatch contracts and clean Vulkan validation logs before becoming a completed slice.
- When HLSL storage-image component counts and Vulkan image formats disagree, fix the shader/resource contract deliberately; do not rely on “larger format is safer.”

Validation:
- Debug `SelfEngineShaders`, `SelfEngineForward3D`, and `SelfEngineLightingShowcase` builds passed.
- `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_resolve_temporal_bridge` passed `121 pass / 0 fail`.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -OutputDirectory tmp\ssr_regression_after_ffx_resolve_temporal` passed `691 pass / 0 fail`.

## 2026-07-20 - FidelityFX SSSR Prefilter Must Stay An Audited Intermediate

Symptom:
- The SSSR mainline needed to advance from Reproject toward production DNSR without again exposing half-connected SSR output as visible reflection quality.

False leads:
- Treating Prefilter as a visual-quality fix by itself.
- Binding Reprojected history radiance as the current radiance input to Prefilter.

Cause:
- AMD's DNSR chain separates current radiance, reprojected history, average radiance, variance, and sample count. Prefilter consumes current Intersect radiance plus Reproject variance/average/sample-count, then writes an intermediate for ResolveTemporal.

Control test:
- Run `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_prefilter_bridge` and require both FFX real-scene lanes to report contract `6`, Prefilter dispatch `1`, descriptor binds `2`, offset `12`, and FrameGraph validation `0`; the internal-backend control must keep Prefilter dispatch/binds at `0/0`.

Fix:
- Added the official `Prefilter.hlsl` Vulkan descriptor/resources/pipeline/dispatch bridge.
- Registered `FidelityFXSSSRPrefilter` and its resource producers in FrameGraph.
- Extended RendererStats, benchmark CSV, and the strict FFX integration gate with Prefilter resource, extent, memory, offset, dispatch, and bind checks.

Prevention:
- Keep every third-party SSR/DNSR pass as an auditable producer/consumer step before connecting it to the visible image.
- Do not promote a pass to runtime-active unless all prior official passes and the new pass dispatch in both target and control scenes.

Validation:
- Debug `SelfEngineShaders`, `SelfEngineForward3D`, and `SelfEngineLightingShowcase` builds passed.
- `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_prefilter_bridge` passed `110 pass / 0 fail`.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -OutputDirectory tmp\ssr_regression_after_ffx_prefilter` passed `691 pass / 0 fail`.

## 2026-07-20 - FidelityFX SSSR Reproject Needs FrameGraph Resource Proof

Symptom:
- The official FidelityFX SSSR Reproject runtime bridge reported valid resources, descriptors, and dispatches, but the FFX backend lanes still had FrameGraph validation issues.

False leads:
- Treating a passing dispatch/resource health gate as enough proof that the pass chain was fully auditable.

Cause:
- The SSSR FrameGraph path described one monolithic adapter pass with resource names such as `GBuffer`, `DepthPyramid`, and `BlueNoise` that were not registered in the current FrameGraph. The missing refs were not asserted by the FFX integration script.

Control test:
- Run `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_reproject_bridge` and require both FFX backend lanes and the internal-backend control to report `framegraph_validation_issues=0`.

Fix:
- Split the FrameGraph description into the official `ClassifyTiles`, `PrepareIndirectArgs`, `PrepareBlueNoiseTexture`, `Intersect`, and `Reproject` passes.
- Register the FFX ray counter, indirect args, ray list, denoiser tiles, extracted roughness, blue noise, intersection output, bootstrap histories, and Reproject output images as physical resources.
- Tighten `Test-FidelityFxSssrIntegration.ps1` so FrameGraph validation cleanliness is part of the strict FFX gate.

Prevention:
- Every third-party renderer pass must be visible as producer/consumer data, not only as Vulkan dispatch counts.
- Do not let a strict integration gate pass while the FrameGraph reports missing resource refs for that same feature.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed.
- `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_reproject_bridge` passed `100 pass / 0 fail` with official pass dispatches `1/1/1/1/1`, Reproject offset `12`, extents `1280x720` and `160x90`, and FrameGraph validation `0`.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -OutputDirectory tmp\ssr_regression_after_ffx_reproject` passed `691 pass / 0 fail`.

## 2026-07-19 - FidelityFX Storage Image Format Must Match SPIR-V Declaration

Symptom:
- The first official AMD FidelityFX SSSR `ClassifyTiles.hlsl` bridge ran far enough to publish CSV stats, but Vulkan validation rejected the `g_intersection_output` storage image binding.

False leads:
- Assuming HLSL `RWTexture2D<float4>` could be backed by `VK_FORMAT_R16G16B16A16_SFLOAT` because half-float would be smaller and visually sufficient for an intermediate carrier.

Cause:
- The compiled SPIR-V declares `g_intersection_output` as an `Rgba32f` storage image. Vulkan storage image validation requires the bound image view format to match that declared format class, so `R16G16B16A16_SFLOAT` is invalid here.

Control test:
- Run `scripts\Test-FidelityFxSssrIntegration.ps1 -Strict` with `SE_SSR_BACKEND=ffx-sssr`; the FFX lanes must create and dispatch `ClassifyTiles` without storage-image validation diagnostics.

Fix:
- Bind `g_intersection_output` as `VK_FORMAT_R32G32B32A32_SFLOAT`, update the memory estimate, and keep the pass behind the data-first FFX/internal backend control matrix.

Prevention:
- For every third-party shader pass, inspect SPIR-V reflection or Vulkan validation for storage image formats before selecting lower-precision SelfEngine resources.
- Do not optimize intermediate precision until the vendor shader contract and typed descriptor requirements are proven.

Validation:
- `scripts\Test-FidelityFxSssrIntegration.ps1 -Strict -OutputDirectory tmp\ffx_sssr_classify_tiles_v3` passed `71 pass / 0 fail`.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -VerifyHoleDiagnostics -OutputDirectory tmp\ssr_refinement_after_ffx_classify_tiles` passed `861 pass / 0 fail`.

## 2026-07-19 - FidelityFX RWBuffer Maps To Vulkan Storage Texel Buffers

Symptom:
- The first AMD FidelityFX SSSR runtime bridge aborted in Debug validation while creating the `PrepareIndirectArgs.hlsl` compute pipeline.
- Validation reported descriptor type mismatches for `g_ray_counter` and `g_intersect_args`: SelfEngine provided storage buffers, but SPIR-V required storage texel buffers.

False leads:
- Assuming HLSL `RWBuffer<uint>` should map to `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` because the resource is backed by a VkBuffer.

Cause:
- DXC emits HLSL typed `RWBuffer<uint>` as `OpTypeImage` with `Dim=Buffer`, which Vulkan validates as `VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER`, not a raw/storage buffer descriptor.

Control test:
- Run `scripts\Test-FidelityFxSssrIntegration.ps1` with `SE_SSR_BACKEND=ffx-sssr`; the FFX lane should create the pipeline and dispatch after descriptor type correction, while the internal-backend lane should suppress FFX dispatch.

Fix:
- Bind the FFX ray-counter and indirect-args resources as storage texel buffers with `VK_FORMAT_R32_UINT` buffer views, while retaining transfer/indirect usage for clears and future indirect dispatch consumers.
- Add CSV fields for prepare-args resources, descriptor sets, pipeline readiness, dispatches, binds, and buffer bytes.

Prevention:
- For every third-party shader pass, verify descriptor kinds from SPIR-V/reflection or Vulkan validation before writing SelfEngine descriptor layouts.
- Do not infer Vulkan descriptor type solely from HLSL spelling or CPU resource class name.

Validation:
- `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\ffx_sssr_runtime_prepare_args_v3` passed `52 pass / 0 fail`.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -VerifyHoleDiagnostics -OutputDirectory tmp\ssr_refinement_after_ffx_prepare_args` passed `861 pass / 0 fail`.

## 2026-07-19 - SSR Trace Payload Is Not Radiance

Symptom:
- LightingShowcase glossy spheres showed white wall/floor blocks and strong reflection flicker, including while the camera was not moving, when `SE_SSR_CURRENT_HDR_SOURCE=1` was forced on.
- The same scene looked stable when production reflection fell back to probe/IBL instead of current-HDR hit radiance.

False leads:
- Treating receiver roughness trust as enough to make the current-HDR radiance source production-safe.
- Continuing to tune trace confidence and radiance filters after current-HDR had already been proven experimental.

Cause:
- `ssr_temporal.comp` used `rawInput.rgb` in the temporal neighborhood clamp as though it were reflected color. The raw SSR trace payload stores hit UV and validity, not radiance, so it could clamp history against screen-space coordinates and produce blocky, view-dependent artifacts.
- The current-HDR radiance source is still not a production default for glossy reflections; it remains a Debug opt-in source until foreground/background rejection and temporal stability are proven.

Control test:
- Force `SE_SSR_CURRENT_HDR_SOURCE=1` in LightingShowcase to reproduce the white blocks, then run the production default with current-HDR disabled and compare against probe/IBL reflection.
- Run `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -VerifyHoleDiagnostics -OutputDirectory tmp\ssr_production_fallback_static_health`.

Fix:
- Change the temporal neighborhood clamp to resolve neighbor hit radiance through each neighbor's hit UV before converting to YCoCg.
- Add a static shader contract to the SSR health gate that fails if raw trace RGB is used as radiance.
- Add frozen-scene static diagnostic ranges for SSR raw, high-confidence, temporal, and resolved coverage.
- Remove the failed current-HDR roughness-trust control path and keep production defaults on probe/IBL or completed scene-color history fallback, not current-HDR.

Prevention:
- Never treat trace payload channels as color unless the payload contract explicitly says they contain radiance.
- Do not promote current-HDR SSR radiance to the production path until data proves foreground/background rejection, stable source sampling, and static-frame coverage stability.
- Use probe/IBL/captured reflection as the stable glossy baseline while SSR matures as a diagnostically isolated enhancement layer.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed after `ssr_temporal.comp.spv` regenerated.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -VerifyHoleDiagnostics -OutputDirectory tmp\ssr_production_fallback_static_health` passed `861 pass / 0 fail`.
- Key metrics: shader contract `samplesNeighborHitRadiance=1`, `usesRawTraceRgbAsRadiance=0`; production LightingShowcase `radianceSource=2/currentHdr=0`; scene-color-history-disabled control `radianceSource=1/currentHdr=0`; current-HDR opt-in remains explicit at `radianceSource=3`.
- User accepted the real LightingShowcase production-default window as visually normal.

## 2026-07-19 - SSR Miss-History Rejection Needs A Radiance-Source-Aware Gate

Symptom:
- Adding the SSR temporal miss-history reject control made the old current-HDR debug lanes report raw SSR hits but zero temporal/resolved SSR coverage.
- Forward3D current-HDR lanes also produced zero high-trust temporal coverage, so a miss-carried-pixel comparison could not prove the new control by itself.

False leads:
- Treating the new `65536` packed control bit as a trace-step or lower-bit decode regression.
- Requiring the experimental current-HDR radiance source to produce visible SSR coverage in every health lane.

Cause:
- The new miss-reject policy correctly decays carried history on current trace misses, but the current-HDR radiance source is still experimental and disabled by default. In the current LightingShowcase/Forward3D lanes, raw hit confidence does not reliably reach the temporal radiance trust threshold.

Control test:
- Compare `SE_SSR_TEMPORAL_MISS_HISTORY_REJECT=1/0` under current-HDR enabled lanes and inspect `ssr_reconstruction_temporal_miss_history_reject_enabled`, temporal/resolved coverage, and miss-carried pixels.

Fix:
- Add `ssrTemporalMissHistoryRejectEnabled`, `SE_SSR_TEMPORAL_MISS_HISTORY_REJECT`, packed bit `65536`, temporal contract version `11`, and a shader-side history-lock decay on current misses using history validity and motion confidence.
- Update `Test-SsrRefinementHealth.ps1` so current-HDR lanes assert bounded diagnostics instead of assuming that the experimental radiance source must seed coverage.

Prevention:
- Do not use an experimental radiance source lane as proof of final SSR temporal quality. First validate the source trust contract, then validate temporal rejection/denoise behavior on top of it.

Validation:
- Debug MSBuild passed for `SelfEngineForward3D` and `SelfEngineLightingShowcase`.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -VerifyHoleDiagnostics -OutputDirectory tmp\ssr_temporal_miss_history_reject_health_v3` passed `796 pass / 0 fail`.

## 2026-07-19 - Reflection Receiver Gates Need Debug Builds And Steady-State Readiness

Symptom:
- `Test-ReflectionCaptureHealth.ps1` reported parallax/locality failures when run against Release-like staged executables, with box-projection reference masks at `0`.
- The first receiver-audit gate also failed because early LightingShowcase traversal rows had positive receiver weights before every captured cubemap resource was ready.

False leads:
- Treating Release `0` masks as a reflection algorithm regression.
- Requiring every captured frame to have all receiver cubemaps ready, even while the lane is intentionally exercising capture convergence.

Cause:
- Several reflection-capture diagnostics, including locality controls and box-projection reference-ray masks, are compiled only under `!NDEBUG`.
- Receiver weights are valid spatial data before a captured probe finishes all faces and mips; the consumer-ready assertion must be evaluated on the steady producer/consumer state rather than transient warmup rows.

Control test:
- Copy the real Debug `.blocked1` binary into a trusted staging directory and run the reflection capture gate from that `!NDEBUG` executable.
- Compare grid production, grid legacy, and LightingShowcase traversal receiver rows.

Fix:
- Extend `scripts\Test-ReflectionCaptureHealth.ps1` to import and validate the full `reflection_probe_receiver_audit_*` CSV contract.
- Add receiver-audit grid production/legacy lanes plus LightingShowcase spatial/parallax traversal receiver checks.
- Keep receiver audit opt-in by default and clean up all `SE_REFLECTION_RECEIVER_AUDIT*` and legacy blend environment variables per lane.
- Check steady weighted-probe readiness against the final receiver row, while still checking weight sums and box-projection hit masks across receiver-audit rows.

Prevention:
- Run reflection-capture health gates that rely on locality or reference-ray diagnostics with a Debug/development executable, not Release.
- Distinguish capture-convergence rows from steady-state producer/consumer assertions; otherwise data gates will flag valid warmup behavior as a rendering bug.
- Receiver/probe blend changes must publish masks, weights, normalized sums, LODs, coverage, and legacy controls before visual review.

Validation:
- Debug build produced a real `.blocked1` binary; staged as `V:\SelfEngineRunDebugReceiverAudit\SelfEngineForward3D.exe`.
- `scripts\Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict -ExecutablePath V:\SelfEngineRunDebugReceiverAudit\SelfEngineForward3D.exe -OutputDirectory tmp\reflection_probe_receiver_audit_debug_v2` passed `953 pass / 0 warn / 0 fail`.
- Receiver evidence included grid production masks `1/1/1`, grid legacy blend/energy `0/0`, LightingShowcase dominant slot `0..1`, final masks `3/7/7`, normalized sum `1`, and LODs `1.92/1.92/1.92/0`.

## 2026-07-19 - DLSS Jitter CSV Fields Follow The Inverse Projection Convention

Symptom:
- `scripts\Test-DlssVisualQa.ps1` started failing the real `default-object-motion` lane even though the renderer was applying jitter and the DLSS route was otherwise healthy.

False leads:
- Treating `temporal_upscaler_dlss_jitter_offset_*` as a same-sign echo of `temporal_jitter_pixels_*`.
- Blaming the scene or render scale before checking the AA contract helper.

Cause:
- `DlssJitterOffsetPixels()` defaults to the negated Halton jitter. The DLSS CSV field is therefore an inverse contract, not a direct sign match.
- `Assert-DlssJitterConsistency` was comparing the fields as if they were same-sign values.

Control test:
- Compare the AA contract helper in `scripts\Test-AaModeContracts.ps1` with `scripts\Test-DlssVisualQa.ps1`.
- The former already uses `Require-NumericInverse`; the latter needed the same inverse check.

Fix:
- Change the DLSS visual QA jitter assertion to require `temporal_upscaler_dlss_jitter_offset_* == -temporal_jitter_pixels_*` when jitter is applied.

Prevention:
- Whenever a renderer stat is derived through a sign-convention helper, compare the recorded fields against that helper's contract, not against intuition.

Validation:
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Test-DlssVisualQa.ps1 -SkipBuild -StrictGate -Suite default-object-motion,imported-dynamic` passed.

## 2026-07-19 - DLSS Object Motion Gate Must Read Post-Classification State

Symptom:
- DLSS quality gate reported object motion as unavailable in the real Forward3D object-motion lanes even after the renderer was already producing velocity data.

False leads:
- Treating the result as a visual DLSS quality issue.
- Tuning render scale, preset, or quality mode before checking the producer/consumer order.

Cause:
- `BuildFrameTemporalState` published `velocityObjectMotionReady = false` before `BuildGBufferCommandList` and the weighted-translucency / forward-residual motion-vector checks finished. `BuildFrameTemporalUpscaleState` and temporal stats then read that stale value.

Control test:
- Compare the real `default-object-motion` and `imported-dynamic` CSV rows before and after the fix. The gate should read `1/1/0`, the quality masks `255/255/0`, and both camera/object motion readiness bits should be `1`.

Fix:
- Refill `temporalState.velocityObjectMotionReady` after command classification and before building the upscale state and writing temporal stats.

Prevention:
- Never let DLSS quality gating consume pre-classification motion readiness. Producer classification must finish before the consumer contract is published.

Validation:
- Debug build passed.
- Direct CSV checks on `default-object-motion` and `imported-dynamic` both showed `qualityGate=1/1/0`, `qualityMasks=255/255/0`, `qualityMode=1`, `recommendedPreset=12`, and `velocity/object motion ready=1/1`.

## 2026-07-19 - Captured Probe Locality Needs Identity And Region Masks

Symptom:
- The captured-scene locality lane could prove a distant probe ignored a global light revision, but it still did not explain which local lights or renderables made the near probe dirty.
- Aggregate signatures and counts were not enough to separate local influence from a generic scene-wide invalidation.

False leads:
- Treating global revision counters or a single selected probe audit as sufficient locality proof.
- Assuming the existing affected-count fields explained both the producer identity and the spatial region.

Cause:
- The refresh contract recorded signatures and counts, but not explicit identity masks, region masks, or per-probe dirty/ignored flags.

Control test:
- Run `selective-locality-control` and `moving-rigid-refresh`; the near probe should show local-light or geometry dirty attribution while the distant probe still ignores the unrelated global revision.
- Run `SE_REFLECTION_CAPTURE_SELECTIVE_REFRESH_OFF=1` to verify the fallback clears the locality-ignore evidence.

Fix:
- Added identity and region masks for captured local lights and renderables.
- Added per-probe dirty and ignored flags plus CSV/ImGui visibility and strict script checks.

Prevention:
- For spatial cache invalidation, record both identity and region evidence before trusting a global revision signal.
- Multi-probe assertions must check per-resource dirty attribution, not only aggregate refresh counts.

Validation:
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\reflection_capture_dirty_attribution` passed `955 pass / 0 warn / 0 fail`.
- Selective locality lane recorded probe count `2`, light revision delta `9`, ignored-light count `1`, local-light dirty `1`, and dirty local-light count `5`.
- Moving-rigid lane recorded render revision delta `24`, geometry dirty `1`, and dirty renderable count `5`.

## 2026-07-19 - SSR Current HDR Radiance Is Not A Safe Default Source

Symptom:
- LightingShowcase glossy spheres showed strange skybox-like blended reflections and white block artifacts. The artifacts changed with view angle and were most visible on metal surfaces.

False leads:
- Treating the skybox image, local captured probes, or IBL cache as the direct bug.
- Further tuning SSR spatial variance clamps, trace confidence, or reflection strength after the source path was still mixed.

Cause:
- The SSR reconstruction path sampled current-frame HDR scene color as the default hit radiance source. That HDR image can contain high-contrast walls, light panels, visible-skybox/background contribution, and post-lighting results that are not a stable production reflection source for sparse SSR hits. Blending it with probe/environment fallback produced the white blocks and odd skybox-like fusion.

Control test:
- Launch the real LightingShowcase with only `SE_SSR_CURRENT_HDR_SOURCE=0`.

Fix:
- Make `ssrCurrentHdrSourceEnabled` default to off.
- Decouple the legacy `SE_SSR_SCENE_COLOR` history control from current-HDR radiance; only `SE_SSR_CURRENT_HDR_SOURCE=1` opts into the experimental current-HDR source.
- Keep the explicit enabled lane in `Test-SsrRefinementHealth.ps1` so the path remains diagnosable without becoming the production default.

Prevention:
- Do not default SSR to a post/deferred HDR color buffer as its hit-radiance source unless the source is separately validated for foreground geometry, background rejection, exposure/post ordering, mip validity, and temporal stability.
- Treat current-frame HDR sampling as a Debug comparison path until SSR has a production radiance source contract.

Validation:
- User confirmed the single `SE_SSR_CURRENT_HDR_SOURCE=0` LightingShowcase window looked normal and the white blocks disappeared.

## 2026-07-19 - SSR Consumer Stats Must Be Written After Temporal Stats

Symptom:
- LightingShowcase and Forward3D current-HDR lanes still reported stale SSR consumer fields even though the source path and image bindings were already correct.

False leads:
- Treating it as another radiance, history-lock, or probe fallback bug.
- Chasing visuals first when the failure was in the published contract.

Cause:
- `temporalConsumerSsrReady`, `sceneColorHistory*`, `radianceSource`, and `temporalConsumerSsrActive` were computed before `WriteTemporalStats`, so they could read stale temporal state.

Control test:
- Move the consumer-publication block to after `WriteTemporalStats`.
- Rerun the real LightingShowcase current-HDR lane and the Forward3D animated FBX lane.

Fix:
- Publish the SSR consumer stats only after the temporal writeback is complete.

Prevention:
- Any CSV field that summarizes a producer/consumer contract must be written after the producer's final state is settled.
- If a data gate disagrees with a valid visual window, check stat ordering before adding more rendering knobs.

Validation:
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict -VerifyHoleDiagnostics` passed `796 pass / 0 fail`.
- Current release LightingShowcase current-HDR enabled lane reported `sceneColorHistoryReady=1`, `sceneColorHistoryActive=1`, `radianceSource=3`, `reconstructionCurrentHdrSourceEnabled=1`, and `reconstructionCurrentHdrMipChainReady=1`.
- Current release Forward3D animated FBX current-HDR lane reported the same contract values, and the user accepted the real Forward3D visual window.
- The history-lock-off control still reported `reconstructionTemporalHistoryLockEnabled=0` and `holeDiagnosticsTemporalMissCarriedPixels=0`.

## 2026-07-17 - Local PCSS Needs Descriptor And Cold-Compile Contracts

Symptom:
- After adding raw-depth and hardware-comparison local-shadow sampling, Forward3D loaded its FBX model but never reached the first benchmark frame; the window remained unresponsive.

False leads:
- Treating the Ultra `12+16` receiver sample budget as a runtime GPU hang. `SE_SHADOW_QUALITY=off` still stalled before the first frame.
- Reducing the rect-light atlas scan from 64 tiles to a bounded per-light range improved the runtime contract but did not remove cold pipeline compilation time by itself.
- Adding SPIR-V `DontUnroll` controls alone; the NVIDIA driver still copied the complete local-shadow call graph into multiple lighting and debug call sites.

Cause:
- The material descriptor layout grew from 14 to 15 combined-image samplers, but several pools allocating that shared layout still reserved 14 per set. Vulkan validation reported requests for 45 descriptors from pools containing 42.
- Once the descriptor error was fixed, cold PSO creation exposed excessive driver inlining. Pipeline tracing measured approximately `11.8 s` for weighted translucency, `10.4 s` for each uncached forward render-pass variant, and `19.1 s` for deferred lighting.

Control test:
- Redirect Debug stdout/stderr and run four benchmark frames. Descriptor-pool warnings must be zero and CSV must be written.
- Set `SE_VK_PIPELINE_TRACE=1`; begin/end records distinguish cold pipeline compilation from frame execution.
- Compare LightingShowcase Low production, default Ultra production, and `SE_LOCAL_SHADOW_PRODUCTION_FILTER=0`. Fallback must preserve quality, atlas dimensions, and tile assignment while reporting active/fallback as `0/1`.

Fix:
- Define shared frame/material sampler counts and use the 15-binding material count in every pool allocating the material layout.
- Upload contiguous `first/count/requested/valid` tile ranges per local light and bound shader traversal to six tiles per light.
- Use authored point/spot source radius, rect residual source radius, nonlinear depth reconstruction, world-space blocker separation, stable rotated Poisson samples, and hardware comparison filtering in forward, deferred, and weighted-translucency consumers.
- Apply SPIR-V `DontInline` to ten measured high-level lighting/shadow functions during shader generation, validate the rewritten module, and keep pipeline tracing Debug-only. This reduced measured cold PSO work from about `51 s` to `33 s` without the steady-frame regression caused by disabling inlining globally.

Prevention:
- A descriptor-layout binding addition is incomplete until every pool allocating that layout uses the same named descriptor count and a validation-layer runtime probe reaches a real frame.
- Measure cold `vkCreateGraphicsPipelines` time after materially deepening a shader. Runtime sample budgets and PSO compile complexity are separate contracts.
- Keep local-shadow producer ranges and projection geometry versioned and CSV-auditable; never recover ownership by scanning the whole atlas in every fragment.

Validation:
- Local PCSS math/static health passed, including one identical 12-function hash across all three consumers.
- Forward3D four-tier health passed `347 pass / 0 warn / 0 fail`; the Forward3D/LightingShowcase production/fallback matrix passed `134 / 0 / 0`.
- Debug and Release Forward3D/LightingShowcase builds passed; Release contains no `SE_VK_PIPELINE_TRACE` string.
- The selected ten-function boundary measured about `11.08 ms` GPU and `24.68 ms` Debug CPU on the 60-frame Forward3D lane; forcing every function non-inline regressed those values to `16.81 ms` and `30.34 ms` and was rejected.
- Visual acceptance is pending the single full-screen LightingShowcase window.

## 2026-07-16 - Captured Probe Parallax Must Share The FrameGraph Sampling Contract

Symptom:
- Captured-scene probes had shader box-projection support but were excluded by the CPU enable predicate, so captured reflections used uncorrected directions.
- The strict capture health lanes reported a missing `SceneReflectionProbeCubemap` reference even while a selected captured probe was sampled.

False leads:
- Treating the first selected probe's `captureResourceReady` value as a summary of every selected probe.
- Relying on a plausible static reflection image instead of testing rays within and outside each probe box.

Cause:
- `ReflectionProbeBoxProjectionEnabled` rejected the `CapturedScene` source despite box projection being a scene-owned spatial property.
- FrameGraph resource registration followed the top local probe readiness, while the consumer was enabled by the selected cubemap sampling set; multi-probe selection made the two conditions disagree.

Control test:
- Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\reflection_probe_parallax`.
- The `box-parallax-traversal` lane must report captured-box, ray-hit, direction-changed, and outside-fallback masks containing `0x7`, with zero spatial and FrameGraph validation failures.

Fix:
- Enable box projection for every scene-owned probe with nontrivial box extents.
- Add a Debug-only CPU slab-ray audit that mirrors the shader behavior and publish the masks in CSV/ImGui.
- Register `SceneReflectionProbeCubemap` whenever the selected sampling set can consume it, and retain the first FrameGraph issue's stable names in benchmark CSV for diagnosis.

Prevention:
- A FrameGraph resource producer and every consumer must resolve their active state from the same per-frame contract; never summarize a multi-resource selection with a single legacy probe field.
- Spatial reflection changes require deterministic inside, changed-direction, and outside-fallback data evidence before visual acceptance.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed; both development signatures verified valid.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\reflection_probe_parallax` passed `741 pass / 0 warn / 0 fail`.
- Final parallax masks were `0x7/0x7/0x7/0x7`; spatial failure and FrameGraph validation issue counts were `0`.
- User accepted the single normal `SelfEngineLightingShowcase` visual window.

## 2026-07-17 - Optimized Tent PCF Must Be Anchored To Texel Centers

Symptom:
- Directional 5x5 tent PCF made sphere self-shadow ripples and ground bands more visible even though hardware comparison sampling and the nine-tap budget were active.

False leads:
- Disabling receiver-plane bias; this made the ripples worse.
- Treating nine optimized taps as inherently too sparse before verifying the reduction math.

Cause:
- The Castano nine-tap offsets were added to the continuous atlas UV. The derivation requires offsets relative to the lower texel center, so the runtime footprint was asymmetric and did not reconstruct the intended `1/3/4/3/1` tent weights.

Control test:
- `scripts/Test-DirectionalShadowTentMath.ps1 -Strict` reconstructs the hardware bilinear footprint and checks all four shader consumers.

Fix:
- Convert atlas UV to texel-center coordinates, derive the integer base texel and fractional phase there, and apply optimized offsets from that base in forward, deferred, GBuffer debug, and weighted-translucency paths.

Prevention:
- Every optimized hardware-PCF reduction needs an analytic footprint regression; matching tap count and normalization alone do not prove the kernel.

Validation:
- Math health reported corrected maximum weight error `0`; the old unaligned formula reports error `4`.
- User reported that the visible directional-shadow ripples were clearly reduced.

## 2026-07-17 - Stable CSM Requires Rotation-Invariant Projection Scale

Symptom:
- After fixing the tent kernel, directional shadows still flickered while the camera moved.

False leads:
- Further filter-radius or receiver-bias tuning.
- Adding a floating-point epsilon to the existing corner-derived radius quantization; this moved the scale jumps between cascades instead of removing them.

Cause:
- The stable CSM path tightly fit light-space bounds reconstructed from moving frustum corners, then independently snapped min/max bounds. Camera rotation changed the orthographic scale, and boundary rounding changed it by whole quantization steps.
- Forward3D moving-lane data showed all four cascade texel sizes changing by about `0.14%` per capture window.

Control test:
- The moving lane in `Test-CsmStabilityHealth.ps1` now requires each stable cascade texel size to remain invariant within `0.00001` relative change while benchmark camera motion advances.

Fix:
- Use a Valient-style stable cascade fit (Michal Valient, *Stable Rendering of Cascaded Shadow Maps*, ShaderX6): derive a rotation-invariant bounding sphere analytically from projection FOV and split depths, quantize its extent, and snap only the center in fixed light-space axes.
- Keep the corner-derived sphere as the scene-independent fallback for captures without a player-camera projection.

Prevention:
- A `stable snapping` flag is not proof of stability. In fixed-FOV motion, orthographic scale must be constant; only the center may move in shadow-texel increments.
- Prefer the stable sphere fit for production motion. Tight AABB fitting uses resolution more efficiently but requires temporal stabilization and is not the default fallback.

Validation:
- Forward3D and LightingShowcase CSM health both passed `116 pass / 0 warn / 0 fail`.
- All four moving-lane texel-size deltas were exactly `0` in both real scenes.
- `Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with eight animated rows and preserved off-camera caster coverage.
- The user confirmed that camera-motion shadow flicker disappeared in the full-screen real-scene window.

## 2026-07-17 - Curved Directional Receivers Need Cascade-Texel Normal Offset

Symptom:
- Close-range showcase spheres showed structured directional-shadow ripples even after the tent-PCF footprint and cascade motion stability were corrected.

False leads:
- Point, spot, and rect-light shadow filtering: disabling all local shadow passes did not reduce the artifact.
- Disabling all directional reception removed the artifact, but also removed valid directional shadows and was only an attribution control.
- Increasing a global clip-depth bias or disabling sphere casters would be scene-specific and would risk detached or missing shadows.

Cause:
- Sphere casters sampled their own quantized CSM depth on curved receivers. Receiver-plane and raster depth bias did not provide a world-space separation tied to the selected cascade resolution.
- The shader buffer also overwrote `texelWorldSizes.w` with max distance, so cascade 3 did not have a valid world-texel value available for a resolution-aware receiver offset.

Control test:
- With local shadows disabled and directional reception enabled, `SE_SHOWCASE_SPHERE_SHADOW_CASTERS_OFF=1` removed exactly seven sphere casters and the user reported that the ripple disappeared.
- `SE_DIRECTIONAL_NORMAL_OFFSET_BIAS_TEXELS=0` is the generic receiver-path control; it changes the recorded bias state without changing caster count, CSM passes, or CSM draws.

Fix:
- Apply a receiver normal offset in all four directional-shadow consumers. The world offset is the selected cascade's texel size times a bounded texel scale times `sin(acos(saturate(NdotL)))`.
- This follows the normal-offset bias used in production shadow implementations such as MJP's *A Sampling of Shadow Techniques* and Unity URP's `ApplyShadowBias`, while retaining the existing raster and receiver-plane depth biases for their separate roles.
- Use `2.0` cascade texels as the enabled quality-tier default; keep `0` as the strict identity control and clamp explicit overrides to `0..4`.
- Preserve all four `texelWorldSizes` components and expose the resolved normal-offset state through RendererStats, benchmark CSV, and `SE_DIRECTIONAL_NORMAL_OFFSET_BIAS_TEXELS`.

Production budget:
- The receiver offset adds no shadow-map samples and allocates no GPU resources. It adds bounded ALU work per sampled cascade.
- Debug GPU timestamp queries were unavailable (`gpu_available=0`), so this slice does not claim a measured millisecond delta.

Prevention:
- Directional receiver bias must scale in cascade texels, not fixed world or clip units. Curved self-shadowing needs a normal-offset component; raising global depth bias is not an equivalent fix.
- Keep all per-cascade world-texel values intact and assert the shader-facing bias state under both enabled and disabled controls.

Validation:
- `Test-DirectionalShadowNormalOffsetMath.ps1 -Strict` passed the bounded angle/texel math and all four shader contracts.
- LightingShowcase and Forward3D CSM health each passed `120 pass / 0 warn / 0 fail`; the LightingShowcase disabled control also passed `120 / 0 / 0`.
- Enabled versus disabled LightingShowcase data held at `19` casters, `4` CSM passes, `76` CSM draws, and `0` FrameGraph issues while only the normal-offset fields changed.
- `Test-Forward3DShadowRegression.ps1 -SkipBuild` passed all eight animated rows.
- In full-screen DLSS Performance validation, `1.0`, `1.5`, and `2.0` texels progressively reduced the curved self-shadow bands without visible detachment or leaking. At `2.0`, the user reported that a small residual remained only under careful extreme close-up inspection and selected it as the default.

## 2026-07-17 - Grazing Receivers Need A Bounded Light-Direction Slope Offset

Symptom:
- A small structured band remained on close curved receivers after the normal offset fix. Raising near-cascade sampling density reduced it, but made the light-to-shadow transition unnaturally sharp and exposed stair-step aliasing on ground shadows.

False leads:
- Replacing the authored shading normal with a derivative geometric normal; the user reported essentially unchanged banding.
- Reducing the LightingShowcase CSM coverage from `300m` to `60m`; cascade-zero density improved about `4.85x`, but the fixed 5x5 filter then covered too little world space and produced obvious transition aliasing.

Cause:
- The normal offset separated curved receivers along the surface normal, but the remaining grazing-angle depth error had no bounded light-direction component. Shadow-map quantization could still intersect the receiver near the self-shadow terminator.

Control test:
- Keep the scene-independent `300m` coverage, 5x5 tent PCF, and `2.0`-texel normal offset fixed. Change only `SE_DIRECTIONAL_SLOPE_OFFSET_BIAS_TEXELS` between `0` and `0.5`, with local-light shadows disabled for visual attribution.

Fix:
- Add a light-direction receiver offset scaled by cascade world-texel size and `tan(alpha)`, capped at `2x` before applying the configured texel scale. Use `0.5` texel as the enabled quality-tier default and retain `0` as the identity control.
- Apply the same contract in forward, deferred, GBuffer debug, and weighted-translucency directional-shadow consumers.
- This stays in the production receiver-bias family documented by MJP's *A Sampling of Shadow Techniques* and Unity's `ApplyShadowBias`; the project-specific tangent term is explicitly bounded instead of allowing unbounded grazing-angle displacement.

Production budget:
- The slope offset adds no shadow-map samples, passes, draws, or GPU resources. It adds bounded per-receiver ALU only; CSM work remains `4` passes and `76` draws in the accepted LightingShowcase lane.
- Debug GPU timestamps were unavailable (`gpu_available=0`), so this slice does not claim a measured millisecond delta.

Prevention:
- Do not reduce global CSM coverage to hide close-range self-shadowing. Keep coverage, world-space filter width, and receiver bias as separate quality controls, and validate a receiver fix against both curved terminators and cast-shadow edges.

Validation:
- Offset math/static contracts passed, including independent normal/slope controls and finite bounded values.
- LightingShowcase `300m` and Forward3D `60m` strict CSM lanes each passed `124 pass / 0 warn / 0 fail`; the LightingShowcase `0` identity control also passed `124 / 0 / 0`.
- Enabled and disabled data preserved `19` casters, `4` CSM passes, `76` CSM draws, and zero FrameGraph issues.
- In the single full-screen DLSS Performance window, the user reported that the discordant sphere shadow was essentially gone and the ground shadow no longer showed aliasing.

## 2026-07-17 - Directional PCSS Needs World-Space Blocker Separation

Symptom:
- The stable optimized 5x5 tent filter removed structured aliasing, but every directional shadow retained nearly uniform softness instead of contact-hardening from a finite angular light.

False leads:
- Deriving penumbra size directly from normalized shadow depth; each cascade has a different light-space depth span, so the same physical separation would resolve to a different softness.
- Rotating the Poisson pattern from frame-varying screen or camera data; this would trade structured edges for temporal shimmer.
- Trusting the PCSS-off environment control before checking it after the Forward3D production profile had been applied.

Cause:
- Comparison sampling alone cannot recover blocker depth for a blocker search, and the cascade payload did not expose the world-space depth span needed to convert receiver/blocker separation consistently.
- Forward3D reapplied production profile defaults after generic environment settings, which initially overwrote the PCSS-off control back to the Ultra value.

Control test:
- Run Ultra with its configured default, then rerun only with `SE_SHADOW_PCSS_STRENGTH=0`. The active path must report `12/16` blocker/filter samples and `28` maximum depth samples; the control must report disabled reason `1` and return to `9` tent-PCF samples.

Fix:
- Bind a non-comparison raw directional-depth sampler alongside the hardware comparison sampler and publish each cascade's world-space light-depth span.
- Compute penumbra radius from world-space receiver/blocker separation and the scene directional light's angular radius, clamp it to the quality budget, and blend contact regions back to the accepted tent-PCF baseline.
- Use a cascade-stable rotated Poisson pattern so camera motion does not change the noise pattern.
- Reapply all directional PCSS environment controls after the Forward3D production profile resolves. Make Ultra the renderer, Forward3D, and LightingShowcase default while preserving explicit quality overrides.

Prevention:
- Variable-penumbra shadows need both comparison and raw-depth resource contracts; never infer physical distance from cascade-normalized depth alone.
- Any scene/profile preset applied after generic environment settings must also preserve the subsystem's Debug isolation controls, and the disabled lane must assert actual shader cost rather than only a UI value.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed; all four affected SPIR-V shaders regenerated successfully.
- `Test-DirectionalShadowPcssMath.ps1 -Strict` passed the world-space, bounded-penumbra, stable-pattern, descriptor, and four-shader contracts.
- Final Forward3D Ultra health passed `152 pass / 0 warn / 0 fail`; LightingShowcase Ultra health passed `152 / 0 / 0`. Both reported quality `4`, PCSS active `1`, samples `12/16`, raw depth ready `1`, fallback `0`, maximum depth samples `28`, and zero FrameGraph issues.
- The PCSS-off Forward3D control passed `132 / 0 / 0` and reported strength/active/fallback/max samples `0/0/1/9`.
- `Test-Forward3DShadowRegression.ps1 -SkipBuild` captured eight animated rows and preserved off-camera shadow-caster coverage.
- In the single full-screen 2560x1440 DLSS Performance LightingShowcase window, the user reported that the overall result looked coherent with no obvious remaining artifacts.

## 2026-07-17 - Shadow Quality Tiers Need Auditable Cost Contracts

Symptom:
- Low/Medium/High/Ultra produced different images and resource sizes, but there was no single contract proving what each tier cost or whether a scene actually resolved the requested tier.
- `gpu_shadow_ms` covered only the legacy shadow pass, and the old health check incorrectly required the High/Ultra 2x2 rectangle-light projection pattern in Low/Medium.

False leads:
- Counting receiver-side PCSS samples inside shadow-map generation time; receiver filtering runs in the lighting pass and must remain a separate budget.
- Requiring every scene to use the same contact-shadow samples; LightingShowcase explicitly disables contact shadows while retaining the Ultra resource tier.
- Reporting logical depth-image bytes as Vulkan allocator usage; image extent, format, and swapchain image count do not include driver allocation overhead or alignment.

Cause:
- Quality settings, allocated resources, shader sample ceilings, and GPU timestamps were observable through unrelated fields with no versioned producer-to-consumer budget contract.
- The timestamp end marker was written after the legacy pass instead of after legacy, CSM, and local depth generation.

Control test:
- Run `scripts/Test-ShadowQualityBudget.ps1 -SkipBuild -Strict`. It exercises all four Forward3D tiers plus LightingShowcase Low and unoverridden default Ultra.
- Resource dimensions and logical bytes must match across scenes for the same tier; scene-owned effect opt-outs may reduce samples only to an explicit disabled state.

Fix:
- Add shadow budget contract version, resource validity/fallback, swapchain image count, maximum generation passes, directional/local/contact sample ceilings, and logical legacy/CSM/local depth bytes to RendererStats, CSV, and Shadow Debug.
- Move the GPU shadow timestamp boundaries around all three depth-generation paths while leaving receiver PCSS in main-lighting timing.
- Make the four-tier health gate exact, monotonic, cross-scene, and strict; Low/Medium use the intended axial two-projection rectangle pattern and High/Ultra use the 2x2 four-projection pattern.

Prevention:
- Keep generation cost, receiver sampling cost, and logical resource footprint as separate named budgets.
- A quality tier is complete only when its resolved settings, resources, fallback, and cost telemetry are data-tested in structurally different scenes.
- Treat scene-owned disabled effects as explicit bounded opt-outs, not as evidence that the engine tier silently changed.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed.
- Forward3D four-tier health passed `251 pass / 0 warn / 0 fail`; the cross-scene budget gate passed `71 / 0 / 0`.
- Logical shadow depth budgets were `96 / 540 / 1392 / 1392 MiB`; measured Forward3D generation averages were approximately `0.050 / 0.078 / 0.144 / 0.144 ms` on the RTX 5070 Ti.
- LightingShowcase without a quality override resolved `Ultra (4)`, and the user accepted the single full-screen visual window with no visible regression.

## 2026-07-07 - Local Light Tile Split Lines

Symptom:
- Small-window Forward3D scene showed horizontal or vertical brightness split lines on ground and wall.
- Fullscreen originally looked normal, then some views also showed seams.
- The seam was screen/tile aligned and changed with local light visibility.

False leads:
- Directional shadow cascade split/fade.
- Rect light local shadow projection.
- Light gizmo transparency.
- Shadow bias tuning.

Cause:
- Local-light screen tile culling was too aggressive.
- Rect lights were culled using sphere-style bounds even though their visible contribution can cover broad screen areas.
- Point/spot lights used only six axis samples for projected sphere bounds, which could underestimate tile coverage in some small-window views.

Control test:
- `SE_SHADOW_REGRESSION_LOCAL_LIGHTS_OFF=1` made the seam disappear.
- Making all local lights full-screen tile candidates made the seam disappear.
- Closing light gizmos did not remove the seam.

Fix:
- Treat rect lights as full-screen tile candidates; shader attenuation still decides actual contribution.
- Use conservative point/spot bounds with 8 projected AABB corners and a tile guard.
- Keep CPU tile assignment in `src/renderer/vulkan/renderer.cpp` and GPU compute tile culling in `assets/shaders/light_tile_cull.comp` synchronized.

Prevention:
- For screen-fixed light/shadow seams, first test local-light tile assignment, viewport/scissor, and tiled/clustered light dispatch before tuning shadow bias or cascade settings.
- When fixing tiled light culling, update both CPU and GPU culling paths.

Validation:
- User reported the seam disappeared with all local lights as full-screen candidates.
- User reported the conservative final version was basically normal and the split line did not appear.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `maxLocalShadowRecordedTilePasses = 8`, `animatedRows = 8`, and no skipped local shadow tiles.

## 2026-07-07 - Rect Light Shadow Versus Rect Light Illumination

Symptom:
- A wall showed a large vertical lighting discontinuity while testing rect lights.

False leads:
- Rect light local shadow bias or atlas sampling alone.

Cause:
- With `SE_RECT_LIGHT_SHADOWS_OFF=1`, the seam still appeared in rect-only tests, proving rect illumination tile assignment was sufficient to cause the artifact.

Control test:
- Rect-only scene with rect shadows disabled still showed the seam before the tile assignment fix.
- Rect-only scene with rect lights as full-screen tile candidates made the seam basically disappear.

Fix:
- Do not rely on sphere-projected tile bounds for rect light illumination.

Prevention:
- Separate "light illumination exists in tile" from "shadow sample exists" before changing shadow settings.

Validation:
- User confirmed rect-only, shadows-on validation no longer brought the split line back after the tile fix.

## 2026-07-07 - Real Scene Requirement For DLSS And Shadow QA

Symptom:
- Static or simplified scene tests looked stable, while the real Forward3D animated FBX scene still had jitter, shadow issues, or missing animation/texture behavior.

False leads:
- Treating a static scene as representative for temporal AA, DLSS, skinned animation, or dynamic shadow cache behavior.

Cause:
- Temporal AA/DLSS and skinned shadow bugs depend on real camera matrices, animation, velocity, render scale, and shadow command paths.

Control test:
- Validate from `D:\VSproject\SelfEngine\build\Debug` by launching `SelfEngineForward3D.exe` directly.
- Use the real shadow regression Forward3D scene with animated `Fist Fight B.fbx`.

Fix:
- Use one-window-at-a-time visual QA in the real scene for user-facing rendering bugs.

Prevention:
- Do not declare DLSS/TAA/shadow fixes based only on a static toy scene.
- For DLSS SR quality, test near fullscreen or target display resolution; small windows can exaggerate blur and aliasing.

Validation:
- User accepted DLSS/TAA and shadow improvements only after real-scene visual checks.

## 2026-07-07 - DLSS Hot-Switch From TAA Jitter

Symptom:
- Cold-start DLSS/DLAA was stable, but starting in Native TAA and pressing F6 into DLSS DLAA caused severe jitter in the real Forward3D scene.

False leads:
- Tuning DLSS sharpness, SR scale, or shadow settings did not explain why cold-start DLSS was stable while hot-switch DLSS was not.

Cause:
- TAA startup did not request DLSS Vulkan/NGX requirements before device creation, so hot-switching entered a half-enabled upscaler path.
- DLSS feature-create warmup also marked temporal upscale output initialized before a real output existed, so the first real evaluate could miss reset.

Control test:
- Start `SelfEngineForward3D.exe` with `SE_FORWARD3D_AA_MODE=taa`, switch once with F6 to `DLSS DLAA L`, and compare against cold-start DLAA.

Fix:
- Request `SE_ENABLE_DLSS_VULKAN_EXTENSIONS=1` and `SE_UPSCALER_PLUGIN=dlss` before Vulkan creation even when startup mode is Native TAA.
- Mark temporal upscale output initialized only when DLSS reports output ready.
- Force DLSS reset on first real evaluate after output is uninitialized or the feature was recreated.

Prevention:
- Any runtime AA mode switch into DLSS must validate Vulkan requirements, output initialization, and first-evaluate reset as one contract.

Validation:
- User confirmed TAA to DLAA hot-switch looked normal in the real Forward3D scene.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-AaModeContracts.ps1 -ExecutablePath build\Debug\SelfEngineForward3D.exe -OutputDirectory tmp\aa_mode_contracts_after_gizmo_fix_retry -TimeoutSeconds 90` passed.

## 2026-07-07 - Transparent Light Gizmos Jitter Under DLAA

Symptom:
- After the TAA to DLAA hot-switch fix, the main scene was stable but point/spot/rect light visualization geometry still jittered under DLAA.
- Native TAA did not show the same visible gizmo jitter.

False leads:
- The remaining artifact looked like the earlier global DLSS jitter, but only affected the light visualization meshes.

Cause:
- Light gizmos used `Blend`/`Transparent` materials and entered the weighted-translucency path before DLSS evaluate.
- As semi-transparent debug geometry, they did not provide the same stable depth/GBuffer contract as opaque scene geometry, making DLAA temporal reconstruction visibly unstable on their edges.

Control test:
- Change only `PointLightGizmoMaterial`, `SpotLightGizmoMaterial`, and `RectLightGizmoMaterial` from transparent blend to opaque deferred materials.
- If the gizmos stop jittering while the rest of DLAA remains stable, the transparent gizmo path is the cause.
- Move the same transparent gizmos to a post-DLSS overlay scene; if they remain stable, the final fix can preserve translucency without entering DLSS input.

Fix:
- Render light gizmos through a separate 3D overlay scene after DLSS/post-source composite, with alpha blending restored.
- Keep overlay visualizers out of the main scene GBuffer, weighted translucency, DLSS mask, and temporal upscale input paths.
- Do not let overlay scene queue building overwrite the main scene shadow caster queue or shadow descriptor sets.

Prevention:
- Debug/editor visualization geometry should render after DLSS as overlay, or provide full opaque temporal inputs; do not feed thin semi-transparent gizmos into DLSS as if they were stable scene surfaces.
- Overlay render queues must not participate in shadow queue construction unless they explicitly own a separate shadow path; otherwise animated main-scene casters can disappear from shadow diagnostics.

Validation:
- User confirmed the light gizmos basically stopped jittering.
- User confirmed the transparent overlay gizmos looked normal and did not jitter.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed after fixing overlay shadow-queue interference.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-AaModeContracts.ps1 -ExecutablePath build\Debug\SelfEngineForward3D.exe -OutputDirectory tmp\aa_mode_contracts_after_overlay_shadow_fix -TimeoutSeconds 90` passed.

## 2026-07-07 - Local Shadow Isolation Controls

Purpose:
- Add internal controls for isolating which local light shadow causes an artifact.

Controls:
- `SE_POINT_LIGHT_SHADOWS_OFF=1` or `SE_LOCAL_SHADOW_POINT_OFF=1`: keep point lights illuminating, but skip point-light shadow tiles.
- `SE_SPOT_LIGHT_SHADOWS_OFF=1` or `SE_LOCAL_SHADOW_SPOT_OFF=1`: keep spot lights illuminating, but skip spot-light shadow tiles.
- `SE_RECT_LIGHT_SHADOWS_OFF=1` or `SE_LOCAL_SHADOW_RECT_OFF=1`: keep rect lights illuminating, but skip rect-light shadow tiles.
- `SE_LOCAL_SHADOW_DEBUG_LIGHT_INDEX=N` or `SE_LOCAL_SHADOW_ONLY_LIGHT_INDEX=N`: generate local shadow tiles only for local light index `N`; use `-1` for normal behavior.

Evidence:
- With `SE_LOCAL_SHADOW_DEBUG_LIGHT_INDEX=0`, the shadow regression scene records 6 local-shadow tiles, matching one point light cubemap.
- With `SE_POINT_LIGHT_SHADOWS_OFF=1`, the shadow regression scene records 2 local-shadow tiles, matching spot + rect lights.

Prevention:
- When a local-shadow artifact appears, isolate by light index or light type before changing global local shadow bias, PCF, PCSS, or face blending.

## 2026-07-07 - Rect Local Shadow Bias Scale Control

Symptom:
- Rect-light local shadows needed tuning without changing point/spot shadow bias or rebuilding shader constants for every trial.

False leads:
- Raising global local shadow bias, which can hide acne but also damages point/spot contact quality.

Cause:
- Rect-light shadow sampling previously used a shader-side hardcoded bias multiplier, so experiments could not be isolated to rect lights.

Control test:
- Run the shadow regression scene once with defaults and once with `SE_RECT_SHADOW_BIAS_SCALE=4`; compare `local_shadow_rect_bias_scale` in the benchmark CSV.

Fix:
- Added `VulkanShadowSettings::rectLightShadowBiasScale`, uploaded it through local shadow soft controls, and used it only for rect-light local shadow visibility in forward, deferred, and weighted translucency shaders.
- Added `SE_RECT_SHADOW_BIAS_SCALE` and `SE_LOCAL_SHADOW_RECT_BIAS_SCALE` for runtime tuning, plus CSV/ImGui reporting.

Prevention:
- Keep per-light-kind local shadow tuning separate before changing shared bias, PCF, PCSS, or face blend values.

Validation:
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed.
- Default CSV recorded `local_shadow_rect_bias_scale = 8`.
- Override run with `SE_RECT_SHADOW_BIAS_SCALE=4` recorded `local_shadow_rect_bias_scale = 4`, `local_shadow_recorded_tile_passes = 8`, and `local_shadow_cache_skipped_tiles = 0`.

## 2026-07-07 - Per-Light Local Shadow Filter Controls

Symptom:
- Local shadow tuning needed to target point, spot, or rect lights independently without degrading the other light types.

False leads:
- Adding new environment variables was not enough because Forward3D reapplies the production shadow profile after the renderer's general environment settings.

Cause:
- Local shadow filtering was a single shared `filterControls`/`softShadowControls` path.
- Forward3D production defaults could overwrite explicit per-light environment overrides unless those overrides were applied after the production profile.

Control test:
- Run the shadow regression scene with `SE_LOCAL_SHADOW_RECT_PCF_RADIUS=0.75` and `SE_LOCAL_SHADOW_RECT_PCSS_STRENGTH=0.05`.
- Confirm CSV changes only `local_shadow_rect_pcf_radius` and `local_shadow_rect_pcss_strength`; point and spot fields remain at production defaults.

Fix:
- Added point/spot/rect local shadow filter settings for bias min, bias slope, PCF radius, PCF kernel radius, and PCSS strength.
- Uploaded per-light-kind filter controls through the local shadow SSBO and selected them by `lightKind` in forward, deferred, and weighted translucency shaders.
- Reapplied explicit Forward3D local-shadow environment overrides after production shadow defaults.
- Added CSV/ImGui reporting and regression-script cleanup for the new environment variables.

Prevention:
- When adding Forward3D tuning env vars, verify the final value in CSV after all scene/profile defaults have been applied.
- Keep production presets as defaults only; explicit debug overrides must win last.

Validation:
- Debug build succeeded and regenerated the three affected shader SPIR-V files.
- Shadow regression passed with `animatedRows = 8`, `maxLocalShadowRecordedTilePasses = 8`, and `maxLocalShadowSkippedTiles = 0`.
- Rect-only override check recorded point/spot PCF radius `2.4`, point/spot PCSS `0.28`, rect PCF radius `0.75`, and rect PCSS `0.05`.
- Shared override check with `SE_LOCAL_SHADOW_PCF_RADIUS=1.25` recorded point/spot/rect PCF radius all equal to `1.25`.

## 2026-07-07 - Shadow Debug Overlay For Real-Scene QA

Symptom:
- Shadow artifacts were repeatedly hard to classify by eye: cascade split lines, local-light tile seams, local-shadow cache issues, contact noise, and debug-gizmo interactions could look similar.

False leads:
- Tuning bias or filter values before checking which shadow path was actually active.
- Looking only at the giant performance panel, where shadow and light-tile signals were too buried for quick control-variable testing.

Cause:
- The project had the required stats, but they were scattered across the main ImGui window and performance block instead of grouped by shadow subsystem.

Control test:
- Add a dedicated `Shadow Debug` ImGui window without changing render settings or shadow passes.

Fix:
- Group quick shadow debug views, CSM stats, local shadow atlas/cache/filter stats, contact shadow stats, light tile stats, debug-pass counts, and warning conditions in one window.
- Keep the main scene and overlay rendering paths unchanged.

Prevention:
- Before changing shadow quality, bias, PCF, PCSS, cache, or tile-culling logic, open the `Shadow Debug` window and identify whether the active warning/stat points to CSM, local shadows, contact shadows, or light tiles.

Validation:
- Debug build of `SelfEngineForward3D.exe` succeeded.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, and `maxLocalShadowRecordedTilePasses = 8`.

## 2026-07-07 - Off-Camera Shadow Casters Disappearing

Symptom:
- A caster's visible mesh left the camera view, but its shadow was still inside the camera view and disappeared with the mesh.

False leads:
- Shadow bias, PCF, cascade split, or contact-shadow tuning; the symptom was visibility-queue ownership, not filtering.

Cause:
- Forward3D built the main render queue with the camera frustum, then built the shadow queue from that already-culled main queue.
- Off-camera objects therefore never reached the shadow pass, even when their projected shadows were visible.

Control test:
- Add off-camera casters to the shadow regression scene.
- The main queue must cull them, while the shadow queue must still include them.

Fix:
- Build the Forward3D shadow queue directly from the full scene with `shadowCastersOnly = true` and no main-camera frustum.
- Keep main-camera culling only for the visible main render queue.

Prevention:
- Never derive shadow casters from the camera-visible queue; shadow visibility is a light-space problem and must start from scene shadow casters.

Validation:
- Debug build succeeded.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `maxMainCulled = 2`, `maxMainVisible = 7`, and `maxShadowVisible = 8`.

## 2026-07-08 - Per-Pass Shadow Command Culling

Symptom:
- After preserving off-camera shadow casters, the global shadow queue was correct but every CSM cascade and every local-shadow tile could still record the full shadow-caster list.

False leads:
- Returning to camera-frustum culling would reduce draws but reintroduce disappearing off-camera shadows.

Cause:
- Shadow visibility was fixed at the global candidate-queue level, but command recording did not yet cull per cascade or per local-light tile.

Control test:
- Keep `shadow_visible` larger than `main_visible` in the shadow regression scene, while `local_shadow_recorded_draws` must be lower than `local_shadow_recorded_tile_passes * shadow_visible`.

Fix:
- Build per-cascade and per-local-shadow-tile command lists from the full shadow-caster queue using each pass's shadow view-projection clip volume.
- For local shadows, also reuse point-face and light-radius relevance tests before clip-volume testing.
- Report actual recorded draws in shadow bind stats and show actual/candidate draw counts in the Shadow Debug panel.

Prevention:
- Preserve the full shadow-caster candidate queue for correctness, then cull at each shadow pass/tile. Do not use main-camera visibility as a shadow-caster source.

Validation:
- Debug build succeeded.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `maxShadowVisible = 8`, `maxLocalShadowRecordedTilePasses = 8`, `maxLocalShadowRecordedDraws = 37`, and `fullLocalShadowDrawEstimate = 64`.

## 2026-07-08 - Forward3D Shadow Quality Production Tiers

Symptom:
- The accepted Forward3D shadow look needed to become a reproducible quality tier instead of one loose set of tuned values.
- Future tuning also needed a visible cost summary so quality changes could be judged against CSM/local-shadow pass counts.

False leads:
- Treating Low/Medium/High/Ultra as generic shadow defaults; the real Forward3D production scene needs narrower cascade distances and softer local filtering than the engine-wide defaults.
- Judging a tier only by visual softness without checking shadow pass counts or off-camera caster correctness.

Cause:
- The production scene's accepted baseline was effectively a High-quality profile: stable cascades, 4 CSM cascades at 60m, directional 5x5 PCF with moderate PCSS, and stronger local-shadow face blending.
- Medium and Ultra needed to move around that baseline without changing the core shadow command path.

Control test:
- Run the real Forward3D shadow regression with only `SE_SHADOW_QUALITY` changed to `low`, `medium`, `high`, or `ultra`.
- Confirm the CSV records the intended CSM range, filter settings, and pass/draw counts while preserving `shadow_visible > main_visible`.

Fix:
- Added production overrides for Forward3D shadow quality tiers.
- Low uses 1 non-cascade directional shadow range at 45m, directional 3x3 PCF, local PCF 0.75, no local PCSS, face blend 0.35.
- Medium uses 3 cascades at 55m, directional 3x3 PCF, no directional PCSS, local PCF 1.4, local PCSS 0.08, face blend 0.60.
- High preserves the accepted 60m production baseline: 4 cascades, directional 5x5 PCF, PCSS 0.35, local PCF 1.4, local PCSS 0.0, face blend 0.85.
- Ultra extends to 75m with stronger softening: directional PCSS 0.45, local PCF 2.8, local PCSS 0.38, face blend 0.92, contact 8 steps.
- The Shadow Debug window now shows quality and cost summaries next to warnings and pass stats.

Prevention:
- When changing shadow quality presets, validate the final applied values from CSV or Shadow Debug after production overrides, not just the enum default values.
- Keep High as the user-accepted production baseline unless a real-scene visual window confirms a better replacement.

Validation:
- Debug build of `SelfEngineForward3D.exe` succeeded.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild -ShadowQuality low -OutputDirectory tmp\shadow_closure_low` passed with `shadow_cascade_max_distance = 45`, `shadow_cascade_atlas_passes = 1`, `shadow_cascade_atlas_draws = 8`, `local_shadow_point_pcf_radius = 0.75`, `local_shadow_point_pcss_strength = 0`, and `shadow_contact_steps = 2`.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild -ShadowQuality medium -OutputDirectory tmp\shadow_closure_medium` passed with `shadow_cascade_max_distance = 55`, `shadow_cascade_atlas_passes = 3`, `shadow_cascade_atlas_draws = 22`, `local_shadow_point_pcf_radius = 1.4`, `local_shadow_point_pcss_strength = 0.08`, and `shadow_contact_steps = 4`.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild -ShadowQuality high -OutputDirectory tmp\shadow_closure_high` passed with `shadow_cascade_max_distance = 60`, `shadow_cascade_atlas_passes = 4`, `shadow_cascade_atlas_draws = 30`, `shadow_pcss_strength = 0.35`, `local_shadow_point_pcf_radius = 1.4`, `local_shadow_point_pcss_strength = 0`, and `shadow_contact_steps = 6`.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild -ShadowQuality ultra -OutputDirectory tmp\shadow_closure_ultra` passed with `shadow_cascade_max_distance = 75`, `shadow_cascade_atlas_passes = 4`, `shadow_cascade_atlas_draws = 30`, `shadow_pcss_strength = 0.45`, `local_shadow_point_pcf_radius = 2.8`, `local_shadow_point_pcss_strength = 0.38`, and `shadow_contact_steps = 8`.
- All four closure runs recorded `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, `maxMainCulled = 2`, `maxMainVisible = 7`, `maxShadowVisible = 8`, and `maxLocalShadowRecordedDraws = 37`.
- Real-scene visual validation for High production is complete after the local-shadow banding adjustment; Medium and Ultra still need one-window visual checks if their presentation quality matters.

## 2026-07-08 - Local Shadow Close-Range Banding

Symptom:
- At close camera distances, soft local shadows under the sphere showed visible concentric or stair-stepped bands in the penumbra.
- Directional CSM shadows still had a tiny amount of banding, but it was nearly negligible compared with the local-shadow contribution.

False leads:
- Contact shadows: disabling contact shadows alone did not remove the visible local-shadow bands.
- Local PCSS strength: setting `SE_LOCAL_SHADOW_PCSS_STRENGTH=0` reduced one variable but still left obvious banding.

Cause:
- High production local shadows used a large PCF radius (`2.4`) with only a 5x5 fixed sample grid.
- That produced too few visibility levels across a wide penumbra, so close inspection revealed discrete rings.

Control test:
- Disable point/spot/rect local shadows while keeping CSM enabled; the banding became nearly negligible.
- Keep local shadows enabled, set local PCSS to `0`, and lower local PCF radius to `1.4`; the user judged the remaining artifact acceptable.
- Restore contact shadows with the same local PCF settings; the result was still acceptable.

Fix:
- Change Forward3D High production local-shadow defaults to `localPcfRadius = 1.4`, `localPcfKernelRadius = 2`, and `localPcssStrength = 0.0`.
- Keep contact shadows enabled for production High.

Prevention:
- Do not increase local-shadow PCF radius beyond what the current fixed sample count can support. If a softer local penumbra is needed, add a better sampling pattern or more samples before raising radius.
- Validate local-shadow softness at close range, not only from normal gameplay distance.

Validation:
- User confirmed `SE_LOCAL_SHADOW_PCF_RADIUS=1.4` and `SE_LOCAL_SHADOW_PCSS_STRENGTH=0` made the close-range banding acceptable.
- Debug build of `SelfEngineForward3D.exe` succeeded.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild -ShadowQuality high -OutputDirectory tmp\shadow_closure_high` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, `maxMainCulled = 2`, `maxShadowVisible = 8`, and `maxLocalShadowRecordedDraws = 37`.
- CSV confirmed `local_shadow_point_pcf_radius = 1.4`, `local_shadow_point_pcss_strength = 0`, `local_shadow_spot_pcf_radius = 1.4`, `local_shadow_rect_pcf_radius = 1.4`, and `shadow_contact_strength = 0.48`.

## 2026-07-08 - Lighting Showcase Black Room

Symptom:
- The lighting showcase looked like a nearly black box even with many visible strip and area lights.
- Wall-mounted strip lights were visible, but nearby walls and the room structure were hard to read.

False leads:
- Raising exposure, bloom, or visible emitter intensity alone.
- Assuming emissive light meshes would automatically bounce light onto surrounding walls.
- Treating the issue as a rect-light tile or shadow bug before checking material albedo.

Cause:
- The showcase floor and backdrop materials were near-black after texture texel color and `baseColorFactor` multiplied together.
- Their occlusion strength was also very high, which suppressed indirect/probe contribution.
- The engine does not provide real-time GI from emissive geometry, so visible lamp meshes are not a substitute for actual lights or readable base materials.

Control test:
- Change only `ShowcaseCharcoalMaterial` and `ShowcaseBackdropMaterial` to neutral gray albedo and lower occlusion strength.
- Keep the same light layout and renderer lighting settings.

Fix:
- Raised showcase charcoal/backdrop texels and base colors to neutral gray.
- Reduced their occlusion strength so direct, ambient, and probe lighting can remain visible.

Prevention:
- Before tuning exposure or adding more lights, inspect material effective albedo: texture color multiplied by base color.
- Use a neutral diagnostic room when validating lighting logic, then art-direct the final mood after the system is readable.

Validation:
- User confirmed the lighting showcase became clearly readable after the material-only change.

## 2026-07-08 - Lighting Showcase Lamp Fixtures

Symptom:
- The lighting showcase became readable, but visible area-light geometry still looked like large debug panels instead of believable lamps.
- The oversized visible emitters distracted from material and shadow evaluation.

False leads:
- Making the raw rect-light visualizers brighter or larger.
- Treating visible emitter geometry as the same thing as the authored light fixture.

Cause:
- Rect lights are lighting primitives; their full lighting extent is not always a good visible mesh.
- The scene needed separate authored fixture models: housings, diffusers, wall backplates, caps, stands, and bases.

Control test:
- Keep actual `RectLight` contribution while replacing only the visible geometry and layout.
- Validate in the real `build\Debug\SelfEngineForward3D.exe` lighting showcase window.

Fix:
- Replaced naked rect-light panels with wall strip fixtures, side softboxes, an overhead diffuser, and low bounce fixtures.
- Added a neutral metallic `ShowcaseLampFixtureMaterial`.
- Reorganized the lights into a readable studio/showroom layout.

Prevention:
- Keep debug light visualizers separate from art-directed visible lamp fixtures.
- For presentation scenes, model fixtures independently from light volume size and direction.

Validation:
- User confirmed the arrangement looked good and should be used as the project showcase scene going forward.

## 2026-07-08 - Showcase Reflection Cubemap Artifacts

Symptom:
- Metallic and glossy showcase spheres looked semi-transparent, with red/green/blue internal-looking blobs.
- After disabling cubemap sampling, the colored blobs disappeared but a six-sided room-box imprint remained.

False leads:
- Treating the spheres as transparent or transmissive materials.
- Blaming SSR alone; disabling SSR did not remove the artifact.

Cause:
- The showcase reflection path mixed procedural environment with IBL cubemap samples even when local cubemap sampling was disabled.
- On low-roughness spheres, the sampled cubemap/box projection read as interior colored structure or six-face room shadows.

Control test:
- `SE_REFLECTION_PROBE_CUBEMAP=0` removed the colored blobs but left box projection artifacts.
- `SE_REFLECTION_PROBE_FALLBACK=0` removed reflections entirely, proving the issue was reflection-probe/environment input rather than material transparency.

Fix:
- When cubemap sampling is disabled, `GlobalEnvironmentRadiance` in deferred, forward, and weighted-translucency shaders now uses the smooth procedural environment without mixing in irradiance/prefiltered cubemap samples.
- Lighting showcase defaults to reflection fallback enabled but cubemap sampling disabled, preserving clean reflections without cubemap artifacts.

Prevention:
- Do not judge glossy material quality from cubemap-fed reflection probes until the cubemap source is known to be authored/prefiltered and visually appropriate.
- For neutral material showcase scenes, prefer smooth procedural environment reflections over noisy placeholder cubemaps.

Validation:
- User confirmed reflections returned while the colored blobs and six-sided box artifacts disappeared.

## 2026-07-08 - Showcase Reflection Specular Speckles

Symptom:
- Low-roughness showcase spheres and some glossy objects showed small black speckles that changed with camera angle.
- The artifacts remained after local shadows, contact shadows, and SSAO were isolated.

False leads:
- Treating the dots as local-light shadow acne.
- Tuning contact shadows or SSAO first.
- Removing all reflections, which hid the issue but destroyed material quality.

Cause:
- The showcase reflection-probe specular contribution was too strong for the neutral procedural reflection fallback and glossy material probes.
- On low-roughness surfaces, the reflection input compressed into high-contrast angle-dependent speckles.

Control test:
- `SE_REFLECTION_PROBE_FALLBACK=0` or `SE_REFLECTION_PROBE_SPECULAR_INTENSITY=0` removed the dots, proving the issue was reflection-probe specular rather than shadows.
- `SE_REFLECTION_PROBE_SPECULAR_INTENSITY=0.10` made the dots nearly imperceptible while preserving acceptable material quality.

Fix:
- Lighting showcase now defaults `reflectionProbeSpecularIntensity` to `0.10`.
- Renderer supports `SE_REFLECTION_PROBE_DIFFUSE_INTENSITY` and `SE_REFLECTION_PROBE_SPECULAR_INTENSITY` overrides for isolated probe tuning.

Prevention:
- For glossy material showcase scenes, isolate reflection-probe diffuse/specular intensity before changing shadow bias, contact shadows, or SSAO.
- Avoid judging material quality with a reflection intensity that is stronger than the authored or procedural environment can support cleanly.

Validation:
- User confirmed the black dots were nearly impossible to distinguish by eye and material quality remained acceptable.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, and `maxLocalShadowRecordedDraws = 37`.

## 2026-07-08 - Showcase Rect-Shadow Ring On Material Spheres

Symptom:
- The lighting showcase material spheres showed a dark circular ring or environment-like wave on the surface.
- The ring was most visible from specific angles even after reflection speckles were reduced.

False leads:
- Lowering reflection-probe specular intensity alone.
- Raising rect-light shadow bias scale to very high values.

Cause:
- Showcase material spheres were casting into rect-light local shadow maps and then receiving that shadow on their own glossy curved surface.
- The current rect-light local-shadow path can produce a visible self-shadow ring on hero material probes.

Control test:
- `SE_RECT_LIGHT_SHADOWS_OFF=1` removed the ring.
- `SE_SHOWCASE_SPHERE_SHADOW_CASTERS_OFF=1` removed the ring while keeping rect-light illumination.
- Raising `SE_RECT_SHADOW_BIAS_SCALE` to `16` or `32` did not reliably remove it.

Fix:
- Lighting showcase spheres were temporarily defaulted to non-shadow-casting presentation probes.
- `SE_SHOWCASE_SPHERE_SHADOW_CASTERS_ON=1` or `SE_LIGHTING_SHOWCASE_SPHERE_SHADOW_CASTERS_ON=1` restored sphere shadow casting for deeper engine tests.
- Existing `*_SPHERE_SHADOW_CASTERS_OFF` controls forced the presentation behavior.

Prevention:
- For material-presentation spheres, keep hero probes from self-casting local rect-light shadows unless the rect-light receiver path has been explicitly validated.
- When a surface ring follows light/shadow geometry, isolate caster participation before increasing global bias.

Validation:
- User confirmed the environment wave/ring was clearly gone and the overall showcase result was acceptable.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, `maxMainCulled = 2`, and `maxShadowVisible = 8`.

## 2026-07-09 - Showcase Sphere Shadow Casting Restored

Symptom:
- After simplifying the lighting showcase, cubes cast clear shadows but spheres appeared to cast little or no shadow.

False leads:
- Treating this as a sphere shadow-map or primitive bounds bug.

Cause:
- Showcase spheres were still defaulting to non-shadow-casting from the earlier glossy self-shadow ring workaround.

Control test:
- `SE_SHOWCASE_SPHERE_SHADOW_CASTERS_ON=1` brought sphere shadows back.
- `SE_SHOWCASE_PLAIN_SPHERES=1` made the material bright enough to judge the old ring; the user reported it was basically not obvious.

Fix:
- Restore sphere shadow casting as the lighting showcase default.
- Keep `SE_SHOWCASE_SPHERE_SHADOW_CASTERS_OFF=1` / `SE_LIGHTING_SHOWCASE_SPHERE_SHADOW_CASTERS_OFF=1` as the one-variable rollback control.

Prevention:
- Before diagnosing missing object shadows, check whether the showcase scene intentionally disabled caster participation for presentation reasons.
- Validate ring-like artifacts on a bright diagnostic material before disabling caster participation globally.

Validation:
- Default showcase contract with probe grid enabled reported `shadow_visible = 19` and `shadow_cascade_atlas_draws = 76`.
- OFF control reported `shadow_visible = 12` and `shadow_cascade_atlas_draws = 48`.

## 2026-07-08 - Authored Equirectangular Cubemap Startup Stall

Symptom:
- After connecting `assets/skybox/bk.jpg` as the lighting showcase reflection input, the Debug Forward3D window opened white and stayed Not Responding for a long time.
- The process consumed CPU during startup, so this was not a renderer crash; it was synchronous reflection-probe preprocessing.

False leads:
- Treating the hang as an app launch failure or GPU presentation issue.
- Assuming the equirectangular source can keep its full derived face resolution during Debug startup.

Cause:
- `bk.jpg` is a 6144x3072 equirectangular image. The loader derived a 1536px cubemap face from the source height and then synchronously converted and GGX-prefiltered every mip at startup.
- Even a 512px cap was still too heavy for a responsive Debug launch with the current CPU prefilter path.

Control test:
- `SE_REFLECTION_PROBE_EQUIRECT_FACE_SIZE=128` made the same Debug scene responsive within the startup window.

Fix:
- Equirectangular authored cubemap conversion now uses a capped face-size path.
- The default cap is `128`, with `SE_REFLECTION_PROBE_EQUIRECT_FACE_SIZE` available for isolated quality tests from `64` to `1024`.

Prevention:
- Never feed a high-resolution equirectangular image directly into synchronous startup prefiltering without an explicit face-size cap.
- For higher IBL quality, move prefiltering offline or onto a background/GPU path before raising the runtime default.

Validation:
- `MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo` passed.
- Launching `build\Debug\SelfEngineForward3D.exe` with the lighting scene and no face-size override reported `Responding=True` after 8 seconds.

## 2026-07-08 - Stable Skybox But Blurry Background

Symptom:
- After the DLSS/TAA skybox stabilization fix, the background no longer jittered and room edges were stable, but the visible skybox looked very blurry.

False leads:
- Further tuning temporal jitter, DLSS sharpness, or skybox blur.
- Raising `SE_REFLECTION_PROBE_EQUIRECT_FACE_SIZE`, which would reintroduce the synchronous startup stall.

Cause:
- The visible skybox was sampling `localReflectionProbeMaps[0]`, the same low-resolution cubemap path used for reflection/IBL.
- The authored equirectangular source was intentionally capped to a 128px cubemap face for responsive Debug startup, so using that cubemap as visible background made the sky stable but soft.

Control test:
- Keep the accepted hybrid temporal skybox path, but replace only the visible skybox color source with a direct 2D equirectangular sample from `assets/skybox/bk.jpg`.

Fix:
- Added frame descriptor binding 12 as `visibleSkyboxTexture`.
- Renderer loads `assets/skybox/bk.jpg` as a high-resolution `VulkanTexture2D` for visible skybox rendering, with a 1x1 fallback texture.
- `deferred_lighting.frag` and `hdr_composite.frag` now sample the visible skybox via equirectangular UVs instead of the low-resolution reflection cubemap.
- Reflection probes and IBL keep the capped/pre-filtered cubemap path.

Prevention:
- Do not reuse low-resolution reflection or IBL cubemaps for first-person visible backgrounds.
- For skybox issues, distinguish temporal placement from resource resolution: stable-but-blurry usually points to the visible texture source, not DLSS/TAA jitter.

Validation:
- `MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo` passed.
- User confirmed the real Debug lighting scene looked acceptable after the high-resolution visible skybox path was connected.

## 2026-07-08 - Global IBL And Local Probe Cubemap Contract

Symptom:
- Reflection/material quality was acceptable only after repeated scene-specific tuning, and prior reflection artifacts could return when authored/local probe cubemap state changed.
- Local probe cubemap readiness implicitly influenced global IBL sampling, making it hard to tell whether artifacts came from visible skybox, global environment, or local probe blending.

False leads:
- Treating each black dot, colored blob, or glossy instability as a material parameter issue.
- Continuing to tune reflection intensity without first separating the environment source contracts.

Cause:
- `GlobalEnvironmentRadiance` in deferred, forward, and weighted-translucency shaders inferred global cubemap sampling from `localReflectionProbeColor.a` and selected probe cubemap slots.
- This coupled global IBL to local reflection probe state, so enabling or loading a local/authored probe could silently change the global environment lighting path.

Control test:
- Keep local reflection probe cubemap sampling enabled, but make global IBL cubemap sampling depend only on an explicit global control.

Fix:
- Added `VulkanShadowSettings::globalIblCubemapEnabled`, with `SE_GLOBAL_IBL_CUBEMAP` override.
- Added `SE_LOCAL_REFLECTION_PROBE_CUBEMAP` as the explicit local-probe cubemap override while preserving existing `SE_REFLECTION_PROBE_CUBEMAP` compatibility.
- Uploaded the global IBL cubemap flag through `reflectionProbeBlendControls.z`.
- Updated deferred, forward, and weighted-translucency `GlobalEnvironmentRadiance` to read only that flag for global cubemap sampling.
- Added ImGui and CSV visibility through `globalIblCubemapSamplingEnabled` / `reflection_probe_global_ibl_cubemap_sampling_enabled`.

Prevention:
- Do not let local reflection probe descriptor readiness change global IBL behavior.
- When diagnosing reflection artifacts, first classify the source as visible skybox, global IBL, or local probe cubemap before tuning intensity, roughness, or material parameters.

Validation:
- `MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo` passed.
- User confirmed the real Debug lighting showcase had good overall visual quality after the contract split.

## 2026-07-08 - Global IBL Quality Source Cache Contract

Symptom:
- Reflection and IBL quality were improving, but the system still relied on scene-specific tuning.
- It was hard to tell whether a new reflection artifact came from global IBL quality, requested source, actual source, cache policy, or local probe state.

False leads:
- Raising authored skybox or cubemap resolution directly in the runtime global IBL path.
- Feeding high-resolution equirectangular sources into synchronous Debug startup preprocessing.
- Treating fallback procedural IBL as if it were the same thing as an authored cubemap request.

Cause:
- Global IBL generation did not have an explicit quality/source/cache contract.
- The renderer could not report requested versus actual IBL source, or explain why a non-procedural source/cache policy fell back.

Control test:
- Launch the accepted lighting showcase with default IBL environment variables cleared.
- The renderer should report medium quality, procedural requested source, procedural actual source, runtime-generated cache policy, no fallback reason, and preserve the accepted visual baseline.

Fix:
- Added global IBL generation settings and result info for quality, source, source fallback reason, cache policy, cache fallback reason, cache hit, and runtime-generated state.
- Added environment controls for `SE_GLOBAL_IBL_QUALITY`, `SE_GLOBAL_IBL_SOURCE`, and `SE_GLOBAL_IBL_CACHE_POLICY`, with legacy `SE_IBL_*` aliases.
- Added ImGui and benchmark CSV visibility for the resolved IBL contract.
- Kept the default visual baseline at medium/procedural/runtime-generated: BRDF 128, irradiance 32, prefiltered 256, 5 mips.

Prevention:
- Do not feed high-resolution authored cubemaps into runtime global IBL without an offline, background, or GPU cache path.
- Always record requested source/cache separately from actual source/cache before diagnosing reflection artifacts.
- Keep visible skybox, global IBL, and local reflection probe sources independently observable.

Validation:
- Debug build of `SelfEngineForward3D.exe` succeeded.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, and `maxLocalShadowRecordedTilePasses = 8`.
- CSV default fields reported `ibl_quality = 1`, `ibl_requested_source = 0`, `ibl_actual_source = 0`, `ibl_source_fallback_reason = 0`, `ibl_cache_policy = 0`, `ibl_cache_fallback_reason = 0`, `ibl_cache_hit = 0`, `ibl_runtime_generated = 1`, BRDF 128, irradiance 32, prefiltered 256, and 5 mips.
- User confirmed the real Debug lighting showcase looked acceptable.

## 2026-07-08 - Global IBL Disk Cache First Slice

Symptom:
- Global IBL had source/cache observability, but `PreferOffline` still fell back immediately because there was no real disk cache path.
- Re-running the same Debug scene repeated CPU-side procedural IBL payload generation.

False leads:
- Jumping directly to high-resolution authored equirectangular runtime preprocessing, which previously caused Debug startup stalls.
- Treating local reflection probe in-memory cache hits as equivalent to a global IBL cache.

Cause:
- The global IBL path had no persistent cache format for BRDF LUT, irradiance cubemap, and prefiltered environment base pixels.

Control test:
- Run the shadow regression twice with `SE_GLOBAL_IBL_CACHE_POLICY=prefer-offline` and the same empty `SE_GLOBAL_IBL_CACHE_DIR`.
- First run should miss and write cache; second run should report cache hit without regenerating the CPU payload.

Fix:
- Added a `.seibl` binary cache for global IBL CPU payloads.
- `PreferOffline` now reads the cache before generation, writes it after a miss, and reports `cacheHit` / `runtimeGenerated` through the existing IBL stats.
- Default `RuntimeGenerated` policy remains unchanged, so accepted visuals and default startup behavior do not silently depend on cache state.

Prevention:
- Keep the first persistent cache slice conservative: cache the current low-cost procedural payload before adding expensive authored skybox convolution.
- Do not enable high-resolution authored global IBL at runtime until the cache can store and validate authored-source signatures.

Validation:
- Debug build of `SelfEngineForward3D.exe` succeeded.
- First cache run with an empty cache directory passed shadow regression and reported `ibl_cache_policy = 1`, `ibl_cache_fallback_reason = 1`, `ibl_cache_hit = 0`, `ibl_runtime_generated = 1`.
- Second cache run with the same cache directory passed shadow regression and reported `ibl_cache_policy = 1`, `ibl_cache_fallback_reason = 0`, `ibl_cache_hit = 1`, `ibl_runtime_generated = 0`.
- The cache file `global_ibl_v1_q1_src0_b128_i32_p256_m5.seibl` was written at about 3.1 MB.
- Default policy regression still reported `ibl_cache_policy = 0`, `ibl_cache_fallback_reason = 0`, `ibl_cache_hit = 0`, `ibl_runtime_generated = 1`.

## 2026-07-08 - Authored Equirectangular Global IBL Cache

Symptom:
- The visible skybox could use `assets/skybox/bk.jpg`, but global IBL still resolved to procedural even when an authored source was requested.
- This kept visible background and global environment lighting on separate source contracts.

False leads:
- Reusing the visible high-resolution 2D skybox texture directly for reflection/IBL sampling.
- Enabling high-resolution runtime prefiltering without source-signature cache validation.

Cause:
- Global IBL settings did not carry an authored source asset path or file signature.
- The disk cache key did not distinguish different authored skybox files.

Control test:
- Run the shadow regression twice with `SE_GLOBAL_IBL_SOURCE=authored-equirectangular`, `SE_GLOBAL_IBL_CACHE_POLICY=prefer-offline`, `SE_GLOBAL_IBL_ASSET=assets/skybox/bk.jpg`, and an empty cache directory.
- First run should load the authored equirectangular image and write a source-signature cache; second run should load the same authored source from cache.

Fix:
- Added `sourceAssetPath`, source asset specified/found flags, and source signature to the global IBL generation contract.
- Added v2 `.seibl` cache files with source signature in both header and filename.
- Added authored equirectangular payload generation for prefiltered base cubemap and diffuse irradiance; specular mip generation still uses the existing Vulkan mip path.
- `visible-skybox` and `authored-equirectangular` requests default to `assets/skybox/bk.jpg` unless overridden by `SE_GLOBAL_IBL_ASSET`, `SE_GLOBAL_IBL_SOURCE_ASSET`, `SE_GLOBAL_IBL_SOURCE_PATH`, or `SE_IBL_ASSET`.
- Added ImGui and CSV reporting for source asset specified/found/signature.

Prevention:
- Cache authored global IBL by source file signature before increasing face size or adding heavier GGX prefiltering.
- Do not let authored skybox requests silently fall back to procedural without recording asset missing/load-failed reason.
- Keep default global IBL procedural until authored visual quality is explicitly accepted in the real lighting showcase.

Validation:
- Debug build of `SelfEngineForward3D.exe` succeeded.
- Authored first run passed shadow regression and reported `ibl_requested_source = 2`, `ibl_actual_source = 2`, `ibl_source_fallback_reason = 0`, `ibl_cache_hit = 0`, `ibl_runtime_generated = 1`, `ibl_source_asset_found = 1`.
- Authored second run passed shadow regression and reported `ibl_cache_hit = 1`, `ibl_runtime_generated = 0`, with the same source signature.
- Default-policy regression still reported procedural runtime generation with no source asset specified.
- User confirmed the real Debug lighting showcase looked generally normal with authored equirectangular global IBL cache enabled.

## 2026-07-08 - Lighting Showcase Authored Global IBL Default

Symptom:
- Authored equirectangular global IBL worked only through manual environment variables, so the accepted lighting showcase could silently fall back to procedural IBL on normal Debug startup.

False leads:
- Treating the first `Start-Process` timeout as an IBL startup or auto-exit failure.
- Making authored global IBL the global renderer default, which would have changed shadow and ordinary scene baselines.

Cause:
- The lighting showcase had no scene-scoped defaults for global IBL source, cache policy, or source asset.
- The automated timeout was a test harness issue: `Start-Process` with redirected output left the lighting process not responding, while direct `& SelfEngineForward3D.exe` exited cleanly.

Control test:
- Launch the real Debug lighting showcase with `SE_GLOBAL_IBL_SOURCE`, `SE_GLOBAL_IBL_CACHE_POLICY`, and `SE_GLOBAL_IBL_ASSET` cleared, and set only `SE_GLOBAL_IBL_CACHE_DIR` for an empty cache.
- The benchmark CSV should report authored equirectangular requested/actual source, prefer-offline cache policy, source asset found, and a source-signature cache file.

Fix:
- Added lighting-showcase-only environment defaults for `SE_GLOBAL_IBL_SOURCE=authored-equirectangular`, `SE_GLOBAL_IBL_CACHE_POLICY=prefer-offline`, and `SE_GLOBAL_IBL_ASSET=assets/skybox/bk.jpg`.
- Defaults are applied before renderer creation and only when the corresponding explicit env vars or aliases are unset.
- The shadow regression script clears IBL env vars before running so the ordinary shadow/default path remains procedural.

Prevention:
- Keep authored IBL defaults scene-scoped until each real scene has accepted visuals and performance.
- Use direct process execution for automated Forward3D benchmark validation; do not diagnose renderer hangs from `Start-Process` timeout alone.
- Verify both the promoted scene default and a non-showcase baseline before declaring a rendering default change complete.

Validation:
- Debug build of `SelfEngineForward3D.exe` succeeded.
- Direct lighting-showcase benchmark without manual IBL source/cache/asset env vars exited with code 0 and reported `temporal_mode = 5`, `ibl_requested_source = 2`, `ibl_actual_source = 2`, `ibl_cache_policy = 1`, `ibl_source_asset_found = 1`, and signature `14556192076403317526`.
- The empty-cache run wrote `global_ibl_v2_q1_src2_sig14556192076403317526_b128_i32_p256_m5.seibl`.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed and its CSV still reported `ibl_requested_source = 0`, `ibl_actual_source = 0`, `ibl_cache_policy = 0`, and no source asset specified.

## 2026-07-08 - Captured Local Probe Specular Roughness Gate

Symptom:
- After enabling captured-scene local reflection probes in the lighting showcase, small black speckles returned on smooth metallic spheres near the bright/dark reflection boundary.

False leads:
- Treating the issue as a material or Global IBL regression.
- Assuming any prefiltered local cubemap can safely drive smooth mirror-like specular.

Cause:
- The first captured local probe used a low-resolution 64px derived cubemap.
- Smooth and semi-smooth materials sampled that cubemap directly, exposing high-frequency dark texels as black speckles on curved glossy surfaces.

Control test:
- Launch the same Debug lighting showcase with only `SE_LOCAL_REFLECTION_PROBE_CUBEMAP=0`.
- The black speckles disappear while the rest of the lighting path remains unchanged.

Fix:
- Keep captured-scene local probes enabled and descriptor-bound.
- Gate local cubemap radiance in deferred, forward, and weighted-translucency shaders so the captured cubemap only fades in for very rough reflections (`smoothstep(0.82, 0.96, roughness)`).
- Smooth glossy reflections fall back to the stable Global IBL plus local tint instead of sampling the low-resolution captured cubemap.

Prevention:
- Do not feed low-resolution captured/local cubemaps directly into glossy specular.
- Before increasing local probe influence, validate smooth metallic spheres at grazing angles and bright/dark reflection boundaries.
- If true glossy local reflections are required, first raise probe quality or implement proper scene capture and filtering.

Validation:
- User confirmed the black speckles disappeared in the real Debug lighting showcase after the roughness gate.
- Lighting CSV still reported captured probes active: `capture_source = 3`, `fallback_reason = 0`, `capture_ready = 1`, `descriptor_bound = 1`, `selected_sampling = 3`, local cubemap `64px / 7 mips`, source type `3`.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, and `maxLocalShadowRecordedTilePasses = 8`.

## 2026-07-09 - Rect Light Specular Point-Sample Dots

Symptom:
- Lighting showcase glossy spheres showed many circular light spots even though the scene used rectangular visible light sources.
- The spots appeared as repeated round clusters on mirror-like black/chrome spheres.

False leads:
- Blaming the visible skybox or authored global IBL first.
- Treating the artifact as a captured local reflection-probe cubemap issue.
- Continuing to rearrange visible lamp fixtures without isolating direct specular.

Cause:
- `AccumulateRectAreaLight` approximated each rect light with four point samples.
- Each point sample fed the ordinary GGX direct specular path, so one rectangular light produced several circular highlights on low-roughness spheres.
- The captured local probe path also initially dropped rect `width` / `height`, so synthetic captured cubemaps could not represent rectangular light shape correctly.

Control test:
- Launch the accepted lighting showcase with only `SE_GLOBAL_IBL_CUBEMAP=0`.
- The circular clusters remained, proving the dominant spots came from direct rect-light specular rather than the skybox/global IBL.

Fix:
- Keep the four rect-light samples for diffuse/visibility approximation, but set their direct specular contribution to zero.
- Add a separate rect-light specular term by reflecting the view ray and intersecting it with the rectangular light plane.
- Update forward, deferred, and weighted-translucency shader paths together.
- Carry rect-light `width` and `height` into captured reflection probe light samples and hash them into the captured probe signature.
- Stabilize captured-scene probe generation by using a scene-level captured probe instead of changing the captured cubemap with camera-position probe selection.

Prevention:
- Do not let area-light diffuse sampling points contribute ordinary point-light specular on glossy materials.
- When a glossy sphere shows repeated round spots, isolate direct local-light specular before changing skybox, IBL, or lamp layout.
- Area-light shape data must survive every renderer and probe path that synthesizes reflections.

Validation:
- `MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo` passed and regenerated the three affected shader SPIR-V files.
- User confirmed the real Debug lighting showcase looked normal after the rect-light specular path change.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, `maxMainVisible = 7`, `maxShadowVisible = 8`, and `maxLocalShadowRecordedDraws = 37`.

## 2026-07-09 - Rect Area Shadow Projection Blocks

Symptom:
- Lighting showcase rect lights cast shallow but visibly rectangular shadow blocks on floors and walls.
- Disabling rect-light shadows made the unnatural rectangular marks disappear while rect-light illumination remained.

False leads:
- Treating the issue as fixture mesh placement or material reflectance.
- Only widening PCF/PCSS on the single rect shadow map, which diffused the blocks but kept the projection shape.

Cause:
- Rect local shadows used one shadow tile from the rect-light center with an orthographic projection.
- That behaves more like a single projector than a physical area light whose visibility is averaged over the light surface.

Control test:
- Launch the real Debug lighting showcase with `SE_RECT_LIGHT_SHADOWS_OFF=1` / `SE_LOCAL_SHADOW_RECT_OFF=1`.
- The rectangular shadow blocks disappear, proving the dominant artifact comes from rect local-shadow projection.

Fix:
- Keep rect-light illumination enabled.
- Generate two local shadow tiles per rect light from samples on the light surface's long axis.
- Average rect-light visibility across those sample tiles in forward, deferred, and weighted-translucency shaders.
- Keep the previous rect-only area softness and tile-edge fade as a fallback smoothing layer.

Prevention:
- Do not model rect area-light shadows as a single center orthographic projection when judging presentation quality.
- Validate rect-light shadows in scenes with multiple rect lights, because other lights fill shadows and make single-light artifacts harder to classify.
- Track local-shadow tile capacity before increasing rect-light sample count.

Validation:
- User confirmed the real Debug lighting showcase looked more natural after two-sample rect area-shadow visibility.
- `MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo` passed and regenerated forward, deferred, and weighted-translucency shader SPIR-V files.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, `maxLocalShadowRecordedTilePasses = 9`, `maxShadowVisible = 8`, and `maxLocalShadowRecordedDraws = 45`.

## 2026-07-09 - Local Shadow Debug View Temporal Jitter

Symptom:
- The new `Local Shadow Selected` debug view made the lighting-showcase room edges jitter violently under the default DLSS Performance startup path.
- The same scene looked stable when temporal AA/upscaling was disabled.

False leads:
- Treating the edge jitter as a new shadow-map or local-light visibility problem.
- Looking at local-shadow filter or rect-light shadow parameters before isolating the temporal path.

Cause:
- `Local Shadow Selected` was routed through the deferred HDR composite, so it also entered DLSS/TAA temporal reconstruction.
- The debug image is a high-contrast classification/contribution visualization, not a stable material/color signal for temporal reconstruction.
- In DLSS SR modes, the scene render targets could also remain at the reduced internal resolution, making debug edges worse when composited.

Control test:
- Launch the same Debug lighting showcase and same `SE_RENDER_VIEW=local-shadow-selected` with only `SE_FORWARD3D_AA_MODE=off`.
- The room-edge jitter disappears, proving the dominant artifact is temporal reconstruction of the debug view rather than shadow visibility.

Fix:
- Add `DebugViewBypassesTemporalReconstruction` and mark `LocalShadowSelected` as a bypass view.
- Disable temporal jitter, TAA resolve, DLSS post-source routing, temporal upscale requests, and DLSS SR internal render-scale reduction for that debug view.
- Recreate scene render targets when switching into or out of a view whose desired internal extent differs from the current temporal target.

Prevention:
- Debug views that display masks, IDs, contribution classes, visibility terms, or false colors should not be fed into DLSS/TAA unless they provide a proper temporal contract.
- When a debug view jitters but lit rendering is stable, first test `SE_FORWARD3D_AA_MODE=off` before changing shadow or lighting math.
- If a view bypasses temporal reconstruction, also bypass reduced internal render scale, not only the final upscaler output.

Validation:
- User confirmed the default-DLSS Debug lighting showcase no longer jitters in `Local Shadow Selected`.
- `MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, `maxLocalShadowRecordedTilePasses = 9`, `maxShadowVisible = 8`, and `maxLocalShadowRecordedDraws = 45`.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-AaModeContracts.ps1 -ExecutablePath build\Debug\SelfEngineForward3D.exe -OutputDirectory tmp\aa_mode_contracts_after_local_shadow_selected_bypass -TimeoutSeconds 90` passed; DLSS post source was active for normal DLSS, output ready was `1`, and SR internal extents still reported Quality `640x360`, Balanced `557x313`, Performance `480x270`.

## 2026-07-09 - Local Shadow Attribution Before Tuning

Symptom:
- Local-light shadow artifacts can be hard to trace back to a specific point, spot, or rect light in the lighting showcase.
- The selected local-shadow debug view and the local-shadow tile generation filter were separate controls, making it easy to view one light while the shadow pass was isolated to another.

False leads:
- Tuning bias, PCF, PCSS, or rect area-shadow parameters before proving which local light owns the artifact.
- Relying on visible debug labels in the rendered scene for attribution.

Cause:
- The Shadow Debug UI did not directly map a selected GPU local-light slot back to the Scene3D light name and did not report selected-light tile assignment, cache, or recorded caster draws.
- Draw counts alone were still too coarse for the lighting showcase; the renderer needed selected-light caster attribution to distinguish tile/caster instability from rect-light shadow-model limitations.

Control test:
- Select a local light in Shadow Debug, use `View selected` to inspect its contribution, then use `Isolate tiles` to restrict local-shadow tile generation to the same slot.
- The attribution panel should show matching selected slot, scene light name, expected/requested/assigned/dropped tiles, cache hits/misses, and recorded tile passes/draws.
- Run isolated lighting-showcase captures for local light indices 0-10 and compare `local_shadow_attribution_caster_signature`, tile candidate draw summaries, and unique caster counts.

Fix:
- Added local-shadow attribution stats for the selected local light.
- Added Shadow Debug controls for `View selected`, `Isolate tiles`, and `Clear isolate`.
- Added Scene3D light-name mapping for the selected GPU local-light slot without rendering visible labels by default.
- Added Debug-build renderable identity/name propagation into shadow commands and CSV fields for selected-light candidate draws, unique casters, caster signature, tile draw summary, and caster name summary.

Prevention:
- For local-shadow artifacts, first identify the owning light slot and tile assignment status before changing filter or bias settings.
- Keep debug labels hidden from presentation rendering; use Shadow Debug attribution for internal diagnosis.
- If all selected-light caster signatures are stable and no local shadow tiles are dropped, do not keep tuning cache or tile assignment; investigate the shadow approximation/model next.

Validation:
- `MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo` passed.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowRecordedTilePasses = 9`, and `maxLocalShadowRecordedDraws = 45`.
- Lighting-showcase isolated captures in `tmp\local_shadow_attribution_audit_v3` showed all 11 local lights are rect lights, every light received 2/2 requested tiles with 0 drops, and the 3-frame v2 captures showed stable caster signatures for every selected light.
- Practical/key/rim/overhead rect lights recorded 11-12 unique casters per light; wall-wash lights recorded only 1-2 unique casters, so strong room-wide rectangular artifacts are not explained by wall-wash tile ownership.

## 2026-07-09 - Adaptive Rect Area Shadow Samples

Symptom:
- Rect-light shadows in the lighting showcase were acceptable after the two-sample fix, but the model was still too coarse for a fuller light/shadow-system baseline.

False leads:
- Globally raising PCF/PCSS or bias values after attribution already showed stable tile/caster data.
- Raising every rect light from 2 to 4 shadow tiles without checking atlas capacity.

Cause:
- Lighting-showcase rect shadows were still sampled only along one axis of the light surface.
- The local shadow atlas cannot give all 11 rect lights 4 samples at High quality without exceeding the 32-tile budget.

Control test:
- Compare default High against `SE_LOCAL_SHADOW_RECT_SAMPLE_TILES=2`.
- Default High should request/assign 32 local shadow tiles with 0 drops; forced 2-sample should request/assign 22 local shadow tiles with 0 drops.

Fix:
- Added adaptive rect-light shadow sample budgeting.
- Rect lights keep a 2-sample baseline; higher-impact rect lights receive 4 samples when atlas capacity allows.
- Forward, deferred, and weighted-translucency shaders now average all assigned rect shadow tiles for the light instead of assuming exactly two samples.
- Added CSV/Shadow Debug stats for rect sample base/max/extra/budget-limited tiles.

Prevention:
- Treat rect area-shadow sample count as a budgeted quality feature, not a blind global multiplier.
- Keep `SE_LOCAL_SHADOW_RECT_SAMPLE_TILES=2` as the control path when judging new rect-shadow artifacts.

Validation:
- Debug build passed and regenerated forward, deferred, and weighted-translucency shader SPIR-V.
- Lighting-showcase default High reported `local_shadow_requested_tiles = 32`, `local_shadow_assigned_tiles = 32`, `local_shadow_dropped_tiles = 0`, `local_shadow_rect_max_sample_tiles = 4`, `local_shadow_rect_extra_sample_tiles = 10`, and `local_shadow_rect_budget_limited_sample_tiles = 12`.
- Forced `SE_LOCAL_SHADOW_RECT_SAMPLE_TILES=2` reported `local_shadow_requested_tiles = 22`, `local_shadow_assigned_tiles = 22`, and `local_shadow_dropped_tiles = 0`.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, `maxLocalShadowRecordedTilePasses = 11`, and `maxLocalShadowRecordedDraws = 59`.
- User confirmed the real lighting showcase looked good overall with no obvious artifact.

## 2026-07-15 - Rect Shadow 2x2 Surface Samples And Attribution Gate

Symptom:
- Rect-light shadows had become acceptable, but the underlying four-sample path still used an axial cross pattern rather than a true two-dimensional area-light sample layout.
- Future tuning needed machine-readable proof that each rect light received the intended sample pattern and did not silently lose tiles.

False leads:
- Judging the change through screenshots first; the user correctly preferred data that the agent can inspect reliably.
- Treating every local-shadow artifact as bias/PCF/PCSS tuning after attribution already showed stable caster signatures and zero tile drops.

Cause:
- The existing four-tile rect-light path added two samples on the long axis and two on the short axis. This improved over a single projector, but it still did not sample the rectangle surface as a 2D area.

Control test:
- Run `scripts\Test-LocalShadowAttributionHealth.ps1` against the LightingShowcase scene and require every valid rect light with four requested tiles to report `local_shadow_rect_sample_pattern = 1`.

Fix:
- Keep two-sample rect shadows as the axial fallback.
- Change four-sample rect shadows to a stable 2x2 surface pattern over tangent/bitangent offsets.
- Add `local_shadow_rect_sample_pattern` to CSV, Shadow Debug, and attribution JSON so the active model is inspectable.

Prevention:
- Do not claim a rect-light shadow model improvement without recording per-light requested/assigned tiles, dropped tiles, caster signature stability, and sample pattern.
- Use local-shadow attribution JSON as the primary gate; screenshots remain optional evidence.

Validation:
- `MSBuild SelfEngineLightingShowcase.vcxproj /p:Configuration=Debug /v:minimal /nologo` passed.
- `MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo` passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-LocalShadowAttributionHealth.ps1 -SkipBuild -OutputDirectory tmp\local_shadow_attribution_health_rect2x2` passed with `128 pass / 0 warn / 0 fail`; all eight current LightingShowcase rect lights reported `requested=4`, `assigned=4`, `dropped=0`, and `pattern=1`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowHealth.ps1 -SkipBuild -ShadowQuality high -OutputDirectory tmp\shadow_health_rect2x2` passed with `34 pass / 0 warn / 0 fail`.

## 2026-07-09 - Lighting Energy Balance Before Tuning

Symptom:
- Lighting/material quality had improved, but further tuning risked becoming scene-specific because direct, IBL, local probe, SSAO, shadow suppression, and emissive contributions were not visible in one place.

False leads:
- Continuing to tune light intensity, reflection strength, probe influence, or material roughness from the lit view alone.

Cause:
- Existing debug views showed individual components, but not the per-pixel dominant contribution balance.

Control test:
- Launch the real Debug lighting showcase with `SE_RENDER_VIEW=lighting-energy` and a short benchmark capture.
- The CSV should report `render_debug_lighting_energy_view_enabled = 1`, deferred PBR debug view `22`, and temporal reconstruction bypass enabled.

Fix:
- Added a deferred `Lighting Energy` debug view that color-mixes direct diffuse, direct specular, ambient diffuse/IBL, ambient specular/reflection, probe grid, emissive, and shadow/AO suppression.
- The view bypasses DLSS/TAA temporal reconstruction because it is a false-color diagnostic image.
- Added ImGui, `SE_RENDER_VIEW`, and benchmark CSV visibility for the resolved view contract.

Prevention:
- Before tuning global light energy, IBL/probe strength, or reflection intensity, inspect the energy balance view in the real scene.
- Keep classification/contribution debug views out of temporal reconstruction unless they have a deliberate temporal contract.

Validation:
- Debug build passed and regenerated `deferred_lighting.frag.spv`.
- Lighting-showcase CSV with `SE_RENDER_VIEW=lighting-energy` reported `render_debug_forward_view = 55`, `render_debug_deferred_pbr_view = 22`, `render_debug_lighting_energy_view_enabled = 1`, `render_debug_temporal_reconstruction_bypassed = 1`, `temporal_render_scale_active = 1`, and no DLSS evaluate.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, `maxLocalShadowRecordedTilePasses = 11`, and `maxLocalShadowRecordedDraws = 59`.
- User confirmed the real lighting showcase energy view has clear contribution regions and no visible jitter.

## 2026-07-09 - CSM Receiver UV Guard For Split-Line Shadows

Symptom:
- In the lighting showcase, a diagonal shadow split line appeared on floor/wall receivers; above the line shadows looked normal, below it directional shadows disappeared or became too bright.

False leads:
- Contact shadows: disabling contact shadows did not remove the line.
- Per-cascade caster culling: forcing all cascades to render the full shadow-caster list did not restore shadows below the line.
- Single-cascade mode: `SE_SHADOW_CASCADE_COUNT=1` removed the visible line but also lost useful shadow coverage, so it only bypassed the failure.

Cause:
- The CSM receiver-side light-space X/Y coverage was too tight for some receiver pixels, so the selected cascade projected them outside `[0, 1]` UV and the shader treated them as fully lit.
- The new `shadow-cascade-receiver` debug view showed the failed region as purple, matching UV-out-of-range pixels.
- Stable snapping also had a small edge-contract risk because snapped minimums were floored while maximums were reconstructed from the old width.

Control test:
- Launch lighting showcase with `SE_RENDER_VIEW=shadow-cascade-receiver`; the failing region turns purple.
- `SE_SHADOW_CASCADE_RECEIVER_GUARD=0.5` made the purple region and lit-view split line disappear.
- `0.12` brought the line back; `0.18` and `0.22` still showed close-range traces; `0.25` was the lowest accepted stable value in this scene.

Fix:
- Added `ShadowCascadeReceiver` debug view and `SE_RENDER_VIEW=shadow-cascade-receiver`; the view bypasses DLSS/TAA temporal reconstruction.
- Split CSM sampling validity from visibility in forward, deferred, weighted-translucency, and GBuffer debug shaders so invalid projection is diagnosable instead of silently becoming "lit".
- Added a CSM receiver guard ratio, defaulting to `0.25`, with `SE_SHADOW_CASCADE_RECEIVER_GUARD` override.
- Stable cascade snapping now floors min and ceils max to texel boundaries instead of preserving the old width after flooring min.
- Added `shadow_cascade_receiver_guard` to benchmark CSV and Shadow Debug stats.

Prevention:
- When a shadow split line appears, inspect `shadow-cascade-receiver` before tuning bias, PCF, PCSS, fade, or local-shadow parameters.
- Treat UV/depth-invalid cascade samples as a coverage bug first, not a soft-shadow quality issue.
- Do not use single-cascade mode as a fix for split lines; it can hide the boundary while removing valid shadow range.

Validation:
- User confirmed the purple CSM receiver failure matched the lit-view split line.
- User confirmed `SE_SHADOW_CASCADE_RECEIVER_GUARD=0.5` and then `0.25` removed the line in the real lighting showcase; `0.12`, `0.18`, and `0.22` still showed visible traces.
- Debug build passed and regenerated affected shaders.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, `maxLocalShadowRecordedTilePasses = 11`, and `maxLocalShadowRecordedDraws = 59`.
- Regression CSV reported `shadow_cascade_receiver_guard = 0.25`, `shadow_cascade_active_count = 4`, `shadow_cascade_max_distance = 60`, and `shadow_cascade_atlas_draws = 30`.

## 2026-07-15 - Forward3D And Lighting Showcase Entry Points

Symptom:
- Launching `build\Debug\SelfEngineForward3D.exe` directly was temporarily changed to the lighting showcase, but the expected Forward3D debug entry is the animated FBX/shadow-regression scene.
- `build\Debug\SelfEngineLightingShowcase.exe` could show an older, darker lighting scene when only `SelfEngineForward3D.vcxproj` had been rebuilt.

False leads:
- Treating the Forward3D startup complaint as permission to move the default debug entry to the lighting showcase.
- Assuming the standalone lighting showcase had separate scene logic; it is the same `src/forward_3d.cpp` target with `SE_FORCE_LIGHTING_SHOWCASE=1`.

Cause:
- `Forward3DBenchmarkSceneName()` automatically selected `shadow-regression` whenever the working directory was `build\Debug`.
- That made a normal double-click launch synchronously load `assets\models\Fist Fight B.fbx` in Debug, while the runtime model cache is only in-process and is cold on every new launch.
- Changing that fallback to `lighting-showcase` fixed startup time but broke the user's expected Forward3D entry point.
- `SelfEngineLightingShowcase.exe` had an older timestamp and needed to be rebuilt after the accepted lighting-showcase changes.

Control test:
- `SelfEngineForward3D.exe` default launch should print `Shadow regression scene enabled` and report `runtime_import_model_requested = 1`.
- `SelfEngineLightingShowcase.exe` should print `Lighting showcase enabled` and report `runtime_import_model_requested = 0`, `ibl_requested_source = 2`, and `ibl_cache_hit = 1`.

Fix:
- Restored the `build\Debug` `SelfEngineForward3D.exe` fallback to `shadow-regression`.
- Rebuilt both `SelfEngineForward3D.vcxproj` and `SelfEngineLightingShowcase.vcxproj` after shared `forward_3d.cpp` changes.
- Added opt-in shutdown timing logs behind `SE_SHUTDOWN_TRACE=1` to diagnose exit stalls without default Release cost.

Prevention:
- Keep executable responsibilities distinct: `SelfEngineForward3D` is the animated FBX Forward3D debug scene; `SelfEngineLightingShowcase` is the lighting/material showcase.
- When a shared source file feeds multiple exe targets, rebuild and validate every affected target before judging visuals.
- Verify the resolved scene in stdout and benchmark CSV before diagnosing startup or lighting regressions.

Validation:
- Debug build passed.
- Post-fix `SelfEngineForward3D.exe` default launch took `13.481s`, printed `Shadow regression scene enabled`, and CSV reported `runtime_import_model_requested = 1`, `runtime_import_model_loaded = 1`, `shadow_cascade_atlas_draws = 30`, and `local_shadow_recorded_draws = 59`.
- Post-fix `SelfEngineLightingShowcase.exe` launch took `5.6-6.7s` across three runs, printed `Lighting showcase enabled`, and left no lingering process.
- `SE_SHUTDOWN_TRACE=1` on `SelfEngineLightingShowcase.exe` showed renderer shutdown completed in about `162ms`, with DLSS/NGX shutdown about `108ms`.
- Manual `SelfEngineForward3D.exe` close with `SE_SHUTDOWN_TRACE=1` showed `application wait_idle = 1.0ms`, renderer shutdown about `181ms`, and DLSS/NGX shutdown about `124ms`; the remaining visible delay was not a renderer shutdown hang.
- Vulkan validation reported two `nv.ngx.dlss.resource` layout warnings. DLSS resource tracing showed SelfEngine input/output/mask image handles did not match those warning handles, and disabling optional mask bindings did not remove the warnings, so these are best treated as NGX internal validation noise unless a matching SelfEngine handle appears.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` still passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, and `maxLocalShadowRecordedDraws = 59`.

## 2026-07-15 - Hardware Swap And Early Exit Stall

Symptom:
- After changing from Ryzen 5600 + RTX 5070 to Ryzen 7 5700X + RTX 5070 Ti, both `SelfEngineForward3D.exe` and `SelfEngineLightingShowcase.exe` could appear to take many seconds to exit when closed soon after launch.

False leads:
- Debug build overhead: the same symptom reproduced in Release.
- Forward3D FBX loading: the lighting showcase also reproduced it.
- DLSS shutdown: traced NGX/DLSS shutdown was about 100-125 ms, not seconds.
- Project IBL cache mismatch: `.selfengine/ibl_cache` contains `.seibl` texture payloads, not GPU-name or device-id pipeline blobs.

Cause:
- Current best cause is a cold driver/pipeline-cache window after hardware/driver change, not a SelfEngine persistent device cache containing the old RTX 5070.
- SelfEngine currently does not own a `VkPipelineCache`, so Vulkan pipeline compilation and reuse depend mostly on the NVIDIA driver cache.
- Closing while early graphics work is still cold can make the window appear to exit slowly; after warm-up, the same window closes in about 0.3 seconds.

Control test:
- Release `SelfEngineLightingShowcase.exe` closed after 8 seconds once measured about 15 seconds to exit.
- After cache warm-up, the same 8-second and 30-second close tests measured about 0.28-0.36 seconds.
- `nvidia-smi` and WMI reported `NVIDIA GeForce RTX 5070 Ti` / device id `0x2C0510DE`; no project cache search found a plain RTX 5070 device record under `.selfengine`.

Fix:
- Added a SelfEngine-owned Vulkan pipeline cache under `.selfengine/pipeline_cache`.
- The cache filename is keyed by GPU device name, vendor id, device id, driver version, Vulkan API version, and `pipelineCacheUUID`, so changing from RTX 5070 to RTX 5070 Ti naturally selects a different cache file.
- Both graphics and compute pipeline creation now pass `VulkanDevice::PipelineCacheHandle()` instead of `VK_NULL_HANDLE`.
- Added diagnostic controls: `SE_VK_PIPELINE_CACHE=0` disables the cache, `SE_VK_PIPELINE_CACHE_RESET=1` ignores existing data for one run, `SE_VK_PIPELINE_CACHE_TRACE=1` logs load/save bytes, path, and timing.

Prevention:
- After GPU or driver changes, verify current hardware, project caches, and driver cache behavior separately.
- Do not treat an early-close stall as renderer destructor time until close-to-loop-exit and renderer shutdown are timed independently.
- Add a SelfEngine-owned pipeline cache before relying on stable cold-start or early-close behavior across hardware changes.

Validation:
- Release lighting showcase with `SE_VK_PIPELINE_CACHE_RESET=1` wrote a `1668526` byte pipeline cache for `NVIDIA_GeForce_RTX_5070_Ti`.
- A second Release lighting showcase run reported `loaded=1 bytes=1668526`.
- Debug lighting showcase also wrote the matching device-keyed cache under `build/Debug/.selfengine/pipeline_cache`.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, and `maxLocalShadowRecordedDraws = 59`.
- After waiting for scene-ready stdout and posting `WM_CLOSE`, Release lighting showcase and Forward3D both exited in about `2s`; `CloseMainWindow()` can over-report because it synchronously waits for the window to process the close message.

## 2026-07-15 - Taskbar Close Stall At Vulkan Device Destroy

Symptom:
- Closing `SelfEngineForward3D.exe` from the taskbar could visibly hang for many seconds after the scene was already running.
- `SelfEngineLightingShowcase.exe` felt better in some runs, but the user still perceived scene-window shutdown as unreliable.

False leads:
- DLSS/NGX shutdown: NGX shutdown stayed around 90-120ms and TAA/Off modes still reproduced the stall.
- FBX/skinned animation: grid and lighting-showcase scene overrides in the Forward3D executable still reproduced after enough frames.
- Shadow profile and local shadows: production, legacy/raw, and local-shadow-off controls still stalled.
- Present mode: mailbox, FIFO, and immediate controls all still left a long tail.

Cause:
- The stall was after renderer shutdown, scene resource scope, pipeline-cache save, and `vkDeviceWaitIdle`.
- The slow call was `vkDestroyDevice` itself: Forward3D Release measured about 17-22s after 30 rendered frames, while `vkDeviceWaitIdle` immediately before it was below 1ms.
- The issue appears to be NVIDIA driver/device teardown cost after this standalone Forward3D runtime path, not active GPU work or SelfEngine renderer destruction.

Control test:
- `SE_AUTO_EXIT_FRAMES=1` exited with `vkDestroyDevice` around 30-40ms.
- `SE_AUTO_EXIT_FRAMES=30` made Forward3D `vkDestroyDevice` jump to roughly 12-22s.
- Waiting for scene-ready stdout and posting `SC_CLOSE` measured Release `SelfEngineForward3D` at about 17.7s before the fast-exit fix.

Fix:
- `Application::DestroyRenderer()` is now public so standalone scene programs can destroy the renderer while scene/camera/model objects are still alive.
- Forward3D/LightingShowcase now save the Vulkan pipeline cache explicitly after renderer shutdown and use a default fast process-exit path that skips the pathological final `vkDestroyDevice` call.
- Set `SE_CLEAN_SHUTDOWN=1` or `SE_VULKAN_CLEAN_SHUTDOWN=1` to force full Vulkan teardown when validating object lifetimes.
- Set `SE_FAST_PROCESS_EXIT=0` or `SE_FAST_EXIT=0` to disable the fast-exit path.
- Added `SE_SHUTDOWN_TRACE` coverage for GLFW close callbacks, renderer swapchain/sync reset, device wait/destroy, and window destroy.

Prevention:
- Do not call a scene shutdown issue fixed until timing separates close callback, run-loop end, renderer shutdown, scene-scope destruction, pipeline-cache save, `vkDeviceWaitIdle`, `vkDestroyDevice`, and window teardown.
- For standalone visual demo executables, prefer explicit renderer/cache shutdown plus fast process exit when a driver teardown path is known to stall; keep clean shutdown opt-in for validation.

Validation:
- Ready-scene taskbar-style close after the fix measured about `339.6ms` for Release `SelfEngineForward3D`, `373ms` for Release `SelfEngineLightingShowcase`, `733.9ms` for Debug `SelfEngineForward3D`, and `1598.5ms` for Debug `SelfEngineLightingShowcase`.
- The fast path logs `[shutdown] forward3d fast_process_exit` and no longer logs `[shutdown] device destroy_device`.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-Forward3DShadowRegression.ps1 -SkipBuild` passed with `capturedRows = 8`, `animatedRows = 8`, `maxLocalShadowSkippedTiles = 0`, and `maxLocalShadowRecordedDraws = 59`.

## 2026-07-15 - Captured-Scene Reflection Refresh Contract

Symptom:
- `CapturedScene` had a ready cubemap resource, but its CSV did not expose upload count, capture signature, scene revisions, or the reason a refresh occurred.
- `SE_REFLECTION_PROBE_SCENE_DIRTY` only changed a diagnostic bit; it did not make the resource refresh.
- Camera movement could be confused with scene changes because the default Forward3D key light follows the camera.

False leads:
- Treating a ready descriptor as proof that the probe contained a rasterized view of scene geometry.
- Using the old `captured-scene placeholder` counters as resource-refresh evidence; they were derived from selected-source state rather than the actual resource update result.

Cause:
- The existing path is a 64x64-per-face analytic CPU cubemap generator driven by probe parameters and `FrameLightSet`, followed by GPU upload and CPU prefiltering. It does not render meshes, material response, visibility, or occlusion into the probe.
- Its update decision compared only the analytic radiance signature, so rigid scene changes were invisible to the policy, and the environment dirty override was never wired into the resource path.

Control test:
- Run `powershell -ExecutionPolicy Bypass -File scripts\Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict`.
- The script runs static, moving-light, moving-rigid, and camera-orbit lanes. The camera lane enables Debug-only `SE_REFLECTION_CAPTURE_CAMERA_INVARIANT_CONTROL=1`, which stops the camera-following key light after initialization so it is a pure camera-transform control.

Fix:
- Added explicit `analytic CPU` versus `rasterized GPU` capture-backend telemetry. The current backend reports six faces and `rasterized_geometry=0`; it must not be described as a real scene raster capture.
- `SceneDirty` now compares `Scene3D` membership, light, and render revisions, analytic radiance signature, and explicit force/dirty overrides. Static, file-signature, and forced policies now follow distinct update decisions.
- Added capture audit fields to ImGui and benchmark CSV: backend, face count, actual uploads/checks, performed flag, current/last reason, dirty mask, active/requested/radiance signatures, and scene revisions.
- Added `scripts\Test-ReflectionCaptureHealth.ps1` and its JSON report for automated Debug-only verification.

Prevention:
- Before claiming a reflection-probe refresh works, verify the resource upload counter and refresh reason in the four data lanes; do not infer capture freshness from visual reflection alone.
- Do not use camera movement as a no-refresh control while any test light is camera-relative.
- Keep the analytic backend label until an offscreen GPU six-face render path captures the actual scene queue, then change `rasterized_geometry` only with matching resource/pass validation.

Validation:
- Debug `SelfEngineForward3D` build passed.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\reflection_capture_health_initial` passed with `51 pass / 0 warn / 0 fail`.
- Static lane kept uploads at `1 -> 1`; moving-light lane recorded `LightChanged` with uploads `4 -> 15`; moving-rigid lane recorded `RenderChanged` with uploads `4 -> 15`; camera-invariant lane advanced camera time while holding uploads at `1 -> 1` and light/render revisions unchanged.

## 2026-07-15 - CSM Stability Needs Static And Moving Data Lanes

Symptom:
- CSM split lines, receiver UV misses, shadow shimmer, and local-light tile seams can look similar by eye, especially after several shadow fixes.

False leads:
- Judging CSM stability from a static scene only.
- Treating a moving-camera test as valid when `SE_BENCHMARK_CAMERA_MOTION` was applied to a benchmark scene where the motion path is intentionally disabled.

Cause:
- CSM stability has two separate contracts: fixed-camera cascade values must not drift frame to frame, while moving-camera cascade/texel values must remain continuous and finite.
- In Forward3D, benchmark camera orbit applies only to non-benchmark scenes, so `shadow-regression` is good for static stability but not for camera-motion CSM continuity.

Control test:
- Static lane: run the real `shadow-regression` scene and require split depths, texel world sizes, shadow-visible count, and cascade draw count to remain frame-stable.
- Moving lane: clear `SE_BENCHMARK_SCENE`, set `SE_FORWARD3D_DEBUG_DEFAULT_SCENE=default`, enable `SE_BENCHMARK_CAMERA_MOTION=orbit`, and require camera motion time to advance while split/texel changes stay continuous.

Fix:
- Added `scripts\Test-CsmStabilityHealth.ps1`, which emits `csm_stability_health.json` with separate static and moving CSM reports.
- The script checks framegraph hard errors, active/configured cascade count, stable snapping, receiver guard, atlas allocation/pass/draw signals, ordered splits, positive nondecreasing texel sizes, static zero-drift, and moving-camera jump thresholds.

Prevention:
- Before changing CSM bias, split lambda, snapping, receiver guard, PCF/PCSS, or atlas sampling, run the CSM stability health script and inspect the JSON lanes.
- Do not call a moving-camera CSM check valid unless `benchmark_camera_motion_time_seconds` advances in the CSV.

Validation:
- `cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cd /d D:\VSproject\SelfEngine\build && MSBuild SelfEngineForward3D.vcxproj /p:Configuration=Debug /v:minimal /nologo'` passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-CsmStabilityHealth.ps1 -SkipBuild -OutputDirectory tmp\csm_stability_health_final -Strict` passed with `92 pass / 0 warn / 0 fail`.
- Static lane reported 8 rows, 4 active cascades, receiver guard `0.25`, atlas draws `30`, and split/texel deltas `0`.
- Moving default-orbit lane reported 8 rows, camera motion delta about `0.107s`, 4 active cascades, receiver guard `0.25`, atlas draws `20`, split deltas no larger than `0.0002`, and texel max relative step about `0.0022`.

## 2026-07-15 - Local Shadow Cache Needs Per-Tile Invalidation Reasons

Symptom:
- Local shadow cache behavior was visible only as aggregate hit/miss counts, so a flicker or unexpected redraw could not be attributed to a light change, caster change, atlas layout change, cold image, or dynamic skinning.
- A single dynamic skinned caster disabled local-shadow cache reuse for every tile, including tiles whose light volume did not see the animated model.

False leads:
- Raising local bias, PCF, PCSS, or face blending before proving the shadow tile was actually redrawn.
- Treating all `local_shadow_cache_skipped_tiles > 0` values in animated scenes as stale-cache failures.

Cause:
- The cache stored only one combined key per atlas tile and did not retain the individual tile identity, light projection, and caster-signature components required for diagnosis.
- Cache reuse was gated by a global `HasDynamicSkinnedShadowCaster` result rather than the current tile's relevant caster set.

Control test:
- Static grid: every populated tile becomes a cache hit after the per-swapchain-image warmup.
- Move point light 0: its six cubemap faces report `light` misses while unrelated tiles remain hits.
- Move the default rigid control cube: only tiles whose light volume sees it report `caster` misses.
- Orbit the camera around a frozen default scene: camera time advances while every local-shadow tile remains a hit.
- Run animated `shadow-regression`: tiles seeing the skinned FBX report `skinned` misses, while unrelated tiles may remain hits.

Fix:
- Split each cached tile entry into tile identity, light signature, and caster signature.
- Add per-tile decisions: `cold`, `hit`, `layout`, `light`, `caster`, and `skinned`.
- Restrict dynamic-skinning cache bypass to the tile whose relevant caster set contains the animated skinned command.
- Add Debug-only tile summaries such as `t0:l0:f0=light` to Shadow Debug and benchmark CSV, plus typed reason counters.
- Add `scripts\Test-LocalShadowCacheHealth.ps1` with static reuse, moving-light, moving-rigid-caster, moving-camera, and real-skinned-FBX lanes.
- Update the Forward3D shadow regression to allow only cache skips outside the dynamically skinned tile set.

Prevention:
- Before changing local-shadow filtering or cache behavior, run `Test-LocalShadowCacheHealth.ps1` and inspect the JSON reason counters before visual tuning.
- In animated scenes, require every `skinned` tile to miss, but do not forbid hits on unrelated local-shadow tiles.

Validation:
- Debug and Release `SelfEngineForward3D` builds passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-LocalShadowCacheHealth.ps1 -SkipBuild -OutputDirectory tmp\local_shadow_cache_health_initial -Strict` passed with `45 pass / 0 warn / 0 fail`.
- Static grid: `23 hit / 0 miss`; moving light: `17 hit / 6 light miss`; moving rigid caster: `6 hit / 17 caster miss`; moving camera: `23 hit / 0 miss` with camera motion delta about `0.152s`; skinned FBX: `1 hit / 10 skinned miss`.
- `scripts\Test-Forward3DShadowHealth.ps1 -SkipBuild -ShadowQuality high -Strict` passed with `36 pass / 0 warn / 0 fail`, including `10` dynamically skinned bypass tiles and `1` safe unrelated cache hit.

## 2026-07-15 - Contact Shadow Debug Must Bypass Temporal Reconstruction

Symptom:
- Contact-shadow quality could not be diagnosed reliably under DLSS/TAA because the false-color `Contact Shadow` view was still entering temporal jitter and temporal reconstruction.

False leads:
- Tuning contact ray length, thickness, steps, or jitter strength before proving that the diagnostic image itself was not being reconstructed by DLSS/TAA.
- Treating a contact-shadow debug shimmer as proof that the screen-space ray march was unstable.

Cause:
- `ForwardDebugView::ContactShadow` was omitted from `DebugViewBypassesTemporalReconstruction()` even though it is a visibility diagnostic, not a temporally complete scene input.
- The contact ray march uses deterministic screen-pixel noise (`floor(uv * depthExtent)`), but the old debug route could still add temporal jitter and DLSS/TAA history behavior after that computation.

Control test:
- Request `SE_RENDER_VIEW=contact-shadow` together with `SE_FORWARD3D_AA_MODE=sr-performance`.
- Require the CSV to report debug view `29` / deferred view `9`, temporal reconstruction bypass `1`, applied temporal jitter `0`, no temporal-upscale request, no DLSS evaluate, and one contact debug draw/frame/GBuffer bind.
- Run the same debug route with orbit camera motion and animated `shadow-regression` FBX; the bypass contract must remain active while motion/animation counters advance.

Fix:
- Add `ForwardDebugView::ContactShadow` to `DebugViewBypassesTemporalReconstruction()`.
- Add `scripts\Test-ContactShadowHealth.ps1`, a JSON gate covering normal lit TAA, DLSS contact-debug bypass, moving camera, animated FBX, and disabled-contact control lanes.

Prevention:
- Treat contact-shadow debug as a deterministic diagnostic route. Do not feed it through TAA/DLSS unless it later receives an explicit temporal history contract.
- Before changing contact shadow strength, length, thickness, steps, jitter, or edge fade, run the contact health script and inspect the JSON instead of judging the debug view through temporal artifacts.

Validation:
- Debug `SelfEngineForward3D` build passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-ContactShadowHealth.ps1 -SkipBuild -OutputDirectory tmp\contact_shadow_health_initial -Strict` passed with `53 pass / 0 warn / 0 fail`.
- Normal lit TAA remains active with High controls `strength=0.48`, `length=0.28`, `thickness=0.12`, `steps=6`, deterministic jitter strength `0.18`, and `24px` edge fade.
- DLSS contact-debug lane reports `bypass=1`, `jitterApplied=0`, no DLSS evaluate; moving-camera lane advances by about `0.144s` under the same bypass; animated FBX lane reports `65` changed bone-palette entries under the same bypass.

## 2026-07-15 - GPU Reflection Capture Must Bind Valid Fallbacks Before Its First Face

Symptom:
- The first GPU reflection-capture frame triggered Vulkan descriptor validation: the local reflection-probe sampler array contained a null image view before the main-frame descriptor update.

False leads:
- Treating the error as a cubemap image-layout or framebuffer transition problem.
- Assuming disabling local reflection in the capture UBO makes uninitialized descriptor bindings harmless.

Cause:
- Reflection capture is recorded before the normal main-frame `UpdateEnvironmentDescriptorSets()` call so it can use face-specific UBO/light/material data. The newly allocated frame descriptor set had never received fallback cube views.

Control test:
- Launch a captured-scene probe with `SE_REFLECTION_PROBE_CAPTURE_BACKEND=gpu` and capture frames from process start. Validation must stay clean during faces 0 through 5, before the completed cubemap is sampled.

Fix:
- Bind the regular global IBL fallback views to the current frame descriptor set immediately before every offscreen capture face.
- Keep local probe sampling disabled in the capture UBO to prevent recursive feedback; descriptors remain valid even when the shader branch is inactive.
- Preserve the previous completed cubemap while rendering the next six-face staging capture, then switch only after mip generation succeeds.

Prevention:
- Any auxiliary pass that binds the shared frame descriptor layout before the main pass must explicitly initialize every sampled image binding it can expose.
- Do not use the main-camera queue for a cubemap face: rebuild a face-specific probe-center frustum and record visibility/draw counters for each capture.

Validation:
- Debug builds of `SelfEngineForward3D` and `SelfEngineLightingShowcase` passed.
- `scripts\Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict` passed with `63 pass / 0 warn / 0 fail`.
- A zero-warmup capture records face progress `1/6` through `6/6`, then reports `17` cumulative geometry draws, `13` culled commands, mip readiness, and one completed capture upload.

## 2026-07-15 - Captured Scene Probes Must Never Share Spatial Capture Images

Symptom:
- The Lighting Showcase could select several `CapturedScene` probe volumes, but metal reflections showed displaced room geometry, incomplete light-strip placement, and spatially implausible outlines.

False leads:
- Tuning box-parallax, cubemap Y orientation, material roughness, or showcase light placement.
- Treating the issue as a low capture resolution problem.

Cause:
- GPU capture owned one global active cubemap while several probe volumes had different centers. Descriptor binding reused that image for every selected `CapturedScene` slot, so samples from one capture origin were reinterpreted as if captured at another origin.

Control test:
- Run `scripts\Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict` with its `multi-probe-identity` Lighting Showcase lane.
- Require three selected captured probes, three allocated and ready per-probe resources, three distinct active image views, producer-match mask `0x7`, and duplicate-view mask `0`.

Fix:
- Store active cubemap, staging cubemap, depth image, face framebuffer/views, refresh revisions, and audit state by probe `sceneIndex`.
- Schedule at most one face per frame while round-robining pending probe indices; keep each probe's last completed active image sampled until its own six-face replacement is complete.
- Bind `localReflectionProbeMaps[slot]` by the selected probe's `sceneIndex`, not by a global captured-scene image.
- Add Debug/CSV resource counts, ready/in-flight counts, active-view distinctness, and duplicate selected-view detection.

Prevention:
- Any render resource representing a spatial probe, light, cascade, or camera must carry producer identity through allocation, update, selection, descriptor binding, and audit output.
- Do not unblock visual tuning with a shared fallback image when several spatial origins are selectable; make the fallback explicit and measurable instead.

Validation:
- Debug `SelfEngineForward3D` build passed.
- `powershell -ExecutionPolicy Bypass -File scripts\Test-ReflectionCaptureHealth.ps1 -OutputDirectory out\reflection_capture_per_probe_health -SkipBuild -Strict` passed `89 pass / 0 warn / 0 fail`.
- The final multi-probe CSV frame reports selected/resources/ready/distinct views `3/3/3/3`, in-flight `0`, producer match mask `0x7`, duplicate active-view mask `0`, and three completed uploads.

## 2026-07-16 - Captured Scene Roughness Needs GGX Filtering And A Real Mip Contract

Symptom:
- Captured-scene local reflections used `roughness * 4.0` even though the 256x256 runtime cubemap has nine mip levels.
- Its generated mips were linear blits, which reduce resolution but do not represent the GGX roughness convolution expected by PBR specular sampling.

False leads:
- Raising capture resolution or tuning material roughness while retaining the fixed five-mip shader assumption.
- Calling a linearly downsampled cubemap "prefiltered" without recording how it was filtered.

Cause:
- The dynamic capture path had no per-probe sampler LOD contract and no GPU convolution stage after its sixth face completed.

Control test:
- Run `scripts\Test-ReflectionCaptureHealth.ps1 -OutputDirectory out\reflection_capture_ggx_health_final -SkipBuild -Strict`.
- Require selected captured probes to publish their actual mip counts, a completed GGX prefilter flag, non-zero mip dispatches, and a sample budget of at least 32.

Fix:
- Append per-slot `reflectionProbeMipControls` to the shared frame UBO and use it in forward, deferred, and weighted-translucency local-probe sampling instead of hardcoded `roughness * 4.0`.
- Add a GPU compute pass that samples cubemap level zero with 64 Hammersley/GGX importance samples for each destination mip and writes the six-layer storage-image mip view.
- Keep the prior active cubemap visible until all capture faces and GGX destination mips are complete.

Prevention:
- A texture LOD selected from material roughness must use the resource's actual mip count, not a constant copied from an unrelated cubemap.
- Dynamic PBR captures must expose whether their roughness chain is GGX-convolved, not merely whether a mip chain exists.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed.
- `Test-ReflectionCaptureHealth.ps1` passed `105 pass / 0 warn / 0 fail` across static, light motion, rigid motion, camera invariance, and three-probe identity lanes.
- Each completed captured probe reports 9 mip levels, 8 GGX prefilter dispatches, and a 64-sample budget.

## 2026-07-16 - Captured Probes Need Their Own Shadow Contract

Symptom:
- GPU-captured reflections contained direct light and geometry response but omitted directional occlusion because capture deliberately disabled all shadow sampling.

False leads:
- Reusing the main camera CSM atlas or the current local-light shadow tiles for the cubemap faces.
- Treating a valid main-pass shadow map as valid capture input even though capture runs before the main-frame shadow passes are rebuilt.

Cause:
- Main CSM coverage is camera-relative and its resources are produced later in the frame. Local-shadow tiles are also camera-frame state, so either reuse produces stale or spatially unrelated capture shading.

Control test:
- Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-ReflectionCaptureHealth.ps1 -OutputDirectory tmp\reflection_capture_directional_shadow_health -SkipBuild -Strict`.
- Require a ready 32x32 diffuse irradiance cubemap and, for directional capture shadows, requested/ready state, six passes, a `0x3F` face mask, nonzero draws/casters, full map size, camera-independent projection, suppressed local tiles, and correct probe identity.

Fix:
- Convolve completed captured radiance into a 32x32, 64-sample cosine-weighted diffuse irradiance cubemap for each probe.
- Build a single stable directional projection from the probe center and capture range, render it immediately before each face into a dedicated full-size shadow map, and bind a capture-only material descriptor set to sample it.
- Use the reserved directional-cascade UBO channel to distinguish the standalone map from the main 2x2 CSM atlas, and clear the local-shadow buffer before capture shading.

Prevention:
- An offscreen capture pass must record every resource producer it samples; do not borrow camera-relative shadow data without an explicit spatial and frame-lifecycle contract.
- For every probe face, audit both resource identity and producer state before relying on visual results.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict` passed `182 pass / 0 warn / 0 fail`.
- Static and camera lanes reported six 4096 shadow passes, `0x3F` face coverage, camera-independent projection, and zero later shadow/upload delta; moving-light/object lanes recorded new passes during refresh; Lighting Showcase visual acceptance was natural and stable.

## 2026-07-16 - Scene Light Labels Must Follow Frame-Light Packing

Symptom:
- Adding one ceiling point light and two ceiling spotlights to the Lighting Showcase made debug labels 8-10 appear to identify the new fixtures, while local-shadow attribution reported those frame slots as rect lights.

False leads:
- Treating the attribution output as a point/spot shadow-generation failure.
- Assuming Scene3D creation order and renderer frame-light order are interchangeable.

Cause:
- `BuildFrameLightSet` packs all point lights first, then spots, then rects. The scene's optional labels were assigned in construction order, which had all pre-existing rect lights before the newly created point/spot lights.

Control test:
- Run `scripts\Test-LightingShowcaseCeilingLightsHealth.ps1 -SkipBuild -Strict`.
- Run `scripts\Test-LocalShadowAttributionHealth.ps1 -LightIndices 0-2 -SkipBuild -Strict` and require point/spot/spot kinds with requested tiles `6/1/1`.

Fix:
- Label Lighting Showcase local lights with separate point, spot, and rect counters matching the frame packing convention.
- Add a combined showcase health gate for the 1 point, 2 spot, and 8 rect lights, plus targeted kind/footprint assertions for frame slots 0-2.

Prevention:
- Any debug label, selector, or attribution key that crosses Scene3D and frame-light layers must use the renderer's canonical packed index, not source creation order.
- When adding a light type to a stress scene, validate both combined atlas allocation and per-light producer identity.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed.
- `Test-LightingShowcaseCeilingLightsHealth.ps1 -SkipBuild -Strict` passed `10 pass / 0 fail` with point/spot/rect/total `1/2/8/11`, shadow tiles `32/32/0`, and point/spot footprints `6/2`.
- `Test-LocalShadowAttributionHealth.ps1 -SkipBuild -Strict` passed `170 pass / 0 warn / 0 fail` across slots 0-10; slots 0-2 report point/spot/spot with `6/1/1` assigned tiles.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict` passed `182 pass / 0 warn / 0 fail`; user accepted the Lighting Showcase visual result.

## 2026-07-16 - Captured Probes Need Transient Point And Spot Shadow Atlases

Symptom:
- Captured-scene reflections had probe-local directional shadowing, but point and spot lights were direct-lit without their occluders because capture cleared the local-shadow tile buffer.

False leads:
- Sampling the main camera's local-shadow atlas during cubemap capture.
- Reusing the main local-shadow cache state because its tile image already exists for the current swapchain image.

Cause:
- Main local-shadow tiles are camera-frame resources produced later in the frame and their cache keys encode main-frame state. They are neither spatially valid nor lifecycle-safe for an earlier per-probe cubemap face.

Control test:
- Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-ReflectionCaptureHealth.ps1 -OutputDirectory tmp\reflection_capture_local_point_spot_health -SkipBuild -Strict`.
- In the LightingShowcase lane require capture-local kind masks `0x3/0x4`, nonzero point and spot tile counts, zero rect tiles, a `0x3F` face mask, camera independence, and a valid probe identity.

Fix:
- Build a fresh point/spot-only `LocalShadowTileSet` from the full caster queue for every cubemap face, with no cache state.
- Clear and render the transient local atlas immediately before the face shading pass, upload its tile UBO, and bind the atlas through the capture-only material descriptors.
- Keep rectangle lights directly lit but explicitly record them as capture-shadow suppressed until a separate multi-sample area-shadow budget is implemented.

Prevention:
- Never reuse a main-camera local-shadow atlas or cache for an offscreen probe capture without an explicit per-probe spatial and lifecycle contract.
- When a capture feature supports only a subset of light kinds, publish supported and suppressed masks in CSV/ImGui and make the health gate assert both.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict` passed `197 pass / 0 warn / 0 fail`.
- The completed LightingShowcase probe reports 48 local tile passes: point/spot/rect `36/12/0`, 360 draw submissions, 1024-pixel tiles, `0x3F` face coverage, camera independence, and producer probe index 0.
- User accepted the single real `SelfEngineLightingShowcase` visual window.

## 2026-07-16 - Captured Rect Lights Need The Same Budgeted Surface Samples As Main Shadows

Symptom:
- Captured reflections had point and spot occlusion, but rect lights remained direct-lit because the capture path deliberately excluded their multi-sample tiles.

False leads:
- Reusing the main camera's completed rect tiles during capture.
- Adding a single center rect projection to the probe path and relying on filtering to hide the resulting hard block.

Cause:
- The capture-local tile builder was restricted to point and spot lights, even though the main path already had an importance-ranked two/four-sample rect area-shadow budget and shader resolve.

Control test:
- Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-ReflectionCaptureHealth.ps1 -OutputDirectory tmp\reflection_capture_rect_area_health_retry -SkipBuild -Strict`.
- In the normal LightingShowcase lane require rect tiles, requested/max budget accounting, zero rect drops, support mask `0x7`, suppression mask `0`, and all six capture faces.
- In the `rect-capture-disabled` lane set `SE_REFLECTION_CAPTURE_RECT_SHADOWS_OFF=1` and require rect tiles/requested/dropped all zero with masks `0x3/0x4`.

Fix:
- Reuse the rect sample budget and surface-view-projection policy when building each transient capture-local tile set; retain no main-camera cache state.
- Reserve point/spot tiles first, then allocate rect extra samples by importance within the capture atlas capacity.
- Add passive CSV/ImGui audit fields for requested, maximum, granted-extra, budget-limited, and dropped rect tiles.

Prevention:
- A probe capture must use the same light-kind sampling policy as its main shading consumer, or explicitly declare the kind as suppressed.
- Area-shadow quality changes require both a normal budget-accounting lane and a type-specific disabled control; an image alone cannot prove the fallback contract.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict` passed `231 pass / 0 warn / 0 fail`.
- Completed LightingShowcase capture reported 192 local tile passes: point/spot/rect `36/12/144`, rect requested/maximum `144/192`, extra/budget-limited `48/48`, no drops, tile size 1024, and `0x3F` coverage.
- User accepted the single real `SelfEngineLightingShowcase` visual window.

## 2026-07-16 - Probe Capture Shadow Snapshots Must Own Their Resources

Symptom:
- A complete six-face captured-scene probe refresh rebuilt the same probe-centered directional and local shadow depth maps once for every cubemap face.
- Reusing descriptor slot zero without waiting for the previous main submission caused Vulkan validation errors when that main frame still referenced the slot.

False leads:
- Sharing the main camera shadow map or local atlas with probe capture.
- Round-robinning unrelated probe faces while one capture snapshot is still needed.

Cause:
- Capture shadow projections are derived from probe/light/caster state and are face-independent within one coherent refresh, but the old path treated each face as an independent shadow producer.
- Main rendering and capture both use swapchain-indexed global descriptors, so the capture-reserved slot can still be pending from the prior main submission.

Control test:
- Set `SE_REFLECTION_CAPTURE_SHADOW_SNAPSHOT_OFF=1` to force the legacy per-face depth rebuild.
- Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict`.

Fix:
- Give capture a dedicated single-slot directional shadow map, local atlas, framebuffers, and material descriptors.
- Build and render the snapshot on face zero, serialize the active probe until face five, and reuse the stored tile lists/depth maps for faces one through five.
- Wait for the graphics queue before rewriting descriptor slot zero, then submit and wait the capture before main rendering consumes its normal frame state.
- Publish build/reuse masks, saved directional/local work, identity, readiness, and fallback state to CSV and ImGui.

Prevention:
- A resource reused across cubemap faces must be owned by the active probe refresh, not by a main-camera frame or an unrelated probe.
- Any reserved descriptor slot shared with a previous submission needs an explicit synchronization proof before it is rewritten.
- Keep a disabled control path and validate both a structurally different Forward3D scene and LightingShowcase before visual review.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict` passed `280 pass / 0 warn / 0 fail` across static, moving-light, moving-object, camera-invariant, LightingShowcase multi-probe, rect-disabled, and snapshot-disabled lanes.
- Normal completed captures report one shadow snapshot build, five reuse faces, five saved directional passes, and LightingShowcase saved 160 local tile passes / 2060 local draws; the disabled control reports six directional passes and no snapshot reuse.
- User accepted the single real `SelfEngineLightingShowcase` visual window.

## 2026-07-16 - Captured Probes Must Filter Global Dirty Revisions By Influence

Symptom:
- Any scene light or render revision previously caused every captured-scene probe to refresh, even when the changed light or object could not affect that probe.
- The six-face capture scheduler had no per-probe cooldown, so repeated local changes could continually re-enter the capture budget.

False leads:
- Treating a global `Scene3D` revision as a sufficient per-probe invalidation contract.
- Tuning the LightingShowcase probe order or a scene-specific refresh interval.

Cause:
- Captured probe resources tracked only global membership, light, and render revisions. They had no probe-local representation of influenced lights or renderables.

Control test:
- `SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES=64` with a moving light must defer refreshes without an additional completed upload.
- The Debug-only `SE_REFLECTION_CAPTURE_LOCALITY_CONTROL=1` grid lane adds a distant captured probe; a local moving light must advance the global revision while the distant probe remains locally clean.
- `SE_REFLECTION_CAPTURE_SELECTIVE_REFRESH_OFF=1` restores the auditable global invalidation fallback.

Fix:
- Compute per-probe local-light and geometry signatures from conservative influence bounds, then only treat changed global revisions as dirty when their matching local signature changed.
- Prioritize pending probes by influence score, serialize an active six-face snapshot, and enforce a configurable minimum refresh interval with force/scene-dirty overrides exempted.
- Publish scheduler frame, completion frame, signatures, affected counts, priority, cooldown/defer state, and locality-bypass counts through CSV and ImGui.

Prevention:
- A global scene revision is a candidate invalidation signal, never proof that every spatial cache must rebuild.
- Any cache refresh budget must expose both the defer decision and an explicit disabled fallback in Debug data.
- Use a spatially separated control resource before accepting locality logic; a single showcase probe cannot prove it.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed and both signed binaries verified valid.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\reflection_capture_selective_refresh_final` passed `413 pass / 0 warn / 0 fail`.
- Budget control: interval `64`, deferred-count delta `11`, deferred flag `1`, upload delta `0`.
- Locality control: two probe resources, ignored-light-revision count `1`; disabled-selective fallback recorded two additional uploads.
- User accepted the single normal `SelfEngineLightingShowcase` visual window.

## 2026-07-16 - Persistent Probe Shadow Snapshots Need Bounded Per-Probe Ownership

Symptom:
- A probe whose local lights and geometry had not changed still rebuilt its directional depth map and local shadow atlas on every later six-face refresh.
- A naive per-probe allocation would retain one 2048-scale directional map and local atlas for every captured probe without a VRAM bound.

False leads:
- Reusing the main camera's shadow resources or camera-local shadow cache.
- Treating a face-local reuse counter as proof that a separate later refresh reused the same depth data.

Cause:
- The existing capture snapshot belonged only to the active six-face refresh. It had no persistent probe identity, input signature, resource ownership, or capacity policy across completed captures.

Control test:
- Set `SE_REFLECTION_PROBE_SCENE_DIRTY=1` with `SE_REFLECTION_CAPTURE_REFRESH_MIN_FRAMES=0` to force repeated static-scene captures.
- Set `SE_REFLECTION_CAPTURE_PERSISTENT_SHADOW_CACHE_OFF=1` to restore the explicit per-refresh rebuild fallback.
- Use the three-probe LightingShowcase lane to require two resident slots and an auditable eviction.

Fix:
- Add a fixed two-slot LRU cache keyed by probe `sceneIndex`. Each slot owns its directional map, local atlas, framebuffers, material descriptors, input signature, and last-used scheduler frame.
- Reuse a slot only when conservative local-light/geometry signatures, probe volume, and relevant shadow settings match; otherwise rebuild that slot's snapshot.
- Export per-capture hit state plus global capacity, resource count, eviction count, slot owner, and signature through Debug ImGui and benchmark CSV.
- Release all slots when material descriptors, swapchain resources, or shadow-map dimensions are rebuilt.

Prevention:
- A persistent GPU cache must have a stable spatial key, complete input signature, explicit ownership, a bounded capacity, deterministic eviction, and a measurable disabled fallback.
- Multi-resource tests must query global cache state directly; a single selected probe audit cannot prove overall cache occupancy.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed and both signed binaries verified valid.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\reflection_capture_persistent_shadow` passed `490 pass / 0 warn / 0 fail`.
- Reuse control recorded a persistent hit with shadow build `0`, directional depth pass `0`, and local depth pass `0`; disabled control reported zero persistent resources.
- Three-probe LightingShowcase reported capacity/resources/evictions `2/2/1`, with two distinct slot owners and nonzero input signatures.
- User accepted the single normal `SelfEngineLightingShowcase` visual window.

## 2026-07-16 - Captured Radiance Filtering Needs An Explicit Quality And Fallback Contract

Symptom:
- GPU-captured probes always used a hidden 64-sample GGX constant, so visual quality and refresh cost could not be selected or audited.
- Turning the convolution off naively would leave roughness-selected mip levels undefined or unsafe to sample.

False leads:
- Raising the cubemap face resolution to compensate for stochastic GGX sample quality.
- Treating a generated mip count as proof that every mip has a valid roughness-filtering contract.

Cause:
- The capture prefilter compute pass received a fixed sample count and exported only a dispatch count. The probe refresh signature did not contain the filtering configuration.

Control test:
- `SE_REFLECTION_CAPTURE_FILTER_QUALITY=high` must resolve to quality `3`, `128` samples, eight mip dispatches, and no fallback in LightingShowcase.
- `SE_REFLECTION_CAPTURE_FILTER_QUALITY=off` must resolve to quality `0`, a one-sample direct-radiance mip fallback, eight dispatches, a ready mip chain, and explicit fallback state in default Forward3D.

Fix:
- Add scene-independent `Off/Low/Medium/High/Ultra` captured-radiance presets with sample budgets `1/16/64/128/256`; default Medium preserves the prior 64-sample result.
- Include the resolved preset in the capture signature so a quality change schedules a new capture.
- Generate direct-radiance mips for Off rather than exposing uninitialized roughness LODs, and publish quality, samples, dispatches, readiness, and fallback state through CSV and ImGui.

Prevention:
- Any roughness-mip producer must publish both its filter quality and its valid fallback path; mip count alone is insufficient evidence.
- Quality controls belong to the renderer contract and refresh signature, never to an individual showcase scene.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed and both signed binaries verified valid.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\reflection_capture_filter_quality` passed `578 pass / 0 warn / 0 fail`.
- Default Medium reported `quality/samples/dispatches = 2/64/8`; LightingShowcase High reported `3/128/8` across three probe resources; Off reported `0/1/8`, mip ready `1`, GGX ready `0`, fallback `1`.
- User accepted the single normal `SelfEngineLightingShowcase` visual window.

## 2026-07-16 - Reflection-Probe Spatial Contracts Need Traversal Evidence

Symptom:
- Per-probe resources and filtering were individually correct, but there was no data proof that six capture faces used the expected cubemap axes or that multi-probe selection stayed spatially coherent while the camera moved.

False leads:
- Treating a visually plausible static LightingShowcase frame as proof of cubemap orientation or blend correctness.
- Checking resource-view uniqueness without checking selected probe identity, blend normalization, box-projection masks, and usable roughness mips together.

Cause:
- The probe pipeline had producer/consumer audits, but no single spatial contract spanning capture orientation, selected-probe identity, blend normalization, box-projection membership, and camera traversal.

Control test:
- The Debug-only `spatial-blend-traversal` lane moves the camera through LightingShowcase with three captured probes and requires the resolved blend to vary.

Fix:
- Validate and record each canonical cubemap face orientation at capture time.
- Publish a per-frame spatial contract with duplicate scene-index mask, normalized blend error, box-projection subset check, and selected roughness-mip readiness mask.
- Extend `Test-ReflectionCaptureHealth.ps1` with strict generic assertions and the LightingShowcase traversal lane; explicit benchmark camera motion now works in all scene types without changing normal camera behavior.

Prevention:
- Do not accept multi-probe reflection changes from a static screenshot alone. Require capture-axis, identity, normalization, resource-readiness, and movement-driven blend evidence before visual review.
- Keep camera traversal opt-in through `SE_BENCHMARK_CAMERA_MOTION`; scene type must not silently disable an explicit diagnostic control.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed and both signed binaries verified valid.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\reflection_probe_spatial` passed `694 pass / 0 warn / 0 fail`.
- Traversal reports canonical orientation `0x3F`, three selected/ready probes, mip-ready mask `0x7`, duplicate/failure masks `0x0`, blend error `1.19209e-7`, and blend delta `0.02769`.
- User accepted the single normal `SelfEngineLightingShowcase` visual window.
## SSR Hi-Z producer/consumer validation must include actual GPU commands

Symptoms:
- CPU stats reported hierarchical SSR active and a complete mip mask, while an early validation run still emitted `VUID-vkCmdDraw-None-09600` for an `UNDEFINED` depth-pyramid layout in Hi-Z-off lanes.
- The first health script also reported zero build commands even though the completed CSV later contained `11` dispatches and one consumer draw.

False leads:
- Treating the expected mip count as proof that the compute producer executed.
- Accepting a zero-issue FrameGraph as proof that Vulkan image layouts were valid.
- Invoking a Win32 GUI executable with PowerShell's call operator and assuming the script waited for process exit.

Cause:
- The Deferred shader statically declares the depth-pyramid binding even when the runtime branch selects fixed-step SSR, so the image must have a valid descriptor layout before every Deferred draw, not only while Hi-Z dispatches run.
- Windows PowerShell can return from a GUI-subsystem executable invocation before that process exits. The health script imported an early CSV row while the renderer was still running.

Fix:
- Transition every depth-pyramid image to `GENERAL` during resource creation; active Hi-Z frames explicitly discard/rebuild the full chain before Deferred consumption.
- Record actual build dispatches, descriptor binds, and Deferred consumer draws in `RendererBindStats`, and compare their per-frame min/max against the expected mip count.
- Run GUI benchmark lanes with `Start-Process -Wait`, capture stdout/stderr separately, and reject any `VUID` or Vulkan validation diagnostic.

Prevention:
- A screen-space producer is not data-closed until expected state, actual command counts, consumer use, fallback command suppression, FrameGraph validation, and Vulkan validation all agree.
- Keep descriptors valid across dynamic branches even when a shader is expected not to sample them at runtime.

## 2026-07-18 - Deferred SSR Must Reproject Its Previous-Frame Resolve

Symptom:
- Reflected color fragments changed with material content and camera distance: distant receivers showed more fragments, close receivers showed fewer, and very close receivers hid them.
- The fragments moved as the camera moved and appeared on several materials, so the failure was not specific to a sphere, material, light, or probe.

False leads:
- Tuning sphere roughness, SSR strength, spatial radius, hit thickness, or showcase lighting.
- Treating the reflected colors as bad cubemap compression after current-frame HDR radiance had already been proven active.

Cause:
- `SSRResolved` was produced after Deferred and consumed on the next frame, but Deferred sampled it at the current `fragUv`.
- The previous-frame resolve therefore crossed current-frame object and silhouette boundaries. The error grows in screen-space significance as geometry becomes smaller with distance.

Control test:
- `SE_SSR_DEFERRED_REPROJECTION=0` disables only the Deferred receiver reprojection/rejection path while preserving trace, temporal, spatial, HDR-radiance, and probe fallback work.

Fix:
- Bind `SSRHistoryMetadata` beside `SSRResolved`, sample both at `fragUv - velocity`, and reject history by previous-view depth, octahedral normal, and roughness before blending with the environment fallback.
- Bind resolved color and metadata from the same previous submitted image identity, model `SSRResolved` as persistent history, and publish contract version, descriptor readiness, rejection state, and alias state through Debug CSV.
- The algorithm follows the common production reflection-denoiser contract used by FidelityFX Denoiser and NRD: motion-vector reprojection plus receiver depth/normal/material rejection. It reuses the existing RGBA16F metadata image, adding one sampled read and bounded ALU without adding another full-resolution allocation.

Prevention:
- A post-lighting image consumed by the next frame is temporal history even if its producer is named spatial resolve; its consumer must reproject and validate it.
- Never accept a temporal reflection chain from producer-side denoising alone. Audit the final consumer coordinates and metadata identity explicitly.

Validation:
- `Test-SsrHitValidationMath.ps1` passed `29 / 0`.
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed and regenerated `deferred_lighting.frag.spv`.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\ssr_deferred_receiver_reprojection_health` passed `363 / 0` across LightingShowcase, animated Forward3D FBX, disabled-reprojection, Hi-Z fallback, and disabled controls.
- Default and FBX lanes report contract `5`, reprojection/depth/normal/roughness `1/1/1/1`, metadata bound `1`, alias `0`, trace/temporal/spatial `1/1/1`, zero FrameGraph issues, and no Vulkan validation diagnostics. Visual acceptance is pending.

## 2026-07-18 - SSR Spatial Denoise Must Not Binary-Gate Low-Confidence Centers

Symptom:
- Metallic spheres and other materials showed camera-distance-dependent colored fragments. The pattern appeared where sparse SSR samples alternated with the IBL fallback.

False leads:
- Further tuning one material, sphere distance, probe/cubemap input, SSR strength, or temporal reprojection.

Cause:
- The Debug topology audit measured `127479` raw hit pixels in the normal LightingShowcase lane, but only `4398` passed the spatial shader's hard `centerTraceConfidence >= 0.18` gate. Temporal valid pixels remained `127479`, proving temporal rejection was not the dominant loss.
- The binary center gate discarded low-confidence grazing hits before the existing edge-aware depth/normal/roughness neighborhood filter could reconstruct them, leaving sparse SSR pixels interleaved with environment fallback.

Fix:
- Remove the binary center-confidence rejection from `ssr_spatial.comp`.
- Keep invalid-depth and high-roughness rejection, then use same-surface depth/normal/roughness weights to reconstruct both low-confidence centers and center-miss neighborhoods. Confidence remains soft and falls back only when no valid temporal neighborhood exists.
- Add a Debug-only `ssr_diagnostics.comp` pass that records raw hits, high-confidence hits, temporal-valid pixels, resolved-valid pixels, isolated hits, center-miss neighborhoods, and resolved holes into the existing host-visible diagnostics buffer. Release creates and dispatches no diagnostic pass.

Prevention:
- Never use a single SSR confidence threshold as a binary spatial validity gate. Confidence must control contribution weight; topology and surface compatibility decide whether reconstruction is allowed.
- For screen-space denoisers, compare raw coverage, temporal coverage, spatial coverage, and neighborhood-hole counts before changing scene or material parameters.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed; `ssr_diagnostics.comp.spv` and the updated `ssr_spatial.comp.spv` regenerated successfully.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\ssr_hole_diagnostics_health_after_spatial_gate` passed `382 / 0` across ten LightingShowcase/Forward3D lanes with zero FrameGraph issues and no Vulkan validation diagnostics.
- After the fix, the representative normal lane reported `193600` resolved-valid pixels versus `4339` before the fix. The absolute `resolvedHolePixels` counter increased from `510` to `1909`, so visual acceptance and further topology interpretation remain open rather than being declared complete.

## 2026-07-18 - Packed SSR Control Fields Must Decode Every Bit Position

Symptom:
- SSR visual behavior and control lanes were inconsistent: the configured step count was not reflected in shader execution, and disabling the current HDR scene-color source did not reliably disable temporal radiance.

Cause:
- `ssrControls.w` packs step count plus flags at `64/128/256/512/1024`, but the shader-side decoders manually subtracted only the lower flags. The `1024` Deferred receiver-reprojection bit leaked into step and scene-color tests.

Fix:
- Decode the packed field by positional `floor/mod` tests in `ssr_trace.comp`, `ssr_temporal.comp`, and `deferred_lighting.frag`. Step count now reads the low six bits; Hi-Z, scene-color, hit-validation, reconstruction, and receiver-reprojection each read their own bit.
- Extend `Test-SsrHitValidationMath.ps1` with a packed-control contract check.

Validation:
- `Test-SsrHitValidationMath.ps1` passed `30 / 0`.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\ssr_control_decode_health` passed `382 / 0` across ten cross-scene lanes.
- After decoding, the normal LightingShowcase lane reports `raw/temporal/resolved = 2616/2616/9525`; the explicit scene-color-off lane reports `2616/0/0`, proving the control now reaches the GPU shader path.
- A fresh normal `SelfEngineLightingShowcase` visual window is open for acceptance; the earlier window was from before this control fix.

## 2026-07-18 - SSR Temporal Must Keep Current HDR Separate From Deferred History

Symptom:
- Wall reflections produced distance-dependent white fragments and became soft or unstable. Hiding the showcase walls removed almost all of the fragments, proving that the receiver/material was not the source.

Cause:
- SSR reconstruction descriptor binding 7 was created for the current frame's `HdrSceneColor`, but the per-frame history update later overwrote the same binding with the previous temporal history image. The Deferred consumer legitimately needs that previous history at its separate binding 16; SSR Temporal runs after Deferred and needs the current HDR attachment.

Fix:
- Keep binding 7 in `VulkanSsrReconstructionDescriptorSets` bound to the current image-indexed HDR scene color.
- Rebind only the GBuffer/Deferred binding 16 to the previous submitted temporal history.
- Remove the reconstruction-side history update API and expose the current-HDR binding state through the existing Debug CSV contract.

Validation:
- Regenerated `ssr_trace.comp.spv` and `ssr_temporal.comp.spv`.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\\ssr_current_hdr_binding_health` passed `382 / 0` across LightingShowcase controls and animated Forward3D FBX.
- Normal lanes report `currentHdr=1`, history descriptor bound `1`, temporal/spatial dispatches `1/1`, FrameGraph issues `0`, and raw/resolved plus metadata alias masks `0`.
- Visual acceptance is pending on the restored-wall LightingShowcase window.

## 2026-07-18 - SSR Full-Mip HDR Sampling Needs Explicit Per-Range Layout Transitions

Symptom:
- SSR reflections still looked weak and noisy even after the current-HDR path was wired up. Strict health kept failing with `VUID-vkCmdDraw-None-09600` on `SelfEngine.DLSS.InputColor.image[0]` mips `1..10`.

False leads:
- Raising SSR strength, changing roughness, or treating the artifact as a temporal-quality issue.

Cause:
- `ssr_trace.comp` still carried a stale current-HDR sampling binding even though the trace pass only writes hit UV/confidence.
- The mip-generation path assumed the whole HDR image shared one old layout. In reality mip 0 and mip 1+ start in different states, so a single broad barrier left the higher mips in an undefined state for the full-mip descriptor.

Fix:
- Remove the unused current-HDR radiance sampling from `ssr_trace.comp`.
- Split HDR mip generation into separate transitions for mip 0 and mip 1+ in both `command_buffer.cpp` and the generic `image.cpp` mipmap helper.
- Keep the full-mip current-HDR sample only in the temporal pass that runs after mip generation.

Prevention:
- A view that spans multiple mip levels must not be bound until every covered subresource has a concrete layout contract.
- When a pass no longer needs a resource, remove the shader binding instead of leaving an apparently harmless stale dependency behind.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed and regenerated `ssr_trace.comp.spv`.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\\ssr_refinement_health_trace_pruned_v2` passed `420 / 0`.
- The LightingShowcase lane now reports `currentHdr=1`, `mipLevels=11`, `mipReady=1`, and `validationDiagnostics=0`.

## 2026-07-18 - SSR Current HDR Radiance Needs a Small Neighborhood Filter

Symptom:
- Mirror-like materials still showed bright white fragments and blocky highlights, especially on the lower half of the metallic sphere and along other glossy reflection boundaries.

False leads:
- Treating the artifact as another layout bug, shadow seam, or trace-confidence issue.

Cause:
- The current-HDR reflection source was still sampled too directly in `ssr_temporal.comp`. Roughness-selected mips alone were not enough to suppress isolated high-contrast source pixels from the room lights and wall edges.

Fix:
- Add a small weighted neighborhood filter in `SampleCurrentHitRadiance` and clamp the result in YCoCg space.
- Keep the original center-sample path available through `SE_SSR_CURRENT_HDR_FILTER_OFF=1` for Debug comparison.
- Publish `reconstructionCurrentHdrRadianceFilterEnabled` through CSV/stats so the health gate can assert the production path is active.

Prevention:
- When a glossy reflection still shows sparse bright blocks, inspect the radiance input filter before touching SSR tracing or shadow bias.
- A valid mip chain is not enough; current-frame reflection radiance still needs footprint-aware filtering.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\\ssr_refinement_health_current_hdr_filter_v1` passed `420 / 0`.
- The LightingShowcase lane reports `currentHdrSource=1`, `currentHdrFilter=1`, `mipLevels=11`, `mipReady=1`, and `validationDiagnostics=0`.

## 2026-07-18 - SSR Spatial Variance Clamp Needs Support-Based Confidence

Symptom:
- White fragments and speckled blocks remained on glossy spheres and other reflective boundaries even after temporal/current-HDR filtering.

False leads:
- More trace steps, stronger current-HDR filtering, or blaming the depth pyramid / hit validation.

Cause:
- The spatial resolve still let sparse center hits dominate the final color and alpha; it did not clip strongly enough against local variance and neighborhood support.

Control test:
- `SE_SSR_SPATIAL_VARIANCE_CLAMP_OFF=1` versus the default-on lane in `scripts\Test-SsrRefinementHealth.ps1`.
- LightingShowcase and Forward3D both reported `reconstructionSpatialVarianceClampEnabled=1` with `supportTapCount=13`; the off lane reported `0`.

Fix:
- Add support-weighted YCoCg variance clipping in `ssr_spatial.comp`.
- Add the `SE_SSR_SPATIAL_VARIANCE_CLAMP` / `_OFF` control and expose `reconstructionSpatialVarianceClampEnabled` plus `reconstructionSpatialSupportTapCount`.
- Make the confidence output support-aware so sparse outliers fade back to probe/IBL sooner.

Prevention:
- If glossy reflections still show sparse blocks after temporal filtering, inspect the spatial support envelope before revisiting SSR tracing or HDR mip generation.
- Follow the production spatial-denoise / variance-clipping pattern used by AMD FidelityFX SSSR / denoiser-style filters; do not accept a pure bilateral average as the final answer.

Validation:
- Debug `SelfEngineForward3D` and `SelfEngineLightingShowcase` builds passed after `ssr_spatial.comp.spv` regenerated.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\\ssr_spatial_variance_clamp_health` passed `469 / 0`.

## 2026-07-19 - FidelityFX SSSR Passes Need Producer/Consumer Gates Before Visibility

Symptom:
- The project was replacing an unstable custom SSR path with AMD FidelityFX SSSR, but a partial bridge could look like progress without proving that official intermediate resources were actually produced and consumed.

False leads:
- Treating source vendoring, shader compilation, or a single visible scene as enough evidence that the third-party SSR path was functionally integrated.

Cause:
- FidelityFX SSSR is a multi-pass contract. `ClassifyTiles`, `PrepareIndirectArgs`, `PrepareBlueNoiseTexture`, `Intersect`, and later DNSR/reprojection passes each need explicit Vulkan descriptors, resource formats, dispatch ordering, and fallback controls.

Control test:
- `SE_SSR_BACKEND=ffx-sssr` must activate the official pass chain in both LightingShowcase and Forward3D FBX lanes; `SE_SSR_BACKEND=selfengine` must keep the same resources available but suppress all FFX dispatch/bind counts.

Fix:
- Add official AMD 128x128 1spp blue-noise tables, `PrepareBlueNoiseTexture` resources, and `Intersect` resources/dispatches behind the FFX backend.
- Extend `RendererStats`, benchmark CSV, and `scripts\Test-FidelityFxSssrIntegration.ps1` so the bridge reports contract `4`, blue-noise table counts, blue-noise `16x16` groups, Intersect input readiness, indirect dispatch activity, and internal-backend suppression.

Prevention:
- Advance third-party renderer integrations one official pass at a time. Do not connect a new pass to the visible image until data proves its producer, consumer, dimensions, formats, descriptors, dispatches, and fallback lane are correct across two real scenes.

Validation:
- `SelfEngineShaders`, `SelfEngineForward3D`, and `SelfEngineLightingShowcase` Debug builds passed.
- `scripts\Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict -OutputDirectory tmp\\ffx_sssr_intersect_bridge_default_exes` passed `89 / 0`.
- `scripts\Test-SsrRefinementHealth.ps1 -SkipBuild -Strict -OutputDirectory tmp\\ssr_regression_after_ffx_intersect` passed `691 / 0`.

## 2026-07-21 - FFX SSR hit confidence must be independent from radiance alpha

Symptom:
- The same-frame FFX Apply pass needed to distinguish a real screen-space hit from the vendor radiance/environment fallback while selecting a local reflection probe for misses.

False leads:
- Treating `RadianceHistory.a` as hit confidence, or tuning the local probe blend until the visible result looked stable in LightingShowcase.

Cause:
- FFX radiance alpha carries a vendor temporal/composite signal, not a SelfEngine hit-provenance contract. The new confidence image/history also exposed two integration gaps: the Apply shader's zero-direction guard could replace valid components with `-0.0001`, and its FrameGraph input name did not match the existing physical `SceneReflectionProbeCubemap` resource.

Fix:
- Add a dedicated `R32_SFLOAT` hit-confidence producer, reproject it with motion/depth/normal/roughness rejection, and bind it to Apply independently from radiance alpha.
- Disable hit-history reads until the previous FFX history has actually been written.
- Match Apply's local-probe priority/coverage weights to Deferred, use the scene-independent `SceneReflectionProbeCubemap` FrameGraph identity, and keep the internal-backend lane as an explicit no-consumer control.

Prevention:
- Never overload vendor radiance alpha with an engine-owned provenance meaning.
- Every newly sampled resource must have the same identity in shader bindings, FrameGraph reads, physical allocation, and CSV producer/consumer evidence.
- A history resource being allocated is not the same as history being valid; gate first-frame reads with the actual writeback state.

Validation:
- `Test-FidelityFxSssrIntegration.ps1 -StaticOnly -SkipBuild -Strict` passed `59 / 0`.
- Debug and Release shader/renderer builds passed.
- The strict FFX integration matrix passed `1115 / 0` with zero FrameGraph/Vulkan diagnostics.
- The SSR/Hi-Z regression passed `691 / 0` across LightingShowcase and the animated Forward3D FBX lane.

## 2026-07-21 - Glossy Overlapping Probes Need Dominant Selection

Symptom:
- Forcing captured Probe MIP0 made the fixture bars and housing crisp, but a metal receiver showed duplicated room/fixture silhouettes.

False leads:
- Increasing capture resolution, disabling global IBL, or treating the artifact as a missing captured object.

Cause:
- Two spatially overlapping local Probes were both valid and were mixed at `0.257578 / 0.742422`. Their different capture positions and Box Projection directions produced parallax-inconsistent mirror images. The FFX Apply fallback used the same multi-Probe blend contract and therefore had to agree with Deferred.

Control test:
- Default `SE_REFLECTION_PROBE_DOMINANT_MIRROR_OFF` unset: roughness `0.24` resolves effective mask `0x2`, dominant weight `1.0`.
- `SE_REFLECTION_PROBE_DOMINANT_MIRROR_OFF=1`: the same receiver returns effective mask `0x3` and dominant weight `0.742422`.

Fix:
- Use coverage/volume-priority dominant Probe selection for glossy materials, with a roughness-dependent transition from dominant-only at roughness `<=0.30` to regular multi-Probe blending by `0.60`.
- Apply the same rule in Deferred environment resolution and FFX SSSR Apply fallback; keep the control Debug-only and scene-independent.

Prevention:
- Do not solve glossy Probe ghosting by disabling IBL or lowering capture quality. Audit normalized Probe weights and Box Projection identity first, then use roughness-aware dominant selection for overlapping mirror views.

Validation:
- `Test-SsrRefinementHealth.ps1 -SkipBuild -Strict` passed `769 / 0` with default and dominant-selection-off lanes.
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict` passed `1109 / 0` with zero warnings/failures.

## 2026-07-21 - Reflection Probe Captures Must Exclude the Receiving Surface

Symptom:
- A polished metal sphere showed a distinct sphere-shaped image even though no second sphere existed in the corresponding direction. The image disappeared when the sphere was excluded from reflection capture.

Cause:
- The local captured-scene Probe overlapped the receiver's bounds. The receiver was therefore rendered into its own cubemap and later sampled that cubemap as environment reflection. This is a capture-ownership error, not a missing scene object or an SSR ray hit.

Control test:
- Keep the receiver in the main camera and normal shadow queues, but set its scene-owned `ReflectionCaptureVisible` flag to `false`.
- Compare the real `SelfEngineLightingShowcase` with Probe Mip0 forced and FFX SSSR enabled. The suspected self-image must disappear while wall, fixture, and other-geometry reflections remain.

Fix:
- Add a generic `Renderable3D` reflection-capture visibility contract with a default of `true`.
- Filter the flag from captured-scene color draws, capture-side shadow draws, capture geometry signatures, and invalidation accounting. Do not branch on scene names, object names, or Probe indices in the renderer.
- Opt the showcase's metal receiver out through scene-owned data only.

Prevention:
- Every reflection-capture receiver must have an explicit capture-visibility policy when a Probe can overlap its bounds. Do not diagnose a self-image by disabling SSR, lowering Probe quality, or hiding the object from the main camera.
- Capture filtering must be applied consistently to color, shadow, geometry-signature, and debug-stat paths; otherwise stale self-content can survive after the visible draw is removed.

Validation:
- `Test-ReflectionCaptureHealth.ps1 -SkipBuild -Strict` passed `1132 / 0 / 0` across capture reuse, invalidation, filter, Mip0, multi-Probe, Box Projection, rect-light, and capture-shadow lanes.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict` passed `809 / 0` with zero FrameGraph/Vulkan diagnostics.
- `Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict` passed `1115 / 0` across the official FFX SSSR bridge and controls.
- Real `SelfEngineLightingShowcase` visual acceptance confirmed that the suspected self-reflection disappeared.

## 2026-07-21 - Measure SSR Coverage Before Tuning Probe Fallback

Symptom:
- Forcing captured Probe Mip0 made nearby geometry sharp but exposed incorrect perspective, hollow-looking pedestals, and duplicate silhouettes. Restoring roughness-filtered Probe mips removed the sharp duplication but left the polished sphere looking like blurred IBL.

Cause:
- The LightingShowcase FFX attribution lane classified `156622` reflection rays: `4070` high-confidence screen-space hits, `1749` partial hits, and `150803` environment fallbacks. About `96.3%` of classified rays therefore had no usable screen-space geometry. A single-point cubemap cannot reconstruct accurate near-field parallax, and a roughness mip can only blur that spatial error.

False leads:
- Forcing Mip0, sharpening the Probe, lowering material roughness, or cross-fading misaligned SSR and Probe images more aggressively.
- Treating a valid FFX dispatch/resource chain as proof that the visible receiver has enough screen-space hit coverage.

Production direction:
- Follow AMD FidelityFX Hybrid Reflections: use SSSR for reliable on-screen hits, hardware ray tracing for off-screen/disoccluded geometry, and Probe/IBL only as the final distant/environment fallback.
- Keep the RT path optional. Unsupported devices must retain the current SSSR plus Probe fallback without failing logical-device creation.

Validation:
- The RTX 5070 Ti reports `VK_KHR_acceleration_structure`, `VK_KHR_deferred_host_operations`, `VK_KHR_ray_query`, and `VK_KHR_ray_tracing_pipeline` support.
- `Test-HybridReflectionsCapability.ps1 -SkipBuild -Strict` passed `44 / 0` across LightingShowcase, animated Forward3D, explicit-disable, and not-requested lanes.
- Requested lanes report Ray Query hardware/device readiness `1/1`, runtime resources `0`, and explicit pending fallback `6`; the disable lane reports device readiness `0` and fallback `2`.
- Existing FFX SSSR and SSR/Hi-Z regressions remain at `1115 / 0` and `809 / 0`.

## 2026-07-21 - Build Hybrid Reflection AS From the Full Scene

Symptom:
- Screen-space attribution showed that most glossy reflection rays had no usable on-screen hit, but a camera-culled acceleration structure would also drop off-screen geometry that Ray Query is meant to recover.

False leads:
- Reusing the visible main render queue for TLAS instances.
- Giving every 2D, transparent, or skinned buffer device-address/AS-input usage merely because Ray Query was enabled on the device.
- Treating a valid TLAS address as proof that ray-traced reflection radiance is already active.

Cause:
- Hybrid reflections require a scene-level geometry producer with ownership independent of main-camera visibility. Static/rigid geometry can share BLAS resources; skinned and non-opaque geometry need separate update/intersection policies and must remain measurable fallbacks until those paths exist.

Control test:
- Compare requested/supported lanes against explicit-disable and not-requested lanes.
- The requested lanes must build non-empty BLAS/TLAS resources from an unculled queue, while control lanes must retain zero BLAS/TLAS counts, addresses, and memory.
- The animated Forward3D lane must count its skinned model as fallback instead of silently tracing its bind pose.

Fix:
- Add device-address and AS-build-input usage only to requested 3D mesh buffers.
- Cache static/rigid BLAS resources, allocate one update-capable TLAS per swapchain image, and rebuild frame-slot ownership after swapchain recreation.
- Record host-to-build, BLAS-to-TLAS, and TLAS-to-compute/fragment Ray Query barriers with Debug CSV lifecycle and memory diagnostics.

Prevention:
- Never build an off-screen fallback structure from the camera-frustum queue.
- Never call AS resources "active reflections" until a shader consumer is bound and produces validated hit/radiance data.
- Clear lane output files before every health run so a blocked executable cannot pass by reusing stale CSV data.

Validation:
- `Test-HybridReflectionsCapability.ps1 -SkipBuild -Strict` passed `53 / 0` across LightingShowcase, animated Forward3D, explicit-disable, and not-requested lanes with zero Vulkan validation messages.
- LightingShowcase reported `51` TLAS instances and `4` ready BLAS entries; Forward3D reported `8` rigid instances, `3` ready BLAS entries, and `1` skinned fallback.
- `Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict` passed `1115 / 0`.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict` passed `809 / 0`.

## 2026-07-21 - Ray Query Consumers Need Typed Producer Contracts

Symptom:
- The first hybrid-reflection consumer run recorded one indirect dispatch but returned zero candidate rays and emitted `VUID-VkWriteDescriptorSet-descriptorType-08765` for every frame descriptor.

False leads:
- Treating the empty diagnostics as a bad TLAS, invalid reflection direction, insufficient ray distance, or readback synchronization failure.
- Enabling visible RT composition before proving the compact-ray producer/consumer contract.

Cause:
- FFX owns the ray counter as an `RWBuffer<uint>` backed by a storage-texel buffer. The first consumer declared it as read-only `Buffer<uint>` and bound the same view as `UNIFORM_TEXEL_BUFFER`, but the producer buffer intentionally lacks uniform-texel usage.
- The first health window was also shorter than the swapchain-image fence/readback cycle, so a valid dispatched image had not yet returned to the CPU.

Control test:
- Run requested LightingShowcase and animated Forward3D lanes, an independent `SE_HYBRID_REFLECTIONS_RAY_QUERY_OFF=1` lane, the whole-RT-off lane, and a not-requested lane.
- Assert `candidate = screenAccepted + trace + invalid` and `trace = committedHit + miss` only after the corresponding image fence.

Fix:
- Preserve the FFX ray counter's storage-texel descriptor type in the consumer and keep the ray list as its existing uniform-texel producer contract.
- Record enough frames to recycle per-image submissions, keep diagnostics atomics/readback Debug-only, and leave results in an independent `R32G32_UINT` image until hit shading is validated.

Prevention:
- Derive descriptor type from both shader access and the producer buffer's Vulkan usage flags; a logically read-only consumer does not permit rebinding an `RWBuffer` producer as a different descriptor class.
- A dispatch count is not proof of useful GPU work. Require nonzero data plus conservation equations and explicit disable/resource-free controls.
- Do not publish a Ray Query hit buffer as active reflections until material-aware hit shading and visible composition have separate passing gates.

Validation:
- `Test-HybridReflectionsCapability.ps1 -SkipBuild -Strict` passed `91 / 0` with zero Vulkan validation messages.
- LightingShowcase recorded `156599` candidates, `152510` traces, `57975` committed hits, and `94535` misses; Forward3D recorded `6379`, `6234`, `3049`, and `3185` respectively.
- `Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict` passed `1115 / 0`.
- `Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict` passed `809 / 0`.

## 2026-07-21 - Ray Query Hit Attributes Need ABI and Device-Feature Closure

Symptom:
- Ray Query returned valid hit distance and instance IDs, but those values were insufficient to shade off-screen geometry or prove which triangle, vertex attributes, and material had been selected.
- The first attribute-enabled runtime produced correct data but Vulkan validation reported SPIR-V `Int64` without `VkPhysicalDeviceFeatures::shaderInt64` enabled.

False leads:
- Treating a committed triangle hit as proof that material identity and vertex data were valid.
- Adding a new bindless descriptor-array subsystem or changing shader languages before using the engine's existing device-address buffers and DXC path.
- Treating `bufferDeviceAddress` support as sufficient for every HLSL physical-address operation.

Cause:
- Vulkan Ray Query built-ins identify the instance and primitive but do not supply application vertex attributes or material ownership. Those must be resolved through an instance record whose identity matches the TLAS build order.
- DXC lowers `vk::RawBufferLoad` from a 64-bit physical address with the SPIR-V `Int64` capability, which requires `shaderInt64` to be both supported and enabled on the logical device.

Control test:
- Compare normal requested lanes with `SE_HYBRID_REFLECTIONS_HIT_ATTRIBUTES_OFF=1`: both must dispatch and trace the same class of rays, while only the normal lane may populate attribute counters.
- Keep independent consumer-off, RT-off, and not-requested lanes to distinguish metadata ownership from dispatch and device-resource ownership.

Fix:
- Store vertex/index addresses, counts, vertex stride, and dense material index in one 32-byte record per unculled TLAS instance; make `instanceCustomIndex` equal the record index.
- Resolve triangle indices and `Vertex3D` position/normal/UV with official DXC physical-address loads, use full barycentrics `(1-u-v,u,v)`, and transform positions/normals with the committed object/world matrices.
- Add compile-time CPU layout assertions, GPU conservation/range/checksum diagnostics, and make `shaderInt64` part of Ray Query hardware/device readiness.

Prevention:
- Never enable visible ray-traced radiance from hit distance alone. Require instance identity, bounds-checked primitive/vertex access, material fallback accounting, and reconstructed-position agreement first.
- Every SPIR-V capability introduced by a shader change must appear in the physical-feature query, logical-device enablement, CSV contract, and disabled/unsupported test lanes.
- Build hit metadata from the unculled scene queue so off-screen geometry retains the identity needed by the off-screen fallback.

Validation:
- `spirv-val --target-env vulkan1.2` passes; disassembly contains Ray Query, Physical Storage Buffer, `Int64`, binding `11`, metadata `ArrayStride 32`, barycentrics, and object/world transform operations.
- `Test-HybridReflectionsCapability.ps1 -SkipBuild -Strict` passed `138 / 0` across six lanes with zero Vulkan diagnostics. LightingShowcase resolved `57976 / 57976` hits and Forward3D resolved `3055 / 3055`; both had zero invalid records, zero position mismatch, and `2 micrometers` maximum position error.
- `Test-FidelityFxSssrIntegration.ps1 -SkipBuild -Strict` passed `1115 / 0`; `Test-SsrRefinementHealth.ps1 -SkipBuild -SkipSigning -Strict` passed `809 / 0`.
