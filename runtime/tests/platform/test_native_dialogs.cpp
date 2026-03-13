/// Unit tests for UzEngine Native Dialogs and OS Integration.
///
/// Covers:
///   - DialogFilter struct layout and pointer semantics
///   - MessageBoxButtons / MessageBoxIcon / MessageBoxResult enum distinctness
///   - FileDialogResult default construction (ok == false, paths empty)
///   - IsSystemDarkMode — returns without crashing, result is a valid bool
///   - SetTaskbarProgress / ClearTaskbarProgress — null HWND handled gracefully
///   - ShowTrayIcon / HideTrayIcon — null HWND handled gracefully
///   - SetDarkModeCallback / NotifyDarkModeChange — nullptr callback is safe
///   - NotifyDarkModeChange with a live callback delivers the correct signature
///   - SetDarkModeCallback clears a previously-registered callback
///
/// File dialogs (ShowOpenFileDialog, ShowSaveFileDialog) and ShowMessageBox
/// require interactive UI and therefore cannot be tested headlessly. Their
/// data-structure contracts are verified here instead.

#include "native_dialogs.h"
#include "os_integration.h"
#include "log.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — initialises logging once per suite, silences output so
// UZ_LOG calls inside implementations do not produce noise during test runs.
// ─────────────────────────────────────────────────────────────────────────────

class NativeDialogsTest : public ::testing::Test {
protected:
    void SetUp() override {
        namespace fs = std::filesystem;
        fs::path tmpLog = fs::temp_directory_path() / "uzengine_dialogs_test.log";
        uz::LogInit(tmpLog.string().c_str());
        uz::LogSetLevel(uz::LogLevel::Error);
    }

    void TearDown() override {
        // Clear any dark mode callback registered during the test so global
        // state does not leak between test cases.
        uz::SetDarkModeCallback(nullptr, nullptr);
        uz::LogShutdown();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// DialogFilter — struct layout and pointer semantics
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, DialogFilter_MembersArePointerSized) {
    // Verify the struct is exactly two pointer members (description + extensions).
    static_assert(sizeof(uz::DialogFilter) == 2 * sizeof(const char*),
                  "DialogFilter must be exactly two const char* members");
    SUCCEED();
}

TEST_F(NativeDialogsTest, DialogFilter_CanBeConstructedWithLiterals) {
    const uz::DialogFilter filter{ "Image Files", "*.png;*.jpg;*.bmp" };

    EXPECT_STREQ(filter.description, "Image Files");
    EXPECT_STREQ(filter.extensions,  "*.png;*.jpg;*.bmp");
}

TEST_F(NativeDialogsTest, DialogFilter_CanBeConstructedWithNullPointers) {
    // nullptr is a valid state — implementation guards against it.
    const uz::DialogFilter filter{ nullptr, nullptr };

    EXPECT_EQ(filter.description, nullptr);
    EXPECT_EQ(filter.extensions,  nullptr);
}

TEST_F(NativeDialogsTest, DialogFilter_ArrayOf_ThreeFilters_AccessibleByIndex) {
    const uz::DialogFilter filters[3] = {
        { "UzEngine Scene", "*.uzscene"      },
        { "UzEngine Mesh",  "*.uzmesh"       },
        { "All Files",      "*.*"            },
    };

    EXPECT_STREQ(filters[0].description, "UzEngine Scene");
    EXPECT_STREQ(filters[0].extensions,  "*.uzscene");

    EXPECT_STREQ(filters[1].description, "UzEngine Mesh");
    EXPECT_STREQ(filters[1].extensions,  "*.uzmesh");

    EXPECT_STREQ(filters[2].description, "All Files");
    EXPECT_STREQ(filters[2].extensions,  "*.*");
}

// ─────────────────────────────────────────────────────────────────────────────
// MessageBoxButtons — all four values are distinct uint8_t constants
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, MessageBoxButtons_AllValuesAreDistinct) {
    const auto ok          = static_cast<uint8_t>(uz::MessageBoxButtons::Ok);
    const auto okCancel    = static_cast<uint8_t>(uz::MessageBoxButtons::OkCancel);
    const auto yesNo       = static_cast<uint8_t>(uz::MessageBoxButtons::YesNo);
    const auto yesNoCancel = static_cast<uint8_t>(uz::MessageBoxButtons::YesNoCancel);

    EXPECT_NE(ok,          okCancel)    << "Ok and OkCancel must differ";
    EXPECT_NE(ok,          yesNo)       << "Ok and YesNo must differ";
    EXPECT_NE(ok,          yesNoCancel) << "Ok and YesNoCancel must differ";
    EXPECT_NE(okCancel,    yesNo)       << "OkCancel and YesNo must differ";
    EXPECT_NE(okCancel,    yesNoCancel) << "OkCancel and YesNoCancel must differ";
    EXPECT_NE(yesNo,       yesNoCancel) << "YesNo and YesNoCancel must differ";
}

TEST_F(NativeDialogsTest, MessageBoxButtons_UnderlyingType_IsUint8) {
    static_assert(std::is_same_v<std::underlying_type_t<uz::MessageBoxButtons>, uint8_t>,
                  "MessageBoxButtons underlying type must be uint8_t");
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// MessageBoxIcon — all four values are distinct uint8_t constants
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, MessageBoxIcon_AllValuesAreDistinct) {
    const auto info     = static_cast<uint8_t>(uz::MessageBoxIcon::Info);
    const auto warning  = static_cast<uint8_t>(uz::MessageBoxIcon::Warning);
    const auto error    = static_cast<uint8_t>(uz::MessageBoxIcon::Error);
    const auto question = static_cast<uint8_t>(uz::MessageBoxIcon::Question);

    EXPECT_NE(info,    warning)  << "Info and Warning must differ";
    EXPECT_NE(info,    error)    << "Info and Error must differ";
    EXPECT_NE(info,    question) << "Info and Question must differ";
    EXPECT_NE(warning, error)    << "Warning and Error must differ";
    EXPECT_NE(warning, question) << "Warning and Question must differ";
    EXPECT_NE(error,   question) << "Error and Question must differ";
}

TEST_F(NativeDialogsTest, MessageBoxIcon_UnderlyingType_IsUint8) {
    static_assert(std::is_same_v<std::underlying_type_t<uz::MessageBoxIcon>, uint8_t>,
                  "MessageBoxIcon underlying type must be uint8_t");
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// MessageBoxResult — all four values are distinct uint8_t constants
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, MessageBoxResult_AllValuesAreDistinct) {
    const auto ok     = static_cast<uint8_t>(uz::MessageBoxResult::Ok);
    const auto cancel = static_cast<uint8_t>(uz::MessageBoxResult::Cancel);
    const auto yes    = static_cast<uint8_t>(uz::MessageBoxResult::Yes);
    const auto no     = static_cast<uint8_t>(uz::MessageBoxResult::No);

    EXPECT_NE(ok,     cancel) << "Ok and Cancel must differ";
    EXPECT_NE(ok,     yes)    << "Ok and Yes must differ";
    EXPECT_NE(ok,     no)     << "Ok and No must differ";
    EXPECT_NE(cancel, yes)    << "Cancel and Yes must differ";
    EXPECT_NE(cancel, no)     << "Cancel and No must differ";
    EXPECT_NE(yes,    no)     << "Yes and No must differ";
}

TEST_F(NativeDialogsTest, MessageBoxResult_UnderlyingType_IsUint8) {
    static_assert(std::is_same_v<std::underlying_type_t<uz::MessageBoxResult>, uint8_t>,
                  "MessageBoxResult underlying type must be uint8_t");
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// FileDialogResult — default construction contract
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, FileDialogResult_DefaultConstruct_OkIsFalse) {
    const uz::FileDialogResult result{};
    EXPECT_FALSE(result.ok) << "Default-constructed FileDialogResult must have ok == false";
}

TEST_F(NativeDialogsTest, FileDialogResult_DefaultConstruct_PathsIsEmpty) {
    const uz::FileDialogResult result{};
    EXPECT_TRUE(result.paths.empty())
        << "Default-constructed FileDialogResult must have an empty paths vector";
}

TEST_F(NativeDialogsTest, FileDialogResult_DefaultConstruct_PathsSizeIsZero) {
    const uz::FileDialogResult result{};
    EXPECT_EQ(result.paths.size(), 0u);
}

TEST_F(NativeDialogsTest, FileDialogResult_ManuallyConstructedOk_HoldsExpectedPath) {
    uz::FileDialogResult result{};
    result.ok = true;
    result.paths.push_back("C:/assets/mesh.uzmesh");

    EXPECT_TRUE(result.ok);
    ASSERT_EQ(result.paths.size(), 1u);
    EXPECT_EQ(result.paths[0], "C:/assets/mesh.uzmesh");
}

TEST_F(NativeDialogsTest, FileDialogResult_ManuallyConstructedMultiSelect_HoldsAllPaths) {
    uz::FileDialogResult result{};
    result.ok = true;
    result.paths = { "C:/a.png", "C:/b.png", "C:/c.png" };

    EXPECT_TRUE(result.ok);
    ASSERT_EQ(result.paths.size(), 3u);
    EXPECT_EQ(result.paths[0], "C:/a.png");
    EXPECT_EQ(result.paths[1], "C:/b.png");
    EXPECT_EQ(result.paths[2], "C:/c.png");
}

// ─────────────────────────────────────────────────────────────────────────────
// IsSystemDarkMode — returns a valid bool without crashing
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, IsSystemDarkMode_Called_ReturnsBoolWithoutCrashing) {
    // We cannot assert a specific value because it depends on the OS theme
    // of the machine running the tests. We only verify:
    //   1. The call completes without crashing.
    //   2. The return type is bool (compile-time).
    //   3. The returned value is a valid bool (0 or 1, not uninitialised garbage).
    static_assert(std::is_same_v<decltype(uz::IsSystemDarkMode()), bool>,
                  "IsSystemDarkMode must return bool");

    const bool isDark = uz::IsSystemDarkMode();
    EXPECT_TRUE(isDark == true || isDark == false)
        << "IsSystemDarkMode returned an indeterminate value";
}

TEST_F(NativeDialogsTest, IsSystemDarkMode_CalledTwiceConsecutively_Deterministic) {
    // Two consecutive reads with no system change in between must agree.
    const bool first  = uz::IsSystemDarkMode();
    const bool second = uz::IsSystemDarkMode();
    EXPECT_EQ(first, second)
        << "IsSystemDarkMode returned inconsistent results on consecutive calls";
}

// ─────────────────────────────────────────────────────────────────────────────
// SetDarkModeCallback / NotifyDarkModeChange
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, SetDarkModeCallback_NullCallback_DoesNotCrash) {
    // Setting a null callback must be safe — it clears the registered callback.
    uz::SetDarkModeCallback(nullptr, nullptr);
    SUCCEED();
}

TEST_F(NativeDialogsTest, NotifyDarkModeChange_NoCallbackRegistered_IsNoOp) {
    // With no callback set, NotifyDarkModeChange must complete without crashing.
    uz::SetDarkModeCallback(nullptr, nullptr);
    uz::NotifyDarkModeChange();
    SUCCEED();
}

TEST_F(NativeDialogsTest, NotifyDarkModeChange_WithCallback_InvokesCallback) {
    bool callbackFired = false;

    uz::SetDarkModeCallback(
        [](bool /*isDark*/, void* userData) {
            *static_cast<bool*>(userData) = true;
        },
        &callbackFired);

    uz::NotifyDarkModeChange();

    EXPECT_TRUE(callbackFired)
        << "NotifyDarkModeChange must invoke the registered callback";
}

TEST_F(NativeDialogsTest, NotifyDarkModeChange_WithCallback_ReceivesCurrentDarkModeState) {
    // The value passed to the callback must match IsSystemDarkMode().
    const bool currentDarkMode = uz::IsSystemDarkMode();
    bool receivedDarkMode = !currentDarkMode; // Start with inverted value.

    uz::SetDarkModeCallback(
        [](bool isDark, void* userData) {
            *static_cast<bool*>(userData) = isDark;
        },
        &receivedDarkMode);

    uz::NotifyDarkModeChange();

    EXPECT_EQ(receivedDarkMode, currentDarkMode)
        << "Callback received isDark=" << receivedDarkMode
        << " but IsSystemDarkMode()=" << currentDarkMode;
}

TEST_F(NativeDialogsTest, NotifyDarkModeChange_WithUserData_ForwardsUserDataPtr) {
    // The callback receives the exact userData pointer passed to SetDarkModeCallback.
    // We write a sentinel through the pointer to confirm it is forwarded correctly.
    int sentinel = 0xBEEF;

    uz::SetDarkModeCallback(
        [](bool /*isDark*/, void* userData) {
            *static_cast<int*>(userData) = 0xCAFE;
        },
        &sentinel);

    uz::NotifyDarkModeChange();

    EXPECT_EQ(sentinel, 0xCAFE)
        << "SetDarkModeCallback must forward userData pointer unchanged to callback";
}

TEST_F(NativeDialogsTest, SetDarkModeCallback_ClearsRegisteredCallback_SubsequentNotifyIsNoOp) {
    int callCount = 0;

    uz::SetDarkModeCallback(
        [](bool /*isDark*/, void* userData) {
            ++(*static_cast<int*>(userData));
        },
        &callCount);

    uz::NotifyDarkModeChange(); // Should fire — count goes to 1.
    ASSERT_EQ(callCount, 1) << "Callback should fire once after first notify";

    uz::SetDarkModeCallback(nullptr, nullptr); // Clear.
    uz::NotifyDarkModeChange();                // Should NOT fire.

    EXPECT_EQ(callCount, 1)
        << "After clearing callback, NotifyDarkModeChange must not fire it again";
}

TEST_F(NativeDialogsTest, SetDarkModeCallback_OverwritesExistingCallback) {
    int firstCount  = 0;
    int secondCount = 0;

    uz::SetDarkModeCallback(
        [](bool /*isDark*/, void* userData) { ++(*static_cast<int*>(userData)); },
        &firstCount);

    // Overwrite with a different callback before firing.
    uz::SetDarkModeCallback(
        [](bool /*isDark*/, void* userData) { ++(*static_cast<int*>(userData)); },
        &secondCount);

    uz::NotifyDarkModeChange();

    EXPECT_EQ(firstCount,  0) << "First callback must not fire after being overwritten";
    EXPECT_EQ(secondCount, 1) << "Second (current) callback must fire";
}

// ─────────────────────────────────────────────────────────────────────────────
// SetTaskbarProgress / ClearTaskbarProgress — null HWND must not crash
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, SetTaskbarProgress_NullHwnd_DoesNotCrash) {
    // The implementation guards `if (!hwnd) return` before any COM call.
    uz::SetTaskbarProgress(nullptr, 50, 100);
    SUCCEED();
}

TEST_F(NativeDialogsTest, SetTaskbarProgress_NullHwnd_ZeroValueZeroMax_DoesNotCrash) {
    uz::SetTaskbarProgress(nullptr, 0, 0);
    SUCCEED();
}

TEST_F(NativeDialogsTest, SetTaskbarProgress_NullHwnd_MaxValue_DoesNotCrash) {
    uz::SetTaskbarProgress(nullptr, 100, 100);
    SUCCEED();
}

TEST_F(NativeDialogsTest, SetTaskbarProgress_NullHwnd_ValueExceedsMax_DoesNotCrash) {
    // value > max is a legal (if odd) call; impl must not divide-by-zero or crash.
    uz::SetTaskbarProgress(nullptr, 200, 100);
    SUCCEED();
}

TEST_F(NativeDialogsTest, SetTaskbarProgress_NullHwnd_NegativeValue_DoesNotCrash) {
    uz::SetTaskbarProgress(nullptr, -1, 100);
    SUCCEED();
}

TEST_F(NativeDialogsTest, ClearTaskbarProgress_NullHwnd_DoesNotCrash) {
    uz::ClearTaskbarProgress(nullptr);
    SUCCEED();
}

TEST_F(NativeDialogsTest, ClearTaskbarProgress_NullHwnd_CalledTwice_DoesNotCrash) {
    uz::ClearTaskbarProgress(nullptr);
    uz::ClearTaskbarProgress(nullptr);
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowTrayIcon / HideTrayIcon — null HWND must not crash
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, ShowTrayIcon_NullHwnd_ReturnsFalse) {
    // The implementation returns false immediately when hwnd is null.
    const bool result = uz::ShowTrayIcon(nullptr, "Test Tooltip", 0);
    EXPECT_FALSE(result)
        << "ShowTrayIcon must return false when hwnd is nullptr";
}

TEST_F(NativeDialogsTest, ShowTrayIcon_NullTooltip_ReturnsFalse) {
    // The implementation guards `if (!hwnd || !tooltip) return false`.
    // We pass a fake non-null HWND value so only the tooltip is null.
    void* fakeHwnd = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    const bool result = uz::ShowTrayIcon(fakeHwnd, nullptr, 0);
    EXPECT_FALSE(result)
        << "ShowTrayIcon must return false when tooltip is nullptr";
}

TEST_F(NativeDialogsTest, ShowTrayIcon_NullHwnd_NullTooltip_ReturnsFalse) {
    const bool result = uz::ShowTrayIcon(nullptr, nullptr, 0);
    EXPECT_FALSE(result)
        << "ShowTrayIcon must return false when both hwnd and tooltip are nullptr";
}

TEST_F(NativeDialogsTest, HideTrayIcon_NullHwnd_DoesNotCrash) {
    // The implementation guards `if (!hwnd) return` before Shell_NotifyIconW.
    uz::HideTrayIcon(nullptr);
    SUCCEED();
}

TEST_F(NativeDialogsTest, HideTrayIcon_NullHwnd_CalledMultipleTimes_DoesNotCrash) {
    uz::HideTrayIcon(nullptr);
    uz::HideTrayIcon(nullptr);
    uz::HideTrayIcon(nullptr);
    SUCCEED();
}

TEST_F(NativeDialogsTest, HideTrayIcon_CalledWithoutPriorShow_IsNoOp) {
    // Calling HideTrayIcon without a prior ShowTrayIcon must be safe.
    // Shell_NotifyIcon(NIM_DELETE, ...) on a non-existent icon is a no-op.
    void* fakeHwnd = reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEAD));
    uz::HideTrayIcon(fakeHwnd);
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowTrayNotification — null-guard paths
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, ShowTrayNotification_NullHwnd_DoesNotCrash) {
    uz::ShowTrayNotification(nullptr, "Title", "Body");
    SUCCEED();
}

TEST_F(NativeDialogsTest, ShowTrayNotification_NullTitle_DoesNotCrash) {
    void* fakeHwnd = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    uz::ShowTrayNotification(fakeHwnd, nullptr, "Body");
    SUCCEED();
}

TEST_F(NativeDialogsTest, ShowTrayNotification_NullBody_DoesNotCrash) {
    void* fakeHwnd = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    uz::ShowTrayNotification(fakeHwnd, "Title", nullptr);
    SUCCEED();
}

TEST_F(NativeDialogsTest, ShowTrayNotification_AllNulls_DoesNotCrash) {
    uz::ShowTrayNotification(nullptr, nullptr, nullptr);
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Enum coverage — all enumerators reachable via switch (compile-time coverage)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(NativeDialogsTest, MessageBoxButtons_SwitchCoversAllEnumerators) {
    // If a new enumerator is added to MessageBoxButtons without updating this
    // switch, -Wswitch-enum will catch it at compile time (when enabled).
    // This test ensures the switch actually compiles and is exhaustive.
    for (const auto btn : {
        uz::MessageBoxButtons::Ok,
        uz::MessageBoxButtons::OkCancel,
        uz::MessageBoxButtons::YesNo,
        uz::MessageBoxButtons::YesNoCancel,
    }) {
        switch (btn) {
            case uz::MessageBoxButtons::Ok:          break;
            case uz::MessageBoxButtons::OkCancel:    break;
            case uz::MessageBoxButtons::YesNo:       break;
            case uz::MessageBoxButtons::YesNoCancel: break;
        }
    }
    SUCCEED();
}

TEST_F(NativeDialogsTest, MessageBoxIcon_SwitchCoversAllEnumerators) {
    for (const auto icon : {
        uz::MessageBoxIcon::Info,
        uz::MessageBoxIcon::Warning,
        uz::MessageBoxIcon::Error,
        uz::MessageBoxIcon::Question,
    }) {
        switch (icon) {
            case uz::MessageBoxIcon::Info:     break;
            case uz::MessageBoxIcon::Warning:  break;
            case uz::MessageBoxIcon::Error:    break;
            case uz::MessageBoxIcon::Question: break;
        }
    }
    SUCCEED();
}

TEST_F(NativeDialogsTest, MessageBoxResult_SwitchCoversAllEnumerators) {
    for (const auto result : {
        uz::MessageBoxResult::Ok,
        uz::MessageBoxResult::Cancel,
        uz::MessageBoxResult::Yes,
        uz::MessageBoxResult::No,
    }) {
        switch (result) {
            case uz::MessageBoxResult::Ok:     break;
            case uz::MessageBoxResult::Cancel: break;
            case uz::MessageBoxResult::Yes:    break;
            case uz::MessageBoxResult::No:     break;
        }
    }
    SUCCEED();
}
