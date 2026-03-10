#include "uz_runtime.h"
#include <cassert>
#include <cstring>
#include <cstdio>

static void test_version() {
    const char* ver = uz_runtime_version();
    assert(ver != nullptr);
    assert(std::strcmp(ver, "0.2.0") == 0);
    std::printf("  PASS: uz_runtime_version() == \"%s\"\n", ver);
}

static void test_backend() {
    const char* backend = uz_runtime_backend();
    assert(backend != nullptr);
    assert(std::strcmp(backend, "uzengine") == 0);
    std::printf("  PASS: uz_runtime_backend() == \"%s\"\n", backend);
}

static void test_init_shutdown() {
    int result = uz_runtime_init();
    assert(result == 1);
    std::printf("  PASS: uz_runtime_init() == 1\n");

    uz_runtime_shutdown();
    std::printf("  PASS: uz_runtime_shutdown() OK\n");
}

int main() {
    std::printf("=== Runtime Unit Tests ===\n");
    test_version();
    test_backend();
    test_init_shutdown();
    std::printf("=== All tests passed ===\n");
    return 0;
}
