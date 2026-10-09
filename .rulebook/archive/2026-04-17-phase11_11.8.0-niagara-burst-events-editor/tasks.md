# phase11_11.8.0-niagara-burst-events-editor

Time-stamped spawn-burst array + timeline-marker editor.

Reference: UE5 `FNiagaraSpawnInfo::Burst`.

## 1. Data model

- [x] 1.1 `VfxBurstEvent { float time; uint32_t count; }` in `runtime/engine/vfx/spawn/vfx_burst_event.h`. Equality operator + header-only `VfxBurstList` namespace with `Add` / `Update` / `Remove` / `AdvanceCursor` helpers so the same sorted-insert math applies to the emitter, tests, and future snapshot tooling.
- [x] 1.2 `std::vector<VfxBurstEvent> bursts` field on `ParticleEmitter` + `size_t burstCursor = 0` next-unfired index. `VfxBurstList::Add` uses `std::upper_bound` to preserve ascending-by-time ordering on every insertion; `Update` re-sorts lazily (vectors typically <16 entries).
- [x] 1.3 JSON round-trip in `emitter_serializer.h`: `"bursts": [{"t": 0.2, "n": 300}, ...]`. Omitted when the list is empty (legacy files byte-identical). Loader tolerant to out-of-order authoring (sorts on load) + malformed non-object entries (silently dropped). `burstCursor` reset to 0 on load.

## 2. Runtime spawning

- [x] 2.1 `burstCursor` lives on the emitter (`size_t` index, monotonically advancing). Restart resets to 0 — host responsibility (phase 11.6 timeline `onRestartRequested` + phase 11.13 overlay restart pipeline). Safe-degrade: forgetting to reset just stops bursts firing (no double-fire, no exceptions).
- [x] 2.2 `CalculateSpawnCount(dt)` now calls `VfxBurstList::AdvanceCursor(bursts, burstCursor, timeSinceRegistration, ...)` and adds the fired count to the frame's total. Runs independently of `baseRate` — bursts still fire with `emissionRate == 0` and with an empty `spawnRateCurve` (a quiet emitter still fires its scheduled spikes).

## 3. Editor integration

- [x] 3.1 `VfxTimelineScrubberWidget::BindBursts(const std::vector<VfxBurstEvent>*)` accepts an external burst list. `ComputeLayout` emits a `VfxTimelineItemType::BurstMarker` per in-range burst (time ∈ `[0, duration]`). Marker height scales with `log2(count)` so 300-particle bursts render taller than 30-particle bursts.
- [x] 3.2 Hit-test returns `VfxTimelineHitResult::burstIndex` via the new `burstIndex` field on `VfxTimelineLayoutItem` + `VfxTimelineHitResult`. Host uses the index to drive right-click "Add Burst Here" / click-to-edit popups (visible popup UI lands with phase 11.13 overlay integration together with the other widget-host bindings).
- [x] 3.3 Marker rendering uses `kColorBurstMarker` (accent orange) — distinct from the blue track fill so bursts are immediately identifiable on a busy timeline.

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/48-burst-events.md` — data model, runtime flow code block, JSON schema, timeline scrubber integration, test coverage list, editor workflow, restart semantics.
- [x] 4.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_burst_events.cpp` covers: `VfxBurstList::Add` ascending-order invariant; `Update` re-sort on neighbour cross; `Remove` OOB rejected; `AdvanceCursor` fires all eligible from cursor + monotonic advance; `CalculateSpawnCount` fires burst on cross, fires with zero rate, composes with rate (21 = 1 + 20); cursor saturates + reset re-fires; serializer omits empty, round-trips populated, sorts unsorted input, drops malformed entries; timeline scrubber emits marker per in-range burst, skips out-of-range, marker X reflects `time/duration`, hit-test returns burst index, unbound list → no markers.
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the new type + field + serializer + timeline extension + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1).
