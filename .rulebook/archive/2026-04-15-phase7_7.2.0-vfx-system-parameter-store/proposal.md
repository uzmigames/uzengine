# Proposal: phase7_7.2.0-vfx-system-parameter-store

## Why
UE Niagara components expose named user parameters (User.MyVar) that game code writes and emitters read without recompiling the VFX graph. UzEngine has no such mechanism - every tweakable parameter must be a preset field, forcing preset/asset edits even for gameplay-driven values (intensity, damage, team_color). Tied to 7.1.0 namespaced attributes.

## What Changes
Implement VfxParameterStore at the system level (one per VfxSystemInstance) with string-keyed typed slots bound to the System scope of the attribute store. Lua and C++ can write; emitter modules bind parameter overrides to these slots. Auto-rebinds on hot-reload.

## Impact
- Affected specs: docs/engine/25-vfx/18-vfx-system-compound.md
- Affected code: runtime/engine/vfx/params/, system/vfx_system_instance.*, ecs/environment/vfx_system_component.h
- Breaking change: NO
- User benefit: Expose gameplay values (health, intensity, team color) to VFX without preset recompilation.

## Source
Reference: UE5 FNiagaraParameterStore + FNiagaraUserRedirectionParameterStore. Depends on: phase7_7.1.0-vfx-namespaced-attributes.
