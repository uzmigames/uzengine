/// VFX Audio Spectrum Module implementation -- FFT-based audio-reactive particles.
///
/// @ref Cooley & Tukey 1965 "An Algorithm for the Machine Calculation
///      of Complex Fourier Series" -- iterative radix-2 FFT
/// @ref Harris 1978 "On the Use of Windows for Harmonic Analysis with
///      the Discrete Fourier Transform" -- Hann window choice

#include "audio_spectrum_module.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 2.0f * kPi;

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for audio-reactive particle modulation
// ---------------------------------------------------------------------------

static constexpr const char* kAudioSpectrumShaderCode = R"hlsl(
// --- AudioSpectrumModule ---
#ifdef USE_AUDIO_SPECTRUM
{
    float uv = (float)cbFrequencyBand / 511.0;
    float amp = g_spectrum.SampleLevel(g_spectrumSamp, uv, 0) * cbSensitivityMul;

    // Modulate particle properties based on bitmask:
    //   bit 0 = size, bit 1 = color red channel, bit 2 = velocity magnitude
    if (cbModulateMask & 1u) {
        float currentSize = Sizes[particleIdx];
        Sizes[particleIdx] = currentSize * (1.0 + amp * 2.0);
    }
    if (cbModulateMask & 2u) {
        float4 currentColor = Colors[particleIdx];
        currentColor.r = saturate(currentColor.r + amp);
        Colors[particleIdx] = currentColor;
    }
    if (cbModulateMask & 4u) {
        float3 currentVel = Velocities[particleIdx].xyz;
        Velocities[particleIdx] = float4(currentVel * (1.0 + amp), 0.0);
    }
}
#endif
)hlsl";

// ---------------------------------------------------------------------------
// Bit-reversal permutation helper
// ---------------------------------------------------------------------------

/// Returns the bit-reversed index for a given index and bit width.
/// Used by the iterative Cooley-Tukey FFT for in-place reordering.
static uint32_t BitReverse(uint32_t x, uint32_t bits) {
    uint32_t result = 0;
    for (uint32_t i = 0; i < bits; ++i) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// AudioSpectrumModule
// ---------------------------------------------------------------------------

AudioSpectrumModule::AudioSpectrumModule()
    : AudioSpectrumModule(AudioSpectrumModuleConfig{})
{}

AudioSpectrumModule::AudioSpectrumModule(const AudioSpectrumModuleConfig& config)
    : config_(config)
{
    std::memset(spectrumData_, 0, sizeof(spectrumData_));
    std::memset(pcmBuffer_, 0, sizeof(pcmBuffer_));
}

// -- IVFXModule interface -----------------------------------------------------

const char* AudioSpectrumModule::GetName() const noexcept {
    return "audio_spectrum";
}

std::string_view AudioSpectrumModule::GetShaderCode() const {
    return kAudioSpectrumShaderCode;
}

std::vector<ParameterDef> AudioSpectrumModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(7);

    {
        ParameterDef p;
        p.name       = "fft_bands";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(config_.fftBands);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "sensitivity";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.sensitivity;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "smoothing_alpha";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.smoothingAlpha;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name       = "frequency_band";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(config_.frequencyBand);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "beat_threshold";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.beatThreshold;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "burst_on_beat";
        p.type        = ParameterType::Bool;
        p.defaultBool = config_.burstOnBeat;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name       = "burst_count";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(config_.burstCount);
        params.push_back(p);
    }

    return params;
}

void AudioSpectrumModule::ApplyJsonConfig(const nlohmann::json& config) {
    config_ = ParseConfig(config);
}

void AudioSpectrumModule::Execute(const ExecuteContext& /*ctx*/) {
    // Capture PCM samples from the audio provider.
    // If no provider is set or capture fails, the spectrum remains at its
    // previous smoothed state (graceful degradation to silence).
    const uint32_t sampleCount = config_.fftBands * 2;

    if (provider_) {
        if (!provider_->GetPCMSamples(pcmBuffer_, sampleCount)) {
            // Capture failed -- zero the PCM buffer so FFT produces silence.
            // The IIR smoothing will gradually fade the spectrum to zero.
            std::memset(pcmBuffer_, 0, sampleCount * sizeof(float));
        }
    } else {
        // No provider -- zero input produces zero spectrum via smoothing decay.
        std::memset(pcmBuffer_, 0, sampleCount * sizeof(float));
    }

    ComputeFFT();

    // Beat detection: compare the smoothed amplitude at the configured
    // frequency band against the threshold.
    const uint32_t maxBand = config_.fftBands / 2;
    const uint32_t band = std::min(config_.frequencyBand, maxBand > 0 ? maxBand - 1 : 0u);
    beatTriggered_ = spectrumData_[band] >= config_.beatThreshold;
}

// -- Configuration & provider access ------------------------------------------

const AudioSpectrumModuleConfig& AudioSpectrumModule::GetConfig() const noexcept {
    return config_;
}

void AudioSpectrumModule::SetProvider(IAudioSpectrumProvider* provider) noexcept {
    provider_ = provider;
}

IAudioSpectrumProvider* AudioSpectrumModule::GetProvider() const noexcept {
    return provider_;
}

bool AudioSpectrumModule::IsBeat() const noexcept {
    return beatTriggered_;
}

const float* AudioSpectrumModule::GetSpectrumData() const noexcept {
    return spectrumData_;
}

// -- FFT implementation -------------------------------------------------------

void AudioSpectrumModule::ComputeFFT() {
    const uint32_t N = config_.fftBands * 2; // number of real samples
    const uint32_t halfN = N / 2;            // number of complex elements for FFT

    // Clamp N to valid range (256..1024 samples -> 128..512 complex bins).
    if (N < 2 || N > kMaxSpectrumBins * 2) {
        return;
    }

    // Count bits needed for bit-reversal.
    uint32_t bits = 0;
    {
        uint32_t temp = halfN;
        while (temp > 1) {
            temp >>= 1;
            ++bits;
        }
    }

    // Step 1: Apply Hann window to the raw PCM samples.
    // w(n) = 0.5 * (1 - cos(2*pi*n / (N-1)))
    // This reduces spectral leakage at the cost of slightly wider main lobe.
    float windowed[kMaxSpectrumBins * 2] = {};
    const float invNm1 = (N > 1) ? 1.0f / static_cast<float>(N - 1) : 0.0f;
    for (uint32_t i = 0; i < N; ++i) {
        const float w = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i) * invNm1));
        windowed[i] = pcmBuffer_[i] * w;
    }

    // Step 2: Pack real samples into complex pairs for in-place FFT.
    // We treat pairs of real samples as (real, imag) of halfN complex numbers.
    // This is a standard optimization for real-valued FFT using half the work.
    // After the FFT we extract the real spectrum.
    //
    // complexBuf layout: [re0, im0, re1, im1, ..., re_{halfN-1}, im_{halfN-1}]
    float complexBuf[kMaxSpectrumBins * 2] = {};
    for (uint32_t i = 0; i < halfN; ++i) {
        complexBuf[2 * i]     = windowed[2 * i];     // real part
        complexBuf[2 * i + 1] = windowed[2 * i + 1]; // imag part
    }

    // Step 3: Bit-reversal permutation.
    for (uint32_t i = 0; i < halfN; ++i) {
        const uint32_t j = BitReverse(i, bits);
        if (i < j) {
            std::swap(complexBuf[2 * i],     complexBuf[2 * j]);
            std::swap(complexBuf[2 * i + 1], complexBuf[2 * j + 1]);
        }
    }

    // Step 4: Iterative Cooley-Tukey butterfly computation.
    for (uint32_t stage = 1; stage <= bits; ++stage) {
        const uint32_t m = 1u << stage;       // butterfly group size
        const uint32_t halfM = m / 2;
        const float angle = -kTwoPi / static_cast<float>(m);

        for (uint32_t k = 0; k < halfN; k += m) {
            for (uint32_t j2 = 0; j2 < halfM; ++j2) {
                const float theta = angle * static_cast<float>(j2);
                const float wRe = std::cos(theta);
                const float wIm = std::sin(theta);

                const uint32_t evenIdx = k + j2;
                const uint32_t oddIdx = k + j2 + halfM;

                const float tRe = wRe * complexBuf[2 * oddIdx]
                                - wIm * complexBuf[2 * oddIdx + 1];
                const float tIm = wRe * complexBuf[2 * oddIdx + 1]
                                + wIm * complexBuf[2 * oddIdx];

                const float eRe = complexBuf[2 * evenIdx];
                const float eIm = complexBuf[2 * evenIdx + 1];

                complexBuf[2 * evenIdx]     = eRe + tRe;
                complexBuf[2 * evenIdx + 1] = eIm + tIm;
                complexBuf[2 * oddIdx]      = eRe - tRe;
                complexBuf[2 * oddIdx + 1]  = eIm - tIm;
            }
        }
    }

    // Step 5: Compute magnitude spectrum (positive frequencies only).
    // mag[i] = sqrt(re^2 + im^2) * 2.0 / N
    // The factor 2/N normalizes the one-sided spectrum.
    const float normFactor = 2.0f / static_cast<float>(N);
    const uint32_t spectrumBins = config_.fftBands / 2;

    for (uint32_t i = 0; i < spectrumBins; ++i) {
        const float re = complexBuf[2 * i];
        const float im = complexBuf[2 * i + 1];
        const float mag = std::sqrt(re * re + im * im) * normFactor;

        // IIR smoothing: smoothed = lerp(smoothed, raw, alpha)
        // This prevents jarring frame-to-frame changes while still tracking
        // audio transients. Alpha=0.15 gives a good balance for music.
        const float smoothed = spectrumData_[i] + config_.smoothingAlpha * (mag - spectrumData_[i]);

        // Apply sensitivity scaling and clamp to [0, 1].
        spectrumData_[i] = std::clamp(smoothed * config_.sensitivity, 0.0f, 1.0f);
    }

    // Zero-pad remaining bins (for 256-band FFT, bins 128..511 are zero).
    for (uint32_t i = spectrumBins; i < kMaxSpectrumBins; ++i) {
        // Smoothly decay padded bins toward zero to avoid discontinuities
        // when switching between 256 and 512 band modes.
        spectrumData_[i] = spectrumData_[i] * (1.0f - config_.smoothingAlpha);
    }
}

// -- JSON deserialization -----------------------------------------------------

AudioSpectrumModuleConfig AudioSpectrumModule::ParseConfig(const nlohmann::json& j) {
    AudioSpectrumModuleConfig cfg;

    if (!j.is_object()) {
        UZ_LOG(VFX, Warning,
               "AudioSpectrumModule::ParseConfig: expected JSON object, got {}; "
               "using defaults",
               j.type_name());
        return cfg;
    }

    if (j.contains("fft_bands") && j["fft_bands"].is_number_integer()) {
        const uint32_t raw = j["fft_bands"].get<uint32_t>();
        // Only powers of two 256 and 512 are supported.
        if (raw == 256 || raw == 512) {
            cfg.fftBands = raw;
        } else {
            UZ_LOG(VFX, Warning,
                   "AudioSpectrumModule::ParseConfig: fft_bands {} is not 256 or 512; "
                   "defaulting to 256",
                   raw);
            cfg.fftBands = 256;
        }
    }

    if (j.contains("sensitivity") && j["sensitivity"].is_number()) {
        cfg.sensitivity = std::max(0.0f, j["sensitivity"].get<float>());
    }

    if (j.contains("smoothing_alpha") && j["smoothing_alpha"].is_number()) {
        cfg.smoothingAlpha = std::clamp(j["smoothing_alpha"].get<float>(), 0.01f, 1.0f);
    }

    if (j.contains("frequency_band") && j["frequency_band"].is_number_integer()) {
        cfg.frequencyBand = std::min(j["frequency_band"].get<uint32_t>(), 511u);
    }

    if (j.contains("beat_threshold") && j["beat_threshold"].is_number()) {
        cfg.beatThreshold = std::clamp(j["beat_threshold"].get<float>(), 0.0f, 1.0f);
    }

    if (j.contains("burst_on_beat") && j["burst_on_beat"].is_boolean()) {
        cfg.burstOnBeat = j["burst_on_beat"].get<bool>();
    }

    if (j.contains("burst_count") && j["burst_count"].is_number_integer()) {
        cfg.burstCount = std::max(1u, j["burst_count"].get<uint32_t>());
    }

    return cfg;
}

} // namespace uz
