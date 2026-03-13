#pragma once

/// UzEngine System Information — macOS Implementation
///
/// Queries hardware and OS capabilities at engine startup for telemetry,
/// adaptive quality selection, and diagnostic logging. All info structs use
/// fixed-size buffers to avoid heap allocation, making them safe to capture
/// in crash handler context.
///
/// macOS does not expose /proc or /sys. This implementation uses:
/// - `sysctlbyname` for CPU topology, cache sizes, and feature flags.
/// - Mach VM statistics via `host_statistics64` for memory pressure.
/// - `uname()` for kernel version.
/// - Metal framework (`MTLCreateSystemDefaultDevice`) for GPU adapter name.
/// - IOKit `AppleSmartBattery` registry entry for battery status.
/// - POSIX `setlocale` / LANG env var for locale (same as Linux).
///
/// # Functions
/// - `GetCpuInfo()`      — sysctlbyname for topology, features, and cache sizes.
/// - `GetMemoryInfo()`   — Mach VM info for physical memory pressure.
/// - `GetOsInfo()`       — uname() + kern.osproductversion for product version.
/// - `GetGpuInfo()`      — Metal default device for adapter name and power class.
/// - `GetBatteryInfo()`  — IOKit AppleSmartBattery registry for charge state.
/// - `GetLocaleInfo_()`  — POSIX setlocale / LANG env var for locale tag.
/// - `LogSystemInfo()`   — Logs all of the above at startup.
///
/// # Thread Safety
/// All functions are safe to call from any thread. They query read-only kernel
/// state or instantiate temporary Metal/IOKit objects. The structs are plain
/// value types with no shared mutable state.

#ifdef __APPLE__

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CPU Information
// ─────────────────────────────────────────────────────────────────────────────

/// CPU identification and feature flags.
///
/// Brand string is read from `machdep.cpu.brand_string` via sysctlbyname.
/// On Apple Silicon the brand reflects the SoC name (e.g., "Apple M3 Pro").
/// Physical and logical core counts come from `hw.physicalcpu` and
/// `hw.logicalcpu`. Cache sizes come from `hw.l1dcachesize`, `hw.l2cachesize`,
/// and `hw.l3cachesize` (returned in bytes, stored in KB).
/// Feature flags come from `hw.optional.*` sysctl nodes; on Apple Silicon
/// (arm64) the x86 SIMD flags are left false as the ISA is incompatible.
struct CpuInfo {
    char     brand[128];        ///< CPU brand string (e.g., "Apple M3 Pro" or "Intel Core i9-13900K").
    uint32_t physicalCores;     ///< Physical core count from hw.physicalcpu.
    uint32_t logicalCores;      ///< Logical core count from hw.logicalcpu (includes hyperthreading on Intel).
    uint32_t l1CacheKB;         ///< L1 data cache size in KB (0 if unavailable).
    uint32_t l2CacheKB;         ///< L2 cache size in KB (0 if unavailable).
    uint32_t l3CacheKB;         ///< L3 cache size in KB (0 on Apple Silicon — no shared L3 node).
    bool     hasSSE42;          ///< SSE 4.2 support (Intel only; false on Apple Silicon).
    bool     hasAVX;            ///< AVX support (Intel only; false on Apple Silicon).
    bool     hasAVX2;           ///< AVX2 support (Intel only; false on Apple Silicon).
    bool     hasAVX512;         ///< AVX-512F support (Intel only; false on Apple Silicon).
    bool     hasFMA;            ///< FMA3 support (Intel only; false on Apple Silicon).
    bool     hasAESNI;          ///< AES hardware acceleration (present on both Intel and Apple Silicon).
};

// ─────────────────────────────────────────────────────────────────────────────
// Memory Information
// ─────────────────────────────────────────────────────────────────────────────

/// System memory status queried via Mach VM host statistics.
///
/// Total physical memory comes from `hw.memsize` (bytes). Available memory is
/// computed from `vm_statistics64` as (free_count + inactive_count) * page_size.
/// macOS does not expose a CommitLimit equivalent; totalVirtualMB mirrors
/// totalPhysicalMB as an approximation of the usable address space.
struct MemoryInfo {
    uint64_t totalPhysicalMB;      ///< Total installed physical RAM in MB.
    uint64_t availablePhysicalMB;  ///< Currently available RAM in MB (free + inactive pages).
    uint64_t totalVirtualMB;       ///< Virtual address space in MB (approximated as totalPhysicalMB on macOS).
    uint32_t usagePercent;         ///< Approximate memory load percentage, 0–100.
};

// ─────────────────────────────────────────────────────────────────────────────
// OS Information
// ─────────────────────────────────────────────────────────────────────────────

/// Operating system version information.
///
/// The human-readable name is composed from the sysname ("Darwin") and the
/// product version string from `kern.osproductversion` (e.g., "14.5").
/// majorVersion / minorVersion / buildNumber are parsed from the product
/// version string rather than the kernel release (more user-meaningful).
/// Architecture comes from utsname.machine ("x86_64" or "arm64").
struct OsInfo {
    char     name[128];        ///< Human-readable name (e.g., "macOS 14.5").
    uint32_t majorVersion;     ///< macOS product major version (e.g., 14 for Sonoma).
    uint32_t minorVersion;     ///< macOS product minor version.
    uint32_t buildNumber;      ///< macOS product patch level (third field, 0 if absent).
    char     architecture[16]; ///< Machine architecture from utsname.machine ("x86_64" or "arm64").
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU Information
// ─────────────────────────────────────────────────────────────────────────────

/// Primary GPU adapter information via the Metal framework.
///
/// Uses `MTLCreateSystemDefaultDevice()` to obtain the system default Metal
/// device. The adapter name is read from `[MTLDevice name]`. The `isLowPower`
/// property maps to `dedicatedVRAM_MB == 0` (integrated / efficiency GPU).
/// PCI vendor/device IDs are not surfaced by Metal; they remain zero.
/// On headless systems where Metal is unavailable, all fields are empty/zero.
struct GpuInfo {
    char     name[256];          ///< Adapter name from [MTLDevice name] (e.g., "Apple M3 Pro" or "AMD Radeon Pro 5500M").
    char     driverVersion[64];  ///< Driver version string (empty — not exposed by Metal API).
    uint32_t vendorId;           ///< PCI vendor ID (0 — not surfaced by Metal).
    uint32_t deviceId;           ///< PCI device ID (0 — not surfaced by Metal).
    uint64_t dedicatedVRAM_MB;   ///< Dedicated VRAM in MB from [MTLDevice recommendedMaxWorkingSetSize] (0 for integrated).
    uint64_t sharedVRAM_MB;      ///< Shared/unified memory in MB (0 if Metal reports dedicated VRAM).
};

// ─────────────────────────────────────────────────────────────────────────────
// Battery Information
// ─────────────────────────────────────────────────────────────────────────────

/// Battery and AC power status from the IOKit AppleSmartBattery registry entry.
///
/// Reads `CurrentCapacity`, `MaxCapacity`, and `IsCharging` properties from
/// the `AppleSmartBattery` IOService. On desktop Macs without a battery the
/// service is absent and hasBattery is false.
struct BatteryInfo {
    bool    hasBattery;  ///< True if the AppleSmartBattery IOService is present.
    bool    isCharging;  ///< True if the battery IsCharging property is non-zero.
    uint8_t percentage;  ///< Charge percentage 0–100, or 255 if unreadable.
};

// ─────────────────────────────────────────────────────────────────────────────
// Locale Information
// ─────────────────────────────────────────────────────────────────────────────

/// User locale derived from setlocale(LC_ALL, "") and the LANG env var.
///
/// macOS supports the same POSIX locale mechanism as Linux. Locale strings
/// follow the "language_REGION.encoding" convention (e.g., "en_US.UTF-8").
/// The language field is stored as a BCP-47 tag (underscore replaced by hyphen).
struct LocaleInfo {
    char language[16]; ///< BCP-47-style language tag (e.g., "en-US" or "pt-BR").
    char region[16];   ///< Region subtag extracted from the locale string (e.g., "US").
};

// ─────────────────────────────────────────────────────────────────────────────
// Query Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Queries CPU identification, core counts, cache sizes, and feature flags.
///
/// Uses `sysctlbyname` to read `machdep.cpu.brand_string`, `hw.physicalcpu`,
/// `hw.logicalcpu`, `hw.l1dcachesize`, `hw.l2cachesize`, `hw.l3cachesize`,
/// and `hw.optional.*` feature flag nodes.
///
/// On Apple Silicon, the `hw.optional.sse4_2`, `hw.optional.avx*`, and
/// `hw.optional.fma` nodes are absent; their flags remain false. The AES flag
/// (`hw.optional.aes`) is present and true on all modern Apple Silicon SoCs.
///
/// # Returns
/// Populated CpuInfo. Fields default to zero/false if the sysctl node is
/// absent or the kernel call fails.
[[nodiscard]] CpuInfo GetCpuInfo();

/// Queries current physical and virtual memory status via Mach VM host info.
///
/// Reads `hw.memsize` for total physical RAM. Available memory is derived
/// from `host_statistics64(HOST_VM_INFO64)` as
/// `(free_count + inactive_count) * vm_page_size`.
///
/// # Returns
/// Populated MemoryInfo. All fields zero if the Mach call fails.
[[nodiscard]] MemoryInfo GetMemoryInfo();

/// Queries OS version via uname() and kern.osproductversion.
///
/// `kern.osproductversion` returns the user-visible macOS version (e.g., "14.5")
/// rather than the Darwin kernel version. majorVersion / minorVersion /
/// buildNumber are parsed from that string.
///
/// # Returns
/// Populated OsInfo. majorVersion/minorVersion/buildNumber default to zero
/// if the sysctl or uname call fails.
[[nodiscard]] OsInfo GetOsInfo();

/// Queries the primary GPU adapter via the Metal framework.
///
/// Calls `MTLCreateSystemDefaultDevice()` and reads the adapter name and
/// working set size. This is the only non-root method to obtain a GPU name
/// on macOS without Vulkan or IOKit enumeration of the graphics accelerator.
///
/// # Returns
/// Populated GpuInfo with name and dedicatedVRAM_MB set. vendorId / deviceId
/// are always zero. All fields empty/zero on headless systems.
[[nodiscard]] GpuInfo GetGpuInfo();

/// Queries battery and AC power status from the IOKit registry.
///
/// Looks up the `AppleSmartBattery` IOService and reads `CurrentCapacity`,
/// `MaxCapacity`, and `IsCharging` as CFNumber / CFBoolean properties.
///
/// # Returns
/// Populated BatteryInfo. hasBattery is false on desktop Macs. percentage is
/// 255 if MaxCapacity is zero (prevents divide-by-zero).
[[nodiscard]] BatteryInfo GetBatteryInfo();

/// Queries user default locale from setlocale and the LANG environment variable.
///
/// Trailing underscore avoids a naming collision on some platforms.
///
/// # Returns
/// Populated LocaleInfo with language tag and region subtag.
/// Falls back to LANG env var if setlocale returns nullptr or empty.
[[nodiscard]] LocaleInfo GetLocaleInfo_();

/// Logs all system information using the Platform log category.
///
/// Calls every query function above and emits Info-level log lines via
/// `UZ_LOG(Platform, Info, ...)`. Intended to be called once at engine
/// startup after the log system is initialized.
void LogSystemInfo();

} // namespace uz

#endif // __APPLE__
