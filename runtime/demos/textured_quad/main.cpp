/// Entry point for the textured quad demo.

#include "textured_quad_demo.h"
#include "log.h"

int main() {
    uz::LogInit();
    bool ok = uz::RunTexturedQuadDemo(0);
    uz::LogShutdown();
    return ok ? 0 : 1;
}
