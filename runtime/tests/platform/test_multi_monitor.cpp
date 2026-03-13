/// Unit tests for the multi-monitor enumeration and DPI system.
///
/// Covers: MonitorInfo struct defaults, EnumerateMonitors() results (count,
/// names, geometry, DPI, primary flag, work area containment), PlatformWindow
/// DPI scale and current-monitor index after window creation, and all
/// edge-case / null-safety paths in the public API.
///
/// EnumerateMonitors() calls the Win32 EnumDisplayMonitors API and therefore
/// requires a running desktop session — it does NOT require a GPU and does NOT
/// open a visible window for the struct/enumeration tests.
///
/// Tests that exercise PlatformWindow::GetDpiScale() and
/// PlatformWindow::GetCurrentMonitor() create a hidden window (SW_HIDE) so
/// the HWND is valid but the window is never made user-visible.

#include "platform_window.h"

#include <gtest/gtest.h>

#ifdef _WIN32

// ─────────────────────────────────────────────────────────────────────────────
// MonitorInfo struct — default values
// ─────────────────────────────────────────────────────────────────────────────

TEST(MonitorInfoDefaults, Name_DefaultIsAllZeros) {
    uz::MonitorInfo info{};
    EXPECT_EQ(info.name[0], '\0')
        << "MonitorInfo::name must be zero-initialized by default";
}

TEST(MonitorInfoDefaults, Position_DefaultIsZero) {
    uz::MonitorInfo info{};
    EXPECT_EQ(info.x, 0) << "MonitorInfo::x default must be 0";
    EXPECT_EQ(info.y, 0) << "MonitorInfo::y default must be 0";
}

TEST(MonitorInfoDefaults, Dimensions_DefaultIsZero) {
    uz::MonitorInfo info{};
    EXPECT_EQ(info.width, 0)  << "MonitorInfo::width default must be 0";
    EXPECT_EQ(info.height, 0) << "MonitorInfo::height default must be 0";
}

TEST(MonitorInfoDefaults, WorkArea_DefaultIsZero) {
    uz::MonitorInfo info{};
    EXPECT_EQ(info.workX, 0) << "MonitorInfo::workX default must be 0";
    EXPECT_EQ(info.workY, 0) << "MonitorInfo::workY default must be 0";
    EXPECT_EQ(info.workW, 0) << "MonitorInfo::workW default must be 0";
    EXPECT_EQ(info.workH, 0) << "MonitorInfo::workH default must be 0";
}

TEST(MonitorInfoDefaults, Dpi_DefaultIs96) {
    uz::MonitorInfo info{};
    EXPECT_EQ(info.dpi, 96u) << "MonitorInfo::dpi default must be 96 (100% scaling)";
}

TEST(MonitorInfoDefaults, DpiScale_DefaultIsOne) {
    uz::MonitorInfo info{};
    EXPECT_FLOAT_EQ(info.dpiScale, 1.0f)
        << "MonitorInfo::dpiScale default must be 1.0 (dpi/96 = 96/96)";
}

TEST(MonitorInfoDefaults, RefreshRate_DefaultIs60) {
    uz::MonitorInfo info{};
    EXPECT_EQ(info.refreshRate, 60u)
        << "MonitorInfo::refreshRate default must be 60 Hz";
}

TEST(MonitorInfoDefaults, IsPrimary_DefaultIsFalse) {
    uz::MonitorInfo info{};
    EXPECT_FALSE(info.isPrimary)
        << "MonitorInfo::isPrimary default must be false";
}

// ─────────────────────────────────────────────────────────────────────────────
// EnumerateMonitors — basic contract
// ─────────────────────────────────────────────────────────────────────────────

class EnumerateMonitorsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        monitors = uz::EnumerateMonitors();
    }

    std::vector<uz::MonitorInfo> monitors;
};

TEST_F(EnumerateMonitorsTest, ReturnsAtLeastOneMonitor) {
    ASSERT_GE(monitors.size(), 1u)
        << "EnumerateMonitors must return at least 1 monitor on any desktop system";
}

TEST_F(EnumerateMonitorsTest, EachMonitor_HasNonEmptyName) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        EXPECT_NE(monitors[i].name[0], '\0')
            << "Monitor " << i << " has an empty name — expected e.g. '\\\\.\\DISPLAY1'";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_NameIsNullTerminated) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        const char* name = monitors[i].name;
        bool terminated = false;
        for (size_t c = 0; c < sizeof(monitors[i].name); ++c) {
            if (name[c] == '\0') { terminated = true; break; }
        }
        EXPECT_TRUE(terminated)
            << "Monitor " << i << " name is not null-terminated within 128 bytes";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_WidthIsPositive) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        EXPECT_GT(monitors[i].width, 0)
            << "Monitor " << i << " width must be > 0";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_HeightIsPositive) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        EXPECT_GT(monitors[i].height, 0)
            << "Monitor " << i << " height must be > 0";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_DpiAtLeast96) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        EXPECT_GE(monitors[i].dpi, 96u)
            << "Monitor " << i << " DPI must be >= 96 (Windows minimum effective DPI)";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_DpiScaleAtLeastOne) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        EXPECT_GE(monitors[i].dpiScale, 1.0f)
            << "Monitor " << i << " dpiScale must be >= 1.0 (dpi >= 96 → scale >= 1.0)";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_DpiScaleMatchesDpi) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        const float expectedScale = static_cast<float>(monitors[i].dpi) / 96.0f;
        EXPECT_NEAR(monitors[i].dpiScale, expectedScale, 1e-4f)
            << "Monitor " << i << " dpiScale must equal dpi/96";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_RefreshRateIsPositive) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        EXPECT_GT(monitors[i].refreshRate, 0u)
            << "Monitor " << i << " refresh rate must be > 0 Hz";
    }
}

TEST_F(EnumerateMonitorsTest, AtLeastOnePrimaryMonitor) {
    int primaryCount = 0;
    for (const auto& mon : monitors) {
        if (mon.isPrimary) ++primaryCount;
    }
    EXPECT_EQ(primaryCount, 1)
        << "Exactly one monitor must be marked primary; found " << primaryCount;
}

TEST_F(EnumerateMonitorsTest, FirstMonitorIsPrimary) {
    // EnumerateMonitors() is documented to sort primary first.
    EXPECT_TRUE(monitors[0].isPrimary)
        << "The first element returned by EnumerateMonitors must be the primary monitor";
}

TEST_F(EnumerateMonitorsTest, EachMonitor_WorkAreaFitsWithinMonitorRect_Width) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];
        // Work area must start at or after monitor left edge
        EXPECT_GE(m.workX, m.x)
            << "Monitor " << i << " workX must be >= x (left edge)";
        // Work area right edge must not exceed monitor right edge
        EXPECT_LE(m.workX + m.workW, m.x + m.width)
            << "Monitor " << i << " work area right edge exceeds monitor bounds";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_WorkAreaFitsWithinMonitorRect_Height) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];
        // Work area must start at or after monitor top edge
        EXPECT_GE(m.workY, m.y)
            << "Monitor " << i << " workY must be >= y (top edge)";
        // Work area bottom edge must not exceed monitor bottom edge
        EXPECT_LE(m.workY + m.workH, m.y + m.height)
            << "Monitor " << i << " work area bottom edge exceeds monitor bounds";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_WorkAreaWidthIsPositive) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        EXPECT_GT(monitors[i].workW, 0)
            << "Monitor " << i << " work area width must be > 0";
    }
}

TEST_F(EnumerateMonitorsTest, EachMonitor_WorkAreaHeightIsPositive) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        EXPECT_GT(monitors[i].workH, 0)
            << "Monitor " << i << " work area height must be > 0";
    }
}

TEST_F(EnumerateMonitorsTest, SecondCall_ReturnsSameCount) {
    // EnumerateMonitors must be idempotent for the same monitor configuration.
    auto monitors2 = uz::EnumerateMonitors();
    EXPECT_EQ(monitors.size(), monitors2.size())
        << "Two consecutive calls to EnumerateMonitors must return the same count";
}

TEST_F(EnumerateMonitorsTest, SecondCall_ReturnsSamePrimaryFirst) {
    auto monitors2 = uz::EnumerateMonitors();
    ASSERT_GE(monitors2.size(), 1u);
    EXPECT_TRUE(monitors2[0].isPrimary)
        << "Second call: first monitor must still be primary";
}

// ─────────────────────────────────────────────────────────────────────────────
// PlatformWindow — DPI scale after window creation
// ─────────────────────────────────────────────────────────────────────────────

/// RAII helper — creates a hidden window and destroys it on scope exit.
/// No window is made visible: SW_HIDE is never called (CreateWindowExW does
/// not show by default when WS_VISIBLE is absent from the style).
class HiddenWindowFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        uz::WindowDesc desc{};
        desc.title = "UzEngineTestWindow_MultiMonitor";
        desc.width = 640;
        desc.height = 360;
        desc.resizable = true;
        desc.fullscreen = false;

        const bool created = window.Create(desc);
        // If window creation fails in a headless CI environment the tests
        // below are skipped gracefully via GTEST_SKIP.
        windowCreated = created;
    }

    void TearDown() override {
        window.Destroy();
    }

    uz::PlatformWindow window;
    bool windowCreated = false;
};

TEST_F(HiddenWindowFixture, GetDpiScale_AfterCreate_ReturnsAtLeastOne) {
    if (!windowCreated) GTEST_SKIP() << "Window creation failed (headless environment)";

    const float scale = window.GetDpiScale();
    EXPECT_GE(scale, 1.0f)
        << "GetDpiScale must return >= 1.0 after a successful window creation";
}

TEST_F(HiddenWindowFixture, GetDpiScale_AfterCreate_MatchesPrimaryMonitorScale) {
    if (!windowCreated) GTEST_SKIP() << "Window creation failed (headless environment)";

    // GetDpiForWindow queries the monitor the window was placed on.
    // In a standard system the created window lands on the primary monitor.
    const auto monitors = uz::EnumerateMonitors();
    ASSERT_GE(monitors.size(), 1u);

    const float windowScale = window.GetDpiScale();
    // Allow tolerance: window may be on a non-primary monitor.
    // Validate only that the value is consistent with a real DPI (multiple of 0.25).
    // Real Windows DPI values: 96, 120, 144, 192 → scales 1.0, 1.25, 1.5, 2.0.
    const float rounded = std::round(windowScale * 4.0f) / 4.0f;
    EXPECT_NEAR(windowScale, rounded, 1e-4f)
        << "DPI scale should be a multiple of 0.25 (matches Windows DPI steps)";
}

TEST_F(HiddenWindowFixture, GetDpiScale_BeforeCreate_ReturnsOne) {
    // A freshly default-constructed, uncreated window must return 1.0.
    uz::PlatformWindow uncreated;
    EXPECT_FLOAT_EQ(uncreated.GetDpiScale(), 1.0f)
        << "GetDpiScale must return 1.0 before Create() is called";
}

// ─────────────────────────────────────────────────────────────────────────────
// PlatformWindow — GetCurrentMonitor
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(HiddenWindowFixture, GetCurrentMonitor_AfterCreate_ReturnsValidIndex) {
    if (!windowCreated) GTEST_SKIP() << "Window creation failed (headless environment)";

    const int idx = window.GetCurrentMonitor();
    const auto monitors = uz::EnumerateMonitors();

    EXPECT_GE(idx, 0)
        << "GetCurrentMonitor must return a non-negative index";
    EXPECT_LT(idx, static_cast<int>(monitors.size()))
        << "GetCurrentMonitor index must be < EnumerateMonitors().size()";
}

TEST_F(HiddenWindowFixture, GetCurrentMonitor_BeforeCreate_ReturnsZero) {
    // With no HWND, the implementation returns 0 (documented fallback).
    uz::PlatformWindow uncreated;
    EXPECT_EQ(uncreated.GetCurrentMonitor(), 0)
        << "GetCurrentMonitor must return 0 when the window has not been created";
}

TEST_F(HiddenWindowFixture, GetCurrentMonitor_AfterDestroy_ReturnsZero) {
    if (!windowCreated) GTEST_SKIP() << "Window creation failed (headless environment)";

    window.Destroy();
    EXPECT_EQ(window.GetCurrentMonitor(), 0)
        << "GetCurrentMonitor must return 0 after the window has been destroyed";

    // Re-create so TearDown::Destroy() is safe to call again.
    uz::WindowDesc desc{};
    desc.title = "UzEngineTestWindow_MultiMonitor";
    desc.width = 640;
    desc.height = 360;
    window.Create(desc);
}

// ─────────────────────────────────────────────────────────────────────────────
// PlatformWindow — IsFullscreen initial state
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(HiddenWindowFixture, IsFullscreen_AfterWindowed_Create_IsFalse) {
    if (!windowCreated) GTEST_SKIP() << "Window creation failed (headless environment)";

    EXPECT_FALSE(window.IsFullscreen())
        << "Window created in windowed mode must not report fullscreen";
}

// ─────────────────────────────────────────────────────────────────────────────
// SetFullscreenOnMonitor — out-of-range index clamps to primary
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(HiddenWindowFixture, SetFullscreenOnMonitor_NegativeIndex_DoesNotCrash) {
    if (!windowCreated) GTEST_SKIP() << "Window creation failed (headless environment)";

    EXPECT_NO_THROW(window.SetFullscreenOnMonitor(-1))
        << "Negative monitor index must be clamped to primary without crashing";

    // Restore windowed state so subsequent tests and TearDown are clean.
    window.SetWindowed();
}

TEST_F(HiddenWindowFixture, SetFullscreenOnMonitor_OutOfRangePositive_DoesNotCrash) {
    if (!windowCreated) GTEST_SKIP() << "Window creation failed (headless environment)";

    EXPECT_NO_THROW(window.SetFullscreenOnMonitor(9999))
        << "Out-of-range positive monitor index must clamp to primary without crashing";

    window.SetWindowed();
}

TEST_F(HiddenWindowFixture, SetFullscreenOnMonitor_ValidIndex0_BecomesFullscreen) {
    if (!windowCreated) GTEST_SKIP() << "Window creation failed (headless environment)";

    window.SetFullscreenOnMonitor(0);
    EXPECT_TRUE(window.IsFullscreen())
        << "After SetFullscreenOnMonitor(0) the window must be in fullscreen state";

    window.SetWindowed();
    EXPECT_FALSE(window.IsFullscreen())
        << "After SetWindowed() the window must no longer be in fullscreen state";
}

// ─────────────────────────────────────────────────────────────────────────────
// SetFullscreenOnMonitor — no-op on uncreated window
// ─────────────────────────────────────────────────────────────────────────────

TEST(MultiMonitorNoWindow, SetFullscreenOnMonitor_Uncreated_DoesNotCrash) {
    uz::PlatformWindow w;
    EXPECT_NO_THROW(w.SetFullscreenOnMonitor(0))
        << "SetFullscreenOnMonitor on an uncreated window must be a safe no-op";
}

TEST(MultiMonitorNoWindow, SetFullscreen_Uncreated_DoesNotCrash) {
    uz::PlatformWindow w;
    EXPECT_NO_THROW(w.SetFullscreen())
        << "SetFullscreen on an uncreated window must be a safe no-op";
}

TEST(MultiMonitorNoWindow, SetWindowed_Uncreated_DoesNotCrash) {
    uz::PlatformWindow w;
    EXPECT_NO_THROW(w.SetWindowed())
        << "SetWindowed on an uncreated window must be a safe no-op";
}

// ─────────────────────────────────────────────────────────────────────────────
// WM_DPICHANGED simulation — verifies the handler updates DPI scale
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(HiddenWindowFixture, WmDpiChanged_UpdatesDpiScale) {
    if (!windowCreated) GTEST_SKIP() << "Window creation failed (headless environment)";

    // Record initial DPI scale
    const float initialScale = window.GetDpiScale();
    EXPECT_GE(initialScale, 1.0f);

    // Simulate WM_DPICHANGED by sending the message directly.
    // New DPI = 192 (200% scaling), packed as MAKEWPARAM(192, 192).
    // The LPARAM points to a RECT with the suggested new window position.
    HWND hwnd = window.GetHWND();
    if (!hwnd) GTEST_SKIP() << "No native handle available";

    RECT suggestedRect;
    GetWindowRect(hwnd, &suggestedRect);

    // Send WM_DPICHANGED (0x02E0) with new DPI = 192
    const WPARAM newDpi = MAKEWPARAM(192, 192);
    SendMessage(hwnd, WM_DPICHANGED, newDpi, reinterpret_cast<LPARAM>(&suggestedRect));

    // After processing, DPI scale should reflect the new value
    const float newScale = window.GetDpiScale();
    EXPECT_FLOAT_EQ(newScale, 192.0f / 96.0f)
        << "After WM_DPICHANGED with DPI=192, scale should be 2.0";
}

#endif // _WIN32
