## 1. Distance Curve Authoring in AnimAsset
- [ ] 1.1 Define `DistanceCurve` struct in `src/assets/AnimAsset.h` with `std::vector<float> time_s` and `std::vector<float> cumulative_distance_m` parallel arrays, plus `max_distance_m` (float) pre-computed from the last element
- [ ] 1.2 Update `AnimAsset` to store `distance_curve` (optional, empty by default) per clip
- [ ] 1.3 Update the `.uzanim` file format to include a `[distance_curve]` INI block with two arrays: `time` (seconds, comma-separated) and `distance` (meters, comma-separated); block is omitted if no root motion exists
- [ ] 1.4 Update `AnimAsset::load()` in `src/assets/AnimAsset.cpp` to parse the `[distance_curve]` block when present and populate the `DistanceCurve` struct

## 2. Distance Curve Baking in AnimImporter
- [ ] 2.1 In `tools/importer/AnimImporter.cpp`, after sampling all root motion keyframes, accumulate cumulative distance: for each pair of adjacent root position samples, compute `|delta_pos|` and add to running total
- [ ] 2.2 Write the resulting `(time, cumulative_distance)` pairs into the `[distance_curve]` block in the `.uzanim` output at the same sample rate as the root motion track
- [ ] 2.3 Log the total baked distance (meters) and sample count to the import log for each animation with a root motion track; skip silently for animations without root motion

## 3. DistanceMatchingSystem
- [ ] 3.1 Create `src/animation/DistanceMatchingSystem.h` declaring `DistanceMatchingSystem` with `update(float delta_time, EntityWorld& world)` method
- [ ] 3.2 Create `src/animation/DistanceMatchingSystem.cpp` implementing the per-frame distance matching algorithm: for each entity with `CDistanceMatching` component, query the distance curve to find the animation time at which cumulative distance equals the entity's current travel distance accumulator
- [ ] 3.3 Implement `DistanceCurve::time_at_distance(float target_distance_m) const` using binary search on the `cumulative_distance_m` array and linear interpolation between adjacent samples, returning the interpolated time in seconds
- [ ] 3.4 Implement the playback rate fallback: when the entity's travel distance exceeds `DistanceCurve::max_distance_m`, compute a rate multiplier clamped to `max_rate_multiplier` (default 1.5x) and apply it to the clip's playback speed rather than seeking to a fixed time

## 4. CDistanceMatching ECS Component
- [ ] 4.1 Add `CDistanceMatching` ECS component to `src/scene/components.h` with fields: `enabled` (bool, default false), `cumulative_travel_m` (float, accumulated each frame), `max_rate_multiplier` (float, default 1.5f), `sync_marker_group` (string, optional, for cross-animation gait phase sync)
- [ ] 4.2 Register `CDistanceMatching` in the ECS component registry with reflection metadata
- [ ] 4.3 In `DistanceMatchingSystem::update()`, accumulate `|entity_velocity| * delta_time` into `cumulative_travel_m` each frame; reset to 0 when the active clip changes

## 5. Sync Marker Integration
- [ ] 5.1 After `DistanceMatchingSystem` repositions the playback cursor, find the nearest sync marker in the clip's sync marker list (by time proximity) and set it as the gait phase reference for cross-animation blends in `AnimatorSystem`
- [ ] 5.2 Expose `sync_marker_group` in `CDistanceMatching` to filter which sync marker group is used as the gait phase reference (e.g., only "Footstep" markers, not "Attack" markers)

## 6. State Machine Node Configuration
- [ ] 6.1 Add `distance_matching_enabled` (bool, default false) field to the animation state machine node config in the `.uzanim` state machine INI format
- [ ] 6.2 In `AnimatorSystem`, enable or disable distance matching for a clip based on the state node's `distance_matching_enabled` flag; non-locomotion states (attack, death) must default to disabled

## 7. Testing
- [ ] 7.1 Create `tests/animation/test_distance_curve.cpp` with Google Test cases: `time_at_distance()` returns 0 at distance 0, returns max time at max distance, interpolates correctly between samples, handles a monotonically increasing curve correctly
- [ ] 7.2 Add tests for the playback rate fallback: when `cumulative_travel_m > max_distance_m`, playback rate is clamped to `max_rate_multiplier`; rate is 1.0 when travel distance is within curve range
- [ ] 7.3 Add tests for `DistanceMatchingSystem`: entity at zero velocity does not advance `cumulative_travel_m`, entity at 2 m/s advances by `2 * delta_time` per frame, clip change resets `cumulative_travel_m` to 0
- [ ] 7.4 Add tests for distance curve baking in `AnimImporter`: a straight-line root motion of 3 m/s over 2 seconds produces a curve with max_distance = 6.0m; stationary root motion produces all-zero distances
- [ ] 7.5 Add an integration test where a mock entity walks 5 meters at 2.5 m/s and verify the playback cursor lands within 0.05s of the expected animation time
- [ ] 7.6 Verify line coverage >= 95% and branch coverage >= 95% across `DistanceMatchingSystem.cpp` and the `DistanceCurve` methods

## 8. Documentation
- [ ] 8.1 Update `docs/specs/animation-system.md` with `CDistanceMatching` component field reference, the distance matching algorithm description, rate multiplier fallback behavior, and sync marker integration note
- [ ] 8.2 Update `docs/specs/skeletal-mesh.md` with a note that distance matching requires root motion data and the `[distance_curve]` block to be present in the `.uzanim` file
