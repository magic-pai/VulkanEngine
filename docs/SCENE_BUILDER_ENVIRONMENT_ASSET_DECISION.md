# Scene Builder Environment Asset Decision

## Scope

The first Scene Builder environment-asset slice selects between the renderer
default IBL and the shipped Studio Panorama. It does not accept arbitrary file
paths or add a new image format.

## Candidates

| Source | License | Windows/Vulkan fit | Integration cost | Decision |
| --- | --- | --- | --- | --- |
| Existing `stb_image` 2.26 | Public domain / MIT | Already compiled into the renderer and used by `ibl_generator.cpp` for Radiance HDR and LDR equirectangular inputs | None | Use |
| Khronos KTX-Software | Apache-2.0 | Mature KTX2/Basis container tooling | Adds CMake dependency, transcoding, packaging and cache migration; does not replace equirectangular-to-cubemap filtering | Defer |

## Contract

- Quality: one selected environment source creates two purpose-specific GPU
  resources: an unfiltered equirectangular texture for the visible skybox and
  a diffuse/specular IBL set for lighting and material reflections. The
  skybox blur control samples only the source texture's mip chain; it never
  samples the reflection prefilter map.
- Cost: a source change is an explicit discrete edit. It waits for the device,
  builds one replacement IBL set, then updates dependent descriptors. Steady
  frames allocate no new environment resources. Source and IBL textures are
  persistent, purpose-specific representations of the same selected source.
- Fallback: v1-v6 documents migrate to `RendererDefault`. A failed replacement
  leaves the previously valid IBL set active for both lighting and skybox.
- Future: arbitrary HDR/EXR/KTX2 import is a separate asset-pipeline milestone.
