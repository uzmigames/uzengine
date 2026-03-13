/// UzEngine System Information — macOS Implementation
///
/// Queries CPU, memory, OS, GPU, battery, and locale information using
/// sysctlbyname, Mach VM statistics, uname(), the Metal framework, and IOKit.
/// Compiled as Objective-C++ (.mm) because Metal and IOKit APIs require it.

#if defined(__APPLE__)

#include "system_info.h"
#include "log.h"
#include "profiler.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale.h>

#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <mach/mach.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>

#import <Metal/Metal.h>
#import <IOKit/IOKitLib.h>
#import <Foundation/Foundation.h>

/// Forward-declare the Platform log category. The category is defined elsewhere
/// in the platform layer (the same translation unit that owns the category definition
/// for the current platform). This matches the Linux system_info.cpp pattern.
UZ_DECLARE_LOG_CATEGORY(Platform);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Reads a null-terminated string via sysctlbyname into `out` (up to outSize-1 chars).
/// Returns true on success, false if the node does not exist or the buffer is too small.
bool SysctlString(const char* name, char* out, size_t outSize) noexcept {
    size_t len = outSize - 1;
    if (::sysctlbyname(name, out, &len, nullptr, 0) != 0) {
        out[0] = '\0';
        return false;
    }
    out[len] = '\0';
    return true;
}

/// Reads a uint32_t from a sysctlbyname node. Returns 0 on failure.
uint32_t SysctlUint32(const char* name) noexcept {
    uint32_t value = 0;
    size_t   len   = sizeof(value);
    ::sysctlbyname(name, &value, &len, nullptr, 0);
    return value;
}

/// Reads a uint64_t from a sysctlbyname node. Returns 0 on failure.
uint64_t SysctlUint64(const char* name) noexcept {
    uint64_t value = 0;
    size_t   len   = sizeof(value);
    ::sysctlbyname(name, &value, &len, nullptr, 0);
    return value;
}

/// Reads an int32 feature-flag node from hw.optional.*. Returns true if the
/// node exists and its value is non-zero.
bool SysctlOptionalFeature(const char* name) noexcept {
    int32_t value = 0;
    size_t  len   = sizeof(value);
    if (::sysctlbyname(name, &value, &len, nullptr, 0) != 0) {
        return false;
    }
    return value != 0;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// GetCpuInfo — sysctlbyname topology, cache sizes, and hw.optional.* flags
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] CpuInfo GetCpuInfo() {
    UZ_PROFILE_FUNCTION();

    CpuInfo info{};

    // ── Brand string ─────────────────────────────────────────────────────────
    // "machdep.cpu.brand_string" is present on Intel; on Apple Silicon the
    // kernel exposes the SoC name via the same node (e.g., "Apple M3 Pro").
    if (!SysctlString("machdep.cpu.brand_string", info.brand, sizeof(info.brand))) {
        std::snprintf(info.brand, sizeof(info.brand), "Unknown CPU");
    }

    // ── Core counts ──────────────────────────────────────────────────────────
    info.physicalCores = SysctlUint32("hw.physicalcpu");
    info.logicalCores  = SysctlUint32("hw.logicalcpu");

    // Guard against a kernel returning zero on unusual configurations.
    if (info.logicalCores == 0) {
        info.logicalCores = 1;
    }
    if (info.physicalCores == 0) {
        info.physicalCores = info.logicalCores;
    }

    // ── Cache sizes ───────────────────────────────────────────────────────────
    // sysctlbyname returns values in bytes; store as KB.
    const uint64_t l1Bytes = SysctlUint64("hw.l1dcachesize");
    const uint64_t l2Bytes = SysctlUint64("hw.l2cachesize");
    const uint64_t l3Bytes = SysctlUint64("hw.l3cachesize");

    constexpr uint64_t kBytesPerKB = 1024ULL;
    info.l1CacheKB = static_cast<uint32_t>(l1Bytes / kBytesPerKB);
    info.l2CacheKB = static_cast<uint32_t>(l2Bytes / kBytesPerKB);
    info.l3CacheKB = static_cast<uint32_t>(l3Bytes / kBytesPerKB);

    // ── Feature flags ─────────────────────────────────────────────────────────
    // These nodes are absent on Apple Silicon — SysctlOptionalFeature() returns
    // false for missing nodes, which is the correct behavior (arm64 ≠ x86).
    info.hasSSE42  = SysctlOptionalFeature("hw.optional.sse4_2");
    info.hasAVX    = SysctlOptionalFeature("hw.optional.avx1_0");
    info.hasAVX2   = SysctlOptionalFeature("hw.optional.avx2_0");
    info.hasAVX512 = SysctlOptionalFeature("hw.optional.avx512f");
    info.hasFMA    = SysctlOptionalFeature("hw.optional.fma");
    // AES is available on both Intel Macs and Apple Silicon (AMX coprocessor).
    info.hasAESNI  = SysctlOptionalFeature("hw.optional.aes");

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetMemoryInfo — hw.memsize + Mach VM host statistics
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] MemoryInfo GetMemoryInfo() {
    UZ_PROFILE_FUNCTION();

    MemoryInfo info{};

    // ── Total physical RAM ────────────────────────────────────────────────────
    const uint64_t totalBytes = SysctlUint64("hw.memsize");
    constexpr uint64_t kBytesPerMB = 1024ULL * 1024ULL;
    info.totalPhysicalMB = totalBytes / kBytesPerMB;

    // macOS does not expose CommitLimit; use physical total as approximation.
    info.totalVirtualMB = info.totalPhysicalMB;

    // ── Available RAM via Mach VM statistics ──────────────────────────────────
    // vm_statistics64 reports page counts. Available = free + inactive pages,
    // which is the standard macOS "available" definition (Activity Monitor uses
    // the same formula). Inactive pages are reclaimed first under pressure.
    vm_statistics64_data_t vmStats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const kern_return_t kr = ::host_statistics64(
        ::mach_host_self(),
        HOST_VM_INFO64,
        reinterpret_cast<host_info64_t>(&vmStats),
        &count
    );

    if (kr == KERN_SUCCESS) {
        const uint64_t pageSize    = static_cast<uint64_t>(::vm_page_size);
        const uint64_t availPages  = vmStats.free_count + vmStats.inactive_count;
        const uint64_t availBytes  = availPages * pageSize;
        info.availablePhysicalMB   = availBytes / kBytesPerMB;

        if (info.totalPhysicalMB > 0) {
            const uint64_t usedMB = (info.totalPhysicalMB > info.availablePhysicalMB)
                                    ? (info.totalPhysicalMB - info.availablePhysicalMB)
                                    : 0;
            info.usagePercent = static_cast<uint32_t>((usedMB * 100ULL) / info.totalPhysicalMB);
        }
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetOsInfo — uname() + kern.osproductversion
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] OsInfo GetOsInfo() {
    UZ_PROFILE_FUNCTION();

    OsInfo info{};

    // ── Architecture from uname ───────────────────────────────────────────────
    struct utsname uts{};
    if (::uname(&uts) == 0) {
        std::snprintf(info.architecture, sizeof(info.architecture), "%s", uts.machine);
    } else {
        std::snprintf(info.architecture, sizeof(info.architecture), "unknown");
    }

    // ── Product version from kern.osproductversion ───────────────────────────
    // Returns the user-visible string like "14.5" or "13.6.4" — more meaningful
    // than the Darwin kernel version (23.x) for diagnostics and support requests.
    char productVersion[32]{};
    if (SysctlString("kern.osproductversion", productVersion, sizeof(productVersion))) {
        std::snprintf(info.name, sizeof(info.name), "macOS %s", productVersion);

        unsigned int major = 0, minor = 0, patch = 0;
        std::sscanf(productVersion, "%u.%u.%u", &major, &minor, &patch);
        info.majorVersion = major;
        info.minorVersion = minor;
        info.buildNumber  = patch;
    } else {
        // kern.osproductversion unavailable (older macOS pre-10.13 or restricted sandbox)
        if (::uname(&uts) == 0) {
            std::snprintf(info.name, sizeof(info.name), "Darwin %s", uts.release);
            unsigned int major = 0, minor = 0, patch = 0;
            std::sscanf(uts.release, "%u.%u.%u", &major, &minor, &patch);
            info.majorVersion = major;
            info.minorVersion = minor;
            info.buildNumber  = patch;
        } else {
            std::snprintf(info.name, sizeof(info.name), "macOS (unknown version)");
        }
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetGpuInfo — Metal MTLCreateSystemDefaultDevice
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] GpuInfo GetGpuInfo() {
    UZ_PROFILE_FUNCTION();

    GpuInfo info{};

    // MTLCreateSystemDefaultDevice returns the preferred GPU for the current
    // process. On multi-GPU Macs (eGPU connected) this is the discrete card.
    // Wrapped in an @autoreleasepool so the retained Metal device is released
    // immediately after we copy the name — avoids leaking into an outer pool.
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            // Headless build target or no Metal-capable GPU present.
            return info;
        }

        // Copy adapter name into the fixed-size buffer.
        const char* nameUtf8 = [[device name] UTF8String];
        if (nameUtf8) {
            std::snprintf(info.name, sizeof(info.name), "%s", nameUtf8);
        }

        // recommendedMaxWorkingSetSize is the usable VRAM budget in bytes.
        // For integrated / unified-memory GPUs (Apple Silicon) this reflects a
        // portion of the unified pool, not a discrete VRAM bank.
        const uint64_t workingSetBytes = [device recommendedMaxWorkingSetSize];
        constexpr uint64_t kBytesPerMB = 1024ULL * 1024ULL;

        if ([device isLowPower]) {
            // Integrated GPU — report as shared memory (no discrete VRAM bank).
            info.sharedVRAM_MB    = workingSetBytes / kBytesPerMB;
            info.dedicatedVRAM_MB = 0;
        } else {
            // Discrete GPU — treat working set as dedicated VRAM.
            info.dedicatedVRAM_MB = workingSetBytes / kBytesPerMB;
            info.sharedVRAM_MB    = 0;
        }

        // PCI vendor/device IDs are not surfaced by the Metal API.
        // They would require IOKit IOPCIDevice enumeration, which adds complexity
        // for little diagnostic value — the name string is sufficient.
        info.vendorId = 0;
        info.deviceId = 0;
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetBatteryInfo — IOKit AppleSmartBattery registry
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] BatteryInfo GetBatteryInfo() {
    UZ_PROFILE_FUNCTION();

    BatteryInfo info{};
    info.percentage = 255; // Sentinel: unknown

    // The AppleSmartBattery service is only registered on portable Macs.
    // On Mac Pro / Mac mini / iMac it is absent, so hasBattery stays false.
    io_service_t service = ::IOServiceGetMatchingService(
        kIOMasterPortDefault,
        ::IOServiceMatching("AppleSmartBattery")
    );

    if (service == IO_OBJECT_NULL) {
        info.hasBattery = false;
        return info;
    }

    info.hasBattery = true;

    // ── CurrentCapacity (mAh or %) ────────────────────────────────────────────
    CFTypeRef currentRef = ::IORegistryEntryCreateCFProperty(
        service,
        CFSTR("CurrentCapacity"),
        kCFAllocatorDefault,
        0
    );
    uint64_t current = 0;
    if (currentRef && CFGetTypeID(currentRef) == CFNumberGetTypeID()) {
        CFNumberGetValue(static_cast<CFNumberRef>(currentRef), kCFNumberSInt64Type, &current);
        CFRelease(currentRef);
    }

    // ── MaxCapacity ───────────────────────────────────────────────────────────
    CFTypeRef maxRef = ::IORegistryEntryCreateCFProperty(
        service,
        CFSTR("MaxCapacity"),
        kCFAllocatorDefault,
        0
    );
    uint64_t maxCapacity = 0;
    if (maxRef && CFGetTypeID(maxRef) == CFNumberGetTypeID()) {
        CFNumberGetValue(static_cast<CFNumberRef>(maxRef), kCFNumberSInt64Type, &maxCapacity);
        CFRelease(maxRef);
    }

    // Derive percentage — guard against zero MaxCapacity to prevent divide-by-zero.
    if (maxCapacity > 0) {
        const uint64_t pct = (current * 100ULL) / maxCapacity;
        info.percentage = static_cast<uint8_t>(pct > 100 ? 100 : pct);
    }

    // ── IsCharging ────────────────────────────────────────────────────────────
    CFTypeRef chargingRef = ::IORegistryEntryCreateCFProperty(
        service,
        CFSTR("IsCharging"),
        kCFAllocatorDefault,
        0
    );
    if (chargingRef) {
        if (CFGetTypeID(chargingRef) == CFBooleanGetTypeID()) {
            info.isCharging = (chargingRef == kCFBooleanTrue);
        } else if (CFGetTypeID(chargingRef) == CFNumberGetTypeID()) {
            int32_t chargingInt = 0;
            CFNumberGetValue(static_cast<CFNumberRef>(chargingRef), kCFNumberSInt32Type, &chargingInt);
            info.isCharging = (chargingInt != 0);
        }
        CFRelease(chargingRef);
    }

    ::IOObjectRelease(service);
    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetLocaleInfo_ — setlocale + LANG env var (POSIX, same as Linux)
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] LocaleInfo GetLocaleInfo_() {
    UZ_PROFILE_FUNCTION();

    LocaleInfo info{};

    // setlocale(LC_ALL, "") reads the environment (LANG, LC_ALL, LC_CTYPE, …).
    // On macOS this returns strings like "en_US.UTF-8" or "C".
    const char* localeName = ::setlocale(LC_ALL, "");
    if (!localeName || localeName[0] == '\0' || std::strcmp(localeName, "C") == 0) {
        // Fall back to the LANG environment variable (common in Terminal sessions).
        localeName = ::getenv("LANG");
    }
    if (!localeName || localeName[0] == '\0') {
        std::snprintf(info.language, sizeof(info.language), "en");
        return info;
    }

    // Locale strings look like "en_US.UTF-8" or "pt_BR.UTF-8".
    // Copy the part before the dot as the language tag, replace '_' with '-'
    // for BCP-47 compatibility.
    char lang[32]{};
    std::snprintf(lang, sizeof(lang), "%s", localeName);

    char* dot = std::strchr(lang, '.');
    if (dot) *dot = '\0'; // Strip encoding suffix (e.g., ".UTF-8")

    for (char* p = lang; *p; ++p) {
        if (*p == '_') *p = '-'; // "en_US" -> "en-US"
    }

    std::snprintf(info.language, sizeof(info.language), "%s", lang);

    // Extract region subtag (e.g., "en-US" → region = "US").
    const char* hyphen = std::strchr(lang, '-');
    if (hyphen) {
        std::snprintf(info.region, sizeof(info.region), "%s", hyphen + 1);
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// LogSystemInfo — Log all system info at startup
// ─────────────────────────────────────────────────────────────────────────────

void LogSystemInfo() {
    UZ_PROFILE_FUNCTION();

    UZ_LOG(Platform, Info, "=== System Information ===");

    // --- CPU ---
    const auto cpu = GetCpuInfo();
    UZ_LOG(Platform, Info, "CPU: {}", cpu.brand);
    UZ_LOG(Platform, Info, "  Cores: {} physical, {} logical",
           cpu.physicalCores, cpu.logicalCores);
    UZ_LOG(Platform, Info, "  Cache: L1={}KB L2={}KB L3={}KB",
           cpu.l1CacheKB, cpu.l2CacheKB, cpu.l3CacheKB);
    UZ_LOG(Platform, Info, "  Features: SSE4.2={} AVX={} AVX2={} AVX-512={} FMA={} AES={}",
           cpu.hasSSE42, cpu.hasAVX, cpu.hasAVX2, cpu.hasAVX512, cpu.hasFMA, cpu.hasAESNI);

    // --- Memory ---
    const auto mem = GetMemoryInfo();
    UZ_LOG(Platform, Info, "Memory: {} MB total, {} MB available ({}% used)",
           mem.totalPhysicalMB, mem.availablePhysicalMB, mem.usagePercent);
    UZ_LOG(Platform, Info, "  Virtual (approx): {} MB", mem.totalVirtualMB);

    // --- OS ---
    const auto os = GetOsInfo();
    UZ_LOG(Platform, Info, "OS: {} ({}.{}.{}) [{}]",
           os.name, os.majorVersion, os.minorVersion, os.buildNumber, os.architecture);

    // --- GPU ---
    const auto gpu = GetGpuInfo();
    if (gpu.name[0] != '\0') {
        const uint64_t vramMB = (gpu.dedicatedVRAM_MB > 0)
                                ? gpu.dedicatedVRAM_MB
                                : gpu.sharedVRAM_MB;
        UZ_LOG(Platform, Info, "GPU: {} ({} VRAM: {} MB)",
               gpu.name,
               gpu.dedicatedVRAM_MB > 0 ? "dedicated" : "unified/shared",
               vramMB);
    } else {
        UZ_LOG(Platform, Info, "GPU: no Metal device found (headless or unavailable)");
    }

    // --- Battery ---
    const auto battery = GetBatteryInfo();
    if (battery.hasBattery) {
        UZ_LOG(Platform, Info, "Battery: {}% ({})",
               battery.percentage != 255 ? static_cast<int>(battery.percentage) : -1,
               battery.isCharging ? "charging" : "on battery");
    } else {
        UZ_LOG(Platform, Info, "Battery: not present (desktop Mac)");
    }

    // --- Locale ---
    const auto locale = GetLocaleInfo_();
    UZ_LOG(Platform, Info, "Locale: {} (region: {})",
           locale.language, locale.region[0] != '\0' ? locale.region : "N/A");

    UZ_LOG(Platform, Info, "=========================");
}

} // namespace uz

#endif // __APPLE__
