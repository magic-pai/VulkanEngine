# Render Observability Stack

## Selection

| Candidate | Source | License | Vulkan/Windows fit | Decision |
|---|---|---|---|---|
| RenderDoc 1.44 | https://renderdoc.org/ and https://github.com/baldurk/renderdoc | MIT | Cross-vendor frame capture, pipeline/resource/shader inspection, official in-app API | Use as the default frame-truth layer. |
| NVIDIA Nsight Graphics 2026.2 | https://developer.nvidia.com/nsight-graphics | NVIDIA tool license | RTX-specific GPU Trace, DLSS, Ray Query/RT, shader and driver inspection | Keep as the deep NVIDIA/performance layer. |
| Vulkan Validation Layers | https://github.com/KhronosGroup/Vulkan-ValidationLayers | Apache-2.0 | API, synchronization, descriptor, layout, and lifetime validation | Keep as the legality layer. |
| SelfEngine Full Audit | Local Debug-only code | Project code | Knows scene identity, RenderQueue/TLAS ownership, expected hit and fallback policy | Restrict to engine semantics external tools cannot infer. |

## Capture Contract

The Debug-only RenderDoc controller is enabled by
`SE_RENDERDOC_CAPTURE_FRAME=<one-based rendered frame>`. It requires RenderDoc
to have been injected before Vulkan startup. `SE_RENDERDOC_REQUIRE_API=1` turns
missing injection or capture failure into a process failure.

The capture wrapper supplies:

- `SE_RENDERDOC_CAPTURE_PATH`: capture path template.
- `SE_RENDERDOC_CAPTURE_TITLE`: title shown in RenderDoc.
- `SE_RENDERDOC_CAPTURE_COMMENTS`: scene/frame/binary contract metadata.
- `SE_RENDERDOC_STATUS_JSON`: machine-readable API/capture result.

Run a focused capture with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\Capture-RenderDocFrame.ps1 `
  -ExecutablePath build\Debug\SelfEngineLightingShowcase.exe `
  -OutputDirectory tmp\renderdoc_lighting `
  -CaptureFrame 12 `
  -AaMode taa
```

Run the static/runtime contract gate with
`scripts\Test-RenderDocIntegration.ps1 -Strict`. Pass `-CaptureManifest` after
a real capture to verify the `.rdc` artifact as well. The wrapper also invokes
RenderDoc's official `thumb` command and requires a decodable, non-empty image;
this rejects missing or structurally invalid capture files without parsing the
private `.rdc` format.

Release builds do not construct or call the controller. Fast Audit remains the
default semantic gate; Raw Evidence remains opt-in after a focused capture
cannot answer the written question.

For hybrid reflections, add `-CaptureRenderDocOnFailure` to
`Capture-HybridReflectionFullAudit.ps1`. It runs the fast semantic gate first
and captures only after a strict failure, preserving both the failing analysis
and the focused RenderDoc manifest.
