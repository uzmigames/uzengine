# Poison-allocation empirical gate — design (2026-10-13)

Design by the architect consultant (claude-fable-5-1) for item 3.3: an empirical
determinism gate proving no pass reads allocation-recycled (uninitialised) GPU memory
in the three target references. This file records the design; implementation and gate
results are in 03.

## Decision: two-pattern fill at creation (not a NOT_ZEROED flip)

The engine creates D3D12MA with `ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED`
(`dx12_device.cpp:707-709`), so fresh resources hold leftover bytes. The gate fills
every engine-created non-depth resource with a deterministic byte pattern at creation,
runs the references under two opposite patterns, and asserts the measured frames are
bit-identical across patterns. A read-before-write of any such resource makes the two
runs differ.

Rejected: toggling `NOT_ZEROED` (zeroed vs not-zeroed) is **unsound** — four independent
false-PASS paths: (1) zero is the benign value for `hasHistory`/counters/zero-weight
reads; (2) `CREATE_NOT_ZEROED` only skips re-zeroing *process-recycled* memory, OS-fresh
memory is zeroed either way so both states coincide on never-recycled ranges (an accident
of allocation history — exactly the layout dependence under test); (3) committed
allocations (large RTs) are zeroed in both states (D3D12MA adds the flag only to
default-pool block heaps); (4) pool-recycled sub-ranges are stale in both.

## Patterns

Two non-zero, opposite-sign, finite-in-all-formats bytes: **A = 0x3F, B = 0xBF**
(float32 +0.747 / -1.498; half +1.81 / -1.94; UNORM8 0.247 / 0.749; depth byte/255 =
0.247 / 0.749, inside (0,1) for reversed-Z). Avoid 0x00/0xFF (NaN/UINT_MAX), 0x7F
(->~FLT_MAX->inf), 0x80 (denormal flushed to 0, zero-tolerant).

## Switch rides RHIDeviceDesc, not a CVar

`--cvar` overrides apply at `application.cpp:124`, after device creation
(`application_platform.cpp`) and after `SceneRenderer::Init`, so a CVar-keyed poison
would miss every init-time allocation (SkyCubemap / AP volume / RTUVTestOutput). The byte
is a new `--poison-allocations <N>` CLI arg -> `ApplicationConfig::poisonAllocations` ->
`RHIDeviceDesc::debugPoisonByte` -> `DX12Device`. A mirror `r.Debug.PoisonAllocations`
CVar is set to the byte only so the capture manifest records it; it is not the switch.

## Fill mechanism (one path, no descriptors, no format logic)

A raw-byte copy from a pattern-filled UPLOAD buffer fills every non-depth resource:
`CopyTextureRegion` from a `PLACED_FOOTPRINT` for every non-DS texture (RT/UAV/SRV-only/
BC/3D/cube/mips via `GetCopyableFootprints` per subresource), `CopyBufferRegion` for every
DEFAULT buffer. Depth-stencil uses `ClearDepthStencilView` (already in DEPTH_WRITE).
Upload/Readback buffers are CPU-mapped (`DX12Buffer::mappedPtr`) -> `memset`. No MSAA
exists (every `SampleDesc.Count==1`), so there is no uncopyable texture class.

Textures: barrier `COMMON->COPY_DEST`, copy each subresource, barrier `COPY_DEST->COMMON`
(mandatory — implicit COPY_DEST promotion does not decay, and the render-graph state
registry assumes the created state). Buffers promote/decay, no barrier. One DIRECT
command allocator + list + a grow-to-fit pattern upload buffer live on the device;
`ExecuteCommandLists` + `WaitIdle` per creation (a few hundred ms over init — acceptable
for a gate). Skipped (documented): BLAS/TLAS result+scratch, swapchain back buffers,
breadcrumb/profiler readbacks.

## Scope

Run the full fill and the full 7-case sweep, but **gate 3.3 on reflection, fog_shaft,
path_tracer only**; local_shadow/decal/hdri/sponza are reported (expected identical; a
failure there is filed as a new candidate with its diff, not a 3.3 blocker). Rule that
keeps it honest: if a latent candidate makes one of the three gated references fail, it
is on the critical path and is fixed now. Pool-reuse re-poisoning (writer early-outs) is
a follow-up (the same fill in `RgResourcePool::Acquire*` reuse branches).

## Gate

`scripts/rendering/test_poison_determinism.py` imports CASES/PROCESSES from
`test_rg_states_identity.py`, runs each case under `--poison-allocations 0x3F` and `0xBF`
(2 processes per pattern), and for each equal-pre-load-length process pair asserts
`max|delta| == 0` on the frame PNGs, within and across patterns; each run.log must carry
the poison-active line. Verdicts: PASS; FAIL (across-pattern delta>0, within 0 =
uninitialised read); INDETERMINATE (within-pattern delta>0 = non-memory nondeterminism,
reported separately). 3.3 closes when the three gated references PASS.

**Negative control (before trusting green):** revert one guard (e.g. the
`m_apVolumeFilled` gate at `frame_constants.cpp:1004`), run fog_shaft + reflection A/B,
require FAIL. A gate that has never failed proves nothing.

## What it proves / does not

Proves: in this binary / resolution / CVar set, the measured frame of each gated
reference is bit-identical under two deterministic initial contents of every
engine-created texture and buffer — so no read-before-write of such a resource reaches
those frames. Does not prove: reads masked by downstream math under both patterns; reads
whose influence decays before the measured frame (TAA/accumulation); anything about AS
buffers, back buffers, descriptor contents, or pool-recycled transients; other
scenes/resolutions; sub-LSB effects (8-bit PNG compare, same resolution as the identity
harness that saw the symptom). Expected failure mode: a poisoned indirect-args/counter
buffer -> ~1e9 dispatch groups -> TDR -> no screenshot; that is a finding, not a gate
defect.
