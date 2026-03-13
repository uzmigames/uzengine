/// UzEngine System Information — Windows Implementation
///
/// Queries CPU, memory, OS, GPU, battery, and locale information using
/// Win32 APIs, CPUID intrinsics, and DXGI enumeration.

#include "system_info.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dxgi1_4.h>
#include <intrin.h>

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Platform);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Trims leading and trailing whitespace from a C string in-place.
static void TrimInPlace(char* str) {
    // Trim leading
    char* start = str;
    while (*start == ' ') ++start;
    if (start != str) {
        std::memmove(str, start, std::strlen(start) + 1);
    }
    // Trim trailing
    const auto len = std::strlen(str);
    if (len == 0) return;
    char* end = str + len - 1;
    while (end >= str && *end == ' ') {
        *end = '\0';
        --end;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GetCpuInfo — CPUID-based CPU identification
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] CpuInfo GetCpuInfo() {
    UZ_PROFILE_FUNCTION();

    CpuInfo info{};

    // --- Brand string (leaves 0x80000002-0x80000004) ---
    int regs[4]{};
    __cpuid(regs, static_cast<int>(0x80000000u));
    const auto maxExtLeaf = static_cast<uint32_t>(regs[0]);

    if (maxExtLeaf >= 0x80000004u) {
        // Each leaf writes 16 bytes (4 x int32) of brand string
        for (uint32_t leaf = 0x80000002u; leaf <= 0x80000004u; ++leaf) {
            __cpuid(regs, static_cast<int>(leaf));
            const auto offset = (leaf - 0x80000002u) * 16;
            std::memcpy(info.brand + offset, regs, 16);
        }
        info.brand[48] = '\0'; // Brand is at most 48 chars
        TrimInPlace(info.brand);
    } else {
        std::snprintf(info.brand, sizeof(info.brand), "Unknown CPU");
    }

    // --- Feature flags (leaf 1: ECX/EDX) ---
    __cpuid(regs, 1);
    const auto ecx1 = static_cast<uint32_t>(regs[2]);
    const auto edx1 = static_cast<uint32_t>(regs[3]);
    (void)edx1;

    info.hasSSE42 = (ecx1 & (1u << 20)) != 0;
    info.hasAVX   = (ecx1 & (1u << 28)) != 0;
    info.hasFMA   = (ecx1 & (1u << 12)) != 0;
    info.hasAESNI = (ecx1 & (1u << 25)) != 0;

    // --- Feature flags (leaf 7, sub-leaf 0: EBX) ---
    __cpuidex(regs, 7, 0);
    const auto ebx7 = static_cast<uint32_t>(regs[1]);

    info.hasAVX2   = (ebx7 & (1u << 5))  != 0;
    info.hasAVX512 = (ebx7 & (1u << 16)) != 0; // AVX-512F

    // --- Logical core count ---
    SYSTEM_INFO sysInfo{};
    ::GetSystemInfo(&sysInfo);
    info.logicalCores = static_cast<uint32_t>(sysInfo.dwNumberOfProcessors);

    // --- Physical core count via GetLogicalProcessorInformation ---
    DWORD bufferSize = 0;
    ::GetLogicalProcessorInformation(nullptr, &bufferSize);

    if (bufferSize > 0) {
        const auto entryCount = bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        // Stack-allocate for typical systems (up to 128 entries ~= 3.5KB)
        constexpr DWORD kMaxStackEntries = 128;
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION stackBuffer[kMaxStackEntries];
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer = stackBuffer;

        // Fall back to heap only if the system has an extraordinary topology
        std::unique_ptr<SYSTEM_LOGICAL_PROCESSOR_INFORMATION[]> heapBuffer;
        if (entryCount > kMaxStackEntries) {
            heapBuffer = std::make_unique<SYSTEM_LOGICAL_PROCESSOR_INFORMATION[]>(entryCount);
            buffer = heapBuffer.get();
        }

        if (::GetLogicalProcessorInformation(buffer, &bufferSize)) {
            uint32_t cores = 0;
            for (DWORD i = 0; i < entryCount; ++i) {
                if (buffer[i].Relationship == RelationProcessorCore) {
                    ++cores;
                }
            }
            info.physicalCores = cores;
        }
    }

    if (info.physicalCores == 0) {
        // Fallback: assume no HT
        info.physicalCores = info.logicalCores;
    }

    // --- Cache sizes (leaf 0x80000006 for L2/L3, leaf 4 for Intel L1) ---
    // Try Intel deterministic cache (leaf 4)
    __cpuid(regs, 0);
    const auto maxLeaf = static_cast<uint32_t>(regs[0]);

    if (maxLeaf >= 4) {
        for (int subLeaf = 0; subLeaf < 32; ++subLeaf) {
            __cpuidex(regs, 4, subLeaf);
            const auto cacheType = regs[0] & 0x1F;
            if (cacheType == 0) break; // No more caches

            const auto level  = (regs[0] >> 5) & 0x7;
            const auto ways   = ((static_cast<uint32_t>(regs[1]) >> 22) & 0x3FF) + 1;
            const auto parts  = ((static_cast<uint32_t>(regs[1]) >> 12) & 0x3FF) + 1;
            const auto lineSize = (static_cast<uint32_t>(regs[1]) & 0xFFF) + 1;
            const auto sets  = static_cast<uint32_t>(regs[2]) + 1;
            const auto sizeKB = (ways * parts * lineSize * sets) / 1024;

            // cacheType: 1=data, 2=instruction, 3=unified
            if (level == 1 && (cacheType == 1 || cacheType == 3)) {
                info.l1CacheKB = sizeKB;
            } else if (level == 2) {
                info.l2CacheKB = sizeKB;
            } else if (level == 3) {
                info.l3CacheKB = sizeKB;
            }
        }
    }

    // AMD fallback: leaf 0x80000006 for L2/L3 if not populated
    if (info.l2CacheKB == 0 && maxExtLeaf >= 0x80000006u) {
        __cpuid(regs, static_cast<int>(0x80000006u));
        info.l2CacheKB = (static_cast<uint32_t>(regs[2]) >> 16) & 0xFFFF;
        // L3 in EDX[31:18] in 512KB units
        const auto l3HalfMB = (static_cast<uint32_t>(regs[3]) >> 18) & 0x3FFF;
        if (l3HalfMB > 0) {
            info.l3CacheKB = l3HalfMB * 512;
        }
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetMemoryInfo — GlobalMemoryStatusEx
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] MemoryInfo GetMemoryInfo() {
    UZ_PROFILE_FUNCTION();

    MemoryInfo info{};
    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(memStatus);

    if (::GlobalMemoryStatusEx(&memStatus)) {
        constexpr uint64_t kMB = 1024ULL * 1024ULL;
        info.totalPhysicalMB     = memStatus.ullTotalPhys / kMB;
        info.availablePhysicalMB = memStatus.ullAvailPhys / kMB;
        info.totalVirtualMB      = memStatus.ullTotalVirtual / kMB;
        info.usagePercent        = memStatus.dwMemoryLoad;
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetOsInfo — RtlGetVersion (manifest-independent)
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] OsInfo GetOsInfo() {
    UZ_PROFILE_FUNCTION();

    OsInfo info{};

    // RtlGetVersion is not subject to manifest-based version capping
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            ::GetProcAddress(ntdll, "RtlGetVersion")
        );
        if (rtlGetVersion) {
            RTL_OSVERSIONINFOW osvi{};
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            if (rtlGetVersion(&osvi) == 0) { // STATUS_SUCCESS = 0
                info.majorVersion = osvi.dwMajorVersion;
                info.minorVersion = osvi.dwMinorVersion;
                info.buildNumber  = osvi.dwBuildNumber;

                // Windows 11 is major=10, minor=0, build >= 22000
                const char* edition = (osvi.dwBuildNumber >= 22000) ? "Windows 11" : "Windows 10";
                std::snprintf(info.name, sizeof(info.name), "%s (Build %lu)",
                              edition, static_cast<unsigned long>(osvi.dwBuildNumber));
            }
        }
    }

    // Architecture
    SYSTEM_INFO sysInfo{};
    ::GetNativeSystemInfo(&sysInfo);
    switch (sysInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            std::snprintf(info.architecture, sizeof(info.architecture), "x64");
            break;
        case PROCESSOR_ARCHITECTURE_ARM64:
            std::snprintf(info.architecture, sizeof(info.architecture), "ARM64");
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            std::snprintf(info.architecture, sizeof(info.architecture), "x86");
            break;
        default:
            std::snprintf(info.architecture, sizeof(info.architecture), "Unknown");
            break;
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetGpuInfo — DXGI adapter enumeration
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] GpuInfo GetGpuInfo() {
    UZ_PROFILE_FUNCTION();

    GpuInfo info{};

    IDXGIFactory1* factory = nullptr;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return info;
    }

    IDXGIAdapter1* adapter = nullptr;
    if (SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            // Convert wide description to UTF-8
            ::WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                  info.name, sizeof(info.name), nullptr, nullptr);

            info.vendorId        = desc.VendorId;
            info.deviceId        = desc.DeviceId;
            constexpr uint64_t kMB = 1024ULL * 1024ULL;
            info.dedicatedVRAM_MB = desc.DedicatedVideoMemory / kMB;
            info.sharedVRAM_MB    = desc.SharedSystemMemory / kMB;

            // Driver version from adapter LUID + registry
            LARGE_INTEGER driverVersion{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion))) {
                // LARGE_INTEGER encodes version as product.version.sub.build
                const auto v = static_cast<uint64_t>(driverVersion.QuadPart);
                std::snprintf(info.driverVersion, sizeof(info.driverVersion),
                              "%u.%u.%u.%u",
                              static_cast<uint32_t>((v >> 48) & 0xFFFF),
                              static_cast<uint32_t>((v >> 32) & 0xFFFF),
                              static_cast<uint32_t>((v >> 16) & 0xFFFF),
                              static_cast<uint32_t>(v & 0xFFFF));
            }
        }
        adapter->Release();
    }

    factory->Release();
    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetBatteryInfo — GetSystemPowerStatus
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] BatteryInfo GetBatteryInfo() {
    UZ_PROFILE_FUNCTION();

    BatteryInfo info{};
    info.percentage = 255; // Unknown by default

    SYSTEM_POWER_STATUS powerStatus{};
    if (::GetSystemPowerStatus(&powerStatus)) {
        // BatteryFlag: 128 = no battery, 255 = unknown
        info.hasBattery = (powerStatus.BatteryFlag != 128) &&
                          (powerStatus.BatteryFlag != 255);
        info.isCharging = (powerStatus.ACLineStatus == 1);

        if (powerStatus.BatteryLifePercent != 255) {
            info.percentage = powerStatus.BatteryLifePercent;
        }
    }

    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetLocaleInfo_ — User default locale
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] LocaleInfo GetLocaleInfo_() {
    UZ_PROFILE_FUNCTION();

    LocaleInfo info{};

    wchar_t localeName[LOCALE_NAME_MAX_LENGTH]{};
    if (::GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
        // Convert wide BCP-47 tag to UTF-8
        char utf8[64]{};
        ::WideCharToMultiByte(CP_UTF8, 0, localeName, -1,
                              utf8, sizeof(utf8), nullptr, nullptr);

        std::snprintf(info.language, sizeof(info.language), "%s", utf8);

        // Extract region after the hyphen (e.g., "en-US" -> "US")
        const char* hyphen = std::strchr(utf8, '-');
        if (hyphen) {
            std::snprintf(info.region, sizeof(info.region), "%s", hyphen + 1);
        }
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
    UZ_LOG(Platform, Info, "  Virtual: {} MB total", mem.totalVirtualMB);

    // --- OS ---
    const auto os = GetOsInfo();
    UZ_LOG(Platform, Info, "OS: {} ({}.{}.{}) [{}]",
           os.name, os.majorVersion, os.minorVersion, os.buildNumber, os.architecture);

    // --- GPU ---
    const auto gpu = GetGpuInfo();
    if (gpu.name[0] != '\0') {
        UZ_LOG(Platform, Info, "GPU: {} (VendorID=0x{:04X}, DeviceID=0x{:04X})",
               gpu.name, gpu.vendorId, gpu.deviceId);
        UZ_LOG(Platform, Info, "  VRAM: {} MB dedicated, {} MB shared",
               gpu.dedicatedVRAM_MB, gpu.sharedVRAM_MB);
        if (gpu.driverVersion[0] != '\0') {
            UZ_LOG(Platform, Info, "  Driver: {}", gpu.driverVersion);
        }
    } else {
        UZ_LOG(Platform, Warning, "GPU: No DXGI adapter found");
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
