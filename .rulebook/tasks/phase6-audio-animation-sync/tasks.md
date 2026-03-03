## 1. AnimNotify Data Structures
- [ ] 1.1 Define `AnimNotify` struct in `src/animation/AnimNotify.h` with fields: `name` (string, 64-char max), `frame_index` (int), `asset_ref` (AssetHandle, optional audio asset), `volume` (float, default 1.0f), `pitch` (float, default 1.0f)
- [ ] 1.2 Define `AudioAnimNotify` as a subtype of `AnimNotify` (or a typed union entry) in the same header, specifically for audio playback notifies
- [ ] 1.3 Add `std::vector<AnimNotify> notifies` field to `AnimClip` struct in `src/animation/AnimClip.h`, sorted by `frame_index` ascending to enable binary search at evaluation time
- [ ] 1.4 Update the `.uzanim` file format to include a `[[notify]]` INI array block per clip, storing `name`, `frame`, `asset`, `volume`, `pitch`; update `AnimAsset::load()` in `src/assets/AnimAsset.cpp` to parse these blocks

## 2. Notify Evaluation in AnimationSystem
- [ ] 2.1 In `AnimationSystem::update()` (`src/animation/`) add a notify evaluation pass at the end of each tick that compares `previous_normalized_time` and `current_normalized_time` for each active clip
- [ ] 2.2 Convert the normalized time window to a frame index range and collect all `AnimNotify` entries whose `frame_index` falls within `[prev_frame, curr_frame]` using a binary search on the sorted notify list
- [ ] 2.3 Handle animation looping: when the clip loops during the frame, fire notifies from `[prev_frame, end_frame]` and then from `[0, curr_frame]` to ensure no notify is skipped
- [ ] 2.4 Handle sub-frame long-delta case: if `delta_time` is large enough that multiple full loops occur (unlikely but possible at low frame rate), fire notifies for each complete loop pass
- [ ] 2.5 After collecting fired notifies, dispatch `AudioAnimNotify` entries to `IAudioEngine::play_oneshot(asset_ref, world_position, volume, pitch)` using the entity's world position as emitter origin

## 3. Lua Callback API
- [ ] 3.1 Add `ctx.anim:on_notify(notify_name, handler_fn)` in `src/lua_bindings/LuaAnimBindings.cpp` that registers a per-character Lua callback for the named notify; callbacks are stored in a `std::unordered_map<std::string, LuaRef>` per entity
- [ ] 3.2 Fire all registered Lua callbacks for matching notify names during the notify dispatch pass, passing a notify info table `{name, frame, time}` as the single argument
- [ ] 3.3 Add `ctx.anim:remove_notify_handler(notify_name)` to deregister a callback, preventing dangling references when a character is destroyed
- [ ] 3.4 Update `docs/specs/lua-scripting.md` with the `on_notify`, `remove_notify_handler` API, parameter types, and a footstep sound usage example

## 4. Animation Editor Timeline Indicators
- [ ] 4.1 In `src/editor/animation_editor/`, add notify indicator rendering on the timeline scrubber: draw a vertical tick mark at each notify's normalized position, colored by type (orange for audio, white for generic)
- [ ] 4.2 Add tooltip on hover showing `name`, `frame_index`, `asset_ref` filename, `volume`, and `pitch` for the hovered notify indicator
- [ ] 4.3 Add click-to-select behavior on notify indicators that opens a property panel for editing `name`, `asset_ref`, `volume`, and `pitch`

## 5. Testing
- [ ] 5.1 Create `tests/animation/test_anim_notify.cpp` with Google Test cases: notify within playback window is fired, notify outside window is not fired, notify at exact boundary frame is fired, looping animation fires notify on each loop pass, sub-frame notify is not skipped on long delta
- [ ] 5.2 Add tests for audio dispatch: `AudioAnimNotify` calls `IAudioEngine::play_oneshot()` with correct asset handle, world position, volume, and pitch; missing audio asset handle is ignored without crash
- [ ] 5.3 Add tests for Lua callback registration: registered callback is invoked with correct notify info table, removed callback is not invoked after `remove_notify_handler`, registering two callbacks for same name replaces the first
- [ ] 5.4 Add `.uzanim` round-trip test: file with `[[notify]]` blocks loads all notifies with correct fields, save/reload preserves notify data
- [ ] 5.5 Add frame-rate independence test: animate at 10 fps and 240 fps with same elapsed time; assert identical set of notifies fired in both cases
- [ ] 5.6 Verify line coverage >= 95% and branch coverage >= 95% across `AnimNotify.h/.cpp`, the notify evaluation pass in `AnimationSystem`, and `LuaAnimBindings.cpp` notify paths

## 6. Documentation
- [ ] 6.1 Update `docs/specs/animation-system.md` with the `AnimNotify` struct fields, notify evaluation algorithm (window comparison, loop handling), and the dispatch order (audio first, then Lua callbacks)
- [ ] 6.2 Update `docs/specs/audio-system.md` with the `IAudioEngine::play_oneshot()` signature called by the animation system, and the emitter position source
- [ ] 6.3 Update `docs/specs/lua-scripting.md` with complete `ctx.anim:on_notify()` and `ctx.anim:remove_notify_handler()` documentation including a footstep sound callback example
