# Poison-allocation gate — build + first run (2026-10-13)

The gate from 02 is implemented and run. It reveals that the shipped sentinel guards
(#1 sky cubemap, #3 AP volume) do **not** fully eliminate the allocation-layout
dependence: a residual read-before-write still reaches the measured frame on all three
gated references.

## Implementation (shipped this session)

- `--poison-allocations <byte>` fills every engine-created non-depth GPU resource with
  that byte at creation (DX12 backend: `dx12_device.cpp` `PoisonTexture` = per-subresource
  `CopyTextureRegion` from a pattern UPLOAD buffer under COMMON->COPY_DEST->COMMON barriers,
  or `ClearDepthStencilView` for depth; `PoisonBuffer` = `CopyBufferRegion`; mapped
  Upload/Readback buffers = `memset`). The byte rides `RHIDeviceDesc::debugPoisonByte`.
  NOTE: the capture tool `runtime/tools/uzengine.cpp` has its own arg path (`BuildConfig`
  + `ToDeviceDesc`); the byte is wired there (not only in `application_platform.cpp`),
  else the gate's `uzengine.exe` runs ignore the flag.
- Gate: `scripts/rendering/test_poison_determinism.py` (reuses `test_rg_states_identity`
  cases/runner), two patterns 0x3F / 0xBF, 2 processes each, equal-pre-load pairing,
  bit-exact PNG compare. `D3D12 allocation poison active: 0x..` is logged and asserted
  per run.
- Vendored zig build exit 0 (139 shaders). Owned gtests: 7640 passed, 0 owned failures
  (the single `RuntimeViewportTest.LoadSceneAndGetTree` failure is pre-existing and
  unrelated: `uz_load_scene` cannot resolve `scenes/pbr_showcase.uzscene` in a standalone
  `test_runtime.exe` run — an asset-mount/CWD dependency, not the poison change, which is
  inert when `--poison-allocations` is absent).

## First run — FAIL (residual read-before-write)

| case | verdict | within-pattern delta | across-pattern delta | differing px | region |
|---|---|---|---|---|---|
| reflection [gated] | FAIL | 0, 0 | 1, 1, 1, 1 | 0.151% | full frame |
| fog_shaft [gated] | FAIL | 0, 0 | 1, 1, 1, 1 | 0.583% | full frame |
| path_tracer [gated] | FAIL | 0, 0 | 1, 1, 1, 1 | 0.163% | upper half (y 2..553) |
| local_shadow | FAIL | 0, 0 | 2 | — | — |
| decal | FAIL | 0, 0 | 1 | — | — |
| hdri | FAIL | 0, 0 | 3 | — | — |
| sponza | INDETERMINATE | 1, 0 | 1 | — | within-pattern nondeterminism |

Reading: within-pattern frames are bit-identical, so the capture is deterministic; the
cross-pattern difference is driven solely by the poison byte, which proves an
uninitialised allocation is read and reaches frame 120/237. The magnitude is **1 code
value** (the 8-bit quantisation floor), scattered across the frame — the exact "one code
value" symptom this task chased, propagated through a full-screen pass. This confirms the
#1/#3 guards are necessary but not sufficient; at least one more read-before-write
remains. The reflection reference has SSGI / GI reflections / final gather / radiance
cache / GTAO / fog all OFF and only TAA on, which narrows the residual source.

sponza is INDETERMINATE (a within-pattern pair already differs by 1), i.e. it carries a
non-memory nondeterminism source (TAA pre-load state) at this frame; it is not a gated
reference and is reported, not blocking.

## Localisation of the residual (2026-10-13)

- The carrier is the **TAA resolve pass**. A poison A/B on the reflection reference with
  `r.TAA=0` is bit-identical (max_delta 0, 0.000%); with TAA on it is max_delta 1, 0.151%.
  So everything the lit scene writes before TAA is clean; the residual enters through TAA.
- It is **not** the frame-0 history content. The TAA resolve already blends 100% current on
  the first frame (`taaCb.blendWeight = firstFrame ? 1.0`, `frame_postprocess.cpp:269`
  `firstFrame = !m_prevViewProjValid || !m_taaHistoryWritten[readIdx]`), so the first-frame
  history sample is multiplied by zero. Zeroing `rawHistory` on `stableReconReset` was a
  logical no-op (the output was byte-identical), confirming the frame-0 history read is not
  the source. The 1 code value is a per-frame TAA-path read that the quantised feedback loop
  holds to the measured frame; pinning the exact resource needs several rebuild+gate cycles
  into the TAA internals (clamp-box textures, the StableRecon `history.a` EMA, velocity, or a
  history-texture pad are the remaining suspects).

## Status / decision

The practical content symptom is fixed (the #1 sky-cubemap and #3 AP-volume sentinel
guards). The gate is built (3.1 docs, 3.2 test) and run (3.3), and it proves those guards
are not the whole story: a **1 code value** (8-bit quantisation floor) cross-pattern
residual, carried by TAA, is the original "one code value" symptom still present. Driving
it to bit-identical is a bounded-but-multi-cycle TAA investigation. Whether to drill it now
or accept+document it and proceed is a scope call raised with the user.
