# Proposal: phase9_9.17.0-vfx-ddgi-emissive-readback-feed

## Why
Phase 8.23.0 shipped the DDGI injection helpers (Karis attenuation, top-N select, probe accumulation) and phase 4.13 shipped the top-N emissive readback. Neither side is connected — the DDGI probe-update CS never actually receives the VFX sample buffer.

## What Changes
Wire the 4.13 readback output into the DDGI probe-update pipeline: allocate a bindless structured buffer `VfxEmissiveSample[N]` on the render thread; the DDGI CS reads it and calls `VfxDDGIAccumulateAtProbe(samples, count, probePos)` per probe before SH convolution.

## Impact
- Affected specs: docs/engine/25-vfx/40-ddgi-emissive-injection.md
- Affected code: runtime/engine/vfx/readback + DDGI probe-update CS
- Breaking change: NO
- User benefit: Bright VFX events light the scene via GI.
- Estimated effort: L

## Source
UE5 FNiagaraLumenCommitter bridge.
