# Proposal: System Information (Phase 4.8.0)

## Why
The engine needs CPU/GPU/OS detection for: automatic quality presets, crash context, telemetry, and feature-gating (e.g., mesh shaders require specific GPU). Currently, basic GPU adapter info comes from RHI but CPU features, RAM, OS version, and locale are unknown. UE5's `FPlatformMisc` provides comprehensive system detection via CPUID + WMI; Godot has minimal detection. This is P2 — useful immediately for crash reports and quality auto-detection.

## What Changes

### New Systems
1. **CpuInfo** — core count (physical + logical), brand string, feature flags (SSE4.2, AVX2, AVX-512), L1/L2/L3 cache sizes via CPUID
2. **MemoryInfo** — total/available physical RAM, total/available virtual memory
3. **OsInfo** — OS name, version, build number, architecture (x64/ARM64)
4. **GpuInfo** — adapter name, vendor, driver version, VRAM (extends RHI adapter info to system level)
5. **BatteryInfo** — plugged in / on battery, charge percentage (for laptop power management)
6. **LocaleInfo** — system language, region, date/number format

## Impact
- Affected specs: `docs/engine/07-platform/`
- Affected code: `runtime/platform/windows/`
- Breaking change: NO — purely additive
- User benefit: Auto quality presets, rich crash reports, locale-aware formatting
- Enables: Quality auto-detection, crash context (4.5.0), localization (23)
