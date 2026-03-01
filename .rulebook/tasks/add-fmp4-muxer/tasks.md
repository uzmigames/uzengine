## 1. Box Writer Utilities
- [ ] 1.1 Create src/muxer/fmp4_muxer.h with FMP4Muxer class interface (init, muxFrame, getInitSegment)
- [ ] 1.2 Implement big-endian write helpers: writeU8, writeU16BE, writeU32BE, writeU64BE
- [ ] 1.3 Implement writeBox(type, payload): 4-byte size + 4-byte type + payload
- [ ] 1.4 Implement writeFullBox(type, version, flags, payload): box + 1-byte version + 3-byte flags

## 2. Annex B to AVCC Conversion
- [ ] 2.1 Implement NAL unit parser: split Annex B bitstream by start codes (00 00 00 01 or 00 00 01)
- [ ] 2.2 Implement Annex B to AVCC converter: replace start codes with 4-byte big-endian length prefix
- [ ] 2.3 Implement SPS/PPS extractor: identify NAL type from first byte (SPS=0x67, PPS=0x68)

## 3. Initialization Segment
- [ ] 3.1 Implement ftyp box: brand "isom", minor version 0x200, compatible brands [isom, iso2, avc1, mp41]
- [ ] 3.2 Implement moov box container with mvhd (movie header, timescale=90000)
- [ ] 3.3 Implement trak > tkhd (track header, track ID=1, width/height)
- [ ] 3.4 Implement mdia > mdhd (media header, timescale=90000) + hdlr (handler type "vide")
- [ ] 3.5 Implement minf > vmhd + dinf/dref + stbl with empty sample tables (stsd, stts, stsc, stsz, stco)
- [ ] 3.6 Implement stsd with avc1 sample entry containing avcC box (profile, level, SPS, PPS from codec config)
- [ ] 3.7 Implement mvex > trex (track extends, default sample description index=1)
- [ ] 3.8 Combine ftyp + moov into complete init segment, cache for reuse

## 4. Media Segments
- [ ] 4.1 Implement moof box with mfhd (sequence number, incrementing per fragment)
- [ ] 4.2 Implement traf > tfhd (track ID=1, default-base-is-moof flag)
- [ ] 4.3 Implement tfdt (track fragment decode time, incrementing by frame duration)
- [ ] 4.4 Implement trun (sample count=1, sample duration, sample size, sample flags for IDR vs P-frame)
- [ ] 4.5 Implement mdat box containing the AVCC-formatted H.264 access unit
- [ ] 4.6 Combine moof + mdat into complete media segment

## 5. Testing
- [ ] 5.1 Write unit test: verify ftyp box structure matches ISO 14496-12 specification
- [ ] 5.2 Write unit test: verify Annex B to AVCC conversion produces correct length prefixes
- [ ] 5.3 Write unit test: verify init segment is parseable by mp4box or ffprobe
- [ ] 5.4 Write integration test: create init + media segments from real H.264 NALs, verify playback in browser
- [ ] 5.5 Create standalone HTML test page that loads fMP4 via MSE for manual verification
- [ ] 5.6 Verify sequence numbers increment correctly across multiple media segments

## 6. Documentation
- [ ] 6.1 Document fMP4 box structure and ISO BMFF reference in docs/architecture.md
- [ ] 6.2 Document Annex B vs AVCC format differences and conversion logic
