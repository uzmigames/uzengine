# Proposal: phase3-opus-audio-codec

## Why
OGG Vorbis is the current streaming audio format, but Opus is a measurably superior codec: it produces files approximately 40% smaller at equivalent perceptual quality, has lower algorithmic latency making it better suited for short SFX playback, and is royalty-free. Modern streaming platforms and game engines (notably Godot 4) have adopted Opus as their preferred codec. Shipping a large game with OGG audio wastes significant disk space and increases distribution size without a quality benefit.

## What Changes
- libopus integrated as a vcpkg dependency in the audio module and asset pipeline
- OGG Vorbis → Opus transcoding applied automatically during asset import; source `.ogg` files are encoded to Opus and stored in `.uzaudio` containers
- `.uzaudio` format bumped to v2 header with a codec field (`CODEC_OGG = 0`, `CODEC_OPUS = 1`) for backwards compatibility
- Streaming decoder path for music assets: opus frames decoded on a background thread into XAudio2 streaming buffers
- Preloaded decode path for short SFX: full Opus decode to PCM in RAM at load time, no per-frame decode cost
- Fallback to legacy OGG decode if an existing `.uzaudio` v1 asset is loaded, preserving compatibility with pre-existing content
- Import settings expose bitrate selection (32/64/96/128 kbps) and a per-asset streaming vs. preloaded toggle

## Impact
- Affected specs: `docs/specs/audio-system.md`, `docs/specs/asset-pipeline.md`
- Affected code: `src/audio/`, `src/asset_pipeline/`, `vcpkg.json`
- Breaking change: NO (v1 `.uzaudio` files remain decodable; new imports use v2 Opus by default)
- User benefit: Smaller distributed game size with no perceptual quality loss; shorter SFX playback latency; future-proof codec choice aligned with industry direction.
