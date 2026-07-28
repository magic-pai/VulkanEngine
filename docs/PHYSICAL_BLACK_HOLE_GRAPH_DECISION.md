# Physical Black Hole Graph Decision

Status: authoring contract implemented; runtime executor not implemented

## Scope

The standalone Material Editor can author a constrained physical black-hole
graph. This graph is not a PBR surface material and does not reuse the legacy
black-hole shader. It describes the inputs required by a future dedicated
Kerr geodesic and relativistic radiative-transfer pass.

The first document contract is `SelfEngineBlackHoleGraph` version 1. Its five
required stages are:

1. `KerrSpacetime`
2. `NovikovThorneDisk`
3. `SceneEnvironment`
4. `KerrRadiativeTransfer`
5. `BlackHoleOutput`

All five nodes are required exactly once. Links are strongly typed and the
complete physical topology is mandatory before the document can be saved.

## Primary References

Upstream status was checked through the projects' GitHub repositories on
2026-07-28.

| Reference | License and current fit | Work it can remove | Decision |
|---|---|---|---|
| Eric Bruneton `black_hole_shader` | BSD-3-Clause; portable GLSL and precomputed LUTs; upstream code last pushed 2020-11-11 | Validated constant-time Schwarzschild beam tracing and spectral black-body lookup | Keep as a non-rotating comparison and LUT reference; it cannot satisfy the Kerr target by itself |
| Blacklight | Unlicense; scientific C++ renderer; upstream last pushed 2024-12-24; no Vulkan real-time backend | Reference geodesic radiative transfer, invariant intensity, emission, absorption, and finite optical depth | Use as an equation and offline image reference, not a runtime dependency |
| RAPTOR | GPL-3.0; CPU/OpenMP scientific renderer; upstream last pushed 2023-08-29 | Independent general-relativistic radiative-transfer validation | Do not link or copy into SelfEngine; use only for independent output comparison because the license and runtime model do not fit |
| DNGR | Published Kerr film-rendering method, without a drop-in SelfEngine runtime | Reference for higher-order images, beam filtering, and cinematic Kerr quality | Treat as the long-term cinematic reference |

No third-party library is integrated in this authoring slice. The candidates
provide algorithms or validation references, but none is a maintained,
permissively licensed Vulkan Kerr executor that can be adopted as-is.

## Physical Contract

- Spacetime is Kerr, parameterized by mass in solar masses and dimensionless
  spin `a*` in `[-0.998, 0.998]`.
- Lengths use gravitational radii `r_g = GM/c^2`.
- Camera rays are traced backward as null geodesics.
- The initial accretion model is an optically thick Novikov-Thorne thin disk.
- The disk inner edge is derived from the Kerr ISCO and cannot be authored as
  an unrelated radius.
- The editable Eddington ratio is limited to `[0.001, 0.3]`, where the thin-disk
  assumption remains the intended approximation.
- Radiance transport uses invariant specific intensity. Output is linear scene
  radiance and must enter the renderer before display mapping.
- The event horizon is a geodesic termination condition with zero outgoing
  radiance, not an Unlit sphere placed over an incorrect background.

## Quality And Cost

The graph exposes bounded accuracy controls rather than an unbounded loop:

| Control | Allowed range | Default |
|---|---:|---:|
| Adaptive integrator steps | 64-4096 | 512 |
| Relative tolerance | `1e-7`-`1e-3` | `1e-5` |
| Maximum image order | 1-4 | 2 |
| Spectral samples | 3-64 | 16 |

The runtime implementation must define GPU budgets only after it exists and
can be measured on the development GPU. It must preserve a stable horizon,
continuous gravitational and Doppler shifts, background lensing, and the
requested image order under camera motion. A quality tier may reduce image
order or spectral samples, but it may not silently replace Kerr transport with
the legacy visual shader.

## Failure Behavior

- Invalid node counts, topology, physical ranges, non-finite values, or unknown
  algorithms are rejected during load and save.
- The JSON declares `KerrGeodesicV1` as a required backend.
- Until that backend exists, the graph is authoring-only.
- A missing or failed backend resolves to `Disabled`; legacy fallback is
  explicitly forbidden.
- The editor displays the pending-runtime state and does not claim that the
  authored graph currently renders.
