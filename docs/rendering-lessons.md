# SelfEngine Rendering Lessons

This file records compact debugging lessons for SelfEngine rendering issues. Keep entries practical: symptom, false leads, cause, control test, fix, prevention, validation.

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
