# UzEngine - C++ Core Expert Memory

## Project Structure
- **Root**: `E:\UzEngine`
- **Source**: `E:\UzEngine\src/`
- **Renderer**: `E:\UzEngine\src\renderer\` (renderer.h, renderer.cpp)
- **Common**: `E:\UzEngine\src\common\` (types.h, timer.h)
- **Build**: CMake + vcpkg
- **Language**: C++ (CLAUDE.md says "C" but project is clearly C++20)

## Key Constants (types.h)
- `kFrameCount = 2` (double buffering)
- `kDefaultWidth = 1280`, `kDefaultHeight = 720`
- `kTargetFPS = 30`

## Architecture (see [architecture.md](architecture.md))
- DX12Renderer: owns all GPU resources via ComPtr/RAII
- MVP matrix passed via root 32-bit constants (not CBV)
- `createConstantBuffer()` is a declared no-op (header declares it, impl is intentional no-op)
- Shaders compiled at runtime from embedded HLSL strings (kVertexShaderSrc, kPixelShaderSrc)
- UZENGINE_DEBUG macro enables D3D12 debug layer and shader debug flags

## Conventions
- Namespace: `uz`
- Member naming: trailing underscore (e.g., `width_`, `device_`)
- ComPtr for all COM objects
- ThrowIfFailed helper in anonymous namespace for HRESULT error handling
- Comments explain "why", not "what"
- Sequential file editing required (CLAUDE.md mandate)

## Encoder Module (src/encoder/)
- **encoder.h**: Abstract `IFrameEncoder` interface + `CodecConfig` struct
- **nvenc_encoder.h/.cpp**: `NvEncEncoder` - NVENC hardware encoder via dynamic LoadLibrary
  - Stubbed: `isAvailable()` probes DLL, `initialize()` returns false (no SDK headers)
- **ffmpeg_encoder.h/.cpp**: `FFmpegEncoder` - Windows MF H.264 software encoder
  - Uses `CLSID_CMSH264EncoderMFT` via CoCreateInstance (no external deps)
  - Input: ARGB32 (RGBA swizzled to BGRA), Output: H.264 Annex-B
  - Extracts SPS/PPS from first IDR frame into CodecConfig
  - Links: mf.lib, mfplat.lib, mfuuid.lib, ole32.lib
- **encoder_factory.h/.cpp**: `createEncoder()` tries NVENC first, falls back to MF
- Factory pattern: isAvailable() -> initialize() cascade with fallback

## Muxer Module (src/muxer/)
- **fmp4_muxer.h/.cpp**: `FMP4Muxer` - hand-written ISO 14496-12 fMP4 muxer
  - Produces MSE-compatible init segment (ftyp+moov) and media segments (moof+mdat)
  - Init segment: ftyp(isom) + moov(mvhd+trak(tkhd+mdia(mdhd+hdlr+minf(vmhd+dinf+stbl(stsd(avc1(avcC))+stts+stsc+stsz+stco))))+mvex(trex))
  - avcC box contains SPS/PPS from encoder's CodecConfig
  - Media segments: moof(mfhd+traf(tfhd+tfdt+trun)) + mdat(AVCC NALUs)
  - trun flags=0x0305: data-offset + first-sample-flags + sample-duration + sample-size
  - tfhd flags=0x020000: default-base-is-moof
  - tfdt version=1 for 64-bit base_media_decode_time
  - splitAnnexB() converts Annex B start codes to AVCC 4-byte length prefix, filters SPS/PPS
  - Key frame flags: 0x02000000 (independent), Non-key: 0x01010000 (dependent+non-sync)
  - Default timescale: 90000, frame duration: 3000 (= 90000/30fps)

## User Preferences
- Communicates in Portuguese (BR)
- Prefers DX12 (not DX11)
- NVENC primary + FFmpeg fallback for encoding pipeline
