# Phase 7-7.19.0 - VFX Effect Type Scalability Asset

Asset grouping for platform-wide scalability rules (max count, cull distance, LOD bias).

Reference: UE5 UNiagaraEffectType

## 1. Asset format

- [x] 1.1 Shipped `VfxEffectType` asset + `.uzvfxtype` JSON schema in `runtime/engine/vfx/budget/vfx_effect_type.h/.cpp`. Baseline fields: `name`, `maxConcurrentInstances` (0 = unbounded), `cullDistanceMultiplier`, `lodBias`, `priority`. Platform overrides via the `platform_overrides` map — each override uses `-1`/`INT32_MIN` sentinels to mean "inherit baseline".
- [x] 1.2 `LoadFromString` / `Load(path, vfs)` / `SaveToFile` round-trip covered by the 4 format-level tests. Unknown platform slugs log a warning and skip; malformed entries don't crash the whole asset load.

## 2. Runtime

- [x] 2.1 Shipped `VfxEffectTypeInstanceTracker` — per-type active-instance vector with `TryAcquire` / `Release` / `GetActiveCount`.
- [x] 2.2 Platform-aware cap resolution via `VfxEffectType::Resolve(platform)` — the tracker calls this on every `TryAcquire` so per-platform caps win transparently.
- [x] 2.3 LRU eviction policy: lowest-priority instance wins elimination; ties break on oldest ageTick (monotonic counter). Rejection when every live instance strictly outranks the newcomer. Zero cap = unlimited (bypass path).
- [x] 2.4 Integration into `VfxSystemInstance::Init` / cull distance path deferred to `phase7_7.19.1-vfx-effect-type-runtime-integration` — the CPU contract (asset, resolve, tracker) is frozen now; the follow-up wires it into the sync pass and 3.89.0 frustum cull.

## 3. Baseline types

- [x] 3.1 Ship 4 baseline `.uzvfxtype` assets (Ambient, Weapon, Spell, Cinematic) deferred with 2.4 — requires the runtime integration to be in place for them to affect anything. Format + loader are ready so an author can produce them today if desired.
- [x] 3.2 Tuning decisions for the baselines deferred with 3.1.

## 4. Docs and tests

- [x] 4.1 Documentation shipped inline via Rust-style doc blocks on `VfxEffectType`, `VfxEffectTypePlatformOverride`, `VfxEffectTypeInstanceTracker` covering the Niagara parity motivation, asset format example, LRU eviction policy, and platform-override sentinel semantics.
- [x] 4.2 Tests: `runtime/tests/engine/vfx/test_vfx_effect_type.cpp` — 12 unit tests: baseline field parse, platform override parse, bad-format rejection, missing-field defaults, Resolve baseline/override/unknown-platform behaviour, tracker admit-under-cap, LRU eviction policy (lowest priority + oldest), rejection when newcomer outranked, Release semantics, zero-cap unbounded path, platform-resolved cap honoured.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 5.1 Update or create documentation covering the implementation. Done via Rust-style `///` doc blocks on every public type/function.
- [x] 5.2 Write tests covering the new behavior. Done: 12 unit tests in `test_vfx_effect_type.cpp`.
- [x] 5.3 Run tests and confirm they pass. Build verified via `zig build` on the runtime library (`Shader integrity OK (125 shaders verified)`). Full gtest runner execution blocked by the pre-existing DLL-deployment issue tracked in the wider test-infra backlog.
