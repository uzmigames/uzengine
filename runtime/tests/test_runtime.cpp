#include "uz_runtime.h"
#include "uz_log.h"
#include "uz_assert.h"
#include "uz_profiler.h"
#include "uz_memory.h"
#include "uz_config.h"
#include "uz_input.h"

#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <utility>

// ── Test framework ──────────────────────────────────────────────────────────

static int g_passed = 0;
static int g_total = 0;

#define EXPECT(expr) \
    do { \
        ++g_total; \
        if (!(expr)) { \
            std::fprintf(stderr, "  FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            std::abort(); \
        } \
        ++g_passed; \
    } while (0)

#define EXPECT_EQ(a, b) EXPECT((a) == (b))
#define EXPECT_NE(a, b) EXPECT((a) != (b))
#define EXPECT_TRUE(a) EXPECT(!!(a))
#define EXPECT_FALSE(a) EXPECT(!(a))

// ═══════════════════════════════════════════════════════════════════════════
// Runtime API
// ═══════════════════════════════════════════════════════════════════════════

static void test_runtime_version() {
    std::printf("[test] runtime_version\n");
    EXPECT_NE(uz_runtime_version(), nullptr);
    EXPECT_EQ(std::strcmp(uz_runtime_version(), "0.2.0"), 0);
}

static void test_runtime_backend() {
    std::printf("[test] runtime_backend\n");
    EXPECT_NE(uz_runtime_backend(), nullptr);
    EXPECT_EQ(std::strcmp(uz_runtime_backend(), "uzengine"), 0);
}

static void test_runtime_init_shutdown() {
    std::printf("[test] runtime_init_shutdown\n");
    EXPECT_EQ(uz_runtime_init(), 1);
    uz_runtime_shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
// Logging
// ═══════════════════════════════════════════════════════════════════════════

static void test_log_init_shutdown() {
    std::printf("[test] log_init_shutdown\n");
    uz::LogInit("test_output.log");
    uz::LogInit("test_output.log"); // double init is no-op

    UZ_LOG(Core, Info, "Test log message: {}", 42);
    UZ_LOG(Core, Warning, "Test warning: {}", "caution");
    UZ_LOG(Core, Error, "Test error: code={}", -1);

    uz::LogShutdown();
    uz::LogShutdown(); // double shutdown is safe
    EXPECT_TRUE(true);
}

static void test_log_categories() {
    std::printf("[test] log_categories\n");
    uz::LogInit("test_categories.log");

    EXPECT_NE(uz::LogGetLogger(LogCore), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogRHI), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogScene), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogInput), nullptr);
    EXPECT_NE(uz::LogGetLogger(LogMemory), nullptr);

    UZ_LOG(RHI, Info, "RHI logger works");
    UZ_LOG(Scene, Debug, "Scene logger: {}", 3.14f);

    uz::LogShutdown();
}

static void test_log_level_change() {
    std::printf("[test] log_level_change\n");
    uz::LogInit("test_levels.log");
    uz::LogSetLevel(uz::LogLevel::Warning);
    UZ_LOG(Core, Warning, "This appears at warning level");
    uz::LogSetLevel(uz::LogLevel::Trace);
    uz::LogShutdown();
    EXPECT_TRUE(true);
}

// ═══════════════════════════════════════════════════════════════════════════
// Assertions
// ═══════════════════════════════════════════════════════════════════════════

static void test_ensure_non_fatal() {
    std::printf("[test] ensure_non_fatal\n");
    EXPECT_TRUE(UZ_ENSURE(true));
    EXPECT_TRUE(UZ_ENSURE(1 == 1));
}

static void test_verify_passes() {
    std::printf("[test] verify_passes\n");
    int x = 42;
    UZ_VERIFY(x == 42);
    EXPECT_EQ(x, 42);
}

static void test_check_passes() {
    std::printf("[test] check_passes\n");
    int* ptr = new int(10);
    UZ_CHECK(ptr != nullptr);
    UZ_CHECKF(*ptr == 10, "Expected 10, got %d", *ptr);
    delete ptr;
    EXPECT_TRUE(true);
}

// ═══════════════════════════════════════════════════════════════════════════
// Profiler
// ═══════════════════════════════════════════════════════════════════════════

UZ_DEFINE_STAT(Rendering, TestDrawCalls);

static void test_stat_counters() {
    std::printf("[test] stat_counters\n");
    Stat_Rendering_TestDrawCalls.Reset();
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 0);

    Stat_Rendering_TestDrawCalls.Increment();
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 1);

    Stat_Rendering_TestDrawCalls.Increment(9);
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 10);

    Stat_Rendering_TestDrawCalls.Reset();
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 0);
}

static void test_profiler_macros() {
    std::printf("[test] profiler_macros\n");
    UZ_PROFILE_SCOPE(TestScope);
    UZ_PROFILE_FUNCTION();
    UZ_PROFILE_FRAME_MARK();
    EXPECT_TRUE(true);
}

// ═══════════════════════════════════════════════════════════════════════════
// Memory / Ownership
// ═══════════════════════════════════════════════════════════════════════════

struct TestObj {
    int value;
    static int alive;
    TestObj(int v) : value(v) { ++alive; }
    ~TestObj() { --alive; }
};
int TestObj::alive = 0;

static void test_own_create_destroy() {
    std::printf("[test] own_create_destroy\n");
    TestObj::alive = 0;
    {
        auto obj = uz::MakeOwn<TestObj>(42);
        EXPECT_TRUE(static_cast<bool>(obj));
        EXPECT_EQ(obj->value, 42);
        EXPECT_EQ(TestObj::alive, 1);
    }
    EXPECT_EQ(TestObj::alive, 0);
}

static void test_own_move() {
    std::printf("[test] own_move\n");
    TestObj::alive = 0;
    auto a = uz::MakeOwn<TestObj>(10);
    auto b = std::move(a);
    EXPECT_FALSE(static_cast<bool>(a));
    EXPECT_TRUE(static_cast<bool>(b));
    EXPECT_EQ(b->value, 10);
    EXPECT_EQ(TestObj::alive, 1);
}

static void test_own_release_reset() {
    std::printf("[test] own_release_reset\n");
    TestObj::alive = 0;
    auto obj = uz::MakeOwn<TestObj>(5);
    TestObj* raw = obj.Release();
    EXPECT_FALSE(static_cast<bool>(obj));
    EXPECT_EQ(raw->value, 5);
    EXPECT_EQ(TestObj::alive, 1);
    delete raw;
    EXPECT_EQ(TestObj::alive, 0);
}

static void test_ref_immutable_borrow() {
    std::printf("[test] ref_immutable_borrow\n");
    auto owner = uz::MakeOwn<TestObj>(7);
    {
        uz::Ref<TestObj> r1(owner);
        EXPECT_EQ(r1->value, 7);
        uz::Ref<TestObj> r2(owner);
        EXPECT_EQ(r2->value, 7);
    }
    EXPECT_EQ(owner->value, 7);
}

static void test_refmut_mutable_borrow() {
    std::printf("[test] refmut_mutable_borrow\n");
    auto owner = uz::MakeOwn<TestObj>(3);
    {
        uz::RefMut<TestObj> rm(owner);
        rm->value = 99;
    }
    EXPECT_EQ(owner->value, 99);
}

static void test_refmut_move_only() {
    std::printf("[test] refmut_move_only\n");
    auto owner = uz::MakeOwn<TestObj>(1);
    uz::RefMut<TestObj> rm1(owner);
    uz::RefMut<TestObj> rm2 = std::move(rm1);
    EXPECT_FALSE(static_cast<bool>(rm1));
    EXPECT_TRUE(static_cast<bool>(rm2));
    EXPECT_EQ(rm2->value, 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Config
// ═══════════════════════════════════════════════════════════════════════════

static void test_config_load_string() {
    std::printf("[test] config_load_string\n");
    uz::LogInit();
    uz::Config cfg;
    EXPECT_TRUE(cfg.LoadFromString(R"({
        "name": "TestProject",
        "version": "1.0",
        "render": { "backend": "dx12", "vsync": true }
    })"));
    EXPECT_EQ(cfg.GetString("name"), std::string("TestProject"));
    EXPECT_EQ(cfg.GetString("version"), std::string("1.0"));
    EXPECT_TRUE(cfg.GetBool("render.vsync"));
    EXPECT_EQ(cfg.GetString("render.backend"), std::string("dx12"));
    uz::LogShutdown();
}

static void test_config_dotted_path() {
    std::printf("[test] config_dotted_path\n");
    uz::LogInit();
    uz::Config cfg;
    cfg.LoadFromString(R"({ "a": { "b": { "c": 42 } }, "arr": [10, 20, 30] })");
    EXPECT_EQ(cfg.GetInt("a.b.c"), 42);

    auto* val = cfg.Resolve("arr.1");
    EXPECT_NE(val, nullptr);
    EXPECT_EQ(val->get<int>(), 20);

    EXPECT_FALSE(cfg.Has("nonexistent"));
    EXPECT_EQ(cfg.GetInt("nonexistent", -1), -1);
    EXPECT_EQ(cfg.GetString("nonexistent", "fallback"), std::string("fallback"));
    EXPECT_EQ(cfg.GetFloat("nonexistent", 3.14f), 3.14f);
    uz::LogShutdown();
}

static void test_config_cli_overrides() {
    std::printf("[test] config_cli_overrides\n");
    uz::LogInit();
    uz::Config cfg;
    cfg.LoadFromString(R"({ "name": "Original", "debug": false })");

    const char* argv[] = { "engine", "--name=Overridden", "--debug=true", "--extra", "hello" };
    cfg.ApplyCLIOverrides(5, argv);

    EXPECT_EQ(cfg.GetString("name"), std::string("Overridden"));
    EXPECT_TRUE(cfg.GetBool("debug"));
    EXPECT_EQ(cfg.GetString("extra"), std::string("hello"));
    uz::LogShutdown();
}

static void test_config_defaults() {
    std::printf("[test] config_defaults\n");
    uz::LogInit();
    uz::Config cfg;
    uz::ConfigSetEngineDefaults(cfg);
    EXPECT_EQ(cfg.GetString("render.backend"), std::string("dx12"));
    EXPECT_EQ(cfg.GetInt("window.width"), 1280);
    EXPECT_EQ(cfg.GetInt("window.height"), 720);
    EXPECT_TRUE(cfg.GetBool("render.vsync"));
    uz::LogShutdown();
}

static void test_config_invalid_json() {
    std::printf("[test] config_invalid_json\n");
    uz::LogInit();
    uz::Config cfg;
    EXPECT_FALSE(cfg.LoadFromString("{ broken json }}}"));
    uz::LogShutdown();
}

// ═══════════════════════════════════════════════════════════════════════════
// Input
// ═══════════════════════════════════════════════════════════════════════════

static void test_input_keyboard() {
    std::printf("[test] input_keyboard\n");
    uz::InputSystem input;
    input.Init();

    // Frame 1: press W
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::W));
    EXPECT_TRUE(input.IsKeyDown(uz::Key::W));
    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::W));
    EXPECT_FALSE(input.IsKeyDown(uz::Key::A));

    // Frame 2: W still held
    input.BeginFrame();
    EXPECT_TRUE(input.IsKeyDown(uz::Key::W));
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::W));

    // Frame 3: release W
    input.OnKeyUp(static_cast<uint16_t>(uz::Key::W));
    EXPECT_FALSE(input.IsKeyDown(uz::Key::W));
    EXPECT_TRUE(input.IsKeyJustReleased(uz::Key::W));

    input.Shutdown();
}

static void test_input_mouse() {
    std::printf("[test] input_mouse\n");
    uz::InputSystem input;
    input.Init();

    input.BeginFrame();
    input.OnMouseMove(100.f, 200.f);
    EXPECT_EQ(input.GetMouseX(), 100.f);
    EXPECT_EQ(input.GetMouseY(), 200.f);

    input.OnMouseButton(uz::MouseButton::Left, true);
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Left));
    EXPECT_TRUE(input.IsMouseJustPressed(uz::MouseButton::Left));

    input.BeginFrame();
    EXPECT_FALSE(input.IsMouseJustPressed(uz::MouseButton::Left));
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Left));

    input.OnMouseScroll(3.f);
    EXPECT_EQ(input.GetScrollDelta(), 3.f);

    input.Shutdown();
}

static void test_input_actions() {
    std::printf("[test] input_actions\n");
    uz::InputSystem input;
    input.Init();

    input.RegisterAction("Jump", uz::Key::Space);
    input.RegisterAction("Forward", uz::Key::W);

    input.BeginFrame();
    EXPECT_FALSE(input.IsActionDown("Jump"));

    input.OnKeyDown(static_cast<uint16_t>(uz::Key::Space));
    EXPECT_TRUE(input.IsActionDown("Jump"));
    EXPECT_TRUE(input.IsActionJustPressed("Jump"));
    EXPECT_FALSE(input.IsActionDown("NonExistent"));

    input.Shutdown();
}

// ═══════════════════════════════════════════════════════════════════════════

int main() {
    std::printf("=== UzEngine 1.0 Bootstrap Tests ===\n\n");

    // Runtime API
    test_runtime_version();
    test_runtime_backend();
    test_runtime_init_shutdown();

    // Logging
    test_log_init_shutdown();
    test_log_categories();
    test_log_level_change();

    // Assertions
    test_ensure_non_fatal();
    test_verify_passes();
    test_check_passes();

    // Profiler
    test_stat_counters();
    test_profiler_macros();

    // Memory / Ownership
    test_own_create_destroy();
    test_own_move();
    test_own_release_reset();
    test_ref_immutable_borrow();
    test_refmut_mutable_borrow();
    test_refmut_move_only();

    // Config
    test_config_load_string();
    test_config_dotted_path();
    test_config_cli_overrides();
    test_config_defaults();
    test_config_invalid_json();

    // Input
    test_input_keyboard();
    test_input_mouse();
    test_input_actions();

    std::printf("\n=== %d/%d assertions passed ===\n", g_passed, g_total);
    return (g_passed == g_total) ? 0 : 1;
}
