# 04. Build and validation

Return to the [index](README.md). See [observed commands](01-scope-and-evidence.md)
for the distinction between executed checks and source inspection.

## F-013 — CI does not establish a reproducible clean checkout

**Priority:** P1. **Confidence:** high for the npm lockfile defect; native bootstrap
completeness still needs a clean-environment run.

**Evidence:** [.gitignore](../../../.gitignore), line 49, ignores
`package-lock.json`; `git ls-files '*package-lock.json' '*npm-shrinkwrap.json'`
returned no files. [extension.yml](../../../.github/workflows/extension.yml),
around line 30, and [mcp.yml](../../../.github/workflows/mcp.yml) use `npm ci`.
The runtime workflow configures Zig and builds without the dependency provisioning
performed by setup scripts, while [runtime/build.zig](../../../runtime/build.zig),
around line 1328, links external libraries including Lua and FFmpeg.

**Impact:** local installed dependencies can conceal a broken clean checkout.
`npm ci` requires a lockfile or shrinkwrap, as specified by the
[npm command documentation](https://docs.npmjs.com/cli/v11/commands/npm-ci/).
Native dependency availability and changes to build/setup scripts also need
coverage in workflow triggers. Native-addon/editor integration is not established
by independent package checks.

**Correction and acceptance:** track compatible lockfiles, pin the chosen
toolchains/dependency inputs and bootstrap native prerequisites explicitly.
Validate runtime, DLL, N-API, extension and MCP from a clean disposable checkout,
with no inherited build outputs or `node_modules`. Include relevant setup and
workflow paths in CI triggers. No destructive clean-install command was run in
the user's working directories during this audit.

## F-014 — Test success can mean no tests, and coverage cannot run

**Priority:** P1. **Confidence:** high; source-confirmed and partially executed.

**Evidence:** [extension/vitest.config.mts](../../../extension/vitest.config.mts),
line 7, includes `tests/**/*.test.ts` and enables `passWithNoTests`; the inspected
extension test files live under `src/tests/*-test.ts`. The audit command exited
zero with no test files found. Both packages failed coverage due to missing
`@vitest/coverage-v8`. In [real-search.test.ts](../../../mcp/tests/integration/real-search.test.ts),
line 17 onward, an unavailable documentation database causes setup/individual
tests to return rather than assert or explicitly report an intentional skip.

**Impact:** a green command can provide no evidence for the behavior being
protected. Coverage cannot support a threshold if the provider is absent. The
MCP run in this audit did load its database and passed 68 tests; that successful
run is not being reclassified as a skip.

**Correction and acceptance:** classify native/editor integration tests separately
from unit tests, use correct discovery patterns, and fail required suites when
zero tests are collected. Install a compatible coverage provider and enforce the
configured threshold for a clearly defined scope. Missing required fixtures must
fail; optional hardware checks must report explicit skips rather than vacuous
success. Repair the extension lint failures listed in 01 as part of these gates.

Add negative controls for an empty suite and absent required database. Review
tests that substitute unconditional success for a promised GPU comparison;
`SUCCEED()` can still be appropriate in a deliberate no-crash test. Existing
native test failures remain owned by `phase0_preexisting-test-failures` and must
be rerun on the current build rather than copied from historical reports.

## F-015 — The Falcor comparison script can approve missing or sub-threshold evidence

**Priority:** P1. **Confidence:** high, source-confirmed control flow; the image
comparison was not executed in this audit.

**Evidence:** [falcor_reference_test.py](../../../scripts/falcor/falcor_reference_test.py),
lines 42, 58–63, 105–121 and 173–182. The script defines pass at 35 dB and warning
at 25 dB, resizes mismatched images, classifies missing inputs as SKIP, and only
FAIL changes `all_pass` to false. WARNING or missing inputs can therefore leave
the process exit status successful.

**Impact:** the automated gate does not enforce the project's PSNR >= 35 dB
requirement. Resampling different dimensions can also conceal a capture mismatch.
This finding concerns this script, not every visual-check script in the repository.

**Correction and acceptance:** the required comparison succeeds only with all
required inputs present, compatible capture metadata/dimensions and PSNR >= 35 dB.
Missing, incompatible and below-threshold evidence must return a non-success
status with an explicit reason. Test 24.9, 25, 34.9, 35 and identical-image cases,
missing files and mismatched dimensions. Preserve Falcor references unchanged.
Temporal stability needs its own sequence checks in addition to image similarity.

## F-016 — PowerShell quality scripts resolve the wrong repository root

**Priority:** P2. **Confidence:** high, source-confirmed path calculations.

**Evidence:** [test.ps1](../../../scripts/tools/test.ps1), lines 2 and 29,
uses the parent of `scripts/tools` as the root and constructs `scripts/runtime`.
[lint.ps1](../../../scripts/tools/lint.ps1) and
[format.ps1](../../../scripts/tools/format.ps1) share the root problem.
The lint directory list around line 24 omits `runtime/engine` and uses a C++17
include setup rather than the build's actual compilation commands.
[run_tests.ps1](../../../scripts/tools/run_tests.ps1) also resolves its runtime
path only one level upward. The Bash counterpart uses a different root calculation.

**Impact:** a developer can invoke a documented gate that misses the engine,
fails on a nonexistent path or analyzes a different configuration from the build.

**Correction and acceptance:** centralize script-relative repository discovery,
cover the actual engine layout and use the real compilation configuration where
supported. Run each script from the repository root and an unrelated working
directory, including a checkout path with spaces. Introduce a controlled failing
fixture to prove the gate inspects its intended source and propagates failure.

## F-017 — Language and toolchain contracts disagree

**Priority:** P2. **Confidence:** high, source/documentation drift.

**Evidence:** [runtime/build.zig](../../../runtime/build.zig), around line 35,
uses `-std=c++17`; [runtime/CMakeLists.txt](../../../runtime/CMakeLists.txt), around
line 15, selects C++20. [README.md](../../../README.md) advertises C++23 while
[CONTRIBUTING.md](../../../CONTRIBUTING.md) describes C++20 and a different Zig
version from the README. The active project override specifies C++23.

**Impact:** contributors and CI do not have one reliable language/toolchain
contract. A feature can compile in one path and fail in another, and setup
instructions can install the wrong compiler version.

**Correction and acceptance:** reconcile supported build paths with the project's
C++23 requirement and document the tested Zig version and any real dependency
constraints. Validate clean runtime and DLL builds, plus retained CMake consumers.
If a dependency prevents alignment, document the concrete blocker and resolve it
explicitly; do not silently lower the project contract or claim untested support.
