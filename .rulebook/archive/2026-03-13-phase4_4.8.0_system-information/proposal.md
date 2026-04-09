# System Information

## Why

The engine needs to query hardware capabilities at startup for feature detection (AVX/SSE), GPU adapter selection, memory budgeting, and diagnostic logging. Without this, the engine cannot adapt to different hardware configurations.

## Summary

Implement system information queries: CPUID (brand, cores, cache, ISA features), GlobalMemoryStatusEx (RAM), RtlGetVersion (OS), DXGI adapter enumeration (GPU), GetSystemPowerStatus (battery), and GetUserDefaultLocaleName (locale).

## Impact

- **Startup**: LogSystemInfo() prints hardware summary
- **Renderer**: GPU adapter selection and VRAM budget
- **SIMD**: Feature detection gates AVX2/SSE4.2 code paths

## Acceptance Criteria

1. CPU info returns valid core count, brand string, and feature flags
2. Memory info returns total/available RAM
3. GPU info returns adapter name and VRAM
4. All 28 tests pass with 95%+ coverage
