# Proposal: Per-pass GPU timings in `--diag` (lighting block B, item 1)

## Why
Every cost decision in the lighting assessment of 2026-10-05 (parecer, block B) is a guess
until the engine reports what each pass costs on the GPU. `uzengine.exe --diag` prints
`"timing": []` on every run: `PassInspector::SetTimingData` has no caller, and nothing turns the
GPU profiler on for a diagnostic run. The render graph already brackets every pass with a
timestamp pair and `--run-manifest` already writes per-pass means, so the missing piece is small,
but the existing path has correctness defects that would make the numbers wrong once they are
read: the DX12 profiler resolves the slot of frame N-2 while that frame can still be executing
(ring depth 3 = command-list frames in flight), one shared name table labels frame N-3's timings
with frame N's pass names, and three multi-scatter passes register dangling `std::string` names.
The recommended order of the assessment puts this first ("sem isso toda decisão de custo é
chute").

## What Changes
- `--diag` enables the GPU profiler, measures a window inside its 120-frame budget and fills the
  report's `timing` array (mean / min / max ms and sample count per pass, plus the GPU frame span)
  through `SetTimingData`; the run manifest gets the same statistics.
- `DX12GpuProfiler`: ring depth = command-list frames in flight + 1, per-slot names (copied) and
  per-slot pair counts, resolve reads exactly the pairs a slot wrote, from the frame whose fence
  the CPU has already waited.
- The multi-scatter passes get literal names.
- The per-frame "GPU Profile" log becomes opt-in (`r.GpuProfiler.Log`).
- Docs that describe the profiler as wired or `--gpu-profile` as working for `uzengine.exe` are
  corrected; a behavioural harness guards the output.

## Impact
- Affected specs: gpu-pass-timing (new)
- Affected code: runtime/tools/uzengine.cpp; runtime/engine/rhi/dx12/dx12_gpu_profiler.{h,cpp};
  runtime/engine/rhi/rhi_gpu_profiler_types.h; runtime/engine/renderer/passes/debug/inspector_pass*.{h,cpp};
  runtime/engine/renderer/scene/scene_render_frame.cpp; runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp;
  runtime/engine/scripting/renderer/lua_pass_inspector_api.cpp; scripts/test_diag_timing.py
- Breaking change: NO (the `timing` entries gain fields; `gpu_ms` of the manifest keeps its keys)
- User benefit: `--diag` and the manifest report trustworthy per-pass GPU milliseconds, which the
  rest of block B (SDF reflections cost, GDF incremental, DDGI dedup) needs as its baseline.
