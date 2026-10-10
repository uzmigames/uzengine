# 07. Baseline implementation

Follow-up: [locked native bootstrap and final isolated-build evidence](08-native-bootstrap.md).
The later report supersedes this snapshot's open native-bootstrap limitation;
engine-wide test, coverage and visual acceptance remain separate.

The approved implementation starts with the priority queue's baseline milestone,
audit items 1.1–1.5. Five focused implementation tasks retain that umbrella's
acceptance gate rather than scheduling overlapping renderer work:

| Audit item | Implementation task | Scope |
|---|---|---|
| 1.1 | [Build provenance](../../../.rulebook/tasks/phase0_stabilization-build-provenance/tasks.md) | Toolchain, dependency identities, reproducible builds |
| 1.2 | [PowerShell gates](../../../.rulebook/tasks/phase0_stabilization-powershell-gates/tasks.md) | Repository discovery, actual source scope, failure propagation |
| 1.3 | [Test integrity](../../../.rulebook/tasks/phase0_stabilization-test-integrity/tasks.md) | Real suites, required fixtures, lint and coverage |
| 1.4 | [Falcor gate](../../../.rulebook/tasks/phase0_stabilization-falcor-gate/tasks.md) | Complete, compatible evidence at PSNR >= 35 dB |
| 1.5 | [Diagnostic baseline](../../../.rulebook/tasks/phase0_stabilization-diagnostic-baseline/tasks.md) | Current-source Windows capture and provenance |

Independent tooling checks can run while native build work proceeds, as allowed
by execution-plan stage 0. Native-dependent acceptance remains gated on the build;
synthetic gate tests do not close GPU or native/editor integration acceptance.

## Build contract and provenance

The Windows baseline uses Zig 0.15.2 and C++23 for engine translation units.
Third-party libraries retain their upstream compilation settings; the N-API
consumer has its own C ABI boundary. The runtime's CMake definition also selects
C++23; this is not a claim that every CMake consumer has passed validation.

The Zig Windows archive was checked against the SHA-256 from the
[official release index](https://ziglang.org/download/index.json):
`3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c`.
Project-local tools live in ignored `.tools/`; no system compiler was replaced.

Capture input identities before a build and artifact identities afterwards:

```powershell
python scripts/tools/build_manifest.py --output .sandbox/stabilization/source-manifest.json --zig .tools/zig-x86_64-windows-0.15.2/zig.exe
python scripts/tools/build_manifest.py --output .sandbox/stabilization/build-manifest.json --zig .tools/zig-x86_64-windows-0.15.2/zig.exe --artifact runtime/build/bin/uzengine.exe --artifact runtime/build/bin/test_runtime.exe
```

The manifest includes HEAD, working-tree file hashes (including missing tracked
files), tool versions/hashes and ignored native dependency inputs. It explicitly
does not certify a build or replace command logs. Track package lockfiles for
extension, MCP and N-API; vendor lockfiles remain ignored.

C++23 exposed missing direct includes for `std::sort`, `std::clamp` and the
FidelityFX 1.1 header's `std::popcount`/C math calls. These are supplied at their
owning source or SDK boundary. DirectXMath's defaulted matrix comparisons were
implicitly deleted because the types contain unions. The eight operators are
backported from Microsoft's
[pinned upstream implementation](https://github.com/microsoft/DirectXMath/blob/e2f2b9bddbbc4fd0f6f63586f86b2279213b991d/Inc/DirectXMath.h),
without changing vector math or matrix layout.

SDK header search order must precede Zig's MinGW headers: marking all vendor
directories as system includes selected MinGW's incomplete DirectXMath and
different D3D12 declarations. The helper keeps vendor precedence and marks only
nlohmann as system. The D3D12MA translation unit retains its existing UBSan
exception and suppresses only the unused toolchain argument caused by
[Zig 0.15.2's runtime flag injection](https://github.com/ziglang/zig/blob/0.15.2/src/Compilation.zig#L6694-L6712).
Engine warnings remain errors. VFX assertion formats now match `snprintf`, and
primary-attractor insertion is checked even when shipping assertions are removed.

## PowerShell gates

`scripts/tools/quality_common.ps1` resolves the repository from its own directory.
All four entry points preserve the calling directory and PATH. Missing required
tools, dependency installs or compile databases fail instead of silently skipping.
Formatting covers `runtime/engine`, `editor`, `platform`, `tools`, `tests` and
`napi/src`. Lint uses translation units from the actual compilation database,
rejects empty source discovery and verifies that runtime engine sources occur.

```powershell
pwsh -NoProfile -File scripts/tools/test.ps1
pwsh -NoProfile -File scripts/tools/run_tests.ps1
pwsh -NoProfile -File scripts/tools/format.ps1 --check
pwsh -NoProfile -File scripts/tools/lint.ps1 -RuntimeCompileDatabase runtime/build/clang-debug/compile_commands.json -NapiCompileDatabase napi/build/compile_commands.json
```

Use a CMake generator supporting `CMAKE_EXPORT_COMPILE_COMMANDS`, such as Ninja.
An absent database is an unmet prerequisite, not permission to invent compiler
flags. The CMake test path rebuilds before running the executable, preventing a
failed configure/build from being followed by acceptance of a stale test binary.

`test_quality_scripts.py` uses controlled external-tool doubles to verify path
and exit semantics, including checkout paths containing spaces, unrelated working
directories, an engine source causing a failure, and failed native/TS commands.
These checks do not claim a real clang-tidy or clang-format pass.

## Test integrity

The extension's `src/tests/*-test.ts` files are manual native/editor harnesses.
They do not declare Vitest suites. Unit tests use `*.test.ts`; EngineMode now has
five behavior tests for persisted state, UI commands and repeated toggles.
Both required Vitest configurations fail on empty discovery. The real-search
integration suite throws when its database is unavailable, and CI generates a
database from current sources before running it.

The documentation generator resolves the repository structurally from either TS
source or compiled JS and rejects discovery of zero source files. To run without
the TS loader, use `npm run build` then `node dist/tools/generate-docs.js` in `mcp`.
`UZENGINE_TEST_DOCS_PATH` selects an explicit integration-test fixture; a missing
path must produce a failed suite and nonzero exit, even if Vitest labels its
individual tests skipped after the setup failure.

The V8 providers match the exact Vitest versions in each package. The 95% coverage
thresholds remain intact. A working coverage command that reports insufficient
coverage is a failed gate, not a completed coverage task.

## Falcor evidence contract

Install `scripts/falcor/requirements-gate.txt` with Python 3.11 or newer. Compare
existing images without launching renderers:

```powershell
python scripts/falcor/falcor_reference_test.py --uzengine-image capture.png --falcor-image reference.png --report .sandbox/stabilization/falcor-report.json
```

Each image requires a JSON sidecar with the same stem. Explicit sidecar paths
are accepted through `--uzengine-metadata` and `--falcor-metadata`. A producer
must record these fields; the gate does not invent missing provenance:

| Field | Contract |
|---|---|
| `schema_version` | Integer 1 |
| `renderer` | `UzEngine` or `Falcor`, matching the input role |
| `image_sha256` | SHA-256 of the actual image file |
| `source_manifest_sha256`, `executable_sha256` | Producer provenance, separate for each renderer |
| `scene_sha256` | Hash of the shared canonical scene/asset input manifest |
| `camera` | Object with finite 16-element `view_matrix` and `projection_matrix` arrays |
| `width`, `height` | Positive integers matching the decoded image |
| `frame` | Nonnegative integer capture frame |
| `simulation_time`, `exposure` | Finite numbers |
| `tonemapping`, `color_space` | Nonempty names of the actual capture transforms |

All scene/camera/dimension/frame/time/exposure/transform fields must match.
Images are compared as RGB at original dimensions; resampling is forbidden.
Missing, unreadable, incompatible or stale evidence fails. Scores are classified
before rounding: 34.9999 dB fails and 35 dB passes. Identical images use the
existing finite 100 dB reporting convention. Reports cannot overwrite inputs. Capture subprocesses must both exit successfully
and produce a new output; an unchanged old image fails even after exit 0.

Legacy capture/update options do not manufacture producer sidecars. A successful
renderer exit alone does not establish matched scene/camera provenance. Existing
references without the required metadata must be recaptured or documented by the
actual producer before acceptance. No Falcor rendering implementation or stored
reference is modified by these gate corrections.

## Verification commands

```powershell
python -m unittest discover -s scripts/tools -p "test_*.py"
python -m unittest discover -s scripts/falcor -p test_falcor_reference_gate.py
python scripts/tools/rulebook_queue.py --check
```

Local build and test logs live under `.sandbox/stabilization/`. Final observed
outcomes and remaining prerequisites are recorded in the tasks and the evidence
section below; incomplete work must stay open.

## Observed results — 2026-10-10

All evidence below is local to the working tree based on
`380c142e481d56fe16b045f63315b42359e86a7d`, including the uncommitted corrections.
No hosted CI run or release publication is claimed. Commands and logs are retained
in `.sandbox/stabilization/`; `build-manifest.json` inventories final source,
native inputs and executable artifacts. The earlier `source-manifest.json`
records intermediate inputs, not the final accepted source identity.

| Check | Observed outcome | Evidence |
|---|---|---|
| Zig 0.15.2 / C++23 runtime | Debug build passed, 139 shader-integrity entries | `runtime-cpp23-final.log`; final source rebuild in `runtime-final-build.log` |
| MSVC CMake runtime library | Debug static library compiled; duplicate-symbol warning removed | `cmake-final-verified.log` |
| Runtime DLL | Debug build passed with `-Ddll=true` | `runtime-dll.log`, `runtime-final-build.log` |
| Electron 39.8.0 N-API | MSVC Debug addon compiled and linked against the rebuilt DLL | `napi-final-build.log` and `napi-build/Debug/uzengine.node` |
| Independent npm installs | Fresh extension, MCP and N-API source copies accepted `npm ci` using retained locks | `clean-extension`, `clean-mcp`, `clean-napi` |
| TypeScript and lint | Extension and MCP checks passed; clean-source copies also type-checked | Package check logs |
| Extension unit behavior | 5 tests passed; empty required suite correctly exits 1 | `extension` test logs |
| MCP integration/unit behavior | 68 tests across 7 files passed with one worker; missing required fixture correctly fails the suite | `mcp-coverage-isolated.log` |
| Coverage gates | **Failed** at unchanged 95% thresholds: extension 2.55% lines; MCP 63% lines | Coverage logs; no threshold reduction |
| Tooling regressions | 19 tests passed, including controlled PowerShell failures, provenance, queue and compiled embedder roundtrip | `tools-final.log` |
| Falcor gate regressions | 13 tests passed, including real CLI exit codes and stale/mismatched evidence | `falcor-gate-final.log` |
| Targeted native behavior | 156 tests across 11 suites passed after the final rebuild | `native-final-targeted.log`, `native-final-targeted.json` |
| Full native suite | Initial run crashed; broader runs reached 6,087 successful results, 68 failures and 7 skips from the canonical runtime directory before the 180-second diagnostic limit | `native-tests.log`; `native-canonical-suite.log`, `native-canonical-suite-observations.json` |
| DX12 diagnostic launch | Exit 0, screenshot and run manifest produced on RTX 4090; warnings retained | `renderer-final-capture.log`, `renderer-final-run.json`, `pbr-final-frame5.png` |
| Matched Falcor rendering | **Unavailable**: no matching producer sidecars/reference pair was established | No PSNR acceptance is claimed |

The N-API build is a compilation result; loading the addon in Electron and editor
stress acceptance remain separate. Local Node is 22.11.0, below the 22.13 minimum
required by a transitive ESLint package; installs emitted that warning. CI uses
Node 22. Update the local toolchain before claiming identical CI prerequisites.

### MSVC embedder diagnosis

The alternate runtime build exposed C2026 in generated shader headers: `uzembed`
emitted each expanded HLSL file as one raw string. MSVC limits individual literals
to 16,380 bytes before concatenation ([Microsoft documentation](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-errors-1/compiler-error-c2026?view=msvc-170)).
The generator now emits adjacent 8,192-byte literals and chooses a delimiter
absent from the source. The regression compiles the generator and a consumer,
then verifies an 80 KB source roundtrip, including delimiter text and a long line.
Shader math and the embedded leading/trailing-newline convention are unchanged.

The first MSVC attempt was stopped after recording C2026 so the fixed generator
could be rebuilt with the already compiled objects. Logs from both attempts are
retained (`cmake-build.log`, `cmake-post-embed.log`). A serial compilation attempt was
stopped to enable compiler parallelism (`CL=/MP8`); the final result is in
`cmake-final-build.log`. CMake configuration and the
N-API build required access to the installed Windows SDK inventory outside the
workspace sandbox; that access was approved by automatic review.

The complete MSVC pass then exposed three independent causes: the lightmap UV
header omitted `<string>`; the Windows platform `process.h` shadowed the CRT
header used by `<thread>`; and CMake compiled the Brixelizer loader despite not
configuring `UZ_BRIXELIZER_ENABLED` or its SDK. The header now includes its own
string dependency, the Windows-only header is named `platform_process.h` with
both consumers updated, and CMake excludes the unconfigured loader. No wrapper
with the conflicting old name is retained. The legacy synchronous spdlog source
selection remains; its comment now records the actual historical header collision.

`cmake-fixed-build.log` records validation of these fixes. Its remaining failure
was zlib: Windows compiled libpng without zlib headers or implementation, while
the existing system-zlib lookup applied only to Apple. Windows CMake now compiles
the same 15 static zlib sources as Zig, discovers one validated extraction root,
and permits an explicit `UZ_ZLIB_SOURCE_DIR` when necessary. Missing/ambiguous
inputs fail configuration. The absent-source negative control passed
(`cmake-missing-dependency.log`). The upstream zlib CMake project is not invoked,
so its configure-time renaming of the shared `zconf.h` cannot affect Zig.
`cmake-zlib-build.log` records the final native-library build. This target is the
static CMake runtime library; Zig remains the validated runtime/DLL path and owns
the optional configured SDK integration. A CMake static-library result is not
acceptance of all CMake executables, async logging or optional GPU backends.

The successful zlib build revealed LNK4006: the renderer timing controller and
RHI swap-chain tuning helper both defined `uz::FramePacer` and
`uz::FramePacerConfig`, despite incompatible member layouts. The optimization
helper now uses `uz::AdaptiveFramePacer` / `uz::AdaptiveFramePacerConfig`; renderer
names are unchanged. Direct C++ consumers of `rhi_frame_pacer.h` must use the new
names. The C ABI is unchanged. `FramePacerIdentity` includes both headers, exercises
independent state/lifetime and links both implementations. The final MSVC library
build (`cmake-final-verified.log`) exits 0 without the duplicate-symbol warning.

### Diagnostic capture and limits

The source-built runtime rendered PBR Showcase at 1280×720 on the NVIDIA GeForce
RTX 4090, Windows driver `32.0.16.1714` (WMI inventory in `gpu-driver.json`).
Use `--hidden`, not `--headless`: the latter performs device initialization only
and does not enter the CLI render loop or produce these artifacts.

The default play-mode capture (`renderer-run.json`, `pbr-frame5.png`) is retained
as a failure observation: its camera points away from the showcase, and
`cube_rotator.lua::on_update` calls `set_rotation` without the required fifth
argument. The editor-mode capture uses explicit camera position `0,-1800,900`
and look target `0,200,100`, avoiding script motion. It displays the scene's
five cubes, three spheres and checker floor. Exact CLI, effective CVars, camera,
exposure and frame counts are recorded by the runtime manifest.

`--frames 8` is eight frames after preload; `--screenshot-frame 5` is an earliest
requested frame, not proof of the actual captured frame. The initial captures
include five preload frames and thirteen total rendered frames. Preload-frame
images, matched Falcor matrices/time/transform metadata and a validation-layer
clean run have not been established. Do not use this capture as visual acceptance.
The play-mode diagnostic report contains zero-buffer warnings and unsupported or
disabled pass readbacks. The final capture was repeated after the embedder rebuild.
`diagnostic-evidence.json` binds the executable, screenshot, manifest, driver and
20 declared scene/project inputs by SHA-256 (no missing declared inputs); this
inventory does not claim closure over assets loaded dynamically.
`renderer-final-diagnostics.json` preserves the pass report, which has warnings.
State-registry observation in the initial play-mode run logged eight distinct imported
state disagreements (55 occurrences); the existing state-registry task owns the
subsequent investigation, after the baseline gate is accepted.

### Work retained under explicit ownership

- **Build provenance / F-013:** native clean-checkout bootstrap remains open.
  `setup.ps1` clones moving vcpkg HEAD and several `master` branches, downloads
  FFmpeg `latest`, and lacks verified archive hashes. The Zig build consumes
  ignored vcpkg extracted sources, LuaJIT import libraries and FFmpeg/SDK binaries.
  Input hashes identify this machine's build, but cannot establish clean-checkout
  reproducibility. The runtime workflow still needs a pinned, validated provision
  step before native CI can be claimed. Independent npm installs are not a native
  clean-checkout result.
- **Test integrity / F-014:** required suites now fail honestly; coverage remains
  below 95%. The focused task stays open for coverage acceptance. Real
  clang-format/clang-tidy acceptance also requires installed tools and actual
  compilation databases; controlled-tool regressions prove script semantics only.
- **Native failure owner:** `phase0_preexisting-test-failures` now records the
  current crash in `VulkanBarrierGpuTest.BufferBarrier_VertexToShaderToUAVToCommon_NoValidationErrors`.
  The initial run exited `0xC0000005`; the crash bundle is
  `runtime/build/bin/crashes/2026-10-10_02-15-25/`. There is no final gtest JSON
  because the process aborted. Earlier successful tests do not establish a full
  pass. The final-source attempt from `runtime/build/bin` progressed beyond that
  point but stopped at the 180-second diagnostic limit with 70 failures; the
  original crash is not claimed reproduced or fixed by that run. Failure names,
  skips and passed-count observations are preserved in
  `native-final-suite-observations.json`. Working-directory and runtime dependency
  availability differ between these observations. Crash-handler fixture labels
  such as `TestGPU` are not GPU provenance.
- **Diagnostic baseline:** preliminary current-source images exist, but acceptance
  remains dependent on build/test prerequisites. The default scene/script issue,
  state warnings and missing matched reference are observations, not shader fixes.

The canonical-directory rerun (`cwd=runtime`) likewise exceeded the 180-second
diagnostic limit after 6,087 successful results, 68 failures and 7 skips. These
broader runs preceded the final frame-pacer rename; the final targeted run has
156 passing tests, including the new coexistence regression. No full-suite pass
or resolution of the historical Vulkan crash is inferred. The failed assertion
lists retain distinct VFX/editor, Lua/scene and default-drift cases for their
existing owners, rather than treating one old crash as the entire backlog.

The overall baseline milestone and audit remain **in progress**. Completed gate
implementation is distinct from passing the engine-wide quality thresholds.
