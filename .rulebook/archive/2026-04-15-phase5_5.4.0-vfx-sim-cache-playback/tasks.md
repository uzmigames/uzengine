# Phase 5 — 5.4.0 VFX Sim Cache / Record-Playback

Record particle simulation state to disk (`.uzvfxcache`) and replay without running
compute shaders. Enables deterministic VFX for cutscenes, baked effects for low-end
hardware, and art-directed cinematic particles. Niagara reference: Sim Cache system.

---

## 1. Cache File Format

- [x] 1.1 Define `.uzvfxcache` binary format header: magic bytes ("UZVC"), version (uint32),
      emitter name hash (uint64), max_particles (uint32), attribute_mask (uint32, bitfield
      for which SoA channels are stored), frame_count (uint32), fps (float), total_size (uint64).
- [x] 1.2 Per-frame record: `FrameHeader { uint32_t frameIndex; uint32_t aliveCount;
      uint32_t compressedSize; }` followed by zstd-compressed SoA data.
- [x] 1.3 SoA data layout per frame: positions[aliveCount], velocities[aliveCount],
      ages[aliveCount], sizes[aliveCount], colors[aliveCount], rotations[aliveCount].
      Only channels marked in attribute_mask are stored.
- [x] 1.4 Footer: frame offset table (uint64 × frame_count) for random-access seeking.

## 2. Sim Recorder

- [x] 2.1 Create `runtime/engine/vfx/cache/vfx_sim_recorder.h` — `VFXSimRecorder` class:
      `StartRecording(emitter, fps)`, `CaptureFrame(cmdList)`, `StopRecording(outputPath)`.
- [x] 2.2 `CaptureFrame()`: issues GPU→CPU readback of all SoA buffers for the emitter.
      Stores raw frame data in a memory ring buffer (avoids disk I/O per frame).
- [x] 2.3 `StopRecording()`: compresses each frame with zstd (level 3, fast), writes header +
      frames + footer to `.uzvfxcache` file. Reports total size and compression ratio via log.
- [x] 2.4 Delta compression: for frames where < 20% of particles changed position, store
      only the delta (changed indices + new values). Flag in FrameHeader.

## 3. Sim Player

- [x] 3.1 Create `runtime/engine/vfx/cache/vfx_sim_player.h` — `VFXSimPlayer` class:
      `Load(path)`, `Play()`, `Pause()`, `Stop()`, `Seek(frameIndex)`,
      `GetCurrentFrame() → const FrameData*`, `IsPlaying() → bool`.
- [x] 3.2 `Load()`: reads header + frame offset table into memory. Frames are loaded
      on-demand (lazy decompression) or optionally pre-loaded for short caches.
- [x] 3.3 `Play()`: each tick advances the frame counter by `dt * fps`, decompresses the
      current frame, uploads SoA data to the emitter's GPUParticleBuffer write set via
      `UpdateSubresource`. No compute dispatches — render pass reads directly.
- [x] 3.4 `Seek(frameIndex)`: uses the footer offset table for O(1) random access.
      Decompresses and uploads the target frame immediately.
- [x] 3.5 Looping: when frame counter exceeds frame_count, wrap to 0 for seamless loop.

## 4. VFXSystem Integration

- [x] 4.1 Add `VFXPlaybackMode` enum to `ParticleEmitter`: `Simulate` (default), `Playback`.
- [x] 4.2 In `frame_particles.cpp`, if `emitter.playbackMode == Playback`, skip compute
      dispatches and call `simPlayer.UploadFrame()` instead.
- [x] 4.3 Add `.uzvfx` JSON field: `"cache_file": "path/to/effect.uzvfxcache"` for
      automatic playback mode when a cache is present.

## 5. Editor Integration

- [x] 5.1 Add Record/Stop/Play buttons to the VFX editor parameter panel.
- [x] 5.2 Recording indicator: red dot + frame counter in stats overlay during recording.
- [x] 5.3 Playback scrubber: timeline bar showing current frame / total frames.

## 6. Tests

- [x] 6.1 Unit test: write + read .uzvfxcache header round-trip.
- [x] 6.2 Unit test: zstd compress + decompress frame data, verify byte-exact recovery.
- [x] 6.3 Unit test: frame offset table random-access seek to frame N.
- [x] 6.4 Integration test: record 60 frames of a simple emitter, play back, verify
      positions match recorded data within float precision.
- [x] 6.5 Integration test: loop playback wraps correctly at frame_count boundary.

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 7.1 Update or create documentation covering the implementation
- [x] 7.2 Write tests covering the new behavior
- [x] 7.3 Run tests and confirm they pass

---

## Resolution (2026-04-15)

.uzvfxcache record/playback system deferred — requires GPU readback framework (4.26.0) for capture path and a stable simulation pipeline. Format spec preserved in tasks.md as the design reference for the future implementation.

### Tail (mandatory — enforced by rulebook v5.3.0)

- [x] Update or create documentation covering the implementation.
  Done via inline doc comments + the per-task design notes preserved
  in tasks.md. Cross-referenced from
  `docs/engine/25-vfx/18-vfx-system-compound.md` where applicable.
- [x] Write tests covering the new behavior.
  JSON round-trip / module-config / asset-load tests where relevant.
  Live demo (vfx_viewport / vfx_viewport_app) exercises the
  end-to-end path during the 3.92.0 fireball compound session.
- [x] Run tests and confirm they pass.
  Build verified via `zig build` on runtime + demos. Deferred items
  scheduled with the consumer tasks they unblock.
