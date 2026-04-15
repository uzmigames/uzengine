# Proposal: phase7_7.3.0-vfx-event-pipeline-full

## Why
Niagara's event system lets one emitter emit events (spawn/death/collision/custom) and another react (spawn at event position, inherit velocity). UzEngine has VfxEventGenerator/VfxEventReceiver stubs but no full pipeline. Depends on 4.26.0 GPU readback framework and 7.4.0 persistent IDs. Unblocks secondary effects (sparks on impact, embers on death, smoke on collision).

## What Changes
Implement the full event lifecycle: emitter-side event write (spawn/death/collision CS hooks write to an event ring buffer), readback through the GPU readback framework (1-frame latency acceptable), receiver-side event handler dispatch (spawn N particles per event with payload inheritance).

## Impact
- Affected specs: docs/engine/25-vfx/18-vfx-system-compound.md
- Affected code: runtime/engine/vfx/events/, shaders/runtime/vfx/particle_update_cs.hlsl, collision module
- Breaking change: NO
- User benefit: Proper secondary effects - sparks on impact, embers on death, chain reactions.

## Source
Reference: UE5 FNiagaraEventScript + UNiagaraEventReceiverModule. Depends on: 4.26.0 (readback), 7.4.0 (persistent IDs).
