# ImGuizmo Integration

Source: https://github.com/CedricGuillemet/ImGuizmo

Pinned commit: `dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d`

Upstream version marker: `v1.92.5 WIP`

License: MIT. The unmodified upstream license is retained as `LICENSE`.

SelfEngine compiles only `ImGuizmo.cpp` for the `SelfEngineForward3D` target.
The Scene Builder offers `TRANSLATE` in `WORLD` mode and `ROTATE` / `SCALE` in
`LOCAL` mode through the ImGui overlay. It does not add a Vulkan render pass,
scene renderable, shadow caster, or PBR material. ImGuizmo removes the need to
implement axis hit testing, translation-plane solving, rotation rings, scale
handles, hover treatment, matrix decomposition, and drag math.

`Transform3D` stores XYZ Euler angles in a different convention from
`ImGuizmo::DecomposeMatrixToComponents`. The Scene Builder therefore uses the
vendored GLM `extractEulerAngleXYZ` helper on the normalized result matrix for
the rotation edit, while retaining ImGuizmo's position and scale results. A
Debug-only TRS round-trip contract covers identity, rotated unit-scale, and
rotated non-uniform-scale objects.
