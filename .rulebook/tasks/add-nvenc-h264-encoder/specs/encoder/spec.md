# Encoder Specification

This specification defines the requirements for the UzEngine video encoding subsystem, responsible for compressing rendered RGBA frames into H.264 bitstream for real-time streaming.

## ADDED Requirements

### Requirement: Encoder Interface Abstraction
The system MUST provide an IFrameEncoder abstract interface that allows multiple encoder implementations to be used interchangeably via the strategy pattern.

#### Scenario: Interface contract enforcement
Given a class implements IFrameEncoder
When it is used by the encoding pipeline
Then it MUST implement initialize(), encode(), getCodecConfig(), and shutdown()
And the caller does not need to know which encoder backend is in use

### Requirement: NVENC Hardware Encoder
The system MUST implement an NVENC-based H.264 encoder that dynamically loads nvEncodeAPI64.dll at runtime for hardware-accelerated encoding on NVIDIA GPUs.

#### Scenario: Dynamic library loading and availability check
Given the system has an NVIDIA GPU with compatible drivers
When NvEncEncoder::isAvailable() is called
Then nvEncodeAPI64.dll is loaded dynamically
And the NVENC API version is verified
And the function returns true

#### Scenario: Unavailable NVENC graceful failure
Given the system does not have an NVIDIA GPU or compatible drivers
When NvEncEncoder::isAvailable() is called
Then the function returns false without crashing
And an informational log message is emitted

#### Scenario: NVENC session initialization
Given NVENC is available
When initialize() is called with EncoderConfig
Then an encode session is created with H.264 Baseline profile
And the bitrate is set to CBR at the configured rate
And B-frames are disabled for low latency
And the input format accepts ARGB data

#### Scenario: Frame encoding to H.264 NAL units
Given an initialized NVENC encoder
When encode() is called with RGBA pixel data
Then the pixels are copied to the NVENC input buffer
And the frame is encoded to H.264
And the output contains valid NAL units in Annex B format
And the first frame produces an IDR with SPS and PPS NAL units

### Requirement: Codec Configuration Export
The system MUST export H.264 codec configuration (profile, level, SPS, PPS) for downstream fMP4 muxing.

#### Scenario: SPS and PPS extraction
Given at least one IDR frame has been encoded
When getCodecConfig() is called
Then it returns the H.264 profile indicator byte
And it returns the H.264 level indicator byte
And it returns the raw SPS NAL unit bytes (without start code)
And it returns the raw PPS NAL unit bytes (without start code)
