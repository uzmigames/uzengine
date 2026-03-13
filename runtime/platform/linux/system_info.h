#pragma once

/// UzEngine System Information — Linux / POSIX Implementation
///
/// Queries hardware and OS capabilities at engine startup for telemetry,
/// adaptive quality selection, and diagnostic logging. All info structs use
/// fixed-size buffers to avoid heap allocation, making them safe to capture
/// in crash handler context.
///
/// # Functions
/// - `GetCpuInfo()` — CPUID + /proc/cpuinfo + sysfs cache topology.
/// - `GetMemoryInfo()` — Physical memory status via /proc/meminfo.
/// - `GetOsInfo()` — OS version via uname().
/// - `GetGpuInfo()` — Best-effort GPU info via /sys/class/drm/.
/// - `GetBatteryInfo()` — Battery status via /sys/class/power_supply/.
/// - `GetLocaleInfo_()` — User locale via setlocale / LANG env var.
/// - `LogSystemInfo()` — Logs all of the above at startup.
///
/// # Thread Safety
/// All functions are safe to call from any thread. They open and read files
/// or call reentrant POSIX APIs. The structs are value types with no shared
/// state.

#ifdef __linux__

#pragma once

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CPU Information
// ─────────────────────────────────────────────────────────────────────────────

/// CPU identification and feature flags.
///
/// Brand string is parsed from /proc/cpuinfo "model name".
/// Feature flags are detected via __cpuid_count from <cpuid.h>.
/// Core counts are from sysconf(_SC_NPROCESSORS_ONLN) (logical) and
/// unique "core id" entries in /proc/cpuinfo (physical).
/// Cache sizes are read from /sys/devices/system/cpu/cpu0/cache/.
struct CpuInfo {
    char     brand[128];        ///< CPU brand string (e.g., "AMD Ryzen 9 7950X").
    uint32_t physicalCores;     ///< Physical core count.
    uint32_t logicalCores;      ///< Logical core count (includes hyperthreading).
    uint32_t l1CacheKB;         ///< L1 data cache size in KB (0 if unavailable).
    uint32_t l2CacheKB;         ///< L2 cache size in KB.
    uint32_t l3CacheKB;         ///< L3 cache size in KB.
    bool     hasSSE42;          ///< SSE 4.2 support.
    bool     hasAVX;            ///< AVX support.
    bool     hasAVX2;           ///< AVX2 support.
    bool     hasAVX512;         ///< AVX-512F support.
    bool     hasFMA;            ///< FMA3 support.
    bool     hasAESNI;          ///< AES-NI support.
};

// ─────────────────────────────────────────────────────────────────────────────
// Memory Information
// ─────────────────────────────────────────────────────────────────────────────

/// System memory status parsed from /proc/meminfo.
struct MemoryInfo {
    uint64_t totalPhysicalMB;      ///< Total installed physical RAM in MB.
    uint64_t availablePhysicalMB;  ///< Currently available physical RAM in MB.
    uint64_t totalVirtualMB;       ///< Total virtual address space in MB (CommitLimit).
    uint32_t usagePercent;         ///< Approximate memory load percentage, 0–100.
};

// ─────────────────────────────────────────────────────────────────────────────
// OS Information
// ─────────────────────────────────────────────────────────────────────────────

/// Operating system version from uname().
struct OsInfo {
    char     name[128];        ///< Human-readable name (e.g., "Linux 6.8.0-51-generic").
    uint32_t majorVersion;     ///< Kernel major version (parsed from utsname.release).
    uint32_t minorVersion;     ///< Kernel minor version.
    uint32_t buildNumber;      ///< Kernel patch level (third field, best-effort).
    char     architecture[16]; ///< Machine architecture from utsname.machine (e.g., "x86_64").
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU Information
// ─────────────────────────────────────────────────────────────────────────────

/// Primary GPU adapter information — best effort via /sys/class/drm/.
///
/// Reads vendor/device IDs from `/sys/class/drm/card0/device/`. No driver
/// version or VRAM size is available without root or Vulkan. Returns empty
/// strings and zero IDs on headless systems or when the sysfs node is absent.
struct GpuInfo {
    char     name[256];          ///< Adapter name (empty on Linux — not available via sysfs).
    char     driverVersion[64];  ///< Driver version (empty — requires Vulkan or root).
    uint32_t vendorId;           ///< PCI vendor ID from /sys/class/drm/card0/device/vendor.
    uint32_t deviceId;           ///< PCI device ID from /sys/class/drm/card0/device/device.
    uint64_t dedicatedVRAM_MB;   ///< Dedicated VRAM in MB (0 — not available via sysfs).
    uint64_t sharedVRAM_MB;      ///< Shared memory in MB (0 — not available via sysfs).
};

// ─────────────────────────────────────────────────────────────────────────────
// Battery Information
// ─────────────────────────────────────────────────────────────────────────────

/// Battery / power status read from /sys/class/power_supply/BAT0/.
struct BatteryInfo {
    bool    hasBattery;  ///< True if /sys/class/power_supply/BAT0/ exists.
    bool    isCharging;  ///< True if AC adapter is online (/sys/.../AC/online == 1).
    uint8_t percentage;  ///< Charge percentage 0–100, or 255 if unreadable.
};

// ─────────────────────────────────────────────────────────────────────────────
// Locale Information
// ─────────────────────────────────────────────────────────────────────────────

/// User locale derived from setlocale(LC_ALL, "") and the LANG env var.
struct LocaleInfo {
    char language[16]; ///< BCP-47-style language tag (e.g., "en-US" or "pt_BR").
    char region[16];   ///< Region subtag extracted from the locale string.
};

// ─────────────────────────────────────────────────────────────────────────────
// Query Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Queries CPU identification, core counts, cache sizes, and feature flags.
///
/// Parses /proc/cpuinfo for brand name and physical topology.
/// Reads /sys/devices/system/cpu/cpu0/cache/index* for cache sizes.
/// Calls __cpuid_count for feature flags.
///
/// # Returns
/// Populated CpuInfo. Fields default to zero/false if the data source
/// is absent (e.g., no sysfs cache topology on this kernel).
[[nodiscard]] CpuInfo GetCpuInfo();

/// Queries current physical and virtual memory status from /proc/meminfo.
///
/// # Returns
/// Populated MemoryInfo. All fields zero on parse failure.
[[nodiscard]] MemoryInfo GetMemoryInfo();

/// Queries OS kernel version via uname().
///
/// # Returns
/// Populated OsInfo. majorVersion/minorVersion/buildNumber parsed from
/// utsname.release (e.g., "6.8.0" → 6, 8, 0). Returns zero fields if
/// uname() fails (extremely unlikely).
[[nodiscard]] OsInfo GetOsInfo();

/// Queries primary GPU adapter from /sys/class/drm/card0/device/.
///
/// This is a best-effort read of PCI vendor/device IDs only. Name, VRAM,
/// and driver version require Vulkan enumeration (not done here to avoid
/// linking Vulkan into the platform layer).
///
/// # Returns
/// Populated GpuInfo with vendorId/deviceId if sysfs nodes exist.
/// All fields zero/empty on headless systems.
[[nodiscard]] GpuInfo GetGpuInfo();

/// Queries battery and AC power status from /sys/class/power_supply/.
///
/// # Returns
/// Populated BatteryInfo. hasBattery is false on desktops without BAT0.
[[nodiscard]] BatteryInfo GetBatteryInfo();

/// Queries user default locale from setlocale and the LANG environment variable.
///
/// Trailing underscore avoids a naming collision on some platforms.
///
/// # Returns
/// Populated LocaleInfo with language tag and region subtag.
/// Falls back to LANG env var if setlocale returns nullptr.
[[nodiscard]] LocaleInfo GetLocaleInfo_();

/// Logs all system information using the Platform log category.
///
/// Calls every query function above and emits Info-level log lines.
/// Intended to be called once at engine startup after log initialization.
void LogSystemInfo();

} // namespace uz

#endif // __linux__
