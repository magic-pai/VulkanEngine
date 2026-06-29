#pragma once

#include "core.h"
#include "renderer/vulkan/renderer_stats.h"

#include <filesystem>
#include <fstream>

namespace se {

struct BenchmarkRecorderConfig {
    bool enabled = false;
    u32 warmupFrames = 30;
    u32 captureFrames = 0;
    std::filesystem::path csvPath{ "selfengine_benchmark.csv" };
};

class BenchmarkRecorder {
public:
    static BenchmarkRecorderConfig ConfigFromEnvironment();

    explicit BenchmarkRecorder(BenchmarkRecorderConfig config);
    ~BenchmarkRecorder();

    SE_DISABLE_COPY(BenchmarkRecorder);
    SE_DISABLE_MOVE(BenchmarkRecorder);

    bool Enabled() const;
    bool ShouldStop() const;
    void RecordFrame(u32 renderedFrameIndex, f32 elapsedSeconds, const RendererStats& stats);

private:
    void OpenCsv();
    void WriteHeader();

private:
    BenchmarkRecorderConfig m_Config{};
    std::ofstream m_Csv;
    u32 m_CapturedFrames = 0;
    bool m_StopRequested = false;
};

}
