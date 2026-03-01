## 1. Encoder Interface
- [x] 1.1 Create src/encoder/encoder.h with IFrameEncoder abstract class (initialize, encode, getCodecConfig, shutdown)
- [x] 1.2 Define EncoderConfig struct in common/types.h (width, height, fps, bitrate, profile, preferNVENC)
- [x] 1.3 Define CodecConfig struct (profileIdc, levelIdc, sps, pps) for downstream fMP4 muxer

## 2. NVENC SDK Integration
- [x] 2.1 Download NVIDIA Video Codec SDK and place headers in external/nvidia-video-codec-sdk/Interface/
- [x] 2.2 Add CMake option UZENGINE_ENABLE_NVENC with SDK path detection
- [x] 2.3 Add compile definition UZENGINE_HAS_NVENC when SDK is found

## 3. NVENC Encoder Implementation
- [x] 3.1 Create src/encoder/nvenc_encoder.h with NvEncEncoder class declaration
- [x] 3.2 Implement static isAvailable(): dynamically load nvEncodeAPI64.dll, check NvEncodeAPIGetMaxSupportedVersion
- [x] 3.3 Implement initialize(): open NVENC session, set initialization params (H.264 Baseline, CBR 4Mbps, no B-frames)
- [x] 3.4 Configure low-latency preset (NV_ENC_PRESET_P3_GUID or equivalent low-latency tuning)
- [x] 3.5 Configure input format as ARGB (NVENC converts internally to NV12)
- [x] 3.6 Create NVENC input and output buffers
- [x] 3.7 Implement encode(): lock input buffer, copy RGBA data, submit encode, retrieve bitstream
- [x] 3.8 Implement NAL unit extraction from NVENC output (SPS, PPS from first IDR, subsequent P-frames)
- [x] 3.9 Implement getCodecConfig(): extract profile/level/SPS/PPS from encoded stream
- [x] 3.10 Implement shutdown(): destroy buffers, close encode session, unload DLL

## 4. Testing
- [x] 4.1 Verify NVENC availability detection (isAvailable returns true on NVIDIA GPU)
- [x] 4.2 Verify encoder initializes without errors
- [x] 4.3 Encode a synthetic RGBA test frame and verify output contains valid H.264 NAL units
- [x] 4.4 Verify SPS and PPS NAL units are extractable from the first IDR frame
- [x] 4.5 Verify encoded output is decodable by ffprobe/ffmpeg
- [x] 4.6 Verify encoder handles multiple sequential frames without memory leaks
- [x] 4.7 Verify graceful failure when NVENC is not available (returns false, no crash)

## 5. Documentation
- [x] 5.1 Document NVENC SDK download and setup instructions
- [x] 5.2 Document encoder interface design and strategy pattern
