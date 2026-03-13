# VFX Visual Feedback Loop — AI-Driven Iterative Refinement via Claude Vision

> **Status**: Planned (Phase 3)
> **Source**: `runtime/vfx-ai/visual_feedback.cpp/h`, `editor/vfx-ai-tool/visual_feedback.ts`
> **References**: Claude Vision API, stb_image_write JPEG encoding, frame buffer capture, IPC viewport streaming

## Purpose

After generating a VFX effect, the system captures multi-frame contact sheets and sends them to Claude Vision for visual quality analysis. Claude evaluates colors, timing, particle density, and overall polish, then suggests adjustments. The orchestrator applies changes and re-renders to verify improvement. This creates a human-free refinement loop: **generate → capture → analyze → improve → validate**.

VFX are temporal animations — a single frame is meaningless. A contact sheet (8 frames arranged in a grid) reveals the full effect lifecycle and enables Claude to provide intelligent feedback.

## Architecture

```
VFX Effect Generated
        │
        ▼
┌──────────────────────────────────┐
│  VFX Preview Renderer            │
│  ├── Render at origin            │
│  ├── Fixed camera (orthographic) │
│  ├── Black background (no noise) │
│  ├── 570×720 viewport            │
│  └── No UI overlay               │
└──────────┬───────────────────────┘
           │
           ▼
┌──────────────────────────────────┐
│  Frame Capture (8 frames)        │
│  ├── t=0.00s  → backbuffer copy  │
│  ├── t=0.28s  → backbuffer copy  │
│  ├── t=0.57s  → backbuffer copy  │
│  ├── ...                         │
│  └── t=2.00s  → backbuffer copy  │
└──────────┬───────────────────────┘
           │
           ▼
┌──────────────────────────────────┐
│  Contact Sheet Compositor        │
│  ├── Arrange 4×2 grid            │
│  ├── Add time stamps + frame #   │
│  ├── Output RGBA buffer          │
│  └── 2048×1292 final size        │
└──────────┬───────────────────────┘
           │
           ▼
┌──────────────────────────────────┐
│  JPEG Compression (quality 80)   │
│  ├── stb_image_write encoding    │
│  ├── ~1.8 MB output              │
│  └── Fits Claude Vision budget    │
└──────────┬───────────────────────┘
           │
           ▼
┌──────────────────────────────────┐
│  Claude Vision Analysis          │
│  ├── Structural prompt (JSON out)│
│  ├── Score: 0–10                 │
│  ├── Issues detected             │
│  ├── Suggested adjustments       │
│  └── Shader fix notes            │
└──────────┬───────────────────────┘
           │
     ┌─────┴──────┐
     │             │
  Score≥7?     Score<7?
     │             │
    DONE           ▼
     ✓    Apply adjustments
          Regenerate changed parts
               │
               ▼
          Re-render 8 frames
               │
               ▼
          Re-analyze (max 3 total)
```

## Frame Capture Strategy

### Evenly-Spaced Sampling

For an effect with lifetime **T_total**, capture at N evenly-spaced points:

```
T_frames = T_total / (N - 1)
t[i] = i × T_frames, for i ∈ [0, N-1]
```

**Example: 8 frames across 2.0s lifetime**
```
T_frames = 2.0 / 7 = 0.286s
t = [0.00, 0.29, 0.57, 0.86, 1.14, 1.43, 1.71, 2.00]
```

### Capture Implementation

```cpp
/// Capture N frames at specified time points across effect lifetime.
class VFXFrameCapture {
public:
    struct CaptureConfig {
        uint32_t num_frames = 8;           // 4-16 recommended
        float lifetime_seconds = 2.0f;     // Effect total duration
        uint32_t width = 570;
        uint32_t height = 720;
        bool label_frames = true;          // Add time + frame# overlay
    };

    /// Render effect and capture backbuffer at N time points.
    /// Returns RGBA8 frames suitable for grid composition.
    std::vector<Buffer> CaptureFrames(
        ResourceHandle<VFXGraph> effect,
        const CaptureConfig& config
    ) {
        std::vector<Buffer> frames;
        frames.reserve(config.num_frames);

        float dt = config.lifetime_seconds / (config.num_frames - 1);

        for (uint32_t i = 0; i < config.num_frames; ++i) {
            float t = i * dt;

            // Seek effect to time t
            effect->Seek(t);

            // Render one frame at this time point
            m_renderer->Render(effect, config.width, config.height);

            // Copy backbuffer to CPU memory
            Buffer frame_rgba = m_renderer->ReadBackbuffer();

            // Optional: add timestamp overlay
            if (config.label_frames) {
                AddTimeStampOverlay(frame_rgba, config.width, config.height, t, i);
            }

            frames.push_back(frame_rgba);

            UZ_LOG(VFX, Debug, "Captured frame {}/{} at t={:.2f}s",
                   i + 1, config.num_frames, t);
        }

        return frames;
    }

private:
    /// Render text overlay: "t=1.23s F5" in top-left corner.
    void AddTimeStampOverlay(
        Buffer& rgba_frame,
        uint32_t width,
        uint32_t height,
        float time_seconds,
        uint32_t frame_index
    ) {
        // Use simple text rasterization (or pre-rendered glyphs)
        std::string label = fmt::format("t={:.2f}s F{}", time_seconds, frame_index + 1);
        RasterTextToRGBA(rgba_frame, width, height, label,
                        10, 10,  // x, y position
                        {255, 255, 255, 255});  // white text
    }
};
```

## Contact Sheet Compositor

Arrange captured frames in a grid with minimal padding:

```cpp
/// Compose multiple frames into a contact sheet (sprite strip / grid).
class ContactSheetCompositor {
public:
    struct CompositeConfig {
        uint32_t grid_cols = 4;         // Columns
        uint32_t grid_rows = 2;         // Rows (total frames = cols × rows)
        uint32_t max_width = 2048;      // Claude's optimal width
        bool add_labels = true;         // Add frame numbers
    };

    /// Arrange frames into a grid, return composite RGBA buffer.
    /// Output dimensions computed to fit max_width.
    Buffer ComposeContactSheet(
        const std::vector<Buffer>& frames,
        uint32_t frame_width,
        uint32_t frame_height,
        const CompositeConfig& config
    ) {
        uint32_t grid_cols = config.grid_cols;
        uint32_t grid_rows = (frames.size() + grid_cols - 1) / grid_cols;

        // Compute output dimensions
        uint32_t total_width = grid_cols * frame_width;
        uint32_t total_height = grid_rows * frame_height;

        // Scale if exceeds max_width
        if (total_width > config.max_width) {
            float scale = static_cast<float>(config.max_width) / total_width;
            frame_width = static_cast<uint32_t>(frame_width * scale);
            frame_height = static_cast<uint32_t>(frame_height * scale);
            total_width = grid_cols * frame_width;
            total_height = grid_rows * frame_height;
        }

        // Allocate composite buffer (RGBA8)
        Buffer composite(total_width * total_height * 4, 0);

        // Paste each frame into grid
        for (size_t i = 0; i < frames.size(); ++i) {
            uint32_t col = i % grid_cols;
            uint32_t row = i / grid_cols;

            uint32_t paste_x = col * frame_width;
            uint32_t paste_y = row * frame_height;

            PasteFrame(composite, total_width, total_height,
                      frames[i], frame_width, frame_height,
                      paste_x, paste_y);

            UZ_LOG(VFX, Debug, "Pasted frame {} at grid ({}, {})",
                   i, col, row);
        }

        UZ_LOG(VFX, Info, "Contact sheet: {}×{} ({} frames, {:.1f} MB)",
               total_width, total_height, frames.size(),
               static_cast<float>(composite.size()) / (1024 * 1024));

        return composite;
    }

private:
    /// Paste source frame into composite at (paste_x, paste_y).
    /// Handles scaling and format conversion.
    void PasteFrame(
        Buffer& composite,
        uint32_t composite_w, uint32_t composite_h,
        const Buffer& src,
        uint32_t src_w, uint32_t src_h,
        uint32_t paste_x, uint32_t paste_y
    ) {
        const uint8_t* src_ptr = src.data();
        uint8_t* dst_ptr = composite.data();

        for (uint32_t y = 0; y < src_h; ++y) {
            uint32_t dst_y = paste_y + y;
            if (dst_y >= composite_h) break;

            for (uint32_t x = 0; x < src_w; ++x) {
                uint32_t dst_x = paste_x + x;
                if (dst_x >= composite_w) break;

                uint32_t src_idx = (y * src_w + x) * 4;
                uint32_t dst_idx = (dst_y * composite_w + dst_x) * 4;

                // Copy RGBA
                memcpy(&dst_ptr[dst_idx], &src_ptr[src_idx], 4);
            }
        }
    }
};
```

## JPEG Compression & Transmission

Compress contact sheet for transmission to Claude Vision:

```cpp
/// JPEG compression wrapper for VFX contact sheets.
class JPEGEncoder {
public:
    struct EncodeConfig {
        uint32_t quality = 80;      // 0–100, default 80
        bool optimize = true;       // stb_image_write optimization
    };

    /// Compress RGBA buffer to JPEG bytes.
    /// Uses stb_image_write.h for minimal dependencies.
    Buffer EncodeRGBAtoJPEG(
        const Buffer& rgba_data,
        uint32_t width,
        uint32_t height,
        const EncodeConfig& config
    ) {
        // stb_image_write callback: accumulates JPEG bytes
        std::vector<uint8_t> jpeg_bytes;

        auto write_fn = [](void* context, void* data, int size) {
            auto* out = static_cast<std::vector<uint8_t>*>(context);
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            out->insert(out->end(), bytes, bytes + size);
        };

        // Encode via stb
        int result = stbi_write_jpg_to_func(
            write_fn,
            &jpeg_bytes,
            width, height,
            4,  // 4 channels (RGBA)
            rgba_data.data(),
            config.quality
        );

        if (result == 0) {
            UZ_LOG(VFX, Error, "JPEG encoding failed");
            return Buffer();
        }

        UZ_LOG(VFX, Info, "JPEG compressed: {}×{} → {:.1f} KB at quality {}",
               width, height,
               static_cast<float>(jpeg_bytes.size()) / 1024,
               config.quality);

        return Buffer(jpeg_bytes.begin(), jpeg_bytes.end());
    }
};
```

## Claude Vision Analysis Prompt

Structured prompt that elicits actionable JSON feedback:

```json
{
  "role": "system",
  "content": "You are a VFX quality analyst for a game engine. Analyze particle effect contact sheets and provide structured feedback for improvement."
}
```

```json
{
  "role": "user",
  "content": [
    {
      "type": "text",
      "text": "Analyze this VFX particle effect rendered as a contact sheet.\n\nGrid Layout: 4 columns × 2 rows (8 frames)\nTime Range: 0.0s to 2.0s\nTarget Effect: campfire with rising embers and glowing logs\n\nFor each frame (left-to-right, top-to-bottom), evaluate:\n\n1. **COLOR**: Are the colors correct? Bright orange/red for fire, yellow glow for heat? Smooth gradients?\n2. **TIMING**: Does the effect evolve naturally? Spawn → rise → fade?\n3. **DENSITY**: Too many particles (visual clutter) or too few (sparse)?\n4. **SHAPE**: Silhouette matches \"campfire\"? Natural turbulence?\n5. **FADE**: Does it fade out smoothly, or cut abruptly?\n6. **QUALITY**: Any artifacts? Texture tiling visible? Wrong blend mode?\n\nProvide feedback as JSON:\n\n```json\n{\n  \"score\": <0-10 integer, 10=production quality>,\n  \"strengths\": [\"list\", \"of\", \"good\", \"aspects\"],\n  \"issues\": [\"list\", \"of\", \"problems\"],\n  \"adjustments\": {\n    \"spawn_rate\": \"<'+50%' or 'keep' or null>\",\n    \"lifetime\": \"<seconds or null>\",\n    \"color_start\": \"<[r,g,b,a] or null>\",\n    \"color_end\": \"<[r,g,b,a] or null>\",\n    \"velocity_up\": \"<units/s or null>\",\n    \"gravity\": \"<acceleration or null>\",\n    \"glow_intensity\": \"<multiplier or null>\",\n    \"damping\": \"<0-1 or null>\"\n  },\n  \"shader_fix\": \"<description of required shader change, or null>\",\n  \"texture_fix\": \"<description of required texture change, or null>\",\n  \"next_steps\": \"<brief guidance for iteration>\"\n}\n```\n\nReturn ONLY valid JSON, no markdown formatting."
    },
    {
      "type": "image",
      "source": {
        "type": "base64",
        "media_type": "image/jpeg",
        "data": "<base64-encoded JPEG>"
      }
    }
  ]
}
```

## Iteration Loop Control

```cpp
/// Orchestrate the visual feedback loop: render → analyze → improve → validate.
class VFXVisualFeedback {
public:
    struct FeedbackConfig {
        uint32_t max_iterations = 3;    // Max rounds of refinement
        float score_threshold = 7.0f;   // Minimum score to accept
        bool auto_apply = true;         // Auto-apply Claude's suggestions
    };

    struct IterationResult {
        bool success;
        float final_score;
        uint32_t iterations_used;
        std::vector<std::string> applied_adjustments;
    };

    /// Run visual feedback loop until score ≥ threshold or max iterations.
    IterationResult RefineEffect(
        ResourceHandle<VFXGraph> effect,
        StringView effect_description,
        const FeedbackConfig& config
    ) {
        IterationResult result{ true, 0, 0, {} };

        for (uint32_t iter = 1; iter <= config.max_iterations; ++iter) {
            UZ_LOG(VFX, Info, "Visual feedback iteration {}/{}",
                   iter, config.max_iterations);

            // 1. Capture frames
            auto frames = CaptureFrames(effect);

            // 2. Compose contact sheet
            auto composite = ComposeContactSheet(frames);

            // 3. Compress to JPEG
            auto jpeg = EncodeToJPEG(composite);

            // 4. Send to Claude Vision
            auto analysis = AnalyzeWithClaudeVision(jpeg, effect_description);

            result.final_score = analysis.score;
            result.iterations_used = iter;

            UZ_LOG(VFX, Info, "Iteration {} score: {:.1f}/10", iter, analysis.score);

            // 5. Check if done
            if (analysis.score >= config.score_threshold) {
                UZ_LOG(VFX, Info, "Score target reached after {} iterations", iter);
                return result;
            }

            // 6. Apply adjustments for next iteration
            if (iter < config.max_iterations && !analysis.adjustments.empty()) {
                ApplyAdjustments(effect, analysis.adjustments);
                result.applied_adjustments.push_back(
                    fmt::format("Iteration {}: {}", iter,
                    JoinAdjustments(analysis.adjustments))
                );
            }
        }

        UZ_LOG(VFX, Warning,
               "Max iterations ({}) reached, final score: {:.1f}/10",
               config.max_iterations, result.final_score);

        return result;
    }

private:
    struct ClaudeVisionAnalysis {
        float score;
        std::vector<std::string> strengths;
        std::vector<std::string> issues;
        std::unordered_map<std::string, std::string> adjustments;
        std::string shader_fix;
        std::string texture_fix;
    };

    /// Call Claude Vision API with contact sheet JPEG.
    ClaudeVisionAnalysis AnalyzeWithClaudeVision(
        const Buffer& jpeg_data,
        StringView effect_description
    ) {
        // Build request
        json request;
        request["model"] = "claude-3-5-sonnet-20241022";
        request["max_tokens"] = 1024;
        request["messages"] = json::array();

        std::string base64_jpeg = EncodeBase64(jpeg_data);

        json message;
        message["role"] = "user";
        message["content"] = json::array();

        // Text prompt
        json text_part;
        text_part["type"] = "text";
        text_part["text"] = BuildAnalysisPrompt(effect_description);
        message["content"].push_back(text_part);

        // Image part
        json image_part;
        image_part["type"] = "image";
        image_part["source"] = {
            { "type", "base64" },
            { "media_type", "image/jpeg" },
            { "data", base64_jpeg }
        };
        message["content"].push_back(image_part);

        request["messages"].push_back(message);

        // Call API
        std::string response = CallClaudeAPI(request.dump());

        // Parse response JSON
        auto result = ParseJSON<ClaudeVisionAnalysis>(response);
        return result;
    }

    /// Apply suggested adjustments to effect parameters.
    void ApplyAdjustments(
        ResourceHandle<VFXGraph> effect,
        const std::unordered_map<std::string, std::string>& adjustments
    ) {
        for (const auto& [key, value] : adjustments) {
            if (key == "spawn_rate") {
                float multiplier = ParseMultiplier(value);  // "+50%" → 1.5
                effect->SetSpawnRate(effect->GetSpawnRate() * multiplier);
            } else if (key == "lifetime") {
                float seconds = std::stof(value);
                effect->SetParticleLifetime(seconds);
            } else if (key == "color_start") {
                auto color = ParseColorArray(value);  // "[1, 0.5, 0, 1]"
                effect->SetColorStart(color);
            } else if (key == "glow_intensity") {
                float intensity = std::stof(value);
                effect->SetGlowIntensity(intensity);
            }
            // ... handle other parameters
        }
    }
};
```

## TypeScript Integration (Extension Side)

Display results in the VFX AI Tool panel:

```typescript
/// Visual feedback preview component (React/TSX).
export function VFXVisualFeedbackPanel(props: {
  effect: VFXEffect;
  contactSheetJPEG: Uint8Array;
  analysis: ClaudeVisionAnalysis;
  onApplyAdjustments: (adj: Record<string, any>) => void;
}) {
  const [iterating, setIterating] = useState(false);
  const [scoreHistory, setScoreHistory] = useState<number[]>([props.analysis.score]);

  return (
    <div className="vfx-feedback-panel">
      <h3>Visual Feedback Results</h3>

      {/* Contact Sheet Preview */}
      <div className="contact-sheet">
        <img
          src={`data:image/jpeg;base64,${btoa(String.fromCharCode(...props.contactSheetJPEG))}`}
          alt="Contact Sheet"
        />
      </div>

      {/* Score Display */}
      <div className="score-display">
        <div className="score-value">{props.analysis.score.toFixed(1)}/10</div>
        <div className="score-bar">
          <div className="score-fill" style={{ width: `${props.analysis.score * 10}%` }} />
        </div>
        {props.analysis.score >= 7 && <span className="badge-pass">✓ Approved</span>}
        {props.analysis.score < 7 && <span className="badge-needs-work">⚠ Needs Refinement</span>}
      </div>

      {/* Issues & Strengths */}
      <div className="analysis-details">
        <div className="strengths">
          <h4>Strengths</h4>
          <ul>
            {props.analysis.strengths.map((s, i) => (
              <li key={i}>{s}</li>
            ))}
          </ul>
        </div>

        <div className="issues">
          <h4>Issues to Fix</h4>
          <ul>
            {props.analysis.issues.map((issue, i) => (
              <li key={i}>{issue}</li>
            ))}
          </ul>
        </div>
      </div>

      {/* Suggested Adjustments */}
      {props.analysis.adjustments && Object.keys(props.analysis.adjustments).length > 0 && (
        <div className="adjustments">
          <h4>Suggested Adjustments</h4>
          {Object.entries(props.analysis.adjustments).map(([key, value]) => (
            <div key={key} className="adjustment-item">
              <span className="adjustment-name">{key}</span>
              <span className="adjustment-value">{value}</span>
            </div>
          ))}

          <button
            onClick={() => {
              setIterating(true);
              props.onApplyAdjustments(props.analysis.adjustments);
            }}
            disabled={iterating}
          >
            {iterating ? "Applying..." : "Apply & Re-evaluate"}
          </button>
        </div>
      )}

      {/* Score History */}
      <div className="iteration-history">
        <h4>Iteration History</h4>
        <div className="scores">{scoreHistory.map((s, i) => (
          <span key={i} className="score-badge">{(s).toFixed(1)}</span>
        ))}</div>
      </div>
    </div>
  );
}
```

## Configuration Reference

Runtime configuration for visual feedback:

```cpp
// vfx_config.h
namespace vfx {
    constexpr struct {
        // Preview viewport dimensions
        uint32_t preview_width = 570;
        uint32_t preview_height = 720;

        // Frame capture
        uint32_t capture_frames = 8;      // 4–16
        uint32_t capture_grid_cols = 4;
        uint32_t capture_max_width = 2048;

        // JPEG compression
        uint32_t jpeg_quality = 80;       // 60–95

        // Iteration control
        uint32_t max_iterations = 3;
        float score_threshold = 7.0f;     // Minimum to accept
        bool auto_apply_adjustments = true;

        // Cost limits
        float max_api_cost_usd = 0.15f;   // Per effect iteration
    } kVisualFeedbackConfig;
}
```

## Performance & Cost

| Metric | 4 Frames | 8 Frames | 12 Frames | 16 Frames |
|--------|----------|----------|-----------|-----------|
| **Grid Layout** | 4×1 | 4×2 | 6×2 | 6×3 |
| **Per-Frame Size** | 512×646 | 512×646 | 341×431 | 341×431 |
| **Total Composite** | 2048×646 | 2048×1292 | 2046×862 | 2046×1293 |
| **JPEG Size** | ~900 KB | ~1.8 MB | ~1.2 MB | ~1.8 MB |
| **Claude Tokens (in)** | ~4700 | ~9500 | ~9500 | ~9500 |
| **Claude Tokens (out)** | ~300 | ~300 | ~300 | ~300 |
| **API Cost (Sonnet)** | ~$0.014 | ~$0.029 | ~$0.029 | ~$0.029 |
| **Per-Iteration Time** | ~2–3s | ~2–3s | ~2–3s | ~2–3s |
| **3-Iteration Cost** | ~$0.04 | ~$0.09 | ~$0.09 | ~$0.09 |

**Recommended**: 8 frames in 4×2 grid — best balance of temporal coverage, image size, and cost.

## Related Documentation

- `docs/engine/33-vfx-ai-tool/01-overview.md` — VFX AI Tool overview
- `docs/engine/33-vfx-ai-tool/05-orchestrator.md` — Central orchestrator
- `docs/engine/25-vfx/` — VFX system, effect rendering
- `docs/engine/28-ipc-protocol/` — Extension ↔ Runtime IPC
- `docs/CLAUDE_VISION.md` — Claude Vision API integration patterns
