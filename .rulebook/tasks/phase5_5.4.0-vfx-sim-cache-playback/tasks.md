# Phase 5 — 5.4.0 VFX Sim Cache / Record-Playback

Record particle simulation state to disk (`.uzvfxcache`) and replay without running
compute shaders. Enables deterministic VFX for cutscenes, baked effects for low-end
hardware, and art-directed cinematic particles. Niagara reference: Sim Cache system.

---

## 1. Cache File Format

- [ ] 1.1 Define `.uzvfxcache` binary format header: magic bytes ("UZVC"), version (uint32),
      emitter name hash (uint64), max_particles (uint32), attribute_mask (uint32, bitfield
      for which SoA channels are stored), frame_count (uint32), fps (float), total_size (uint64).
- [ ] 1.2 Per-frame record: `FrameHeader { uint32_t frameIndex; uint32_t aliveCount;
      uint32_t compressedSize; }` followed by zstd-compressed SoA data.
- [ ] 1.3 SoA data layout per frame: positions[aliveCount], velocities[aliveCount],
      ages[aliveCount], sizes[aliveCount], colors[aliveCount], rotations[aliveCount].
      Only channels marked in attribute_mask are stored.
- [ ] 1.4 Footer: frame offset table (uint64 × frame_count) for random-access seeking.

## 2. Sim Recorder

- [ ] 2.1 Create `runtime/engine/vfx/cache/vfx_sim_recorder.h` — `VFXSimRecorder` class:
      `StartRecording(emitter, fps)`, `CaptureFrame(cmdList)`, `StopRecording(outputPath)`.
- [ ] 2.2 `CaptureFrame()`: issues GPU→CPU readback of all SoA buffers for the emitter.
      Stores raw frame data in a memory ring buffer (avoids disk I/O per frame).
- [ ] 2.3 `StopRecording()`: compresses each frame with zstd (level 3, fast), writes header +
      frames + footer to `.uzvfxcache` file. Reports total size and compression ratio via log.
- [ ] 2.4 Delta compression: for frames where < 20% of particles changed position, store
      only the delta (changed indices + new values). Flag in FrameHeader.

## 3. Sim Player

- [ ] 3.1 Create `runtime/engine/vfx/cache/vfx_sim_player.h` — `VFXSimPlayer` class:
      `Load(path)`, `Play()`, `Pause()`, `Stop()`, `Seek(frameIndex)`,
      `GetCurrentFrame() → const FrameData*`, `IsPlaying() → bool`.
- [ ] 3.2 `Load()`: reads header + frame offset table into memory. Frames are loaded
      on-demand (lazy decompression) or optionally pre-loaded for short caches.
- [ ] 3.3 `Play()`: each tick advances the frame counter by `dt * fps`, decompresses the
      current frame, uploads SoA data to the emitter's GPUParticleBuffer write set via
      `UpdateSubresource`. No compute dispatches — render pass reads directly.
- [ ] 3.4 `Seek(frameIndex)`: uses the footer offset table for O(1) random access.
      Decompresses and uploads the target frame immediately.
- [ ] 3.5 Looping: when frame counter exceeds frame_count, wrap to 0 for seamless loop.

## 4. VFXSystem Integration

- [ ] 4.1 Add `VFXPlaybackMode` enum to `ParticleEmitter`: `Simulate` (default), `Playback`.
- [ ] 4.2 In `frame_particles.cpp`, if `emitter.playbackMode == Playback`, skip compute
      dispatches and call `simPlayer.UploadFrame()` instead.
- [ ] 4.3 Add `.uzvfx` JSON field: `"cache_file": "path/to/effect.uzvfxcache"` for
      automatic playback mode when a cache is present.

## 5. Editor Integration

- [ ] 5.1 Add Record/Stop/Play buttons to the VFX editor parameter panel.
- [ ] 5.2 Recording indicator: red dot + frame counter in stats overlay during recording.
- [ ] 5.3 Playback scrubber: timeline bar showing current frame / total frames.

## 6. Tests

- [ ] 6.1 Unit test: write + read .uzvfxcache header round-trip.
- [ ] 6.2 Unit test: zstd compress + decompress frame data, verify byte-exact recovery.
- [ ] 6.3 Unit test: frame offset table random-access seek to frame N.
- [ ] 6.4 Integration test: record 60 frames of a simple emitter, play back, verify
      positions match recorded data within float precision.
- [ ] 6.5 Integration test: loop playback wraps correctly at frame_count boundary.

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
