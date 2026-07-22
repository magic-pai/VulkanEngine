#include "app/renderdoc_capture.h"

#include <memory>

#if defined(_WIN32) && defined(_DEBUG)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "renderdoc_app.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#endif

namespace se {

struct RenderDocCaptureController::Impl {
#if defined(_WIN32) && defined(_DEBUG)
    RENDERDOC_API_1_6_0* api = nullptr;
    std::filesystem::path statusPath;
    std::string capturePathTemplate;
    std::string captureTitle;
    std::string captureComments;
    std::string capturePath;
    std::string error;
    std::uint32_t targetFrame = 0;
    std::uint32_t startedFrame = 0;
    std::uint32_t endedFrame = 0;
    std::uint32_t capturesBefore = 0;
    std::uint32_t capturesAfter = 0;
    std::uint64_t captureTimestamp = 0;
    int apiMajor = 0;
    int apiMinor = 0;
    int apiPatch = 0;
    bool requested = false;
    bool required = false;
    bool available = false;
    bool started = false;
    bool ended = false;
    bool succeeded = false;

    static std::string EnvironmentString(const char* name) {
        char* value = nullptr;
        std::size_t valueSize = 0;
        if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
            return {};
        }
        std::string result(value);
        std::free(value);
        return result;
    }

    static bool EnvironmentFlag(const char* name) {
        std::string value = EnvironmentString(name);
        std::transform(value.begin(), value.end(), value.begin(), [](char character) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        });
        return value == "1" || value == "true" || value == "on" ||
            value == "yes";
    }

    static std::uint32_t EnvironmentUnsigned(const char* name) {
        const std::string value = EnvironmentString(name);
        std::uint32_t result = 0;
        const auto conversion = std::from_chars(
            value.data(),
            value.data() + value.size(),
            result
        );
        return conversion.ec == std::errc{} &&
            conversion.ptr == value.data() + value.size()
            ? result
            : 0;
    }

    void WriteStatus() const noexcept {
        if (statusPath.empty()) {
            return;
        }
        try {
            if (!statusPath.parent_path().empty()) {
                std::filesystem::create_directories(statusPath.parent_path());
            }
            const nlohmann::json status = {
                {"contract_version", 1},
                {"requested", requested},
                {"required", required},
                {"available", available},
                {"api_version", {
                    {"major", apiMajor},
                    {"minor", apiMinor},
                    {"patch", apiPatch}
                }},
                {"target_frame", targetFrame},
                {"started", started},
                {"started_frame", startedFrame},
                {"ended", ended},
                {"ended_frame", endedFrame},
                {"succeeded", succeeded},
                {"captures_before", capturesBefore},
                {"captures_after", capturesAfter},
                {"capture_path_template", capturePathTemplate},
                {"capture_path", capturePath},
                {"capture_timestamp", captureTimestamp},
                {"error", error}
            };
            std::ofstream output(statusPath, std::ios::out | std::ios::trunc);
            output << status.dump(2) << '\n';
        } catch (...) {
        }
    }

    [[noreturn]] void ThrowRequiredFailure(const std::string& message) {
        error = message;
        WriteStatus();
        throw std::runtime_error(message);
    }

    void Initialize() {
        targetFrame = EnvironmentUnsigned("SE_RENDERDOC_CAPTURE_FRAME");
        requested = targetFrame > 0;
        if (!requested) {
            return;
        }

        required = EnvironmentFlag("SE_RENDERDOC_REQUIRE_API");
        statusPath = EnvironmentString("SE_RENDERDOC_STATUS_JSON");
        capturePathTemplate = EnvironmentString("SE_RENDERDOC_CAPTURE_PATH");
        captureTitle = EnvironmentString("SE_RENDERDOC_CAPTURE_TITLE");
        captureComments = EnvironmentString("SE_RENDERDOC_CAPTURE_COMMENTS");

        const HMODULE module = GetModuleHandleW(L"renderdoc.dll");
        if (module == nullptr) {
            const std::string message =
                "RenderDoc capture requested, but renderdoc.dll was not injected before Vulkan startup";
            if (required) {
                ThrowRequiredFailure(message);
            }
            error = message;
            WriteStatus();
            return;
        }

        const auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(
            GetProcAddress(module, "RENDERDOC_GetAPI")
        );
        if (getApi == nullptr ||
            getApi(eRENDERDOC_API_Version_1_6_0,
                reinterpret_cast<void**>(&api)) != 1 ||
            api == nullptr) {
            const std::string message = "RenderDoc API 1.6.0 is unavailable";
            if (required) {
                ThrowRequiredFailure(message);
            }
            error = message;
            WriteStatus();
            return;
        }

        available = true;
        api->GetAPIVersion(&apiMajor, &apiMinor, &apiPatch);
        capturesBefore = api->GetNumCaptures();
        capturesAfter = capturesBefore;
        if (!capturePathTemplate.empty()) {
            api->SetCaptureFilePathTemplate(capturePathTemplate.c_str());
        }
        WriteStatus();
    }

    void BeginFrame(std::uint32_t renderedFrameIndex) {
        if (!requested || !available || started || renderedFrameIndex != targetFrame) {
            return;
        }

        api->StartFrameCapture(nullptr, nullptr);
        started = api->IsFrameCapturing() != 0;
        startedFrame = renderedFrameIndex;
        if (started && !captureTitle.empty()) {
            api->SetCaptureTitle(captureTitle.c_str());
        }
        if (!started) {
            const std::string message = "RenderDoc failed to start the requested frame capture";
            if (required) {
                ThrowRequiredFailure(message);
            }
            error = message;
        } else {
            std::cout << "[renderdoc] capture_started frame=" << renderedFrameIndex
                << std::endl;
        }
        WriteStatus();
    }

    void EndFrame(std::uint32_t renderedFrameIndex) {
        if (!started || ended || renderedFrameIndex != startedFrame) {
            return;
        }

        succeeded = api->EndFrameCapture(nullptr, nullptr) != 0;
        ended = true;
        endedFrame = renderedFrameIndex;
        capturesAfter = api->GetNumCaptures();
        if (succeeded && capturesAfter > capturesBefore) {
            const std::uint32_t captureIndex = capturesAfter - 1u;
            std::uint32_t pathLength = 0;
            api->GetCapture(captureIndex, nullptr, &pathLength, &captureTimestamp);
            if (pathLength > 0) {
                std::vector<char> path(pathLength, '\0');
                if (api->GetCapture(
                        captureIndex,
                        path.data(),
                        &pathLength,
                        &captureTimestamp
                    ) != 0) {
                    capturePath = path.data();
                }
            }
            if (!captureComments.empty()) {
                api->SetCaptureFileComments(
                    capturePath.empty() ? nullptr : capturePath.c_str(),
                    captureComments.c_str()
                );
            }
        }

        if (!succeeded) {
            error = "RenderDoc failed to save the requested frame capture";
        }
        WriteStatus();
        std::cout << "[renderdoc] capture_finished frame=" << renderedFrameIndex
            << " success=" << (succeeded ? 1 : 0)
            << " path=" << capturePath << std::endl;
        if (required && !succeeded) {
            throw std::runtime_error(error);
        }
    }

    ~Impl() {
        if (api != nullptr && started && !ended && api->IsFrameCapturing() != 0) {
            api->DiscardFrameCapture(nullptr, nullptr);
            error = "RenderDoc capture was discarded during stack unwinding";
            WriteStatus();
        }
    }
#endif
};

RenderDocCaptureController::RenderDocCaptureController()
    : m_Impl(std::make_unique<Impl>()) {
#if defined(_WIN32) && defined(_DEBUG)
    m_Impl->Initialize();
#endif
}

RenderDocCaptureController::~RenderDocCaptureController() = default;

void RenderDocCaptureController::BeginFrame(std::uint32_t renderedFrameIndex) {
#if defined(_WIN32) && defined(_DEBUG)
    m_Impl->BeginFrame(renderedFrameIndex);
#else
    (void)renderedFrameIndex;
#endif
}

void RenderDocCaptureController::EndFrame(std::uint32_t renderedFrameIndex) {
#if defined(_WIN32) && defined(_DEBUG)
    m_Impl->EndFrame(renderedFrameIndex);
#else
    (void)renderedFrameIndex;
#endif
}

}
