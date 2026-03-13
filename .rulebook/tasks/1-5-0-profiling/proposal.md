# Proposal: 1.5.0 — Profiling & Instrumentation

## Why

The engine has profiling infrastructure (Tracy macros, GPU timestamp profiler, stat counters) but
**nothing is connected**. Zero functions are instrumented, Tracy isn't linked, GPU profiler isn't
wired into the render loop, and stat counters are never incremented. We cannot identify performance
bottlenecks — CPU or GPU — without full instrumentation.

Additionally, since UzEngine runs inside VSCode, we need `.cpuprofile` export (Chrome DevTools /
VSCode Flame Chart format) for native profiling integration without external tools.

## What Changes

1. **Tracy integration**: Link Tracy, define `UZ_PROFILING_ENABLED`, instrument all engine systems
2. **GPU profiler wiring**: Wire `DX12GpuProfiler` into the render loop (BeginFrame/EndFrame/ResolveFrame)
3. **Stat counter activation**: Increment draw calls, triangles, buffer uploads, shader compiles, cache hits, VFS reads
4. **`.cpuprofile` export**: Generate Chrome DevTools / VSCode-compatible `.cpuprofile` JSON from Tracy or standalone capture
5. **GPU timing overlay**: Optional on-screen or log-based GPU pass timing report
6. **Instrument all systems**: RHI, ShaderPipeline, VFS, AssetCache, Window, CommandList, SwapChain

## Impact
- Affected specs: profiler.h, dx12_gpu_profiler.h, build.zig (Tracy linkage)
- Affected code: All core systems (instrumentation macros added), triangle_demo (GPU profiler wiring)
- Breaking change: NO (macros are no-op when disabled)
- User benefit: Full CPU+GPU performance visibility, VSCode-native flame chart debugging
