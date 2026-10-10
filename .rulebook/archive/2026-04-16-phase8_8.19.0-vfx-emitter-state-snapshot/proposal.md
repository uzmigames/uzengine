# Proposal: phase8_8.19.0-vfx-emitter-state-snapshot

## Why
Debugging a misbehaving effect requires capturing its full state (SoA particle data + RNG + wall-clock time) to disk for offline inspection. Niagara's Snapshot tool does this; UzEngine has nothing equivalent. Complements the sim-cache playback path (5.4.0 deferred).

## What Changes
Add VfxEmitterSnapshot::Capture(emitter, path) that reads back every SoA channel via the 4.26 readback framework + packs them with header metadata into a .uzvfxsnap binary. Inspector tool (deferred) reads the file and renders the particle cloud.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/ + new baker/vfx_snapshot.h/.cpp
- Breaking change: NO
- User benefit: Reproducible offline debugging of VFX bugs.
- Estimated effort: M

## Source
UE5 FNiagaraSnapshotBuilder.
