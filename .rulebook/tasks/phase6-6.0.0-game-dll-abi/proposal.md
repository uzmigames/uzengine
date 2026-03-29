# Proposal: phase6-6.0.0-game-dll-abi

## Why
UzEngine needs a stable C ABI boundary between the runtime executable and game-specific code compiled as a DLL. This enables: (1) game code hot-reload without restarting the engine, (2) game-specific components and systems, (3) clean separation between engine and game code. The GameContext struct provides function pointers to engine services, avoiding any C++ ABI dependency.

## What Changes
- GameContext struct: opaque handles + function pointer callbacks to all engine services
- game.dll C ABI: 4 exported functions (on_start, on_update, on_fixed_update, on_stop)
- DLL loader with hot-reload protocol (serialize → unload → rebuild → reload → deserialize)
- ABI version compatibility check
- POD math types for ABI safety (GcVec3, GcQuat, GcMat4)

## Impact
- New directory: `runtime/engine/game/` (GameContext, DLL loader)
- New project: `game/` (sample game.dll)
- Affected: runtime/engine/app/ (DLL loading in engine loop)
- Spec: engine/game-dll-abi.md
- Breaking change: NO (engine works without game.dll)
