# Encoder Specification - FFmpeg Fallback and Encoder Factory

This specification extends the encoder subsystem with a software-based FFmpeg/libx264 fallback encoder and an automatic encoder selection factory.

## ADDED Requirements

### Requirement: FFmpeg Software Encoder
The system MUST implement an FFmpeg-based H.264 encoder using libx264 as a fallback when NVENC hardware encoding is not available.

#### Scenario: FFmpeg encoder initialization
Given the FFmpeg libraries (libavcodec, libswscale) are available
When FFmpegEncoder::initialize() is called with EncoderConfig
Then an AVCodecContext is created with the libx264 codec
And the preset is set to ultrafast with zerolatency tune
And H.264 Baseline profile is configured
And no B-frames are used

#### Scenario: RGBA to YUV420P color conversion and encoding
Given an initialized FFmpeg encoder
When encode() is called with RGBA pixel data
Then sws_scale converts the pixels from RGBA to YUV420P
And avcodec_send_frame submits the converted frame
And avcodec_receive_packet retrieves the encoded H.264 bitstream
And the output contains valid NAL units in Annex B format

#### Scenario: Codec configuration extraction
Given at least one IDR frame has been encoded by FFmpeg
When getCodecConfig() is called
Then SPS and PPS NAL units are parsed from the encoded output
And profile and level indicator bytes are extracted

### Requirement: Encoder Factory Automatic Selection
The system MUST provide an encoder factory that automatically selects the best available encoder at runtime, preferring NVENC over FFmpeg.

#### Scenario: NVENC selected on NVIDIA hardware
Given the system has an NVIDIA GPU with NVENC support
When createEncoder() is called with preferNVENC=true
Then NvEncEncoder::isAvailable() returns true
And an NvEncEncoder instance is created and initialized
And a log message "NVENC initialized" is emitted

#### Scenario: FFmpeg fallback on non-NVIDIA hardware
Given the system does not have NVENC support
When createEncoder() is called
Then NvEncEncoder::isAvailable() returns false
And an FFmpegEncoder instance is created and initialized
And a log message "FFmpeg fallback encoder initialized" is emitted

#### Scenario: Total encoder failure
Given neither NVENC nor FFmpeg is available
When createEncoder() is called
Then nullptr is returned
And an error log message is emitted
