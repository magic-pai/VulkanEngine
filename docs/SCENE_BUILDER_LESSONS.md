# Scene Builder Lessons

Read this file before diagnosing an editor interaction or rendering problem.
Historical renderer/showcase lessons remain in `rendering-lessons.md` and are
out of scope unless the user explicitly reactivates those scenes.

## Precise Picking

Symptom:
- Overlapping primitives selected an object behind the visible surface.

Cause:
- A fixed local unit AABB was used for every primitive. It falsely covered
  empty volume around planes, spheres, and cones.

Fix:
- `SceneBuilder::SelectAlongRay` uses analytic cube, plane, sphere, and capped
  cone intersections, then chooses the closest ray hit. Other scenes keep the
  generic picker.

Regression:
- The Scene Builder self-test verifies a cone-AABB false-positive case and a
  true front-sphere/rear-cube overlap. `Test-SceneBuilderHealth.ps1 -Strict`
  must report a self-test mask of `0`.

## Selection Presentation

Symptom:
- A projected selection box detached from the object while the camera moved.

Rule:
- Keep selection non-visual until a final-resolution geometry-ID mask exists.
  Do not use bounds wireframes, emissive material mutation, or transparent
  scene shells as a substitute.

Interaction contract:
- Left click selects, right mouse controls the camera, and Delete removes only
  the selected builder-owned identity.
