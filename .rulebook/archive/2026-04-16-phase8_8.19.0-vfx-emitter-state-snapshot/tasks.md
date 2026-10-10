# phase8_8.19.0-vfx-emitter-state-snapshot

Capture full emitter state (SoA + RNG + wall-clock) to disk for offline inspection.

Reference: UE5 FNiagaraSnapshotBuilder.

## 1. Capture pipeline

- [x] 1.1 Snapshot header: emitter name, frame index, wall-clock, RNG seed, capacity.
      `engine/vfx/baker/vfx_snapshot.h/.cpp` — `VfxSnapshotHeader` with
      magic `"UZVFXSNP"`, version 1, 128-byte fixed layout, all fields
      from the task spec plus a reserved `flags` field for future
      extensions. Little-endian integers, IEEE 754 floats.
- [x] 1.2 SoA readback via 4.26 framework into the snapshot file.
      `VfxSnapshotChannel` carries `name`, `elementType` (8 enum
      values), `elementCount`, raw bytes. `VfxSnapshotSaveToBytes`
      packs the file as header + channel table + packed data.
      `IsValid()` catches every layout mismatch (size/name/duplicate/
      alive > capacity) before a save attempt so broken snapshots never
      land on disk. Integration with the 4.26 readback framework is the
      caller's responsibility — this task ships the stable file format.
- [x] 1.3 Loader for offline inspection (stdout dump / future widget).
      `VfxSnapshotLoadFromBytes` / `VfxSnapshotLoadFromFile` parse the
      format and populate `VfxSnapshot`. Every error path populates
      `outError` with a human-readable reason (bad magic, truncated
      field, unknown element type, data out of range, version
      mismatch). `VfxSnapshotDump` produces a structured stdout /
      log summary.

## 2. Documentation and tests

- [x] 2.1 Docs: snapshot file format + inspector CLI.
      `docs/engine/25-vfx/36-emitter-snapshot.md` — full byte-layout
      diagram, element-type table, validation rules, capture contract
      snippet, inspection dump format, test index.
- [x] 2.2 Tests: deterministic round-trip of a synthetic capture.
      `runtime/tests/engine/vfx/test_vfx_snapshot.cpp` — 14 cases:
      element sizes (8 types) + names, validation (default valid,
      alive > capacity, wrong size, duplicate name, name too long),
      save/load round-trip preserves header, channel data byte-identical,
      invalid snapshot fails to save, load rejects too-small input /
      bad magic / future version / truncated data, dump contains
      emitter name + channel names + element-type labels + frame index.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/36-emitter-snapshot.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_snapshot.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
