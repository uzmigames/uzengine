# Audio System

miniaudio backend — spatial audio, bus mixing, and audio components.

> **Status**: Planned (Phase 2)
> **Source**: `runtime/engine/audio/`

| Document | Topic |
|----------|-------|
| [01-overview.md](01-overview.md) | Audio system overview, miniaudio integration, voice management |
| [02-audio-engine.md](02-audio-engine.md) | Audio engine implementation — miniaudio backend, device init |
| [03-spatial-audio.md](03-spatial-audio.md) | 3D positional audio, distance attenuation, HRTF |
| [04-bus-mixing.md](04-bus-mixing.md) | 4-bus mixing graph (Master, SFX, Music, Voice), send routing |
| [05-audio-components.md](05-audio-components.md) | `CAudioSource`, `CAudioListener` components |
| [06-components-api.md](06-components-api.md) | Audio components C++ and Lua API reference |
