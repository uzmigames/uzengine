/// H264Encoder — Hardware-accelerated H.264 encoding via FFmpeg libavcodec.
///
/// Encodes RGBA frames to H.264 baseline profile NAL units for WebCodecs
/// decoding in the VSCode viewport. Tries hardware encoders in priority
/// order (NVENC, AMF, QSV, Media Foundation) with automatic software fallback.
///
/// Supports two encoding modes:
///
/// ## Synchronous (original)
/// The pointer returned by EncodeFrame() is valid only until the next call.
///
/// ## Asynchronous (recommended for real-time streaming)
/// SubmitFrameAsync() copies the RGBA data and returns immediately.
/// Encoding runs on a dedicated background thread. GetEncodedFrame()
/// returns the most recently completed frame. This decouples the render
/// thread from the 3-5ms encode cost at 1080p.
///
/// Only ONE mode should be active at a time — do not interleave
/// EncodeFrame() with SubmitFrameAsync()/GetEncodedFrame().
///
/// # Thread Safety
/// - EncodeFrame(): NOT thread-safe — render thread only.
/// - SubmitFrameAsync() / GetEncodedFrame(): safe to call from the render
///   thread while the encoder thread runs internally.
/// - RequestKeyframe() / SetBitrate(): safe to call from any thread.
///
/// # Lifecycle
/// ```cpp
/// H264Encoder encoder;
/// encoder.Initialize({.width = 1280, .height = 720, .fps = 120});
/// // ... per frame (async path):
/// encoder.SubmitFrameAsync(rgba, stride);
/// const uint8_t* data; uint32_t size;
/// if (encoder.GetEncodedFrame(&data, &size)) {
///     // send NAL units to webview
/// }
/// encoder.Shutdown(); // joins encoder thread
/// ```

#pragma once

#include <cstdint>

namespace uz {

/// Hardware-accelerated H.264 encoder for low-latency viewport frame streaming.
class H264Encoder {
public:
    /// Configuration for the H.264 encoder.
    ///
    /// All fields have sensible defaults for 720p viewport streaming at
    /// 120 FPS with low-latency CBR encoding.
    struct Config {
        uint32_t width          = 1280;   ///< Frame width in pixels.
        uint32_t height         = 720;    ///< Frame height in pixels.
        uint32_t fps            = 60;     ///< Target frame rate (numerator, denominator = 1).
        uint32_t bitrateKbps    = 20000;  ///< Target bitrate in kbps (CBR).
        uint32_t keyframeInterval = 15;   ///< IDR frame interval (in frames).
    };

    H264Encoder();
    ~H264Encoder();

    /// Non-copyable — internal FFmpeg context pointers.
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    /// Movable — transfers ownership of FFmpeg contexts.
    H264Encoder(H264Encoder&& other) noexcept;
    H264Encoder& operator=(H264Encoder&& other) noexcept;

    /// Initializes the encoder with the given configuration.
    ///
    /// Probes hardware encoders in order: h264_nvenc (NVIDIA), h264_amf (AMD),
    /// h264_qsv (Intel), h264_mf (Media Foundation), then falls back to
    /// libx264 software encoding. The first encoder that successfully opens
    /// is used for the lifetime of this instance.
    ///
    /// Also creates a libswscale context for RGBA-to-NV12 color conversion.
    ///
    /// # Returns
    /// true if an encoder was successfully opened, false on failure.
    [[nodiscard]] bool Initialize(const Config& config);

    /// Shuts down the encoder and frees all FFmpeg resources.
    ///
    /// Safe to call multiple times or if never initialized (no-op).
    void Shutdown();

    /// Encodes one RGBA frame and produces H.264 NAL units.
    ///
    /// Performs RGBA-to-NV12 color conversion via libswscale, then feeds the
    /// NV12 frame to the encoder. The output is one or more concatenated
    /// H.264 NAL units suitable for WebCodecs VideoDecoder.
    ///
    /// # Arguments
    /// * `rgbaData` — Pointer to RGBA pixel data (width * height * 4 bytes).
    /// * `rgbaStride` — Row stride in bytes (typically width * 4).
    /// * `outData` — Receives a pointer to the encoded NAL data. The buffer
    ///   is owned by the encoder and valid until the next call to EncodeFrame.
    /// * `outSize` — Receives the size of the encoded data in bytes.
    ///
    /// # Returns
    /// true if a frame was produced, false if the encoder buffered the frame
    /// (no output yet) or an error occurred.
    [[nodiscard]] bool EncodeFrame(const uint8_t* rgbaData, uint32_t rgbaStride,
                                   const uint8_t** outData, uint32_t* outSize);

    /// Submits an RGBA frame for async encoding on the encoder thread.
    ///
    /// Copies rgbaData into an internal staging buffer (the caller's pointer
    /// is not retained after this call returns). Encoding happens on a
    /// background thread that was started during Initialize().
    ///
    /// If the previous frame is still encoding, this call drops the new
    /// frame silently (encoder cannot keep up) and logs a warning.
    ///
    /// # Arguments
    /// * `rgbaData` — Pointer to RGBA pixel data (width * height * 4 bytes).
    /// * `rgbaStride` — Row stride in bytes (typically width * 4).
    void SubmitFrameAsync(const uint8_t* rgbaData, uint32_t rgbaStride);

    /// Returns the most recently completed encoded frame.
    ///
    /// Returns true if a new encoded frame is available since the last call.
    /// outData/outSize point to encoder-owned memory that remains valid
    /// until the next call to GetEncodedFrame() or SubmitFrameAsync().
    ///
    /// # Arguments
    /// * `outData` — Receives a pointer to the encoded NAL data.
    /// * `outSize` — Receives the size of the encoded data in bytes.
    ///
    /// # Returns
    /// true if a new frame was available, false otherwise.
    [[nodiscard]] bool GetEncodedFrame(const uint8_t** outData, uint32_t* outSize);

    /// Forces the next encoded frame to be an IDR keyframe.
    ///
    /// Useful for error recovery when the WebCodecs decoder needs a clean
    /// reference point, or when a viewport panel is first revealed.
    void RequestKeyframe();

    /// Updates the target bitrate for adaptive quality control.
    ///
    /// Takes effect on the next encoded frame. The encoder's rate controller
    /// adjusts gradually to the new target.
    ///
    /// # Arguments
    /// * `kbps` — New target bitrate in kilobits per second.
    void SetBitrate(uint32_t kbps);

    /// Returns true if Initialize() succeeded and Shutdown() has not been called.
    [[nodiscard]] bool IsInitialized() const noexcept;

    /// Returns the FFmpeg encoder name string (e.g., "h264_nvenc", "libx264").
    ///
    /// Returns "none" if not initialized.
    [[nodiscard]] const char* GetEncoderName() const noexcept;

    /// Returns the codec extradata (SPS/PPS NAL units) for WebCodecs.
    ///
    /// The extradata must be passed to `VideoDecoder.configure()` as the
    /// `description` field when `AV_CODEC_FLAG_GLOBAL_HEADER` is set.
    ///
    /// # Arguments
    /// * `outData` — Receives a pointer to the extradata bytes.
    /// * `outSize` — Receives the size of the extradata in bytes.
    ///
    /// # Returns
    /// true if extradata is available, false if not initialized or empty.
    [[nodiscard]] bool GetExtradata(const uint8_t** outData, uint32_t* outSize) const noexcept;

private:
    /// PIMPL — hides FFmpeg headers from all consumers of this header.
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uz
