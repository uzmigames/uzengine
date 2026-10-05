## 1. Make the profiler trustworthy (DX12)
- [x] 1.1 Ring depth = command-list frames in flight + 1 and resolve the slot of the frame whose fence the CPU already waited (N-3), instead of N-2 while it may still execute. **Done** — `kProfilerFramesInFlight` 4 (`rhi_gpu_profiler_types.h`), `DX12GpuProfiler::ResolveFrame` reads the slot after the one just recorded.
- [x] 1.2 Per-slot pass names (copied, not pointers) and per-slot pair counts; resolve reads exactly the pairs the slot wrote (no zero-pair sentinel, no stale entries from an older frame with more passes). **Done** — `FrameSlot { names[64][48], count }`, pairs with end < begin dropped.
- [x] 1.3 Replace the dangling `std::string` pass names of the multi-scatter passes with literals. **Done** — `atmosphere_renderer.cpp` name tables indexed by order (static_assert on `kNumScatterOrders`).

## 2. Wire the output
- [x] 2.1 `--diag` enables the GPU profiler and measures a window inside its frame budget (`--timing-first` still wins when given). **Done** — also `--gpu-profile`; window = first quarter of the budget.
- [x] 2.2 Accumulate mean / min / max / samples per pass and the GPU frame span; fill `PassTimingEntry` and call `SetTimingData` before `GenerateReport`; extend `ToJSON`, the Lua `get_timing` table and the manifest's `gpu_ms` (`_frame`). **Done** — `RunStats::GpuAcc`, `gpuFrameMs`; `PassTimingEntry.minMs/maxMs/samples`.
- [x] 2.3 Per-frame "GPU Profile" log opt-in behind `r.GpuProfiler.Log` (default off). **Done** — `scene_render_frame.cpp`.

## 3. Verify
- [x] 3.1 `scripts/test_diag_timing.py` (pre-registered checks T0-T6) on the sky re-bake scene; before/after on the pre-change executable. **Done** — before 2/7 (timing empty), after 7/7; `SkyCubemapCapture` 1 sample under its own name, per-frame passes 143/143.
- [x] 3.2 Sponza Camera C `--diag` cost table (project configuration) recorded in the design notes as the block-B baseline. **Done** — `docs/analysis/lighting-block-b-01-pass-timings.md` (two runs at 1080p: GPU frame 21.6 ms; DeferredLighting 8.7, SSGI 8.7, GTAO 1.25, Shadow 0.9, RC trace 0.72).

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation (docs/engine/04-observability/03-gpu-profiling.md, docs/analysis/debug/rendering-debug-plan.md, docs/specs/1.2-rhi.md, host-responsiveness verification, known-issues). **Done** — plus `docs/analysis/lighting-block-b-01-pass-timings.md` and design.md.
- [x] 4.2 Write tests covering the new behavior with behavioral assertions and negative controls (the harness of 3.1). **Done** — T0 is the negative control (window after the last frame), T4 the label test.
- [x] 4.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately. **Done** — harness 7/7; `rulebook task validate` clean; gtests unchanged by this task (the 3 pre-existing failures of block A.10 are listed in `lighting-block-a-2026-10-05.md`).
