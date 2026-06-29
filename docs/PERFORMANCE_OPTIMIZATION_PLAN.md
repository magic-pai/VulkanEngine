# SelfEngine Performance Optimization Plan

This document is the renderer performance roadmap. The goal is not to chase one demo-specific trick, but to make the engine's default rendering path cheaper so every later scene benefits.

## North Star

- Keep frame time measurable: every optimization must show where CPU or GPU time moved.
- Maximize FPS / minimize frame time as the highest priority. Visual quality, feature breadth, and tooling polish are secondary unless they protect correctness or measurement.
- Optimize the common path first: queue building, command recording, resource binding, uploads, culling, and shader cost.
- Prefer scalable renderer structure over scene-specific hacks.
- Treat scenes as tests, not as optimization targets: optimizations should live in engine-level systems so future upper-layer scenes inherit the win.
- After each optimization stage, use a high-pressure 3D scene to verify the engine-level win, but do not tune the engine specifically for that scene.
- Keep quality ladders available: high-end defaults can look good, but lower tiers must be one setting away.
- Preserve correctness before speed: no optimization should silently break depth, shadows, material color, model import, or resize behavior.

## Measurement First

Current status:

- CPU frame breakdown exists in the ImGui Performance panel.
- GPU timestamps exist for shadow, main, overlay, ImGui, and total recorded GPU time.
- Draw counts, triangle counts, culling counts, bind counts, and push constant traffic are visible.

Next steps:

- Add more benchmark scenes that are deterministic: large imported model, black hole shader stress, shadow stress.
- Track p50/p95 CPU and GPU frame times from exported CSV, not only FPS.
- Add release-build benchmarking separate from validation/debug builds.
- Add RenderDoc capture checkpoints for major renderer changes.

Implemented:

- `SE_BENCHMARK_FRAMES` enables scripted benchmark capture for every Application-based executable.
- `SE_BENCHMARK_WARMUP_FRAMES` controls how many rendered frames are skipped before capture.
- `SE_BENCHMARK_CSV` controls the output CSV path.
- `SE_BENCHMARK_SCENE=grid` enables a deterministic Forward3D static-grid stress scene.
- `SE_BENCHMARK_GRID_SIZE` controls the Forward3D grid width/depth.
- Benchmark CSV includes bounds, command, visibility, whole-queue, instance-batch, and instance-buffer upload cache hit/miss counters so low-level cache behavior is visible.
- High-pressure validation uses the same Forward3D grid scene with larger `SE_BENCHMARK_GRID_SIZE` values. The scene is an acceptance test for lower-layer renderer changes, not the target of the optimization.
- `SE_ENABLE_GPU_TIMESTAMPS=1` enables GPU timestamp breakdown. It is disabled by default so FPS-first runs avoid profiling command overhead.
- `SE_FPS_FIRST=1` applies engine-level FPS-first defaults. Currently this disables the shadow pass.
- `SE_SHADOW_QUALITY=off|low|medium|high|ultra` overrides the startup shadow preset for all scenes.

Example:

```powershell
$env:SE_BENCHMARK_SCENE='grid'
$env:SE_BENCHMARK_GRID_SIZE='16'
$env:SE_FPS_FIRST='1'
$env:SE_BENCHMARK_WARMUP_FRAMES='30'
$env:SE_BENCHMARK_FRAMES='180'
$env:SE_BENCHMARK_CSV='out\benchmarks\forward_grid.csv'
.\out\build\SelfEngineForward3D.exe
.\scripts\Summarize-BenchmarkCsv.ps1 -Path 'out\benchmarks\forward_grid.csv'
```

For a GPU breakdown run, add:

```powershell
$env:SE_ENABLE_GPU_TIMESTAMPS='1'
```

For shadow-quality comparison, run the same benchmark with different values:

```powershell
$env:SE_SHADOW_QUALITY='off'
$env:SE_SHADOW_QUALITY='medium'
```

## CPU Frame Orchestration

Optimization options:

- Reduce per-frame allocations by reserving and reusing vectors, queues, temporary material lists, and file/model loader buffers.
- Avoid rebuilding unchanged render state: cache transforms, material push data, bounds, and sort keys.
- Split static and dynamic renderables: static commands can be rebuilt only when scene data changes.
- Add scene dirty flags: transform dirty, material dirty, mesh dirty, visibility dirty.
- Avoid per-frame string/id lookups on the render path.
- Move expensive editor-only UI and picking work behind toggles.
- Batch material camera-light updates so only materials that need camera-dependent data are touched.

Current priority:

- Keep shaving queue build cost and command recording cost before adding more rendering features.

Implemented:

- 3D world-bounds caching now lives in `RenderQueue`, keyed by renderable, mesh, transform matrix version, and model matrix. It preserves per-frame material push data while avoiding repeated AABB transforms for unchanged geometry.
- Forward3D grid 24 benchmark evidence on the local machine:
  - Before bounds cache: `cpu_queue_build_ms` p50 about `2.59 ms`, `cpu_total_ms` p50 about `6.19 ms`.
  - After bounds cache: `cpu_queue_build_ms` p50 about `0.91 ms`, `cpu_total_ms` p50 about `4.40 ms`.
  - Cache verification after warmup: `main_bounds_cache_hits` p50 `577`, `main_bounds_cache_misses` p50 `0`.
- Higher-pressure Forward3D grid 40 validation after bounds cache:
  - `main_draws` p50 `781`, `shadow_draws` p50 `781`.
  - `cpu_queue_build_ms` p50 about `2.41 ms`.
  - `cpu_command_record_ms` p50 about `3.12 ms`.
  - `cpu_total_ms` p50 about `8.00 ms`.
  - `main_bounds_cache_hits` p50 `1601`, `main_bounds_cache_misses` p50 `0`.
  - This shows the cache scales correctly, and the next FPS-first target should be CPU command recording / draw submission cost.
- GPU timestamp profiling is now opt-in because the default path should maximize FPS. Use `SE_ENABLE_GPU_TIMESTAMPS=1` only when GPU pass timings are needed.
- FPS-first startup settings are now exposed at the renderer layer. `SE_FPS_FIRST=1` disables the shadow pass for all scenes, cutting shadow draw/recording work without changing scene code.
- Forward3D grid 40 FPS-first validation with shadow draw pass disabled:
  - `shadow_draws` p50 `0`.
  - `push_constant_updates` p50 reduced from `1566` to `785`.
  - `cpu_command_record_ms` p50 about `1.74 ms`.
  - `cpu_total_ms` p50 about `4.34 ms`.
  - The renderer still records an empty shadow layout pass when needed so the sampled shadow descriptor stays in a valid layout.
- Main-pass 3D instancing is now available for consecutive commands that share mesh, material, draw order, and tint.
  - This is implemented in the renderer/command-buffer layer, not in the grid scene.
  - Forward3D grid 40 FPS-first validation after instancing:
    - `main_instanced_draws` p50 `3`.
    - `main_instanced_instances` p50 `780`.
    - `push_constant_updates` p50 reduced from `785` to `8`.
    - `cpu_command_record_ms` p50 about `0.235 ms`.
    - `cpu_total_ms` p50 about `2.75 ms`.
  - The next FPS-first target is queue build / culling, because command recording is no longer the dominant cost in this pressure scene.
- Resource ID lookup caches were added to `VulkanRenderResources2D`, but the isolated grid 40 run did not move the main bottleneck in the right direction:
  - Forward3D grid 40 FPS-first validation after resource lookup caching:
    - `cpu_queue_build_ms` p50 about `2.72 ms`.
    - `cpu_total_ms` p50 about `3.31 ms`.
  - The result reinforced that repeated command construction, not only string lookup, was the hotter path.
- 3D render command skeleton caching now lives in `RenderQueue`, keyed by renderable identity, render-state version, and transform matrix version.
  - Cached commands preserve geometry, sort keys, bounds, and cast-shadow state.
  - Material push constants are refreshed every frame so camera-dependent materials and ImGui edits remain correct.
  - Forward3D grid 40 FPS-first validation after command caching:
    - `main_command_cache_hits` p50 `1601`, `main_command_cache_misses` p50 `0`.
    - `cpu_queue_build_ms` p50 about `1.37 ms`.
    - `cpu_command_record_ms` p50 about `0.222 ms`.
    - `cpu_total_ms` p50 about `1.96 ms`.
  - The next FPS-first target is redundant sorting / static queue rebuild work, because command recording is already very small and queue build remains the largest CPU slice.
- 3D material push constant refresh is now delayed until after visibility has been resolved.
  - Culled objects no longer refresh CPU-side material push constants for a command that will not be submitted.
  - Forward3D grid 40 FPS-first validation after visible-only material refresh:
    - `cpu_queue_build_ms` p50 about `1.26 ms`.
    - `cpu_total_ms` p50 about `1.89 ms`.
- Per-renderable frustum visibility caching now lives beside the 3D command cache in `RenderQueue`.
  - If the frustum signature, transform version, render-state version, and renderable identity are unchanged, the renderer reuses the previous visible/culled decision.
  - If the camera or object moves, the cached decision is naturally invalidated and the AABB/frustum test runs again.
  - Forward3D grid 40 FPS-first validation after visibility caching:
    - `main_visibility_cache_hits` p50 `1601`, `main_visibility_cache_misses` p50 `0`.
    - `cpu_queue_build_ms` p50 about `1.20 ms`.
    - `cpu_total_ms` p50 about `1.86 ms`.
- RenderQueue now has a 3D scene-queue fast path keyed by a safe build signature.
  - The signature includes renderable order/identity, render-state version, transform matrix version, frustum signature, and shadow-caster filtering mode.
  - When the signature is unchanged, the renderer reuses the already culled and sorted 3D command list, then refreshes visible material push constants so ImGui/camera-dependent materials stay correct.
  - This keeps the optimization in the renderer layer; scenes are still only validation inputs.
  - Forward3D grid 40 FPS-first validation after scene-queue caching:
    - `main_queue_cache_hits` p50 `1`, `main_queue_cache_misses` p50 `0`.
    - `main_command_cache_hits` p50 `1601`.
    - `main_visibility_cache_hits` p50 `1601`.
    - `cpu_queue_build_ms` p50 about `0.22 ms`.
    - `cpu_total_ms` p50 about `0.77 ms`.
- `Transform3D` is now a versioned transform with setter/getter APIs.
  - Matrix-affecting changes to position, rotation, and scale increment the matrix version immediately.
  - `MatrixVersion()` no longer calls `Matrix()` or compares position/rotation/scale, so scene-queue signatures can read transform versions cheaply.
  - Forward3D, RuntimeModelLoader, Scene3D animation, and ImGui 3D transform editing now use the versioned setters.
  - Forward3D grid 40 FPS-first validation after versioned transforms:
    - `main_queue_cache_hits` p50 `1`, `main_queue_cache_misses` p50 `0`.
    - `cpu_queue_build_ms` p50 about `0.14 ms`.
    - `cpu_total_ms` p50 about `0.69 ms`.
- Scene3D now exposes membership and render revisions driven by a change-notification chain.
  - Matrix-affecting `Transform3D` setter changes notify `Renderable3D`.
  - Render-state changes and transform changes notify `Scene3D`.
  - Forward3D and renderer-owned overlay 3D queues pass scene identity plus membership/render revisions into `RenderQueueBuildOptions`.
  - RenderQueue uses those revisions for an O(1) scene-queue signature instead of scanning every renderable when the caller provides revision data.
  - Forward3D grid 40 FPS-first validation after scene revision signatures:
    - `main_queue_cache_hits` p50 `1`, `main_queue_cache_misses` p50 `0`.
    - `cpu_queue_build_ms` p50 about `0.12 ms`.
    - `cpu_total_ms` p50 about `0.71 ms`.
- Scene-queue cache hits now reuse the current command vector in place when possible.
  - The fallback still copies from the cached command list if the current vector was externally cleared or has the wrong size.
  - Forward3D grid 40 FPS-first validation after avoiding repeated command-list copies:
    - `main_queue_cache_hits` p50 `1`, `main_queue_cache_misses` p50 `0`.
    - `cpu_queue_build_ms` p50 about `0.11 ms`.
    - `cpu_total_ms` p50 about `0.66 ms`.
- Main-pass instancing now caches batch construction and per-image instance-buffer uploads.
  - If the main 3D queue cache is reused, the renderer reuses the previous instance batches and instance matrix list instead of scanning the command list again.
  - Each swapchain image tracks the uploaded instance signature, so unchanged static instance data is not copied to the instance vertex buffer every frame.
  - Forward3D grid 40 FPS-first validation after instance batch/upload caching:
    - `main_instance_batch_cache_hits` p50 `1`, `main_instance_batch_cache_misses` p50 `0`.
    - `main_instance_buffer_uploads` p50 `0`, `main_instance_buffer_upload_skips` p50 `1`.
    - `cpu_command_record_ms` p50 about `0.15 ms`.
    - `cpu_total_ms` p50 about `0.63 ms`.
- An already-sorted queue skip was tested and reverted.
  - Maintaining submit-order flags added comparison overhead to the hot path.
  - Forward3D grid 40 FPS-first moved in the wrong direction, so the optimization was not kept.

## Render Queue And Visibility

Optimization options:

- Keep 2D and 3D queue paths separate when their data needs differ.
- Frustum-cull 3D before sorting and command recording.
- Build shadow queues from already-visible queues when acceptable.
- Add light-frustum culling for shadow casters when the scene grows.
- Add hierarchical bounds or a BVH/octree once object count becomes large.
- Add occlusion culling later, after depth prepass or Hi-Z infrastructure exists.
- Add LOD selection based on projected screen size.
- Add impostors/billboards for far repeated geometry.

Current priority:

- Make 2D commands lightweight because 2D does not currently need world bounds for the main render pass.
- Keep 3D commands rich because culling, shadows, and picking need accurate bounds.

## Command Recording

Optimization options:

- Deduplicate pipeline, descriptor, material, and mesh binds.
- Split push constants by update frequency.
- Use secondary command buffers for parallel recording once draw counts justify it.
- Cache static command buffers for static-only passes, invalidating only on swapchain/resource changes.
- Use indirect draw or multi-draw style batching when mesh/material layouts stabilize.
- Reduce command buffer reset/re-record work for unchanged scenes.
- Keep shadow, main, overlay, and UI pass costs visible independently.

Current priority:

- Continue reducing redundant command traffic before adding parallel command recording.

## Descriptors And Materials

Optimization options:

- Stop recreating all material descriptors when only one imported material changes.
- Add descriptor cache/free-list for material textures.
- Move toward bindless or descriptor indexing when the renderer needs many materials.
- Separate material data into GPU buffers instead of pushing large material constants.
- Group draw calls by pipeline, material, and mesh where ordering allows.
- Add material dirty flags so cached push data refreshes only when properties change.

Current priority:

- Reduce descriptor churn during model import and drag-drop loading.

## Mesh And Geometry

Optimization options:

- Merge static meshes with the same material when imported as many tiny primitives.
- Build meshlet/cluster metadata offline for future culling and LOD.
- Reorder indices for vertex cache locality.
- Strip unused vertex attributes per pipeline.
- Use 16-bit indices when possible.
- Add tangent/normal generation only when a material actually needs it.
- Cache uploaded meshes by model file hash and import settings.

Current priority:

- Add model import caching and avoid duplicate mesh/material uploads.

## Resource Uploads And Memory

Optimization options:

- Batch uploads through one staging path instead of many immediate submits.
- Use persistent mapped upload buffers for small dynamic data.
- Add transient upload ring buffers per frame.
- Pool Vulkan buffers/images and recycle them.
- Keep texture loading, mip generation, and format conversion off the render hot path.
- Generate mipmaps and compressed textures offline where possible.

Current priority:

- Make runtime model loading reuse upload batches and cached assets.

## GPU Pipeline And Shaders

Optimization options:

- Keep shader variants explicit: 2D, forward 3D, black hole, shadow, debug.
- Add quality tiers for expensive fragment shaders.
- Avoid dynamic branches in hot fragment paths where variants are cleaner.
- Use early depth where possible; avoid unnecessary alpha/discard on opaque geometry.
- Add depth prepass only when overdraw proves expensive.
- Add shader specialization constants for quality switches.
- Move repeated black hole math into cheaper approximations or lookup textures where visual loss is acceptable.
- Add dynamic resolution for heavy full-screen effects.

Current priority:

- Put black hole shader quality under measurable controls after the common renderer path is leaner.

## Shadows

Optimization options:

- Skip shadow pass when disabled, strength is zero, or there are no casters.
- Cull shadow casters.
- Fit light projection to caster bounds.
- Add shadow quality presets: off, low, medium, high.
- Cache shadows for static lights/static casters.
- Use lower shadow resolution on GPU-bound frames.
- Add cascaded shadows only after the single-map path is solid.

Current priority:

- Keep shadow map size and filtering tunable, then add light-frustum culling.

## Swapchain And Frame Pacing

Optimization options:

- Test FIFO, mailbox, and immediate present modes where supported.
- Avoid full device idle except during unavoidable swapchain/resource rebuilds.
- Use frames-in-flight consistently.
- Track acquire/present wait time to separate CPU stall from render cost.
- Recreate swapchain resources minimally.

Current priority:

- Reduce device-idle paths triggered by settings changes and resource imports.

## Multithreading

Optimization options:

- Load/import assets on worker threads.
- Build static render queues and bounds in jobs.
- Parallelize visibility for large object counts.
- Parallel command recording with secondary command buffers.
- Keep Vulkan object creation synchronized and isolated.

Current priority:

- Delay broad multithreading until the single-thread path is measured and clean; otherwise it hides waste instead of removing it.

## Asset Pipeline

Optimization options:

- Convert imported models into an engine-native cache format.
- Store processed vertices, indices, materials, bounds, textures, and thumbnails.
- Hash source files plus import settings.
- Skip Assimp on startup when cache is valid.
- Precompute tangents, bounds, LODs, mesh merge groups, and texture mips.

Current priority:

- Native model cache is one of the highest ROI tasks after the render hot path cleanup.

## Build And Runtime Modes

Optimization options:

- Keep Debug + validation for development.
- Add RelWithDebInfo for profiling.
- Add Release for benchmark comparison.
- Make validation layers optional at runtime.
- Avoid compiling unrelated targets during normal launch tasks.

Current priority:

- Continue using incremental CMake targets and add benchmark-friendly launch tasks.

## Incremental Execution Order

1. Finish cheap common-path cleanup: 2D lightweight commands, cached 3D world bounds, fewer redundant queue operations, fewer per-frame material updates.
2. Add deterministic benchmark scenes and CSV stats export. Started: common CSV export, Forward3D grid stress scene, and CSV p50/p95 summary are implemented.
3. Reduce CPU command recording and draw submission cost. Started: main-pass 3D instancing collapses repeated mesh/material draws.
4. Add asset/model cache to remove slow Assimp startup and drag-drop stalls.
5. Add static/dynamic render queue split with dirty flags. Started: unchanged 3D renderable command skeletons and whole 3D scene queues are now reused across frames when the safe build signature is unchanged. `Transform3D` now has versioned setters and `Scene3D` exposes membership/render revisions for O(1) scene-queue signatures.
6. Add descriptor/material update granularity.
7. Add shadow light-frustum culling and static shadow caching.
8. Add black hole quality ladder and dynamic resolution.
9. Add secondary command buffers/job system only after benchmarks show CPU command recording is still a bottleneck.
10. Add LOD and hierarchical visibility for large scenes.
11. Add release-mode benchmark automation so future changes cannot regress unnoticed.

## Current Slice

The immediate optimization is to keep reducing bottom-layer queue cost without baking assumptions into any scene. The current slice has moved from bounds-only caching to reusable 3D command skeleton caching, frustum visibility caching, whole 3D scene-queue caching, versioned `Transform3D` setters, and `Scene3D` membership/render revisions; Forward3D grid is used only as a deterministic verification scene. The next slice should look at the new dominant costs: command recording, material refresh frequency, and whether static command buffers or indirect drawing provide better returns than more queue-build work.
