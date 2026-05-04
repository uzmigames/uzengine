# Proposal: phase0_fix-vfx-demo-launch-pipeline

## Why

Phase 12 (Niagara-parity editor UI overhaul) shipped ten widget
redesigns, all compiled + tested cleanly via `zig build`. But the
`vfx_viewport_app.exe` demo — the only way to visually verify the
new editor — currently **fails at startup**. The runtime log shows
a chain of pre-existing issues that predate phase 11.1 and have
been silently deferred through every VFX phase since:

```
[2026-04-20 08:36:32] [Renderer] [error] ShaderCompilerFXC: failed to
    load 'vfx_viewport_app\build\bin\d3dcompiler_47.dll' (Win32 error=126)
[2026-04-20 08:36:33] [IO] [error] rt_uv_test_lib.hlsl:34:28: error:
    type 'RayPayload' used as payload requires that it is annotated
    with the [raypayload] attribute
[2026-04-20 08:36:37] [RHI] [error] CreateGraphicsPipelineState failed
    (name='ParticleMeshOpaque'/'ParticleMeshTransparent')
    hr=0x80070057 (E_INVALIDARG)
[...]
Assertion failed: m_pMetadata->IsEmpty() && "Some allocations were not
    freed before destruction of this memory block!"
    D3D12MA/src/D3D12MemAlloc.cpp:8027
```

Plus the headless test binary `test_runtime.exe` fails identically
with `swscale-9.dll` unresolved (the ffmpeg DLLs never get copied
next to the test exe).

Every phase-12 task tail reads _"Test-binary execution continues
blocked by pre-existing missing `swscale-9.dll` — same gap
documented in every prior phase"_. This task closes the gap so
(a) the Niagara editor can be launched end-to-end for the first
time, (b) visual parity screenshots land for phases 12.2-12.10, and
(c) the ~2094+ unit tests the engine has accumulated can actually
execute.

## What Changes

1. **DLL deployment** — `build.zig` / `runtime/build.zig` copies the
   required runtime DLLs to the output `bin/` next to every demo +
   test executable:
   - `d3dcompiler_47.dll` (DXC fallback compiler)
   - `swscale-9.dll`, `avcodec-*.dll`, `avutil-*.dll`,
     `avformat-*.dll` (ffmpeg cluster)
   - Any other thirdparty DLL already deployed to the main
     `runtime/build/bin/` that downstream binaries transitively
     require.
   Existing "DLL staging" logic in `build.zig` should already cover
   this for the main `uzengine.exe`; extend it to every
   `exe.addInstallArtifact` target (demo executables + test runner).

2. **RT test shader payload annotation** —
   `runtime/shaders/rt_uv_test_lib.hlsl` uses the legacy
   unannotated `RayPayload` struct. Modern DXC requires explicit
   `[raypayload]` attribute on the payload struct + per-field
   read/write qualifiers. Update the struct to:

   ```hlsl
   struct [raypayload] RayPayload {
       float4 color : read(caller) : write(miss, closesthit);
   };
   ```

   Verify the pass still compiles + runs.

3. **ParticleMesh PSO creation (`hr=0x80070057`)** — both
   `ParticleMeshOpaque` and `ParticleMeshTransparent` fail to create
   with `E_INVALIDARG`. Diagnose via DX12 validation layer
   (`--gpu-validation`) to identify the invalid PSO desc field
   (likely mismatched vertex layout or a null shader blob) and fix.
   These passes are used by the mesh-particle renderer; failure
   silences every mesh-particle emitter.

4. **D3D12MemAlloc shutdown leak** — `D3D12MA` asserts on
   destruction that all sub-allocations are freed. The demo leaks at
   least one allocation. Bisect via logging allocation names (the
   allocator exposes named allocations); fix the missing free in
   whichever subsystem owns it (likely a GPU texture or buffer that
   outlives its scheduler).

5. **Headless screenshot verification** — once the demo launches,
   `vfx_viewport_app.exe --screenshot-stdout --screenshot-frame 5
   --frames 8` captures a 1280x720 PNG. The phase-12 screenshots
   from 12.2 / 12.3 / 12.4 / 12.10 finally become possible; each
   completed task can update its tail with a `PASS` line referring
   to the captured image.

## Impact

- Affected specs: `specs/vfx-demo-launch-pipeline/spec.md` (new).
- Affected code:
  - `runtime/build.zig` — DLL deployment for every demo + test exe.
  - `runtime/shaders/rt_uv_test_lib.hlsl` — `[raypayload]`
    annotation + field qualifiers.
  - `runtime/engine/renderer/` (or wherever `ParticleMeshOpaque` /
    `Transparent` PSOs are built) — PSO desc fix.
  - Whichever subsystem leaks a GPU allocation on shutdown (TBD).
  - `scripts/setup.ps1` — may need to re-download the ffmpeg DLL
    set if they've been stripped from the checkout.
- Breaking change: NO — all fixes restore existing-intended
  behavior that was silently broken before phase 11.
- User benefit: **the Niagara editor can finally be launched + seen
  running.** The full test suite (~2094 tests across phases) can
  execute via `test_runtime.exe`. Future phases can include screen-
  shot-based visual regression checks instead of only data-level
  assertions.

## Execution order suggestion

1. DLL deployment first (unblocks `test_runtime.exe` so later fixes
   can be validated via tests).
2. RT payload shader fix (one-line change, removes a loud log spam).
3. ParticleMesh PSO fix (the actual rendering gate).
4. D3D12MemAlloc leak (last — only assertions on teardown, doesn't
   block running).
5. Screenshot capture + phase-12 visual verification.
