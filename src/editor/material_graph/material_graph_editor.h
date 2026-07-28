#pragma once

#include "core.h"

#include <filesystem>
#include <memory>

namespace se {

struct MaterialGraphRuntimeStats {
    u32 nodeCount = 0;
    u32 linkCount = 0;
    u32 valid = 0;
    u32 saveCount = 0;
    u32 loadCount = 0;
    u64 revision = 0;
};

// Owns one editable surface material graph. The node UI is an adapter; graph
// validation, material evaluation, and JSON persistence remain local here.
class MaterialGraphEditor {
public:
    MaterialGraphEditor();
    ~MaterialGraphEditor();

    SE_DISABLE_COPY(MaterialGraphEditor);
    SE_DISABLE_MOVE(MaterialGraphEditor);

    void Draw();
    void Shutdown();

    const MaterialGraphRuntimeStats& Stats() const;
    static std::filesystem::path DefaultDocumentPath();
    static std::filesystem::path PhysicalBlackHoleDocumentPath();

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}
