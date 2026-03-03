## 1. TimeSystem Dilation API
- [ ] 1.1 Add `float dilation_factor` field (default 1.0f) to `TimeSystem` in `src/core/time_system.h`
- [ ] 1.2 Implement `TimeSystem::set_dilation(float factor)` clamping to `[0.0f, 100.0f]` and storing the value; factor 0.0 = frozen, 1.0 = normal, > 1.0 = fast-forward
- [ ] 1.3 Implement `TimeSystem::get_dilation()` returning `dilation_factor`
- [ ] 1.4 Modify `TimeSystem::delta_time()` to return `raw_delta * dilation_factor` instead of `raw_delta`
- [ ] 1.5 Implement `TimeSystem::raw_delta_time()` returning `raw_delta` unmodified for use by networking and input polling systems

## 2. Subsystem Integration
- [ ] 2.1 Update `src/physics/physics_world.cpp` to consume `TimeSystem::delta_time()` (dilated) instead of raw time in its simulation step
- [ ] 2.2 Update `src/animation/animation_system.cpp` to consume `TimeSystem::delta_time()` in animation timeline advancement
- [ ] 2.3 Update `src/particle/particle_system.cpp` to consume `TimeSystem::delta_time()` in particle simulation ticks
- [ ] 2.4 Verify `src/audio/audio_system.cpp` consumes `TimeSystem::delta_time()` for scheduled audio events; audio DSP hardware is not affected by dilation but event scheduling is

## 3. Audio Pitch Scaling
- [ ] 3.1 Add `float audio_pitch_coupling` field to `TimeSystem` in `src/core/time_system.h` with default value 0.3f
- [ ] 3.2 Parse `[time] audio_pitch_coupling` from `.uzproj` in `src/engine/ProjectConfig.cpp` and apply to `TimeSystem::audio_pitch_coupling`
- [ ] 3.3 Compute `pitch = 1.0f + (dilation_factor - 1.0f) * audio_pitch_coupling` in `TimeSystem::set_dilation()` and call `AudioSystem::set_global_pitch(pitch)` to apply it

## 4. CTimeDilationOverride ECS Component
- [ ] 4.1 Define `CTimeDilationOverride` component in `src/core/time_system.h` (or new `src/engine/components.h`) with field `bool ignore_global_dilation = true`
- [ ] 4.2 Implement component query in animation and particle systems: if an entity has `CTimeDilationOverride` with `ignore_global_dilation = true`, use `TimeSystem::raw_delta_time()` for that entity's updates instead of `delta_time()`

## 5. Timer System Integration
- [ ] 5.1 Update `TimerSystem` (in `src/engine/core/timer_system.cpp`) to advance timer countdowns using `TimeSystem::delta_time()` (dilated) by default
- [ ] 5.2 Add `schedule_after_realtime(float seconds, Callback cb)` variant to `TimerSystem` that uses `TimeSystem::raw_delta_time()` for countdown, ignoring dilation

## 6. Lua API
- [ ] 6.1 Implement `ctx.time:set_dilation(factor)` Lua binding in `src/lua/lua_time_api.cpp` calling `TimeSystem::set_dilation()`
- [ ] 6.2 Implement `ctx.time:get_dilation()` Lua binding returning `TimeSystem::get_dilation()`
- [ ] 6.3 Implement `ctx.time:get_delta()` Lua binding returning `TimeSystem::delta_time()` (dilated)
- [ ] 6.4 Implement `ctx.time:get_raw_delta()` Lua binding returning `TimeSystem::raw_delta_time()`

## 7. Testing
- [ ] 7.1 Create `tests/core/test_time_dilation.cpp` with Google Test suite `TimeDilationTest`
- [ ] 7.2 Write test `DeltaTimeScaledByDilation`: set `dilation = 0.5f`; call `set_raw_delta(0.016f)`; assert `delta_time() == 0.008f`
- [ ] 7.3 Write test `RawDeltaUnaffectedByDilation`: set `dilation = 0.5f`; assert `raw_delta_time() == 0.016f` (unchanged)
- [ ] 7.4 Write test `ZeroDilationFreezes`: set `dilation = 0.0f`; assert `delta_time() == 0.0f`
- [ ] 7.5 Write test `FastForwardDilation`: set `dilation = 2.0f`; call `set_raw_delta(0.016f)`; assert `delta_time() == 0.032f`
- [ ] 7.6 Write test `DilationClampedAtZero`: call `set_dilation(-1.0f)`; assert `get_dilation() == 0.0f`
- [ ] 7.7 Write test `AudioPitchScaledByCoupling`: set `dilation = 0.5f`, `audio_pitch_coupling = 0.3f`; assert the audio pitch passed to `AudioSystem::set_global_pitch` equals `1.0f + (0.5f - 1.0f) * 0.3f = 0.85f`
- [ ] 7.8 Write test `AudioPitchNeutralAtNormalDilation`: set `dilation = 1.0f`; assert `set_global_pitch` is called with `1.0f`
- [ ] 7.9 Write test `TimerDefaultUsesDialtedDelta`: create a timer for 1 second; advance time by 0.5 seconds with `dilation = 0.5f` (0.25 real seconds); assert the timer has not fired after only 0.5 dilated seconds
- [ ] 7.10 Write test `RealtimeTimerIgnoresDilation`: create a `schedule_after_realtime(1.0f)` timer; advance `raw_delta` by 1.0f with `dilation = 0.0f`; assert the timer fires despite zero dilation
- [ ] 7.11 Write test `EntityWithOverrideUsesRawDelta`: create an entity with `CTimeDilationOverride`; set `dilation = 0.0f`; run an animation update; assert the entity's animation advanced by `raw_delta` not `delta_time()`
- [ ] 7.12 Write test `ProjectConfigParsesAudioPitchCoupling`: parse a `.uzproj` with `[time] audio_pitch_coupling = 0.5`; assert `TimeSystem::audio_pitch_coupling == 0.5f`
- [ ] 7.13 Verify C++ line coverage >= 95% and branch coverage >= 95% for `time_system.cpp`

## 8. Documentation
- [ ] 8.1 Update `docs/specs/timer-system.md` with the dilated vs. realtime timer variants and how dilation affects countdown
- [ ] 8.2 Update `docs/specs/animation-system.md` noting that animation advancement consumes dilated delta time and `CTimeDilationOverride` opts entities out
- [ ] 8.3 Update `docs/specs/lua-scripting.md` with all `ctx.time.*` API entries
- [ ] 8.4 Update `docs/specs/audio-system.md` with the `audio_pitch_coupling` `.uzproj` key and the pitch scaling formula
