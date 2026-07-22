#pragma once

#include <cstdint>
#include <memory>

namespace se {

class RenderDocCaptureController {
public:
    RenderDocCaptureController();
    ~RenderDocCaptureController();

    RenderDocCaptureController(const RenderDocCaptureController&) = delete;
    RenderDocCaptureController& operator=(const RenderDocCaptureController&) = delete;
    RenderDocCaptureController(RenderDocCaptureController&&) = delete;
    RenderDocCaptureController& operator=(RenderDocCaptureController&&) = delete;

    void BeginFrame(std::uint32_t renderedFrameIndex);
    void EndFrame(std::uint32_t renderedFrameIndex);

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}
