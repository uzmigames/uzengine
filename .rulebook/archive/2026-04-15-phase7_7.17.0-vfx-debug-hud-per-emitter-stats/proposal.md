# Proposal: phase7_7.17.0-vfx-debug-hud-per-emitter-stats

## Why
Niagara ships an in-engine Debug HUD (fx.Niagara.DebugHud) showing per-emitter alive counts, spawn/update times, budget usage, LOD levels, and top-offender lists. UzEngine has a generic stats overlay but nothing VFX-specific. Without it, performance debugging relies on GPU captures.

## What Changes
Build a VFX Debug HUD toggled by F10 (or CVar fx.debug) with columns: emitter name, alive/max, CS ms, render ms, LOD level, budget %. Top-N aggregated by cost. Use GPU timing queries already wired for --gpu-profile.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_debug_hud.* (new)
- Breaking change: NO
- User benefit: Fast per-emitter perf debugging without GPU captures.

## Source
Reference: UE5 NiagaraDebugHud + FNiagaraDebugHudSettings.
