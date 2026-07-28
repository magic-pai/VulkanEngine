# SelfEngine Material Editor Decision

## Scope

`SelfEngineMaterialEditor` is a standalone Windows program that authors one
fixed surface material graph and persists it as JSON. The supported shading
models are `LitPBR` and `Unlit`. It does not start the Vulkan renderer, load a
scene, mutate Scene Builder materials, or generate arbitrary shader programs.

The current document format is `SelfEngineMaterialGraph` version 2 with a
`Surface` target and an explicit `shadingModel`. Version 1 `PbrSurface`
documents remain readable and are upgraded on their next save. Scene-side
parsing and runtime material application are a later integration stage.

`Unlit` is the authoritative zero-lighting contract. A future scene adapter
must bypass direct lighting, environment lighting, shadow receiving, and
reflection evaluation for that shading model. A black color connected to an
`UnlitOutput` therefore produces zero outgoing display radiance; its visible
silhouette still comes from the assigned geometry.

The editor also supports a separate `SelfEngineBlackHoleGraph` authoring
contract. It is intentionally not a surface shading model. Its constrained
Kerr stages, physical ranges, runtime boundary, and reference decisions are
recorded in `PHYSICAL_BLACK_HOLE_GRAPH_DECISION.md`.

## Dependencies

| Dependency | License | Role | Decision |
| --- | --- | --- | --- |
| `thedmd/imgui-node-editor` | MIT | Node placement, links, selection, pan/zoom, and deletion | Adopted at commit `021aa0ea4da13fed864bafb2a92d4c5205076866` |
| Dear ImGui Win32 + DirectX 11 backends | MIT | Lightweight standalone application host | Reuse the bundled official backends; no SelfEngine renderer dependency |
| `Nelarius/imnodes` | MIT | Alternative node UI | Retained as fallback if node-editor compatibility becomes unmaintainable |
| ASWF MaterialX | Apache-2.0 | Material exchange and shader generation | Deferred until SelfEngine needs a real shader compiler/import adapter |

SelfEngine's ImGui 1.92 already supplies the left-scalar `ImVec2` multiply
operator. The configure step applies an idempotent compatibility guard to the
pinned node-editor source and fails if that upstream source changes.

## Module Seam

`MaterialGraphEditor` owns graph validation, surface evaluation, node-editor
lifetime, and JSON persistence behind `Draw()`, `Shutdown()`, and read-only
runtime statistics. The standalone host owns only the native window, ImGui
frame loop, and DirectX 11 presentation.

The node-editor create transaction must always pair `BeginCreate()` with
`EndCreate()`, even when `BeginCreate()` returns false. Missing that pairing
leaves the dependency transaction active and triggers its next-frame Debug
assertion.
