/// Unit tests for UzEngine System Information (platform/windows/system_info.h).
///
/// Covers: CpuInfo field invariants, MemoryInfo consistency, OsInfo version
/// bounds, GpuInfo presence on a GPU machine, BatteryInfo crash-safety,
/// LocaleInfo non-empty language, and LogSystemInfo smoke test.
///
/// All tests run entirely in user-space (no GPU, no hardware requirement).
/// GPU-dependent assertions are guarded: a machine without a DXGI adapter
/// returns a zero GpuInfo and the test accepts that gracefully.

#include "system_info.h"
#include "log.h"

#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Fixture — init / shutdown logging once per suite so UZ_LOG does not crash
// ─────────────────────────────────────────────────────────────────────────────

class SystemInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
        fs::path logPath = fs::temp_directory_path() / "uzengine_sysinfo_test.log";
        uz::LogInit(logPath.string().c_str());
        uz::LogSetLevel(uz::LogLevel::Error);
    }

    void TearDown() override {
        uz::LogShutdown();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CpuInfo
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SystemInfoTest, GetCpuInfo_BrandString_IsNonEmpty) {
    const uz::CpuInfo cpu = uz::GetCpuInfo();

    EXPECT_GT(std::strlen(cpu.brand), 0u)
        << "CPU brand string must not be empty on any real processor";
}

TEST_F(SystemInfoTest, GetCpuInfo_PhysicalCores_IsAtLeastOne) {
    const uz::CpuInfo cpu = uz::GetCpuInfo();

    EXPECT_GE(cpu.physicalCores, 1u)
        << "Every machine has at least one physical core";
}

TEST_F(SystemInfoTest, GetCpuInfo_LogicalCores_GreaterOrEqualPhysical) {
    const uz::CpuInfo cpu = uz::GetCpuInfo();

    EXPECT_GE(cpu.logicalCores, cpu.physicalCores)
        << "Logical core count must be >= physical (hyper-threading or 1:1)";
}

TEST_F(SystemInfoTest, GetCpuInfo_SSE42_IsTrue_OnModernX64) {
    // SSE 4.2 is mandatory for any x86-64 CPU manufactured after ~2008 and
    // required by UzEngine's compilation baseline.
    const uz::CpuInfo cpu = uz::GetCpuInfo();

    EXPECT_TRUE(cpu.hasSSE42)
        << "SSE 4.2 must be present on the target build machine";
}

TEST_F(SystemInfoTest, GetCpuInfo_BrandString_FitsBuffer) {
    const uz::CpuInfo cpu = uz::GetCpuInfo();

    // The brand array is 128 bytes. Verify it is null-terminated within bounds.
    bool hasNull = false;
    for (size_t i = 0; i < sizeof(cpu.brand); ++i) {
        if (cpu.brand[i] == '\0') {
            hasNull = true;
            break;
        }
    }
    EXPECT_TRUE(hasNull)
        << "CpuInfo::brand must be null-terminated within the 128-byte buffer";
}

TEST_F(SystemInfoTest, GetCpuInfo_CalledTwice_ReturnsSamePhysicalCores) {
    // GetCpuInfo must be deterministic — calling it twice yields the same count.
    const uz::CpuInfo first  = uz::GetCpuInfo();
    const uz::CpuInfo second = uz::GetCpuInfo();

    EXPECT_EQ(first.physicalCores, second.physicalCores);
    EXPECT_EQ(first.logicalCores,  second.logicalCores);
}

// ─────────────────────────────────────────────────────────────────────────────
// MemoryInfo
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SystemInfoTest, GetMemoryInfo_TotalPhysicalMB_IsGreaterThanZero) {
    const uz::MemoryInfo mem = uz::GetMemoryInfo();

    EXPECT_GT(mem.totalPhysicalMB, 0u)
        << "Total physical RAM must be > 0 on any real machine";
}

TEST_F(SystemInfoTest, GetMemoryInfo_AvailablePhysicalMB_IsGreaterThanZero) {
    const uz::MemoryInfo mem = uz::GetMemoryInfo();

    EXPECT_GT(mem.availablePhysicalMB, 0u)
        << "Available physical RAM must be > 0 while the test process is running";
}

TEST_F(SystemInfoTest, GetMemoryInfo_AvailablePhysicalMB_LessOrEqualTotal) {
    const uz::MemoryInfo mem = uz::GetMemoryInfo();

    EXPECT_LE(mem.availablePhysicalMB, mem.totalPhysicalMB)
        << "Available RAM cannot exceed total installed RAM";
}

TEST_F(SystemInfoTest, GetMemoryInfo_UsagePercent_IsInRange0To100) {
    const uz::MemoryInfo mem = uz::GetMemoryInfo();

    EXPECT_LE(mem.usagePercent, 100u)
        << "Memory usage percentage must be in the range [0, 100]";
}

TEST_F(SystemInfoTest, GetMemoryInfo_TotalVirtualMB_IsGreaterThanZero) {
    const uz::MemoryInfo mem = uz::GetMemoryInfo();

    EXPECT_GT(mem.totalVirtualMB, 0u)
        << "Total virtual address space must be > 0 on a 64-bit process";
}

// ─────────────────────────────────────────────────────────────────────────────
// OsInfo
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SystemInfoTest, GetOsInfo_Name_IsNonEmpty) {
    const uz::OsInfo os = uz::GetOsInfo();

    EXPECT_GT(std::strlen(os.name), 0u)
        << "OS name must not be empty";
}

TEST_F(SystemInfoTest, GetOsInfo_MajorVersion_IsAtLeast10) {
    // UzEngine targets Windows 10 and Windows 11 only.
    const uz::OsInfo os = uz::GetOsInfo();

    EXPECT_GE(os.majorVersion, 10u)
        << "UzEngine requires Windows 10+; major version must be >= 10";
}

TEST_F(SystemInfoTest, GetOsInfo_BuildNumber_IsGreaterThanZero) {
    const uz::OsInfo os = uz::GetOsInfo();

    EXPECT_GT(os.buildNumber, 0u)
        << "Windows build number must be a positive value";
}

TEST_F(SystemInfoTest, GetOsInfo_Architecture_IsNonEmpty) {
    const uz::OsInfo os = uz::GetOsInfo();

    EXPECT_GT(std::strlen(os.architecture), 0u)
        << "Architecture string (e.g. 'x64') must not be empty";
}

TEST_F(SystemInfoTest, GetOsInfo_Architecture_FitsBuffer) {
    const uz::OsInfo os = uz::GetOsInfo();

    bool hasNull = false;
    for (size_t i = 0; i < sizeof(os.architecture); ++i) {
        if (os.architecture[i] == '\0') { hasNull = true; break; }
    }
    EXPECT_TRUE(hasNull)
        << "OsInfo::architecture must be null-terminated within 16 bytes";
}

TEST_F(SystemInfoTest, GetOsInfo_Name_FitsBuffer) {
    const uz::OsInfo os = uz::GetOsInfo();

    bool hasNull = false;
    for (size_t i = 0; i < sizeof(os.name); ++i) {
        if (os.name[i] == '\0') { hasNull = true; break; }
    }
    EXPECT_TRUE(hasNull)
        << "OsInfo::name must be null-terminated within 128 bytes";
}

// ─────────────────────────────────────────────────────────────────────────────
// GpuInfo
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SystemInfoTest, GetGpuInfo_DoesNotCrash) {
    // GetGpuInfo uses DXGI — must not throw or crash even on a headless machine.
    EXPECT_NO_FATAL_FAILURE({
        const uz::GpuInfo gpu = uz::GetGpuInfo();
        (void)gpu;
    });
}

TEST_F(SystemInfoTest, GetGpuInfo_WhenGpuPresent_NameIsNonEmpty) {
    const uz::GpuInfo gpu = uz::GetGpuInfo();

    // On a machine with a GPU the name field must be populated.
    // On a headless server name[0] may be '\0' — that is acceptable.
    // We only assert the name buffer is properly null-terminated.
    bool hasNull = false;
    for (size_t i = 0; i < sizeof(gpu.name); ++i) {
        if (gpu.name[i] == '\0') { hasNull = true; break; }
    }
    EXPECT_TRUE(hasNull)
        << "GpuInfo::name must be null-terminated within 256 bytes";

    if (gpu.name[0] != '\0') {
        // A GPU was found — vendor and device IDs must be non-zero.
        EXPECT_GT(gpu.vendorId, 0u)
            << "GpuInfo::vendorId must be non-zero when a GPU adapter is present";
        EXPECT_GT(gpu.deviceId, 0u)
            << "GpuInfo::deviceId must be non-zero when a GPU adapter is present";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BatteryInfo
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SystemInfoTest, GetBatteryInfo_DoesNotCrash) {
    // Battery may or may not be present — must never crash.
    EXPECT_NO_FATAL_FAILURE({
        const uz::BatteryInfo bat = uz::GetBatteryInfo();
        (void)bat;
    });
}

TEST_F(SystemInfoTest, GetBatteryInfo_WhenNoBattery_PercentageIs255OrZero) {
    const uz::BatteryInfo bat = uz::GetBatteryInfo();

    if (!bat.hasBattery) {
        // Windows returns BATTERY_PERCENTAGE_UNKNOWN (255) or 0 on AC-only machines.
        const bool validUnknown = (bat.percentage == 255u || bat.percentage == 0u);
        EXPECT_TRUE(validUnknown)
            << "Without a battery, percentage should be 255 (unknown) or 0, got "
            << static_cast<unsigned>(bat.percentage);
    }
}

TEST_F(SystemInfoTest, GetBatteryInfo_WhenBatteryPresent_PercentageIsAtMost100) {
    const uz::BatteryInfo bat = uz::GetBatteryInfo();

    if (bat.hasBattery && bat.percentage != 255u) {
        EXPECT_LE(bat.percentage, 100u)
            << "Battery percentage must be in [0, 100] when known";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LocaleInfo
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SystemInfoTest, GetLocaleInfo_Language_IsNonEmpty) {
    const uz::LocaleInfo loc = uz::GetLocaleInfo_();

    EXPECT_GT(std::strlen(loc.language), 0u)
        << "Locale language tag must not be empty (e.g. 'en-US', 'pt-BR')";
}

TEST_F(SystemInfoTest, GetLocaleInfo_Language_FitsBuffer) {
    const uz::LocaleInfo loc = uz::GetLocaleInfo_();

    bool hasNull = false;
    for (size_t i = 0; i < sizeof(loc.language); ++i) {
        if (loc.language[i] == '\0') { hasNull = true; break; }
    }
    EXPECT_TRUE(hasNull)
        << "LocaleInfo::language must be null-terminated within 16 bytes";
}

TEST_F(SystemInfoTest, GetLocaleInfo_Region_FitsBuffer) {
    const uz::LocaleInfo loc = uz::GetLocaleInfo_();

    bool hasNull = false;
    for (size_t i = 0; i < sizeof(loc.region); ++i) {
        if (loc.region[i] == '\0') { hasNull = true; break; }
    }
    EXPECT_TRUE(hasNull)
        << "LocaleInfo::region must be null-terminated within 16 bytes";
}

TEST_F(SystemInfoTest, GetLocaleInfo_CalledTwice_ReturnsSameLanguage) {
    const uz::LocaleInfo first  = uz::GetLocaleInfo_();
    const uz::LocaleInfo second = uz::GetLocaleInfo_();

    EXPECT_STREQ(first.language, second.language)
        << "GetLocaleInfo_ must be deterministic within a single process run";
}

// ─────────────────────────────────────────────────────────────────────────────
// LogSystemInfo
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SystemInfoTest, LogSystemInfo_DoesNotCrash) {
    // LogSystemInfo calls every query function and emits UZ_LOG lines.
    // The only requirement here is that it completes without crashing.
    EXPECT_NO_FATAL_FAILURE(uz::LogSystemInfo());
}

TEST_F(SystemInfoTest, LogSystemInfo_CalledTwice_IsStable) {
    // Ensure there is no internal state that breaks on repeated calls.
    EXPECT_NO_FATAL_FAILURE({
        uz::LogSystemInfo();
        uz::LogSystemInfo();
    });
}
