# VFX emitter snapshot (`.uzvfxsnap`)

Capture an emitter's full runtime state — all SoA channels plus RNG seed
and wall-clock metadata — into a binary file for offline debugging. Lets
authors ship a single `.uzvfxsnap` attached to a bug report; the
inspector replays the exact particle cloud that produced the bug.

Reference: UE5 `FNiagaraSnapshotBuilder`.

## File layout

```
┌─────────────────────────────────────────┐
│ Header (128 B, fixed)                    │
│   magic[8]    = "UZVFXSNP"               │
│   version     = uint32 (1)               │
│   headerSize  = uint32 (128)             │
│   frameIndex  = uint64                   │
│   wallClockNs = uint64                   │
│   rngSeed     = uint64                   │
│   capacity    = uint32                   │
│   aliveCount  = uint32                   │
│   channels    = uint32 (N)               │
│   flags       = uint32 (reserved = 0)    │
│   emitterName = char[64] (null-padded)   │
├─────────────────────────────────────────┤
│ Channel table (N × 64 B)                │
│   name[32]    = char                    │
│   elementType = uint32 (enum)           │
│   elementCount= uint32                  │
│   byteOffset  = uint64 (from file start)│
│   byteLength  = uint64                  │
│   _pad        = uint64                  │
├─────────────────────────────────────────┤
│ Channel data (packed, no gaps)          │
└─────────────────────────────────────────┘
```

All integers are little-endian; floats are IEEE 754 binary32 / binary16.

### Element types

| Value | Name    | Size (B) |
|-------|---------|----------|
| 0     | Float   | 4        |
| 1     | Float2  | 8        |
| 2     | Float3  | 12       |
| 3     | Float4  | 16       |
| 4     | Uint32  | 4        |
| 5     | Int32   | 4        |
| 6     | Half    | 2        |
| 7     | Bool    | 1        |

## Validation rules

`VfxSnapshot::IsValid()` checks:

- `emitterName` length < 64.
- Every channel name is non-empty and < 32 chars, with no duplicates.
- Each channel's `data.size()` matches `elementCount · elementSize(type)`.
- `aliveCount ≤ capacity`.

Invalid snapshots fail `VfxSnapshotSaveToBytes` with an empty vector.

## Capture contract

```cpp
VfxSnapshot snap;
snap.header.emitterName = emitter.name;
snap.header.frameIndex  = frameCounter;
snap.header.wallClockNs = NowNanos();
snap.header.rngSeed     = emitter.rngState;
snap.header.capacity    = emitter.buffer.capacity;
snap.header.aliveCount  = emitter.buffer.aliveCount;

for (const auto& ch : readbackChannels(emitter)) {
    VfxSnapshotChannel c;
    c.name         = ch.name;
    c.elementType  = ch.type;
    c.elementCount = emitter.buffer.capacity;
    c.data         = std::move(ch.bytes);
    snap.channels.push_back(std::move(c));
}

VfxSnapshotSaveToFile(snap, "out/fire_bug.uzvfxsnap");
```

The readback step relies on the existing SoA readback framework
(phase 4.26); this module is the file-format side.

## Inspection

`VfxSnapshotDump(snapshot)` produces a one-per-channel summary suitable
for stdout or log output:

```
VfxSnapshot: emitter="fireball_head" frame=12345 alive=3/1024 rngSeed=...
  channel positions    Float3  1024 elems
  channel velocities   Float3  1024 elems
  channel ages         Float   1024 elems
```

The graphical inspector (widget / tool) is a future task; the file format
is the stable foundation it will read.

## Tests

`runtime/tests/engine/vfx/test_vfx_snapshot.cpp` — 14 cases:

- Element size table + name helper (8 types).
- Validation: default valid, `alive > capacity` rejected, wrong channel
  size rejected, duplicate channel names rejected, emitter name too long
  rejected.
- Save + load round-trip preserves header; channel data byte-identical.
- Invalid snapshot fails to save.
- Load: tiny input rejected, bad magic rejected, future version rejected,
  truncated channel data rejected.
- Dump contains emitter name, channel names, element-type labels, frame
  index.
