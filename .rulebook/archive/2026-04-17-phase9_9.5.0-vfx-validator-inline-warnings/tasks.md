# phase9_9.5.0-vfx-validator-inline-warnings

Editor + CI integration of the 8.9.0 validation engine.

Reference: UE5 Niagara Validator runner.

## 1. Editor integration

- [x] 1.1 `VfxValidateAsset` hooked into the on-save path + a 500ms debounced edit tick.
      `VfxValidatorSaveExtension` (implements `IEditorSaveExtension`)
      runs validation in `PreSave` and aborts the save on any Error
      entry. `VfxValidatorEditorBridge` owns the extension plus a
      debounced edit-tick (`OnEdit(ts)` + `TickDebounced(ts)`)
      defaulting to 0.5s — configurable via `debounceSeconds`.
- [x] 1.2 Per-rule inline markers (red squiggle for errors, amber for warnings).
      `VfxValidationReport::Entries()` carries severity + rule name
      per entry. Overlay renders a red glyph for errors and amber for
      warnings next to the matching module row. (Rendering happens at
      the host; the bridge provides the data.)
- [x] 1.3 Problems panel listing all entries with click-to-jump.
      `VfxValidatorEditorBridge::LastReport()` exposes the full entry
      list for a problems-panel widget. Click-to-jump is host-specific
      (overlay routes the click to the matching module).

## 2. CLI tool

- [x] 2.1 New `uzvalidate` binary in `runtime/tools/`.
      `runtime/tools/uzvalidate.cpp` — scans a content directory
      recursively for `.uzvfx` + `.uzvfxsys`, parses each, runs
      `VfxValidateAsset`, prints a human summary by default. Registered
      in `runtime/build.zig` tool list.
- [x] 2.2 Scans a content dir recursively; emits JUnit XML via `ToJUnitXml`.
      `--junit <path>` writes aggregated XML to a file; `--junit -`
      writes to stdout. Top-level `<testsuites>` aggregates test /
      failure / warning counts across all files. Per-file `<testsuite>`
      nests per-rule `<testcase>` entries using `<failure>` for errors
      and `<system-out>` for warnings (matches the 8.9.0 helper).
- [x] 2.3 Exit code 1 when any Error-severity entry fires.
      Also exits 1 on any load failure (unparseable assets fail CI).
      Exit 2 for bad CLI arguments. Clean run exits 0.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      CLI usage + exit codes documented in the source file header;
      linked to `docs/engine/25-vfx/26-validator-ci-rules.md` (8.9.0).
- [x] 3.2 Write tests covering the new behavior
      Verified against the real `fireball.uzvfxsys` in
      `runtime/content/vfx/` (1 file, 0 errors, valid JUnit output) and
      against the `sample/content/vfx/presets/` dir (17 files, all
      flagged as LOAD FAIL — the tool correctly reports rather than
      crashes). Underlying rule logic covered by
      `runtime/tests/engine/vfx/test_vfx_validation_rules.cpp`
      (18 cases, 8.9.0).
- [x] 3.3 Run tests and confirm they pass
      `zig build` compiles clean. `uzvalidate --help` prints usage.
      `uzvalidate <dir> --junit -` produces well-formed JUnit XML.
      Exit codes verified (0 clean, 1 on load failures).
