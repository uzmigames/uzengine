# Proposal: phase11_11.8.0-niagara-burst-events-editor

## Why
One-time spawn bursts (an explosion fires 300 debris particles at t=0.2s, another 150 at t=0.4s) are a core Niagara feature. UzEngine's runtime already has the spawn pipeline but no author-facing way to schedule time-stamped bursts. Without this, authors simulate bursts via Lua timers or split into multiple emitters.

## What Changes
Add `VfxBurstEvent { float time; uint32_t count; }` + `std::vector<VfxBurstEvent> bursts` to `ParticleEmitter`. Runtime: each frame, compare `timeSinceRegistration` against pending bursts; when crossed, enqueue the burst count as an extra spawn this frame (on top of any rate-curve spawn). Editor UI: burst markers painted on the 11.6 timeline scrubber; right-click timeline at a position → "Add Burst Here"; click burst marker → edit count in a small popup.

## Impact
- Affected specs: docs/engine/25-vfx/ (new: 48-burst-events.md)
- Affected code: ParticleEmitter (`bursts` field); `CalculateSpawnCount()` checks bursts; `VfxTimelineScrubberWidget` (from 11.6) renders + edits burst markers
- Breaking change: NO (empty bursts = no change)
- User benefit: time-stamped spawn pulses without Lua timers
- Estimated effort: S

## Source
UE5 `FNiagaraSpawnInfo::Burst` array + phase 11.6 timeline.
