# Locked Windows native bootstrap

Date: 2026-10-10. Owner: `phase0_stabilization-build-provenance`, audit item 1.1.
This continues the [initial baseline implementation](07-baseline-implementation.md).

## Implemented behavior

`scripts/setup-native.ps1` provisions a Windows x64 profile into
`runtime/thirdparty/.native` without replacing existing vendored sources. Python
3.11+, MSVC Build Tools and Windows SDK 10.0.26100.0 are prerequisites. The script
uses `scripts/native-dependencies.lock.json` and verifies every downloaded archive
before extraction. The full installed file inventory is hashed in `provision.json`;
verification rejects changed inputs, missing files, modified files and extra files.
An existing incomplete profile is not silently accepted or erased. Select another
`-Destination` when deliberately changing the lock.

The profile contains zlib 1.3.1, zstd 1.5.7, Agility 1.619.2, DXC v1.9.2602 and
FFmpeg 8.1 from the dated BtbN autobuild `2026-03-31-13-11`. Headers, import libraries
and DLLs come from the same package. The Windows SDK FXC DLL is checked against its
locked SHA-256. LuaJIT is built in staging from an explicit list of repository
source files; generated leftovers in the developer tree are not inputs. The build
log and output hashes are retained. MSVC and SDK prerequisites still need to be
installed on the host; this is not a claim of bit-identical binaries across MSVC
versions or build paths.

The isolated source build exposed three bare gitlinks with no `.gitmodules`:

| Source | Commit already recorded in the Git index |
|---|---|
| EnTT | `b4e58bdd364ad72246c123a0c28538eab3252672` |
| fastgltf | `d4734a7e55d8d25155e755d895e018e423845cd0` |
| meshoptimizer | `47aafa533b439a78b53cd2854c177db61be7e666` |

These are restored into the isolated profile from commit-addressed, hashed
archives. simdjson 3.9.4 is provisioned alongside them, matching fastgltf's declared
dependency. Existing ordinary vendored sources, including libpng/libjpeg generated
configuration headers, remain revision-controlled inputs. The older `setup.ps1`
vendor-refresh installer still has moving downloads; it is explicitly outside
the locked baseline path. The runtime workflow now provisions and selects the lock.

## Build selection

```powershell
pwsh -NoProfile -File scripts/setup-native.ps1
pwsh -NoProfile -File scripts/setup-native.ps1 -VerifyOnly
cd runtime
zig build '-Dnative-deps=thirdparty/.native' -Ddll=true
zig build test '-Dnative-deps=thirdparty/.native'
```

Quote the `-Dnative-deps` argument in PowerShell. `-Offline` requires a complete,
verified download cache; `-Cache` selects its location. Relative PowerShell path
arguments are resolved against the caller's PowerShell location. Paths with spaces
are covered by a command-level regression.

Zig uses the chosen profile for compilation, linking and DLL deployment. A missing
profile or required FFmpeg deployment file fails the build. Optional DLSS and
Brixelizer binaries are excluded from this baseline profile; builds without
`-Dnative-deps` retain the existing developer SDK configuration. Revision-controlled
Brixelizer headers remain necessary because the current loader source includes
them unconditionally; excluding those headers alone would break compilation.

CMake accepts `-DUZ_NATIVE_DEPS_ROOT=<absolute-profile-path>` for the same dependency
selection. The N-API post-build copy uses DLLs deployed by the selected runtime;
it no longer overwrites them with a second copy from the legacy FFmpeg directory.

## Claude review and root causes

At the user's request, Claude Code (reported model `claude-sonnet-5-5`) performed
read-only reviews of the bootstrap and integration. The parent verified findings
against code and the actual Git index before editing. Useful findings incorporated:

- Existence checks do not establish dependency identity; mutable downloads and
  partial directories must not count as a provisioned checkout.
- Required-file lists must cover the DLLs and source files actually used by the build.
- LuaJIT inputs must exclude generated leftovers; compiler override variables
  must not silently alter the build.
- Gitlinks cannot be restored without repository mappings; the locked profile
  must provide their exact revisions independently.

Actual provisioning also exposed two Windows launcher issues. Python's native
argument quoting escaped the nested `cmd /c` string incorrectly. A generated `.cmd`
launcher now passes paths through environment variables and propagates errors.
`NoDefaultCurrentDirectoryInExePath` prevented upstream tools from finding their
freshly built helpers; the child PATH explicitly includes the trusted staging
directory, preserving that restriction. A zstd test-only symlink is excluded by
extracting the library and license subtree; selected links and traversal paths fail.

Review reports and command logs are retained under `.sandbox/stabilization/`:
`claude-bootstrap-review.txt`, `claude-provision-code-review.txt` and
`claude-clean-input-review.txt`. The reviews were advisory; unverified suggestions
were not treated as acceptance evidence.

## Observed validation

- All nine package URLs were downloaded into a separate empty cache and passed
  their locked hashes (`native-download-verification.json`).
- Eight provisioning regressions passed: offline cache handling, corruption,
  paired headers/binaries, missing DLL, traversal, selected archive links,
  installed-file drift, Windows paths/compiler failure and generated-source exclusion
  are covered across those cases.
- All 27 tooling tests and 13 Falcor gate tests passed
  (`tools-bootstrap-final.log`, `falcor-bootstrap-final.log`).
- A source-only copy was populated from current tracked and nonignored source
  files, excluding installed packages, native outputs and caches. The missing
  gitlinks were discovered by compiling that copy, not by copying their ambient
  directories. It has an independent Zig cache and newly built LuaJIT.
- The complete native suite on the preceding developer-profile binary finished:
  **7,637 tests: 7,536 passed, 85 failed, 16 skipped**, exit 1 in 197.24 seconds.
  `native-complete-suite.json`, `native-complete-suite-run.json` and
  `native-complete-suite-observations.json` retain assertions, executable identity
  and failed names. This replaces timeout-limited observations, not the failures.

The native failure owner retains the failed VFX/editor, Lua/scene/default-drift,
Metal and cross-backend shader cases. The historical Vulkan crash did not recur
in this full observation; it is not declared fixed. Coverage remains below the
unchanged 95% thresholds and matched Falcor producer evidence is still unavailable.
The overall baseline remains in progress.

## Final isolated-build results

The complete locked profile contains **965 files**. `native-clean-source.json`
identifies the source-only snapshot with fingerprint
`47e4d8b8a040c38064c36491cd7ddd1ef04e6ae855ceb0459fb54d4ea799a9c1`.
`native-clean-artifacts.json` binds the snapshot, native profile and lock to the
produced executables, DLL/import library, addon and JS entries.

| Check | Result | Evidence under `.sandbox/stabilization/` |
|---|---|---|
| Zig 0.15.2 Debug runtime, DLL and test executable | Passed with an independent cache and the locked profile | `native-clean-build-complete.log` |
| MSVC CMake static runtime consumer | Passed with `UZ_NATIVE_DEPS_ROOT` | `native-clean-cmake-configure.log`, `native-clean-cmake-build.log` |
| Electron 39.8.0 N-API build | Passed; deployed runtime/FFmpeg DLL hashes match the selected profile | `native-clean-napi-build.log`, `native-clean-artifacts.json` |
| Extension and MCP | Fresh installs, compilation, lint and 5 + 68 tests passed | `native-clean-*-node22.log`, `native-clean-*-lint.log` |
| Complete native suite, locked profile | **7,540 passed, 85 failed, 12 skipped / 7,637**, exit 1 in 150.62 seconds | `native-clean-suite.json`, `native-clean-suite-run.json`, `native-clean-suite-observations.json` |
| Native regression subset affected by build corrections | All **156** passed inside the complete run | Same GoogleTest JSON; no extra exclusions |
| DX12 PBR capture using the new executable | Exit 0; image and manifest produced; diagnostic warnings retained | `native-clean-renderer-result.json`, `native-clean-renderer-run.json`, `native-clean-frame5.png`, `native-clean-renderer-diagnostics.json` |

The isolated native run has exactly the same 85 failed names as the preceding
developer-profile run. Four formerly skipped cases executed successfully; this is
an observation, not a repair claim. MSVC's existing warnings remain visible. This
validates the static-library consumer, not every CMake executable or optional SDK.

The renderer explicitly consumes the existing PBR Showcase project/assets via
`--project`, separately from the source-only build snapshot. Its image shows five
cubes, three spheres and the checker floor at 1280×720 on RTX 4090. There are five
preload frames and thirteen total frames; the screenshot is after preload. The
diagnostic report has no reported failure and does have warnings. No matched
Falcor/PSNR or validation-layer-clean acceptance is inferred.

Node **22.23.3** was installed in `.tools`, checked against the official
[release checksums](https://nodejs.org/dist/v22.23.3/SHASUMS256.txt), and used for
final JS installs, lint and tests. `node-toolchain.json` retains its identity;
extension/MCP workflows now pin the same version. The system Node was not replaced.
Hosted CI, Electron addon loading, editor stress, engine-wide coverage and visual
acceptance remain separate gates.
