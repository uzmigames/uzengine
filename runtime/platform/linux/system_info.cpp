/// UzEngine System Information — Linux Implementation
///
/// Queries CPU, memory, OS, GPU, battery, and locale information using
/// /proc, /sys, uname(), CPUID intrinsics, and POSIX locale APIs.

#include "system_info.h"

#ifdef __linux__

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale.h>
#include <langinfo.h>
#include <set>
#include <string>

#include <cpuid.h>
#include <sys/utsname.h>
#include <unistd.h>

UZ_DECLARE_LOG_CATEGORY(Platform);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Trims leading and trailing whitespace (space, tab, newline) from a C string
/// in-place.
static void TrimInPlace(char* str) noexcept {
    char* start = str;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        ++start;
    }
    if (start != str) {
        std::memmove(str, start, std::strlen(start) + 1);
    }
    const size_t len = std::strlen(str);
    if (len == 0) return;
    char* end = str + len - 1;
    while (end >= str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        --end;
    }
}

/// Reads the first line of a sysfs file into `out` (up to outSize-1 chars).
/// Returns true on success, false if the file cannot be opened.
bool ReadSysfsLine(const char* path, char* out, size_t outSize) noexcept {
    FILE* f = ::fopen(path, "r");
    if (!f) return false;
    const bool ok = (::fgets(out, static_cast<int>(outSize), f) != nullptr);
    ::fclose(f);
    if (ok) {
        TrimInPlace(out);
    }
    return ok;
}

/// Reads an unsigned integer from a sysfs file. Returns 0 on failure.
uint64_t ReadSysfsUint(const char* path) noexcept {
    char buf[32]{};
    if (!ReadSysfsLine(path, buf, sizeof(buf))) return 0;
    return static_cast<uint64_t>(std::strtoull(buf, nullptr, 0));
}

/// Parses a cache size string like "32K", "256K", "8192K" into KB.
uint32_t ParseCacheSizeKB(const char* str) noexcept {
    char* end = nullptr;
    const unsigned long val = std::strtoul(str, &end, 10);
    if (!end) return 0;
    // Suffix: K = kilobytes, M = megabytes (rare but possible)
    if (*end == 'K' || *end == 'k') return static_cast<uint32_t>(val);
    if (*end == 'M' || *end == 'm') return static_cast<uint32_t>(val * 1024);
    return static_cast<uint32_t>(val); // No suffix — assume KB
}

/// Parses /proc/meminfo for a specific key like "MemTotal". Returns kB value.
uint64_t ParseMemInfoKB(const char* key) noexcept {
    FILE* f = ::fopen("/proc/meminfo", "r");
    if (!f) return 0;

    char line[128]{};
    const size_t keyLen = std::strlen(key);
    uint64_t result = 0;

    while (::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, key, keyLen) == 0 && line[keyLen] == ':') {
            // Format: "MemTotal:       16384000 kB"
            char* p = line + keyLen + 1;
            while (*p == ' ') ++p;
            result = std::strtoull(p, nullptr, 10);
            break;
        }
    }

    ::fclose(f);
    return result;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// GetCpuInfo — /proc/cpuinfo + sysfs cache + CPUID intrinsics
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] CpuInfo GetCpuInfo() {
    UZ_PROFILE_FUNCTION();

    CpuInfo info{};

    // ── Brand string and physical topology from /proc/cpuinfo ────────────────
    FILE* f = ::fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256]{};
        std::set<int> coreIds;    // unique physical core IDs
        bool brandFound = false;

        while (::fgets(line, sizeof(line), f)) {
            if (!brandFound && std::strncmp(line, "model name", 10) == 0) {
                const char* colon = std::strchr(line, ':');
                if (colon) {
                    std::snprintf(info.brand, sizeof(info.brand), "%s", colon + 2);
                    TrimInPlace(info.brand);
                    brandFound = true;
                }
            } else if (std::strncmp(line, "core id", 7) == 0) {
                const char* colon = std::strchr(line, ':');
                if (colon) {
                    coreIds.insert(static_cast<int>(std::strtol(colon + 1, nullptr, 10)));
                }
            }
        }
        ::fclose(f);

        if (!brandFound) {
            std::snprintf(info.brand, sizeof(info.brand), "Unknown CPU");
        }

        // Physical cores = distinct core IDs (may undercount on multi-socket, acceptable)
        info.physicalCores = static_cast<uint32_t>(coreIds.size());
    } else {
        std::snprintf(info.brand, sizeof(info.brand), "Unknown CPU");
    }

    // ── Logical core count ───────────────────────────────────────────────────
    const long nproc = ::sysconf(_SC_NPROCESSORS_ONLN);
    info.logicalCores = (nproc > 0) ? static_cast<uint32_t>(nproc) : 1u;

    // Fallback if /proc/cpuinfo had no core id entries
    if (info.physicalCores == 0) {
        info.physicalCores = info.logicalCores;
    }

    // ── Cache sizes from sysfs ───────────────────────────────────────────────
    // Walk /sys/devices/system/cpu/cpu0/cache/index{0,1,2,3}/
    for (int idx = 0; idx < 8; ++idx) {
        char levelPath[128]{};
        char typePath[128]{};
        char sizePath[128]{};

        std::snprintf(levelPath, sizeof(levelPath),
                      "/sys/devices/system/cpu/cpu0/cache/index%d/level", idx);
        std::snprintf(typePath, sizeof(typePath),
                      "/sys/devices/system/cpu/cpu0/cache/index%d/type", idx);
        std::snprintf(sizePath, sizeof(sizePath),
                      "/sys/devices/system/cpu/cpu0/cache/index%d/size", idx);

        char levelStr[8]{};
        if (!ReadSysfsLine(levelPath, levelStr, sizeof(levelStr))) break;

        char typeStr[16]{};
        ReadSysfsLine(typePath, typeStr, sizeof(typeStr));

        char sizeStr[16]{};
        if (!ReadSysfsLine(sizePath, sizeStr, sizeof(sizeStr))) continue;

        const int level = static_cast<int>(std::strtol(levelStr, nullptr, 10));
        const uint32_t sizeKB = ParseCacheSizeKB(sizeStr);

        // Only record data or unified caches (skip instruction-only)
        const bool isData = (std::strcmp(typeStr, "Data") == 0 ||
                             std::strcmp(typeStr, "Unified") == 0);
        if (!isData) continue;

        switch (level) {
            case 1: info.l1CacheKB = sizeKB; break;
            case 2: info.l2CacheKB = sizeKB; break;
            case 3: info.l3CacheKB = sizeKB; break;
            default: break;
        }
    }

    // ── Feature flags via CPUID intrinsics ───────────────────────────────────
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

    // Leaf 1: ECX feature bits
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        info.hasSSE42 = (ecx & (1u << 20)) != 0;
        info.hasAVX   = (ecx & (1u << 28)) != 0;
        info.hasFMA   = (ecx & (1u << 12)) != 0;
        info.hasAESNI = (ecx & (1u << 25)) != 0;
    }

    // Leaf 7, sub-leaf 0: EBX feature bits (AVX2, AVX-512F)
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    info.hasAVX2   = (ebx & (1u << 5))  != 0;
    info.hasAVX512 = (ebx & (1u << 16)) != 0; // AVX-512F

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetMemoryInfo — /proc/meminfo
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] MemoryInfo GetMemoryInfo() {
    UZ_PROFILE_FUNCTION();

    MemoryInfo info{};

    constexpr uint64_t kKBPerMB = 1024ULL;

    const uint64_t totalKB     = ParseMemInfoKB("MemTotal");
    const uint64_t availableKB = ParseMemInfoKB("MemAvailable");
    const uint64_t commitKB    = ParseMemInfoKB("CommitLimit");

    info.totalPhysicalMB     = totalKB / kKBPerMB;
    info.availablePhysicalMB = availableKB / kKBPerMB;
    info.totalVirtualMB      = commitKB / kKBPerMB;

    if (info.totalPhysicalMB > 0) {
        const uint64_t usedKB = (totalKB > availableKB) ? (totalKB - availableKB) : 0;
        info.usagePercent = static_cast<uint32_t>((usedKB * 100ULL) / totalKB);
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetOsInfo — uname()
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] OsInfo GetOsInfo() {
    UZ_PROFILE_FUNCTION();

    OsInfo info{};

    struct utsname uts {};
    if (::uname(&uts) != 0) {
        std::snprintf(info.name, sizeof(info.name), "Linux (unknown version)");
        std::snprintf(info.architecture, sizeof(info.architecture), "unknown");
        return info;
    }

    // Full name: sysname + release
    std::snprintf(info.name, sizeof(info.name), "%s %s", uts.sysname, uts.release);

    // Parse version from release string: "6.8.0-51-generic" -> 6, 8, 0
    unsigned int major = 0, minor = 0, patch = 0;
    std::sscanf(uts.release, "%u.%u.%u", &major, &minor, &patch);
    info.majorVersion = major;
    info.minorVersion = minor;
    info.buildNumber  = patch;

    std::snprintf(info.architecture, sizeof(info.architecture), "%s", uts.machine);

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetGpuInfo — /sys/class/drm/card0/device/
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] GpuInfo GetGpuInfo() {
    UZ_PROFILE_FUNCTION();

    GpuInfo info{};

    // Read PCI vendor and device IDs from sysfs (hex strings like "0x10de")
    char vendorStr[16]{};
    char deviceStr[16]{};

    if (ReadSysfsLine("/sys/class/drm/card0/device/vendor", vendorStr, sizeof(vendorStr))) {
        info.vendorId = static_cast<uint32_t>(std::strtoul(vendorStr, nullptr, 0));
    }
    if (ReadSysfsLine("/sys/class/drm/card0/device/device", deviceStr, sizeof(deviceStr))) {
        info.deviceId = static_cast<uint32_t>(std::strtoul(deviceStr, nullptr, 0));
    }

    // Map well-known vendor IDs to a short name for the log (not stored in name[])
    // The GPU name itself requires Vulkan or NVML — out of scope for the platform layer.
    if (info.vendorId == 0x10DEu) {
        std::snprintf(info.name, sizeof(info.name), "NVIDIA GPU (device 0x%04X)", info.deviceId);
    } else if (info.vendorId == 0x1002u) {
        std::snprintf(info.name, sizeof(info.name), "AMD GPU (device 0x%04X)", info.deviceId);
    } else if (info.vendorId == 0x8086u) {
        std::snprintf(info.name, sizeof(info.name), "Intel GPU (device 0x%04X)", info.deviceId);
    } else if (info.vendorId != 0) {
        std::snprintf(info.name, sizeof(info.name), "GPU (vendor 0x%04X, device 0x%04X)",
                      info.vendorId, info.deviceId);
    }
    // vendorId == 0: headless / no DRM node — leave name empty

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetBatteryInfo — /sys/class/power_supply/
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] BatteryInfo GetBatteryInfo() {
    UZ_PROFILE_FUNCTION();

    BatteryInfo info{};
    info.percentage = 255; // Unknown by default

    // Detect battery presence
    FILE* testFile = ::fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!testFile) {
        // No BAT0 — desktop or battery named differently
        info.hasBattery = false;
        return info;
    }
    ::fclose(testFile);
    info.hasBattery = true;

    // AC adapter online status
    const uint64_t acOnline = ReadSysfsUint("/sys/class/power_supply/AC/online");
    info.isCharging = (acOnline == 1);

    // Capacity percentage
    const uint64_t capacity = ReadSysfsUint("/sys/class/power_supply/BAT0/capacity");
    if (capacity <= 100) {
        info.percentage = static_cast<uint8_t>(capacity);
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetLocaleInfo_ — setlocale + LANG env var
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] LocaleInfo GetLocaleInfo_() {
    UZ_PROFILE_FUNCTION();

    LocaleInfo info{};

    // setlocale(LC_ALL, "") reads the environment (LANG, LC_ALL, etc.)
    const char* localeName = ::setlocale(LC_ALL, "");
    if (!localeName || localeName[0] == '\0') {
        // Fall back to LANG env var
        localeName = ::getenv("LANG");
    }
    if (!localeName || localeName[0] == '\0') {
        std::snprintf(info.language, sizeof(info.language), "en");
        return info;
    }

    // locale strings look like "en_US.UTF-8" or "pt_BR.UTF-8"
    // Copy the part before the dot as the language tag, replace '_' with '-'
    char lang[32]{};
    std::snprintf(lang, sizeof(lang), "%s", localeName);

    // Truncate at '.' (encoding suffix)
    char* dot = std::strchr(lang, '.');
    if (dot) *dot = '\0';

    // Replace underscore with hyphen for BCP-47 style
    for (char* p = lang; *p; ++p) {
        if (*p == '_') *p = '-';
    }

    std::snprintf(info.language, sizeof(info.language), "%s", lang);

    // Extract region after the hyphen (e.g., "en-US" -> "US")
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
    UZ_LOG(Platform, Info, "  Features: SSE4.2={} AVX={} AVX2={} AVX-512={} FMA={} AES-NI={}",
           cpu.hasSSE42, cpu.hasAVX, cpu.hasAVX2, cpu.hasAVX512, cpu.hasFMA, cpu.hasAESNI);

    // --- Memory ---
    const auto mem = GetMemoryInfo();
    UZ_LOG(Platform, Info, "Memory: {} MB total, {} MB available ({}% used)",
           mem.totalPhysicalMB, mem.availablePhysicalMB, mem.usagePercent);
    UZ_LOG(Platform, Info, "  Virtual (CommitLimit): {} MB", mem.totalVirtualMB);

    // --- OS ---
    const auto os = GetOsInfo();
    UZ_LOG(Platform, Info, "OS: {} ({}.{}.{}) [{}]",
           os.name, os.majorVersion, os.minorVersion, os.buildNumber, os.architecture);

    // --- GPU ---
    const auto gpu = GetGpuInfo();
    if (gpu.name[0] != '\0') {
        UZ_LOG(Platform, Info, "GPU: {} (VendorID=0x{:04X}, DeviceID=0x{:04X})",
               gpu.name, gpu.vendorId, gpu.deviceId);
    } else {
        UZ_LOG(Platform, Info, "GPU: no DRM adapter found (headless or no /sys/class/drm/card0)");
    }

    // --- Battery ---
    const auto battery = GetBatteryInfo();
    if (battery.hasBattery) {
        UZ_LOG(Platform, Info, "Battery: {}% ({})",
               battery.percentage != 255 ? static_cast<int>(battery.percentage) : -1,
               battery.isCharging ? "charging" : "on battery");
    } else {
        UZ_LOG(Platform, Info, "Battery: not present (desktop)");
    }

    // --- Locale ---
    const auto locale = GetLocaleInfo_();
    UZ_LOG(Platform, Info, "Locale: {} (region: {})",
           locale.language, locale.region[0] != '\0' ? locale.region : "N/A");

    UZ_LOG(Platform, Info, "=========================");
}

} // namespace uz

#endif // __linux__
