# Scene Builder Reflection Capture Decision

Date: 2026-07-27

## Decision

The first local-reflection tier for Scene Builder is one renderer-captured,
scene-owned cubemap probe. It is a formal scene entity: its name, enabled
state, capture origin, influence radius, box extents, tint, intensity, blend
strength, falloff, source, asset ID, refresh policy, and probe-local capture
exclusion list are editable in the Scene Builder panel and serialised in
`reflectionProbe` in the scene document.

New and migrated Scene Builder documents use `CapturedScene` with the `Static`
refresh policy. The initial capture still populates the cubemap; subsequent
object, camera, or light changes do not implicitly replace it. This keeps the
baseline stable while the reflection producer is validated. `SceneDirty` and
the other supported policies remain explicit authoring choices rather than
hidden renderer defaults.

The current scope remains exactly one local probe, not multi-probe editing.
Older `v1` through `v7` documents contain no probe record and migrate in
memory. Migration derives a covering volume from the builder-owned primitive
bounds and places the capture origin above that volume, so it cannot retain a
legacy origin inside a scene object. Version `v8` documents retain an empty
exclusion list. Pressing Save writes the complete `v9` entity; migration never
rewrites the user's old document by itself.

## Quality, Cost, and Fallback

- Quality: the capture renders six cubemap faces, generates a source mip chain,
  GGX-prefilters the result for rough specular reflection, and derives diffuse
  irradiance. It is an approximation from one probe center, so close-range
  parallax remains a known limit.
- Cost: the existing scheduler captures one face per frame and performs the
  filter work only after all six faces are complete. No capture work is added
  to steady frames. Per-probe exclusions are sorted stable render identities;
  queue filtering uses a bounded binary search and adds no GPU resource or
  readback cost.
- Self reflection: a captured-scene probe may not capture a reflective object
  that samples that same probe. The probe stores an explicit exclusion list,
  analogous to Unreal SceneCapture hidden-actor filtering. It is not inferred
  from object position, material, name, or scene fixture. An exclusion affects
  only that probe; the object can remain visible to every other probe.
- Refresh: the persisted policy controls capture scheduling. `Static` captures
  once and preserves the completed cubemap; `SceneDirty` is available when
  authored changes should request a replacement. The previous completed
  cubemap remains sampled until any replacement is ready.
- Fallback: while capture resources are unavailable or incomplete, normal
  global split-sum IBL remains active. No partial cubemap or black reflection
  is consumed. For a completed GPU capture, opaque-geometry coverage in alpha
  is composed with the global IBL before GGX prefiltering. The filtered local
  probe therefore contains one complete environment instead of switching
  between captured geometry and a separate sky at reflection-sample edges.
  If the global IBL resource is unavailable, the existing alpha fallback is
  retained.

## Third-Party Gate

| Candidate | License | Windows/Vulkan/C++ fit | Decision |
| --- | --- | --- | --- |
| Khronos Vulkan Samples | Apache-2.0 | Good Vulkan render-to-texture reference, but sample code has no SelfEngine scene queue, refresh scheduler, material descriptor, or probe-volume integration | Reference only |
| Google Filament / `cmgen` | Apache-2.0 | Strong IBL prefiltering reference/tooling, but not a runtime Vulkan scene-capture component | Reference only |
| Existing SelfEngine captured-scene probe path | Existing project code | Already owns six-face capture, visibility filtering, refresh scheduling, cubemap mip generation, GGX prefiltering, diffuse irradiance, and deferred/forward/WBOIT descriptors | Reuse |

Adding either external candidate would leave the difficult engine-specific
producer, scheduling, and descriptor work in place while adding a large
dependency boundary. No third-party runtime dependency is adopted for this
fixed Scene Builder primitive scope.

## References

- Khronos Vulkan Samples: https://github.com/KhronosGroup/Vulkan-Samples
- Filament IBL tools: https://google.github.io/filament/Filament.html#lighting/imagebasedlights
- Unreal Engine SceneCaptureComponent hidden/show-only actor lists:
  https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/Components/USceneCaptureComponent
