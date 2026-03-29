# Proposal: phase4-4.0.0-enhanced-input-system

## Why
UzEngine has basic raw input polling via Lua (uz.input.is_key_down), but lacks the structured action-based input system needed for real gameplay. Enhanced Input decouples gameplay actions from physical keys — "Jump" is an action, not "Space". This enables rebinding, gamepad support, input modifiers (dead zones, scaling), and trigger types (pressed, held, chord). Required for any playable demo.

## What Changes
- InputAction: named typed gameplay inputs (Bool, Axis1D, Axis2D)
- InputMappingContext: priority-based sets of key→action mappings
- InputModifier pipeline: DeadZone, Scale, Negate, Swizzle, Smooth, Normalize
- InputTrigger types: Pressed, Released, Held, Pulse, Tap, Chord
- .uzinput file format: declarative action + mapping definitions
- XInput gamepad support (4 controllers)
- Runtime rebinding with persistence to saves/bindings.uzinput
- Lua API: uz.input.action_value, uz.input.bind_action, uz.input.rebind

## Impact
- New directory: `runtime/engine/input/` (replaces basic polling)
- Affected: runtime/engine/scripting/api/lua_input.cpp (major rewrite)
- Depends on: Window/platform input (already exists)
- Breaking change: Lua uz.input API extended (old polling still works)
- Spec: input-system.md
