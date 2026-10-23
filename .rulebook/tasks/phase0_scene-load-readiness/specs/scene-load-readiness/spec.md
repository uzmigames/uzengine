# Scene Load Readiness — Spec Delta

## ADDED Requirements

### Requirement: Explicit Readiness States
The SceneRenderer SHALL expose a `SceneLoadReadiness` state with the ordered values
`Loading`, `ResourcesResident`, `SceneComplete` and `CaptureReady`, and the state MUST only
advance; a failed asset, a failed SDF bake or a removed device MUST still let the state reach
a terminal value with the failure recorded.

#### Scenario: Resources resident
Given a scene whose meshes, textures and SDF requests have all been submitted
When every upload has completed on its fence or failed with a logged error
Then the readiness state is at least `ResourcesResident`
And `IsSceneFullyLoaded()` still reports false while any SDF bake is in flight

#### Scenario: Scene complete
Given the readiness state is `ResourcesResident`
When every mesh SDF has resolved (valid or failed) and one whole-window Global Distance Field composition has been recorded
Then the readiness state is `SceneComplete`
And `IsSceneFullyLoaded()` reports true

#### Scenario: Failed asset does not block readiness
Given one texture fails to decode during the load
When the remaining assets finish
Then the readiness state still reaches `SceneComplete`
And the failure is logged with the asset path

### Requirement: Loading Loop Without Full Renders
During the initial load the runtime SHALL drive `SceneRenderer::ServiceLoading` and present a
loading overlay each iteration; it MUST NOT record a scene frame and MUST NOT call
`WaitIdle` to advance load state. Window messages MUST be pumped at least once per iteration.

#### Scenario: No invisible scene frames
Given the Sponza project is opened with `--run-manifest`
When the load completes
Then the manifest's `frames.preload` counts service iterations
And no `GI recentre` log line is emitted before the first revealed frame
And only the two exposure-seed frames render before reveal

#### Scenario: Window stays responsive under a GPU stall
Given the diagnostic CVar `r.Load.Diagnostic.GpuStallMs` is set to 500
When the loading loop runs
Then window messages are pumped at least every 16 ms
And closing the window during the load exits the process cleanly

### Requirement: Single Whole-Window GDF Composition at Scene Complete
While the readiness state is below `SceneComplete` the Global Distance Field SHALL NOT be
composed; at `SceneComplete` exactly one whole-window composition MUST be recorded, and it
MUST bump `sceneRevision` so that SSGI and the radiance cache reset through their existing
revision contract.

#### Scenario: One composition for 138 SDFs
Given Sponza's 138 mesh SDFs resolve from the cache during the load
When the readiness state becomes `SceneComplete`
Then one whole-window composition is recorded for every clipmap
And `sceneRevision` increases by exactly one
And the SSGI and radiance-cache histories are reset on the first revealed frame

#### Scenario: Camera moved during the load
Given the camera position changes between the first and last loading iteration
When the composition at `SceneComplete` is recorded
Then the clipmap windows are centred on the camera position of that iteration
And every clipmap reports `hasData` before the first revealed frame

### Requirement: Reveal-Time Temporal Rebase
At reveal the renderer SHALL restart its temporal frame counter at 0 and SHALL reset every
temporal history: TAA (matrix and content), SSGI, SSR, denoiser guides, GTAO, clouds, env-MIS
and auto exposure. The TAA jitter index on the first revealed frame MUST be 1 regardless of
the number of loading iterations.

#### Scenario: Jitter phase independent of load length
Given two processes load Sponza in 38 and 45 loading iterations
When each process renders its first revealed frame
Then both use Halton jitter index 1
And both `TemporalHistoryState` decisions report `matrixValid == false` with reason `SceneChanged`

#### Scenario: Per-pass flags are covered
Given GTAO, clouds and env-MIS have accumulated history during the exposure-seed frames
When reveal happens
Then each pass reports its history invalid on the first revealed frame

### Requirement: Capture Readiness
`CaptureSequence`, `--screenshot`, `--diag` and the run manifest SHALL key on `CaptureReady`,
defined as `SceneComplete` plus the reveal reset plus the two exposure-seed frames.
Captures taken with `--screenshot-preload` MUST still be tagged `preloading`.

#### Scenario: Determinism gate passes without the worker wait
Given `r.GI.MeshSdf.PreloadWait` is 0 and `scripts/rendering/test_preload_determinism.py --gate` runs three rounds
When conditions A (defaults), B (30 ms worker delay) and C (no wait, 30 ms delay) are compared
Then all three agree within the gate's code-value tolerance
And the Falcor parity gate reports PSNR >= 35 dB on the existing references

### Requirement: Post-Reveal Streaming Budget
After reveal, main-thread asset integration SHALL stop after `r.LazyLoad.BudgetMs`
milliseconds in a frame in addition to the existing item counts, and the mesh SDF completion
cap of 8 per frame MUST remain so that Global Distance Field recomposes stay regional while
the scene is visible.

#### Scenario: Budget bounds a frame
Given 100 completed meshes are ready and `r.LazyLoad.BudgetMs` is 2
When one frame integrates them
Then integration stops once 2 ms have elapsed and resumes next frame
And no frame recomposes more than `r.GI.GDF.MaxRegions` regions

### Requirement: Worker-Side SDF Staging and Copy-Queue Uploads
Cache-hit SDF volumes SHALL be decompressed by a worker directly into a staging buffer that
the main thread mapped in advance; the worker MUST NOT call the RHI. The upload MUST be
recorded on the copy queue and the staging buffer MUST stay owned by the queue until the
copy fence has passed.

#### Scenario: No WaitIdle in the service
Given 138 cache hits are serviced with `MockRHIDevice`
When the load completes
Then `WaitIdle` is never called by the SDF service
And every staging buffer is released only after its fence value is reported complete
And the uploaded volume bytes equal the decompressed cache entry

#### Scenario: Corrupt cache entry
Given one cache entry fails its body hash
When the worker prepares it
Then the mesh is re-baked on the GPU as today
And the readiness state still reaches `SceneComplete`

## MODIFIED Requirements

### Requirement: Pre-load Determinism Wait
`r.GI.MeshSdf.PreloadWait` SHALL default to 0 once the determinism gate passes with the reveal
rebase; the CVar MUST remain readable for one release and MUST log a deprecation line when set.

#### Scenario: Deprecated wait
Given `--cvar r.GI.MeshSdf.PreloadWait=1`
When the runtime starts
Then a log line states the wait is deprecated and ignored by the loading loop
And the load proceeds without blocking on worker preparations
