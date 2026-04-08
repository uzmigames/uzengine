# Proposal: phase1_pixel-debug-system

## Why
LLM agents currently debug rendering bugs by editing shader source code to add
`return float4(value,0,0,1)` debug outputs, then rebuilding (60s), capturing a
screenshot, guessing at the problem, and repeating 3-10 times per issue.

NVIDIA Falcor's PixelDebug pattern solves this: shaders write debug values to a
structured buffer via `InterlockedAdd`, the CPU reads them back, and outputs
structured JSON — all without modifying shader source or rebuilding.

This is the single highest-impact improvement for rendering development velocity.

## What Changes
1. New HLSL include `pixel_debug.hlsli` — shader-side `PDEBUG()` macros
2. New C++ `PixelDebugSystem` class — GPU buffer management + readback + JSON output
3. New CLI flag `--pixel-debug <x> <y>` — triggers debug mode for a specific pixel
4. Integration into deferred lighting shader as first consumer
5. Permanent debug instrumentation in key shaders (always compiled in, zero-cost when not active)

## Impact
- Affected specs: none (new system, no existing spec changes)
- Affected code: `runtime/engine/renderer/passes/debug/`, `runtime/shaders/runtime/include/`, `runtime/engine/app/`
- Breaking change: NO
- User benefit: 10x faster rendering bug diagnosis — one CLI command instead of edit-rebuild-screenshot cycles
