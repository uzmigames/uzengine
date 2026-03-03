## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-audio-system` is complete — `AudioBus`, `IXAudio2SubmixVoice`, 4 buses exist
- [ ] 1.2 Read `docs/specs/audio-system.md` — understand bus architecture and `IXAudio2SubmixVoice`
- [ ] 1.3 Review XAudio2 XAPO API: `IXAPO`, `IXAPOParameters`, `CreateFX`, `IXAudio2Voice::SetEffectChain`
- [ ] 1.4 Review Xbox Audio Creation Tool (XACT) reverb parameters for reference values

## 2. XAPO Base Wrapper

- [ ] 2.1 Create `src/engine/audio/audio_dsp.h` — `UzXapoBase` template: inherits `IXAPO` and `IXAPOParameters`
- [ ] 2.2 Implement `UzXapoBase::LockForProcess` — validate buffer format (32-bit float, ≤ 2 channels)
- [ ] 2.3 Implement `UzXapoBase::Process` — virtual dispatch to derived class; copy output if in-place not supported
- [ ] 2.4 Implement `UzXapoBase::SetParameters` / `GetParameters` — lock-free parameter update via atomic double buffer
- [ ] 2.5 Define `EffectHandle` = uint32 — opaque ID for effect slots on a bus

## 3. Reverb XAPO

- [ ] 3.1 Implement `UzReverbXapo : UzXapoBase` using `XAUDIO2FX_REVERB_PARAMETERS` internally
- [ ] 3.2 Wrap `XAudio2CreateReverb` for DSP-quality reverb (requires `xaudio2fx.lib`)
- [ ] 3.3 Parameters: `room_gain_db` (-96–0), `room_hf_gain_db`, `pre_delay_ms`, `decay_time_s`, `hf_ratio`, `diffusion`, `density`
- [ ] 3.4 Implement `UzReverbXapo::set_preset(PresetName)` — populate parameters from named preset (CaveSmall, Hall, Room, etc.)

## 4. Parametric EQ XAPO

- [ ] 4.1 Implement `UzEqXapo : UzXapoBase` — 4-band biquad filter (lowshelf, 2× peaking, highshelf)
- [ ] 4.2 Implement biquad coefficients from `type`, `freq_hz`, `gain_db`, `q_factor` per band
- [ ] 4.3 Parameters: 4 bands × `{type, freq_hz, gain_db, q}` = 16 floats total
- [ ] 4.4 Update coefficients atomically when parameters change (avoid pops during real-time adjustment)

## 5. Compressor XAPO

- [ ] 5.1 Implement `UzCompressorXapo : UzXapoBase` — peak-sensing feed-forward compressor
- [ ] 5.2 Parameters: `threshold_db`, `ratio`, `attack_ms`, `release_ms`, `knee_db`, `makeup_gain_db`
- [ ] 5.3 Implement gain computer: `gain = threshold + (input - threshold) / ratio` above threshold
- [ ] 5.4 Implement attack/release envelope: exponential AR with configurable time constants
- [ ] 5.5 Apply gain in `Process()` per sample

## 6. Limiter XAPO

- [ ] 6.1 Implement `UzLimiterXapo : UzXapoBase` — brick-wall limiter (ratio = ∞)
- [ ] 6.2 Parameters: `threshold_db`, `release_ms`
- [ ] 6.3 Reuse compressor with `ratio = 100.0` and instantaneous attack (0ms)
- [ ] 6.4 Recommended as last effect in every bus chain to prevent digital clipping

## 7. AudioBus Effect Chain API

- [ ] 7.1 Update `src/engine/audio/audio_bus.h/cpp` — `AudioBus` gains effect list
- [ ] 7.2 Implement `AudioBus::add_effect(effect_type, params) -> EffectHandle` — creates XAPO, calls `IXAudio2Voice::SetEffectChain`
- [ ] 7.3 Implement `AudioBus::remove_effect(EffectHandle)` — rebuilds effect chain without removed slot
- [ ] 7.4 Implement `AudioBus::set_effect_param(EffectHandle, param_name, value)` — calls `IXAPO::SetParameters`
- [ ] 7.5 Implement `AudioBus::get_effect_param(EffectHandle, param_name) -> float`
- [ ] 7.6 Implement `AudioBus::set_effect_enabled(EffectHandle, bool)` — calls `EnableEffect`/`DisableEffect`

## 8. .uzproj Declarative Configuration

- [ ] 8.1 Extend `.uzproj` `[audio]` section — per-bus `[audio.sfx.effects]` subsection
- [ ] 8.2 Parse declarative effect chains at audio system init: `effect[] = "reverb:{room=-12,decay=1.5}"`
- [ ] 8.3 Apply to bus during `AudioSystem::init()`

## 9. Lua API

- [ ] 9.1 Implement `ctx.audio:bus(name) -> BusProxy` — return proxy for named bus
- [ ] 9.2 Implement `BusProxy:add_effect(type, params_table) -> handle`
- [ ] 9.3 Implement `BusProxy:remove_effect(handle)`
- [ ] 9.4 Implement `BusProxy:set_effect_param(handle, param_name, value)`
- [ ] 9.5 Implement `BusProxy:set_effect_enabled(handle, bool)`

## 10. Testing

- [ ] 10.1 Unit test: `UzReverbXapo` parameter double-buffer — `SetParameters` then `GetParameters` returns new values
- [ ] 10.2 Unit test: `UzEqXapo` biquad coefficients — verify frequency response at cutoff frequency
- [ ] 10.3 Unit test: `UzCompressorXapo` — 0dBFS input at ratio=4, threshold=-12dB → output ≈ -3dBFS
- [ ] 10.4 Unit test: `AudioBus::add_effect` → `IXAudio2Voice::SetEffectChain` called with correct descriptor
- [ ] 10.5 Unit test: `AudioBus::remove_effect` → effect chain rebuilt without removed slot
- [ ] 10.6 Integration test: add reverb to `ambient` bus → `ctx.audio:bus("ambient"):add_effect("reverb", {})` → XAudio2 processes reverb without error
- [ ] 10.7 Integration test: `set_effect_enabled(handle, false)` → effect bypassed, output matches dry signal

## 11. Documentation

- [ ] 11.1 Update `docs/specs/audio-system.md` — "DSP Chains" section with all XAPO types, parameters, Lua API
- [ ] 11.2 Update `docs/guides/audio-guide.md` — "Bus Effects" section with common preset patterns
