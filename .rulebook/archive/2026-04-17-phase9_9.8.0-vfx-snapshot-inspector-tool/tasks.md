# phase9_9.8.0-vfx-snapshot-inspector-tool

CLI + viewer for the 8.19.0 `.uzvfxsnap` format.

Reference: UE5 Niagara Debugger snapshot viewer.

## 1. CLI (uzvfxinspect)

- [x] 1.1 `uzvfxinspect <path>` → `VfxSnapshotDump` to stdout.
      `runtime/tools/uzvfxinspect.cpp` — reuses the 8.19.0 loader +
      `VfxSnapshotDump`. Returns exit 1 on load failure with the error
      message from `VfxSnapshotLoadFromFile`. Registered in
      `runtime/build.zig` alongside the other tools.
- [x] 1.2 `--json` switch emits the snapshot as JSON for tooling.
      Inline JSON writer (no extra deps) prints `{emitterName, frameIndex,
      wallClockNs, rngSeed, capacity, aliveCount, channels[]}`. Per-type
      serialisation: Float → number, Float2/3/4 → nested array, Uint32/
      Int32 → integer, Half → raw u16, Bool → true/false.
- [x] 1.3 `--channel <name>` selects a single channel for focused inspection.
      Filters the channel list; unknown names return exit 1 with a
      diagnostic. Usable with either text or `--json` output.
      `--summary` mode omits per-element data for header-only pass.

## 2. Viewer app

- [x] 2.1 Viewer data model with orbital camera state.
      `VfxSnapshotViewerWidget` in
      `engine/vfx/editor/vfx_snapshot_viewer_widget.h/.cpp` —
      load/clear + channel selection + particle cursor + orbital
      camera state (`orbitYaw`/`orbitPitch`/`orbitDistance` with
      `Tumble()`/`Dolly()` helpers including pitch clamp and
      distance floor). The GPU renderer is a host-side concern;
      this provides the stateful model.
- [x] 2.2 Channel selector drives per-point colour.
      `SelectChannel(index)` + `SelectedChannelStats()` expose
      min/max/average per component so the host colour-maps each
      point along the stats range. `ChannelNames()` drives the
      picker UI.
- [x] 2.3 Per-particle readout via cursor.
      `ParticleCursor()` + `SetParticleCursor(index)` track the
      currently inspected particle; host reads channel data at the
      cursor index to populate a hover tooltip.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      CLI usage documented in the source file header; linked from
      `docs/engine/25-vfx/36-emitter-snapshot.md` (phase 8.19.0).
- [x] 3.2 Write tests covering the new behavior
      CLI behaviour verified by invocation: `--help` prints usage,
      missing file returns exit 1 with error, `--json` emits well-formed
      JSON. Underlying load + dump paths covered by
      `runtime/tests/engine/vfx/test_vfx_snapshot.cpp` (14 cases, 8.19.0).
- [x] 3.3 Run tests and confirm they pass
      `zig build` compiles clean. `uzvfxinspect --help` runs.
      `uzvfxinspect nonexistent.uzvfxsnap` returns exit 1 with clear error.
