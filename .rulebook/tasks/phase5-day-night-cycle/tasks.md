## 1. TimeOfDay ECS Component
- [ ] 1.1 Create `src/scene/time_of_day.h` defining `TimeOfDay` as an ECS singleton component with fields `float hour` (0.0–24.0), `float speed_multiplier` (default 1.0, 0.0 = paused)
- [ ] 1.2 Create `src/scene/time_of_day.cpp` implementing `TimeOfDay::Tick(float dt)` that advances `hour += dt * speed_multiplier / 3600.0f` and wraps at 24.0
- [ ] 1.3 Register `TimeOfDay` as an ECS singleton in the scene system; ensure it is serialized to and deserialized from `.uzscene` under the `[time_of_day]` section

## 2. CurveFloat3 Keyframe Data Type
- [ ] 2.1 Define `CurveFloat3` struct in `src/scene/time_of_day.h` with a `std::vector<CurveKey3>` where `CurveKey3` contains `float time` (0–24), `float r, g, b` (or `x, y, z`)
- [ ] 2.2 Implement `CurveFloat3::Evaluate(float t)` using linear interpolation between adjacent keyframes; wrap `t` at 24.0 for cyclic curves

## 3. Directional Light Curve Driving
- [ ] 3.1 Add `CurveFloat3 sun_direction_curve` and `CurveFloat3 sun_color_curve` fields to `TimeOfDay` component
- [ ] 3.2 In `TimeOfDay::Tick()`, evaluate both curves at the current `hour` and update the scene directional light's `direction` and `color` properties via the ECS

## 4. Sky Atmosphere Parameter Curves
- [ ] 4.1 Add `CurveFloat3 horizon_color_curve`, `CurveFloat3 zenith_color_curve`, `CurveFloat3 sun_disk_curve` (xy=size, z=intensity) fields to `TimeOfDay`
- [ ] 4.2 In `TimeOfDay::Tick()`, evaluate these curves and pass the results to `src/renderer/sky_pass.cpp` via the sky parameters struct

## 5. Ambient Light Curves
- [ ] 5.1 Add `CurveFloat3 sky_light_color_curve` and `CurveFloat3 ground_bounce_color_curve` fields to `TimeOfDay`
- [ ] 5.2 In `TimeOfDay::Tick()`, evaluate ambient curves and update the ECS ambient light component

## 6. Moon Phase Secondary Light
- [ ] 6.1 Add `float moon_phase_angle` (0–1 normalized) and `CurveFloat3 moon_color_curve` fields to `TimeOfDay`
- [ ] 6.2 Activate the secondary directional light entity (moon) when `hour >= 18.0 || hour <= 6.0`; deactivate otherwise
- [ ] 6.3 Drive the moon light direction from `moon_phase_angle` converted to a spherical angle

## 7. Fog Integration
- [ ] 7.1 Add `CurveFloat3 fog_color_curve` and a separate `CurveFloat` (scalar) `fog_density_curve` to `TimeOfDay`
- [ ] 7.2 In `TimeOfDay::Tick()`, evaluate fog curves and push the results to the sky atmosphere pass fog parameters in `src/renderer/sky_pass.cpp`

## 8. Lua API
- [ ] 8.1 Implement `ctx.world:set_time(hour)` Lua binding in `src/lua/lua_world_api.cpp` setting `TimeOfDay::hour` directly
- [ ] 8.2 Implement `ctx.world:get_time()` returning `TimeOfDay::hour` as a float
- [ ] 8.3 Implement `ctx.world:set_time_speed(multiplier)` setting `TimeOfDay::speed_multiplier`

## 9. .uzscene Serialization
- [ ] 9.1 Implement serialization of `TimeOfDay` (including all curve data) to `.uzscene` INI-compatible format: `[time_of_day]` section with keyframe entries like `sun_color_key_0 = 0.0 0.0 0.0 0.0` (time r g b)
- [ ] 9.2 Implement deserialization of `[time_of_day]` section back into `TimeOfDay` component, including all curve keyframe arrays
- [ ] 9.3 Validate that `hour` values in the keyframe arrays are in ascending order on load; log an error and sort if they are not

## 10. Testing
- [ ] 10.1 Create `tests/core/test_time_of_day.cpp` with Google Test suite `TimeOfDayTest`
- [ ] 10.2 Write test `TickAdvancesHour`: call `Tick(3600.0f)` with `speed_multiplier=1.0`; assert `hour` advances by 1.0
- [ ] 10.3 Write test `TickPausedAtZeroSpeed`: call `Tick(3600.0f)` with `speed_multiplier=0.0`; assert `hour` does not change
- [ ] 10.4 Write test `HourWrapsAt24`: set `hour = 23.5`, call `Tick(3600.0f)` with `speed_multiplier = 1.0`; assert `hour` wraps and is approximately `0.5` (< 1.0)
- [ ] 10.5 Write test `CurveEvaluateLerpsBetweenKeys`: create a `CurveFloat3` with keys at hour=0 (0,0,0) and hour=12 (1,1,1); evaluate at hour=6; assert result is (0.5, 0.5, 0.5)
- [ ] 10.6 Write test `CurveEvaluateAtExactKey`: evaluate at an exact keyframe time; assert result equals that keyframe's value with no interpolation error
- [ ] 10.7 Write test `CurveEvaluateWrapsAt24`: evaluate a curve at hour=25; assert it is treated as hour=1 (wrap behavior)
- [ ] 10.8 Write test `MoonActiveAtNight`: set `hour = 20.0`; call `Tick(0.0f)`; assert the moon light is activated
- [ ] 10.9 Write test `MoonInactiveAtNoon`: set `hour = 12.0`; call `Tick(0.0f)`; assert the moon light is deactivated
- [ ] 10.10 Write test `SceneRoundtrip`: serialize a `TimeOfDay` with 3 sun color keyframes, deserialize it, and assert all keyframe values match
- [ ] 10.11 Verify C++ line coverage >= 95% and branch coverage >= 95% for `time_of_day.cpp`

## 11. Documentation
- [ ] 11.1 Update `docs/specs/scene-system.md` with the `TimeOfDay` ECS component, `CurveFloat3` type, and `.uzscene` `[time_of_day]` serialization format
- [ ] 11.2 Update `docs/specs/lua-scripting.md` with `ctx.world:set_time`, `ctx.world:get_time`, and `ctx.world:set_time_speed` API entries
- [ ] 11.3 Update `docs/specs/renderer.md` with sky atmosphere parameter curve inputs and fog curve integration
