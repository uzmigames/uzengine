#pragma once

/// VFX Audio Spectrum Module -- music-reactive particle modulation via FFT.
///
/// Captures PCM audio samples from an `IAudioSpectrumProvider`, computes an
/// in-place iterative Cooley-Tukey FFT, applies Hann windowing and IIR
/// smoothing, and uploads the resulting spectrum as CPU-side float array.
/// The HLSL snippet samples the spectrum to modulate particle size, color,
/// and velocity based on frequency band amplitude.
///
/// # FFT Implementation
/// Uses a power-of-two radix-2 iterative Cooley-Tukey FFT on real-valued
/// input. The Hann window reduces spectral leakage.
///
/// # Beat Detection
/// Simple threshold-based: if the smoothed amplitude at `frequencyBand`
/// exceeds `beatThreshold`, `IsBeat()` returns true for that frame.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
///
/// @ref Cooley & Tukey 1965 "An Algorithm for the Machine Calculation
///      of Complex Fourier Series" -- iterative Cooley-Tukey FFT
/// @ref Harris 1978 "On the Use of Windows for Harmonic Analysis with
///      the Discrete Fourier Transform" -- Hann window choice

#include "vfx_module.h"

#include <cstdint>
#include <json.hpp>

namespace uz {

// ---------------------------------------------------------------------------
// IAudioSpectrumProvider -- query interface for PCM audio data
// ---------------------------------------------------------------------------

/// Abstract interface for capturing mixed audio output samples.
///
/// Concrete implementations may wrap platform audio APIs (WASAPI loopback,
/// PulseAudio monitor, etc.) or return silence for headless/test scenarios.
///
/// # Thread Safety
/// Implementations must be safe to call from the main/render thread.
class IAudioSpectrumProvider {
public:
    virtual ~IAudioSpectrumProvider() = default;

    /// Captures the most recent `sampleCount` mixed PCM samples (mono, float32).
    ///
    /// # Parameters
    /// - `buffer`:      Output buffer, must have room for `sampleCount` floats.
    /// - `sampleCount`: Number of samples to capture.
    ///
    /// # Returns
    /// `true` if samples were captured, `false` if audio is not running or
    /// fewer samples are available (buffer is zeroed on failure).
    [[nodiscard]] virtual bool GetPCMSamples(float* buffer, uint32_t sampleCount) noexcept = 0;
};

// ---------------------------------------------------------------------------
// AudioSpectrumModuleConfig -- configuration for audio-reactive particles
// ---------------------------------------------------------------------------

/// Configuration for `AudioSpectrumModule`.
///
/// | Field           | Default | Description                                    |
/// |-----------------|---------|------------------------------------------------|
/// | fftBands        | 256     | FFT size (256 or 512, power of two)            |
/// | sensitivity     | 1.0     | Linear multiplier on spectrum values            |
/// | smoothingAlpha  | 0.15    | IIR smoothing: s = lerp(s, raw, alpha)          |
/// | frequencyBand   | 64      | Band index exposed as "beat" detector           |
/// | beatThreshold   | 0.6     | Normalized amplitude trigger for beat           |
/// | burstOnBeat     | false   | Emit particle burst on beat detection           |
/// | burstCount      | 50      | Number of particles in a beat burst             |
///
/// # Smoothing Alpha
/// Typical values 0.05-0.3. Lower = more smoothing (slower response),
/// higher = snappier but noisier.
///
/// @ref Cooley & Tukey 1965 -- FFT algorithm
/// @ref Harris 1978 -- Hann window
struct AudioSpectrumModuleConfig {
    /// FFT band count (256 or 512). Must be a power of two.
    uint32_t fftBands = 256;

    /// Linear multiplier applied to spectrum magnitudes before clamping to [0,1].
    float sensitivity = 1.0f;

    /// IIR smoothing coefficient. `smoothed = lerp(smoothed, raw, alpha)`.
    /// Range: [0.01, 1.0]. Lower values produce smoother, slower-responding
    /// spectra; higher values track transients more closely.
    float smoothingAlpha = 0.15f;

    /// Frequency band index (0..fftBands/2-1) used for beat detection.
    /// 64 corresponds roughly to the kick drum range at 44100Hz sample rate.
    uint32_t frequencyBand = 64;

    /// Normalized amplitude threshold [0, 1] that triggers a beat.
    float beatThreshold = 0.6f;

    /// When true, a particle burst is requested on beat detection.
    bool burstOnBeat = false;

    /// Number of particles emitted per beat burst.
    uint32_t burstCount = 50;
};

// ---------------------------------------------------------------------------
// AudioSpectrumModule -- FFT-based audio-reactive particle modulation
// ---------------------------------------------------------------------------

/// VFX module that modulates particle properties based on audio spectrum data.
///
/// Implements `IVFXModule`. The CPU side performs FFT analysis of audio PCM
/// data each frame and stores a smoothed spectrum array. The HLSL snippet
/// samples this spectrum to modulate particle size, color, and velocity.
///
/// # HLSL Dependencies (when USE_AUDIO_SPECTRUM is defined)
/// - `g_spectrum`       : `Texture1D<float>` at register(t10)
/// - `g_spectrumSamp`   : `SamplerState` at register(s3), linear clamp
/// - `cbFrequencyBand`  : `uint` in AudioSpectrumCB at register(b5)
/// - `cbSensitivityMul` : `float` in AudioSpectrumCB
/// - `cbModulateMask`   : `uint` bitmask (bit 0=size, 1=colorR, 2=velocity)
///
/// # Example
/// ```cpp
/// AudioSpectrumModuleConfig cfg;
/// cfg.frequencyBand = 64;
/// cfg.sensitivity = 1.5f;
/// auto module = std::make_unique<AudioSpectrumModule>(cfg);
/// module->SetProvider(&myAudioCapture);
/// stack.Push(std::move(module));
/// ```
class AudioSpectrumModule final : public IVFXModule {
public:
    /// Maximum number of spectrum bins (always upload 512, zero-padded for 256-band).
    static constexpr uint32_t kMaxSpectrumBins = 512;

    /// Default-constructs with `AudioSpectrumModuleConfig` defaults.
    AudioSpectrumModule();

    /// Constructs with the given configuration.
    explicit AudioSpectrumModule(const AudioSpectrumModuleConfig& config);

    ~AudioSpectrumModule() override = default;

    // -- IVFXModule interface -------------------------------------------------

    /// Returns `"audio_spectrum"`.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns the HLSL snippet for audio-reactive particle modulation.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for audio spectrum configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// Applies JSON configuration to module fields.
    void ApplyJsonConfig(const nlohmann::json& config) override;

    /// CPU-side execution: captures PCM, computes FFT, updates spectrum and beat.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration & provider access --------------------------------------

    /// Returns the current configuration.
    [[nodiscard]] const AudioSpectrumModuleConfig& GetConfig() const noexcept;

    /// Sets the audio PCM provider (non-owning pointer).
    void SetProvider(IAudioSpectrumProvider* provider) noexcept;

    /// Returns the current audio provider, or nullptr if none is set.
    [[nodiscard]] IAudioSpectrumProvider* GetProvider() const noexcept;

    /// Returns true if a beat was detected on the most recent frame.
    [[nodiscard]] bool IsBeat() const noexcept;

    /// Returns a pointer to the smoothed spectrum data (512 floats, [0,1] range).
    /// Valid for the module's lifetime. Updated each frame by Execute().
    [[nodiscard]] const float* GetSpectrumData() const noexcept;

    // -- JSON deserialization -------------------------------------------------

    /// Parses an `AudioSpectrumModuleConfig` from a JSON object.
    [[nodiscard]] static AudioSpectrumModuleConfig ParseConfig(const nlohmann::json& j);

private:
    /// Performs the Cooley-Tukey FFT on the captured samples and updates
    /// the smoothed spectrum array.
    ///
    /// Steps:
    /// 1. Apply Hann window to raw PCM samples.
    /// 2. Compute radix-2 iterative FFT (real input -> complex output).
    /// 3. Compute magnitude spectrum (positive frequencies only).
    /// 4. Apply IIR smoothing and sensitivity scaling.
    /// 5. Clamp to [0, 1].
    void ComputeFFT();

    AudioSpectrumModuleConfig config_;

    /// Non-owning pointer to the audio PCM provider.
    IAudioSpectrumProvider* provider_ = nullptr;

    /// Raw PCM sample buffer (double the FFT bands for real-valued FFT input).
    /// Size: fftBands * 2 floats.
    float pcmBuffer_[kMaxSpectrumBins * 2] = {};

    /// Smoothed spectrum data, always 512 elements (zero-padded for 256-band FFT).
    /// Each value in [0, 1] after sensitivity scaling and clamping.
    float spectrumData_[kMaxSpectrumBins] = {};

    /// Whether a beat was detected on the most recent Execute() call.
    bool beatTriggered_ = false;
};

} // namespace uz
