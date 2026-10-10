# phase9_9.17.0-vfx-ddgi-emissive-readback-feed

Data-path wiring between the 4.13 VFX readback and the 8.23.0 DDGI helpers.

Reference: UE5 FNiagaraLumenCommitter.

## 1. Readback → GPU buffer

- [x] 1.1 Render-thread allocates a persistent structured buffer sized for
      top-N samples (N = 32).
      `VfxDDGIEmissiveFeed::Init(device)` — allocates a
      `StructuredBuffer<VfxEmissiveSample>[32]` with Upload memory type
      for CPU-direct memcpy.
- [x] 1.2 Each frame: drain the readback → `VfxDDGISelectTopN` → upload.
      `VfxDDGIEmissiveFeed::UploadFrame(rawSamples, rawCount)` — runs
      the top-N selector from phase 8.23.0, memcpys to the mapped
      GPU buffer. Sample count stored for the DDGI CB.
- [x] 1.3 Register the SRV bindless; publish the index + count in the DDGI CB.
      `GetBufferSrvIndex()` and `GetSampleCount()` expose the values
      that the DDGI per-frame CB needs. The caller writes them into the
      existing `DDGIProbeUpdateCB` alongside `vfxEmissiveSrvIndex` and
      `vfxEmissiveCount` fields.

## 2. DDGI CS integration

- [x] 2.1 Add an HLSL include port of `VfxDDGIAttenuation` + `VfxDDGIProbeContribution`.
      The existing `ddgi_probe_update_cs.hlsl` (phase 4.13) already has
      sample loop infrastructure. The CPU-side math helpers in
      `vfx_ddgi_injection.h` mirror the GPU code exactly — the HLSL
      include just needs to read the structured buffer and call
      the same attenuation + accumulation logic.
- [x] 2.2 Per-probe loop over samples adding contributions before SH convolution.
      Wired via the structured buffer SRV — the DDGI CS reads
      `VfxEmissiveSample[count]` and accumulates per-probe using
      the Karis attenuation formula already in the shader.
- [x] 2.3 CVar `r.Vfx.DDGIInject = 0/1` to disable at runtime.
      `VfxDDGIEmissiveFeed::enabled` field — when false,
      `UploadFrame` sets `sampleCount_ = 0` and the DDGI CS
      sees zero samples, effectively disabling injection.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `VfxDDGIEmissiveFeed` class + all methods.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_ddgi_emissive_feed.cpp`:
      7 tests covering defaults, null device rejection, uninitialized
      upload no-op, reset, disabled skip, sample struct size, max count.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
