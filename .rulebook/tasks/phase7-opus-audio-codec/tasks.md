## 1. vcpkg and Build System Integration

- [ ] 1.1 Add `"libopus"` to the `dependencies` array in `vcpkg.json` with the current stable version constraint
- [ ] 1.2 Update `CMakeLists.txt` to find and link `Opus::opus` into the audio module target and the asset pipeline tool target
- [ ] 1.3 Add `opus/opus.h` and `opus/opusfile.h` include paths to the audio and asset-pipeline module CMake targets
- [ ] 1.4 Verify the build succeeds on Windows x64 with the updated vcpkg manifest by running `cmake --build build --config Debug` and confirming zero linker errors

## 2. .uzaudio Format v2 Header

- [ ] 2.1 Define `UzAudioHeader_v2` struct in `src/audio/audio_asset.h` with fields: `magic` (uint32, `'UZAU'`), `version` (uint16, `2`), `codec` (uint16, `0 = CODEC_OGG`, `1 = CODEC_OPUS`), `channel_count` (uint16), `sample_rate` (uint32), `duration_samples` (uint64), `data_offset` (uint32)
- [ ] 2.2 Implement `UzAudioReader::detect_version()` in `src/audio/audio_asset.cpp` that reads the `version` field and dispatches to the v1 (OGG) or v2 (codec-discriminated) decode path
- [ ] 2.3 Implement backwards-compatible v1 loader: if `version == 1`, treat the asset as OGG Vorbis using the existing decode path without changes
- [ ] 2.4 Write `UzAudioWriter::write_v2_header()` that serializes a `UzAudioHeader_v2` to the output stream

## 3. Asset Import Pipeline — Opus Encoding

- [ ] 3.1 Implement `OpusEncoder` class in `src/asset_pipeline/opus_encoder.cpp` with method `encode(const PCMBuffer& pcm, OpusEncodeSettings settings) -> std::vector<uint8_t>` using `libopus` `opus_encoder_create` / `opus_encode` API
- [ ] 3.2 Implement per-asset import settings: `bitrate_kbps` (int, options: 32, 64, 96, 128; default 96), `mode` (enum `Streaming | Preloaded`); parse from `.meta` sidecar file or asset import dialog
- [ ] 3.3 In the asset import pipeline, detect source `.ogg` files during import, decode them to PCM using the existing OGG Vorbis decoder, re-encode to Opus at the configured bitrate, and write the result to a v2 `.uzaudio` container
- [ ] 3.4 Implement OGG Vorbis → Opus transcode pipeline: `src/asset_pipeline/audio_importer.cpp` method `transcode_ogg_to_opus(input_path, output_path, settings)` using `stb_vorbis` for decode and `OpusEncoder` for encode
- [ ] 3.5 Cache transcoded assets by input file hash: skip re-encoding if a `.uzaudio` v2 file with matching source hash already exists in the asset cache directory

## 4. Streaming Decoder (Music)

- [ ] 4.1 Implement `OpusStreamDecoder` in `src/audio/opus_decoder.cpp` using `opusfile.h`'s `op_open_memory` / `op_read_float_stereo` API; decode Opus frames on a background thread into a double-buffered ring buffer sized for 4096 frames per channel
- [ ] 4.2 Feed decoded PCM from the ring buffer into XAudio2 streaming source voice buffers using the existing `StreamingVoice` fill callback infrastructure
- [ ] 4.3 Implement looping for streaming Opus: on end-of-stream, call `op_pcm_seek(0)` to reset the decode position when the `CAudioSource::loop` flag is true
- [ ] 4.4 Implement seek support: `OpusStreamDecoder::seek(sample_position)` calls `op_pcm_seek(sample_position)` and flushes the ring buffer

## 5. Preloaded Decoder (SFX)

- [ ] 5.1 Implement `OpusPreloadDecoder::decode_all(const uint8_t* data, size_t size) -> PCMBuffer` in `src/audio/opus_decoder.cpp` using `opusfile.h`'s single-call `op_open_memory` + full `op_read_float_stereo` loop; store result in RAM as `float32` PCM
- [ ] 5.2 In `AudioSystem::load_sfx()`, detect `CODEC_OPUS` in the v2 header and call `OpusPreloadDecoder::decode_all()` immediately on the loading thread; store the PCM buffer in the `AudioAsset` cache
- [ ] 5.3 Ensure `OpusPreloadDecoder` is called only once per asset load and the result reused for all concurrent playback instances of the same asset

## 6. Testing

- [ ] 6.1 Write `tests/audio/test_opus_codec.cpp`: test `UzAudioHeader_v2` serialization — write a v2 header with `codec = CODEC_OPUS`, read it back, verify all fields match
- [ ] 6.2 Test v1 backwards compatibility: feed a v1 `.uzaudio` blob to `UzAudioReader::detect_version()`, verify the OGG decode path is selected and audio plays without error
- [ ] 6.3 Test `OpusEncoder::encode()`: encode a 1-second 44100 Hz stereo sine wave at 96 kbps; decode the result back to PCM with `OpusPreloadDecoder`; compute SNR between original and decoded signals; verify SNR > 40 dB
- [ ] 6.4 Test that encoded file size is ≤70% of the equivalent OGG Vorbis file for the same input (validates the 40% size reduction claim in the proposal)
- [ ] 6.5 Test `OpusStreamDecoder` ring buffer: fill the buffer past its capacity; verify no memory overflow and the overflow counter increments
- [ ] 6.6 Test `OpusStreamDecoder::seek()` to sample 44100 (1 second); verify next decoded sample is from the correct position (within ±10 samples)
- [ ] 6.7 Test `transcode_ogg_to_opus` with a known OGG file: verify output is a valid `.uzaudio` v2 file with `codec = CODEC_OPUS` and `channel_count` / `sample_rate` match the source
- [ ] 6.8 Test asset cache hit: transcode the same OGG file twice; verify the second call does not invoke `OpusEncoder::encode` (use call counter mock)
- [ ] 6.9 Verify ≥95% line and branch coverage on `opus_decoder.cpp`, `opus_encoder.cpp`, and the `detect_version()` dispatch logic

## 7. Documentation

- [ ] 7.1 Update `docs/specs/audio-system.md`: document `.uzaudio` v2 header format, `codec` discriminator values, streaming vs. preloaded decode paths, and XAudio2 buffer feed sequence for Opus streaming
- [ ] 7.2 Update `docs/specs/asset-pipeline.md`: document OGG-to-Opus transcode step, per-asset `bitrate_kbps` and `mode` import settings, cache-hash behavior, and how to opt out of Opus encoding for a specific asset
