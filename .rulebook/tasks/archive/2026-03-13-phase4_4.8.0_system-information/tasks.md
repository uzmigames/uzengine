## 1. Implementation
- [x] 1.1 Implement CPUID wrapper: brand string, core count (physical + logical), feature flags <!-- system_info.h/.cpp -->
- [x] 1.2 Detect SSE4.2, AVX, AVX2, AVX-512, FMA, AES-NI feature support
- [x] 1.3 Detect L1/L2/L3 cache sizes via CPUID
- [x] 1.4 Implement `GetMemoryInfo()` via `GlobalMemoryStatusEx`
- [x] 1.5 Implement `GetOsInfo()` via `RtlGetVersion`
- [x] 1.6 Implement `GetGpuInfo()` via DXGI adapter enumeration
- [x] 1.7 Implement `GetBatteryInfo()` via `GetSystemPowerStatus`
- [x] 1.8 Implement `GetLocaleInfo_()` via `GetUserDefaultLocaleName`
- [x] 1.9 `LogSystemInfo()` logs all system info at startup

## 2. Testing
- [x] 2.1 Test CPU core count > 0 and brand string non-empty
- [x] 2.2 Test at least SSE4.2 is detected
- [x] 2.3 Test total RAM > 0 and available RAM <= total RAM
- [x] 2.4 Test OS version returns valid major/minor/build numbers
- [x] 2.5 Test GPU info contains non-empty adapter name
- [x] 2.6 Test locale returns valid language tag
- [x] 2.7 Verify 95%+ coverage on all new code <!-- 28 tests covering CPU, memory, OS, GPU, battery, locale, cache, feature detection -->
- [x] 2.8 Build succeeds (zig build — 0 errors)

## 3. Documentation
- [x] 3.1 Create `docs/engine/07-platform/10-system-information.md`
