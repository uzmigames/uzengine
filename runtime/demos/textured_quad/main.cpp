/// UzEngine Textured Quad Demo — Entry Point
///
/// Validates texture upload and sampling across any RHI backend.
/// Opens a window and renders a procedural checkerboard quad.

#include "textured_quad_demo.h"
#include "log.h"

int main() {
    uz::LogInit();
    uz::TexturedQuadDemo demo;
    const int result = demo.Run();
    uz::LogShutdown();
    return result;
}
