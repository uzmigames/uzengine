# Upscaling Integration — DLSS, FSR, and XeSS

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/graphics/upscaling/`, Nvidia DLSS SDK, AMD FSR SDK, Intel XeSS SDK
> **References**: DLSS 3.0+, FSR 3.0+, XeSS 1.0+, screen-space reconstruction

## Purpose

Upscaling technology renders at lower internal resolution and reconstructs to target resolution using AI or algorithmic reconstruction. This dramatically improves frame rates while maintaining visual quality. UzEngine abstracts over three competing upscaling technologies (NVIDIA DLSS, AMD FSR, Intel XeSS) so games can ship with a single codebase and automatically use the best available upscaler on each GPU.

## Architecture

```
┌──────────────────────────────────────┐
│  Game Code (Request Upscaling)       │
│  uz::Upscaler::Render(color, depth) │
└────────────┬─────────────────────────┘
             │
             ▼
┌──────────────────────────────────────────────────┐
│  Upscaling Manager (runtime abstraction)         │
│  ├─ Detect available upscalers on GPU            │
│  ├─ Initialize selected upscaler                 │
│  ├─ Queue render call                           │
│  └─ Manage motion vectors, depth format         │
└────────────┬─────────────────────────────────────┘
             │
             ├─────────────────┬──────────────┬─────────────────┐
             ▼                 ▼              ▼                 ▼
    ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
    │ DLSS Backend │  │ FSR Backend  │  │ XeSS Backend │  │ No Upscale   │
    │ (NVIDIA)     │  │ (AMD)        │  │ (Intel)      │  │ (Fallback)   │
    └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘
           │                 │                 │                 │
           └─────────────────┼─────────────────┼─────────────────┘
                             │
                             ▼
                    GPU Reconstruction
                    └─ Output: upscaled frame
```

## Upscaler Detection

```cpp
/// Detect available upscalers on this system.
class UpscalerDetector {
public:
    struct DetectionResult {
        bool has_dlss = false;              // NVIDIA GPU + DLSS SDK
        bool has_fsr = false;               // AMD GPU (any) + FSR SDK
        bool has_xess = false;              // Intel GPU + XeSS SDK
        std::string dlss_version;           // e.g., "3.0.0"
        std::string fsr_version;            // e.g., "3.0"
        std::string xess_version;           // e.g., "1.0"
    };

    /// Query available upscalers.
    static DetectionResult DetectUpscalers(ID3D12Device* device) {
        DetectionResult result;

        // Check DLSS availability
        #if UZ_HAS_DLSS
        if (DLSS_CheckFeatures(device)) {
            result.has_dlss = true;
            result.dlss_version = DLSS_GetVersion();
        }
        #endif

        // Check FSR availability (works on any GPU)
        #if UZ_HAS_FSR
        result.has_fsr = true;  // Always available if SDK included
        result.fsr_version = FSR_GetVersion();
        #endif

        // Check XeSS availability
        #if UZ_HAS_XESS
        if (XeSS_CheckFeatures(device)) {
            result.has_xess = true;
            result.xess_version = XeSS_GetVersion();
        }
        #endif

        return result;
    }

    /// Return recommended upscaler for this GPU.
    static UpscalerType RecommendUpscaler(const DetectionResult& detection) {
        // Priority: DLSS (best quality) > XeSS > FSR > None
        if (detection.has_dlss) return UpscalerType::DLSS;
        if (detection.has_xess) return UpscalerType::XeSS;
        if (detection.has_fsr) return UpscalerType::FSR;
        return UpscalerType::None;
    }
};
```

## Upscaler Interface

```cpp
/// Base interface for all upscalers (DLSS, FSR, XeSS).
class IUpscaler {
public:
    virtual ~IUpscaler() = default;

    /// Initialize upscaler with target output resolution.
    virtual bool Initialize(
        uint32_t output_width,
        uint32_t output_height,
        const UpscalerSettings& settings
    ) = 0;

    /// Render upscaled frame.
    /// Inputs: color (input resolution), depth, motion vectors (optional).
    /// Output: upscaled color buffer.
    virtual void Render(
        ID3D12GraphicsCommandList* cmd_list,
        GPUResource input_color,
        GPUResource input_depth,
        GPUResource input_motion,        // May be nullptr
        GPUResource output_color,
        float jitter_x = 0.0f,           // Temporal jitter (for DLSS)
        float jitter_y = 0.0f
    ) = 0;

    /// Get input resolution that upscaler expects.
    uint32_t GetInputWidth() const = 0;
    uint32_t GetInputHeight() const = 0;

    /// Get output resolution (as set in Initialize).
    uint32_t GetOutputWidth() const = 0;
    uint32_t GetOutputHeight() const = 0;

    /// Shutdown upscaler and free resources.
    virtual void Shutdown() = 0;

    /// Query feature support (e.g., does DLSS support DLSS SR on this GPU?).
    virtual bool SupportsFeature(StringView feature) const = 0;

    /// Get human-readable name (e.g., "NVIDIA DLSS 3.0").
    virtual StringView GetName() const = 0;
};

/// Settings controlling upscaler behavior.
struct UpscalerSettings {
    UpscalerType type = UpscalerType::None;
    QualityMode quality = QualityMode::Balanced;  // Performance, Balanced, Quality, Ultra
    bool motion_vectors = true;
    bool auto_exposure = false;
    float sharpness = 0.0f;                      // -1.0 (blurry) to 1.0 (sharp)
    bool jitter_enabled = true;                  // For temporal reconstruction
};

enum class QualityMode {
    Performance,    // 1.3x–1.5x upscale, ~1.3x fps boost
    Balanced,       // 1.5x–1.7x upscale, ~1.5x fps boost
    Quality,        // 1.7x–2.0x upscale, ~1.7x fps boost
    Ultra           // 2.0x upscale, ~2.0x fps boost (DLSS only)
};
```

## DLSS Backend

NVIDIA DLSS uses AI super-resolution. DLSS 3.0+ also includes Frame Generation (non-AI temporal upscaling).

```cpp
/// NVIDIA DLSS upscaler implementation.
class DLSSUpscaler : public IUpscaler {
public:
    bool Initialize(
        uint32_t output_width,
        uint32_t output_height,
        const UpscalerSettings& settings
    ) override;

    void Render(
        ID3D12GraphicsCommandList* cmd_list,
        GPUResource input_color,
        GPUResource input_depth,
        GPUResource input_motion,
        GPUResource output_color,
        float jitter_x = 0.0f,
        float jitter_y = 0.0f
    ) override;

    uint32_t GetInputWidth() const override;
    uint32_t GetInputHeight() const override;
    uint32_t GetOutputWidth() const override { return output_width_; }
    uint32_t GetOutputHeight() const override { return output_height_; }

    void Shutdown() override;
    bool SupportsFeature(StringView feature) const override;
    StringView GetName() const override { return "NVIDIA DLSS 3.0"; }

private:
    // DLSS context (opaque handle from SDK)
    NVSDK_NGX_Handle* dlss_context_ = nullptr;

    uint32_t output_width_ = 0, output_height_ = 0;
    QualityMode quality_ = QualityMode::Balanced;
    bool motion_vectors_enabled_ = true;

    // Map UzEngine quality modes to DLSS modes
    NVSDK_NGX_DLSS_PerfQuality_Value MapQualityMode(QualityMode mode) {
        switch (mode) {
            case QualityMode::Performance: return NVSDK_NGX_DLSS_PerfQuality_MaxPerf;
            case QualityMode::Balanced: return NVSDK_NGX_DLSS_PerfQuality_Balanced;
            case QualityMode::Quality: return NVSDK_NGX_DLSS_PerfQuality_MaxQuality;
            case QualityMode::Ultra: return NVSDK_NGX_DLSS_PerfQuality_UltraPerf;  // Frame Gen
            default: return NVSDK_NGX_DLSS_PerfQuality_Balanced;
        }
    }
};

/// Implementation excerpt:
bool DLSSUpscaler::Initialize(uint32_t out_w, uint32_t out_h, const UpscalerSettings& s) {
    output_width_ = out_w;
    output_height_ = out_h;
    quality_ = s.quality;

    // Initialize DLSS feature via NGX
    NVSDK_NGX_Result result = NVSDK_NGX_D3D12_CreateDLSSFeature(
        /* device, cmd_list, ... */,
        MapQualityMode(quality_),
        &dlss_context_
    );

    if (NVSDK_NGX_SUCCEED(result)) {
        UZ_LOG(Graphics, Info, "DLSS initialized: {}x{} → {}x{}",
            GetInputWidth(), GetInputHeight(), out_w, out_h);
        return true;
    }

    UZ_LOG(Graphics, Error, "Failed to initialize DLSS: {}", result);
    return false;
}
```

## FSR Backend

AMD FidelityFX Super Resolution works on any GPU and is royalty-free.

```cpp
/// AMD FSR upscaler implementation.
class FSRUpscaler : public IUpscaler {
public:
    bool Initialize(
        uint32_t output_width,
        uint32_t output_height,
        const UpscalerSettings& settings
    ) override;

    void Render(
        ID3D12GraphicsCommandList* cmd_list,
        GPUResource input_color,
        GPUResource input_depth,
        GPUResource input_motion,
        GPUResource output_color,
        float jitter_x = 0.0f,
        float jitter_y = 0.0f
    ) override;

    // ... implementation ...

    StringView GetName() const override { return "AMD FidelityFX Super Resolution 3.0"; }

private:
    FfxFsr3Context fsr_context_ = {};
    uint32_t output_width_ = 0, output_height_ = 0;

    FfxFsr3QualityMode MapQualityMode(QualityMode mode) {
        switch (mode) {
            case QualityMode::Performance: return FFX_FSR3_QUALITY_MODE_PERFORMANCE;
            case QualityMode::Balanced: return FFX_FSR3_QUALITY_MODE_BALANCED;
            case QualityMode::Quality: return FFX_FSR3_QUALITY_MODE_QUALITY;
            case QualityMode::Ultra: return FFX_FSR3_QUALITY_MODE_QUALITY;  // Same as quality
            default: return FFX_FSR3_QUALITY_MODE_BALANCED;
        }
    }
};
```

## XeSS Backend

Intel's Super Sampling technology for Arc and Xe-HPG GPUs.

```cpp
/// Intel XeSS upscaler implementation.
class XeSSUpscaler : public IUpscaler {
public:
    bool Initialize(
        uint32_t output_width,
        uint32_t output_height,
        const UpscalerSettings& settings
    ) override;

    void Render(
        ID3D12GraphicsCommandList* cmd_list,
        GPUResource input_color,
        GPUResource input_depth,
        GPUResource input_motion,
        GPUResource output_color,
        float jitter_x = 0.0f,
        float jitter_y = 0.0f
    ) override;

    // ... implementation ...

    StringView GetName() const override { return "Intel XeSS"; }

private:
    xess::XeSSHandle xess_context_ = nullptr;
};
```

## Motion Vectors

Critical for temporal reconstruction quality. Game must compute and provide motion vectors:

```cpp
/// Motion vector computation (velocity in screen space).
/// Called once per frame before upscaling.
void RenderMotionVectors(
    ID3D12GraphicsCommandList* cmd_list,
    const Camera& camera,
    const Camera& prev_camera,
    GPUResource motion_output
) {
    // For each pixel:
    // motion = (current_proj_pos - prev_proj_pos) in screen space
    // Store in RG16F texture

    // Pseudo-shader:
    // float4 prev_pos = mul(world_pos, prev_view_proj);
    // float4 curr_pos = mul(world_pos, curr_view_proj);
    // float2 motion = (curr_pos.xy - prev_pos.xy) / curr_pos.w;
    // motionTexture[px] = motion;
}
```

## Upscaling Manager (Central)

```cpp
/// High-level upscaling manager (wraps DLSS/FSR/XeSS).
class UpscalingManager {
public:
    /// Initialize upscaling system.
    /// Detects available upscalers, picks best one.
    bool Initialize(ID3D12Device* device, uint32_t display_width, uint32_t display_height);

    /// Get available upscalers on this GPU.
    std::vector<UpscalerType> GetAvailableUpscalers() const;

    /// Switch to different upscaler (e.g., user changes setting).
    bool SetActiveUpscaler(UpscalerType type, const UpscalerSettings& settings);

    /// Get current upscaler.
    UpscalerType GetActiveUpscaler() const { return active_upscaler_; }

    /// Render upscaled frame (single call, abstracts DLSS/FSR/XeSS).
    void Render(
        ID3D12GraphicsCommandList* cmd_list,
        const RenderFrameInput& frame_input
    );

    /// Get recommended render resolution for given quality mode.
    uint32_t GetRecommendedRenderWidth(QualityMode quality) const;
    uint32_t GetRecommendedRenderHeight(QualityMode quality) const;

    /// Get quality/performance tiers with descriptions.
    std::vector<QualityTier> GetQualityTiers() const;

    struct RenderFrameInput {
        GPUResource color_input;
        GPUResource depth_input;
        GPUResource motion_input;        // Optional
        GPUResource color_output;        // Upscaled target
        float temporal_jitter_x = 0.0f;
        float temporal_jitter_y = 0.0f;
    };

    struct QualityTier {
        QualityMode mode;
        uint32_t render_width, render_height;
        uint32_t display_width, display_height;
        float scale_ratio;                // (render_w * render_h) / (display_w * display_h)
        StringView description;           // "Performance (60 FPS)", "Balanced (90 FPS)", etc.
        float estimated_fps_improvement;  // e.g., 1.5x faster
    };

private:
    UpscalerType active_upscaler_ = UpscalerType::None;
    std::unique_ptr<IUpscaler> upscaler_;
    uint32_t display_width_ = 0, display_height_ = 0;

    // Map quality modes to recommended render ratios
    std::map<QualityMode, float> quality_scale_ratios_ = {
        { QualityMode::Performance, 0.67f },  // ~1.5x upscale
        { QualityMode::Balanced, 0.60f },     // ~1.67x upscale
        { QualityMode::Quality, 0.50f },      // 2.0x upscale
        { QualityMode::Ultra, 0.50f },        // 2.0x upscale
    };
};
```

## Usage Example

```cpp
// In graphics initialization:
uz::UpscalingManager upscaler;
upscaler.Initialize(device, 1920, 1080);

// Query available options
auto available = upscaler.GetAvailableUpscalers();
UZ_LOG(Graphics, Info, "Available upscalers: {}", available.size());

// Get quality tiers
auto tiers = upscaler.GetQualityTiers();
for (const auto& tier : tiers) {
    UZ_LOG(Graphics, Info, "{}: {}x{} ({}x scale, {:.1f}x fps)",
        tier.description,
        tier.render_width, tier.render_height,
        tier.scale_ratio,
        tier.estimated_fps_improvement);
}

// Render loop:
void UpdateGraphics(float dt) {
    // Render scene at reduced resolution
    uint32_t render_w = upscaler.GetRecommendedRenderWidth(current_quality_mode);
    uint32_t render_h = upscaler.GetRecommendedRenderHeight(current_quality_mode);
    RenderScene(render_w, render_h, color_rtv, depth_rtv);

    // Compute motion vectors
    RenderMotionVectors(cmd_list, motion_output);

    // Upscale to display resolution
    uz::UpscalingManager::RenderFrameInput input = {
        .color_input = color_rtv,
        .depth_input = depth_rtv,
        .motion_input = motion_output,
        .color_output = backbuffer,
    };
    upscaler.Render(cmd_list, input);
}
```

## Settings UI Integration

```lua
-- Lua settings menu
local upscaler_menu = {
    {
        name = "Upscaling",
        type = "category",
        children = {
            {
                name = "Upscaler",
                type = "dropdown",
                options = { "Off", "DLSS", "FSR", "XeSS" },
                default = "DLSS",
                on_change = function(value)
                    ctx.graphics:set_upscaler(value:lower())
                end
            },
            {
                name = "Quality Mode",
                type = "dropdown",
                options = { "Performance", "Balanced", "Quality" },
                default = "Balanced",
                on_change = function(value)
                    ctx.graphics:set_upscaler_quality(value:lower())
                end
            },
            {
                name = "Sharpness",
                type = "slider",
                min = -1.0, max = 1.0, step = 0.1,
                default = 0.0,
                on_change = function(value)
                    ctx.graphics:set_upscaler_sharpness(value)
                end
            }
        }
    }
}
```

## Comparison

| Feature | DLSS 3.0 | FSR 3.0 | XeSS 1.0 |
|---------|----------|---------|----------|
| **Upscale quality** | Best (AI) | Very good | Good |
| **GPU support** | NVIDIA only (RTX 20+) | Any GPU | Intel Arc + iGPU only |
| **Performance** | 1.3x–2.0x | 1.3x–2.0x | 1.3x–1.7x |
| **Latency overhead** | <1ms | <1ms | 2–3ms |
| **Motion vectors required** | Recommended | Recommended | Required |
| **Temporal jitter** | Yes | Yes | Yes |
| **Frame Generation** | Yes (DLSS 3) | Via AMD GPUs | No |
| **Cost** | Free (with RTX) | Free | Free |
| **SDK maturity** | Mature | Mature | Early |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | DLSS + FSR backends, UpscalingManager, quality mode switching, motion vector support, settings UI |
| **Phase 4** | XeSS backend, Frame Generation (DLSS 3), adaptive quality (frame-time based), seasonal upscaler benchmark comparisons |
