# RenderDoc Integration

- Upstream: https://github.com/baldurk/renderdoc
- Source commit: `050034a0faa37d606ce1b8cf677dba4bc36984ea`
- Installed tool version used for integration: `1.44`
- Vendored file: `renderdoc/api/app/renderdoc_app.h`
- SHA-256: `B7005E7DC34C3635046868BBD76D81B9B055AEDE0F56DAA0BD39FEDEE0639FFB`
- License: MIT; the complete license notice is retained in `renderdoc_app.h`.

SelfEngine does not link RenderDoc into Release. A Debug process launched by
`renderdoccmd capture` discovers the already-injected `renderdoc.dll` through
the official in-application API and brackets one requested rendered frame.

The integration does not load RenderDoc after Vulkan initialization because
late loading cannot reliably hook an existing Vulkan instance/device.
