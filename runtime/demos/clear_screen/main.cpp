/// UzEngine DX12 Clear Screen Demo
///
/// Standalone executable to validate the RHI DX12 backend.
/// Opens a window and renders an animated color-cycling clear screen.

#include "dx12_clear_screen.h"
#include "uz_log.h"

int main() {
    uz::LogInit();

    bool success = uz::RunClearScreen(0); // 0 = run until window close

    uz::LogShutdown();
    return success ? 0 : 1;
}
