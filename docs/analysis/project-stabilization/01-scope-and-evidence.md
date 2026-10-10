# 01. Scope and evidence

Date: 2026-10-10. Scope: the local UzEngine working tree reviewed in this session.
Return to the [index](README.md).

## Revision and limits

The checkout HEAD was `19ffb9b94333f56d624eb1485192cf5c6d174bd0`. The working tree
contained thousands of staged or modified files, including later rendering work;
HEAD alone does **not** identify the reviewed source. Paths and line numbers in
this analysis refer to that working tree and must be rechecked before fixing.
No immutable source snapshot or GPU capture bundle was produced by this audit.

Six merge conflicts seen during the initial inspection were resolved by other
workspace activity before the review ended. They are not listed as current
blockers. This review did not resolve or revert those changes.

The review covered runtime/renderer and RHI code, shaders, ECS and scene loading,
N-API/editor communication, asset I/O, build definitions, CI, tests and project
documentation. It is a broad targeted review, not a claim that every source line,
backend, shader permutation or third-party library was verified.

## Evidence vocabulary

- **Source-confirmed:** the relevant code path establishes the stated behavior.
  This does not establish that a user's visible symptom came from that path.
- **Executed:** a command ran during the audit and produced the recorded result.
- **Risk to measure:** a code property is visible, but its cost or manifestation
  needs a controlled runtime experiment.
- **Recommendation:** an improvement to maintainability or verification, not a
  reproduced runtime failure.

Confidence is stated per finding. Priority reflects expected impact and ordering,
not a measured rate of failures: P1 correctness/validation blockers; P2 capability,
robustness or measured-performance work; P3 maintainability after correctness.

## Checks actually executed

Commands below run from the indicated directory. These results came from the
initial audit; organizing the report did not rerun the engine test suite.

| Directory | Command/check | Observed result |
|---|---|---|
| `extension` | `node node_modules/typescript/bin/tsc --noEmit -p .` | Passed |
| `mcp` | `node node_modules/typescript/bin/tsc --noEmit -p .` | Passed |
| `extension` | `node node_modules/eslint/bin/eslint.js src/` | Failed: 6 errors, 1 warning |
| `mcp` | `node node_modules/eslint/bin/eslint.js src/` | Passed |
| `extension` | `node node_modules/vitest/vitest.mjs run --reporter=dot` | Exit 0, **no test files found** |
| `mcp` | `node node_modules/vitest/vitest.mjs run --reporter=dot` | 7 test files, 68 tests passed; documentation database loaded |
| `extension`, `mcp` | `node node_modules/vitest/vitest.mjs run --coverage` | Both failed: missing `@vitest/coverage-v8` |
| Sample assets | JSON parsing of 191 `.uzproject`, `.uzscene`, `.uzmat`, `.uzmaterial`, `.uzfx` files | No syntax errors; no schema or semantic validation implied |
| Python scripts | AST parsing of 161 `.py` files | No syntax errors; scripts were not executed by this check |
| Repository | `git diff --name-only --diff-filter=U` at review close | No unmerged paths |

Extension lint errors were `no-require-imports` at `runtime-worker.ts:95,96,100,140`
and explicit `any` at `src/tests/viewport-input-test.ts:28` and
`src/tests/viewport-streaming-test.ts:30`; the warning was an unused eslint-disable
at `runtime-worker.ts:165`. These are verification results, not rendering causes.

## Checks not executed

- No current C++ runtime, DLL or demo build: Zig was not found in the inspected
  command paths or local tool locations. This is an environment limitation, not
  proof that the source cannot compile.
- No current runtime unit-test run, DX12 debug-layer run, GPU validation, RenderDoc
  capture, pass dump or pixel-debug experiment.
- No Falcor comparison or measured PSNR for this source tree.
- No runtime reproduction of ECS churn, editor races, scene-load rollback,
  moving-object ghosting or unsupported-upscaler output.
- Existing runtime binaries dated July 27 were not treated as evidence for the
  later source in the working tree.

Older task reports remain historical evidence with their own revisions and
hardware. Their pass counts and visual conclusions are not imported as results of
this audit. A successful TypeScript check does not validate the native addon.

## Requirements for the next measurement

Record the commit and a manifest/hash identifying local changes, toolchain,
build configuration, backend, GPU/driver, executable hash, scene/asset identities,
camera, dimensions, CVars, warmup and capture frames, exposure/tonemapping and
reference version. Keep commands, logs, images and metrics together.

Before shader changes, obtain the project's diagnostic output (`--pass-dump`,
`--diag`, `--pixel-debug` or `--vis`) appropriate to the defect and consult the
published rendering reference. Keep Falcor unchanged and require PSNR >= 35 dB
for applicable matched-reference comparisons. Temporal behavior additionally
needs consecutive-frame and motion checks; a single-image score is insufficient.
