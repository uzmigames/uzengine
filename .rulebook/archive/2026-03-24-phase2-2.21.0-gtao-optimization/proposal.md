# Proposal: phase2-2.21.0-gtao-optimization

## Priority: P0 — MAXIMUM

## Why

GTAO currently consumes **1.23ms GPU** (56% of total frame GPU time) for a trivial 9-sphere PBR test scene at 1920×1080. This is the single largest GPU bottleneck preventing the engine from reaching 400+ FPS on simple scenes. The implementation runs full-resolution with no temporal reuse, no Hi-Z acceleration, and no half-res option — despite having shader code for temporal accumulation and bilateral upsample already written but not integrated.

**GPU Profile (frame 20, 1920×1080, DX12):**
- GTAO: 1.23ms (56% of 2.2ms total)
- SSR_Trace: 0.23ms
- SkyAtmosphere: 0.25ms
- DeferredLighting: 0.14ms
- Everything else: 0.35ms

**Target:** Reduce GTAO from 1.23ms to <0.35ms (4× improvement) without visible quality loss.

## Root Cause Analysis

1. **Full-res only**: 2M pixels processed every frame at 1080p. No half-res dispatch option.
2. **21 texture reads/pixel**: 5 for depth-reconstructed normals + 16 for horizon search (2 dirs × 8 steps).
3. **No temporal reuse**: Recomputes AO from scratch every frame. `gtao_temporal_cs.hlsl` exists (339 lines, complete velocity-based reprojection with variance clipping) but is NOT integrated into `GTAOPass::Execute()`.
4. **Hi-Z disabled**: `GTAO_USE_HIZ` flag exists in shader but is off — always samples depth mip 0.
5. **No groupshared preload**: Each thread independently fetches overlapping depth neighborhoods.
6. **Hardcoded quality**: `GTAO_NUM_ANGLES=2`, `GTAO_NUM_TAPS=8` are #defines — no runtime quality presets.
7. **GTAOSettings dead code**: `GTAOSettings` struct exists in .h with worldRadius/intensity/etc but is NEVER sent to the GPU. Shader uses hardcoded values.

## What Changes

### Phase 1: Half-Resolution Pipeline (biggest win — 4× fewer pixels)
- Dispatch GTAO compute at half-res (960×540 at 1080p)
- Integrate the existing `gtao_upsample_ps.hlsl` bilateral upsample shader
- Add half-res texture allocation in GTAOPass::Init()

### Phase 2: Temporal Accumulation (skip redundant work)
- Integrate the existing `gtao_temporal_cs.hlsl` into GTAOPass::Execute()
- Wire velocity buffer + history AO texture (ping-pong)
- 90/10 history/current blend = massive noise reduction + allows lower sample count

### Phase 3: Hi-Z Acceleration (fewer texture fetches)
- Enable `GTAO_USE_HIZ` path in gtao_cs.hlsl
- Feed Hi-Z pyramid SRV to GTAO constant buffer
- Distant steps sample higher mips → fewer cache misses

### Phase 4: Dynamic Quality System
- Bind GTAOSettings to GPU via root constants (replace hardcoded #defines)
- Add quality presets: Low (1 angle, 4 taps, half-res), Medium (2 angles, 6 taps, half-res), High (2 angles, 8 taps, full-res), Ultra (3 angles, 12 taps, full-res)
- Runtime quality switching via console command

### Phase 5: Shader Micro-Optimizations
- Groupshared depth preload (8×8 tile + 8px border → shared memory)
- Replace cos/sin with lookup table or fast approximation
- Wave intrinsics for cross-lane reduction (SM6.0)

## Expected Results

| Configuration | Estimated GPU Time | vs Current |
|--------------|-------------------|------------|
| Current (full-res, no temporal) | 1.23ms | baseline |
| Phase 1: Half-res + upsample | ~0.36ms | 3.4× faster |
| Phase 1+2: Half-res + temporal | ~0.15ms amortized | 8× faster |
| Phase 1+2+3: + Hi-Z | ~0.12ms amortized | 10× faster |
| All phases | ~0.10ms amortized | 12× faster |

## Impact
- Affected specs: `docs/specs/rendering/screen-space-effects.md` (GTAO section)
- Affected code:
  - `runtime/engine/renderer/passes/postprocess/gtao_pass.cpp` (520 lines)
  - `runtime/engine/renderer/passes/postprocess/gtao_pass.h` (232 lines)
  - `runtime/shaders/runtime/postprocess/gtao_cs.hlsl` (518 lines)
  - `runtime/shaders/runtime/postprocess/gtao_denoise_cs.hlsl` (139 lines)
  - `runtime/shaders/runtime/postprocess/gtao_temporal_cs.hlsl` (339 lines — integrate)
  - `runtime/shaders/runtime/postprocess/gtao_upsample_ps.hlsl` (281 lines — integrate)
  - `runtime/engine/renderer/frame/frame_lighting.cpp` (GTAO dispatch wiring)
- Breaking change: NO (defaults match current behavior, optimizations are opt-in)
- User benefit: 3-12× GPU performance improvement on AO, enabling 400+ FPS on simple scenes
