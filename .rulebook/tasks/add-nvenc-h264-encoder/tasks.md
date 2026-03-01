## 1. Encoder Interface
- [ ] 1.1 Create src/encoder/encoder.h with IFrameEncoder abstract class (initialize, encode, getCodecConfig, shutdown)
- [ ] 1.2 Define EncoderConfig struct in common/types.h (width, height, fps, bitrate, profile, preferNVENC)
- [ ] 1.3 Define CodecConfig struct (profileIdc, levelIdc, sps, pps) for downstream fMP4 muxer

## 2. NVENC SDK Integration
- [ ] 2.1 Download NVIDIA Video Codec SDK and place headers in external/nvidia-video-codec-sdk/Interface/
- [ ] 2.2 Add CMake option UZENGINE_ENABLE_NVENC with SDK path detection
- [ ] 2.3 Add compile definition UZENGINE_HAS_NVENC when SDK is found

## 3. NVENC Encoder Implementation
- [ ] 3.1 Create src/encoder/nvenc_encoder.h with NvEncEncoder class declaration
- [ ] 3.2 Implement static isAvailable(): dynamically load nvEncodeAPI64.dll, check NvEncodeAPIGetMaxSupportedVersion
- [ ] 3.3 Implement initialize(): open NVENC session, set initialization params (H.264 Baseline, CBR 4Mbps, no B-frames)
- [ ] 3.4 Configure low-latency preset (NV_ENC_PRESET_P3_GUID or equivalent low-latency tuning)
- [ ] 3.5 Configure input format as ARGB (NVENC converts internally to NV12)
- [ ] 3.6 Create NVENC input and output buffers
- [ ] 3.7 Implement encode(): lock input buffer, copy RGBA data, submit encode, retrieve bitstream
- [ ] 3.8 Implement NAL unit extraction from NVENC output (SPS, PPS from first IDR, subsequent P-frames)
- [ ] 3.9 Implement getCodecConfig(): extract profile/level/SPS/PPS from encoded stream
- [ ] 3.10 Implement shutdown(): destroy buffers, close encode session, unload DLL

## 4. Testing
- [ ] 4.1 Verify NVENC availability detection (isAvailable returns true on NVIDIA GPU)
- [ ] 4.2 Verify encoder initializes without errors
- [ ] 4.3 Encode a synthetic RGBA test frame and verify output contains valid H.264 NAL units
- [ ] 4.4 Verify SPS and PPS NAL units are extractable from the first IDR frame
- [ ] 4.5 Verify encoded output is decodable by ffprobe/ffmpeg
- [ ] 4.6 Verify encoder handles multiple sequential frames without memory leaks
- [ ] 4.7 Verify graceful failure when NVENC is not available (returns false, no crash)

## 5. Documentation
- [ ] 5.1 Document NVENC SDK download and setup instructions
- [ ] 5.2 Document encoder interface design and strategy pattern
