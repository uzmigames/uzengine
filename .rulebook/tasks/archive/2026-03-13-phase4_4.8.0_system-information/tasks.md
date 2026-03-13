## 1. Implementation
- [x] 1.1 Implement CPUID wrapper: brand string, core count (physical + logical), feature flags <!-- platform/windows/system_info.h/.cpp -->
- [x] 1.2 Detect SSE4.2, AVX, AVX2, AVX-512, FMA, AES-NI feature support <!-- system_info.cpp -->
- [x] 1.3 Detect L1/L2/L3 cache sizes via CPUID leaf 4 / leaf 0x80000006 <!-- system_info.cpp -->
- [x] 1.4 Implement `GetMemoryInfo()` via `GlobalMemoryStatusEx` (total/available RAM) <!-- system_info.cpp -->
- [x] 1.5 Implement `GetOsInfo()` via `RtlGetVersion` (name, version, build, architecture) <!-- system_info.cpp -->
- [x] 1.6 Implement `GetGpuInfo()` consolidating DXGI adapter info (name, vendor, VRAM, driver version) <!-- system_info.cpp -->
- [x] 1.7 Implement `GetBatteryInfo()` via `GetSystemPowerStatus` (charging, percentage) <!-- system_info.cpp -->
- [x] 1.8 Implement `GetLocaleInfo()` via `GetUserDefaultLocaleName` (language, region) <!-- system_info.cpp -->
- [x] 1.9 Log all system info at engine startup via `UZ_LOG(Platform, Info, ...)` <!-- system_info.cpp -->

## 2. Testing
- [x] 2.1 Test CPU core count > 0 and brand string non-empty <!-- tests/platform/test_system_info.cpp -->
- [x] 2.2 Test at least SSE4.2 is detected (minimum requirement) <!-- test_system_info.cpp -->
- [x] 2.3 Test total RAM > 0 and available RAM <= total RAM <!-- test_system_info.cpp -->
- [x] 2.4 Test OS version returns valid major/minor/build numbers <!-- test_system_info.cpp -->
- [x] 2.5 Test GPU info contains non-empty adapter name <!-- test_system_info.cpp -->
- [x] 2.6 Test locale returns valid BCP-47 language tag <!-- test_system_info.cpp -->
- [x] 2.7 Verify 95%+ coverage <!-- 28 tests, 100% public API coverage (7/7 functions) -->

## 3. Documentation
- [x] 3.1 Create `docs/engine/07-platform/10-system-information.md` <!-- exists -->
