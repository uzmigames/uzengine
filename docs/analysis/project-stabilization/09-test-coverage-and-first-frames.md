# 09. Test coverage and first-frame diagnostics

Date: 2026-10-10. Continuation of the approved stage-0 stabilization work.
This report supersedes the coverage and first-frame gaps in [08](08-native-bootstrap.md).
It does not accept the remaining renderer/editor findings or claim a green native suite.

## Changes and causes

The extension now tests activation, the IPC client, the worker's native-addon boundary,
the console, hierarchy, viewport and scene editor. The new regressions reproduced
two lifecycle defects before correction: a replaced worker's late events could
settle the replacement's requests, and a disposed client could spawn another worker.
Handlers now check process identity, disposal rejects new calls, and a delayed
restart prompt cannot kill an already-running replacement. Failed process creation
also rejects pending calls on `error`; Node need not emit `exit` after a failed fork.
The worker exposes injectable host/addon boundaries so the real command dispatch,
binary serialization, error replies and cleanup execute without loading a GPU addon.
Native world ownership, queue bounds and request deadlines remain audit item 2.5.

MCP tests exercise all six tools over the real SDK's in-memory transport, index
reload, filters, graph traversal, field tracing, references and binary index persistence.
Server construction is separate from stdio startup, allowing independent instances.
Index insertion rejects nonconsecutive IDs and malformed vectors before mutation;
the previous sparse-ID path could corrupt graph positions and underallocate serialization.
Documentation reload prepares its replacement before publishing, preserving the
previous index if a malformed database fails to load.

Both CI test jobs check types before tests. MCP's new `tsconfig.test.json` includes
the tests themselves, which the production build configuration did not include.
The original 95% global thresholds remain unchanged. Only extension test programs
and declarations are excluded from production coverage; every production module
remains included. The two manual native/editor harnesses are not Vitest suites.
MCP `tools/` remains development-time generator/parser infrastructure; parser and
resolver tests and the required generated-database integration test cover it outside
the existing production `src/` coverage metric. No new production exclusions were added.
Existing Prettier failures in both packages were corrected using their configured formatter.

## Executed TypeScript checks

Local Node **22.23.3**, with the checked-in dependency locks:

| Package | Tests | Statements | Branches | Functions | Lines |
|---|---:|---:|---:|---:|---:|
| Extension | 116 passed | 97.63% | 96.07% | 97.58% | 97.92% |
| MCP | 88 passed | 98.52% | 95.85% | 97.40% | 99.03% |

Type checks, builds, ESLint, Prettier checks and both complete coverage commands
exit 0. These are global coverage gates, not a claim that every file individually
has 95% coverage. The extension tests use VSCode and native-addon boundary doubles;
they do not certify a live Electron session, native rendering ownership or the webview's decoder.
The MCP protocol tests use the actual client, server, transport and search implementation.
The current documentation database was regenerated from 1,404 source files into
15,416 entities before the final MCP run. A separate smoke test started the compiled
server over stdio and discovered all six tools (`mcp-stdio-smoke.json`). These are
local Windows results; no hosted CI run is claimed.

Evidence under `.sandbox/stabilization/`:

- `extension-ipc-before.log`: the two reproduced lifecycle regressions.
- `extension-final-coverage.log`: final 116-test result and metrics.
- `mcp-final-coverage.log`: final 88-test result and metrics.
- `extension-format-before.log`, `mcp-format-before.log`: previously failing format gates.
- `claude-validation-scope-review.txt`: Claude's read-only acceptance/configuration review.
- `claude-baseline-closure-review.txt`: Claude's second read-only review found no
  blocker for the limited stage-0 scope, with the evidence clarifications below.

## First-frame capture

The CLI supports `--screenshot-preload` together with `--screenshot` and
`--screenshot-frames`. It requests each selected screenshot **before** that frame's
render, including frame 0 and exposure-seeding frames when explicitly requested.
Loading and presentation order are preserved. Without the option, preload and
not-ready frames remain excluded. Absolute render indices use zero-based numbering;
the editor overlay displays its own one-based frame counter.

Sequence parsing accepts `N`, `A-B` or `A-B:step`. It rejects trailing garbage,
overflow, descending ranges and zero steps instead of silently capturing a fallback
frame. Numbered filenames work with a bare filename, directory or omitted extension.
The run manifest records requested frame, path, preload phase and asset readiness
at request time. The evidence verifier separately checked every resulting PNG's
presence, dimensions and integrity; a request alone is not accepted as a capture.

The rebuilt isolated native executable has SHA-256
`4aeccdcd02b962e0097003b39d89be36ce1706b4c6aa9267e1d71e865d9a7c34`.
Its source inputs are the snapshot from [08](08-native-bootstrap.md), plus the
three hashed capture files listed in `native-first-frame-evidence.json`.
The dependency profile's `provision.json` hash is retained there as well.

Capture used PBR Showcase, DX12, RTX 4090, driver **32.0.16.1714**, 1280×720,
camera position `[0,-1800,900]`, requested look-at `[0,200,100]`, hidden editor mode and the
effective CVars recorded in `run.json`. Twenty declared project/scene/asset files
were hashed, with no missing references. Builtin meshes are identified separately.
The editor camera remains fixed; per-frame exposure is not exported by this manifest.
The manifest's camera `target` is position plus normalized forward
(`[0,-1799.07,899.629]`), not the original requested look-at point.

```powershell
& <current-executable> --project <pbr-showcase-project> --hidden --editor --verbose `
  --diag --frames 8 --camera-pos 0,-1800,900 --camera-look 0,200,100 `
  --run-manifest <fresh-output>/run.json --screenshot <fresh-output>/frame.png `
  --screenshot-frames 0-8 --screenshot-preload
```

`first-frames-v1/` contains all nine images, frames 0–8, including five preload
frames. The run completed 13 frames and exited 0. Images 0 and 8 were inspected:
the initial scene is still loading; the later image contains the textured objects
and checker floor. `first-frames-default/` contains only frames 5–8 from the same
selection without preload opt-in, as required; it also exited 0. That control uses
`--frames 6` (11 total including preload), versus eight post-load frames in the
opt-in run. Both exact commands and executable hashes are retained in the evidence.
Readiness is sampled before rendering: frame 0 reports `assets_ready: true` before
lazy loading starts, frames 1–2 report false, and frames 3–4 report true again.
This non-monotonic field is an observation, not an initial-frame acceptance gate.

Four `CaptureSequence.*` native tests pass. Actual CLI negative controls reject
`1-9:0` and preload capture without a sequence/destination with exit 1.
Evidence: `native-preload-build.log`, `native-capture-tests.json`,
`native-capture-negative-controls.json`, `native-first-frame-evidence.json`,
and both capture directories. Render-graph state warnings remain visible in the logs.
The diagnostic report has warnings without a hard failure: velocity, emissive,
volumetric fog and TAA history include `WARN_ZERO`; depth/shadow readback is
unsupported, and GTAO/SSGI/RC/DDGI targets are skipped because their passes are
disabled. Neither `WARN_ZERO` nor `SKIPPED` means verified correct rendering.

## Final native test observation

The complete isolated locked-profile suite finished in 64.38 seconds without a
timeout or crash: **7,545 passed, 84 failed, 12 skipped / 7,641**, exit 1. There
are no newly failing names relative to the previous locked-profile baseline.
The four added capture tests pass. A separate capture/VFX run passed all 29 tests.

Two test defects were corrected after reading their implementation: the mesh-stack
test assumed a freed allocation's address could not be reused; it now checks that
serialized configuration replaces mutated runtime state and disabling removes the
module. The seek timing test wrote to an obsolete absolute checkout path; it now
round-trips its JSON through a unique system temporary file. The latter failed in
the earlier canonical baseline; the allocator assertion failed intermittently in
the intermediate full run. Neither correction weakens the production contract.

Evidence: `native-final-contract-suite.json`, `native-final-contract-suite.log`,
`native-final-contract-suite-run.json`, `native-final-contract-suite-observations.json`
(including failed-name comparisons), `native-capture-vfx-targeted.json` and
`native-final-contract-source.json`. The final test executable SHA-256 is
`30f322e764057f4319c121b4c23c5022499dad6e2711dd0890c9806d8f882219`.
Its provenance adds the two test-source hashes to the earlier snapshot and capture
changes. These test-only edits do not change the captured CLI executable.

All 27 tooling regressions and 13 Falcor gate regressions also pass. Their passing
results validate gate behavior, not the renderer's unresolved comparisons.

## Acceptance boundaries and ownership

Stage 0 in [the execution plan](06-execution-plan.md) requires trustworthy build,
test discovery/results and current diagnostics. It does not require accepting
corrected rendering before those corrections are scheduled. The focused diagnostic
proposal also specifies an initial diagnostic baseline, not final visual parity.
The earlier task note treated missing matched Falcor acceptance as a baseline
closure condition; that was broader than its proposal and stage-0 scope.

The baseline evidence is now complete for audit items 1.3 and 1.5. Remaining work
stays with these existing owners:

- [Native failures](../../../.rulebook/tasks/phase0_preexisting-test-failures/tasks.md):
  the final complete locked-profile run has **84 failed and 12 skipped** tests.
  This owner retains native fixtures, Lua/scene/default-drift and cross-backend
  triage; existing VFX/editor ownership remains in place. The earlier Vulkan crash
  did not reproduce in the complete runs and is not declared fixed.
- [Render-graph state registry](../../../.rulebook/tasks/phase0_render-graph-state-registry/tasks.md):
  observed resource-state warnings and the already assigned migration/acceptance work.
- [Audit items 2.x, 3.x and 4.1](../../../.rulebook/tasks/phase0_project-stabilization-audit/tasks.md):
  native lifetime/ownership, rendering corrections and final matched Falcor/sequence
  acceptance. No matched Falcor pair or PSNR pass exists for these captures.

The five focused stage-0 tasks are complete; the umbrella remains in progress.
The user requested a commit of all current changes. No task is archived, no push
is included, and no global native quality waiver is claimed.
