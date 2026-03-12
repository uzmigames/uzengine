/// UzEngine Triangle Demo — Backend-Agnostic Entry Point
///
/// Standalone executable to validate any RHI backend.
/// Opens a window and renders a colored RGB triangle.
/// Supports --rhi flag to select backend (dx12, dx11). Defaults to DX12.

#include "triangle_demo.h"
#include "rhi_backend.h"
#include "log.h"

#include <cstring>
#include <cstdlib>

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::RHIBackend backend = uz::RHIBackend::DX12;
    uint32_t frameCount = 0;

    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--rhi") == 0) {
            const char* val = argv[i + 1];
            if (std::strcmp(val, "dx11") == 0) backend = uz::RHIBackend::DX11;
            else if (std::strcmp(val, "dx12") == 0) backend = uz::RHIBackend::DX12;
            else if (std::strcmp(val, "vulkan") == 0) backend = uz::RHIBackend::Vulkan;
        } else if (std::strcmp(argv[i], "--frames") == 0) {
            frameCount = static_cast<uint32_t>(std::atoi(argv[i + 1]));
        }
    }

    bool success = uz::RunTriangleDemo(backend, frameCount);

    uz::LogShutdown();
    return success ? 0 : 1;
}
