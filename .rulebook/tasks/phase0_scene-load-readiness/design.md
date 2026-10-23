# Scene load readiness integration contract

Status: implementation design, not acceptance evidence.

## Root causes

- The standalone preload loop renders full scene frames and calls `WaitIdle` to advance loading. Its submission count leaks into temporal sampling.
- Empty pending queues do not prove GPU residency. Texture finalization currently establishes a GPU queue dependency, while deferred mesh uploads leave the pending queue before graphics completion.
- SDF completion limits make partial GDF composition span multiple invisible frames. Those frames seed histories against incomplete geometry.
- A removed DX12 device can report `UINT64_MAX` as its completed fence value. Completion queries must reject device removal rather than publish readiness.

## Data flow

1. Application submits scene assets through existing world/VFS APIs. Runtime paths remain application-provided.
2. Loading service discovers and integrates assets, submits uploads, and polls explicit graphics/copy completion. It does not render scene passes or wait for device idle.
3. SDF workers read and validate cache data. Main thread creates and maps staging. Workers decompress into the mapped allocation without RHI calls. Main thread submits copies and retains staging until copy completion.
4. Once resource uploads are complete or individually failed, readiness reaches `ResourcesResident`. Individual failures remain logged with asset identity; device removal enters terminal `Failed`.
5. An internal resources-and-SDF-resolved predicate permits one full-window GDF composition using the latest camera and complete draw list. Composition advances scene revision once, then publishes `SceneComplete`. This predicate resolves the spec's circular wording about composition and SceneComplete.
6. Reveal starts by resetting exposure. Exactly two scene frames seed exposure while the loading surface remains opaque. Reveal completion resets the other temporal histories and rebases the temporal counter without discarding newly seeded exposure.
7. `CaptureReady` permits visible scene rendering and normal capture scheduling. The first revealed jitter index is 1. Capture and simulation frame indices are reveal-local; GPU allocator/submission indices remain independent.
8. After reveal, asset integration shares one elapsed-time deadline from `r.LazyLoad.BudgetMs`; SDF integration additionally stops at eight completions. A separate budget per subsystem would not satisfy the frame-wide limit.

## Ownership

| Owner | Responsibility |
| --- | --- |
| SceneRenderer/core | Readiness state, resource discovery/finalization, loading service, GDF orchestration, reveal transaction, temporal consumers |
| RHI/SDF | Nonblocking completion queries, device-removal rejection, SDF preparation/staging/copy lifetime and completion limits |
| Application/CLI | Message pumping, loading presentation, two seeds, reveal-local frame accounting, capture/diagnostic/manifest gates |
| Tests | State/failure/reveal coverage, SDF staging and fences, A/B/C determinism and responsiveness evidence |

## Lifetime constraints

- Submission completion must protect command allocators and upload-ring slots. A loading iteration number is not evidence that GPU use ended.
- Mapped staging remains alive while a worker writes it and while the copy queue reads it, including cancellation and shutdown paths.
- Cache body hashing uses the original decompressed byte layout, not GPU row padding. Corrupt bodies retain the existing GPU rebake fallback.
- Loading calls must not replace `WaitIdle` with a blocking fence wait. A simulated GPU stall must leave the message loop free to poll.
- `TemporalHistoryState::ResetAll(SceneChanged)` invalidates both matrix and content; `Invalidate(SceneChanged)` alone is insufficient.
- `IsSceneFullyLoaded()` remains true at `SceneComplete` and `CaptureReady`, but false for `Failed`. Do not implement it as an unchecked enum ordinal comparison.

## Validation rules

- Debug and Release builds, GPU captures and heavy test runs execute sequentially.
- Preserve before captures and executable hashes; never relabel an unknown binary configuration.
- A/B/C determinism compares every condition, allows different loading iteration counts, and rejects missing captures or incomplete runs.
- Falcor acceptance requires an existing matched reference with producer metadata. Missing reference data is an unpassed gate, not permission to manufacture provenance.
- GPU timing, responsiveness and coverage claims require measurements. A passing test count does not establish 95% coverage.
