/// UzEngine System Information — Windows Implementation
///
/// Queries hardware and OS capabilities at engine startup for telemetry,
/// adaptive quality selection, and diagnostic logging. All info structs use
/// fixed-size buffers to avoid heap allocation, making them safe to capture
/// in crash handler context.
///
/// # Functions
/// - `GetCpuInfo()` — CPUID-based CPU identification and feature detection.
/// - `GetMemoryInfo()` — Physical and virtual memory status.
/// - `GetOsInfo()` — OS version via RtlGetVersion (manifest-independent).
/// - `GetGpuInfo()` — Primary GPU adapter info via DXGI.
/// - `GetBatteryInfo()` — AC/battery power status.
/// - `GetLocaleInfo_()` — User locale (BCP-47 tag).
/// - `LogSystemInfo()` — Logs all of the above at startup.
///
/// # Thread Safety
/// All functions are safe to call from any thread. They query OS APIs that
/// are internally synchronized. The structs are value types with no shared
/// state.

#pragma once

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CPU Information
// ─────────────────────────────────────────────────────────────────────────────

/// CPU identification and feature flags, queried via CPUID intrinsics.
///
/// Brand string comes from CPUID leaves 0x80000002-0x80000004.
/// Feature flags from leaf 1 (SSE4.2, AVX, FMA, AES-NI) and leaf 7 (AVX2,
/// AVX-512F). Core counts from GetLogicalProcessorInformation.
struct CpuInfo {
    char brand[128];           ///< CPU brand string (e.g., "AMD Ryzen 9 7950X").
    uint32_t physicalCores;    ///< Physical core count.
    uint32_t logicalCores;     ///< Logical core count (includes hyperthreading).
    uint32_t l1CacheKB;        ///< L1 data cache size in KB (0 if unavailable).
    uint32_t l2CacheKB;        ///< L2 cache size in KB.
    uint32_t l3CacheKB;        ///< L3 cache size in KB.
    bool hasSSE42;             ///< SSE 4.2 support.
    bool hasAVX;               ///< AVX support.
    bool hasAVX2;              ///< AVX2 support.
    bool hasAVX512;            ///< AVX-512F support.
    bool hasFMA;               ///< FMA3 support.
    bool hasAESNI;             ///< AES-NI support.
};

// ─────────────────────────────────────────────────────────────────────────────
// Memory Information
// ─────────────────────────────────────────────────────────────────────────────

/// System memory status queried via GlobalMemoryStatusEx.
struct MemoryInfo {
    uint64_t totalPhysicalMB;      ///< Total installed physical RAM in MB.
    uint64_t availablePhysicalMB;  ///< Currently available physical RAM in MB.
    uint64_t totalVirtualMB;       ///< Total virtual address space in MB.
    uint32_t usagePercent;         ///< Memory load percentage, 0-100.
};

// ─────────────────────────────────────────────────────────────────────────────
// OS Information
// ─────────────────────────────────────────────────────────────────────────────

/// Operating system version queried via RtlGetVersion.
///
/// Uses RtlGetVersion instead of GetVersionEx to avoid application manifest
/// dependency that caps reported version at Windows 8.1.
struct OsInfo {
    char name[128];            ///< Human-readable name (e.g., "Windows 11 Pro").
    uint32_t majorVersion;     ///< Major version number (e.g., 10).
    uint32_t minorVersion;     ///< Minor version number (e.g., 0).
    uint32_t buildNumber;      ///< Build number (e.g., 22631).
    char architecture[16];     ///< Processor architecture ("x64", "ARM64").
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU Information
// ─────────────────────────────────────────────────────────────────────────────

/// Primary GPU adapter information queried via DXGI.
///
/// Enumerates DXGI adapters and returns info for the first non-software
/// adapter (adapter index 0). VRAM values are in megabytes.
struct GpuInfo {
    char name[256];            ///< Adapter description (e.g., "NVIDIA GeForce RTX 4090").
    char driverVersion[64];    ///< Driver version string (from LARGE_INTEGER).
    uint32_t vendorId;         ///< PCI vendor ID (e.g., 0x10DE for NVIDIA).
    uint32_t deviceId;         ///< PCI device ID.
    uint64_t dedicatedVRAM_MB; ///< Dedicated video memory in MB.
    uint64_t sharedVRAM_MB;    ///< Shared system memory in MB.
};

// ─────────────────────────────────────────────────────────────────────────────
// Battery Information
// ─────────────────────────────────────────────────────────────────────────────

/// Battery / power status queried via GetSystemPowerStatus.
struct BatteryInfo {
    bool hasBattery;           ///< True if a battery is present.
    bool isCharging;           ///< True if connected to AC power.
    uint8_t percentage;        ///< Charge percentage 0-100, or 255 if unknown.
};

// ─────────────────────────────────────────────────────────────────────────────
// Locale Information
// ─────────────────────────────────────────────────────────────────────────────

/// User locale derived from GetUserDefaultLocaleName.
struct LocaleInfo {
    char language[16];         ///< BCP-47 language tag (e.g., "en-US").
    char region[16];           ///< Region subtag (e.g., "US"), empty if absent.
};

// ─────────────────────────────────────────────────────────────────────────────
// Query Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Queries CPU identification, core counts, cache sizes, and feature flags.
///
/// # Returns
/// Populated CpuInfo. Fields default to zero/false if the corresponding
/// CPUID leaf is not supported by the processor.
[[nodiscard]] CpuInfo GetCpuInfo();

/// Queries current physical and virtual memory status.
///
/// # Returns
/// Populated MemoryInfo. All fields zero on failure (unlikely).
[[nodiscard]] MemoryInfo GetMemoryInfo();

/// Queries OS version using RtlGetVersion (manifest-independent).
///
/// # Returns
/// Populated OsInfo. Name string attempts "Windows 10/11" differentiation
/// based on build number (build >= 22000 = Windows 11).
[[nodiscard]] OsInfo GetOsInfo();

/// Queries primary GPU adapter via DXGI factory enumeration.
///
/// # Returns
/// Populated GpuInfo for adapter 0. All fields zero/empty if DXGI
/// enumeration fails (e.g., headless server with no GPU).
[[nodiscard]] GpuInfo GetGpuInfo();

/// Queries battery and AC power status.
///
/// # Returns
/// Populated BatteryInfo. hasBattery is false on desktops.
[[nodiscard]] BatteryInfo GetBatteryInfo();

/// Queries user default locale.
///
/// Trailing underscore avoids collision with Win32 GetLocaleInfo macro.
///
/// # Returns
/// Populated LocaleInfo with BCP-47 language tag and region subtag.
[[nodiscard]] LocaleInfo GetLocaleInfo_();

/// Logs all system information using the Platform log category.
///
/// Calls every query function above and emits Info-level log lines.
/// Intended to be called once at engine startup after log initialization.
void LogSystemInfo();

} // namespace uz
