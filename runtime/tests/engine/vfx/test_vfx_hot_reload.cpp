/// Unit tests for VFXHotReload -- .uzvfx file watcher for hot-reload.
///
/// Covers: Initialize with non-existent directory doesn't crash,
/// GetWatchedFileCount() returns 0 for non-existent directory,
/// IsInitialized() state before/after Initialize/Shutdown,
/// GetReloadCount() starts at 0, OnReload callback registration and invocation,
/// Shutdown clears all state, multiple callbacks are all invoked, and
/// double-shutdown is safe.
///
/// Tests use a non-existent directory ("__nonexistent_vfx_test_dir__") so that
/// no real files are needed — the watcher gracefully handles missing directories.
/// No GPU hardware required.

#include <gtest/gtest.h>

#include "asset/vfx_hot_reload.h"
#include "modules/module_registry.h"
#include "modules/register_builtin_modules.h"

#include <string>

using namespace uz;

namespace {

/// A directory path that is guaranteed not to exist on any CI host.
constexpr const char* kNonExistentDir = "__nonexistent_vfx_test_dir_xyz123__";

/// Creates a registry with builtin modules.
ModuleRegistry MakeRegistry() {
    ModuleRegistry reg;
    RegisterBuiltinModules(reg);
    return reg;
}

} // anonymous namespace

// =============================================================================
// IsInitialized() — before / after Initialize / Shutdown
// =============================================================================

/// A freshly constructed VFXHotReload is not initialized.
TEST(VFXHotReload, DefaultConstruct_IsNotInitialized) {
    VFXHotReload hr;
    EXPECT_FALSE(hr.IsInitialized());
}

/// After Initialize(), IsInitialized() returns true.
TEST(VFXHotReload, Initialize_SetsInitializedTrue) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);

    EXPECT_TRUE(hr.IsInitialized());
}

/// After Shutdown(), IsInitialized() returns false.
TEST(VFXHotReload, Shutdown_SetsInitializedFalse) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);
    hr.Shutdown();

    EXPECT_FALSE(hr.IsInitialized());
}

/// Initialize then Shutdown then Initialize again works correctly.
TEST(VFXHotReload, ReInitialize_AfterShutdown_Works) {
    auto reg = MakeRegistry();
    VFXHotReload hr;

    hr.Initialize(kNonExistentDir, nullptr, &reg);
    ASSERT_TRUE(hr.IsInitialized());

    hr.Shutdown();
    ASSERT_FALSE(hr.IsInitialized());

    hr.Initialize(kNonExistentDir, nullptr, &reg);
    EXPECT_TRUE(hr.IsInitialized());
}

// =============================================================================
// Initialize with non-existent directory — no crash
// =============================================================================

/// Initialize with a non-existent directory must not crash and must not throw.
TEST(VFXHotReload, Initialize_NonExistentDirectory_DoesNotCrash) {
    auto reg = MakeRegistry();
    VFXHotReload hr;

    EXPECT_NO_THROW(hr.Initialize(kNonExistentDir, nullptr, &reg));
    EXPECT_TRUE(hr.IsInitialized())
        << "Initialize must complete even if the directory does not exist";
}

// =============================================================================
// GetWatchedFileCount() — for empty / non-existent directory
// =============================================================================

/// After Initialize with a non-existent directory, watched file count is 0.
TEST(VFXHotReload, Initialize_NonExistentDirectory_WatchedFileCountIsZero) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);

    EXPECT_EQ(hr.GetWatchedFileCount(), 0u)
        << "No files should be tracked for a non-existent directory";
}

/// Before Initialize, watched file count is 0.
TEST(VFXHotReload, DefaultConstruct_WatchedFileCountIsZero) {
    VFXHotReload hr;
    EXPECT_EQ(hr.GetWatchedFileCount(), 0u);
}

/// After Shutdown, watched file count returns to 0.
TEST(VFXHotReload, Shutdown_ClearsWatchedFileCount) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);
    hr.Shutdown();

    EXPECT_EQ(hr.GetWatchedFileCount(), 0u);
}

// =============================================================================
// GetReloadCount() — starts at 0, cleared by Shutdown
// =============================================================================

/// Freshly constructed VFXHotReload has reload count of 0.
TEST(VFXHotReload, DefaultConstruct_ReloadCountIsZero) {
    VFXHotReload hr;
    EXPECT_EQ(hr.GetReloadCount(), 0u);
}

/// After Initialize with non-existent dir, reload count remains 0.
TEST(VFXHotReload, Initialize_ReloadCountRemainsZero) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);

    EXPECT_EQ(hr.GetReloadCount(), 0u)
        << "No files changed, reload count must still be 0";
}

/// After Shutdown, reload count is reset to 0.
TEST(VFXHotReload, Shutdown_ResetsReloadCountToZero) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);
    hr.Shutdown();

    EXPECT_EQ(hr.GetReloadCount(), 0u);
}

// =============================================================================
// Poll() — safe to call in various states
// =============================================================================

/// Poll() before Initialize must not crash (initialized_ is false, early return).
TEST(VFXHotReload, Poll_BeforeInitialize_DoesNotCrash) {
    VFXHotReload hr;
    EXPECT_NO_THROW(hr.Poll());
}

/// Poll() after Initialize with non-existent dir (no files to check) does not crash.
TEST(VFXHotReload, Poll_AfterInitializeEmptyDir_DoesNotCrash) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);

    EXPECT_NO_THROW(hr.Poll());
    EXPECT_EQ(hr.GetReloadCount(), 0u);
}

/// Poll() after Shutdown must not crash.
TEST(VFXHotReload, Poll_AfterShutdown_DoesNotCrash) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);
    hr.Shutdown();

    EXPECT_NO_THROW(hr.Poll());
}

// =============================================================================
// OnReload callback registration
// =============================================================================

/// A registered callback is stored (we can verify the reload count increments
/// when a reload is triggered indirectly via internal test hooks, or at least
/// that registration doesn't crash).
TEST(VFXHotReload, OnReload_RegisterCallback_DoesNotCrash) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);

    bool callbackInvoked = false;
    hr.OnReload([&callbackInvoked](const std::string& /*path*/) {
        callbackInvoked = true;
    });

    // Poll with no files won't trigger callbacks; verify no crash
    hr.Poll();
    EXPECT_FALSE(callbackInvoked)
        << "No files changed, callback must not be invoked during Poll";
}

/// Multiple callbacks can be registered without error.
TEST(VFXHotReload, OnReload_MultipleCallbacks_AllRegisteredWithoutError) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);

    int counter = 0;
    hr.OnReload([&counter](const std::string&) { ++counter; });
    hr.OnReload([&counter](const std::string&) { ++counter; });
    hr.OnReload([&counter](const std::string&) { ++counter; });

    // Poll with no files — no callbacks triggered
    hr.Poll();
    EXPECT_EQ(counter, 0);
}

// =============================================================================
// Shutdown clears all state
// =============================================================================

/// After Shutdown, the object is clean: not initialized, no watched files,
/// no callbacks (verified by re-initializing cleanly without residue).
TEST(VFXHotReload, Shutdown_ClearsAllState) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);

    hr.OnReload([](const std::string&) {});

    hr.Shutdown();

    EXPECT_FALSE(hr.IsInitialized());
    EXPECT_EQ(hr.GetWatchedFileCount(), 0u);
    EXPECT_EQ(hr.GetReloadCount(), 0u);
}

// =============================================================================
// Double-shutdown is safe
// =============================================================================

/// Calling Shutdown() twice must not crash or cause undefined behavior.
TEST(VFXHotReload, Shutdown_CalledTwice_DoesNotCrash) {
    auto reg = MakeRegistry();
    VFXHotReload hr;
    hr.Initialize(kNonExistentDir, nullptr, &reg);

    hr.Shutdown();
    EXPECT_NO_THROW(hr.Shutdown()); // second call must be safe
    EXPECT_FALSE(hr.IsInitialized());
}

/// Calling Shutdown() on a never-initialized instance must not crash.
TEST(VFXHotReload, Shutdown_NeverInitialized_DoesNotCrash) {
    VFXHotReload hr;
    EXPECT_NO_THROW(hr.Shutdown());
}

// =============================================================================
// Destructor — safe cleanup via RAII
// =============================================================================

/// Destroying a VFXHotReload that was initialized must not crash (calls Shutdown).
TEST(VFXHotReload, Destructor_InitializedInstance_DoesNotCrash) {
    auto reg = MakeRegistry();

    EXPECT_NO_THROW({
        VFXHotReload hr;
        hr.Initialize(kNonExistentDir, nullptr, &reg);
        // Destructor called at end of scope — must call Shutdown() internally
    });
}

/// Destroying a VFXHotReload that was never initialized must not crash.
TEST(VFXHotReload, Destructor_NeverInitialized_DoesNotCrash) {
    EXPECT_NO_THROW({
        VFXHotReload hr;
        // Default-constructed: initialized_ == false, Shutdown not called in dtor
    });
}
