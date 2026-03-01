## 1. FFmpeg Dependencies
- [x] 1.1 Add ffmpeg[x264,avcodec,swscale] to vcpkg.json manifest
- [x] 1.2 Update CMakeLists.txt to find and link FFmpeg libraries (avcodec, avutil, swscale)
- [x] 1.3 Verify vcpkg installs FFmpeg with x264 support successfully

## 2. FFmpeg Encoder Implementation
- [x] 2.1 Create src/encoder/ffmpeg_encoder.h with FFmpegEncoder class declaration
- [x] 2.2 Implement initialize(): find libx264 codec, allocate AVCodecContext with ultrafast preset and zerolatency tune
- [x] 2.3 Configure H.264 Baseline profile, target bitrate, GOP size, no B-frames
- [x] 2.4 Create sws_context for RGBA (AV_PIX_FMT_RGBA) to YUV420P (AV_PIX_FMT_YUV420P) conversion
- [x] 2.5 Allocate AVFrame for input (YUV420P) and AVPacket for output
- [x] 2.6 Implement encode(): convert RGBA to YUV420P via sws_scale, send frame, receive H.264 packets
- [x] 2.7 Extract NAL units from AVPacket data (already in Annex B format from libx264)
- [x] 2.8 Implement getCodecConfig(): parse SPS/PPS from first IDR frame output
- [x] 2.9 Implement shutdown(): free AVCodecContext, AVFrame, sws_context, close codec

## 3. Encoder Factory
- [x] 3.1 Create src/encoder/encoder_factory.h with createEncoder() function declaration
- [x] 3.2 Implement createEncoder(): try NvEncEncoder::isAvailable() first, fall back to FFmpegEncoder
- [x] 3.3 Log which encoder was selected ("NVENC initialized" or "FFmpeg fallback encoder initialized")

## 4. Testing
- [x] 4.1 Verify FFmpeg encoder initializes successfully
- [x] 4.2 Encode a synthetic RGBA test frame and verify valid H.264 output
- [x] 4.3 Verify output contains SPS, PPS, and IDR NAL units
- [x] 4.4 Verify encoded output is decodable by external tools (ffprobe)
- [x] 4.5 Verify encoder factory selects NVENC on NVIDIA systems
- [x] 4.6 Verify encoder factory falls back to FFmpeg when NVENC is unavailable
- [x] 4.7 Verify sequential frame encoding without memory leaks

## 5. Documentation
- [x] 5.1 Document FFmpeg build requirements and vcpkg setup
- [x] 5.2 Document encoder factory selection logic
