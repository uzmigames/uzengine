# 16. Audit closure verification, 2026-10-17

Return to the [index](README.md), [revision ledger](14-closed-findings-ledger.md)
and [finding-to-test inventory](15-finding-test-inventory.md).

## Scope and provenance

Base revision: [28473ec4](https://github.com/uzmigames/uzengine/commit/28473ec4),
branch `feat/phase0-stabilization`. The F-003 corrections described below are an
uncommitted working-tree increment; no landed SHA is claimed for them. Existing
VFX-editor, cloud and scratch changes are not part of this increment.

The compiler is `.tools/zig-x86_64-windows-0.15.2/zig.exe` (version 0.15.2).
The workstation PATH instead resolves Zig 0.16.0; native commands explicitly put
the pinned compiler first. Every build, test and capture has a 180-second maximum.
Native LSP diagnostics are unavailable because clangd is not installed; real
compiler and test diagnostics are used instead.

## F-003: residual graph contracts

The archived registry task's import/manual-state migrations remain accepted at
[466df62e](https://github.com/uzmigames/uzengine/commit/466df62e), with fresh
texture/depth regressions at
[a5ac6275](https://github.com/uzmigames/uzengine/commit/a5ac6275).
Its clean imported-resource matrix did not prove the residual contracts.

Fresh source inspection found all three residuals still present:

- `RgBuilder::ScheduleBarriers` ignored equal UAV states and `Execute` discarded
  same-state barriers, although DX12 already translates UAV-to-UAV into a memory
  barrier. The builder now orders dependent UAV accesses between passes; successive
  read-only UAV accesses do not add a barrier. An omitted-barrier negative control
  produces two detected ordering errors while the normal chain produces none.
- Transients were scheduled from Undefined and the pool forgot their physical
  final states. The pool now owns an `RgStateRegistry`; acquisition resolves
  creation/reuse states and execution reschedules barriers before callbacks.
  Release preserves final states, including across graphs on one command list;
  submission-count tracking handles buffer decay. Trim forgets destroyed handles.
- LightCull substituted an unused depth read for the cluster-buffer dependency.
  It now declares the tracked cluster buffer as a UAV write; the production
  DeferredLighting pass declares the same deduplicated resource as an SRV read.
  Internal raw cluster transitions are removed.

The pool does not alias within a graph: every live resource is acquired before
the first pass and released after the last. Its header now states the actual
same-queue reuse and caller-owned GPU-idle destruction contract, rather than
claiming an unimplemented fence-retirement/within-graph aliasing mechanism.

Nine new `RenderGraphResidual.*` tests cover UAV dependency/omission, read-only
UAVs, render-target/UAV/depth texture first use and reuse, buffer same-list reuse
and submission decay, distinct simultaneous transients, and the production
`AddLightingPass` cluster-buffer consumer. The ninth case covers a read-only UAV
pass followed by a read/write UAV pass in both declaration orders.

## Gates

| Gate | Result | Retained diagnostics |
|---|---|---|
| Pinned Zig: `cd runtime && zig build -Doptimize=ReleaseFast` | Final reviewed-source exit 0 (`NATIVE_BUILD_EXIT=0`); shader integrity 144/144 | `E:/UzmiEngine/audit-closure-build-reviewed.log` |
| Headful `lights-reference.uzproject --gpu-validation`, 32 requested frames | Reviewed-source exit 0; manifest records 36 rendered frames including 4 preload; GPU-based validation explicitly enabled; IDs **527=538=607=942=0** | `E:/UzmiEngine/audit-closure-reviewed-gbv/{manifest.json,run.log,frames/f_31.png}` |
| Headful reflection-reference, 100 frames, screenshot 95 | Reviewed-source exit 0; 104 rendered including 4 preload; frame 95 **PNG byte-identical**, max_code_delta **0**, no shader `compile failed`; image inspected | `E:/UzmiEngine/audit-closure-reviewed-default/{manifest.json,run.log,frames/f_95.png}` |
| `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast` | Final reviewed-source run: **7,827 tests / 781 suites; 7,807 passed (+9 versus 7,798), 17 skipped, only 3 pre-existing VfxEditorSlate failures**; exit **1**, no new regression | `E:/UzmiEngine/audit-closure-gtests-reviewed.log`, `audit-closure-gtests-reviewed.xml` |
| `npm run prepush` = `test:layouts && build && test` | **Exit 0** on reviewed source; runtime DLL, N-API addon, extension and MCP builds completed; both TypeScript compilers exit 0 | `E:/UzmiEngine/audit-closure-prepush-reviewed.log` |
| `npm run test:layouts` (first required prepush stage) | Exit 0; **A 87/87, B 27/27, C 4/4**; all five before-fixture negative controls reject; **165 declarations: 104 mapped, 61 reasoned exclusions, zero unlisted** | Same prepush log, lines 5-142 |
| Extension / MCP Vitest (required prepush test stage) | Extension **152/152** across 7 files; MCP **79 passed / 9 skipped**, 9 files passed / 1 skipped | Same prepush log, lines 188-229 |
| `npm run lint` (extension + MCP) | Exit 0 | `E:/UzmiEngine/audit-closure-lint.log` |
| `python scripts/falcor/test_falcor_reference_gate.py` | Exit 0; 13/13 synthetic gate controls, including missing/incompatible/sub-threshold evidence | `E:/UzmiEngine/audit-closure-falcor-controls.log` |
| `python -m unittest discover -s scripts/tools -p 'test_*.py'` | Exit 0; 34 tests, 5 skipped Windows PowerShell integration cases (the suite's `pwsh` prerequisite is unavailable), 29 passed | `E:/UzmiEngine/audit-closure-tool-controls.log` |

The synthetic Falcor suite deliberately prints crash/skip/warning fixtures while
testing their rejection; those lines are not renderer runs or accepted images.
The skipped PowerShell cases are not fresh acceptance for F-016; its historical
controlled-tool evidence remains linked in the ledger.

The first engine build reached its three-minute bound and was stopped with its
compiler descendants. An incremental retry completed with exit 0; the final
post-edit build also exited 0 with an explicitly captured native process exit
code. The final reviewed-source gates are the accepted results in the table.

Both final captures used executable `fnv1a64:e6133eb1459d31de`. The GBV log creates
the LightCull pipeline and a 48,960-cluster, 6,462,720-byte list, proving that
the corrected cluster-buffer path ran. Its complete 625-line log has no barrier
errors in the required four IDs. It does contain eight allowed id-1422
CREATE_NOT_ZEROED messages and one non-barrier id-680 warning about an unbound
depth-stencil view; neither is silently represented as a wholly warning-free log.
The final GBV engine run lasted approximately 94 seconds. The earlier captures
under `audit-closure-{default,gbv}` also passed, using the pre-review executable
`fnv1a64:081f1053e91681a4`; they are retained as history, not substituted for the
final reviewed-source evidence.

The default capture matches the accepted camera, 1920x1080 render/output size,
`r.TAA=1`, hidden toolbar/tech panel and disabled volumetric fog. Its PNG and
`E:/UzmiEngine/f005-after/frames/f_95.png` are both 1,021,719 bytes with SHA-256
`0a86fc0ea5a4856584aa387cd339d3726663790fc867428f8e655eaaf6814851`.
Byte equality proves zero differing image codes without a tolerance or fit.
The default log reports 104 graphs, zero import-claim mismatches and zero double
imports. One attempted scheduling wrapper exited before launching the default
engine because querying an already-exited process returned a nonzero shell
status; the direct capture above is the accepted run.

The first native run (`audit-closure-gtests.log` / `audit-closure-gtests.xml`)
ran 7,826 tests: 7,804 passed, 17 skipped and 5 failed. In addition to the three
pre-existing VfxEditorSlate failures, two new transient tests had an incorrect
fixture: they began watching the physical texture only inside the first pass,
after the truth command list had already ignored its initial barrier as
unwatched. The corrected fixture watches a fresh, never-accessed pool allocation
before graph execution. Assertions and production synchronization are unchanged.
The corrected-source full rerun is retained separately as
`audit-closure-gtests-final.log` / `audit-closure-gtests-final.xml`.
The fixture-corrected run passed all eight then-present `RenderGraphResidual.*` cases and took 35.191
seconds in test execution. Its only failures are
`VfxEditorSlate.WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName` and
`StatusBarBindingsReflectTelemetry`, the supplied user-WIP baseline. The 17
existing skips remain explicit; none is accepted as fresh coverage of a required
GPU/editor/SDK gate. No device-removal/TDR retry was needed.

Independent static review then identified an uncovered graph-API case: after a
read-only UAV pass, a pass declaring Read before Write could skip ordering at its
first access. Scheduling now considers whether the *whole consuming pass*
writes that resource, not just whether its first declaration is a write. The
ninth regression checks both declaration orders and an omitted-barrier control
(one detected read-to-write ordering error versus zero with the barrier). No
current production pass declares a UAV read, but the general graph contract
must still be correct. The first prepush log records exit 0 before this follow-up;
it is not accepted as verification of the later code change. Final
build/render/native/prepush gates are rerun on this change. Transient barrier
counts at Compile are provisional: acquisition resolves their physical entry
states and Execute reschedules them. Reuse is restricted to the same ordered
queue, or command lists submitted in recording order.
The reviewed-source native run passes all **9/9** residual tests and reports
7,807 passes in 37.467 seconds; XML confirms exactly three failures and zero
errors. Those failures are the same three VFX names above. The delta against the
supplied 7,798-pass baseline is **+9**, with unchanged 17 skips and no TDR.

The MCP real-search integration fixture (`docs.json`) is absent; its nine tests
are explicitly skipped by the existing
[`782315ea`](https://github.com/uzmigames/uzengine/commit/782315ea) optional-fixture
policy, not counted as passing. The required mocked/server/search unit suites
pass. Existing live editor/webview, temporal no-ghost sequence, WPO and vendor-SDK
deferrals remain outside this scoped gate acceptance, as enumerated in the
inventory and original implementation items. Item 4.1 is separately **not
passed**. The native suite is a no-regression acceptance against the supplied
three-failure baseline, not an assertion that every test in the tree is green.

## Checklist disposition

Items **3.1, 6.1, 6.2 and 6.3** are checked for the requested residual correction,
current revision ledger, finding/test map and executed scoped gates. Item **4.1**
remains unchecked. No commit or archive was created. The owner must add the
eventual F-003 revision to the ledger when landing this working-tree increment.
Final scoped `git diff --check` exits 0. The final process cleanup/query exits 0
with `NO_ENGINE_TEST_ZIG_PROCESSES`; no engine, native test or Zig process remains.

## Item 4.1: Falcor matrix remains unavailable

Falcor is installed at `E:/Falcor`; `git describe --tags --always` reports `8.0`.
Installation is not the missing prerequisite. Inspection of
`E:/falcor_scene_assets` found the existing sphere roughness/HDRI and SciFiHelmet
references, not matched audit camera-cut, rigid/skinned/deformation, temporal
toggle, material-context, scene-load and resize sequences.

The committed [PT comparison harness](../../../scripts/falcor/compare_pt.py)
has five static sphere cases, 512-frame captures, a crop, a fitted single-global
energy scalar and a near-mirror known-gap exclusion. Those calibrated proxy
results cannot satisfy this audit's full execution-plan matrix. Historical
[PT results](../path-tracing/09-test-results.md#tier-5m---measurement-items-closed-2026-10-14)
also document the near-mirror geometry gap and manual camera-fly limitation.
Replaying static proxy scores would not establish fresh matched sequence parity.

Meaningful completion requires paired engine/Falcor fixtures with identical
geometry/materials, camera transforms and cut schedule, lighting/HDRI, exposure,
resolution, frame numbering and animation poses; committed sequence provenance;
consecutive-frame velocity/history diagnostics; raw and explicitly calibrated
metrics; and explicit unavailable SDK/WPO/editor-interaction cases. Existing
WPO, vendor-SDK and live-editor integration deferrals remain as such. No matched
matrix was run, no >=35-dB matrix acceptance is claimed, and item 4.1 stays open.
