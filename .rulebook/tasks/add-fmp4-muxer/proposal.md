# Proposal: Add Fragmented MP4 (fMP4) Muxer

## Why
The browser's Media Source Extensions (MSE) API, which the VSCode webview uses for video playback, requires video data in fragmented MP4 (fMP4) format. Raw H.264 NAL units from the encoder cannot be fed directly to MSE. The fMP4 muxer bridges this gap by wrapping H.264 access units into ISO BMFF boxes (ftyp, moov, moof, mdat) that MSE can parse and decode. This is the highest-risk component of the pipeline because MSE is extremely strict about fMP4 format correctness - a single wrong field causes silent failure. A hand-written muxer (rather than using FFmpeg's muxer) gives us full control, zero additional dependencies, and keeps the code portable.

## What Changes
- Implement fMP4 box writer utilities (writeBox, writeFullBox, big-endian integer helpers)
- Implement initialization segment generator: ftyp + moov boxes with avcC (carrying SPS/PPS), trak, mdia, mvex
- Implement media segment generator: moof + mdat boxes with traf, tfhd, tfdt, trun
- Convert H.264 NAL units from Annex B format (00 00 00 01 start codes) to AVCC format (4-byte length prefix) as required by fMP4
- Handle timescale (90000 Hz for video) and frame duration calculation
- Track sequence numbers for fragment ordering

## Impact
- Affected specs: muxer/spec.md (new)
- Affected code: src/muxer/fmp4_muxer.h, src/muxer/fmp4_muxer.cpp
- Breaking change: NO (new module)
- User benefit: Encoded H.264 frames become playable in any MSE-compatible browser/webview
