/// UzEngine DX12 Triangle Demo — entry point.

#include "triangle_demo.h"
#include "uz_log.h"

int main() {
    uz::LogInit();

    bool success = uz::RunTriangleDemo(0);

    uz::LogShutdown();
    return success ? 0 : 1;
}
