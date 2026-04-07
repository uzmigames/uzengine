# Phase 4b — 4.17.0 VFX Audio Spectrum DI

Music-reactive particles driven by real-time FFT audio analysis. The engine
captures audio samples, computes an FFT (256 or 512 bands), uploads the
spectrum as a 1D texture each frame, and the particle update CS samples it to
modulate particle size, color, and velocity magnitude per frequency band.

---

## 1. CPU-Side FFT Pipeline

- [ ] 1.1 Create `runtime/engine/vfx/modules/audio_spectrum_module.h/cpp`.
      Fields:
      ```cpp
      uint32_t fftBands        = 256;    // 256 or 512
      float    sensitivity     = 1.0f;   // linear multiplier on spectrum values
      float    smoothingAlpha  = 0.15f;  // IIR smoothing: s = lerp(s, raw, alpha)
      uint32_t frequencyBand   = 64;     // which band index to expose as "beat"
      float    beatThreshold   = 0.6f;   // normalized amplitude trigger for beat
      ```
- [ ] 1.2 Implement `AudioSpectrumModule::CaptureFFT()` called once per frame:
      - Call `IAudioEngine::GetPCMSamples(float* buf, uint32_t count)` to
        capture `fftBands * 2` samples from the current audio output mix.
      - Apply a Hann window: `buf[i] *= 0.5f * (1.0f - cosf(2*PI*i/(N-1)))`.
      - Compute the FFT using a power-of-two Cooley-Tukey implementation
        (iterative, in-place, real-valued input → complex output). Reference:
        @ref Cooley & Tukey 1965 "An Algorithm for the Machine Calculation of
        Complex Fourier Series".
      - Compute magnitude: `mag[i] = sqrtf(re^2 + im^2) * 2.0f / N`, for
        `i = 0 .. fftBands/2 - 1` (positive frequencies only).
      - Apply IIR smoothing: `smoothed[i] = lerp(smoothed[i], mag[i], smoothingAlpha)`.
      - Scale by `sensitivity` and clamp to `[0, 1]`.
- [ ] 1.3 Store the smoothed spectrum in `float spectrumData[512]` (CPU-side,
      zero-padded if `fftBands = 256`).

## 2. GPU 1D Spectrum Texture

- [ ] 2.1 Allocate a `D3D12_RESOURCE_DIMENSION_TEXTURE1D` with:
      - Width = 512 (always 512, zero-padded for 256-band FFT)
      - Format = `DXGI_FORMAT_R32_FLOAT`
      - Usage = `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE`
      - Update mode: mapped upload buffer, copy each frame.
- [ ] 2.2 Register bindless SRV: `spectrumSrvIndex = device->RegisterTexture1DSRV(spectrumTex)`.
- [ ] 2.3 Each frame, in `AudioSpectrumModule::UploadSpectrum(IRHIDevice* device)`:
      map the upload buffer, `memcpy(spectrumData, 512 * sizeof(float))`, unmap,
      then `CopyBufferToTexture1D()` within the render graph before the VFX update pass.

## 3. Update CS Sampling

- [ ] 3.1 In `particle_update_cs.hlsl`, add (guarded by `#define USE_AUDIO_SPECTRUM 1`):
      ```hlsl
      Texture1D<float>  g_spectrum      : register(t10);
      SamplerState      g_spectrumSamp  : register(s3); // linear clamp
      cbuffer AudioSpectrumCB : register(b5) {
          uint  frequencyBand;   // 0..511 — which band index
          float sensitivityMul;  // additional per-emitter scale
          uint  modulateMask;    // bit 0=size, bit 1=colorR, bit 2=velocity
          float _pad;
      };
      ```
- [ ] 3.2 In the update thread body, after standard forces:
      ```hlsl
      #if USE_AUDIO_SPECTRUM
          float uv = (float)frequencyBand / 511.0;
          float amp = g_spectrum.SampleLevel(g_spectrumSamp, uv, 0) * sensitivityMul;
          if (modulateMask & 1) size  *= (1.0 + amp * 2.0);
          if (modulateMask & 2) color.r = saturate(color.r + amp);
          if (modulateMask & 4) vel   *= (1.0 + amp);
      #endif
      ```
- [ ] 3.3 Bind `g_spectrum` SRV at `t10` in the VFX system's update dispatch helper.

## 4. Beat Detection

- [ ] 4.1 In `AudioSpectrumModule::Tick(float dt)`, after smoothing:
      ```cpp
      beatTriggered = spectrumData[frequencyBand] >= beatThreshold;
      ```
- [ ] 4.2 Add `bool beatTriggered` (read-only, updated by `Tick`) to the module.
      Expose via `IsBeat() const noexcept`.
- [ ] 4.3 Allow sub-emitter bursts on beat: if `beatTriggered` and
      `burstOnBeat == true`, call `emitter.RequestBurst(burstCount)` in `Tick`.
      Fields: `bool burstOnBeat = false; uint32_t burstCount = 50;`.

## 5. Audio Engine Integration

- [ ] 5.1 Add to `IAudioEngine`:
      ```cpp
      /// Captures the most recent \p sampleCount mixed PCM samples (mono, float32).
      /// Returns false if audio is not running or fewer samples are available.
      virtual bool GetPCMSamples(float* buffer, uint32_t sampleCount) noexcept = 0;
      ```
- [ ] 5.2 In `NullAudioEngine`, return `false` (silence — spectrum remains zeroed).
- [ ] 5.3 In the real audio backend, copy from the output ring buffer.
      (If no real audio backend exists yet, the `NullAudioEngine` path ensures
      the module silently degrades — not an error.)

## 6. Demo Integration

- [ ] 6.1 In `vfx_viewport_demo.cpp`, add preset "Music Reactive":
      - 512 particles, lifetime = 3s, `frequencyBand = 64` (kick-drum range),
        `sensitivityMul = 1.5`, `modulateMask = 0b111` (size+color+velocity).
      - Since real audio may not be available in the demo, generate a synthetic
        spectrum: `spectrumData[i] = 0.5f + 0.5f * sinf(time * 2.0f * PI / 2.0f)`
        for `i = 60..68` (simulates a 2-second beat cycle).
- [ ] 6.2 Add ImGui controls: `frequencyBand` slider (0–511), `sensitivity` (0–5),
      `smoothingAlpha` (0.01–1.0), `modulateMask` checkboxes (Size / ColorR / Velocity).
- [ ] 6.3 Render a mini spectrum bar graph in the ImGui panel using
      `ImGui::PlotHistogram("Spectrum", spectrumData, 256)`.

## 7. Build and Verification

- [ ] 7.1 `cd runtime && zig build --clean && zig build` — no errors.
- [ ] 7.2 `cd demos/vfx_viewport && zig build --clean && zig build` — no errors.
- [ ] 7.3 Capture `--screenshot-stdout --screenshot-frame 20 --frames 25`
      (synthetic beat at frame ~20): verify visible size pulse on particles
      in the "Music Reactive" preset.
- [ ] 7.4 Confirm spectrum stays in `[0, 1]` (no NaN from zero-amplitude FFT).

## 8. Documentation

- [ ] 8.1 Add to `audio_spectrum_module.h`:
      ```cpp
      /// @ref Cooley & Tukey 1965 "An Algorithm for the Machine Calculation
      ///      of Complex Fourier Series" — iterative Cooley-Tukey FFT
      /// @ref Harris 1978 "On the Use of Windows for Harmonic Analysis with
      ///      the Discrete Fourier Transform" — Hann window choice
      ```
- [ ] 8.2 Document `smoothingAlpha` semantics: typical values 0.05–0.3;
      lower = more smoothing (slower response), higher = snappier.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
