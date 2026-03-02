# Gameplay Specs

| Spec | Description |
|------|-------------|
| [scene-system.md](scene-system.md) | Node types, `.uzscene` format, SceneManager API, entity pooling, schema evolution |
| [gameplay-systems.md](gameplay-systems.md) | System Scheduler (TickGroups), Gameplay Ability System (GAS), FSM, Behavior Trees |
| [spatial-systems.md](spatial-systems.md) | BVH / Octree spatial indexing, O(log n) queries, physics broad-phase integration |
| [event-bus.md](event-bus.md) | C++ typed EventBus<T> + Lua string-dispatch signals, predefined engine events, priorities |
| [tags-groups.md](tags-groups.md) | Node tags (bitset<64>), find_by_tag queries, Lua API, .uzscene format |
| [timer-system.md](timer-system.md) | Lua timer API: schedule_after, schedule_every, cancel_timer, schedule_next_frame |
| [input-system.md](input-system.md) | Enhanced Input (InputAction + MappingContext), XInput, runtime rebinding |
| [camera-system.md](camera-system.md) | SpringArm, camera modes (FPS/TPS/Orbit), shake, DoF, `.uzcam` presets |
| [save-system.md](save-system.md) | Save/load game state, msgpack serialization, save slots, autosave, thumbnail capture, Lua API |
| [localization.md](localization.md) | I18n via CSV, multi-language support, runtime locale switching, font fallback, hot-reload |
| [navmesh.md](navmesh.md) | Recast + Detour navmesh baking, NavMeshAgent, RVO2 local avoidance, dynamic obstacles |
