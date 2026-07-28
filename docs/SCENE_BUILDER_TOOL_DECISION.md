# SelfEngine Scene Builder Tool Decision

Date: 2026-07-26

## Decision

The runtime scene-building tool is built on the already vendored Dear ImGui with
numeric controls only. No new editor framework, no ECS, and no viewport gizmo
library is adopted in this first slice.

## Candidate Survey

| Candidate | Source | Decision | Why |
| --- | --- | --- | --- |
| Dear ImGui (vendored) | https://github.com/ocornut/imgui | Adopt | Already integrated at `thirdParty/imgui` with a working Vulkan/GLFW backend and an existing `VulkanImGuiLayer`. Numeric widgets cover transform and PBR editing with zero new dependencies. |
| nlohmann/json | https://github.com/nlohmann/json | Adopt | Already fetched and linked by `CMakeLists.txt`, MIT licensed, and used by existing project code. It provides a versioned, readable local scene document without a new dependency. |
| RapidJSON | https://github.com/Tencent/rapidjson | Reject | A viable C++ JSON parser, but would duplicate the existing JSON dependency and require a second serialization style for no editor benefit. |
| ImGuizmo | https://github.com/CedricGuillemet/ImGuizmo | Reject for this slice | Only solves viewport gizmo manipulation, which the user explicitly excluded. It would add viewport interaction, picking, and matrix-ownership coupling before the runtime mutation and queue-invalidation contract is proven. |
| imgui_entt_entity_editor | https://github.com/Green-Sky/imgui_entt_entity_editor | Reject | Hard dependency on EnTT. SelfEngine has no ECS; adopting it would mean rewriting `Scene3D` ownership to satisfy a tool. |
| ImStudio | https://github.com/Raais/ImStudio | Reject | GUI layout designer for authoring ImGui code, not a scene/entity inspector. |
| Full editor framework (Qt, custom docking shell) | - | Reject for this slice | Out of proportion to the requested scope and would introduce a second application/UI lifecycle next to the existing renderer loop. |
| Embree | https://github.com/RenderKit/embree | Reject for primitive picking | Apache-2.0 and actively maintained, but a CPU triangle/BVH integration would add mesh extraction and BVH lifetime management solely to select four analytic editor primitives. |
| tinybvh | https://github.com/jbikker/tinybvh | Reject for primitive picking | MIT and actively maintained, but still needs a general triangle-data/BVH adapter. It provides no correctness advantage for the four fixed primitive surfaces. |

No third-party library can take over an inspector for SelfEngine's own
`Scene3D` / `Renderable3D` / `VulkanMaterial` model without an adapter layer
that is larger than the panel itself. A thin panel over vendored ImGui is the
smallest correct option.

## Superseding Decision: Viewport Translation Gizmo

The original ImGuizmo rejection is superseded by the explicit request for a
standard X/Y/Z drag control. ImGuizmo is adopted at upstream commit
`dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d` (its `v1.92.5 WIP` marker), with
its MIT license retained in `thirdParty/imguizmo/LICENSE`.

It is a C++ Dear ImGui extension with no renderer backend or binary dependency,
so it fits the existing GLFW/Vulkan ImGui pass on Windows. Its source is linked
only by `SelfEngineForward3D`, the active Scene Builder host. The integration
uses only `TRANSLATE` in `WORLD` mode and writes the resulting position through
`SceneBuilder::ApplyObjectEdit`; Scene3D, PBR, shadowing, persistence, and the
normal queues remain authoritative.

Im3d remains rejected for this feature. It would require a custom 3D line
renderer, depth policy, and input adapter before it supplied a transform tool.
ImGuizmo instead supplies axis hit testing, translation-plane solving, hover
behavior, screen-size management, and drag math directly. The first slice
excludes rotation, scale, snapping, and undo/redo.

### Viewport Selection Extension

The 2026-07-26 precise-picking extension replaces the builder viewport's
fixed-unit-AABB picker with standard analytic local-surface intersections. Cube,
plane, sphere, and cone each use their real generated-mesh surface, and the
builder selects the closest valid hit along the world-space click ray. This
avoids a cone, plane, or sphere bounding box falsely blocking a visible object
behind it.

Embree and tinybvh were checked on 2026-07-26. They remain deliberately
unintegrated: an acceleration structure is the correct next step only when the
editor accepts arbitrary imported triangle meshes. The current fixed primitive
set has an exact, allocation-free solution with smaller integration scope.

The extension deliberately has no visual output: the former projected
unit-bounds wireframe was not a real mesh silhouette and could detach from the
rendered object during camera movement.

Dear ImGui is MIT licensed, active as of the check on 2026-07-26, and remains
the panel implementation. ImGuizmo is also active and MIT licensed, but remains
rejected: it implements transform gizmos, not picking or silhouette selection,
and would add matrix ownership before a gizmo is requested. A future selection
visual must use the selected mesh's per-pixel object-ID mask at final output
resolution; it must not reintroduce projected bounds or PBR-material mutation.

## Tool Discovery

No custom development tooling was written for validation. The gate reuses:

- The existing benchmark CSV recorder for observability.
- A PowerShell strict script in the established `scripts\Test-*.ps1` shape.
- Vulkan Validation through stdout/stderr scanning.
- FrameGraph validation counters already present in the CSV.

RenderDoc/Nsight were not required: this slice changes CPU-side scene data
ownership and material creation, not GPU resource contents, ordering, barriers,
or descriptor layouts. Escalate to RenderDoc only if a future slice changes
those.

## Scope Of The First Slice

Implemented:

- Empty scene lane (`SE_SCENE_BUILDER=1` or `SE_BENCHMARK_SCENE=builder`) with
  only an authored key light.
- Create Cube / Plane / Sphere / Cone from already registered mesh ids.
- Select, delete, rename.
- Click scene geometry to select it and delete the selected builder object with
  Delete when ImGui is not editing text. Selection is intentionally non-visual.
- Save builder-owned primitive objects, editable PBR state, and the active
  camera pose/FOV to the local, ignored `.selfengine/scene_builder/scene.json`
  document. The next editor startup automatically restores it when present.
- Numeric position / rotation / scale editing.
- Base color, metallic, roughness, emissive, alpha mode + cutoff, double-sided,
  cast shadow.
- One unique runtime `VulkanMaterial` per builder object.
- Passive Debug observability plus 26 benchmark CSV fields.
- `scripts\Test-SceneBuilderHealth.ps1` strict data gate.

Deliberately excluded:

- Undo/redo.
- Prefabs.
- Arbitrary mesh import.
- Viewport gizmo interaction.
- Any editor-specific render path.

## Generic Contract Notes

- The builder owns only the objects it created. It never branches on scene name,
  object ordinal, or known transform/material values.
- All mutations go through `Renderable3D`, `Transform3D`, and
  `MaterialProperties`. The normal `RenderQueue`, material descriptor, and
  shading paths consume them unchanged.
- `Renderable3D::RenderIdentity()` is the tool's object handle. Vector index is
  never treated as identity, because create/destroy reorders the scene view.
- Metallic and roughness are `MaterialProperties::cameraControls[0]` and `[1]`.
  `cameraControls[2]` is the texture-versus-scalar blend and stays at `0` so the
  authored scalars remain authoritative for factor-only editor materials.
- Blend/Transparent objects are reported, not silently enabled. The renderer
  keeps that path off until its temporal/velocity/reactive-mask contract exists.
- The Scene Builder viewport resolves its own clicks through exact primitive
  surface tests, then applies selection through stable builder-owned identity.
  Other scenes retain `Scene3D::SelectAlongRay`; Delete cannot remove imported
  or future scene-owned objects. Selection is intentionally non-visual until a
  true geometry-ID silhouette path is implemented; it does not alter scene PBR
  data.
- Debug builds expose primitive ray-pick and hit counts in the Scene Builder
  diagnostics. Release builds do not update those counters.
- The builder reserves left mouse for click selection by disabling only the
  camera's orbit-drag input. The existing right-mouse free-look path remains
  available.
- The scene document is JSON with an explicit format name and version. Version
  2 owns primitive type, name, transform, PBR state, backface-culling state,
  shadow participation, and the camera pose/FOV; render identity, mesh id,
  material id, and render class are runtime-derived and never serialized.
  Version 1 object-only documents remain loadable and leave the startup camera
  at its default pose.

## Validation

`scripts\Test-SceneBuilderHealth.ps1 -SkipBuild -Strict` passed `16 / 0` on
2026-07-26 across the only two relevant editor lanes:

- `builder-self-test`: create four primitives, mutate transform + PBR, rename,
  run the precise-picking regressions, and delete the selected object. Main
  draws `3`, shadow draws `2`, FrameGraph issues `0`.
- `builder-empty`: empty scene stays empty. Main/shadow draws `0/0`.

The script intentionally does not launch Grid or another historical scene. The
empty editor lane is the control for editor-owned runtime behavior.

The persistence contract was separately verified in an isolated temporary
working directory: a saved two-object Cube/Sphere document auto-loaded at
startup with `objects=2`, `created=2`, `cube/sphere=1/1`, `main draws=2`, and
clean process exit. The normal health script disables saved-document autoload
so a user's local scene cannot affect deterministic editor checks.

Debug and Release both build. The self test and the diagnostics header are
Debug-only; Release performs no readback, scanning, or selection-visual work
for the builder.

## Runtime Monitor Decision

The Scene Builder runtime monitor uses the already adopted `nlohmann/json`
dependency. No third-party monitoring library is added: the required state is
owned by `Scene3D`, `SceneBuilder`, `Camera3D`, `VulkanRenderResources2D`, and
the renderer's public `RendererStats`, so an external tool could not expose it
without a larger adapter than the monitor itself.

The monitor writes `.selfengine/scene_builder/runtime_monitor.json` after each
completed Scene Builder render frame. It is a compact, atomically replaced
runtime snapshot containing live entities, environment, camera, materials,
renderer feature groups, and the concrete Frame Graph. It is deliberately
separate from `.selfengine/scene_builder/scene.json`: the latter remains the
user-authored, persistent scene document and is never overwritten by runtime
telemetry.

## Default City And Asset Reuse

Date: 2026-07-28

The default Scene Builder document now bootstraps a deterministic city once:
24 buildings (six instances of each of the four building assets), 10 cars, and
one enlarged ground object. The document stores `startupLayout` so subsequent
user edits and saves remain authoritative; startup does not repopulate or
rearrange an already bootstrapped document.

No new asset framework is adopted. The existing dependencies already cover the
expensive work:

| Candidate | Source / license | Maintenance check | Decision |
| --- | --- | --- | --- |
| Assimp | https://github.com/assimp/assimp, BSD 3-Clause | Active, upstream pushed 2026-07-15 | Keep for GLB parsing; replacing it would not improve same-asset instancing. |
| meshoptimizer | https://github.com/zeux/meshoptimizer, MIT | Active, upstream pushed 2026-07-26 | Keep for generated LOD chains and the existing derived-data cache. |
| New scene/asset framework | - | - | Reject. It would duplicate the renderer's resource registry and ownership model. |

`RuntimeModelLoader` remains the asset-template owner. The first canonical-path
load parses the GLB, builds or reads LOD data, uploads meshes and textures, and
registers mesh/material ids. Later instances create only ordinary
`Renderable3D` objects that reference those same ids and retain independent
transforms. Startup scene creation defers material descriptor rebuilding until
the bulk load is complete, reducing it from one rebuild per entity to one
rebuild for the whole startup document.

## City Runtime Asset Bake

Date: 2026-07-28

The dense default city exposed two costs that runtime LOD selection cannot
remove: directional shadows and Hybrid Ray Query deliberately consume LOD0,
and Assimp must parse every unique source GLB before the first frame. The six
source assets total 483.1 MB and approximately 8.95 million source triangles;
the 35-object city produced 50.67 million shadow triangles.

The Scene Builder therefore adopts the already pinned meshoptimizer 1.2
`gltfpack` CLI as an offline bake step. Each city model is simplified to 10%
of source triangles with the default 1% error limit and exported with
`-noq -kn -km -ke`, preserving ordinary glTF buffers, named nodes, named
materials, extras, and the existing PNG/JPEG texture formats. No meshopt,
KTX2, or WebP runtime extension is introduced. Original source files remain
unchanged; runtime assets live under `assets/models/scene_builder`.

Candidate decision:

| Candidate | License / fit | Decision |
| --- | --- | --- |
| meshoptimizer gltfpack | MIT, existing C++ dependency, native Windows CLI, ordinary GLB output | Adopt. Removes geometry before Assimp, GPU upload, shadows, and Ray Query. |
| glTF-Transform | MIT, active TypeScript/Node CLI | Reject for this slice. It would add a second toolchain for transformations already supplied by gltfpack. |
| Draco | Apache-2.0, active C++ codec | Reject. It requires a new compressed-geometry runtime decoder and does not directly reduce texture decode cost. |

Legacy scene documents referencing the source GLBs remain loadable. On the
next default-city migration they are recreated from the baked assets and saved
with the new logical paths. The six baked assets retain about 0.895 million
triangles total, reducing the full-city LOD0 shadow/Ray Query geometry by
approximately 90%; embedded textures remain the next startup-size target.
