# Game DLL ABI — Specification

## ADDED Requirements

### Requirement: GameContext Struct
The engine SHALL pass a GameContext struct to game.dll containing function pointers to engine services.

#### Scenario: GameContext Initialization
Given the engine is loading game.dll
When game_dll_on_start(ctx) is called
Then ctx->version SHALL be the current ABI version
And ctx->registry SHALL point to the shared EnTT registry
And ctx->frame_arena SHALL point to the frame scratch allocator
And all function pointers (scene_*, audio_*, input_*) SHALL be non-null

#### Scenario: ABI Version Check
Given game.dll compiled against ABI version 2
When loaded by engine with ABI version 3 (backwards compatible)
Then game_dll_on_start SHALL succeed (version >= minimum)
When loaded by engine with ABI version 1 (too old)
Then game_dll_on_start SHALL return false and the engine SHALL log an error

### Requirement: DLL Exported Functions
The game DLL SHALL export exactly 4 C-linkage functions.

#### Scenario: Game Lifecycle
Given game.dll is loaded
When the engine starts
Then game_dll_on_start(ctx) SHALL be called once (returns bool)
Each frame
Then game_dll_on_update(ctx, dt) SHALL be called (variable timestep)
At 120Hz fixed rate
Then game_dll_on_fixed_update(ctx, fixed_dt) SHALL be called
When the engine shuts down
Then game_dll_on_stop(ctx) SHALL be called once

### Requirement: Hot-Reload Protocol
The engine SHALL support reloading game.dll without restarting.

#### Scenario: Hot-Reload Cycle
Given game.dll source code has changed
When hot-reload is triggered (file watcher or manual)
Then game_dll_on_stop(ctx) SHALL be called on the old DLL
And game state SHALL be serialized (ECS snapshot)
And the old DLL SHALL be unloaded (FreeLibrary)
And the new DLL SHALL be compiled (background build)
And the new DLL SHALL be loaded (LoadLibrary)
And game state SHALL be deserialized
And game_dll_on_start(ctx) SHALL be called on the new DLL

### Requirement: ABI-Safe Types
All types crossing the DLL boundary SHALL be POD C types.

#### Scenario: Math Types
Given a function pointer ctx->scene_set_position(entity, pos)
Then pos SHALL be GcVec3 { float x, y, z } (POD, no vtable)
And entity SHALL be uint64_t (opaque handle)
And no C++ types (std::string, std::vector) SHALL cross the boundary
