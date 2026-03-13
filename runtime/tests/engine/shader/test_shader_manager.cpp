/// Tests for ShaderManager initialization, default state, and null-safety.
///
/// All tests are CPU-only — no real GPU device or shader compiler is invoked.
/// They verify:
/// - IsInitialized() is false before Init()
/// - Init(nullptr) returns false and does not set initialized state
/// - Shutdown() is safe to call before Init(), after Init(nullptr), and twice
/// - The destructor can be called safely without a prior Init()

#ifdef _WIN32

#include <gtest/gtest.h>

#include "shader_manager.h"
#include "shader_compiler_fxc.h"  // required for unique_ptr destructor of ShaderCompilerFXC

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// ShaderManager::IsInitialized() — default state
// ─────────────────────────────────────────────────────────────────────────────

/// Freshly constructed ShaderManager must not report itself as initialized.
TEST(ShaderManager, Default_IsInitialized_ReturnsFalse) {
    ShaderManager mgr;
    EXPECT_FALSE(mgr.IsInitialized())
        << "ShaderManager must not be initialized before Init() is called";
}

/// Multiple fresh instances are each independently uninitialized.
TEST(ShaderManager, MultipleInstances_EachUninitialized) {
    ShaderManager a;
    ShaderManager b;
    EXPECT_FALSE(a.IsInitialized());
    EXPECT_FALSE(b.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderManager::Init(nullptr) — null device path
// ─────────────────────────────────────────────────────────────────────────────

/// Init(nullptr) must return false — a null device is not a valid argument.
TEST(ShaderManager, Init_NullDevice_ReturnsFalse) {
    ShaderManager mgr;
    bool result = mgr.Init(nullptr);
    EXPECT_FALSE(result)
        << "Init(nullptr) must return false — no valid RHI device provided";
}

/// After Init(nullptr), IsInitialized() must still return false.
TEST(ShaderManager, Init_NullDevice_IsInitialized_StaysFalse) {
    ShaderManager mgr;
    mgr.Init(nullptr);
    EXPECT_FALSE(mgr.IsInitialized())
        << "IsInitialized() must remain false after Init(nullptr)";
}

/// Init(nullptr) must not crash.
TEST(ShaderManager, Init_NullDevice_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({
        ShaderManager mgr;
        mgr.Init(nullptr);
    });
}

/// Calling Init(nullptr) twice must not crash and must leave manager uninitialized.
TEST(ShaderManager, Init_NullDevice_CalledTwice_DoesNotCrash) {
    ShaderManager mgr;
    EXPECT_NO_FATAL_FAILURE({
        mgr.Init(nullptr);
        mgr.Init(nullptr);
    });
    EXPECT_FALSE(mgr.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderManager::Shutdown() — idempotency
// ─────────────────────────────────────────────────────────────────────────────

/// Shutdown() before any Init() must not crash (no-op).
TEST(ShaderManager, Shutdown_BeforeInit_DoesNotCrash) {
    ShaderManager mgr;
    EXPECT_NO_FATAL_FAILURE({
        mgr.Shutdown();
    });
}

/// Shutdown() after Init(nullptr) must not crash.
TEST(ShaderManager, Shutdown_AfterNullInit_DoesNotCrash) {
    ShaderManager mgr;
    mgr.Init(nullptr);
    EXPECT_NO_FATAL_FAILURE({
        mgr.Shutdown();
    });
}

/// Calling Shutdown() twice (before any Init) must not crash.
TEST(ShaderManager, Shutdown_CalledTwice_BeforeInit_DoesNotCrash) {
    ShaderManager mgr;
    EXPECT_NO_FATAL_FAILURE({
        mgr.Shutdown();
        mgr.Shutdown();
    });
}

/// Calling Shutdown() three times must not crash.
TEST(ShaderManager, Shutdown_CalledThreeTimes_DoesNotCrash) {
    ShaderManager mgr;
    EXPECT_NO_FATAL_FAILURE({
        mgr.Shutdown();
        mgr.Shutdown();
        mgr.Shutdown();
    });
}

/// After Shutdown(), IsInitialized() must return false.
TEST(ShaderManager, Shutdown_BeforeInit_IsInitialized_ReturnsFalse) {
    ShaderManager mgr;
    mgr.Shutdown();
    EXPECT_FALSE(mgr.IsInitialized());
}

/// After Init(nullptr) and Shutdown(), IsInitialized() must return false.
TEST(ShaderManager, Shutdown_AfterNullInit_IsInitialized_ReturnsFalse) {
    ShaderManager mgr;
    mgr.Init(nullptr);
    mgr.Shutdown();
    EXPECT_FALSE(mgr.IsInitialized());
}

/// Shutdown() followed by Init(nullptr) again must still return false and not crash.
TEST(ShaderManager, Shutdown_ThenInit_NullDevice_ReturnsFalse) {
    ShaderManager mgr;
    mgr.Shutdown();
    bool result = mgr.Init(nullptr);
    EXPECT_FALSE(result);
    EXPECT_FALSE(mgr.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderManager destructor — safe without prior Init
// ─────────────────────────────────────────────────────────────────────────────

/// Destructor of a never-initialized ShaderManager must not crash.
TEST(ShaderManager, Destructor_WithoutInit_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({
        ShaderManager mgr;
        // mgr goes out of scope here — destructor called
    });
}

/// Destructor after Init(nullptr) must not crash.
TEST(ShaderManager, Destructor_AfterNullInit_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({
        ShaderManager mgr;
        mgr.Init(nullptr);
        // destructor called on scope exit
    });
}

/// Destructor after Shutdown() must not crash (double-free guard).
TEST(ShaderManager, Destructor_AfterShutdown_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({
        ShaderManager mgr;
        mgr.Shutdown();
        // destructor called — Shutdown() must not have left state that triggers a crash
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderManager — heap-allocated instance lifetime
// ─────────────────────────────────────────────────────────────────────────────

/// Heap-allocated ShaderManager that is never initialized must delete safely.
TEST(ShaderManager, HeapAlloc_NeverInit_DeleteSafely) {
    EXPECT_NO_FATAL_FAILURE({
        auto* mgr = new ShaderManager();
        EXPECT_FALSE(mgr->IsInitialized());
        delete mgr;
    });
}

/// Heap-allocated ShaderManager after Init(nullptr) must delete safely.
TEST(ShaderManager, HeapAlloc_NullInit_DeleteSafely) {
    EXPECT_NO_FATAL_FAILURE({
        auto* mgr = new ShaderManager();
        mgr->Init(nullptr);
        EXPECT_FALSE(mgr->IsInitialized());
        delete mgr;
    });
}

/// Heap-allocated ShaderManager after Shutdown() must delete safely.
TEST(ShaderManager, HeapAlloc_AfterShutdown_DeleteSafely) {
    EXPECT_NO_FATAL_FAILURE({
        auto* mgr = new ShaderManager();
        mgr->Shutdown();
        EXPECT_FALSE(mgr->IsInitialized());
        delete mgr;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderCompilerFXC — companion type used by ShaderManager internals
// ─────────────────────────────────────────────────────────────────────────────

/// Default ShaderCompilerFXC must not report itself as initialized.
TEST(ShaderCompilerFXC, Default_IsInitialized_ReturnsFalse) {
    ShaderCompilerFXC fxc;
    EXPECT_FALSE(fxc.IsInitialized())
        << "ShaderCompilerFXC must not be initialized before Init() is called";
}

/// Shutdown() on a default (never-initialized) ShaderCompilerFXC must not crash.
TEST(ShaderCompilerFXC, Shutdown_BeforeInit_DoesNotCrash) {
    ShaderCompilerFXC fxc;
    EXPECT_NO_FATAL_FAILURE({
        fxc.Shutdown();
    });
}

/// Calling Shutdown() twice before Init() must not crash.
TEST(ShaderCompilerFXC, Shutdown_CalledTwice_BeforeInit_DoesNotCrash) {
    ShaderCompilerFXC fxc;
    EXPECT_NO_FATAL_FAILURE({
        fxc.Shutdown();
        fxc.Shutdown();
    });
}

/// After Shutdown() before Init(), IsInitialized() must remain false.
TEST(ShaderCompilerFXC, Shutdown_BeforeInit_IsInitialized_ReturnsFalse) {
    ShaderCompilerFXC fxc;
    fxc.Shutdown();
    EXPECT_FALSE(fxc.IsInitialized());
}

/// Destructor of a never-initialized ShaderCompilerFXC must not crash.
TEST(ShaderCompilerFXC, Destructor_WithoutInit_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({
        ShaderCompilerFXC fxc;
    });
}

/// Init() with a path that does not exist must return false.
TEST(ShaderCompilerFXC, Init_NonExistentDll_ReturnsFalse) {
    ShaderCompilerFXC fxc;
    bool result = fxc.Init("nonexistent_d3dcompiler.dll");
    EXPECT_FALSE(result)
        << "Init() must return false when the DLL cannot be loaded";
    EXPECT_FALSE(fxc.IsInitialized());
}

/// Init() with an empty path must return false and not crash.
TEST(ShaderCompilerFXC, Init_EmptyPath_ReturnsFalse) {
    ShaderCompilerFXC fxc;
    EXPECT_NO_FATAL_FAILURE({
        bool result = fxc.Init("");
        EXPECT_FALSE(result);
    });
}

#endif // _WIN32
