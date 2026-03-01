# Fragmented MP4 Muxer Specification

This specification defines the requirements for the UzEngine fragmented MP4 muxer, responsible for packaging H.264 NAL units into ISO BMFF compliant fMP4 segments compatible with the browser Media Source Extensions (MSE) API.

## ADDED Requirements

### Requirement: Annex B to AVCC NAL Unit Conversion
The system MUST convert H.264 NAL units from Annex B format (start code delimited) to AVCC format (length-prefixed) as required by the ISO BMFF container.

#### Scenario: Start code replacement with length prefix
Given an H.264 bitstream with Annex B start codes (00 00 00 01)
When the NAL unit converter processes the bitstream
Then each start code is replaced with a 4-byte big-endian length prefix
And the length value equals the NAL unit size excluding the prefix itself
And no start codes remain in the output

#### Scenario: SPS and PPS extraction from bitstream
Given an H.264 IDR frame bitstream containing SPS and PPS NAL units
When the NAL parser processes the bitstream
Then SPS NAL units (type 0x07) are identified and extracted without start codes
And PPS NAL units (type 0x08) are identified and extracted without start codes

### Requirement: fMP4 Initialization Segment Generation
The system MUST generate a valid fMP4 initialization segment containing ftyp and moov boxes with H.264 codec configuration for MSE compatibility.

#### Scenario: Init segment with correct box hierarchy
Given H.264 codec configuration (SPS, PPS, profile, level)
When getInitSegment() is called
Then the output contains an ftyp box with brand "isom"
And the output contains a moov box with mvhd, trak, and mvex sub-boxes
And the trak contains stsd with avc1 sample entry and avcC box
And the avcC box contains the provided SPS and PPS data
And the timescale is set to 90000 Hz

#### Scenario: Init segment playable by MSE
Given a generated init segment
When it is appended to an MSE SourceBuffer with codec "avc1.42E01F"
Then the SourceBuffer accepts the data without error
And the MediaSource transitions to "open" state

### Requirement: fMP4 Media Segment Generation
The system MUST generate valid fMP4 media segments containing moof and mdat boxes for each encoded H.264 frame.

#### Scenario: Media segment with correct fragment structure
Given an encoded H.264 access unit and a sequence number
When muxFrame() is called
Then the output contains a moof box with mfhd (sequence number) and traf sub-boxes
And the traf contains tfhd with default-base-is-moof flag
And the traf contains tfdt with the correct decode timestamp
And the traf contains trun with sample duration, size, and flags
And the output contains an mdat box with the AVCC-formatted H.264 data

#### Scenario: IDR frame flagging
Given an encoded H.264 IDR frame
When muxFrame() is called
Then the trun sample flags indicate a sync sample (key frame)

#### Scenario: Monotonic sequence numbering
Given multiple frames are muxed sequentially
When each muxFrame() call completes
Then the mfhd sequence number increments by 1 for each fragment
And the tfdt decode timestamp increments by the frame duration

### Requirement: ISO BMFF Box Format Compliance
The system MUST write ISO BMFF boxes with correct size headers, box types, and big-endian byte ordering per ISO 14496-12.

#### Scenario: Correct box size calculation
Given a box with type "mdat" and 1024 bytes of payload
When the box is written
Then the first 4 bytes contain the total box size (1032) in big-endian format
And the next 4 bytes contain the ASCII type "mdat"
And the remaining bytes contain the payload
