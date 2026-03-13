# Platform — System Information

> **Status**: Implemented
> **Source**: `platform/windows/system_info.h`
> **Depends on**: CPUID intrinsics, GlobalMemoryStatusEx, RtlGetVersion, DXGI, GetSystemPowerStatus, GetUserDefaultLocaleName

## Purpose

The engine auto-detects hardware and OS capabilities to:
- Automatically choose quality presets (low for integrated GPU, high for RTX)
- Enrich crash reports with system context
- Gate features requiring specific GPUs (mesh shaders) or CPU features (AVX-512)
- Track performance across hardware configurations
- Enable locale-aware formatting (date, numbers, currency)

`IPlatformMisc::GetCpuInfo()`, `GetMemoryInfo()`, `GetOsInfo()` provide this information without platform-specific code.

## Architecture

```
IPlatformMisc
    │
    ├── CpuInfo
    │   ├── Brand string ("Intel Core i9-13900K")
    │   ├── Physical cores
    │   ├── Logical cores (with hyperthreading)
    │   ├── Cache sizes (L1/L2/L3)
    │   └── Feature flags (SSE4.2, AVX, AVX2, AVX-512, BMI2)
    │
    ├── MemoryInfo
    │   ├── Physical RAM (total / available)
    │   ├── Virtual memory (total / available)
    │   └── Page size
    │
    ├── OsInfo
    │   ├── OS name ("Windows 11")
    │   ├── Version (major.minor.build)
    │   ├── Architecture ("x64" / "ARM64")
    │   └── Build number
    │
    ├── GpuInfo
    │   ├── Adapter name
    │   ├── Vendor ID
    │   ├── Driver version
    │   └── Dedicated VRAM
    │
    ├── BatteryInfo
    │   ├── Plugged in (AC / battery)
    │   ├── Charge percentage
    │   └── Battery health
    │
    └── LocaleInfo
        ├── Language code ("en-US", "pt-BR")
        ├── Region
        ├── Number format
        ├── Date format
        └── Currency code
```

## CPU Information

Queries CPU identification, core counts, cache sizes, and feature flags via CPUID intrinsics and Windows API.

```cpp
/// CPU identification and feature flags, queried via CPUID intrinsics.
///
/// Brand string comes from CPUID leaves 0x80000002-0x80000004.
/// Feature flags from leaf 1 (SSE4.2, AVX, FMA, AES-NI) and leaf 7 (AVX2, AVX-512F).
/// Core counts from GetLogicalProcessorInformation.
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

/// Queries CPU identification, core counts, cache sizes, and feature flags.
///
/// # Returns
/// Populated CpuInfo. Fields default to zero/false if the corresponding
/// CPUID leaf is not supported by the processor.
[[nodiscard]] CpuInfo GetCpuInfo();

// Usage
CpuInfo cpu = uz::GetCpuInfo();
UZ_LOG(Platform, Info,
    "CPU: {} ({}/{} cores, L3={} KB)",
    cpu.brand, cpu.physicalCores, cpu.logicalCores, cpu.l3CacheKB);

// Feature-gate SIMD code
if (cpu.hasAVX2) {
    g_math->UseAvx2Paths();
} else {
    g_math->UseScalarPaths();
}
```

## Memory Information

```cpp
/// System memory status queried via GlobalMemoryStatusEx.
struct MemoryInfo {
    uint64_t totalPhysicalMB;      ///< Total installed physical RAM in MB.
    uint64_t availablePhysicalMB;  ///< Currently available physical RAM in MB.
    uint64_t totalVirtualMB;       ///< Total virtual address space in MB.
    uint32_t usagePercent;         ///< Memory load percentage, 0-100.
};

/// Queries current physical and virtual memory status.
///
/// # Returns
/// Populated MemoryInfo. All fields zero on failure (unlikely).
[[nodiscard]] MemoryInfo GetMemoryInfo();

// Usage
MemoryInfo mem = uz::GetMemoryInfo();
UZ_LOG(Platform, Info,
    "RAM: {}/{} MB available ({} % used)",
    mem.availablePhysicalMB,
    mem.totalPhysicalMB,
    mem.usagePercent);

// Auto-adjust streaming pool size based on available memory
uint64_t streaming_pool_mb = mem.availablePhysicalMB / 2;
g_asset_system->SetStreamingPoolSizeMB(streaming_pool_mb);
```

## OS Information

Queries OS version using RtlGetVersion (manifest-independent, avoids GetVersionEx limitation).

```cpp
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

/// Queries OS version using RtlGetVersion (manifest-independent).
///
/// # Returns
/// Populated OsInfo. Name string attempts "Windows 10/11" differentiation
/// based on build number (build >= 22000 = Windows 11).
[[nodiscard]] OsInfo GetOsInfo();

// Usage
OsInfo os = uz::GetOsInfo();
UZ_LOG(Platform, Info,
    "{} ({}.{}, Build {}, {})",
    os.name, os.majorVersion, os.minorVersion, os.buildNumber, os.architecture);
```

## GPU Information

Queries primary GPU adapter via DXGI factory enumeration.

```cpp
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

/// Queries primary GPU adapter via DXGI factory enumeration.
///
/// # Returns
/// Populated GpuInfo for adapter 0. All fields zero/empty if DXGI
/// enumeration fails (e.g., headless server with no GPU).
[[nodiscard]] GpuInfo GetGpuInfo();

// Usage
GpuInfo gpu = uz::GetGpuInfo();
UZ_LOG(Platform, Info,
    "GPU: {} ({} MB dedicated, {} MB shared, driver {})",
    gpu.name,
    gpu.dedicatedVRAM_MB,
    gpu.sharedVRAM_MB,
    gpu.driverVersion);

// Auto-adjust quality preset based on VRAM
if (gpu.dedicatedVRAM_MB > 8192) {
    g_renderer->SetQualityPreset(QualityPreset::Ultra);
} else if (gpu.dedicatedVRAM_MB > 4096) {
    g_renderer->SetQualityPreset(QualityPreset::High);
} else {
    g_renderer->SetQualityPreset(QualityPreset::Low);
}
```

## Battery Information

Queries battery and AC power status via GetSystemPowerStatus.

```cpp
/// Battery / power status queried via GetSystemPowerStatus.
struct BatteryInfo {
    bool hasBattery;           ///< True if a battery is present.
    bool isCharging;           ///< True if connected to AC power.
    uint8_t percentage;        ///< Charge percentage 0-100, or 255 if unknown.
};

/// Queries battery and AC power status.
///
/// # Returns
/// Populated BatteryInfo. hasBattery is false on desktops.
[[nodiscard]] BatteryInfo GetBatteryInfo();

// Usage
BatteryInfo battery = uz::GetBatteryInfo();
if (!battery.isCharging && battery.hasBattery && battery.percentage < 20) {
    UZ_LOG(Platform, Warning, "Low battery: {}%", static_cast<uint32_t>(battery.percentage));
    g_renderer->SetQualityPreset(QualityPreset::Low);  // Save power
}
```

## Locale Information

Queries user default locale via GetUserDefaultLocaleName.

```cpp
/// User locale derived from GetUserDefaultLocaleName.
struct LocaleInfo {
    char language[16];         ///< BCP-47 language tag (e.g., "en-US").
    char region[16];           ///< Region subtag (e.g., "US"), empty if absent.
};

/// Queries user default locale.
///
/// Trailing underscore avoids collision with Win32 GetLocaleInfo macro.
///
/// # Returns
/// Populated LocaleInfo with BCP-47 language tag and region subtag.
[[nodiscard]] LocaleInfo GetLocaleInfo_();

// Usage
LocaleInfo locale = uz::GetLocaleInfo_();
UZ_LOG(Platform, Info,
    "Locale: {}-{}",
    locale.language, locale.region);

// Initialize localization system
std::string lang_region = std::string(locale.language);
if (locale.region[0] != '\0') {
    lang_region += "_";
    lang_region += locale.region;
}
g_locale->SetCurrentLocale(lang_region.c_str());
```

## Startup Pattern: LogSystemInfo()

Call LogSystemInfo() once during engine initialization to dump all system information to logs:

```cpp
/// Logs all system information using the Platform log category.
///
/// Calls every query function above and emits Info-level log lines.
/// Intended to be called once at engine startup after log initialization.
void LogSystemInfo();

// Usage in engine startup
void EngineInit() {
    // ... initialize logging system ...

    // Log hardware and OS info for diagnostics
    uz::LogSystemInfo();

    // ... rest of initialization ...
}
```

Example output:

```
[Platform] CPU: Intel Core i9-13900K (24/32 cores, L3=36864 KB)
[Platform]     SSE4.2=yes AVX=yes AVX2=yes AVX-512F=yes FMA=yes AES-NI=yes
[Platform] RAM: 128000/256000 MB available (50% used)
[Platform] OS: Windows 11 Pro (10.0, Build 22631, x64)
[Platform] GPU: NVIDIA GeForce RTX 4090 (24576 MB dedicated, 0 MB shared, driver 555.99)
[Platform] Battery: Plugged in (Desktop)
[Platform] Locale: en-US
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| CPU info | `FPlatformMisc::GetCPUBrand()` + CPUID | `OS::get_processor_count()` | `CpuInfo` struct |
| Memory info | `FPlatformMisc::GetPhysicalGBRam()` | `OS::get_static_memory_usage()` | `MemoryInfo` struct |
| OS info | `FGenericPlatformMisc::GetOSVersion()` | `OS::get_name()`, `get_unique_id()` | `OsInfo` struct |
| GPU info | RHI adapter info | `RenderingServer::get_rendering_device()` | `GpuInfo` struct + RHI |
| Battery | N/A | `OS::get_power_percent()` | `BatteryInfo` struct |
| Locale | `FInternationalization` | `TranslationServer` | `LocaleInfo` struct |

## Rules

1. **ALWAYS call system information queries once at startup** — cache results via `LogSystemInfo()`, don't query every frame
2. **NEVER trust system reported VRAM** — use conservative estimate (80% of reported) for allocator budgets
3. **ALWAYS handle missing/zero values gracefully** — some systems don't report certain info (e.g., cache sizes, battery status on desktops)
4. **NEVER assume hyperthreading** — use `physicalCores < logicalCores` to detect it
5. **ALWAYS respect battery status** — reduce quality/frame rate when on battery (`!isCharging && hasBattery`)
6. **NEVER query GetLocaleInfo_() without checking field lengths** — buffers are fixed-size, may be empty strings
7. **ALWAYS handle DXGI enumeration failures** — headless servers return zero-filled GpuInfo, use CPU core count to detect
