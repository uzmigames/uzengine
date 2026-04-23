# User-Declared Per-Particle Attributes

## Overview

A VFX emitter can declare extra per-particle SoA channels that modules
and custom compute passes read and write by name. This is the engine's
Niagara "Parameters" parity: designers extend particle state without
modifying engine C++.

Supported channels:

| Type  | Max count | Buffer stride | HLSL type                    |
|-------|-----------|---------------|------------------------------|
| Float | 4         | 4 bytes       | `StructuredBuffer<float>`    |
| Vec4  | 2         | 16 bytes      | `StructuredBuffer<float4>`   |

## Data Model

```cpp
struct UserAttributeDef {
    std::string       name;          // e.g. "Mass", "CustomColor"
    UserAttributeType type;          // Float or Vec4
    float             defaultFloat;  // default for Float
    Vec4              defaultVec4;   // default for Vec4
    uint32_t          slotIndex;     // assigned by RegisterUserAttribute
};
```

The registry lives on `ParticleEmitter`:

```cpp
std::vector<UserAttributeDef> userAttributes;
```

Slots are assigned sequentially per type the first time a declaration is
registered. Re-declaration is idempotent (returns the existing slot) and
a type-mismatch is rejected with a warning.

## Lifecycle

1. **Declaration** — loader / gameplay / editor registers a channel via
   `ParticleEmitter::RegisterUserAttribute(def)`.
2. **Allocation** — `scene_vfx_sync` calls
   `EnsureUserAttributeBuffers(device)` after the emitter's base buffers
   are ready. Each declared slot creates a matching `StructuredBuffer`
   on both ping-pong attribute sets.
3. **Spawn** — the Spawn CS writes the declared default into every
   enabled channel at particle birth. See
   `particle_spawn_cs.hlsl` user-attribute block.
4. **Update / Render** — modules access the channel through
   `user_attrs.hlsli` helpers using the slot index stored in the CB.

## HLSL API

Include `user_attrs.hlsli` in any module that reads or writes user
channels:

```hlsl
#include "user_attrs.hlsli"

// Read a float channel with bindless SRV index from your CB.
float mass = UserAttr_ReadFloat(cb.userFloatSrvIdx[0], particleID);

// Write a float channel.
UserAttr_WriteFloat(cb.userFloatUavIdx[0], particleID, mass + 1.0);

// Vec4 variants.
float4 tint = UserAttr_ReadVec4(cb.userVec4SrvIdx[0], particleID);
UserAttr_WriteVec4(cb.userVec4UavIdx[0], particleID, newTint);
```

Unbound slots (`0xFFFFFFFFu`) short-circuit to a zero read and a no-op
write so modules can safely run against emitters that did not declare
the channel.

## Asset Format

`.uzfx` files carry a `userAttributes` array:

```json
{
  "maxParticles": 512,
  "emissionRate": 60,
  "userAttributes": [
    { "name": "Mass", "type": "float", "default": 1.0 },
    { "name": "CustomColor", "type": "vec4", "default": [1, 0.5, 0.25, 1] }
  ]
}
```

The serializer in `emitter_serializer.h` round-trips the array; the
sample at `sample/content/vfx/tests/user_attr_demo.uzfx` demonstrates
the format.

## Lua API

```lua
-- Declare a channel. Returns the slot index or nil on failure.
local slot = uz.vfx:declare_user_attr(handle, "Mass", "float", 1.0)

-- Declare a vec4 channel.
uz.vfx:declare_user_attr(handle, "CustomColor", "vec4", {1, 0.5, 0.25, 1})

-- Query a previously declared slot.
local s = uz.vfx:get_user_attr_slot(handle, "Mass")

-- Count declared channels.
local n = uz.vfx:get_user_attr_count(handle)
```

## Editor

The Parameters panel surfaces a read-only "User Attributes" module that
lists every declared channel and lets the designer edit its default
spawn value. Adding or removing channels is performed through the
asset file or the Lua API; the editor only exposes the defaults.

## Limits

Both quotas are compile-time constants in `vfx_graph.h`
(`kMaxUserFloatAttrs` / `kMaxUserVec4Attrs`). Raising them costs one
slot per attribute set per emitter in VRAM and is a simple change: bump
the constant, extend the CB (`ParticleSpawnConstants` /
`VFXSpawnConstants`), and expand the unroll loop in the Spawn CS.
